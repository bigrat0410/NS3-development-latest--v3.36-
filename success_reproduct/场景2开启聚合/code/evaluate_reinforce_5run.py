#!/usr/bin/env python3
"""Run five independent Scenario 2 comparisons and plot each trajectory."""

import csv
import functools
import html
import os
import random
import subprocess
from pathlib import Path

import offline_reinforce_train as T
from offline_full_rbf_train import (
    ENTROPY_COEF,
    RBF_CENTERS_DB,
    RbfImportanceEntropyAgent,
    rbf_policy_state,
)


ARCHIVE = Path(__file__).resolve().parents[1]
RESULTS = ARCHIVE / "results" / "comparison_5run"
RAW = RESULTS / "raw"
MODEL = ARCHIVE / "models" / "reinforce-episode1401-final.pt"
PREFIX = "scenario2-ampdu-on-per-ampdu-episode1401"
RUNS = 5
SEED_GENERATOR = 20260724
AMPDU_SIZE = 65535
SIMULATION_TIME = 40.0
START_DISTANCE = 1.0
MOVING_SPEED = 3.0
RANDOM_WALK_MIN_DISTANCE = 0.0
RANDOM_WALK_MAX_DISTANCE = 24.0
RANDOM_WALK_DIRECTION_INTERVAL = 2.0


def baseline_command(manager, seed, output):
    return [
        str(T.EXECUTABLE),
        "--scenario=2",
        f"--simulationTime={SIMULATION_TIME}",
        "--trafficStartTime=0.5",
        f"--startDistance={START_DISTANCE}",
        f"--movingSpeed={MOVING_SPEED}",
        f"--randomWalkMinDistance={RANDOM_WALK_MIN_DISTANCE}",
        f"--randomWalkMaxDistance={RANDOM_WALK_MAX_DISTANCE}",
        f"--randomWalkSpeed={MOVING_SPEED}",
        f"--randomWalkDirectionInterval={RANDOM_WALK_DIRECTION_INTERVAL}",
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
        f"--ns3::WifiMac::BE_MaxAmpduSize={AMPDU_SIZE}",
    ]


def read_curve(path):
    with Path(path).open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    return (
        [float(row["time_s"]) for row in rows],
        [float(row["throughput_mbps"]) for row in rows],
    )


def write_svg(run, seed, series, output):
    width, height = 960, 560
    left, right, top, bottom = 78, 28, 42, 66
    plot_width = width - left - right
    plot_height = height - top - bottom
    x_max = SIMULATION_TIME
    y_max = 65.0
    sx = lambda value: left + value / x_max * plot_width
    sy = lambda value: top + plot_height - value / y_max * plot_height
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
    ]
    for value in range(0, 41, 5):
        x = sx(value)
        svg.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_height}" stroke="#e5e7eb"/>')
        svg.append(f'<text x="{x:.2f}" y="{height - 38}" text-anchor="middle" font-size="12">{value}</text>')
    for value in range(0, 66, 5):
        y = sy(value)
        svg.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_width}" y2="{y:.2f}" stroke="#e5e7eb"/>')
        svg.append(f'<text x="{left - 9}" y="{y + 4:.2f}" text-anchor="end" font-size="12">{value}</text>')
    svg.extend([
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}" stroke="#111827"/>',
        f'<line x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" y2="{top + plot_height}" stroke="#111827"/>',
    ])
    for label, times, values, mean, color in series:
        points = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(times, values))
        svg.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2"/>')
    title = html.escape(f"Scenario 2 Run {run}: A-MPDU Enabled (seed {seed})")
    svg.extend([
        f'<text x="{width / 2}" y="25" text-anchor="middle" font-size="18" font-weight="bold">{title}</text>',
        f'<text x="{width / 2}" y="{height - 10}" text-anchor="middle" font-size="14">Simulation time (s)</text>',
        f'<text x="20" y="{height / 2}" text-anchor="middle" font-size="14" transform="rotate(-90 20 {height / 2})">Throughput (Mbps)</text>',
    ])
    for index, (label, _, _, mean, color) in enumerate(series):
        x, y = 620, 58 + index * 24
        svg.append(f'<line x1="{x}" y1="{y}" x2="{x + 34}" y2="{y}" stroke="{color}" stroke-width="3"/>')
        svg.append(f'<text x="{x + 42}" y="{y + 4}" font-size="13">{html.escape(label)} (mean {mean:.3f} Mbps)</text>')
    svg.append("</svg>")
    output.write_text("\n".join(svg), encoding="utf-8")


