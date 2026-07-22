#!/usr/bin/env python3
"""Static sanity check: STA parked at s=1 m, 2 s per episode, 100 episodes.

At 1 m the channel is excellent, so the optimal action is HtMcs7. If the
REINFORCE implementation is correct, the greedy MCS and the softmax mass
should march up toward MCS7 as training proceeds. We print the policy's
output probabilities (evaluated on the states actually observed this
episode) after every round so convergence is directly observable.
"""

import argparse
import csv
import math
from pathlib import Path

import numpy as np
import torch

from offline_reinforce_model import ReinRateAgent
from offline_reinforce_train import RESULTS, create_interface, run_episode


def summarize_decision_csv(path):
    """Return (mean_probs[8], greedy_mcs, action_counts[8]) from a decision log."""
    prob_rows = []
    action_counts = [0] * 8
    with Path(path).open(newline="") as handle:
        for row in csv.DictReader(handle):
            probs = [float(value) for value in row["probabilities"].split()]
            prob_rows.append(probs)
            action_counts[int(row["next_mcs"])] += 1
    mean_probs = np.mean(prob_rows, axis=0) if prob_rows else np.zeros(8)
    greedy = int(np.argmax(mean_probs))
    return mean_probs, greedy, action_counts


def main():
    parser = argparse.ArgumentParser(description="Static s=1 convergence sanity check")
    parser.add_argument("--episodes", type=int, default=100)
    parser.add_argument("--simulationTime", dest="sim_time", type=float, default=2.0)
    parser.add_argument("--distance", type=float, default=1.0)
    parser.add_argument("--learningRate", dest="lr", type=float, default=1e-4)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument("--updateBatch", dest="update_batch", type=int, default=None,
                        help="flush a gradient step every N windows (mini-batch); "
                             "default None = one update per whole episode")
    parser.add_argument("--seed", type=int, default=774015)
    args = parser.parse_args()

    RESULTS.mkdir(exist_ok=True)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    import random
    random.seed(args.seed)

    agent = ReinRateAgent(lr=args.lr, gamma=args.gamma, epsilon=args.epsilon,
                          update_batch=args.update_batch)
    interface = create_interface()

    print(
        f"static sanity: s={args.distance}m sim={args.sim_time}s "
        f"episodes={args.episodes} lr={args.lr} gamma={args.gamma} eps={args.epsilon} "
        f"update_batch={args.update_batch}",
        flush=True,
    )
    print("ep | greedy | P(MCS0..7)                                                  "
          "| windows | mean_rew", flush=True)

    history = []
    for episode in range(1, args.episodes + 1):
        result = run_episode(
            agent,
            args.seed,                       # fixed seed -> static channel every round
            args.sim_time,
            args.distance,
            0.0,                             # movingSpeed = 0 -> STA stays at s=1
            f"static-sanity-ep{episode:04d}",
            training=True,
            interface=interface,
            log_decisions=True,
            print_every=10 ** 9,             # silence per-window prints
        )
        if not args.update_batch and result["updates"] != 1:
            raise RuntimeError(f"episode {episode}: {result['updates']} updates, expected 1")
        mean_probs, greedy, _counts = summarize_decision_csv(result["decision_csv"])
        history.append((episode, greedy, mean_probs, result["mean_window_reward"]))
        prob_str = " ".join(f"{p:.3f}" for p in mean_probs)
        print(
            f"{episode:3d} |   {greedy}    | {prob_str} | "
            f"{result['windows']:5d}   | {result['mean_window_reward']:.3f} "
            f"| upd={result['updates']}",
            flush=True,
        )

    del interface

    # Compact verdict: did P(MCS7) rise and become the greedy action?
    first_p7 = history[0][2][7]
    last_p7 = history[-1][2][7]
    final_greedy = history[-1][1]
    print(
        f"\nP(MCS7): {first_p7:.3f} -> {last_p7:.3f} | final greedy MCS = {final_greedy} | "
        f"{'CONVERGED to MCS7' if final_greedy == 7 else 'did NOT converge to MCS7'}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
