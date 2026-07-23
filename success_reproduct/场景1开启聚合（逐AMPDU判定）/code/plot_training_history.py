#!/usr/bin/env python3

import argparse
import csv
import math
from html import escape
from pathlib import Path


def points(values, sx, sy):
    return " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in values)


def main():
    parser = argparse.ArgumentParser(description="Plot episodic training statistics")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--title", default="Window-20 Episodic Training Statistics")
    args = parser.parse_args()
    with args.input.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit("training history is empty")

    episodes = [int(row["episode"]) for row in rows]
    train = [(episode, float(row["train_throughput_mbps"])) for episode, row in zip(episodes, rows)]
    validation = [
        (episode, float(row["validation_throughput_mbps"]))
        for episode, row in zip(episodes, rows)
        if math.isfinite(float(row["validation_throughput_mbps"]))
    ]
    rewards = [(episode, float(row["mean_window_reward"])) for episode, row in zip(episodes, rows)]
    probabilities = [
        (episode, float(row["validation_probability"]))
        for episode, row in zip(episodes, rows)
        if math.isfinite(float(row["validation_probability"]))
    ]

    width, height = 960, 700
    left, right, top, bottom = 75, 30, 45, 55
    panel_gap = 55
    panel_height = (height - top - bottom - panel_gap) / 2
    plot_width = width - left - right
    x_max = max(episodes)
    sx = lambda value: left + (value / max(x_max, 1)) * plot_width

    throughput_max = max(60.0, max(value for _, value in train + validation))
    reward_max = max(value for _, value in rewards) if rewards else 1.0
    reward_max = max(reward_max, 1e-9)
    top_sy = lambda value: top + panel_height - value / throughput_max * panel_height
    lower_top = top + panel_height + panel_gap
    reward_sy = lambda value: lower_top + panel_height - value / reward_max * panel_height
    probability_sy = lambda value: lower_top + panel_height - value * panel_height

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width / 2}" y="24" text-anchor="middle" font-size="18" font-weight="bold">{escape(args.title)}</text>',
    ]
    for panel_top in (top, lower_top):
        svg.append(f'<line x1="{left}" y1="{panel_top}" x2="{left}" y2="{panel_top + panel_height}" stroke="black"/>')
        svg.append(f'<line x1="{left}" y1="{panel_top + panel_height}" x2="{left + plot_width}" y2="{panel_top + panel_height}" stroke="black"/>')
    for episode in range(0, x_max + 1, 50):
        x = sx(episode)
        for panel_top in (top, lower_top):
            svg.append(f'<line x1="{x:.2f}" y1="{panel_top}" x2="{x:.2f}" y2="{panel_top + panel_height}" stroke="#e5e5e5"/>')
        svg.append(f'<text x="{x:.2f}" y="{height - 28}" text-anchor="middle" font-size="11">{episode}</text>')

    svg.append(f'<polyline points="{points(train, sx, top_sy)}" fill="none" stroke="#1565c0" stroke-width="1.5"/>')
    if validation:
        svg.append(f'<polyline points="{points(validation, sx, top_sy)}" fill="none" stroke="#d32f2f" stroke-width="2.2"/>')
        for episode, value in validation:
            svg.append(f'<circle cx="{sx(episode):.2f}" cy="{top_sy(value):.2f}" r="3" fill="#d32f2f"/>')
    svg.append(f'<polyline points="{points(rewards, sx, reward_sy)}" fill="none" stroke="#2e7d32" stroke-width="1.5"/>')
    if probabilities:
        svg.append(f'<polyline points="{points(probabilities, sx, probability_sy)}" fill="none" stroke="#7b1fa2" stroke-width="2.2"/>')

    svg.extend([
        f'<text x="18" y="{top + panel_height / 2}" text-anchor="middle" font-size="13" transform="rotate(-90 18 {top + panel_height / 2})">Throughput (Mbps)</text>',
        f'<text x="18" y="{lower_top + panel_height / 2}" text-anchor="middle" font-size="13" transform="rotate(-90 18 {lower_top + panel_height / 2})">Reward / Probability</text>',
        f'<text x="{width / 2}" y="{height - 7}" text-anchor="middle" font-size="13">Episode</text>',
        '<line x1="720" y1="58" x2="750" y2="58" stroke="#1565c0" stroke-width="2"/><text x="758" y="62" font-size="12">Train throughput</text>',
        '<line x1="720" y1="78" x2="750" y2="78" stroke="#d32f2f" stroke-width="2"/><text x="758" y="82" font-size="12">Validation throughput</text>',
        f'<line x1="720" y1="{lower_top + 15}" x2="750" y2="{lower_top + 15}" stroke="#2e7d32" stroke-width="2"/><text x="758" y="{lower_top + 19}" font-size="12">Mean window reward</text>',
        f'<line x1="720" y1="{lower_top + 35}" x2="750" y2="{lower_top + 35}" stroke="#7b1fa2" stroke-width="2"/><text x="758" y="{lower_top + 39}" font-size="12">Validation max probability</text>',
        '</svg>',
    ])
    args.output.write_text("\n".join(svg), encoding="utf-8")
    print(f"Wrote {args.output} from {len(rows)} episodes")


if __name__ == "__main__":
    main()
