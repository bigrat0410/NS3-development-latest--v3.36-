#!/usr/bin/env python3
"""Build a focused Minstrel-HT switching report from ns-3 debug/stat outputs."""

import argparse
import re
from collections import Counter
from pathlib import Path


TIME_RE = re.compile(r"^\+(\d+\.\d+)s (.*)$")
FIND_RE = re.compile(r"FindRate packet=(\d+)")
RATE_RE = re.compile(r"FindRate (maxTpRrate|sampleRate)=(\d+)")
STATUS_RE = re.compile(
    r"DoReportAmpduTxStatus\. TxRate=(\d+) SuccMpdus=(\d+) FailedMpdus=(\d+)"
)
MAX_RE = re.compile(r"max tp=(\d+)")
MCS_ROW_RE = re.compile(
    r"^HT\s+20\s+800\s+1\s+(.*?)MCS(\d+)\s+(\d+)\s+"
    r"(\d+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+"
    r"([0-9.]+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$"
)


def parse_debug(path, start, end, packet_size):
    decisions = []
    update_times = []
    active = None
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = TIME_RE.match(line.rstrip())
            if not match:
                continue
            time_s = float(match.group(1))
            message = match.group(2)

            status = STATUS_RE.search(message)
            if status and active is not None:
                active["feedback_ms"] = time_s * 1000
                active["tx_rate"] = int(status.group(1))
                active["success"] = int(status.group(2))
                active["failed"] = int(status.group(3))
                elapsed = time_s - active["time_s"]
                active["feedback_delay_ms"] = elapsed * 1000
                active["goodput"] = (
                    active["success"] * packet_size * 8 / elapsed / 1e6
                    if elapsed > 0 else 0.0
                )
                active = None

            find = FIND_RE.search(message)
            if find:
                if start <= time_s <= end:
                    active = {
                        "time_s": time_s,
                        "time_ms": time_s * 1000,
                        "packet": int(find.group(1)),
                        "mcs": None,
                        "reason": "unknown",
                    }
                    decisions.append(active)
                else:
                    active = None
                continue

            rate = RATE_RE.search(message)
            if rate and active is not None:
                active["mcs"] = int(rate.group(2))
                active["reason"] = "sample" if rate.group(1) == "sampleRate" else "max_tp"

            if MAX_RE.search(message):
                update_times.append(time_s)

    return [d for d in decisions if "success" in d], update_times


