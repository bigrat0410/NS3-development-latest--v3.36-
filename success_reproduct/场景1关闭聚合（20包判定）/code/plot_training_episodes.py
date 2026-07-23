#!/usr/bin/env python3

import argparse
import csv
import glob
from html import escape
from pathlib import Path
import re


COLORS = [
    "#1565c0",
    "#d32f2f",
    "#2e7d32",
    "#7b1fa2",
    "#ef6c00",
    "#00838f",
    "#5d4037",
    "#455a64",
]


def episode_number(path):
    match = re.search(r"(?:train|validation)-episode(\d+)-seed", Path(path).name)
    if not match:
        raise SystemExit(f"Cannot parse episode number from: {path}")
    return int(match.group(1))


def read_series(path):
    with Path(path).open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    return (
        [float(row["distance_m"]) for row in rows],
        [float(row["throughput_mbps"]) for row in rows],
    )


def main():
    parser = argparse.ArgumentParser(description="Plot each training episode")
    parser.add_argument("--input-glob", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--title", default="Standard REINFORCE: Training Episodes")
    parser.add_argument("--x-min", type=float, default=0.0)
    parser.add_argument("--x-max", type=float)
    parser.add_argument("--y-max", type=float)
    parser.add_argument(
        "--episodes",
        type=int,
        nargs="+",
        help="Only plot these episode numbers, in the supplied order",
    )
    args = parser.parse_args()

    files = [path for path in glob.glob(args.input_glob) if "-decisions-" not in path]
    files = sorted(files, key=episode_number)
    if not files:
        raise SystemExit(f"No files matched: {args.input_glob}")
    if args.episodes:
        by_episode = {episode_number(path): path for path in files}
        missing = [episode for episode in args.episodes if episode not in by_episode]
        if missing:
            raise SystemExit(f"Missing episode files: {missing}")
        files = [by_episode[episode] for episode in args.episodes]
    series = []
    for index, path in enumerate(files):
        xs, ys = read_series(path)
        episode = episode_number(path)
        series.append((f"Episode {episode}", xs, ys, COLORS[index % len(COLORS)]))

    width, height = 900, max(540, 110 + 24 * len(series))
    left, right, top, bottom = 70, 150, 35, 60
    plot_width = width - left - right
    plot_height = height - top - bottom
    x_min = args.x_min
    x_max = args.x_max or max(max(xs) for _, xs, _, _ in series)
    if x_max <= x_min:
        parser.error("--x-max must be greater than --x-min")
    y_max = args.y_max or max(60.0, max(max(ys) for _, _, ys, _ in series))
    if y_max <= 0:
        parser.error("--y-max must be positive")
    sx = lambda value: left + (value - x_min) / (x_max - x_min) * plot_width
    sy = lambda value: top + plot_height - value / y_max * plot_height

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
    ]
    x_tick_start = int(x_min)
    x_tick_step = 1 if x_max - x_min <= 10 else 5
    for value in range(x_tick_start, int(x_max) + 1, x_tick_step):
        x = sx(value)
        svg.append(
            f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" '
            f'y2="{top + plot_height}" stroke="#e8e8e8" stroke-dasharray="3,3"/>'
        )
        svg.append(
            f'<text x="{x:.2f}" y="{height - 35}" text-anchor="middle" '
            f'font-size="12">{value}</text>'
        )
    for value in range(0, int(y_max) + 1, 10):
        y = sy(value)
        svg.append(
            f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_width}" '
            f'y2="{y:.2f}" stroke="#e8e8e8" stroke-dasharray="3,3"/>'
        )
        svg.append(
            f'<text x="{left - 8}" y="{y + 4:.2f}" text-anchor="end" '
            f'font-size="12">{value}</text>'
        )
    svg.extend(
        [
            f'<line x1="{left}" y1="{top}" x2="{left}" '
            f'y2="{top + plot_height}" stroke="black"/>',
            f'<line x1="{left}" y1="{top + plot_height}" '
            f'x2="{left + plot_width}" y2="{top + plot_height}" stroke="black"/>',
        ]
    )
    for index, (label, xs, ys, color) in enumerate(series):
        points = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(xs, ys))
        svg.append(
            f'<polyline points="{points}" fill="none" stroke="{color}" '
            f'stroke-width="1.8" opacity="0.9"/>'
        )
        legend_y = 62 + index * 24
        svg.append(
            f'<line x1="{width - right + 20}" y1="{legend_y}" '
            f'x2="{width - right + 52}" y2="{legend_y}" '
            f'stroke="{color}" stroke-width="2"/>'
        )
        svg.append(
            f'<text x="{width - right + 60}" y="{legend_y + 4}" '
            f'font-size="13">{escape(label)}</text>'
        )
    svg.extend(
        [
            f'<text x="{width / 2}" y="22" text-anchor="middle" '
            f'font-size="18" font-weight="bold">{escape(args.title)}</text>',
            f'<text x="{left + plot_width / 2}" y="{height - 8}" '
            f'text-anchor="middle" font-size="14">Distance (m)</text>',
            f'<text x="18" y="{top + plot_height / 2}" text-anchor="middle" '
            f'font-size="14" transform="rotate(-90 18 {top + plot_height / 2})">'
            "Throughput (Mbps)</text>",
            "</svg>",
        ]
    )
    args.output.write_text("\n".join(svg), encoding="utf-8")
    print(f"Wrote {args.output} from {len(files)} episodes")


if __name__ == "__main__":
    main()
