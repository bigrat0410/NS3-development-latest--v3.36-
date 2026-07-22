#!/usr/bin/env python3

import argparse
import csv
import math
import os
import random
import signal
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

import reproduction_reinrate_py
from offline_reinforce_model import ReinRateAgent


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "build/scratch/reproduction/ns3.36.1-two-node-ht-default"
RESULTS = ROOT / "my-project-results"
PREFIX = "reproduction-scenario1-offline-window20-episodic-reinforce"
PACKET_WINDOW = 20
MAX_REFERENCE_MBPS = 29.6


def policy_state(observation):
    """Normalize the state observed at a 20-packet decision boundary."""
    snr = observation["snr"]
    throughput = observation["throughput"]
    if not math.isfinite(snr) or snr < 0.0:
        snr = 0.0
    if not math.isfinite(throughput) or throughput < 0.0:
        throughput = 0.0
    cw = min(max(observation["cw"], 0), 1023)
    mcs = min(max(observation["mcs"], 0), 7)
    return np.asarray(
        [
            math.log1p(snr) / 12.0,
            math.log2(cw + 1.0) / 10.0,
            (mcs + 1.0) / 8.0,
            min(throughput / MAX_REFERENCE_MBPS, 1.0),
        ],
        dtype=np.float32,
    )


def build_command(seed, simulation_time, start_distance, moving_speed, throughput_csv):
    return [
        str(EXECUTABLE),
        f"--simulationTime={simulation_time}",
        "--trafficStartTime=0.5",
        f"--startDistance={start_distance}",
        f"--movingSpeed={moving_speed}",
        "--rateManager=ns3::rl-rateWifiManager",
        "--lossModel=log-distance",
        "--pathLossExponent=3",
        "--referenceLoss=66.6777",
        "--dataRate=60Mbps",
        "--packetSize=1420",
        "--sampleInterval=0.5",
        f"--Seed={seed}",
        "--Run=1",
        f"--outputFile={throughput_csv}",
        "--ns3::WifiMac::BE_MaxAmpduSize=0",
        "--ns3::rl-rateWifiManager::EnableAi=true",
        "--ns3::rl-rateWifiManager::DecisionPerAmpdu=false",
        f"--ns3::rl-rateWifiManager::DecisionPacketWindow={PACKET_WINDOW}",
        "--ns3::rl-rateWifiManager::DataMode=HtMcs7",
        "--ns3::rl-rateWifiManager::ControlMode=HtMcs0",
        "--ns3::rl-rateWifiManager::PayloadSize=1420",
        "--ns3::rl-rateWifiManager::MeasurementStart=0.5s",
    ]


def throughput_metrics(path):
    with Path(path).open(newline="") as handle:
        values = [float(row["throughput_mbps"]) for row in csv.DictReader(handle)]
    zero_flags = [value <= 1e-9 for value in values]
    longest_zero_run = 0
    current_zero_run = 0
    for is_zero in zero_flags:
        current_zero_run = current_zero_run + 1 if is_zero else 0
        longest_zero_run = max(longest_zero_run, current_zero_run)
    return {
        "mean": sum(values) / len(values) if values else 0.0,
        "zero_fraction": sum(zero_flags) / len(zero_flags) if zero_flags else 1.0,
        "longest_zero_run": longest_zero_run,
    }


def create_interface():
    return reproduction_reinrate_py.Ns3AiMsgInterfaceImpl(
        True, False, True, 4096,
        "My Seg", "My Cpp to Python Msg", "My Python to Cpp Msg", "My Lockable",
    )


