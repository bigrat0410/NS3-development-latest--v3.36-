#!/usr/bin/env python3

import argparse
import csv
import math
import os
import signal
import subprocess
import sys
import warnings
from collections import Counter, deque
from pathlib import Path

import numpy as np
import torch

import reproduction_reinrate_py
from reinforce_model import ReinforceAgent


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "build/scratch/reproduction/ns3.36.1-two-node-ht-default"
RESULTS = ROOT / "my-project-results"


#源码奖励使用的单空间流吞吐参考表，第一轮基准不修改、不裁剪
REFERENCE_THROUGHPUT_MBPS = {
    20: [5.4, 9.8, 13.6, 17.5, 23.0, 26.1, 28.1, 29.6],
    40: [13.5, 27.0, 40.5, 54.0, 81.0, 108.0, 121.5, 135.0],
}


def calculate_reward(achieved_throughput, offered_load=60.0):
      ratio = achieved_throughput / offered_load
      return float(np.clip(ratio, 0.0, 1.0))


class ReinrateContainer:
    """复刻源码train_reinforce路径，不加入状态扩展或训练批处理。"""

    def __init__(self, convergence_window=200):
        self.agent = ReinforceAgent(1, 8)
        self.last_state = {"throughput": 0.0, "cw": 0, "mcs": 0}
        self.last_action = {"nss": 0, "next_mcs": 0}
        self.is_first_episode = True
        self.convergence_window = convergence_window
        self.recent = deque(maxlen=convergence_window)
        self.converged = False
        self.convergence_events = 0

    def update_convergence(self, mcs, reward):
        self.recent.append((mcs, reward))
        if self.converged:
            recent = list(self.recent)[-50:]
            success_rate = sum(reward > 0 for _, reward in recent) / len(recent)
            if len(recent) == 50 and success_rate < 0.70:
                self.converged = False
            return

        if len(self.recent) < self.convergence_window:
            return
        samples = list(self.recent)
        half = self.convergence_window // 2
        first_mean = sum(reward for _, reward in samples[:half]) / half
        second_mean = sum(reward for _, reward in samples[half:]) / (self.convergence_window - half)
        reward_change = abs(second_mean - first_mean) / max(abs(first_mean), 1e-6)
        success_rate = sum(reward > 0 for _, reward in samples) / len(samples)
        recent_success_rate = sum(reward > 0 for _, reward in samples[-50:]) / 50
        dominant_fraction = Counter(mcs for mcs, _ in samples).most_common(1)[0][1] / len(samples)
        if (reward_change <= 0.20 and success_rate >= 0.75
                and recent_success_rate >= 0.80 and dominant_fraction >= 0.45):
            self.converged = True
            self.convergence_events += 1

    def train_reinforce(self, observation):
        throughput = observation["throughput"]
        if not math.isfinite(throughput):
            throughput = 0.0
        snr = observation["snr"]
        if not math.isfinite(snr) or snr <= -1:
            snr = 0.0

        #源码训练输入只有一个特征：log(ACK_SNR+1)
        scaled_snr = np.log(snr + 1).astype(np.float32)

        if self.is_first_episode:
            self.last_state["mcs"] = observation["mcs"]
            next_mcs = self.agent.choose_action([scaled_snr])
            self.last_action["next_mcs"] = next_mcs
            self.is_first_episode = False
            return next_mcs, 0.0, math.nan, float(scaled_snr)

        #env.mcs是刚刚产生当前吞吐反馈的实际动作，奖励必须归属于它。
        reward = calculate_reward(throughput, observation["mcs"], 20)
        if not math.isfinite(reward):
            reward = 0.0
        self.agent.rewards.append(reward)
        loss = self.agent.update()
        self.update_convergence(observation["mcs"], reward)

        next_mcs = self.agent.choose_action([scaled_snr], greedy=self.converged)
        self.last_state["mcs"] = observation["mcs"]
        self.last_state["throughput"] = throughput
        self.last_action["next_mcs"] = next_mcs
        return next_mcs, reward, loss, float(scaled_snr)

    def inference_reinforce(self, observation):
        snr = observation["snr"]
        if not math.isfinite(snr) or snr <= -1:
            snr = 0.0
        scaled_snr = float(np.log(snr + 1).astype(np.float32))
        self.agent.eval()
        next_mcs = self.agent.choose_action([scaled_snr])
        self.last_action["next_mcs"] = next_mcs
        return next_mcs, scaled_snr


def build_command(args, throughput_csv):
    """完全采用Result_recor.md中的Scenario 1/Minstrel无线和业务参数。"""
    return [
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
        f"--Seed={args.seed}",
        "--Run=1",
        f"--outputFile={throughput_csv}",
        "--ns3::rl-rateWifiManager::EnableAi=true",
        "--ns3::rl-rateWifiManager::DataMode=HtMcs7",
        "--ns3::rl-rateWifiManager::ControlMode=HtMcs0",
        "--ns3::rl-rateWifiManager::PayloadSize=1420",
        "--ns3::rl-rateWifiManager::MeasurementStart=0.5s",
        "--ns3::rl-rateWifiManager::DecisionInterval=12ms",
    ]


