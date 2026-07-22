#!/usr/bin/env python3

import argparse
import csv
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from offline_reinforce_model import ReinRateAgent
from offline_reinforce_train import EXECUTABLE, RESULTS, ROOT, run_episode
from offline_reinforce_train import throughput_metrics


def write_evaluation_report(path, reinforcement, ideal_metrics, minstrel_metrics, args):
    with Path(path).open("w", encoding="utf-8") as report:
        report.write("# Offline REINFORCE 冻结推理测试报告\n\n")
        report.write(f"生成时间：{datetime.now().astimezone().isoformat()}\n\n")
        report.write("## 测试配置\n\n```text\n")
        config = {
            "model": args.model,
            "seed": args.seed,
            "simulation_time_s": args.simulation_time,
            "start_distance_m": args.start_distance,
            "moving_speed_mps": args.moving_speed,
            "inference": "deterministic argmax",
            "ampdu_enabled": True,
            "decision_unit": "one completed PPDU/A-MPDU",
            "learning_rate_during_training": 0.001,
            "gamma": 0.0,
        }
        report.write("    ".join(f"{key} = {value}" for key, value in config.items()) + "\n")
        report.write("```\n\n## 测试结果\n\n```text\n")
        summary = {
            "reinforce_throughput_mbps": f"{reinforcement['throughput']:.9f}",
            "ideal_throughput_mbps": f"{ideal_metrics['mean']:.9f}",
            "minstrel_throughput_mbps": f"{minstrel_metrics['mean']:.9f}",
            "reinforce_zero_fraction": f"{reinforcement['zero_fraction']:.9f}",
            "ideal_zero_fraction": f"{ideal_metrics['zero_fraction']:.9f}",
            "minstrel_zero_fraction": f"{minstrel_metrics['zero_fraction']:.9f}",
            "reinforce_longest_zero_run": reinforcement["longest_zero_run"],
            "feedbacks": reinforcement["steps"],
            "decisions": reinforcement["decisions"],
            "action_counts": reinforcement["action_counts"],
            "mean_max_probability": f"{reinforcement['mean_probability']:.9f}",
        }
        report.write("    ".join(f"{key} = {value}" for key, value in summary.items()) + "\n")
        report.write("```\n\n## 逐聚合包推理日志\n\n")
        report.write("每行参数以四个空格横向分隔。\n\n")
        fields = [
            "feedback_step", "decision", "simulation_time_s", "aggregate_mpdus",
            "successful_mpdus", "failed_mpdus", "mcs", "next_mcs", "cw",
            "throughput_mbps", "snr_linear", "state_snr", "state_cw", "state_mcs",
            "state_throughput", "reward", "max_probability", "probabilities",
        ]
        with Path(reinforcement["decision_csv"]).open(newline="") as source:
            for row in csv.DictReader(source):
                report.write("```text\n")
                report.write("    ".join(f"{key} = {row[key]}" for key in fields) + "\n")
                report.write("```\n\n")
        report.write("## 输出文件\n\n")
        report.write(f"RL吞吐量CSV：`{reinforcement['throughput_csv']}`\n\n")
        report.write(f"RL决策CSV：`{reinforcement['decision_csv']}`\n")


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
    ]


def main():
    parser = argparse.ArgumentParser(description="Evaluate a frozen Scenario 1 policy")
    parser.add_argument(
        "--model",
        type=Path,
        default=RESULTS / "reproduction-scenario1-offline-reinforce-h10-best.pt",
    )
    parser.add_argument("--Seed", dest="seed", type=int, default=994015)
    parser.add_argument("--simulationTime", dest="simulation_time", type=float, default=80.0)
    parser.add_argument("--startDistance", dest="start_distance", type=float, default=1.0)
    parser.add_argument("--movingSpeed", dest="moving_speed", type=float, default=0.5)
    parser.add_argument("--reportMarkdown", dest="report_markdown", type=Path)
    args = parser.parse_args()

    agent = ReinRateAgent()
    agent.load_policy(args.model)
    reinforcement = run_episode(
        agent,
        args.seed,
        args.simulation_time,
        args.start_distance,
        args.moving_speed,
        "offline-reinforce-h10-test",
        training=False,
    )

    ideal_csv = RESULTS / f"reproduction-scenario1-offline-test-ideal-seed{args.seed}.csv"
    minstrel_csv = RESULTS / f"reproduction-scenario1-offline-test-minstrel-seed{args.seed}.csv"
    subprocess.run(
        baseline_command(
            "ns3::IdealWifiManager", args.seed, args.simulation_time,
            args.start_distance, args.moving_speed, ideal_csv,
        ),
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        baseline_command(
            "ns3::MinstrelHtWifiManager", args.seed, args.simulation_time,
            args.start_distance, args.moving_speed, minstrel_csv,
        ),
        cwd=ROOT,
        check=True,
    )

    output_svg = RESULTS / f"reproduction-scenario1-offline-reinforce-h10-test-seed{args.seed}.svg"
    output_csv = RESULTS / f"reproduction-scenario1-offline-reinforce-h10-comparison-seed{args.seed}.csv"
    subprocess.run(
        [
            sys.executable,
            "-B",
            str(ROOT / "scratch/reproduction/result_figure.py"),
            "--ideal-glob",
            str(ideal_csv),
            "--minstrel-glob",
            str(minstrel_csv),
            "--reinrate-csv",
            str(reinforcement["throughput_csv"]),
            "--reinrate-label",
            "H10 REINFORCE",
            "--output",
            str(output_svg),
            "--average-csv",
            str(output_csv),
            "--title",
            "Scenario 1: H10 REINFORCE, Per-A-MPDU Decisions",
        ],
        cwd=ROOT,
        check=True,
    )
    print(f"reinforce_throughput={reinforcement['throughput']:.6f}")
    print(f"reinforce_mean_probability={reinforcement['mean_probability']:.6f}")
    print(f"reinforce_feedbacks={reinforcement['steps']}")
    print(f"reinforce_windows={reinforcement['complete_windows']}")
    print(f"reinforce_decisions={reinforcement['decisions']}")
    print(f"reinforce_action_counts={reinforcement['action_counts']}")
    print(f"reinforce_zero_fraction={reinforcement['zero_fraction']:.6f}")
    print(f"reinforce_longest_zero_run={reinforcement['longest_zero_run']}")
    ideal_metrics = throughput_metrics(ideal_csv)
    minstrel_metrics = throughput_metrics(minstrel_csv)
    if args.report_markdown is not None:
        write_evaluation_report(
            args.report_markdown,
            reinforcement,
            ideal_metrics,
            minstrel_metrics,
            args,
        )
    print(f"ideal_throughput={ideal_metrics['mean']:.6f}")
    print(f"ideal_zero_fraction={ideal_metrics['zero_fraction']:.6f}")
    print(f"minstrel_throughput={minstrel_metrics['mean']:.6f}")
    print(f"minstrel_zero_fraction={minstrel_metrics['zero_fraction']:.6f}")
    print(f"output_svg={output_svg}")
    print(f"output_csv={output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
