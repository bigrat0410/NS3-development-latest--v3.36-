#!/usr/bin/env python3

import argparse
import csv
import math
import os
import random
import signal
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import torch

import reproduction_reinrate_py
from offline_reinforce_model import ReinRateAgent


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "build/scratch/reproduction/ns3.36.1-two-node-ht-default"
RESULTS = ROOT / "my-project-results"
OFFERED_LOAD_MBPS = 60.0


def policy_state(observation):
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
            min(throughput / OFFERED_LOAD_MBPS, 1.0),
        ],
        dtype=np.float32,
    )


def calculate_reward(throughput_mbps):
    # 每个完整PPDU/A-MPDU反馈一次，对原始吞吐量reward应用三次方变换。
    if not math.isfinite(throughput_mbps) or throughput_mbps < 0.0:
        return 0.0
    return throughput_mbps ** 3


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
        "--ns3::rl-rateWifiManager::EnableAi=true",
        "--ns3::rl-rateWifiManager::DecisionPerAmpdu=true",
        "--ns3::rl-rateWifiManager::DataMode=HtMcs7",
        "--ns3::rl-rateWifiManager::ControlMode=HtMcs0",
        "--ns3::rl-rateWifiManager::PayloadSize=1420",
        "--ns3::rl-rateWifiManager::MeasurementStart=0.5s",
    ]


def mean_throughput(path):
    with Path(path).open(newline="") as handle:
        values = []
        for row in csv.DictReader(handle):
            value = row.get("throughput_mbps")
            if value not in (None, ""):
                values.append(float(value))
    return sum(values) / len(values) if values else 0.0


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


def ideal_validation_metrics(seed, simulation_time, start_distance, moving_speed):
    output = RESULTS / f"reproduction-scenario1-offline-training-ideal-seed{seed}.csv"
    command = [
        str(EXECUTABLE),
        f"--simulationTime={simulation_time}",
        "--trafficStartTime=0.5",
        f"--startDistance={start_distance}",
        f"--movingSpeed={moving_speed}",
        "--rateManager=ns3::IdealWifiManager",
        "--lossModel=log-distance",
        "--pathLossExponent=3",
        "--referenceLoss=66.6777",
        "--dataRate=60Mbps",
        "--packetSize=1420",
        "--sampleInterval=0.5",
        f"--Seed={seed}",
        "--Run=1",
        f"--outputFile={output}",
    ]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return throughput_metrics(output)


def create_interface():
    return reproduction_reinrate_py.Ns3AiMsgInterfaceImpl(
        True, False, True, 4096,
        "My Seg", "My Cpp to Python Msg", "My Python to Cpp Msg", "My Lockable",
    )


def write_markdown_report(path, run, agent, configuration):
    """把一轮训练的逐包决策和每次梯度更新写成可审计Markdown。"""
    path = Path(path)
    with path.open("w", encoding="utf-8") as report:
        report.write("# Offline REINFORCE 单轮完整训练日志\n\n")
        report.write(f"生成时间：{datetime.now().astimezone().isoformat()}\n\n")
        report.write("## 算法与场景参数\n\n")
        report.write("```text\n")
        report.write(
            "    ".join(f"{key} = {value}" for key, value in configuration.items()) + "\n"
        )
        report.write("```\n\n")
        report.write("## 训练汇总\n\n")
        report.write("```text\n")
        summary = {
            "feedbacks": run["steps"],
            "rewarded_aggregates": run["complete_windows"],
            "decisions": run["decisions"],
            "gradient_updates": run["updates"],
            "discarded_tail": run["discarded_tail"],
            "mean_cubic_reward": f"{run['mean_reward']:.9f}",
            "sampled_throughput_mbps": f"{run['throughput']:.9f}",
            "action_counts": run["action_counts"],
        }
        report.write("    ".join(f"{key} = {value}" for key, value in summary.items()) + "\n")
        report.write("```\n\n")
        report.write("## 逐聚合包决策与梯度日志\n\n")
        report.write(
            "每行参数以四个空格横向分隔；`reward = nan` 的首行是业务开始前的初始动作。\n\n"
        )
        with Path(run["decision_csv"]).open(newline="") as source:
            for row in csv.DictReader(source):
                report.write("```text\n")
                ordered = [
                    "feedback_step", "decision", "mcs", "next_mcs", "cw",
                    "simulation_time_s", "aggregate_mpdus", "successful_mpdus",
                    "failed_mpdus", "throughput_mbps", "snr_linear", "state_snr", "state_cw",
                    "state_mcs", "state_throughput", "reward", "max_probability",
                    "probabilities", "update", "loss", "return_mean", "return_std",
                    "gradient_norm", "parameter_norm_before", "parameter_norm_after",
                    "learning_rate", "batch_rewards", "batch_returns", "update_actions",
                    "action_sample_counts", "action_mean_rewards",
                    "normalized_action_rewards",
                ]
                report.write(
                    "    ".join(f"{key} = {row[key]}" for key in ordered) + "\n"
                )
                report.write("```\n\n")
        report.write("## 文件索引\n\n")
        report.write(f"逐聚合包CSV：`{run['decision_csv']}`\n\n")
        report.write(f"吞吐量CSV：`{run['throughput_csv']}`\n")


