#!/usr/bin/env python3
"""Full 1-41 m RBF REINFORCE with per-SNR-bin advantages."""

import argparse
import csv
import math
import random
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

from offline_reinforce_model import ReinRateAgent
from offline_reinforce_train import RESULTS, ROOT, create_interface, run_episode


PREFIX = "reproduction-scenario1-offline-full-rbf05dbadv-piq-entropy05-rewardmcs2over9-500"
RUN_LABEL = PREFIX.removeprefix("reproduction-scenario1-")
RBF_CENTERS_DB = np.arange(5.0, 52.0 + 1e-9, 0.5, dtype=np.float32)
RBF_SIGMA_DB = 0.6
ENTROPY_COEF = 0.05
MAX_REFERENCE_MBPS = 33.6


def rbf_policy_state(observation, max_reference_mbps=MAX_REFERENCE_MBPS):
    snr = observation["snr"]
    valid = math.isfinite(snr) and snr > 0.0
    if valid:
        snr_db = 10.0 * math.log10(snr)
        rbf = np.exp(
            -((snr_db - RBF_CENTERS_DB) ** 2)
            / (2.0 * RBF_SIGMA_DB ** 2)
        ).astype(np.float32)
    else:
        snr_db = 0.0
        rbf = np.zeros_like(RBF_CENTERS_DB)

    throughput = observation["throughput"]
    if not math.isfinite(throughput) or throughput < 0.0:
        throughput = 0.0
    cw = min(max(observation["cw"], 0), 1023)
    mcs = min(max(observation["mcs"], 0), 7)
    packets = observation["packet_count"]
    success_ratio = (
        observation["successful_packets"] / packets if packets else 0.0
    )
    tail = np.asarray(
        [
            float(valid),
            math.log2(cw + 1.0) / 10.0,
            (mcs + 1.0) / 8.0,
            min(throughput / max_reference_mbps, 1.0),
            success_ratio,
        ],
        dtype=np.float32,
    )
    return np.concatenate((rbf, tail)).astype(np.float32)


