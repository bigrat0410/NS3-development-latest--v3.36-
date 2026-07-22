#!/usr/bin/env python3

import random

import torch
import torch.nn as nn
import torch.optim as optim


class PolicyNetwork(nn.Module):
    """Map four normalized link features to HtMcs0-HtMcs7 probabilities."""

    def __init__(self, state_size=4, action_size=8):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(state_size, 256),
            nn.ReLU(),
            nn.Linear(256, 256),
            nn.ReLU(),
            nn.Linear(256, 256),
            nn.ReLU(),
            nn.Linear(256, action_size),
        )
        for layer in self.layers:
            if isinstance(layer, nn.Linear):
                nn.init.xavier_uniform_(layer.weight)
        # Paper Table II: all layers use Xavier Uniform. The output layer keeps
        # its Xavier Uniform weights (previously zeroed, which forced a uniform
        # 0.125 policy and symmetric, self-cancelling gradients at start).

    def forward(self, state):
        return torch.softmax(self.layers(state), dim=-1)


class ReinRateAgent:
    """Episode-level policy gradient over consecutive 20-packet windows."""

    def __init__(self, state_size=4, action_size=8, lr=1e-4, gamma=0.99, epsilon=0.3,
                 update_batch=None, normalize_returns=True, loss_reduction="mean",
                 use_behavior_probability=False):
        if not 0.0 <= gamma <= 1.0:
            raise ValueError("gamma must be in [0, 1]")
        if not 0.0 <= epsilon <= 1.0:
            raise ValueError("epsilon must be in [0, 1]")
        if loss_reduction not in ("mean", "sum"):
            raise ValueError("loss_reduction must be 'mean' or 'sum'")
        self.policy = PolicyNetwork(state_size, action_size)
        self.optimizer = optim.Adam(self.policy.parameters(), lr=lr)
        self.action_size = action_size
        self.gamma = gamma
        self.epsilon = epsilon
        # Mini-batch cadence: flush an update every `update_batch` windows so that
        # gamma still discounts over a real multi-step sequence (unlike per-window
        # updates where the buffer is length-1 and gamma never engages), while the
        # number of gradient steps per episode grows ~windows/update_batch. None =
        # original behaviour (one update per whole episode via finish_episode()).
        self.update_batch = update_batch
        self.normalize_returns = normalize_returns
        self.loss_reduction = loss_reduction
        self.use_behavior_probability = use_behavior_probability
        self.states = []
        self.actions = []
        self.rewards = []
        self.last_state = None
        self.last_action = None
        self.last_exploratory = False
        self.gradient_updates = 0
        self.last_update = None

    def select_action(self, state) -> int:
        """Paper Algorithm 1, lines 6-9: with prob epsilon pick a uniform random
        MCS, otherwise SAMPLE from the softmax policy pi(a|s) (not argmax)."""
        state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(-1)
        with torch.no_grad():
            probabilities = self.policy(state_tensor)
        self.last_exploratory = random.random() < self.epsilon
        if self.last_exploratory:
            action = random.randrange(self.action_size)
        else:
            action = int(torch.multinomial(probabilities, 1).item())
        self.last_state = state_tensor.clone()
        self.last_action = action
        return action

    def select_greedy_action(self, state) -> int:
        with torch.no_grad():
            state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(1, -1)
            return int(torch.argmax(self.policy(state_tensor), dim=-1).item())

    def action_probabilities(self, state):
        with torch.no_grad():
            state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(1, -1)
            return self.policy(state_tensor).squeeze(0).tolist()

    def record_window(self, reward):
        """Align one completed 20-packet (or final partial) window to its action."""
        if self.last_state is None or self.last_action is None:
            raise RuntimeError("select_action must precede record_window")
        self.states.append(self.last_state)
        self.actions.append(self.last_action)
        self.rewards.append(float(reward))
        if not (len(self.states) == len(self.actions) == len(self.rewards)):
            raise RuntimeError("episode state/action/window-reward buffers are not aligned")
        # Mini-batch cadence: flush a gradient step once the buffer fills.
        if self.update_batch and len(self.rewards) >= self.update_batch:
            self.finish_episode()

    def finish_episode(self):
        """Discount between windows and perform exactly one optimizer step."""
        if not self.rewards:
            raise RuntimeError("cannot finish an empty episode")

        discounted_returns = []
        value = 0.0
        for reward in reversed(self.rewards):
            value = reward + self.gamma * value
            discounted_returns.append(value)
        discounted_returns.reverse()

        states = torch.stack(self.states)
        actions = torch.tensor(self.actions, dtype=torch.long)
        returns = torch.tensor(discounted_returns, dtype=torch.float32)
        # Paper Algorithm 1, line 13: normalize the returns. This subtracts a
        # per-episode baseline (mean) and scales by std, so that windows that did
        # better than the episode average get a positive weight and those that did
        # worse get a negative one -- essential when every raw reward is non-negative.
        normalized_returns = returns
        if (self.normalize_returns and returns.numel() > 1
                and returns.std(unbiased=False) > 0):
            normalized_returns = (returns - returns.mean()) / (
                returns.std(unbiased=False) + 1e-8
            )
        probabilities = self.policy(states)
        if self.use_behavior_probability:
            probabilities = (
                (1.0 - self.epsilon) * probabilities
                + self.epsilon / self.action_size
            )
        selected_log_probs = torch.log(
            probabilities.gather(1, actions.unsqueeze(1)).squeeze(1) + 1e-8
        )
        weighted_log_probs = selected_log_probs * normalized_returns.detach()
        loss = -(
            weighted_log_probs.sum()
            if self.loss_reduction == "sum"
            else weighted_log_probs.mean()
        )

        parameter_norm_before = self._parameter_norm()
        self.optimizer.zero_grad()
        loss.backward()
        gradient_norm = torch.sqrt(sum(
            (parameter.grad.detach() ** 2).sum()
            for parameter in self.policy.parameters()
            if parameter.grad is not None
        )).item()
        self.optimizer.step()
        self.gradient_updates += 1

        result = {
            "update": self.gradient_updates,
            "loss": float(loss.item()),
            "return_mean": float(returns.mean().item()),
            "return_std": float(returns.std(unbiased=False).item()),
            "return_min": float(returns.min().item()),
            "return_max": float(returns.max().item()),
            "returns": discounted_returns,
            "rewards": list(self.rewards),
            "reward_mean": sum(self.rewards) / len(self.rewards),
            "gradient_norm": gradient_norm,
            "parameter_norm_before": parameter_norm_before,
            "parameter_norm_after": self._parameter_norm(),
            "learning_rate": self.optimizer.param_groups[0]["lr"],
            "sample_count": len(self.rewards),
        }
        self.last_update = result
        self.reset_rollout()
        return result

    def _parameter_norm(self):
        return torch.sqrt(sum(
            (parameter.detach() ** 2).sum() for parameter in self.policy.parameters()
        )).item()

    def reset_rollout(self):
        discarded = len(self.rewards)
        self.states.clear()
        self.actions.clear()
        self.rewards.clear()
        self.last_state = None
        self.last_action = None
        self.last_exploratory = False
        return discarded

    def save_policy(self, path):
        torch.save(self.policy.state_dict(), path)

    def load_policy(self, path):
        self.policy.load_state_dict(torch.load(path, map_location="cpu", weights_only=True))

    def save_checkpoint(self, path, episode):
        torch.save({
            "episode": episode,
            "policy": self.policy.state_dict(),
            "optimizer": self.optimizer.state_dict(),
            "gradient_updates": self.gradient_updates,
            "gamma": self.gamma,
            "epsilon": self.epsilon,
            "normalize_returns": self.normalize_returns,
            "loss_reduction": self.loss_reduction,
            "use_behavior_probability": self.use_behavior_probability,
        }, path)

    def load_checkpoint(self, path):
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
        self.policy.load_state_dict(checkpoint["policy"])
        self.optimizer.load_state_dict(checkpoint["optimizer"])
        self.gradient_updates = int(checkpoint.get("gradient_updates", 0))
        self.reset_rollout()
        return int(checkpoint["episode"])


BatchReinforceAgent = ReinRateAgent
