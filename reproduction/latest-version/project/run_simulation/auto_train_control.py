#!/usr/bin/env python3
"""Interactive controller for the offline window-20 REINFORCE policy.

Flow (all units are *episodes*; one episode = one 80 s ns-3 run over a few
thousand 20-packet decision windows):

  1. Ask for the initial number of episodes to train (or take --initialEpisodes).
  2. Train that many episodes, printing a progress line every --progressEvery
     episodes (default 50) and a full statistics block every --statsEvery
     episodes (default 200).
  3. At the end of each training chunk, print the statistics block and ask
     whether to keep training and for how many more episodes.
  4. When you choose to stop, the final policy is saved and evaluated against
     Minstrel-HT and Ideal over --testRuns random seeds (default 20); the
     per-distance average of all three is drawn on a single SVG in
     my-project-results via the existing offline_reinforce_eval.py.

Training artifacts use an "-auto" name suffix so this never overwrites the
hand-run reproduction-scenario1-offline-window20-episodic-reinforce-*.pt files.
"""

import argparse
import csv
import functools
import math
import random
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

# This controller lives in reproduction/run_simulation/; the offline modules
# live one level up in reproduction/ (or project/ in latest-version).
PROJECT_DIRECTORY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_DIRECTORY))

import offline_reinforce_train as T  # noqa: E402
from offline_full_rbf_train import (  # noqa: E402
    RbfImportanceEntropyAgent,
    rbf_policy_state,
    RBF_CENTERS_DB,
    ENTROPY_COEF,
)


AUTO_PREFIX = f"{T.PREFIX}-rbf-auto"
AUTO_LABEL_BASE = "offline-window20-episodic-reinforce-auto"
HISTORY_FIELDS = [
    "episode", "train_seed", "train_throughput_mbps", "mean_window_reward",
    "total_reward", "windows", "packets", "partial_windows", "updates",
    "loss", "return_mean", "return_std", "return_min", "return_max",
    "gradient_norm", "parameter_norm_before", "parameter_norm_after",
    "exploration_count", "action_counts", "validation_throughput_mbps",
    "validation_probability", "validation_zero_fraction",
    "validation_longest_zero_run", "gamma", "epsilon", "learning_rate",
]


def ask_int(prompt, default, minimum=1):
    """Read a positive integer from the terminal, falling back to default."""
    if not sys.stdin.isatty():
        print(f"{prompt} [non-interactive -> {default}]", flush=True)
        return default
    while True:
        raw = input(f"{prompt} [{default}]: ").strip()
        if not raw:
            return default
        try:
            value = int(raw)
        except ValueError:
            print("  请输入一个整数。")
            continue
        if value < minimum:
            print(f"  请输入 >= {minimum} 的整数。")
            continue
        return value


def ask_yes_no(prompt, default=True):
    # Non-interactive (piped/cron): always stop, so a lost stdin can never
    # spin the "continue?" prompt into an unbounded training loop.
    if not sys.stdin.isatty():
        print(f"{prompt} [non-interactive -> n]", flush=True)
        return False
    suffix = "Y/n" if default else "y/N"
    while True:
        raw = input(f"{prompt} [{suffix}]: ").strip().lower()
        if not raw:
            return default
        if raw in ("y", "yes", "是", "继续"):
            return True
        if raw in ("n", "no", "否", "停止", "结束"):
            return False
        print("  请输入 y 或 n。")


def progress_line(episode, target, train, update):
    print(
        f"[进度] episode={episode}/{target} "
        f"train_tp={train['throughput']:.3f}Mbps "
        f"reward={train['mean_window_reward']:.4f} "
        f"loss={update['loss']:.4f} "
        f"windows={train['windows']} "
        f"explore={train['exploration_count']}",
        flush=True,
    )


def statistics_block(recent, episode, agent):
    """Aggregate the episodes recorded since the last statistics print."""
    if not recent:
        return
    n = len(recent)
    tp = [row["throughput"] for row in recent]
    reward = [row["mean_window_reward"] for row in recent]
    loss = [row["loss"] for row in recent]
    grad = [row["gradient_norm"] for row in recent]
    explore = [row["exploration_count"] for row in recent]
    action_totals = [0] * 8
    for row in recent:
        for i, count in enumerate(row["action_counts"]):
            action_totals[i] += count
    total_actions = sum(action_totals) or 1
    val = [row["validation"] for row in recent if row["validation"] is not None]

    span = f"{recent[0]['episode']}-{recent[-1]['episode']}"
    print("=" * 68, flush=True)
    print(f"[统计] episodes {span}  (共 {n} 个 episode，累计梯度更新 {agent.gradient_updates})", flush=True)
    print(f"  train throughput  均值 {sum(tp)/n:7.3f}  最新 {tp[-1]:7.3f}  "
          f"最好 {max(tp):7.3f} Mbps", flush=True)
    print(f"  window reward     均值 {sum(reward)/n:7.4f}  最新 {reward[-1]:7.4f}", flush=True)
    print(f"  loss              均值 {sum(loss)/n:7.4f}  最新 {loss[-1]:7.4f}", flush=True)
    print(f"  gradient norm     均值 {sum(grad)/n:7.4f}  最新 {grad[-1]:7.4f}", flush=True)
    print(f"  exploration/ep    均值 {sum(explore)/n:7.1f}", flush=True)
    if val:
        print(f"  validation tp     均值 {sum(val)/len(val):7.3f}  最新 {val[-1]:7.3f} Mbps", flush=True)
    dist = "  ".join(f"MCS{i}:{100*c/total_actions:4.1f}%" for i, c in enumerate(action_totals))
    print(f"  动作分布   {dist}", flush=True)
    print("=" * 68, flush=True)


