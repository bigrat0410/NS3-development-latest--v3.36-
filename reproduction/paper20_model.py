#!/usr/bin/env python3

import math

import torch
import torch.nn as nn
import torch.optim as optim
from torch.distributions import Categorical


class PolicyNetwork(nn.Module):
    def __init__(self, state_size=4, action_size=8):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(state_size, 16),
            nn.ReLU(),
            nn.Linear(16, 16),
            nn.ReLU(),
            nn.Linear(16, 16),
            nn.ReLU(),
            nn.Linear(16, action_size),
        )
        for index in (0, 2, 4, 6):
            nn.init.xavier_uniform_(self.layers[index].weight)

    def forward(self, state):
        return torch.softmax(self.layers(state), dim=-1)


class Paper20ReinforceAgent:
    """One sampled MCS is held for a 20-packet decision window."""

    def __init__(self, lr=1e-4):
        self.policy = PolicyNetwork()
        self.optimizer = optim.Adam(self.policy.parameters(), lr=lr)
        self.pending_log_probability = None
        self.return_count = 0
        self.return_mean = 0.0
        self.return_m2 = 0.0

    def choose_action(self, state, epsilon=0.3):
        tensor = torch.tensor(state, dtype=torch.float32).reshape(1, -1)
        probabilities = self.policy(tensor)

        # The executed policy is an epsilon mixture, so exploratory actions have
        # a log-probability under the same behavior distribution used to sample.
        behavior = (1.0 - epsilon) * probabilities + epsilon / probabilities.shape[-1]
        distribution = Categorical(behavior)
        action = distribution.sample()
        self.pending_log_probability = distribution.log_prob(action)
        return action.item()

    def update(self, window_return):
        if self.pending_log_probability is None:
            raise RuntimeError("received a window return without a sampled action")

        if self.return_count >= 8:
            variance = self.return_m2 / self.return_count
            advantage = (window_return - self.return_mean) / max(math.sqrt(variance), 1e-8)
        else:
            advantage = window_return

        loss = -self.pending_log_probability * float(advantage)
        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        self.return_count += 1
        delta = window_return - self.return_mean
        self.return_mean += delta / self.return_count
        self.return_m2 += delta * (window_return - self.return_mean)
        self.pending_log_probability = None
        return loss.item(), advantage

    def save_model(self, path):
        torch.save(self.policy.state_dict(), path)