def main():
    parser = argparse.ArgumentParser(description="Run the source-aligned REINRATE baseline")
    parser.add_argument("--Seed", dest="seed", type=int, default=774015)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument(
        "--epochs",
        type=int,
        default=0,
        help="maximum online-training callbacks; 0 trains for the full simulation",
    )
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument(
        "--resultLabel",
        default="reinrate",
        help="label used in generated result filenames",
    )
    parser.add_argument("--convergenceWindow", dest="convergence_window", type=int, default=200)
    parser.add_argument("--printEvery", dest="print_every", type=int, default=500)
    args = parser.parse_args()

    RESULTS.mkdir(exist_ok=True)
    throughput_csv = RESULTS / f"reproduction-scenario1-{args.resultLabel}-seed{args.seed}.csv"
    decision_csv = RESULTS / f"reproduction-scenario1-{args.resultLabel}-decisions-seed{args.seed}.csv"
    model_file = RESULTS / f"reproduction-scenario1-{args.resultLabel}-seed{args.seed}.pt"

    #Python侧创建共享内存；C++侧以相同名称连接
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

    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(ROOT / "build/lib")
    process = subprocess.Popen(
        build_command(args, throughput_csv),
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=os.setpgrp,
    )

    np.random.seed(args.agent_seed)
    torch.manual_seed(args.agent_seed)
    container = ReinrateContainer(convergence_window=args.convergence_window)
    step = 0
    rewards = []
    actions = [0] * 8

    #单样本return.std()在源码中会触发PyTorch自由度警告；隐藏警告但不改变计算
    warnings.filterwarnings("ignore", message="std\(\): degrees of freedom")

    try:
        with decision_csv.open("w", newline="") as decision_handle:
            writer = csv.writer(decision_handle)
            writer.writerow([
                "step", "mcs", "max_mcs", "cw", "throughput_mbps", "snr_linear",
                "scaled_snr", "reward", "next_mcs", "loss", "converged",
            ])

            while True:
                interface.PyRecvBegin()
                if interface.PyGetFinished():
                    interface.PyRecvEnd()
                    break
                env = interface.GetCpp2PyStruct()
                observation = {
                    "mcs": int(env.mcs),
                    "max_mcs": int(env.max_mcs),
                    "cw": int(env.cw),
                    "throughput": float(env.throughput),
                    "snr": float(env.snr),
                }
                interface.PyRecvEnd()

                #在线模式贯穿整个仿真。若显式设置上限，之后使用策略最大概率
                #动作推理，避免把边界处的一次随机探索永久固定。
                if args.epochs <= 0 or step <= args.epochs:
                    next_mcs, reward, loss, scaled_snr = container.train_reinforce(observation)
                else:
                    next_mcs, scaled_snr = container.inference_reinforce(observation)
                    reward = math.nan
                    loss = math.nan
                rewards.append(reward)
                actions[next_mcs] += 1
                writer.writerow([
                    step,
                    observation["mcs"],
                    observation["max_mcs"],
                    observation["cw"],
                    observation["throughput"],
                    observation["snr"],
                    scaled_snr,
                    reward,
                    next_mcs,
                    loss,
                    int(container.converged),
                ])

                if step < 3 or step % args.print_every == 0:
                    print(
                        f"step={step} mcs={observation['mcs']} next={next_mcs} "
                        f"tpt={observation['throughput']:.3f} "
                        f"snr_in={scaled_snr:.6f} reward={reward:.6f} loss={loss:.6f}"
                    )

                interface.PySendBegin()
                action = interface.GetPy2CppStruct()
                action.nss = 1
                action.next_mcs = next_mcs
                interface.PySendEnd()
                step += 1

        stdout, stderr = process.communicate(timeout=10)
        if process.returncode != 0:
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            return process.returncode or 1

        container.agent.save_model(model_file)
        finite_rewards = [reward for reward in rewards if math.isfinite(reward)]
        print(f"completed_steps={step}")
        training_calls = step if args.epochs <= 0 else min(step, args.epochs + 1)
        gradient_updates = max(training_calls - 1, 0)
        print(f"training_calls={training_calls}")
        print(f"gradient_updates={gradient_updates}")
        print(f"action_counts={actions}")
        print(f"mean_reward={sum(finite_rewards) / len(finite_rewards):.6f}")
        print(f"convergence_events={container.convergence_events}")
        print(f"converged_at_finish={int(container.converged)}")
        print(f"throughput_csv={throughput_csv}")
        print(f"decision_csv={decision_csv}")
        print(f"model={model_file}")
        return 0
    finally:
        if process.poll() is None:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        del interface


if __name__ == "__main__":
    raise SystemExit(main())