class RbfImportanceEntropyAgent(ReinRateAgent):
    """Importance-corrected policy gradient with per-dB-bin advantages."""

    def __init__(self, state_size, lr, gamma, epsilon, entropy_coef, device="auto"):
        super().__init__(
            state_size=state_size,
            action_size=8,
            lr=lr,
            gamma=gamma,
            epsilon=epsilon,
            update_batch=None,
            normalize_returns=False,
            loss_reduction="mean",
            use_behavior_probability=False,
            device=device,
        )
        self.entropy_coef = entropy_coef
        self.rbf_centers_db = RBF_CENTERS_DB.tolist()
        self.rbf_sigma_db = RBF_SIGMA_DB
        self.advantage_bin_width_db = 0.5

    def finish_episode(self):
        if not self.rewards:
            raise RuntimeError("cannot finish an empty episode")
        states_cpu = torch.stack(self.states)
        valid_cpu = states_cpu[:, len(RBF_CENTERS_DB)] > 0.5
        peak_cpu = states_cpu[:, :len(RBF_CENTERS_DB)].argmax(dim=1)
        invalid_bin = len(RBF_CENTERS_DB)
        bin_ids_cpu = torch.where(
            valid_cpu, peak_cpu, torch.full_like(peak_cpu, invalid_bin)
        )
        returns = torch.tensor(
            self.rewards, dtype=torch.float32, device=self.device
        )
        states = states_cpu.to(self.device)
        actions = torch.tensor(self.actions, dtype=torch.long, device=self.device)
        bin_ids = bin_ids_cpu.to(self.device)
        probabilities = self.policy(states)
        selected_pi = probabilities.gather(1, actions.unsqueeze(1)).squeeze(1)
        selected_q = (
            (1.0 - self.epsilon) * selected_pi
            + self.epsilon / self.action_size
        )
        importance = (selected_pi / selected_q).detach()
        log_pi = torch.log(selected_pi + 1e-8)
        entropy = -(probabilities * torch.log(probabilities + 1e-8)).sum(dim=1)

        num_bins = len(RBF_CENTERS_DB) + 1
        counts = torch.bincount(bin_ids, minlength=num_bins)
        counts_f = counts.to(returns.dtype).clamp_min(1.0)
        reward_sums = torch.zeros(num_bins, device=self.device).scatter_add_(
            0, bin_ids, returns
        )
        reward_square_sums = torch.zeros(num_bins, device=self.device).scatter_add_(
            0, bin_ids, returns.square()
        )
        reward_means = reward_sums / counts_f
        reward_vars = (reward_square_sums / counts_f - reward_means.square()).clamp_min(0.0)
        reward_stds = reward_vars.sqrt()
        sample_stds = reward_stds[bin_ids]
        eligible = (counts[bin_ids] >= 2) & (sample_stds > 1e-8)
        advantages = torch.where(
            eligible,
            (returns - reward_means[bin_ids]) / (sample_stds + 1e-8),
            torch.zeros_like(returns),
        )
        actor_samples = -importance * log_pi * advantages.detach()
        actor_sums = torch.zeros(num_bins, device=self.device).scatter_add_(
            0, bin_ids, actor_samples
        )
        entropy_sums = torch.zeros(num_bins, device=self.device).scatter_add_(
            0, bin_ids, entropy
        )
        nonempty = counts > 0
        bin_losses = (
            actor_sums / counts_f
            - self.entropy_coef * entropy_sums / counts_f
        )
        loss = bin_losses[nonempty].mean()
        if not torch.isfinite(loss):
            raise RuntimeError("non-finite per-bin policy loss")

        parameter_norm_before = self._parameter_norm()
        self.optimizer.zero_grad()
        loss.backward()
        gradient_norm = torch.sqrt(sum(
            (parameter.grad.detach() ** 2).sum()
            for parameter in self.policy.parameters()
            if parameter.grad is not None
        )).item()
        self.optimizer.step()
        self._sync_inference_policy()
        self.gradient_updates += 1
        bin_counts = counts.detach().cpu().tolist()
        means_cpu = reward_means.detach().cpu().tolist()
        stds_cpu = reward_stds.detach().cpu().tolist()
        entropy_cpu = (entropy_sums / counts_f).detach().cpu().tolist()
        advantages_cpu = advantages.detach().cpu()
        bin_reward_means = []
        bin_reward_stds = []
        bin_advantage_mins = []
        bin_advantage_maxs = []
        bin_entropies = []
        for index, count in enumerate(bin_counts):
            if count:
                values = advantages_cpu[bin_ids_cpu == index]
                bin_reward_means.append(means_cpu[index])
                bin_reward_stds.append(stds_cpu[index])
                bin_advantage_mins.append(float(values.min().item()))
                bin_advantage_maxs.append(float(values.max().item()))
                bin_entropies.append(entropy_cpu[index])
            else:
                bin_reward_means.append(math.nan)
                bin_reward_stds.append(math.nan)
                bin_advantage_mins.append(math.nan)
                bin_advantage_maxs.append(math.nan)
                bin_entropies.append(math.nan)
        result = {
            "update": self.gradient_updates,
            "loss": float(loss.item()),
            "return_mean": float(returns.mean().item()),
            "return_std": float(returns.std(unbiased=False).item()),
            "return_min": float(returns.min().item()),
            "return_max": float(returns.max().item()),
            "returns": list(self.rewards),
            "rewards": list(self.rewards),
            "reward_mean": sum(self.rewards) / len(self.rewards),
            "gradient_norm": gradient_norm,
            "parameter_norm_before": parameter_norm_before,
            "parameter_norm_after": self._parameter_norm(),
            "learning_rate": self.optimizer.param_groups[0]["lr"],
            "sample_count": len(self.rewards),
            "importance_weight_mean": float(importance.mean().item()),
            "importance_weight_min": float(importance.min().item()),
            "importance_weight_max": float(importance.max().item()),
            "entropy_mean": float(entropy.mean().item()),
            "snr_bin_counts": bin_counts,
            "snr_bin_reward_means": bin_reward_means,
            "snr_bin_reward_stds": bin_reward_stds,
            "snr_bin_advantage_mins": bin_advantage_mins,
            "snr_bin_advantage_maxs": bin_advantage_maxs,
            "snr_bin_entropies": bin_entropies,
        }
        self.last_update = result
        self.reset_rollout()
        return result

    def save_checkpoint(self, path, episode):
        super().save_checkpoint(path, episode)
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
        checkpoint.update({
            "rbf_experiment": PREFIX,
            "rbf_centers_db": self.rbf_centers_db,
            "rbf_sigma_db": self.rbf_sigma_db,
            "advantage_bin_width_db": self.advantage_bin_width_db,
            "advantage_bin_count": len(RBF_CENTERS_DB) + 1,
            "invalid_snr_bin": len(RBF_CENTERS_DB),
            "entropy_coef": self.entropy_coef,
            "state_size": len(RBF_CENTERS_DB) + 5,
            "reward_mcs_factor": "(mcs+2)/9",
            "advantage_normalization": "per_snr_bin",
            "updates_per_80s_episode": 1,
            "python_random_state": random.getstate(),
            "torch_random_state": torch.get_rng_state(),
            "torch_cuda_random_state": (
                torch.cuda.get_rng_state(self.device)
                if self.device.type == "cuda" else None
            ),
        })
        torch.save(checkpoint, path)

    def load_experiment_checkpoint(self, path, gamma, epsilon, entropy_coef):
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
        expected = {
            "rbf_experiment": PREFIX,
            "rbf_centers_db": self.rbf_centers_db,
            "rbf_sigma_db": self.rbf_sigma_db,
            "advantage_bin_width_db": 0.5,
            "advantage_bin_count": len(RBF_CENTERS_DB) + 1,
            "invalid_snr_bin": len(RBF_CENTERS_DB),
            "entropy_coef": entropy_coef,
            "state_size": len(RBF_CENTERS_DB) + 5,
            "reward_mcs_factor": "(mcs+2)/9",
            "advantage_normalization": "per_snr_bin",
            "updates_per_80s_episode": 1,
            "gamma": gamma,
            "epsilon": epsilon,
        }
        for key, value in expected.items():
            if checkpoint.get(key) != value:
                raise RuntimeError(
                    f"checkpoint configuration mismatch for {key}: "
                    f"{checkpoint.get(key)!r} != {value!r}"
                )
        episode = self.load_checkpoint(path)
        if "python_random_state" in checkpoint:
            random.setstate(checkpoint["python_random_state"])
        else:
            random.seed(774015 + episode)
        if "torch_random_state" in checkpoint:
            torch.set_rng_state(checkpoint["torch_random_state"])
        else:
            torch.manual_seed(774015 + episode)
        if self.device.type == "cuda" and checkpoint.get("torch_cuda_random_state") is not None:
            torch.cuda.set_rng_state(checkpoint["torch_cuda_random_state"], self.device)
        return episode


