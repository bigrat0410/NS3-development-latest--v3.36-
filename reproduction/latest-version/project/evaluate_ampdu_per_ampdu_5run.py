#!/usr/bin/env python3
import csv
import functools
import random
import subprocess
from pathlib import Path

import offline_reinforce_train as T
from offline_full_rbf_train import (
    ENTROPY_COEF, RBF_CENTERS_DB, RbfImportanceEntropyAgent, rbf_policy_state,
)


PREFIX = "reproduction-scenario1-offline-window20-episodic-reinforce-rbf-auto-ampdu-on-per-ampdu"
RUN_LABEL = PREFIX.removeprefix("reproduction-scenario1-")
MODEL = T.RESULTS / f"{PREFIX}-final.pt"
RUNS = 20
RUN_TAG = f"test{RUNS}"
AMPDU = 65535
ROOT = T.ROOT


def command(manager, seed, output):
    return [
        str(T.EXECUTABLE), "--simulationTime=80.0", "--trafficStartTime=0.5",
        "--startDistance=1.0", "--movingSpeed=0.5", f"--rateManager={manager}",
        "--lossModel=log-distance", "--pathLossExponent=3",
        "--referenceLoss=66.6777", "--dataRate=60Mbps", "--packetSize=1420",
        "--sampleInterval=0.5", f"--Seed={seed}", "--Run=1",
        f"--outputFile={output}", f"--ns3::WifiMac::BE_MaxAmpduSize={AMPDU}",
    ]


def main():
    if not MODEL.exists():
        raise SystemExit(f"missing model: {MODEL}")
    agent = RbfImportanceEntropyAgent(
        len(RBF_CENTERS_DB) + 5, 1e-4, 0.0, 0.0, ENTROPY_COEF, device="cpu"
    )
    agent.load_policy(MODEL)
    state_encoder = functools.partial(
        rbf_policy_state, max_reference_mbps=T.max_reference_mbps(AMPDU)
    )
    seeds = random.Random(20260723).sample(range(1, 2_000_000_000), RUNS)
    interface = T.create_interface()
    rows = []
    try:
        for index, seed in enumerate(seeds, 1):
            tag = f"{RUN_LABEL}-{RUN_TAG}-run{index:02d}"
            rl = T.run_episode(
                agent, seed, 80.0, 1.0, 0.5, tag, False,
                interface=interface, log_decisions=False,
                state_encoder=state_encoder, be_max_ampdu_size=AMPDU,
                decision_per_ampdu=True, print_every=10 ** 9,
                collect_segments=True,
            )
            ideal_path = T.RESULTS / f"{PREFIX}-{RUN_TAG}-ideal-run{index:02d}-seed{seed}.csv"
            minstrel_path = T.RESULTS / f"{PREFIX}-{RUN_TAG}-minstrel-run{index:02d}-seed{seed}.csv"
            subprocess.run(command("ns3::IdealWifiManager", seed, ideal_path), cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            subprocess.run(command("ns3::MinstrelHtWifiManager", seed, minstrel_path), cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            ideal = T.throughput_metrics(ideal_path)["mean"]
            minstrel = T.throughput_metrics(minstrel_path)["mean"]
            rows.append({"run": index, "seed": seed, "rl_mbps": rl["throughput"], "ideal_mbps": ideal, "minstrel_mbps": minstrel, "segments": len(rl["segments"])})
            print(f"run={index}/{RUNS} seed={seed} rl={rl['throughput']:.6f} ideal={ideal:.6f} minstrel={minstrel:.6f} segments={len(rl['segments'])}", flush=True)
    finally:
        del interface
    summary = T.RESULTS / f"{PREFIX}-{RUN_TAG}-summary.csv"
    with summary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader(); writer.writerows(rows)
    svg = T.RESULTS / f"{PREFIX}-{RUN_TAG}-average.svg"
    average = T.RESULTS / f"{PREFIX}-{RUN_TAG}-average.csv"
    subprocess.run([
        T.ROOT / "ns3ai_env/bin/python", "-B", str(T.PLOTTING_DIRECTORY / "result_figure.py"),
        "--ideal-glob", str(T.RESULTS / f"{PREFIX}-{RUN_TAG}-ideal-run*-seed*.csv"),
        "--minstrel-glob", str(T.RESULTS / f"{PREFIX}-{RUN_TAG}-minstrel-run*-seed*.csv"),
        "--reinrate-glob", str(T.RESULTS / f"reproduction-scenario1-{RUN_LABEL}-{RUN_TAG}-run??-seed*.csv"),
        "--reinrate-label", "Reinforce", "--output", str(svg), "--average-csv", str(average),
        "--title", "Scenario 1: A-MPDU Per-Decision 20-Run Average",
    ], cwd=ROOT, check=True)
    for key in ("rl_mbps", "ideal_mbps", "minstrel_mbps"):
        print(f"{key}_mean={sum(row[key] for row in rows) / RUNS:.6f}")
    print(f"summary={summary}\naverage={average}\nsvg={svg}")


if __name__ == "__main__":
    main()
