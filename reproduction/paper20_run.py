#!/usr/bin/env python3

import argparse
import csv
import math
import os
import signal
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

import reproduction_reinrate_py
from paper20_model import Paper20ReinforceAgent


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "build/scratch/reproduction/ns3.36.1-two-node-ht-default"
RESULTS = ROOT / "my-project-results"


def policy_state(env):
    snr = float(env.snr)
    if not math.isfinite(snr) or snr < 0.0:
        snr = 0.0
    throughput = float(env.throughput)
    if not math.isfinite(throughput) or throughput < 0.0:
        throughput = 0.0
    return np.array(
        [
            math.log1p(snr),
            min(float(env.cw), 1023.0) / 1023.0,
            min(float(env.mcs), 7.0) / 7.0,
            min(throughput, 60.0) / 60.0,
        ],
        dtype=np.float32,
    )


def build_command(args, throughput_csv):
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
        "--ns3::rl-rateWifiManager::DecisionPacketWindow=20",
        "--ns3::rl-rateWifiManager::RewardDiscount=0.99",
    ] + ([] if args.enable_ampdu else ["--ns3::WifiMac::BE_MaxAmpduSize=0"])


def main():
    parser = argparse.ArgumentParser(description="Run the Paper20 ReinRate experiment")
    parser.add_argument("--Seed", dest="seed", type=int, default=774015)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument(
        "--enableAmpdu",
        dest="enable_ampdu",
        action="store_true",
        help="retain the scenario's default BE A-MPDU aggregation",
    )
    parser.add_argument("--resultLabel", default="reinrate-paper20")
    parser.add_argument("--printEvery", dest="print_every", type=int, default=100)
    args = parser.parse_args()

    if not 0.0 <= args.epsilon <= 1.0:
        parser.error("epsilon must be between 0 and 1")

    RESULTS.mkdir(exist_ok=True)
    throughput_csv = RESULTS / f"reproduction-scenario1-{args.resultLabel}-seed{args.seed}.csv"
    decision_csv = RESULTS / f"reproduction-scenario1-{args.resultLabel}-decisions-seed{args.seed}.csv"
    model_file = RESULTS / f"reproduction-scenario1-{args.resultLabel}-seed{args.seed}.pt"

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
    agent = Paper20ReinforceAgent()
    window = 0
    first_window = True
    action_counts = [0] * 8

    try:
        with decision_csv.open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(
                [
                    "window", "mcs", "cw", "throughput_mbps", "snr_linear",
                    "window_return", "advantage", "next_mcs", "loss",
                ]
            )
            while True:
                interface.PyRecvBegin()
                if interface.PyGetFinished():
                    interface.PyRecvEnd()
                    break
                env = interface.GetCpp2PyStruct()
                state = policy_state(env)
                observation = (int(env.mcs), int(env.cw), float(env.throughput), float(env.snr))
                window_return = float(env.reward)
                interface.PyRecvEnd()

                if first_window:
                    loss = math.nan
                    advantage = math.nan
                    first_window = False
                else:
                    loss, advantage = agent.update(window_return)

                next_mcs = agent.choose_action(state, epsilon=args.epsilon)
                action_counts[next_mcs] += 1
                writer.writerow(
                    [
                        window,
                        observation[0],
                        observation[1],
                        observation[2],
                        observation[3],
                        window_return,
                        advantage,
                        next_mcs,
                        loss,
                    ]
                )
                if window < 3 or window % args.print_every == 0:
                    print(
                        f"window={window} mcs={observation[0]} next={next_mcs} "
                        f"tp={observation[2]:.3f} return={window_return:.6f} loss={loss:.6f}"
                    )

                interface.PySendBegin()
                action = interface.GetPy2CppStruct()
                action.nss = 1
                action.next_mcs = next_mcs
                interface.PySendEnd()
                window += 1

        stdout, stderr = process.communicate(timeout=10)
        if process.returncode != 0:
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            return process.returncode or 1

        agent.save_model(model_file)
        print(f"completed_windows={window}")
        print(f"gradient_updates={max(window - 1, 0)}")
        print(f"action_counts={action_counts}")
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
