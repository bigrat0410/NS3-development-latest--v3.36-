#!/usr/bin/env python3

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "my-project-results"
SERIES = {
    "reinrate": "#c44e52",
    "default": "#2f6f9f",
}


def read_series(label):
    path = RESULTS / f"reinrate-scenario1-{label}.csv"
    xs, ys = [], []
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            x = float(row["distance_m"])
            if 0.0 <= x <= 50.0:
                xs.append(x)
                ys.append(float(row["throughput_mbps"]))
    return xs, ys


def main():
    data = {label: read_series(label) for label in SERIES}
    combined = RESULTS / "reinrate-scenario1-throughput-distance.csv"
    with combined.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["algorithm", "distance_m", "throughput_mbps"])
        for label, (xs, ys) in data.items():
            for x, y in zip(xs, ys):
                writer.writerow([label, f"{x:.3f}", f"{y:.6f}"])

    width, height = 920, 560
    left, right, top, bottom = 78, 24, 34, 72
    plot_w = width - left - right
    plot_h = height - top - bottom
    y_max = max([1.0] + [max(ys) for _, ys in data.values() if ys])
    y_max = max(5.0, (int(y_max / 5.0) + 1) * 5.0)

    def sx(x):
        return left + x / 50.0 * plot_w

    def sy(y):
        return top + plot_h - y / y_max * plot_h

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,Helvetica,sans-serif;font-size:14px;fill:#222}.title{font-size:20px;font-weight:700}.axis{stroke:#222;stroke-width:1.4}.grid{stroke:#ddd;stroke-width:1}.legend{font-size:14px}</style>',
        f'<text class="title" x="{width / 2:.0f}" y="24" text-anchor="middle">REINRATE Scenario 1: Distance vs Throughput</text>',
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
    for index, (label, color) in enumerate(SERIES.items()):
        xs, ys = data[label]
        points = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(xs, ys))
        if points:
            lines.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.2"/>')
        y = legend_y + index * 22
        lines.append(f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 28}" y2="{y}" stroke="{color}" stroke-width="3"/>')
        lines.append(f'<text class="legend" x="{legend_x + 36}" y="{y + 5}">{label}</text>')
    lines.append("</svg>")
    svg = RESULTS / "reinrate-scenario1-throughput-distance.svg"
    svg.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {combined}")
    print(f"wrote {svg}")


if __name__ == "__main__":
    main()