def run_episode(
    agent,
    seed,
    simulation_time,
    start_distance,
    moving_speed,
    label,
    training,
    print_every=1000,
    interface=None,
):
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

    feedback_step = 0
    decision = 0
    action_counts = [0] * 8
    maximum_probabilities = []
    selected_actions = []
    reward_sum = 0.0
    reward_count = 0
    active_action = None
    active_log_prob = None
    active_probabilities = None
    update_count_before = agent.gradient_updates

    try:
        with decision_csv.open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(
                [
                    "feedback_step", "decision", "mcs", "cw", "throughput_mbps", "snr_linear",
                    "simulation_time_s", "aggregate_mpdus", "successful_mpdus", "failed_mpdus",
                    "state_snr", "state_cw", "state_mcs", "state_throughput",
                    "reward", "next_mcs", "max_probability", "probabilities",
                    "update", "loss", "return_mean", "return_std", "gradient_norm",
                    "parameter_norm_before", "parameter_norm_after", "learning_rate",
                    "batch_rewards", "batch_returns", "update_actions",
                    "action_sample_counts", "action_mean_rewards",
                    "normalized_action_rewards",
                ]
            )
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
                    "simulation_time": float(env.simulation_time),
                    "aggregate_mpdus": int(env.aggregate_mpdus),
                    "successful_mpdus": int(env.successful_mpdus),
                    "failed_mpdus": int(env.failed_mpdus),
                }
                interface.PyRecvEnd()

                reward = math.nan
                choose_new_action = active_action is None
                update = None
                if active_action is not None:
                    reward = calculate_reward(observation["throughput"])
                    choose_new_action = True
                    decision += 1
                    if training:
                        update = agent.step(active_log_prob, reward)
                    reward_sum += reward
                    reward_count += 1

                if choose_new_action:
                    state = policy_state(observation)
                    active_probabilities = agent.action_probabilities(state)
                    if training:
                        active_action = agent.select_action(state)
                        active_log_prob = agent.last_log_prob
                    else:
                        active_action = agent.select_greedy_action(state)
                        active_log_prob = None
                    maximum_probability = max(active_probabilities)
                    maximum_probabilities.append(maximum_probability)
                    selected_actions.append(active_action)
                    action_counts[active_action] += 1
                else:
                    maximum_probability = max(active_probabilities)
                next_mcs = active_action
                probabilities = active_probabilities
                writer.writerow(
                    [
                        feedback_step, decision,
                        observation["mcs"],
                        observation["cw"],
                        observation["throughput"],
                        observation["snr"],
                        observation["simulation_time"],
                        observation["aggregate_mpdus"],
                        observation["successful_mpdus"],
                        observation["failed_mpdus"],
                        state[0], state[1], state[2], state[3],
                        reward,
                        next_mcs,
                        maximum_probability,
                        " ".join(f"{value:.6f}" for value in probabilities),
                        update["update"] if update else "",
                        update["loss"] if update else "",
                        update["return_mean"] if update else "",
                        update["return_std"] if update else "",
                        update["gradient_norm"] if update else "",
                        update["parameter_norm_before"] if update else "",
                        update["parameter_norm_after"] if update else "",
                        update["learning_rate"] if update else "",
                        " ".join(f"{value:.9f}" for value in update["rewards"])
                        if update else "",
                        " ".join(f"{value:.9f}" for value in update["returns"])
                        if update else "",
                        " ".join(str(value) for value in update["unique_actions"])
                        if update else "",
                        " ".join(str(value) for value in update["action_sample_counts"])
                        if update else "",
                        " ".join(f"{value:.9f}" for value in update["action_mean_rewards"])
                        if update else "",
                        " ".join(
                            f"{value:.9f}" for value in update["normalized_action_rewards"]
                        ) if update else "",
                    ]
                )
                if feedback_step < 3 or feedback_step % print_every == 0:
                    mode = "train" if training else "eval"
                    print(
                        f"{mode} feedback={feedback_step} decision={decision} "
                        f"mcs={observation['mcs']} next={next_mcs} "
                        f"tp={observation['throughput']:.3f} pmax={maximum_probability:.3f}"
                    )

                interface.PySendBegin()
                action = interface.GetPy2CppStruct()
                action.nss = 1
                action.next_mcs = next_mcs
                interface.PySendEnd()
                feedback_step += 1

        stdout, stderr = process.communicate(timeout=10)
        if process.returncode != 0:
            print(stdout, end="")
            print(stderr, end="", file=sys.stderr)
            raise RuntimeError(f"ns-3 exited with status {process.returncode}")
        discarded_tail = agent.reset_rollout() if training else 0
        sampled_metrics = throughput_metrics(throughput_csv)
        return {
            "steps": feedback_step,
            "decisions": len(selected_actions),
            "complete_windows": reward_count,
            "partial_window_packets": 0,
            "updates": agent.gradient_updates - update_count_before,
            "discarded_tail": discarded_tail,
            "mean_reward": reward_sum / reward_count if reward_count else 0.0,
            "mean_probability": sum(maximum_probabilities) / len(maximum_probabilities),
            "actions": selected_actions,
            "action_counts": action_counts,
            "last_update": agent.last_update,
            "throughput": sampled_metrics["mean"],
            "zero_fraction": sampled_metrics["zero_fraction"],
            "longest_zero_run": sampled_metrics["longest_zero_run"],
            "throughput_csv": throughput_csv,
            "decision_csv": decision_csv,
        }
    finally:
        if process.poll() is None:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            process.wait(timeout=10)
        if owns_interface:
            del interface


