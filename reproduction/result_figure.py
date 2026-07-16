#!/usr/bin/env python3

import argparse
import csv
import glob
from pathlib import Path


def read_csv(path):
    with Path(path).open(newline="") as handle:
        return list(csv.DictReader(handle))


def average_files(pattern):
    files = sorted(glob.glob(pattern))
    if not files:
        raise SystemExit(f"No files matched: {pattern}")
    runs = [read_csv(path) for path in files]
    count = min(map(len, runs))
    xs = [sum(float(run[i]["distance_m"]) for run in runs) / len(runs) for i in range(count)]
    ys = [sum(float(run[i]["throughput_mbps"]) for run in runs) / len(runs) for i in range(count)]
    return xs, ys, len(files)


def main():
    parser = argparse.ArgumentParser(description="Plot Scenario 1 throughput")
    parser.add_argument("csv_file", nargs="?", type=Path)
    parser.add_argument("--ideal-glob")
    parser.add_argument("--minstrel-glob")
    parser.add_argument("--reinrate-csv", type=Path)
    parser.add_argument("--reinrate-label", default="REINRATE Online")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--average-csv", type=Path)
    parser.add_argument("--title", default="Scenario 1: 20-run Average")
    parser.add_argument("--label", default="Throughput")
    args = parser.parse_args()

    if args.ideal_glob and args.minstrel_glob:
        ix, iy, ic = average_files(args.ideal_glob)
        mx, my, mc = average_files(args.minstrel_glob)
        series = [("Ideal", ix, iy, "#1565c0", "circle"),
                  ("Minstrel-HT", mx, my, "#d32f2f", "cross")]
        if args.reinrate_csv:
            rows = read_csv(args.reinrate_csv)
            rx = [float(row["distance_m"]) for row in rows]
            ry = [float(row["throughput_mbps"]) for row in rows]
            series.append((args.reinrate_label, rx, ry, "#2e7d32", "square"))
        output = args.output or Path("my-project-results/reproduction-scenario1-average.svg")
        average_csv = args.average_csv or output.with_suffix(".csv")
        with average_csv.open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["algorithm", "distance_m", "throughput_mbps", "runs"])
            for label, xs, ys, _, _ in series:
                runs = ic if label == "Ideal" else mc if label == "Minstrel-HT" else 1
                for x, y in zip(xs, ys):
                    writer.writerow([label, f"{x:.6f}", f"{y:.6f}", runs])
    elif args.csv_file:
        rows = read_csv(args.csv_file)
        xs = [float(row["distance_m"]) for row in rows]
        ys = [float(row["throughput_mbps"]) for row in rows]
        series = [(args.label, xs, ys, "#1565c0", "circle")]
        output = args.output or args.csv_file.with_suffix(".svg")
    else:
        parser.error("provide csv_file or both --ideal-glob and --minstrel-glob")

    width, height = 800, 500
    left, right, top, bottom = 70, 25, 30, 60
    plot_width, plot_height = width - left - right, height - top - bottom
    x_max = max(max(xs) for _, xs, _, _, _ in series)
    y_max = max(60.0, max(max(ys) for _, _, ys, _, _ in series))
    sx = lambda value: left + value / x_max * plot_width
    sy = lambda value: top + plot_height - value / y_max * plot_height

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
           '<rect width="100%" height="100%" fill="white"/>']
    for value in range(0, int(x_max) + 1, 5):
        x = sx(value)
        if value > 0:
            svg.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_height}" stroke="#e8e8e8" stroke-dasharray="3,3"/>')
        svg.append(f'<text x="{x:.2f}" y="{height - 35}" text-anchor="middle" font-size="12">{value}</text>')
    for value in range(0, int(y_max) + 1, 10):
        y = sy(value)
        if value > 0:
            svg.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_width}" y2="{y:.2f}" stroke="#e8e8e8" stroke-dasharray="3,3"/>')
        svg.append(f'<text x="{left - 8}" y="{y + 4:.2f}" text-anchor="end" font-size="12">{value}</text>')
    svg.extend([f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}" stroke="black"/>',
                f'<line x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" y2="{top + plot_height}" stroke="black"/>'])
    for label, xs, ys, color, marker in series:
        points = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in zip(xs, ys))
        svg.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2"/>')
        for x, y in zip(xs, ys):
            px, py = sx(x), sy(y)
            if marker == "circle":
                svg.append(f'<circle cx="{px:.2f}" cy="{py:.2f}" r="2.5" fill="{color}"/>')
            elif marker == "square":
                svg.append(f'<rect x="{px-2.5:.2f}" y="{py-2.5:.2f}" width="5" height="5" fill="{color}"/>')
            else:
                svg.append(f'<path d="M {px-3:.2f} {py-3:.2f} L {px+3:.2f} {py+3:.2f} M {px-3:.2f} {py+3:.2f} L {px+3:.2f} {py-3:.2f}" stroke="{color}"/>')
    svg.extend([f'<text x="{width/2}" y="20" text-anchor="middle" font-size="18" font-weight="bold">{args.title}</text>',
                f'<text x="{width/2}" y="{height-8}" text-anchor="middle" font-size="14">Distance (m)</text>',
                f'<text x="18" y="{height/2}" text-anchor="middle" font-size="14" transform="rotate(-90 18 {height/2})">Throughput (Mbps)</text>'])
    for index, (label, _, _, color, _) in enumerate(series):
        y = 48 + index * 20
        svg.append(f'<line x1="620" y1="{y}" x2="650" y2="{y}" stroke="{color}" stroke-width="2"/>')
        svg.append(f'<text x="658" y="{y+4}" font-size="13">{label}</text>')
    svg.append('</svg>')
    output.write_text("\n".join(svg), encoding="utf-8")
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