def run_episode(
    agent,
    seed,
    simulation_time,
    start_distance,
    moving_speed,
    label,
    training,
    interface=None,
    log_decisions=False,
    print_every=5000,
    state_encoder=policy_state,
):
    """Run one frozen-policy trajectory and optionally update once at its end."""
    throughput_csv = RESULTS / f"reproduction-scenario1-{label}-seed{seed}.csv"
    decision_csv = RESULTS / f"reproduction-scenario1-{label}-decisions-seed{seed}.csv"
    owns_interface = interface is None
    if owns_interface:
        interface = create_interface()
    interface.PyReset()
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(ROOT / "build/lib")
    process = subprocess.Popen(
        build_command(seed, simulation_time, start_distance, moving_speed, throughput_csv),
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=os.setpgrp,
    )

    fieldnames = [
        "window", "simulation_time_s", "packet_count", "successful_packets",
        "failed_packets", "mcs", "next_mcs", "exploratory", "cw",
        "window_throughput_mbps", "window_reward", "snr_linear",
        "state_snr", "state_cw", "state_mcs", "state_throughput",
        "max_probability", "probabilities",
    ]
    decision_handle = decision_csv.open("w", newline="") if log_decisions else None
    writer = csv.DictWriter(decision_handle, fieldnames=fieldnames) if decision_handle else None
    if writer:
        writer.writeheader()

    active_action = None
    active_probabilities = None
    active_exploratory = False
    action_counts = [0] * 8
    exploration_count = 0
    window_rewards = []
    packet_counts = []
    max_probabilities = []
    window = 0
    update_count_before = agent.gradient_updates

    try:
        while True:
            interface.PyRecvBegin()
            if interface.PyGetFinished():
                interface.PyRecvEnd()
                break
            env = interface.GetCpp2PyStruct()
            observation = {
                "mcs": int(env.mcs),
                "cw": int(env.cw),
                "throughput": float(env.throughput),
                "snr": float(env.snr),
                "window_reward": float(env.raw_reward),
                "simulation_time": float(env.simulation_time),
                "packet_count": int(env.aggregate_mpdus),
                "successful_packets": int(env.successful_mpdus),
                "failed_packets": int(env.failed_mpdus),
            }
            interface.PyRecvEnd()

            if active_action is not None:
                if observation["mcs"] != active_action:
                    raise RuntimeError(
                        f"reward/action mismatch: observed MCS{observation['mcs']} "
                        f"for active MCS{active_action}"
                    )
                packet_count = observation["packet_count"]
                if not 1 <= packet_count <= PACKET_WINDOW:
                    raise RuntimeError(f"invalid completed-packet window: {packet_count}")
                if observation["successful_packets"] + observation["failed_packets"] != packet_count:
                    raise RuntimeError("successful and failed packet counts do not match window size")
                if observation["window_reward"] < 0.0 or not math.isfinite(observation["window_reward"]):
                    raise RuntimeError("window reward must be finite and non-negative")
                if training:
                    agent.record_window(observation["window_reward"])
                window_rewards.append(observation["window_reward"])
                packet_counts.append(packet_count)
                window += 1

            state = state_encoder(observation)
            active_probabilities = agent.action_probabilities(state)
            if training:
                active_action = agent.select_action(state)
                active_exploratory = agent.last_exploratory
            else:
                active_action = agent.select_greedy_action(state)
                active_exploratory = False
            action_counts[active_action] += 1
            exploration_count += int(active_exploratory)
            max_probabilities.append(max(active_probabilities))

            if writer:
                writer.writerow({
                    "window": window,
                    "simulation_time_s": observation["simulation_time"],
                    "packet_count": observation["packet_count"],
                    "successful_packets": observation["successful_packets"],
                    "failed_packets": observation["failed_packets"],
                    "mcs": observation["mcs"],
                    "next_mcs": active_action,
                    "exploratory": int(active_exploratory),
                    "cw": observation["cw"],
                    "window_throughput_mbps": observation["throughput"],
                    "window_reward": observation["window_reward"],
                    "snr_linear": observation["snr"],
                    "state_snr": state[0],
                    "state_cw": state[1],
                    "state_mcs": state[2],
                    "state_throughput": state[3],
                    "max_probability": max(active_probabilities),
                    "probabilities": " ".join(f"{value:.8f}" for value in active_probabilities),
                })

            if window < 3 or window % print_every == 0:
                mode = "train" if training else "eval"
                print(
                    f"{mode} window={window} packets={observation['packet_count']} "
                    f"mcs={observation['mcs']} next={active_action} "
                    f"reward={observation['window_reward']:.6f} "
                    f"pmax={max(active_probabilities):.4f}",
                    flush=True,
                )

            interface.PySendBegin()
            action = interface.GetPy2CppStruct()
            action.nss = 1
            action.next_mcs = active_action
            interface.PySendEnd()

        stdout, stderr = process.communicate(timeout=10)
        if process.returncode != 0:
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            raise RuntimeError(f"ns-3 exited with status {process.returncode}")

        update = agent.finish_episode() if training and agent.rewards else None
        metrics = throughput_metrics(throughput_csv)
        partial_windows = sum(count < PACKET_WINDOW for count in packet_counts)
        if partial_windows > 1 or (partial_windows and packet_counts[-1] == PACKET_WINDOW):
            raise RuntimeError("only the final decision window may contain fewer than 20 packets")
        return {
            "windows": len(window_rewards),
            "packets": sum(packet_counts),
            "partial_windows": partial_windows,
            "updates": agent.gradient_updates - update_count_before,
            "mean_window_reward": (
                sum(window_rewards) / len(window_rewards) if window_rewards else 0.0
            ),
            "total_reward": sum(window_rewards),
            "throughput": metrics["mean"],
            "zero_fraction": metrics["zero_fraction"],
            "longest_zero_run": metrics["longest_zero_run"],
            "action_counts": action_counts,
            "exploration_count": exploration_count,
            "mean_probability": sum(max_probabilities) / len(max_probabilities),
            "last_update": update,
            "throughput_csv": throughput_csv,
            "decision_csv": decision_csv if log_decisions else None,
        }
    finally:
        if decision_handle:
            decision_handle.close()
        if process.poll() is None:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            process.wait(timeout=10)
        if owns_interface:
            del interface


