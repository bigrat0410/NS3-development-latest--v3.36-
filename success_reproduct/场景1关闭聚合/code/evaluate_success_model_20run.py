#!/usr/bin/env python3
"""Compare the frozen 1000-episode RBF policy with Ideal and Minstrel-HT."""

import csv
import os
import random
import subprocess
from pathlib import Path

from offline_full_rbf_train import RBF_CENTERS_DB, RbfImportanceEntropyAgent, rbf_policy_state
from offline_reinforce_train import BUILD_DIRECTORY, RESULTS, ROOT, create_interface, run_episode, throughput_metrics


MODEL = (
    ROOT / "scratch/success_reproduct/models/current_1000/"
    "reproduction-scenario1-offline-full-rbf05dbadv-piq-entropy05-"
    "rewardmcs2over9-500-final.pt"
)
PREFIX = "success-reproduct-current1000-20run"


def baseline_command(manager, seed, output):
    executable = BUILD_DIRECTORY / "scratch/reproduction/ns3.36.1-two-node-ht-default"
    return [
        str(executable),
        "--simulationTime=80.0",
        "--trafficStartTime=0.5",
        "--startDistance=1.0",
        "--movingSpeed=0.5",
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


def read_curve(path):
    with Path(path).open(newline="") as handle:
        return list(csv.DictReader(handle))


def average_curves(paths):
    curves = [read_curve(path) for path in paths]
    count = min(map(len, curves))
    xs = [sum(float(curve[i]["distance_m"]) for curve in curves) / len(curves) for i in range(count)]
    ys = [sum(float(curve[i]["throughput_mbps"]) for curve in curves) / len(curves) for i in range(count)]
    return xs, ys


def marker(svg, kind, x, y, color):
    if kind == "circle":
        svg.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3" fill="white" stroke="{color}" stroke-width="1.5"/>')
    elif kind == "triangle":
        points = f"{x:.2f},{y-3.5:.2f} {x-3.5:.2f},{y+3:.2f} {x+3.5:.2f},{y+3:.2f}"
        svg.append(f'<polygon points="{points}" fill="white" stroke="{color}" stroke-width="1.5"/>')
    else:
        svg.append(f'<path d="M {x-3:.2f} {y-3:.2f} L {x+3:.2f} {y+3:.2f} M {x-3:.2f} {y+3:.2f} L {x+3:.2f} {y-3:.2f}" stroke="{color}" stroke-width="1.5"/>')


def write_svg(series, output):
    width, height = 920, 540
    left, right, top, bottom = 75, 30, 35, 65
    plot_width, plot_height = width - left - right, height - top - bottom
    x_max = 41.0
    y_max = max(35.0, max(max(ys) for _, _, ys, _, _, _ in series) * 1.05)
    sx = lambda value: left + value / x_max * plot_width
    sy = lambda value: top + plot_height - value / y_max * plot_height
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
    ]
    for value in range(0, 41, 5):
        x = sx(value)
        svg.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top+plot_height}" stroke="#e5e7eb"/>')
        svg.append(f'<text x="{x:.2f}" y="{height-38}" text-anchor="middle" font-size="12">{value}</text>')
    for value in range(0, int(y_max) + 1, 5):
        y = sy(value)
        svg.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left+plot_width}" y2="{y:.2f}" stroke="#e5e7eb"/>')
        svg.append(f'<text x="{left-9}" y="{y+4:.2f}" text-anchor="end" font-size="12">{value}</text>')
    svg.extend([
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top+plot_height}" stroke="#111827"/>',
        f'<line x1="{left}" y1="{top+plot_height}" x2="{left+plot_width}" y2="{top+plot_height}" stroke="#111827"/>',
    ])
    for label, xs, ys, color, kind, mean in series:
        points = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(xs, ys))
        svg.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2"/>')
        for index in range(0, len(xs), 4):
            marker(svg, kind, sx(xs[index]), sy(ys[index]), color)
    svg.extend([
        f'<text x="{width/2}" y="23" text-anchor="middle" font-size="18" font-weight="bold">Scenario 1: 20-Run Average Throughput</text>',
        f'<text x="{width/2}" y="{height-10}" text-anchor="middle" font-size="14">Distance (m)</text>',
        f'<text x="20" y="{height/2}" text-anchor="middle" font-size="14" transform="rotate(-90 20 {height/2})">Throughput (Mbps)</text>',
    ])
    for index, (label, _, _, color, kind, mean) in enumerate(series):
        x, y = 590, 52 + index * 24
        svg.append(f'<line x1="{x}" y1="{y}" x2="{x+32}" y2="{y}" stroke="{color}" stroke-width="2"/>')
        marker(svg, kind, x + 16, y, color)
        svg.append(f'<text x="{x+40}" y="{y+4}" font-size="13">{label}: mean {mean:.3f} Mbps</text>')
    svg.append('</svg>')
    output.write_text("\n".join(svg), encoding="utf-8")


