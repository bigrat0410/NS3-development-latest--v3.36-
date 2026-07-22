#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = (
    ROOT
    / "build/scratch/reproduction_round2/ns3.36.1-round2-scenario1-default"
)
RESULTS = ROOT / "my-project-results"
SEEDS = (
    774015, 939968, 515926, 750706, 859569,
    189491, 17046, 70198, 941358, 247505,
    52249, 863680, 997997, 343976, 454830,
    296570, 555664, 585294, 553339, 456740,
)
MANAGERS = {
    "ideal": "ns3::IdealWifiManager",
    "minstrel": "ns3::MinstrelHtWifiManager",
}


def main():
    RESULTS.mkdir(exist_ok=True)
    for label, manager in MANAGERS.items():
        for seed in SEEDS:
            output = RESULTS / f"round2-scenario1-{label}-seed{seed}.csv"
            subprocess.run(
                [
                    str(EXECUTABLE),
                    f"--rateManager={manager}",
                    f"--Seed={seed}",
                    "--Run=1",
                    f"--outputFile={output}",
                ],
                cwd=ROOT,
                check=True,
            )

    subprocess.run(
        [
            sys.executable,
            "-B",
            str(Path(__file__).with_name("result_figure.py")),
            "--ideal-glob",
            str(RESULTS / "round2-scenario1-ideal-seed*.csv"),
            "--minstrel-glob",
            str(RESULTS / "round2-scenario1-minstrel-seed*.csv"),
            "--output",
            str(RESULTS / "round2-scenario1-20seed-average.svg"),
            "--average-csv",
            str(RESULTS / "round2-scenario1-20seed-average.csv"),
            "--title",
            "Round 2 Scenario 1: 20-Seed Average",
        ],
        cwd=ROOT,
        check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