def baseline_command(
    manager, seed, simulation_time, start_distance, moving_speed, output,
    be_max_ampdu_size,
):
    """A non-AI ns-3 run for a stock rate manager (Minstrel-HT / Ideal)."""
    return [
        str(T.EXECUTABLE),
        f"--simulationTime={simulation_time}",
        "--trafficStartTime=0.5",
        f"--startDistance={start_distance}",
        f"--movingSpeed={moving_speed}",
        f"--rateManager={manager}",
        "--lossModel=log-distance",
        "--pathLossExponent=3",
        "--referenceLoss=66.6777",
        "--dataRate=60Mbps",
        "--packetSize=1420",
        "--sampleInterval=0.5",
        f"--Seed={seed}",
        "--Run=1",
        f"--outputFile={output}",
        f"--ns3::WifiMac::BE_MaxAmpduSize={be_max_ampdu_size}",
    ]


def run_test(final_policy, args, interface):
    """Evaluate the final policy against Minstrel-HT and Ideal over N seeds.

    Reuses the caller's single ns3-ai interface for every RL run. A second
    Ns3AiMsgInterfaceImpl in the same process fails with a boost interprocess
    library_error (the named segment is not released synchronously on del), so
    the whole program must share exactly one interface -- the same one the
    training loop used. Baselines are plain ns-3 runs (no shared memory).
    The per-distance average of all three is drawn on one SVG via the
    existing result_figure.py.
    """
    runs = args.test_runs
    mode = T.ampdu_mode(args.be_max_ampdu_size)
    state_encoder = functools.partial(
        rbf_policy_state,
        max_reference_mbps=T.max_reference_mbps(args.be_max_ampdu_size),
    )
    run_prefix = f"{AUTO_PREFIX}-{mode}"
    run_label_base = f"{AUTO_LABEL_BASE}-{mode}"
    print(f"\n[测试] 用最终模型 {final_policy.name} 与 Minstrel-HT / Ideal 对比 "
          f"{runs} 个随机种子，取平均后画图...\n", flush=True)

    agent = RbfImportanceEntropyAgent(
        state_size=len(RBF_CENTERS_DB) + 5,
        lr=args.learning_rate,
        gamma=args.gamma,
        epsilon=0.0,
        entropy_coef=ENTROPY_COEF,
    )
    agent.load_policy(final_policy)
    rng = random.Random(args.test_seed_generator)
    seeds = rng.sample(range(1, 2_000_000_000), runs)

    summary_path = T.RESULTS / f"{run_prefix}-test-{runs}run-summary.csv"
    rows = []
    for index, seed in enumerate(seeds, start=1):
        run_tag = f"test-{runs}run-run{index:02d}"
        rl = T.run_episode(
            agent, seed, args.simulation_time,
            args.start_distance, args.moving_speed,
            f"{run_label_base}-{run_tag}",
            training=False, interface=interface, log_decisions=index == 1,
            state_encoder=state_encoder,
            be_max_ampdu_size=args.be_max_ampdu_size,
        )
        ideal_csv = T.RESULTS / f"{run_prefix}-test-{runs}run-ideal-run{index:02d}-seed{seed}.csv"
        minstrel_csv = T.RESULTS / f"{run_prefix}-test-{runs}run-minstrel-run{index:02d}-seed{seed}.csv"
        subprocess.run(
            baseline_command("ns3::IdealWifiManager", seed, args.simulation_time,
                             args.start_distance, args.moving_speed, ideal_csv,
                             args.be_max_ampdu_size),
            cwd=T.ROOT, check=True, stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            baseline_command("ns3::MinstrelHtWifiManager", seed, args.simulation_time,
                             args.start_distance, args.moving_speed, minstrel_csv,
                             args.be_max_ampdu_size),
            cwd=T.ROOT, check=True, stdout=subprocess.DEVNULL,
        )
        ideal = T.throughput_metrics(ideal_csv)
        minstrel = T.throughput_metrics(minstrel_csv)
        rows.append({
            "run": index, "seed": seed,
            "reinforce_throughput_mbps": rl["throughput"],
            "minstrel_throughput_mbps": minstrel["mean"],
            "ideal_throughput_mbps": ideal["mean"],
        })
        print(f"  run={index}/{runs} seed={seed} "
              f"reinforce={rl['throughput']:.3f} "
              f"minstrel={minstrel['mean']:.3f} "
              f"ideal={ideal['mean']:.3f} Mbps", flush=True)

    with summary_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    output_svg = T.RESULTS / f"{AUTO_PREFIX}-test-{runs}run-average.svg"
    average_csv = T.RESULTS / f"{AUTO_PREFIX}-test-{runs}run-average.csv"
    subprocess.run(
        [sys.executable, "-B", str(T.PLOTTING_DIRECTORY / "result_figure.py"),
         "--ideal-glob", str(T.RESULTS / f"{run_prefix}-test-{runs}run-ideal-run*-seed*.csv"),
         "--minstrel-glob", str(T.RESULTS / f"{run_prefix}-test-{runs}run-minstrel-run*-seed*.csv"),
         "--reinrate-glob", str(T.RESULTS / f"reproduction-scenario1-{run_label_base}-test-{runs}run-run??-seed*.csv"),
         "--reinrate-label", "Window-20 REINFORCE",
         "--output", str(output_svg),
         "--average-csv", str(average_csv),
         "--title", f"Scenario 1: {runs}-Run Frozen-Policy Average"],
        cwd=T.ROOT, check=True,
    )
    for name in ("reinforce", "minstrel", "ideal"):
        values = [row[f"{name}_throughput_mbps"] for row in rows]
        print(f"  {name}_mean_throughput={sum(values)/len(values):.6f} Mbps", flush=True)
    print(f"  summary_csv={summary_path}", flush=True)
    print(f"  average_csv={average_csv}", flush=True)
    print(f"  对比图 SVG = {output_svg}", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Interactive REINFORCE training controller")
    parser.add_argument("--initialEpisodes", dest="initial_episodes", type=int, default=None,
                        help="初始训练 episode 数（不给则交互式询问，默认 200）")
    parser.add_argument("--progressEvery", dest="progress_every", type=int, default=50)
    parser.add_argument("--statsEvery", dest="stats_every", type=int, default=200)
    parser.add_argument("--continueDefault", dest="continue_default", type=int, default=200,
                        help="每次询问“继续训练”时提示的默认追加 episode 数")
    parser.add_argument("--testRuns", dest="test_runs", type=int, default=20)
    parser.add_argument("--testSeedGenerator", dest="test_seed_generator", type=int, default=20260721)
    parser.add_argument("--validateEvery", dest="validate_every", type=int, default=0,
                        help="每多少 episode 跑一次验证并保存 best（0=关闭，测试用 final）")
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=1.0)
    parser.add_argument("--movingSpeed", dest="moving_speed", type=float, default=0.5)
    parser.add_argument("--trainSeed", dest="train_seed", type=int, default=774015)
    parser.add_argument("--validationSeed", dest="validation_seed", type=int, default=884015)
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument("--learningRate", dest="learning_rate", type=float, default=1e-4)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument(
        "--beMaxAmpduSize", dest="be_max_ampdu_size", type=int,
        default=T.DEFAULT_BE_MAX_AMPDU_SIZE,
        help="BE A-MPDU maximum size in bytes (0 disables aggregation)",
    )
    parser.add_argument("--resume", action="store_true", help="从上次的 -auto 检查点续训")
    args = parser.parse_args()
    mode = T.ampdu_mode(args.be_max_ampdu_size)
    state_encoder = functools.partial(
        rbf_policy_state,
        max_reference_mbps=T.max_reference_mbps(args.be_max_ampdu_size),
    )

    T.RESULTS.mkdir(exist_ok=True)
    random.seed(args.agent_seed)
    np.random.seed(args.agent_seed)
    torch.manual_seed(args.agent_seed)

    agent = RbfImportanceEntropyAgent(
        state_size=len(RBF_CENTERS_DB) + 5,
        lr=args.learning_rate,
        gamma=args.gamma,
        epsilon=args.epsilon,
        entropy_coef=ENTROPY_COEF,
    )
    run_prefix = f"{AUTO_PREFIX}-{mode}"
    run_label_base = f"{AUTO_LABEL_BASE}-{mode}"
    checkpoint = T.RESULTS / f"{run_prefix}-checkpoint.pt"
    best_policy = T.RESULTS / f"{run_prefix}-best.pt"
    final_policy = T.RESULTS / f"{run_prefix}-final.pt"
    history_csv = T.RESULTS / f"{run_prefix}-training.csv"
    history_svg = T.RESULTS / f"{run_prefix}-training-statistics.svg"

    start_episode = 1
    best_throughput = -math.inf
    append = False
    if args.resume and checkpoint.exists():
        start_episode = agent.load_checkpoint(checkpoint) + 1
        append = history_csv.exists()
        if append:
            with history_csv.open(newline="") as handle:
                for row in csv.DictReader(handle):
                    value = float(row["validation_throughput_mbps"])
                    if math.isfinite(value):
                        best_throughput = max(best_throughput, value)
        print(f"[续训] 从 episode {start_episode} 继续（已完成 {start_episode - 1} 个）。", flush=True)

    print("\n==== 离线 Window-20 REINFORCE 交互式训练 ====", flush=True)
    print(f"    模型/历史前缀: {run_prefix} ({mode})", flush=True)
    print(f"    进度每 {args.progress_every} episode 打印，统计每 {args.stats_every} episode 打印\n", flush=True)

    initial = args.initial_episodes
    if initial is None:
        initial = ask_int("请输入初始训练的 episode 步数", default=200)
    target = start_episode - 1 + initial

    interface = T.create_interface()
    recent = []  # episodes accumulated since the last statistics block
    stopped_early = False
    try:
        with history_csv.open("a" if append else "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=HISTORY_FIELDS)
            if not append:
                writer.writeheader()

            episode = start_episode
            while episode <= target:
                train_seed = args.train_seed + episode - 1
                keep_trace = episode == 1 or episode % 50 == 0
                train = T.run_episode(
                    agent, train_seed, args.simulation_time,
                    args.start_distance, args.moving_speed,
                    f"{run_label_base}-train-episode{episode:04d}",
                    training=True, interface=interface, log_decisions=keep_trace,
                    state_encoder=state_encoder,
                    be_max_ampdu_size=args.be_max_ampdu_size,
                )
                if train["updates"] != 1:
                    raise RuntimeError(f"episode {episode} made {train['updates']} updates, expected 1")
                update = train["last_update"]

                validation = None
                if args.validate_every and episode % args.validate_every == 0:
                    validation = T.run_episode(
                        agent, args.validation_seed, args.simulation_time,
                        args.start_distance, args.moving_speed,
                        f"{run_label_base}-validation-episode{episode:04d}",
                        training=False, interface=interface, log_decisions=True,
                        state_encoder=state_encoder,
                        be_max_ampdu_size=args.be_max_ampdu_size,
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

                recent.append({
                    "episode": episode,
                    "throughput": train["throughput"],
                    "mean_window_reward": train["mean_window_reward"],
                    "loss": update["loss"],
                    "gradient_norm": update["gradient_norm"],
                    "exploration_count": train["exploration_count"],
                    "action_counts": train["action_counts"],
                    "validation": validation["throughput"] if validation else None,
                })

                if episode % args.progress_every == 0:
                    progress_line(episode, target, train, update)
                stats_due = episode % args.stats_every == 0
                if stats_due:
                    statistics_block(recent, episode, agent)
                    recent = []

                # At the end of a chunk, show stats (if not just shown) and ask.
                if episode == target:
                    if not stats_due:
                        statistics_block(recent, episode, agent)
                        recent = []
                    agent.save_policy(final_policy)
                    _plot_history_safe(history_csv, history_svg)
                    if ask_yes_no(f"已训练到 episode {episode}。是否继续训练？", default=True):
                        more = ask_int("再训练多少个 episode", default=args.continue_default)
                        target += more
                    else:
                        stopped_early = True
                        break
                episode += 1

        agent.save_policy(final_policy)
        if not best_policy.exists():
            agent.save_policy(best_policy)
        _plot_history_safe(history_csv, history_svg)

        print(f"\n[完成] 训练结束（{'手动停止' if stopped_early else '达到目标'}），"
              f"最终模型: {final_policy}", flush=True)
        print(f"        训练历史: {history_csv}", flush=True)
        print(f"        训练统计图: {history_svg}", flush=True)

        # Reuse the SAME interface for the test -- a second one in this
        # process would fail with a boost interprocess library_error.
        run_test(final_policy, args, interface)
    finally:
        del interface

    print("\n全部完成。对比图已输出到 my-project-results/。", flush=True)
    return 0


def _plot_history_safe(history_csv, output_svg):
    """Draw the training-statistics SVG; never let a plotting hiccup stop training."""
    try:
        subprocess.run(
            [sys.executable, str(T.PLOTTING_DIRECTORY / "plot_training_history.py"),
             "--input", str(history_csv), "--output", str(output_svg)],
            cwd=T.ROOT, check=True,
        )
    except Exception as error:  # noqa: BLE001
        print(f"[warn] 训练统计图绘制失败: {error}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