def parse_stats(path):
    tables = []
    current = None
    with path.open(encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.rstrip()
            if line.startswith("               best"):
                current = []
                tables.append(current)
                continue
            match = MCS_ROW_RE.match(line)
            if match and current is not None:
                flags = match.group(1).strip() or "-"
                current.append({
                    "flags": flags,
                    "mcs": int(match.group(2)),
                    "airtime_us": int(match.group(4)),
                    "perfect_tp": float(match.group(5)),
                    "estimated_tp": float(match.group(6)),
                    "ewma_prob": float(match.group(7)),
                    "ewmsd": float(match.group(8)),
                    "interval_prob": float(match.group(9)),
                    "retry": int(match.group(10)),
                    "success": int(match.group(11)),
                    "attempt": int(match.group(12)),
                    "success_hist": int(match.group(13)),
                    "attempt_hist": int(match.group(14)),
                })
    return tables


def write_report(path, decisions, updates, start, end, seed, packet_size):
    counts = Counter(d["mcs"] for d in decisions)
    sample_count = sum(d["reason"] == "sample" for d in decisions)
    total_success = sum(d["success"] for d in decisions)
    total_failed = sum(d["failed"] for d in decisions)
    duration = end - start
    aggregate_goodput = total_success * packet_size * 8 / duration / 1e6

    lines = [
        f"# Minstrel-HT {start:g}-{end:g} 秒速率切换明细",
        "",
        "## 实验定义",
        "",
        f"- 随机种子：`{seed}`，Run：`1`。",
        "- AP：`ns3::MinstrelHtWifiManager`；802.11n，20 MHz，单空间流，长 GI。",
        f"- 起始距离：1 m；STA 以 0.5 m/s 远离；{start:g}-{end:g} 秒对应约 "
        f"{1 + 0.5 * start:.1f}-{1 + 0.5 * end:.1f} m。",
        "- UDP：60 Mbps，应用负载 1420 bytes；A-MPDU 保持默认开启。",
        "- Minstrel-HT 默认参数：统计更新间隔 50 ms，EWMA level 75。",
        "",
        "## 如何理解这里的“决策”和“单包吞吐量”",
        "",
        "`FindRate` 是 Minstrel 为下一次发送机会选择速率。启用 A-MPDU 后，一次选择通常服务多个 MPDU，",
        "因此它不是“一次 UDP 包对应一次决策”。`packet_index` 是 Minstrel 内部累计 MPDU 索引。",
        "下表的 `goodput_mbps` 使用本次反馈中成功 MPDU 的应用负载估算：",
        "",
        "```text",
        f"goodput = success_mpdus * {packet_size} * 8 / feedback_delay",
        "```",
        "",
        "它是单次 A-MPDU 发送机会的有效载荷 goodput，不是 PHY 标称速率，也不是 10 ms/50 ms 窗口吞吐量。",
        "失败 MPDU 单独列出；重传会在后续发送机会再次进入统计。",
        "",
        "## 窗口摘要",
        "",
        f"- 完整记录的速率选择：{len(decisions)} 次。",
        f"- 探测速率选择：{sample_count} 次；常规最大吞吐率选择：{len(decisions)-sample_count} 次。",
        f"- 成功 MPDU：{total_success}；失败 MPDU：{total_failed}。",
        f"- 按 {duration:g} 秒窗口成功负载计算的总体 goodput：{aggregate_goodput:.3f} Mbps。",
        "- MCS 选择次数：" + ", ".join(f"MCS{k}={counts[k]}" for k in sorted(counts)) + "。",
        "",
        "## Minstrel-HT 更新公式",
        "",
        "每个约 50 ms 的统计周期内，对每个 MCS 统计尝试数 `attempt` 和成功数 `success`：",
        "",
        "```text",
        "p_interval = 100 * success / attempt",
        "p_ewma(new) = [25 * p_interval + 75 * p_ewma(old)] / 100",
        "```",
        "",
        "第一次得到该速率样本时直接令 `p_ewma = p_interval`。估算吞吐为：",
        "",
        "```text",
        "p_used = 0,                  p_ewma < 10%",
        "p_used = min(p_ewma, 90%),   otherwise",
        "estimated_throughput = p_used / perfect_tx_time",
        "```",
        "",
        "因为成功率在内部按 0-100 而非 0-1 表示，throughput 内部值相当于 `100 * packets/s`；",
        "统计文件经过 `/100` 后显示为 packets/s。Minstrel 选出：",
        "`A=max_tp`、`B=second_max_tp`、`P=max_probability`，并间歇选择 sample MCS 探测信道。",
        "",
        "## 每次速率选择与发送反馈",
        "",
        "|decision_ms|feedback_ms|packet_index|reason|MCS|success_mpdus|failed_mpdus|feedback_delay_ms|goodput_mbps|distance_m|",
        "|---:|---:|---:|:---|---:|---:|---:|---:|---:|---:|",
    ]
    for d in decisions:
        distance = 1.0 + 0.5 * d["time_s"]
        lines.append(
            f"|{d['time_ms']:.6f}|{d['feedback_ms']:.6f}|{d['packet']}|{d['reason']}|"
            f"{d['mcs']}|{d['success']}|{d['failed']}|{d['feedback_delay_ms']:.6f}|"
            f"{d['goodput']:.3f}|{distance:.6f}|"
        )

    lines += [
        "",
        "## 50 ms 内部统计更新",
        "",
        "标志：`A` 为最高估算吞吐速率，`B` 为第二高，`P` 为最高成功概率。",
        "每个更新时间点后列出 8 个 MCS 的内部计算结果。",
        "",
        "|update_ms|MCS|flags|airtime_us|estimated_tp_scaled|ewma_prob_pct|interval_prob_pct|success/attempt|history_success/attempt|retry|",
        "|---:|---:|:---|---:|---:|---:|---:|:---|:---|---:|",
    ]
    for time_s, table in updates:
        for row in table:
            lines.append(
                f"|{time_s*1000:.6f}|{row['mcs']}|{row['flags']}|{row['airtime_us']}|"
                f"{row['estimated_tp']:.3f}|{row['ewma_prob']:.3f}|{row['interval_prob']:.3f}|"
                f"{row['success']}/{row['attempt']}|{row['success_hist']}/{row['attempt_hist']}|{row['retry']}|"
            )

    lines += [
        "",
        "## 观察重点",
        "",
        "逐次表中的短暂非主 MCS 通常是 `sample` 探测，不等同于主策略永久切换。",
        "真正的主速率切换应结合 50 ms 表中 `A` 标志变化判断。高负载和 A-MPDU 下，",
        "一次 `FindRate` 后可成功确认几十个 MPDU，这正是决策次数明显少于包数的原因。",
        "",
        "## 原始证据",
        "",
        "- `my-project-results/minstrel-switch-seed774015-debug.log`：带 ns 时间戳的 Minstrel-HT debug trace。",
        "- `minstrel-ht-stats-00:00:00:00:00:01.txt`：每次统计更新的完整内部速率表。",
        "- `my-project-results/minstrel-switch-seed774015-throughput-10ms.csv`：独立的 10 ms 接收吞吐采样。",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def append_initialization(path, decisions, updates, start, end, packet_size):
    counts = Counter(d["mcs"] for d in decisions)
    sample_count = sum(d["reason"] == "sample" for d in decisions)
    total_success = sum(d["success"] for d in decisions)
    total_failed = sum(d["failed"] for d in decisions)
    duration = end - start
    aggregate_goodput = total_success * packet_size * 8 / duration / 1e6
    lines = [
        "",
        f"## 冷启动初始化明细（{start:.3f}-{end:.3f} s）",
        "",
        "该区间从 UDP 业务启动开始，覆盖初始逐率探测、首次统计建立，以及主速率稳定到 MCS7。",
        "它使用与前面15-30秒完全相同的决策、反馈和goodput定义。",
        "",
        f"- 完整速率选择：{len(decisions)} 次，其中 sample {sample_count} 次。",
        f"- 成功 MPDU：{total_success}；失败 MPDU：{total_failed}。",
        f"- 按 {duration:.3f} 秒成功负载计算的总体 goodput：{aggregate_goodput:.3f} Mbps。",
        "- MCS选择次数：" + ", ".join(f"MCS{k}={counts[k]}" for k in sorted(counts)) + "。",
        "- 主速率路径：MCS0（初始） -> MCS5（约558 ms） -> MCS7（约668 ms）。",
        "",
        "### 初始化阶段每次速率选择与反馈",
        "",
        "|decision_ms|feedback_ms|packet_index|reason|MCS|success_mpdus|failed_mpdus|feedback_delay_ms|goodput_mbps|distance_m|",
        "|---:|---:|---:|:---|---:|---:|---:|---:|---:|---:|",
    ]
    for d in decisions:
        distance = 1.0 + 0.5 * d["time_s"]
        lines.append(
            f"|{d['time_ms']:.6f}|{d['feedback_ms']:.6f}|{d['packet']}|{d['reason']}|"
            f"{d['mcs']}|{d['success']}|{d['failed']}|{d['feedback_delay_ms']:.6f}|"
            f"{d['goodput']:.3f}|{distance:.6f}|"
        )
    lines += [
        "",
        "### 初始化阶段50 ms内部统计更新",
        "",
        "|update_ms|MCS|flags|airtime_us|estimated_tp_scaled|ewma_prob_pct|interval_prob_pct|success/attempt|history_success/attempt|retry|",
        "|---:|---:|:---|---:|---:|---:|---:|:---|:---|---:|",
    ]
    for time_s, table in updates:
        for row in table:
            lines.append(
                f"|{time_s*1000:.6f}|{row['mcs']}|{row['flags']}|{row['airtime_us']}|"
                f"{row['estimated_tp']:.3f}|{row['ewma_prob']:.3f}|{row['interval_prob']:.3f}|"
                f"{row['success']}/{row['attempt']}|{row['success_hist']}/{row['attempt_hist']}|{row['retry']}|"
            )
    lines += [
        "",
        "初始化结论：最初没有各MCS的成功率，Minstrel先从MCS0保守发送，并直接sample多个速率。",
        "第一次有样本时EWMA直接采用区间成功率；MCS5先成为最高估算吞吐率，随后MCS7探测成功，",
        "在下一次统计更新中超过MCS5并成为主速率。",
        "",
    ]
    with path.open("a", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug", type=Path, required=True)
    parser.add_argument("--stats", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--start", type=float, default=10.0)
    parser.add_argument("--end", type=float, default=15.0)
    parser.add_argument("--seed", type=int, default=774015)
    parser.add_argument("--packet-size", type=int, default=1420)
    parser.add_argument("--initialization-start", type=float)
    parser.add_argument("--initialization-end", type=float)
    args = parser.parse_args()

    decisions, update_times = parse_debug(
        args.debug, args.start, args.end, args.packet_size
    )
    tables = parse_stats(args.stats)
    if len(update_times) != len(tables):
        raise RuntimeError(
            f"cannot align update times ({len(update_times)}) and stats tables ({len(tables)})"
        )
    updates = [
        (time_s, table)
        for time_s, table in zip(update_times, tables)
        if args.start <= time_s <= args.end
    ]
    write_report(
        args.output, decisions, updates, args.start, args.end, args.seed, args.packet_size
    )
    init_message = ""
    if args.initialization_start is not None and args.initialization_end is not None:
        init_decisions, _ = parse_debug(
            args.debug, args.initialization_start, args.initialization_end, args.packet_size
        )
        init_updates = [
            (time_s, table)
            for time_s, table in zip(update_times, tables)
            if args.initialization_start <= time_s <= args.initialization_end
        ]
        append_initialization(
            args.output,
            init_decisions,
            init_updates,
            args.initialization_start,
            args.initialization_end,
            args.packet_size,
        )
        init_message = f" init_decisions={len(init_decisions)} init_updates={len(init_updates)}"
    print(f"decisions={len(decisions)} updates={len(updates)}{init_message} output={args.output}")


if __name__ == "__main__":
    main()
