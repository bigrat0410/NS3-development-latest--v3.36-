#!/usr/bin/env python3
"""Train one frozen-policy trajectory per update over the 15-20 m switch."""

import argparse
import csv
import math
import random
import subprocess
import sys

import numpy as np
import torch

from offline_reinforce_model import ReinRateAgent
from offline_reinforce_train import RESULTS, ROOT, create_interface, policy_state, run_episode


PREFIX = "reproduction-scenario1-offline-local-switch-trajectory-gamma0-hidden256x3-logpi-snrdb"
RUN_LABEL = PREFIX.removeprefix("reproduction-scenario1-")


def local_switch_policy_state(observation):
    """Use the ACK SNR in dB instead of compressing its linear ratio."""
    state = policy_state(observation)
    snr = observation["snr"]
    state[0] = (
        10.0 * math.log10(snr)
        if math.isfinite(snr) and snr > 0.0
        else 0.0
    )
    return state


def plot_validation_curves(episodes, validate_every):
    if not episodes:
        return None
    plotter = ROOT / "scratch/reproduction/plot_training_episodes.py"
    input_glob = str(RESULTS / f"{PREFIX}-validation-episode*-seed*.csv")
    common = [
        sys.executable,
        "-B",
        str(plotter),
        "--input-glob",
        input_glob,
        "--x-min",
        "15",
        "--x-max",
        "20",
        "--y-max",
        "35",
    ]
    latest = episodes[-1]
    snapshot = RESULTS / f"{PREFIX}-validation-episode{latest:04d}.svg"
    subprocess.run(
        common + [
            "--output",
            str(snapshot),
            "--title",
            f"15-20 m Frozen Policy: Episode {latest}",
            "--episodes",
            str(latest),
        ],
        cwd=ROOT,
        check=True,
    )
    combined = RESULTS / f"{PREFIX}-validation-every-{validate_every}-episodes.svg"
    subprocess.run(
        common + [
            "--output",
            str(combined),
            "--title",
            f"15-20 m Frozen Policy: Every {validate_every} Episodes",
            "--episodes",
            *[str(episode) for episode in episodes],
        ],
        cwd=ROOT,
        check=True,
    )
    return combined


def main():
    parser = argparse.ArgumentParser(description="Train 2000 trajectories over the 15-20 m switch")
    parser.add_argument("--maxEpisodes", dest="max_episodes", type=int, default=2000)
    parser.add_argument("--validateEvery", dest="validate_every", type=int, default=500)
    parser.add_argument("--trainSeed", dest="train_seed", type=int, default=774015)
    parser.add_argument("--validationSeed", dest="validation_seed", type=int, default=884015)
    parser.add_argument("--agentSeed", dest="agent_seed", type=int, default=774015)
    parser.add_argument("--learningRate", dest="learning_rate", type=float, default=1e-4)
    parser.add_argument("--gamma", type=float, default=0.0)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.max_episodes < 1 or args.validate_every < 1:
        parser.error("episode counts must be positive")

    # Mobility begins at t=0. Traffic runs from t=0.5 through 10.5 seconds,
    # so startDistance=14.75 makes the active training trajectory exactly 15-20 m.
    simulation_time = 10.5
    start_distance = 14.75
    moving_speed = 0.5

    RESULTS.mkdir(exist_ok=True)
    random.seed(args.agent_seed)
    np.random.seed(args.agent_seed)
    torch.manual_seed(args.agent_seed)
    agent = ReinRateAgent(
        lr=args.learning_rate,
        gamma=args.gamma,
        epsilon=args.epsilon,
        update_batch=None,
        normalize_returns=False,
        loss_reduction="sum",
        use_behavior_probability=False,
    )

    checkpoint = RESULTS / f"{PREFIX}-checkpoint.pt"
    final_policy = RESULTS / f"{PREFIX}-final.pt"
    history_csv = RESULTS / f"{PREFIX}-training.csv"
    start_episode = 1
    if args.resume and checkpoint.exists():
        start_episode = agent.load_checkpoint(checkpoint) + 1

    append = args.resume and history_csv.exists()
    milestones = list(range(args.validate_every, start_episode, args.validate_every))
    interface = create_interface()
    fieldnames = [
        "episode",
        "train_seed",
        "train_throughput_mbps",
        "mean_window_reward",
        "windows",
        "updates",
        "loss",
        "gradient_norm",
        "sample_count",
        "action_counts",
        "validation_throughput_mbps",
        "validation_windows",
        "validation_action_counts",
        "gradient_updates_total",
        "learning_rate",
        "gamma",
        "epsilon",
        "update_batch",
    ]

    try:
        with history_csv.open("a" if append else "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            if not append:
                writer.writeheader()

            for episode in range(start_episode, args.max_episodes + 1):
                train_seed = args.train_seed + episode - 1
                train = run_episode(
                    agent,
                    train_seed,
                    simulation_time,
                    start_distance,
                    moving_speed,
                    f"{RUN_LABEL}-train-episode{episode:04d}",
                    training=True,
                    interface=interface,
                    log_decisions=episode == 1 or episode % args.validate_every == 0,
                    print_every=10 ** 9,
                    state_encoder=local_switch_policy_state,
                )
                if train["updates"] != 1:
                    raise RuntimeError(
                        f"episode {episode}: made {train['updates']} updates, expected 1"
                    )
                update = train["last_update"]

                validation = None
                if episode % args.validate_every == 0:
                    validation = run_episode(
                        agent,
                        args.validation_seed,
                        simulation_time,
                        start_distance,
                        moving_speed,
                        f"{RUN_LABEL}-validation-episode{episode:04d}",
                        training=False,
                        interface=interface,
                        log_decisions=True,
                        print_every=10 ** 9,
                        state_encoder=local_switch_policy_state,
                    )
                    agent.save_policy(RESULTS / f"{PREFIX}-episode{episode:04d}.pt")
                    milestones.append(episode)
                    plot_validation_curves(milestones, args.validate_every)

                writer.writerow({
                    "episode": episode,
                    "train_seed": train_seed,
                    "train_throughput_mbps": train["throughput"],
                    "mean_window_reward": train["mean_window_reward"],
                    "windows": train["windows"],
                    "updates": train["updates"],
                    "loss": update["loss"],
                    "gradient_norm": update["gradient_norm"],
                    "sample_count": update["sample_count"],
                    "action_counts": " ".join(map(str, train["action_counts"])),
                    "validation_throughput_mbps": (
                        validation["throughput"] if validation else math.nan
                    ),
                    "validation_windows": validation["windows"] if validation else 0,
                    "validation_action_counts": (
                        " ".join(map(str, validation["action_counts"]))
                        if validation else ""
                    ),
                    "gradient_updates_total": agent.gradient_updates,
                    "learning_rate": args.learning_rate,
                    "gamma": args.gamma,
                    "epsilon": args.epsilon,
                    "update_batch": "full_trajectory",
                })
                handle.flush()
                agent.save_checkpoint(checkpoint, episode)
                print(
                    f"episode={episode} throughput={train['throughput']:.6f} "
                    f"windows={train['windows']} updates={train['updates']} "
                    f"reward={train['mean_window_reward']:.6f} "
                    f"validation={validation['throughput'] if validation else math.nan:.6f}",
                    flush=True,
                )
    finally:
        del interface

    agent.save_policy(final_policy)
    plot_validation_curves(milestones, args.validate_every)
    print(f"gradient_updates={agent.gradient_updates}")
    print(f"history={history_csv}")
    print(f"final_policy={final_policy}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
