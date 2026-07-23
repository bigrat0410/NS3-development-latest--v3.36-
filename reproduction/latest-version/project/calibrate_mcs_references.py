#!/usr/bin/env python3
"""Measure fixed-MCS 20-packet wall-clock goodput at a fixed 1 m distance."""

import argparse
import csv
from pathlib import Path

import numpy as np

from offline_reinforce_train import (
    DEFAULT_BE_MAX_AMPDU_SIZE,
    RESULTS,
    ampdu_mode,
    create_interface,
    run_episode,
)


class FixedMcsAgent:
    def __init__(self, mcs):
        self.mcs = mcs
        self.gradient_updates = 0

    def action_probabilities(self, state):
        probabilities = [0.0] * 8
        probabilities[self.mcs] = 1.0
        return probabilities

    def select_greedy_action(self, state):
        return self.mcs


def main():
    parser = argparse.ArgumentParser(
        description="Calibrate fixed-MCS references for one BE A-MPDU configuration"
    )
    parser.add_argument(
        "--beMaxAmpduSize", dest="be_max_ampdu_size", type=int,
        default=65535,
        help="BE A-MPDU maximum size in bytes (0 disables aggregation)",
    )
    args = parser.parse_args()
    mode = ampdu_mode(args.be_max_ampdu_size)
    RESULTS.mkdir(exist_ok=True)
    interface = create_interface()
    summary = []
    try:
        for mcs in range(8):
            result = run_episode(
                FixedMcsAgent(mcs),
                seed=990001,
                simulation_time=40.0,
                start_distance=1.0,
                moving_speed=0.0,
                label=f"mcs-reference-{mode}-1m-40s-mcs{mcs}",
                training=False,
                interface=interface,
                log_decisions=True,
                print_every=10 ** 9,
                be_max_ampdu_size=args.be_max_ampdu_size,
            )
            with Path(result["decision_csv"]).open(newline="") as handle:
                rows = list(csv.DictReader(handle))
            goodputs = np.asarray(
                [
                    float(row["window_throughput_mbps"])
                    for row in rows
                    if int(row["packet_count"]) == 20
                ],
                dtype=np.float64,
            )
            if goodputs.size == 0:
                raise RuntimeError(f"MCS{mcs} produced no complete windows")
            row = {
                "mcs": mcs,
                "complete_windows": goodputs.size,
                "mean_goodput_mbps": goodputs.mean(),
                "p95_goodput_mbps": np.percentile(goodputs, 95),
                "p99_goodput_mbps": np.percentile(goodputs, 99),
                "maximum_goodput_mbps": goodputs.max(),
                "scenario_mean_throughput_mbps": result["throughput"],
            }
            summary.append(row)
            print(" ".join(f"{key}={value}" for key, value in row.items()), flush=True)
    finally:
        del interface

    output = RESULTS / f"reproduction-mcs-reference-{mode}-1m-40s-summary.csv"
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=summary[0].keys())
        writer.writeheader()
        writer.writerows(summary)
    print(f"summary={output}")


if __name__ == "__main__":
    main()