def main():
    RESULTS.mkdir(exist_ok=True)
    rng = random.Random(20260722)
    seeds = rng.sample(range(1, 2_000_000_000), 20)
    agent = RbfImportanceEntropyAgent(
        len(RBF_CENTERS_DB) + 5, 1e-4, 0.0, 0.3, 0.05, device="cpu"
    )
    agent.load_policy(MODEL)
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(BUILD_DIRECTORY / "lib")
    paths = {"model": [], "ideal": [], "minstrel": []}
    summary = []
    interface = create_interface()
    try:
        for index, seed in enumerate(seeds, 1):
            model = run_episode(
                agent, seed, 80.0, 1.0, 0.5,
                f"{PREFIX}-model-run{index:02d}", False, interface=interface,
                log_decisions=False, print_every=10**9, state_encoder=rbf_policy_state,
            )
            paths["model"].append(Path(model["throughput_csv"]))
            ideal_path = RESULTS / f"{PREFIX}-ideal-run{index:02d}-seed{seed}.csv"
            minstrel_path = RESULTS / f"{PREFIX}-minstrel-run{index:02d}-seed{seed}.csv"
            subprocess.run(baseline_command("ns3::IdealWifiManager", seed, ideal_path), cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
            subprocess.run(baseline_command("ns3::MinstrelHtWifiManager", seed, minstrel_path), cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
            paths["ideal"].append(ideal_path)
            paths["minstrel"].append(minstrel_path)
            ideal = throughput_metrics(ideal_path)["mean"]
            minstrel = throughput_metrics(minstrel_path)["mean"]
            summary.append((index, seed, model["throughput"], ideal, minstrel))
            print(f"run={index:02d} seed={seed} model={model['throughput']:.6f} ideal={ideal:.6f} minstrel={minstrel:.6f}", flush=True)
    finally:
        del interface

    summary_path = RESULTS / f"{PREFIX}-summary.csv"
    with summary_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["run", "seed", "model_mbps", "ideal_mbps", "minstrel_mbps"])
        writer.writerows(summary)
    means = {
        "model": sum(row[2] for row in summary) / 20,
        "ideal": sum(row[3] for row in summary) / 20,
        "minstrel": sum(row[4] for row in summary) / 20,
    }
    curves = {name: average_curves(items) for name, items in paths.items()}
    average_path = RESULTS / f"{PREFIX}-average.csv"
    with average_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["algorithm", "distance_m", "throughput_mbps", "runs"])
        for name, (xs, ys) in curves.items():
            for x, y in zip(xs, ys):
                writer.writerow([name, f"{x:.6f}", f"{y:.6f}", 20])
    series = [
        ("Saved RBF policy", *curves["model"], "#15803d", "cross", means["model"]),
        ("Ideal", *curves["ideal"], "#1d4ed8", "circle", means["ideal"]),
        ("Minstrel-HT", *curves["minstrel"], "#dc2626", "triangle", means["minstrel"]),
    ]
    output = RESULTS / f"{PREFIX}-average.svg"
    write_svg(series, output)
    print(f"means={means}")
    print(f"summary={summary_path}")
    print(f"average={average_path}")
    print(f"svg={output}")


if __name__ == "__main__":
    main()
