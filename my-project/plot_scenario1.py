#!/usr/bin/env python3

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "my-project-results"
ALGORITHMS = ["minstrel", "mab", "dqn"]
COLORS = {
    "minstrel": "#2f6f9f",
    "mab": "#c44e52",
    "dqn": "#3d8b37",
}


def read_series(algorithm):
    path = RESULTS / f"throughput-{algorithm}.csv"
    distances = []
    throughputs = []
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if row.get("phase", "") == "warmup":
                continue
            distance = float(row["distance_m"])
            if 0.0 <= distance <= 50.0:
                distances.append(distance)
                throughputs.append(float(row["throughput_mbps"]))
    return distances, throughputs


def polyline(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def main():
    RESULTS.mkdir(exist_ok=True)
    series = {algorithm: read_series(algorithm) for algorithm in ALGORITHMS}

    combined_path = RESULTS / "scenario1-throughput-distance.csv"
    with combined_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["algorithm", "distance_m", "throughput_mbps"])
        for algorithm, (distances, throughputs) in series.items():
            for distance, throughput in zip(distances, throughputs):
                writer.writerow([algorithm, f"{distance:.3f}", f"{throughput:.6f}"])

    width, height = 920, 560
    left, right, top, bottom = 78, 24, 34, 72
    plot_w = width - left - right
    plot_h = height - top - bottom
    y_max = max([1.0] + [max(values) for _, values in series.values() if values])
    y_max = max(5.0, ((int(y_max / 5.0) + 1) * 5.0))

    def sx(distance):
        return left + distance / 50.0 * plot_w

    def sy(throughput):
        return top + plot_h - throughput / y_max * plot_h

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:14px;fill:#222}.title{font-size:20px;font-weight:700}.axis{stroke:#222;stroke-width:1.4}.grid{stroke:#ddd;stroke-width:1}.legend{font-size:14px}</style>',
        f'<text class="title" x="{width / 2:.0f}" y="24" text-anchor="middle">Scenario 1: AP-STA Distance vs Throughput</text>',
    ]

    for tick in range(0, 51, 10):
        x = sx(tick)
        lines.append(f'<line class="grid" x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}"/>')
        lines.append(f'<text x="{x:.2f}" y="{height - 42}" text-anchor="middle">{tick}</text>')
    for tick in range(0, int(y_max) + 1, 5):
        y = sy(tick)
        lines.append(f'<line class="grid" x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 5:.2f}" text-anchor="end">{tick}</text>')

    lines.append(f'<line class="axis" x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}"/>')
    lines.append(f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}"/>')
    lines.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 12}" text-anchor="middle">Distance (m)</text>')
    lines.append(f'<text x="18" y="{top + plot_h / 2:.2f}" text-anchor="middle" transform="rotate(-90 18 {top + plot_h / 2:.2f})">Throughput (Mbps)</text>')

    legend_x = left + plot_w - 150
    legend_y = top + 22
    for index, algorithm in enumerate(ALGORITHMS):
        distances, throughputs = series[algorithm]
        points = [(sx(distance), sy(throughput)) for distance, throughput in zip(distances, throughputs)]
        color = COLORS[algorithm]
        if points:
            lines.append(f'<polyline points="{polyline(points)}" fill="none" stroke="{color}" stroke-width="2.2"/>')
        y = legend_y + index * 22
        lines.append(f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 28}" y2="{y}" stroke="{color}" stroke-width="3"/>')
        lines.append(f'<text class="legend" x="{legend_x + 36}" y="{y + 5}">{algorithm}</text>')

    lines.append("</svg>")
    svg_path = RESULTS / "scenario1-throughput-distance.svg"
    svg_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {combined_path}")
    print(f"wrote {svg_path}")


if __name__ == "__main__":
    main()