def action_agreement(first, second):
    count = min(len(first), len(second))
    if count == 0:
        return 0.0
    return sum(first[index] == second[index] for index in range(count)) / count


def main():
    parser = argparse.ArgumentParser(description="Repeated Scenario 1 REINFORCE training")
    parser.add_argument("--maxEpisodes", dest="max_episodes", type=int, default=60)
    parser.add_argument("--minEpisodes", dest="min_episodes", type=int, default=15)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=1.0)
    parser.add_argument("--movingSpeed", dest="moving_speed", type=float, default=0.5)
    parser.add_argument("--trainingSegmentTime", dest="training_segment_time", type=float, default=100.0)
    parser.add_argument("--trainSeed", dest="train_seed", type=int, default=774015)
    parser.add_argument("--validationSeed", dest="validation_seed", type=int, default=884015)
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument("--validateEvery", dest="validate_every", type=int, default=5)
    parser.add_argument("--stableChecks", dest="stable_checks", type=int, default=3)
    parser.add_argument("--probabilityThreshold", dest="probability_threshold", type=float, default=0.90)
    parser.add_argument("--agreementThreshold", dest="agreement_threshold", type=float, default=0.98)
    parser.add_argument("--throughputTolerance", dest="throughput_tolerance", type=float, default=0.5)
    parser.add_argument("--idealRatio", dest="ideal_ratio", type=float, default=0.95)
    parser.add_argument("--zeroFractionTolerance", dest="zero_fraction_tolerance", type=float, default=0.02)
    parser.add_argument("--zeroRunTolerance", dest="zero_run_tolerance", type=int, default=2)
    parser.add_argument("--learningRate", dest="learning_rate", type=float, default=1e-3)
    parser.add_argument("--gamma", type=float, default=0.0)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument("--printEvery", dest="print_every", type=int, default=2000)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--reportMarkdown", dest="report_markdown", type=Path)
    args = parser.parse_args()

    RESULTS.mkdir(exist_ok=True)
    random.seed(args.agent_seed)
    np.random.seed(args.agent_seed)
    torch.manual_seed(args.agent_seed)
    agent = ReinRateAgent(
        lr=args.learning_rate,
        gamma=args.gamma,
        epsilon=args.epsilon,
    )
    best_policy = RESULTS / "reproduction-scenario1-offline-reinforce-h10-best.pt"
    checkpoint = RESULTS / "reproduction-scenario1-offline-reinforce-h10-checkpoint.pt"
    history_csv = RESULTS / "reproduction-scenario1-offline-reinforce-h10-training.csv"
    previous_validation = None
    stable_checks = 0
    best_throughput = -math.inf
    converged = False
    ideal_metrics = ideal_validation_metrics(
        args.validation_seed,
        args.simulation_time,
        args.start_distance,
        args.moving_speed,
    )
    ideal_throughput = ideal_metrics["mean"]
    target_throughput = args.ideal_ratio * ideal_throughput
    interface = create_interface()

    start_episode = 1
    if args.resume and checkpoint.exists():
        start_episode = agent.load_checkpoint(checkpoint) + 1
        if history_csv.exists():
            with history_csv.open(newline="") as existing_history:
                for row in csv.DictReader(existing_history):
                    value = float(row["validation_throughput_mbps"])
                    if math.isfinite(value):
                        best_throughput = max(best_throughput, value)

    append_history = args.resume and history_csv.exists()
    with history_csv.open("a" if append_history else "w", newline="") as history_handle:
        writer = csv.writer(history_handle)
        if not append_history:
            writer.writerow(
                [
                    "episode", "train_seed", "train_throughput_mbps", "train_reward",
                    "updates", "validation_throughput_mbps", "validation_probability",
                    "action_agreement", "stable_checks", "gamma",
                    "epsilon", "target_throughput_mbps", "discarded_tail",
                    "validation_zero_fraction", "validation_longest_zero_run",
                    "ideal_zero_fraction", "ideal_longest_zero_run",
                    "training_start_distance_m",
                ]
            )
        for episode in range(start_episode, args.max_episodes + 1):
            train_seed = args.train_seed + episode - 1
            training_start_distance = args.start_distance
            train_result = run_episode(
                agent,
                train_seed,
                args.training_segment_time,
                training_start_distance,
                args.moving_speed,
                f"offline-train-episode{episode:03d}",
                training=True,
                print_every=args.print_every,
                interface=interface,
            )
            if args.report_markdown is not None:
                write_markdown_report(
                    args.report_markdown,
                    train_result,
                    agent,
                    {
                        "episode": episode,
                        "network": "4-16-16-8",
                        "decision_unit": "one completed PPDU/A-MPDU",
                        "ampdu_enabled": True,
                        "batch_size": agent.batch_size,
                        "return_horizon": agent.return_horizon,
                        "update_start_step": agent.buffer_size,
                        "batch_logic": "10 samples; average by MCS; equal action weights",
                        "mcs_normalization": "(mcs + 1) / 8",
                        "reward": "throughput_mbps ** 3",
                        "gamma": agent.gamma,
                        "epsilon": agent.epsilon,
                        "learning_rate": args.learning_rate,
                        "simulation_time_s": args.training_segment_time,
                        "start_distance_m": training_start_distance,
                        "moving_speed_mps": args.moving_speed,
                        "ns3_seed": train_seed,
                        "agent_seed": args.agent_seed,
                    },
                )

            validation_throughput = math.nan
            validation_probability = math.nan
            agreement = math.nan
            if episode % args.validate_every == 0:
                validation = run_episode(
                    agent,
                    args.validation_seed,
                    args.simulation_time,
                    args.start_distance,
                    args.moving_speed,
                    f"offline-validation-episode{episode:03d}",
                    training=False,
                    print_every=args.print_every,
                    interface=interface,
                )
                validation_throughput = validation["throughput"]
                validation_probability = validation["mean_probability"]
                if validation_throughput > best_throughput:
                    best_throughput = validation_throughput
                    agent.save_policy(best_policy)

                if previous_validation is not None:
                    agreement = action_agreement(previous_validation["actions"], validation["actions"])
                    throughput_stable = (
                        abs(validation_throughput - previous_validation["throughput"])
                        <= args.throughput_tolerance
                    )
                    policy_stable = agreement >= args.agreement_threshold
                    concentrated = validation_probability >= args.probability_threshold
                    performance_good = validation_throughput >= target_throughput
                    tail_good = (
                        validation["zero_fraction"]
                        <= ideal_metrics["zero_fraction"] + args.zero_fraction_tolerance
                        and validation["longest_zero_run"]
                        <= ideal_metrics["longest_zero_run"] + args.zero_run_tolerance
                    )
                    stable_checks = stable_checks + 1 if all(
                        (throughput_stable, policy_stable, concentrated, performance_good, tail_good)
                    ) else 0
                previous_validation = validation
                converged = episode >= args.min_episodes and stable_checks >= args.stable_checks

            writer.writerow(
                [
                    episode,
                    train_seed,
                    train_result["throughput"],
                    train_result["mean_reward"],
                    train_result["updates"],
                    validation_throughput,
                    validation_probability,
                    agreement,
                    stable_checks,
                    agent.gamma,
                    agent.epsilon,
                    target_throughput,
                    train_result["discarded_tail"],
                    validation["zero_fraction"] if episode % args.validate_every == 0 else math.nan,
                    validation["longest_zero_run"] if episode % args.validate_every == 0 else math.nan,
                    ideal_metrics["zero_fraction"],
                    ideal_metrics["longest_zero_run"],
                    training_start_distance,
                ]
            )
            history_handle.flush()
            agent.save_checkpoint(checkpoint, episode)
            print(
                f"episode={episode} train_tp={train_result['throughput']:.3f} "
                f"train_reward={train_result['mean_reward']:.3f} updates={train_result['updates']} "
                f"discarded={train_result['discarded_tail']} "
                f"val_tp={validation_throughput:.3f} val_p={validation_probability:.3f} "
                f"agreement={agreement:.3f} target={target_throughput:.3f} stable={stable_checks}"
            )
            last_update = train_result["last_update"] or {}
            print(
                f"episode_metrics feedbacks={train_result['steps']} "
                f"windows={train_result['complete_windows']} "
                f"decisions={train_result['decisions']} "
                f"partial_window={train_result['partial_window_packets']} "
                f"action_counts={train_result['action_counts']} "
                f"last_loss={last_update.get('loss', math.nan):.6f} "
                f"return_mean={last_update.get('return_mean', math.nan):.6f} "
                f"return_std={last_update.get('return_std', math.nan):.6f}"
            )
            if converged:
                print(f"converged_at_episode={episode}")
                break

    if not best_policy.exists():
        agent.save_policy(best_policy)
    print(f"converged={int(converged)}")
    print(f"best_validation_throughput={best_throughput:.6f}")
    print(f"best_policy={best_policy}")
    print(f"history={history_csv}")
    parameter_count = sum(parameter.numel() for parameter in agent.policy.parameters())
    parameter_l2 = math.sqrt(
        sum(float((parameter.detach() ** 2).sum()) for parameter in agent.policy.parameters())
    )
    print(f"model_parameters={parameter_count}")
    print(f"model_parameter_l2={parameter_l2:.6f}")
    print(f"total_gradient_updates={agent.gradient_updates}")
    print(f"gamma={agent.gamma} epsilon={agent.epsilon}")
    del interface
    return 0 if converged else 2


if __name__ == "__main__":
    raise SystemExit(main())
