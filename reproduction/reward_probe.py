#!/usr/bin/env python3

import argparse
import math
import os
import signal
import subprocess
import sys
from pathlib import Path

import reproduction_reinrate_py


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "build/scratch/reproduction/ns3.36.1-two-node-ht-default"


#原REINRATE奖励函数使用的单空间流吞吐参考表；当前先原样保留用于核对
REFERENCE_THROUGHPUT_MBPS = {
    20: [5.4, 9.8, 13.6, 17.5, 23.0, 26.1, 28.1, 29.6],
    40: [13.5, 27.0, 40.5, 54.0, 81.0, 108.0, 121.5, 135.0],
}


class SourceSnrPreprocessor:
    """复现源码推理路径：log(snr+1)、最近5次平滑和简单离群值删除。"""

    def __init__(self):
        self.pool = []

    def update(self, linear_snr):
        safe_snr = linear_snr if math.isfinite(linear_snr) and linear_snr > -1 else 0.0
        self.pool.append(math.log(safe_snr + 1.0))
        if len(self.pool) > 5:
            self.pool.pop(0)

        if len(self.pool) == 5:
            mean = sum(self.pool) / len(self.pool)
            variance = sum((value - mean) ** 2 for value in self.pool) / len(self.pool)
            standard_deviation = math.sqrt(variance)
            maximum = max(self.pool)
            minimum = min(self.pool)
            if abs(maximum - mean) > 1.5 * standard_deviation:
                self.pool.remove(maximum)
            elif abs(minimum - mean) > 1.5 * standard_deviation:
                self.pool.remove(minimum)

        return sum(self.pool) / len(self.pool)


def calculate_reward(achieved_throughput, mcs_index, channel_bandwidth):
    """按REINRATE源码公式计算奖励，不裁剪，便于发现公式与场景不匹配。"""
    if channel_bandwidth not in REFERENCE_THROUGHPUT_MBPS:
        raise ValueError("channel_bandwidth must be 20 or 40 MHz")
    if not 0 <= mcs_index <= 7:
        raise ValueError("mcs_index must be in [0, 7]")

    reference = REFERENCE_THROUGHPUT_MBPS[channel_bandwidth][mcs_index]
    safe_throughput = achieved_throughput if math.isfinite(achieved_throughput) else 0.0
    ratio = safe_throughput / reference
    reward = (mcs_index / 7.0) * (ratio ** 3)
    return reward, reference, ratio


def start_simulation(args):
    """启动启用AI接口的ns-3场景；本脚本不包含策略网络。"""
    command = [
        str(EXECUTABLE),
        f"--simulationTime={args.simulation_time}",
        "--trafficStartTime=0.5",
        "--startDistance=1",
        "--movingSpeed=0.5",
        "--rateManager=ns3::rl-rateWifiManager",
        "--lossModel=log-distance",
        "--pathLossExponent=3",
        "--referenceLoss=66.6777",
        "--dataRate=60Mbps",
        "--packetSize=1420",
        "--sampleInterval=0.5",
        "--outputFile=/tmp/reproduction-reward-probe.csv",
        "--ns3::rl-rateWifiManager::EnableAi=true",
        "--ns3::rl-rateWifiManager::DataMode=HtMcs7",
        "--ns3::rl-rateWifiManager::ControlMode=HtMcs0",
        "--ns3::rl-rateWifiManager::PayloadSize=1420",
        "--ns3::rl-rateWifiManager::MeasurementStart=0.5s",
    ]
    if args.disable_ampdu:
        command.append("--ns3::WifiMac::BE_MaxAmpduSize=0")
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(ROOT / "build/lib")
    return subprocess.Popen(
        command,
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=os.setpgrp,
    )


def main():
    parser = argparse.ArgumentParser(
        description="Print REINRATE rewards while returning the current MCS unchanged"
    )
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=2.0)
    parser.add_argument("--channelWidth", dest="channel_width", type=int, choices=(20, 40), default=20)
    parser.add_argument("--printEvery", dest="print_every", type=int, default=10)
    parser.add_argument("--disableAmpdu", dest="disable_ampdu", action="store_true")
    parser.add_argument("--candidateMcs", dest="candidate_mcs", type=int, choices=range(8))
    args = parser.parse_args()

    #Python创建共享内存；参数名称和C++侧ns3-ai单例保持一致
    interface = reproduction_reinrate_py.Ns3AiMsgInterfaceImpl(
        True,
        False,
        True,
        4096,
        "My Seg",
        "My Cpp to Python Msg",
        "My Python to Cpp Msg",
        "My Lockable",
    )
    process = start_simulation(args)
    snr_preprocessor = SourceSnrPreprocessor()
    step = 0
    maximum_reward = 0.0

    try:
        while True:
            #严格同步顺序：先取得Observation所有权，复制字段后立即释放
            interface.PyRecvBegin()
            if interface.PyGetFinished():
                interface.PyRecvEnd()
                break
            env = interface.GetCpp2PyStruct()
            mcs = int(env.mcs)
            max_mcs = int(env.max_mcs)
            cw = int(env.cw)
            throughput = float(env.throughput)
            snr = float(env.snr)
            interface.PyRecvEnd()

            reward, reference, ratio = calculate_reward(
                throughput, mcs, args.channel_width
            )
            policy_input = snr_preprocessor.update(snr)
            maximum_reward = max(maximum_reward, reward)

            #没有策略网络时使用当前MCS作为候选；可用--candidateMcs测试源码约束逻辑
            candidate_mcs = mcs if args.candidate_mcs is None else args.candidate_mcs
            #源码推理路径把max_mcs当作下限：候选动作低于它时直接提升到max_mcs
            next_mcs = max(candidate_mcs, max_mcs)
            next_mcs = min(next_mcs, 7)

            if step < 3 or step % args.print_every == 0:
                print(
                    f"step={step:03d} mcs={mcs} max_mcs={max_mcs} cw={cw} "
                    f"snr={snr:.3f} policy_input={policy_input:.6f} "
                    f"throughput={throughput:.3f}Mbps reference={reference:.3f} "
                    f"ratio={ratio:.3f} reward={reward:.6f} "
                    f"candidate={candidate_mcs} decision={next_mcs}"
                )

            #无REINFORCE核心：只执行上面的源码决策约束并写回动作
            interface.PySendBegin()
            action = interface.GetPy2CppStruct()
            action.nss = 1
            action.next_mcs = next_mcs
            interface.PySendEnd()
            step += 1

        stdout, stderr = process.communicate(timeout=5)
        if process.returncode != 0:
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            return process.returncode or 1

        print(f"completed_steps={step} maximum_reward={maximum_reward:.6f}")
        if maximum_reward > 1.0:
            print(
                "WARNING: reward exceeded 1.0; the source formula is not bounded "
                "when measured window throughput exceeds its reference table."
            )
        return 0
    finally:
        if process.poll() is None:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        del interface


if __name__ == "__main__":
    raise SystemExit(main())