def main():
    if T.BUILD_PROFILE != "optimized":
        raise SystemExit("Set REPRODUCTION_NS3_PROFILE=optimized for this archive")
    if not MODEL.is_file():
        raise SystemExit(f"missing model: {MODEL}")
    RAW.mkdir(parents=True, exist_ok=True)
    # Keep all three algorithms' raw trajectories inside this archive.
    T.RESULTS = RAW
    agent = RbfImportanceEntropyAgent(
        len(RBF_CENTERS_DB) + 5,
        lr=1e-4,
        gamma=0.99,
        epsilon=0.0,
        entropy_coef=ENTROPY_COEF,
        device="cpu",
    )
    agent.load_policy(MODEL)
    state_encoder = functools.partial(
        rbf_policy_state,
        max_reference_mbps=T.max_reference_mbps(AMPDU_SIZE),
    )
    seeds = random.Random(SEED_GENERATOR).sample(range(1, 2_000_000_000), RUNS)
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(T.BUILD_DIRECTORY / "lib")
    rows = []
    interface = T.create_interface()
    try:
        for index, seed in enumerate(seeds, 1):
            tag = f"{PREFIX}-reinforce-run{index:02d}"
            reinforce = T.run_episode(
                agent,
                seed,
                SIMULATION_TIME,
                START_DISTANCE,
                MOVING_SPEED,
                tag,
                training=False,
                interface=interface,
                log_decisions=False,
                print_every=10**9,
                state_encoder=state_encoder,
                be_max_ampdu_size=AMPDU_SIZE,
                decision_per_ampdu=True,
                collect_segments=False,
                scenario=2,
                random_walk_min_distance=RANDOM_WALK_MIN_DISTANCE,
                random_walk_max_distance=RANDOM_WALK_MAX_DISTANCE,
                random_walk_direction_interval=RANDOM_WALK_DIRECTION_INTERVAL,
            )
            reinforce_path = Path(reinforce["throughput_csv"])
            ideal_path = RAW / f"{PREFIX}-ideal-run{index:02d}-seed{seed}.csv"
            minstrel_path = RAW / f"{PREFIX}-minstrel-run{index:02d}-seed{seed}.csv"
            subprocess.run(
                baseline_command("ns3::IdealWifiManager", seed, ideal_path),
                cwd=T.ROOT, env=environment, check=True, stdout=subprocess.DEVNULL,
            )
            subprocess.run(
                baseline_command("ns3::MinstrelHtWifiManager", seed, minstrel_path),
                cwd=T.ROOT, env=environment, check=True, stdout=subprocess.DEVNULL,
            )
            ideal_mean = T.throughput_metrics(ideal_path)["mean"]
            minstrel_mean = T.throughput_metrics(minstrel_path)["mean"]
            row = {
                "run": index,
                "seed": seed,
                "reinforce_mbps": reinforce["throughput"],
                "minstrel_ht_mbps": minstrel_mean,
                "ideal_mbps": ideal_mean,
                "reinforce_decisions": reinforce["windows"],
            }
            rows.append(row)
            rt, ry = read_curve(reinforce_path)
            mt, my = read_curve(minstrel_path)
            it, iy = read_curve(ideal_path)
            svg = RESULTS / f"{PREFIX}-run{index:02d}-seed{seed}.svg"
            write_svg(index, seed, [
                ("reinforce", rt, ry, reinforce["throughput"], "#15803d"),
                ("Minstrel-HT", mt, my, minstrel_mean, "#dc2626"),
                ("Ideal", it, iy, ideal_mean, "#1d4ed8"),
            ], svg)
            print(
                f"run={index:02d}/{RUNS} seed={seed} "
                f"reinforce={reinforce['throughput']:.6f} "
                f"minstrel_ht={minstrel_mean:.6f} ideal={ideal_mean:.6f} "
                f"svg={svg.name}", flush=True,
            )
    finally:
        del interface

    summary = RESULTS / f"{PREFIX}-summary.csv"
    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f"summary={summary}")


if __name__ == "__main__":
    main()