def plot_final_curves(episodes, validate_every):
    plotter = ROOT / "scratch/reproduction/plot_training_episodes.py"
    output = RESULTS / f"{PREFIX}-validation-every-{validate_every}-episodes.svg"
    subprocess.run([
        sys.executable, "-B", str(plotter),
        "--input-glob", str(RESULTS / f"{PREFIX}-validation-episode*-seed*.csv"),
        "--output", str(output),
        "--title", "Raw-dB RBF pi/q 0.5-dB Advantage: Validation",
        "--x-min", "1", "--x-max", "41", "--y-max", "60",
        "--episodes", *[str(ep) for ep in episodes],
    ], cwd=ROOT, check=True)
    return output


def main():
    parser = argparse.ArgumentParser(description="Full 1-41 m per-bin-advantage RBF REINFORCE")
    parser.add_argument("--maxEpisodes", type=int, default=500)
    parser.add_argument("--validateEvery", type=int, default=100)
    parser.add_argument("--trainSeed", type=int, default=774015)
    parser.add_argument("--validationSeed", type=int, default=884015)
    parser.add_argument("--agentSeed", type=int, default=774015)
    parser.add_argument("--learningRate", type=float, default=1e-4)
    parser.add_argument("--gamma", type=float, default=0.0)
    parser.add_argument("--epsilon", type=float, default=0.3)
    parser.add_argument("--entropyCoef", type=float, default=ENTROPY_COEF)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.maxEpisodes < 1 or args.validateEvery < 1:
        parser.error("episode counts must be positive")

    random.seed(args.agentSeed)
    np.random.seed(args.agentSeed)
    torch.manual_seed(args.agentSeed)
    state_size = len(RBF_CENTERS_DB) + 5
    agent = RbfImportanceEntropyAgent(
        state_size, args.learningRate, args.gamma, args.epsilon, args.entropyCoef,
        device=args.device,
    )
    print(f"training_device={agent.device}", flush=True)
    RESULTS.mkdir(exist_ok=True)
    history = RESULTS / f"{PREFIX}-training.csv"
    checkpoint = RESULTS / f"{PREFIX}-checkpoint.pt"
    final_policy = RESULTS / f"{PREFIX}-final.pt"
    milestones = list(range(args.validateEvery, args.maxEpisodes + 1, args.validateEvery))
    start_episode = 1
    if args.resume:
        if not checkpoint.exists() or not history.exists():
            parser.error("--resume requires the existing checkpoint and history")
        loaded_episode = agent.load_experiment_checkpoint(
            checkpoint, args.gamma, args.epsilon, args.entropyCoef
        )
        with history.open(newline="") as handle:
            existing_rows = list(csv.DictReader(handle))
        existing_episodes = [int(row["episode"]) for row in existing_rows]
        if existing_episodes != list(range(1, loaded_episode + 1)):
            raise RuntimeError("history is not continuous through checkpoint episode")
        if any(
            int(row["updates"]) != 1
            or int(row["gradient_updates_total"]) != int(row["episode"])
            for row in existing_rows
        ):
            raise RuntimeError("history update counts do not match episode numbers")
        start_episode = loaded_episode + 1
        if start_episode > args.maxEpisodes:
            parser.error("checkpoint already reached --maxEpisodes")
    interface = create_interface()
    fields = [
        "episode", "train_seed", "train_throughput_mbps", "mean_window_reward",
        "windows", "updates", "loss", "gradient_norm", "sample_count",
        "action_counts", "validation_throughput_mbps", "validation_windows",
        "validation_action_counts", "gradient_updates_total", "importance_weight_mean",
        "importance_weight_min", "importance_weight_max", "entropy_mean",
        "snr_bin_counts", "snr_bin_reward_means", "snr_bin_reward_stds",
        "snr_bin_advantage_mins", "snr_bin_advantage_maxs", "snr_bin_entropies",
        "train_zero_fraction", "train_longest_zero_run", "validation_zero_fraction",
        "validation_longest_zero_run", "gamma", "epsilon", "entropy_coef",
    ]
    mode = "a" if args.resume else "w"
    with history.open(mode, newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        if not args.resume:
            writer.writeheader()
        for episode in range(start_episode, args.maxEpisodes + 1):
            train = run_episode(
                agent, args.trainSeed + episode - 1, 80.0, 1.0, 0.5,
                f"{RUN_LABEL}-train-episode{episode:04d}", True, interface=interface,
                log_decisions=episode == 1 or episode % args.validateEvery == 0,
                print_every=10 ** 9, state_encoder=rbf_policy_state,
            )
            if train["updates"] != 1:
                raise RuntimeError(f"episode {episode} made {train['updates']} updates")
            validation = None
            if episode % args.validateEvery == 0:
                validation = run_episode(
                    agent, args.validationSeed, 80.0, 1.0, 0.5,
                    f"{RUN_LABEL}-validation-episode{episode:04d}", False,
                    interface=interface, log_decisions=True, print_every=10 ** 9,
                    state_encoder=rbf_policy_state,
                )
                agent.save_policy(RESULTS / f"{PREFIX}-episode{episode:04d}.pt")
            update = train["last_update"]
            writer.writerow({
                "episode": episode, "train_seed": args.trainSeed + episode - 1,
                "train_throughput_mbps": train["throughput"],
                "mean_window_reward": train["mean_window_reward"],
                "windows": train["windows"], "updates": train["updates"],
                "loss": update["loss"], "gradient_norm": update["gradient_norm"],
                "sample_count": update["sample_count"],
                "action_counts": " ".join(map(str, train["action_counts"])),
                "validation_throughput_mbps": validation["throughput"] if validation else math.nan,
                "validation_windows": validation["windows"] if validation else 0,
                "validation_action_counts": " ".join(map(str, validation["action_counts"])) if validation else "",
                "gradient_updates_total": agent.gradient_updates,
                "importance_weight_mean": update["importance_weight_mean"],
                "importance_weight_min": update["importance_weight_min"],
                "importance_weight_max": update["importance_weight_max"],
                "entropy_mean": update["entropy_mean"],
                "snr_bin_counts": " ".join(map(str, update["snr_bin_counts"])),
                "snr_bin_reward_means": " ".join(map(str, update["snr_bin_reward_means"])),
                "snr_bin_reward_stds": " ".join(map(str, update["snr_bin_reward_stds"])),
                "snr_bin_advantage_mins": " ".join(map(str, update["snr_bin_advantage_mins"])),
                "snr_bin_advantage_maxs": " ".join(map(str, update["snr_bin_advantage_maxs"])),
                "snr_bin_entropies": " ".join(map(str, update["snr_bin_entropies"])),
                "train_zero_fraction": train["zero_fraction"],
                "train_longest_zero_run": train["longest_zero_run"],
                "validation_zero_fraction": validation["zero_fraction"] if validation else math.nan,
                "validation_longest_zero_run": validation["longest_zero_run"] if validation else 0,
                "gamma": args.gamma, "epsilon": args.epsilon,
                "entropy_coef": args.entropyCoef,
            })
            handle.flush()
            agent.save_checkpoint(checkpoint, episode)
            if episode % args.validateEvery == 0:
                print(
                    f"episode={episode} train_tp={train['throughput']:.6f} "
                    f"val_tp={validation['throughput']:.6f} "
                    f"loss={update['loss']:.6f} entropy={update['entropy_mean']:.6f} "
                    f"rho={update['importance_weight_mean']:.6f} "
                    f"actions={' '.join(map(str, validation['action_counts']))}",
                    flush=True,
                )
    agent.save_policy(final_policy)
    plot_final_curves(milestones, args.validateEvery)
    del interface
    print(f"history={history}")
    print(f"final_policy={final_policy}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
