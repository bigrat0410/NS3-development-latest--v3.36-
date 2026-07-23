#!/usr/bin/env python3

import argparse
import csv
import random
import subprocess
import sys
from pathlib import Path

from offline_reinforce_model import ReinRateAgent
from offline_reinforce_train import EXECUTABLE, PREFIX, RESULTS, ROOT, run_episode
from offline_reinforce_train import throughput_metrics


def baseline_command(manager, seed, simulation_time, start_distance, moving_speed, output):
    return [
        str(EXECUTABLE),
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
        "--ns3::WifiMac::BE_MaxAmpduSize=0",
    ]


def main():
    parser = argparse.ArgumentParser(description="Evaluate RL, Minstrel-HT and Ideal over random seeds")
    parser.add_argument("--model", type=Path, default=RESULTS / f"{PREFIX}-best.pt")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--seedGenerator", dest="seed_generator", type=int, default=20260721)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=1.0)
    parser.add_argument("--movingSpeed", dest="moving_speed", type=float, default=0.5)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")

    rng = random.Random(args.seed_generator)
    seeds = rng.sample(range(1, 2_000_000_000), args.runs)
    agent = ReinRateAgent()
    agent.load_policy(args.model)
    summary_path = RESULTS / f"{PREFIX}-test-{args.runs}run-summary.csv"
    rows = []

    for index, seed in enumerate(seeds, start=1):
        run_tag = f"test-{args.runs}run-run{index:02d}"
        rl = run_episode(
            agent,
            seed,
            args.simulation_time,
            args.start_distance,
            args.moving_speed,
            f"offline-window20-episodic-reinforce-{run_tag}",
            training=False,
            log_decisions=index == 1,
        )
        ideal_csv = RESULTS / f"{PREFIX}-test-{args.runs}run-ideal-run{index:02d}-seed{seed}.csv"
        minstrel_csv = RESULTS / f"{PREFIX}-test-{args.runs}run-minstrel-run{index:02d}-seed{seed}.csv"
        subprocess.run(
            baseline_command(
                "ns3::IdealWifiManager", seed, args.simulation_time,
                args.start_distance, args.moving_speed, ideal_csv,
            ),
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            baseline_command(
                "ns3::MinstrelHtWifiManager", seed, args.simulation_time,
                args.start_distance, args.moving_speed, minstrel_csv,
            ),
            cwd=ROOT,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        ideal = throughput_metrics(ideal_csv)
        minstrel = throughput_metrics(minstrel_csv)
        rows.append({
            "run": index,
            "seed": seed,
            "reinforce_throughput_mbps": rl["throughput"],
            "minstrel_throughput_mbps": minstrel["mean"],
            "ideal_throughput_mbps": ideal["mean"],
            "reinforce_zero_fraction": rl["zero_fraction"],
            "minstrel_zero_fraction": minstrel["zero_fraction"],
            "ideal_zero_fraction": ideal["zero_fraction"],
            "reinforce_longest_zero_run": rl["longest_zero_run"],
            "minstrel_longest_zero_run": minstrel["longest_zero_run"],
            "ideal_longest_zero_run": ideal["longest_zero_run"],
            "reinforce_windows": rl["windows"],
            "reinforce_action_counts": " ".join(map(str, rl["action_counts"])),
        })
        print(
            f"run={index} seed={seed} reinforce={rl['throughput']:.6f} "
            f"minstrel={minstrel['mean']:.6f} ideal={ideal['mean']:.6f}",
            flush=True,
        )

    with summary_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    output_svg = RESULTS / f"{PREFIX}-test-{args.runs}run-average.svg"
    average_csv = RESULTS / f"{PREFIX}-test-{args.runs}run-average.csv"
    subprocess.run(
        [
            sys.executable,
            "-B",
            str(ROOT / "scratch/reproduction/result_figure.py"),
            "--ideal-glob", str(RESULTS / f"{PREFIX}-test-{args.runs}run-ideal-run*-seed*.csv"),
            "--minstrel-glob", str(RESULTS / f"{PREFIX}-test-{args.runs}run-minstrel-run*-seed*.csv"),
            "--reinrate-glob", str(RESULTS / f"reproduction-scenario1-offline-window20-episodic-reinforce-test-{args.runs}run-run??-seed*.csv"),
            "--reinrate-label", "Window-20 REINFORCE",
            "--output", str(output_svg),
            "--average-csv", str(average_csv),
            "--title", f"Scenario 1: {args.runs}-Run Frozen-Policy Average",
        ],
        cwd=ROOT,
        check=True,
    )
    for name in ("reinforce", "minstrel", "ideal"):
        values = [float(row[f"{name}_throughput_mbps"]) for row in rows]
        print(f"{name}_mean_throughput={sum(values) / len(values):.6f}")
    print(f"seeds={seeds}")
    print(f"summary_csv={summary_path}")
    print(f"average_csv={average_csv}")
    print(f"output_svg={output_svg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