def plot_milestones(max_episode):
    episodes = list(range(50, max_episode + 1, 50))
    if not episodes:
        return None
    output = RESULTS / f"{PREFIX}-every-50-episodes.svg"
    subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("plot_training_episodes.py")),
            "--input-glob",
            str(RESULTS / f"{PREFIX}-train-episode*-seed*.csv"),
            "--output", str(output),
            "--title", "Window-20 Episodic Policy Gradient: Every 50 Episodes",
            "--episodes", *[str(episode) for episode in episodes],
        ],
        cwd=ROOT,
        check=True,
    )
    return output


def plot_history(history_csv):
    output = RESULTS / f"{PREFIX}-training-statistics.svg"
    subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("plot_training_history.py")),
            "--input", str(history_csv),
            "--output", str(output),
        ],
        cwd=ROOT,
        check=True,
    )
    return output


def main():
    parser = argparse.ArgumentParser(description="Train the 20-packet episodic policy")
    parser.add_argument("--maxEpisodes", dest="max_episodes", type=int, default=1000)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=1.0)
    parser.add_argument("--movingSpeed", dest="moving_speed", type=float, default=0.5)
    parser.add_argument("--trainSeed", dest="train_seed", type=int, default=774015)
    parser.add_argument("--validationSeed", dest="validation_seed", type=int, default=884015)
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument("--validateEvery", dest="validate_every", type=int, default=50)
    parser.add_argument("--learningRate", dest="learning_rate", type=float, default=1e-4)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    RESULTS.mkdir(exist_ok=True)
    random.seed(args.agent_seed)
    np.random.seed(args.agent_seed)
    torch.manual_seed(args.agent_seed)
    agent = ReinRateAgent(lr=args.learning_rate, gamma=args.gamma, epsilon=args.epsilon)
    checkpoint = RESULTS / f"{PREFIX}-checkpoint.pt"
    best_policy = RESULTS / f"{PREFIX}-best.pt"
    final_policy = RESULTS / f"{PREFIX}-final.pt"
    history_csv = RESULTS / f"{PREFIX}-training.csv"
    start_episode = 1
    best_throughput = -math.inf
    if args.resume and checkpoint.exists():
        start_episode = agent.load_checkpoint(checkpoint) + 1
        if history_csv.exists():
            with history_csv.open(newline="") as handle:
                for row in csv.DictReader(handle):
                    value = float(row["validation_throughput_mbps"])
                    if math.isfinite(value):
                        best_throughput = max(best_throughput, value)

    append = args.resume and history_csv.exists()
    interface = create_interface()
    with history_csv.open("a" if append else "w", newline="") as handle:
        fieldnames = [
            "episode", "train_seed", "train_throughput_mbps", "mean_window_reward",
            "total_reward", "windows", "packets", "partial_windows", "updates",
            "loss", "return_mean", "return_std", "return_min", "return_max",
            "gradient_norm", "parameter_norm_before", "parameter_norm_after",
            "exploration_count", "action_counts", "validation_throughput_mbps",
            "validation_probability", "validation_zero_fraction",
            "validation_longest_zero_run", "gamma", "epsilon", "learning_rate",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        if not append:
            writer.writeheader()

        for episode in range(start_episode, args.max_episodes + 1):
            train_seed = args.train_seed + episode - 1
            keep_trace = episode == 1 or episode % 50 == 0
            train = run_episode(
                agent,
                train_seed,
                args.simulation_time,
                args.start_distance,
                args.moving_speed,
                f"offline-window20-episodic-reinforce-train-episode{episode:04d}",
                training=True,
                interface=interface,
                log_decisions=keep_trace,
            )
            if train["updates"] != 1:
                raise RuntimeError(f"episode {episode} made {train['updates']} updates, expected 1")
            update = train["last_update"]

            validation = None
            if episode % args.validate_every == 0:
                validation = run_episode(
                    agent,
                    args.validation_seed,
                    args.simulation_time,
                    args.start_distance,
                    args.moving_speed,
                    f"offline-window20-episodic-reinforce-validation-episode{episode:04d}",
                    training=False,
                    interface=interface,
                    log_decisions=True,
                )
                if validation["throughput"] > best_throughput:
                    best_throughput = validation["throughput"]
                    agent.save_policy(best_policy)

            writer.writerow({
                "episode": episode,
                "train_seed": train_seed,
                "train_throughput_mbps": train["throughput"],
                "mean_window_reward": train["mean_window_reward"],
                "total_reward": train["total_reward"],
                "windows": train["windows"],
                "packets": train["packets"],
                "partial_windows": train["partial_windows"],
                "updates": train["updates"],
                "loss": update["loss"],
                "return_mean": update["return_mean"],
                "return_std": update["return_std"],
                "return_min": update["return_min"],
                "return_max": update["return_max"],
                "gradient_norm": update["gradient_norm"],
                "parameter_norm_before": update["parameter_norm_before"],
                "parameter_norm_after": update["parameter_norm_after"],
                "exploration_count": train["exploration_count"],
                "action_counts": " ".join(map(str, train["action_counts"])),
                "validation_throughput_mbps": validation["throughput"] if validation else math.nan,
                "validation_probability": validation["mean_probability"] if validation else math.nan,
                "validation_zero_fraction": validation["zero_fraction"] if validation else math.nan,
                "validation_longest_zero_run": validation["longest_zero_run"] if validation else math.nan,
                "gamma": agent.gamma,
                "epsilon": agent.epsilon,
                "learning_rate": args.learning_rate,
            })
            handle.flush()
            agent.save_checkpoint(checkpoint, episode)
            if episode % 50 == 0:
                plot_milestones(episode)
                plot_history(history_csv)
            print(
                f"episode={episode} train_tp={train['throughput']:.6f} "
                f"windows={train['windows']} packets={train['packets']} "
                f"reward={train['mean_window_reward']:.6f} loss={update['loss']:.6f} "
                f"updates={train['updates']} val_tp="
                f"{validation['throughput'] if validation else math.nan:.6f}",
                flush=True,
            )

    agent.save_policy(final_policy)
    if not best_policy.exists():
        agent.save_policy(best_policy)
    plot_milestones(args.max_episodes)
    plot_history(history_csv)
    del interface
    print(f"best_validation_throughput={best_throughput:.6f}")
    print(f"best_policy={best_policy}")
    print(f"final_policy={final_policy}")
    print(f"history={history_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
