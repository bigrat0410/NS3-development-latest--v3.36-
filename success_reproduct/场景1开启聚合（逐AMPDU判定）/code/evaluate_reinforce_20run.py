#!/usr/bin/env python3
"""Evaluate the saved A-MPDU REINFORCE policy against two ns-3 baselines."""

import csv
import functools
import os
import random
import subprocess
import sys
from pathlib import Path

import offline_reinforce_train as T
from offline_full_rbf_train import (
    ENTROPY_COEF,
    RBF_CENTERS_DB,
    RbfImportanceEntropyAgent,
    rbf_policy_state,
)


ARCHIVE = Path(__file__).resolve().parents[1]
RESULTS = ARCHIVE / "results" / "comparison_20run"
RAW = RESULTS / "raw"
MODEL = ARCHIVE / "models" / "reinforce-episode0750-final.pt"
PREFIX = "reinforce-ampdu-perdecision-episode0750-20run"
RUNS = 20
SEED_GENERATOR = 20260723
AMPDU_SIZE = 65535
SIMULATION_TIME = 80.0
START_DISTANCE = 1.0
MOVING_SPEED = 0.5


def baseline_command(manager, seed, output):
    return [
        str(T.EXECUTABLE),
        f"--simulationTime={SIMULATION_TIME}",
        "--trafficStartTime=0.5",
        f"--startDistance={START_DISTANCE}",
        f"--movingSpeed={MOVING_SPEED}",
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


def main():
    if T.BUILD_PROFILE != "default":
        raise SystemExit("This archived comparison requires REPRODUCTION_NS3_PROFILE=default")
    if not MODEL.is_file():
        raise SystemExit(f"missing model: {MODEL}")
    RAW.mkdir(parents=True, exist_ok=True)
    # run_episode resolves its output directory through this module global.
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
    seeds = random.Random(SEED_GENERATOR).sample(
        range(1, 2_000_000_000), RUNS
    )
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(T.BUILD_DIRECTORY / "lib")
    rows = []
    interface = T.create_interface()
    try:
        for index, seed in enumerate(seeds, 1):
            label = f"{PREFIX}-reinforce-run{index:02d}"
            reinforce = T.run_episode(
                agent,
                seed,
                SIMULATION_TIME,
                START_DISTANCE,
                MOVING_SPEED,
                label,
                training=False,
                interface=interface,
                log_decisions=False,
                print_every=10**9,
                state_encoder=state_encoder,
                be_max_ampdu_size=AMPDU_SIZE,
                decision_per_ampdu=True,
                collect_segments=False,
            )
            ideal_path = RAW / f"{PREFIX}-ideal-run{index:02d}-seed{seed}.csv"
            minstrel_path = RAW / f"{PREFIX}-minstrel-run{index:02d}-seed{seed}.csv"
            subprocess.run(
                baseline_command("ns3::IdealWifiManager", seed, ideal_path),
                cwd=T.ROOT,
                env=environment,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            subprocess.run(
                baseline_command("ns3::MinstrelHtWifiManager", seed, minstrel_path),
                cwd=T.ROOT,
                env=environment,
                check=True,
                stdout=subprocess.DEVNULL,
            )
            ideal = T.throughput_metrics(ideal_path)["mean"]
            minstrel = T.throughput_metrics(minstrel_path)["mean"]
            rows.append({
                "run": index,
                "seed": seed,
                "reinforce_mbps": reinforce["throughput"],
                "minstrel_ht_mbps": minstrel,
                "ideal_mbps": ideal,
                "reinforce_decisions": reinforce["windows"],
                "reinforce_action_counts_mcs0_to_mcs7": " ".join(
                    map(str, reinforce["action_counts"])
                ),
            })
            print(
                f"run={index:02d}/{RUNS} seed={seed} "
                f"reinforce={reinforce['throughput']:.6f} "
                f"minstrel_ht={minstrel:.6f} ideal={ideal:.6f}",
                flush=True,
            )
    finally:
        del interface

    summary = RESULTS / f"{PREFIX}-summary.csv"
    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    average = RESULTS / f"{PREFIX}-average.csv"
    svg = RESULTS / f"{PREFIX}-average.svg"
    python = T.ROOT / "ns3ai_env/bin/python"
    subprocess.run(
        [
            str(python),
            "-B",
            str(ARCHIVE / "code" / "result_figure.py"),
            "--ideal-glob",
            str(RAW / f"{PREFIX}-ideal-run*-seed*.csv"),
            "--minstrel-glob",
            str(RAW / f"{PREFIX}-minstrel-run*-seed*.csv"),
            "--reinrate-glob",
            str(RAW / f"reproduction-scenario1-{PREFIX}-reinforce-run??-seed*.csv"),
            "--reinrate-label",
            "Reinforce",
            "--output",
            str(svg),
            "--average-csv",
            str(average),
            "--title",
            "Scenario 1: A-MPDU Enabled, 20-Run Mean",
        ],
        cwd=T.ROOT,
        check=True,
    )
    for field in ("reinforce_mbps", "minstrel_ht_mbps", "ideal_mbps"):
        mean = sum(float(row[field]) for row in rows) / RUNS
        print(f"{field}_mean={mean:.6f}")
    print(f"summary={summary}")
    print(f"average={average}")
    print(f"svg={svg}")


if __name__ == "__main__":
    main()
