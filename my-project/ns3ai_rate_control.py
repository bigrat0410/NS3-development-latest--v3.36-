#!/usr/bin/env python3
"""Python 侧 ns3-ai 驱动程序。

这个脚本不是单独训练离线模型，而是和 ns-3 仿真进程同步运行：
1. Python 先创建 ns3-ai 共享内存接口；
2. 再启动 C++ 仿真可执行文件；
3. C++ 每到速率决策点写入 env 并等待；
4. Python 读取 env，调用 MAB/DQN 选择 action，再把 action 写回 C++。
"""

import argparse
import csv
import os
import signal
import subprocess
import sys
import traceback

import my_project_rate_control_py as py_binding

from dqn_agent import DqnAgent
from mab_agent import MabAgent


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
EXECUTABLE = os.path.join(
    ROOT, "build", "scratch", "my-project", "ns3.36.1-my-project-rate-control-default"
)


def make_agent(name, num_actions):
    """根据命令行参数创建学习器。

    num_actions 初始给 8，对应 802.11n 单流 HtMcs0..HtMcs7；实际运行时如果
    C++ 上报的 env.numActions 不同，agent 会在 select_action 内动态修正。
    """
    if name == "dqn":
        return DqnAgent(num_actions=num_actions)
    if name == "mab":
        return MabAgent(num_actions=num_actions)
    raise ValueError(f"Python/ns3ai mode supports dqn or mab, got {name}")


def run_ns3(args):
    """启动 ns-3 C++ 仿真进程。

    Python 必须先创建共享内存，再启动 C++。C++ 侧构造速率管理器时会连接这块
    共享内存；如果顺序反过来，C++ 可能找不到接口或阻塞。
    """
    cmd = [
        EXECUTABLE,
        f"--algorithm={args.algorithm}",
        f"--simulationTime={args.simulation_time}",
        f"--fastMobility={'true' if args.fast_mobility else 'false'}",
        f"--Seed={args.seed}",
        f"--Run={args.run}",
        f"--decisionInterval={args.decision_interval}",
        f"--linearMobility={'true' if args.linear_mobility else 'false'}",
        f"--startDistance={args.start_distance}",
        f"--mobilitySpeed={args.mobility_speed}",
        f"--sampleInterval={args.sample_interval}",
    ]
    if args.convergence_start is not None:
        cmd.append(f"--convergenceStart={args.convergence_start}")
    if args.max_distance is not None:
        cmd.append(f"--maxDistance={args.max_distance}")
    if args.min_distance is not None:
        cmd.append(f"--minDistance={args.min_distance}")
    if args.mobility_period is not None:
        cmd.append(f"--mobilityPeriod={args.mobility_period}")

    env = os.environ.copy()
    # ns-3 的动态库位于 build/lib，子进程需要 LD_LIBRARY_PATH 才能加载 libai/libwifi 等库。
    env["LD_LIBRARY_PATH"] = os.path.join(ROOT, "build", "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
    return subprocess.Popen(
        cmd,
        cwd=ROOT,
        env=env,
        text=True,
        stdout=None if args.show_ns3_output else subprocess.PIPE,
        stderr=None if args.show_ns3_output else subprocess.PIPE,
        # 单独进程组便于 finally 中一次性终止 ns-3 及其可能派生的子进程。
        preexec_fn=os.setpgrp,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--algorithm", choices=["dqn", "mab"], default="dqn")
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=50.0)
    parser.add_argument("--fastMobility", dest="fast_mobility", action="store_true")
    parser.add_argument("--no-fastMobility", dest="fast_mobility", action="store_false")
    parser.set_defaults(fast_mobility=True)
    parser.add_argument("--Seed", dest="seed", type=int, default=1)
    parser.add_argument("--Run", dest="run", type=int, default=31)
    parser.add_argument("--convergenceStart", dest="convergence_start", type=float, default=None)
    parser.add_argument("--minDistance", dest="min_distance", type=float, default=None)
    parser.add_argument("--maxDistance", dest="max_distance", type=float, default=None)
    parser.add_argument("--mobilityPeriod", dest="mobility_period", type=float, default=None)
    parser.add_argument("--decisionInterval", dest="decision_interval", type=float, default=0.001)
    parser.add_argument("--linearMobility", dest="linear_mobility", action="store_true")
    parser.add_argument("--no-linearMobility", dest="linear_mobility", action="store_false")
    parser.set_defaults(linear_mobility=False)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=0.0)
    parser.add_argument("--mobilitySpeed", dest="mobility_speed", type=float, default=0.5)
    parser.add_argument("--sampleInterval", dest="sample_interval", type=float, default=1.0)
    parser.add_argument("--showNs3Output", dest="show_ns3_output", action="store_true")
    args = parser.parse_args()

    os.makedirs(os.path.join(ROOT, "my-project-results"), exist_ok=True)
    log_path = os.path.join(ROOT, "my-project-results", f"python-agent-{args.algorithm}.csv")

    # 这里的字符串名字必须和 C++/pybind 侧使用的消息布局一致。
    # 第一个 True 表示 Python 创建共享内存；C++ 构造函数里设置为非创建者。
    msg_interface = py_binding.Ns3AiMsgInterfaceImpl(
        True,
        False,
        True,
        4096,
        "My Seg",
        "My Cpp to Python Msg",
        "My Python to Cpp Msg",
        "My Lockable",
    )
    proc = run_ns3(args)
    agent = make_agent(args.algorithm, 8)

    try:
        with open(log_path, "w", newline="") as log_file:
            writer = csv.writer(log_file)
            writer.writerow(
                [
                    "step",
                    "algorithm",
                    "snr_db",
                    "ack_ratio",
                    "reward",
                    "last_action",
                    "next_action",
                    "num_actions",
                    "epsilon",
                ]
            )
            while True:
                # PyRecvBegin 等待 C++ 写入 env；PySendBegin 锁定 Python -> C++ 的动作缓冲区。
                msg_interface.PyRecvBegin()
                msg_interface.PySendBegin()
                if msg_interface.PyGetFinished():
                    break

                env = msg_interface.GetCpp2PyStruct()
                if args.algorithm == "mab":
                    # MAB 是表格式方法，需要先把上一轮 reward 记录到对应 SNR 桶和 action。
                    agent.observe(env)
                action, epsilon = agent.select_action(env)
                # 再做一次 Python 侧裁剪；C++ 侧也会裁剪，这是为了日志和动作结构体保持合理。
                action = max(1, min(int(action), max(1, int(env.numActions))))

                act = msg_interface.GetPy2CppStruct()
                act.nextAction = action
                act.nextMcs = action - 1
                act.epsilon = float(epsilon)
                writer.writerow(
                    [
                        env.step,
                        args.algorithm,
                        env.snrDb,
                        env.ackRatio,
                        env.reward,
                        env.lastAction,
                        action,
                        env.numActions,
                        epsilon,
                    ]
                )
                # End 调用释放两侧锁。顺序要和 Begin 配对，否则 C++ 会一直等待。
                msg_interface.PyRecvEnd()
                msg_interface.PySendEnd()

        stdout, stderr = proc.communicate(timeout=5)
        if stdout:
            print(stdout, end="")
        if stderr:
            print(stderr, end="", file=sys.stderr)
        return proc.returncode or 0
    except Exception:
        traceback.print_exc()
        return 1
    finally:
        if proc.poll() is None:
            # 异常退出时停止 C++ 仿真，避免遗留进程占用共享内存锁。
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        del msg_interface


if __name__ == "__main__":
    raise SystemExit(main())
