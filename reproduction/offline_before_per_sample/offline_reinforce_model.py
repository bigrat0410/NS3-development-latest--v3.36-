#!/usr/bin/env python3

import random

import torch
import torch.nn as nn
import torch.optim as optim
from torch.distributions import Categorical


class PolicyNetwork(nn.Module):
    """将四维链路状态映射为 HtMcs0-HtMcs7 的动作概率。"""

    def __init__(self, state_size=4, action_size=8):
        super().__init__()
        self.layers = nn.Sequential(
            nn.Linear(state_size, 16),
            nn.ReLU(),
            nn.Linear(16, 16),
            nn.ReLU(),
            nn.Linear(16, action_size),
        )
        for layer in self.layers:
            if isinstance(layer, nn.Linear):
                nn.init.xavier_uniform_(layer.weight)
        # 输出层从完全均匀的MCS策略开始，消除随机初始化动作偏置。
        nn.init.zeros_(self.layers[-1].weight)
        nn.init.zeros_(self.layers[-1].bias)

    def forward(self, state):
        return torch.softmax(self.layers(state), dim=-1)


class ReinRateAgent:
    """每10步按动作平均reward并等权更新动作类型的REINFORCE智能体。"""

    def __init__(
        self,
        state_size=4,
        action_size=8,
        lr=1e-3,
        gamma=0.0,
        epsilon=0.3,
    ):
        self.policy = PolicyNetwork(state_size, action_size)
        self.optimizer = optim.Adam(self.policy.parameters(), lr=lr)
        self.action_size = action_size
        self.gamma = gamma
        self.epsilon = epsilon
        self.batch_size = 10
        self.return_horizon = 1
        self.buffer_size = self.batch_size

        # 两个列表位置严格对齐：log_probs[i]对应rewards[i]。
        self.log_probs = []
        self.rewards = []
        self.states = []
        self.actions = []
        self.last_log_prob = None
        self.last_state = None
        self.last_action = None
        self.gradient_updates = 0
        self.last_update = None

    def select_action(self, state) -> int:
        """按30%均匀探索、70%策略分布采样动作，并保存log pi(a|s)。"""
        state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(1, -1)
        probabilities = self.policy(state_tensor)
        policy_distribution = Categorical(probabilities)
        if random.random() < self.epsilon:
            action = torch.tensor([random.randrange(self.action_size)])
        else:
            action = policy_distribution.sample()

        self.last_log_prob = policy_distribution.log_prob(action).reshape(())
        self.last_state = state_tensor.detach().squeeze(0)
        self.last_action = int(action.item())
        return int(action.item())

    def select_greedy_action(self, state) -> int:
        """冻结评估时选择策略概率最大的动作，不创建训练计算图。"""
        with torch.no_grad():
            state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(1, -1)
            return int(torch.argmax(self.policy(state_tensor), dim=-1).item())

    def action_probabilities(self, state):
        """返回日志所需的八个策略概率。"""
        with torch.no_grad():
            state_tensor = torch.as_tensor(state, dtype=torch.float32).reshape(1, -1)
            return self.policy(state_tensor).squeeze(0).tolist()

    def step(self, log_prob, reward, eligible=True):
        """记录一个逐聚合包transition；累计10步后更新一次。"""
        if log_prob is None:
            raise ValueError("log_prob must come from the action that produced reward")
        self.log_probs.append(log_prob)
        self.rewards.append(float(reward))
        self.states.append(self.last_state)
        self.actions.append(self.last_action)
        if not (
            len(self.log_probs)
            == len(self.rewards)
            == len(self.states)
            == len(self.actions)
        ):
            raise RuntimeError("log_prob and reward buffers are not aligned")
        if len(self.rewards) == self.buffer_size:
            return self._update_policy()
        if len(self.rewards) > self.buffer_size:
            raise RuntimeError("rolling training buffer exceeded its capacity")
        return None

    def _update_policy(self):
        """按MCS平均即时reward，归一化动作均值后等权更新。"""
        if len(self.log_probs) != self.buffer_size or len(self.rewards) != self.buffer_size:
            raise RuntimeError("policy update requires exactly 10 aligned samples")

        returns = list(self.rewards)
        returns_tensor = torch.tensor(returns, dtype=torch.float32)
        unique_actions = sorted(set(self.actions))
        action_mean_returns = []
        action_mean_log_probs = []
        action_sample_counts = []
        for action in unique_actions:
            indices = [index for index, value in enumerate(self.actions) if value == action]
            action_sample_counts.append(len(indices))
            action_mean_returns.append(returns_tensor[indices].mean())
            action_mean_log_probs.append(
                torch.stack([self.log_probs[index] for index in indices]).mean()
            )

        action_returns_tensor = torch.stack(action_mean_returns)
        normalized_action_returns = (
            action_returns_tensor - action_returns_tensor.mean()
        ) / (action_returns_tensor.std(unbiased=False) + 1e-9)
        loss = -(
            torch.stack(action_mean_log_probs)
            * normalized_action_returns.detach()
        ).mean()

        parameter_norm_before = torch.sqrt(
            sum((parameter.detach() ** 2).sum() for parameter in self.policy.parameters())
        ).item()
        self.optimizer.zero_grad()
        loss.backward()
        gradient_norm = torch.sqrt(
            sum(
                (parameter.grad.detach() ** 2).sum()
                for parameter in self.policy.parameters()
                if parameter.grad is not None
            )
        ).item()
        self.optimizer.step()
        parameter_norm_after = torch.sqrt(
            sum((parameter.detach() ** 2).sum() for parameter in self.policy.parameters())
        ).item()
        self.gradient_updates += 1

        result = {
            "update": self.gradient_updates,
            "loss": float(loss.item()),
            "return_mean": float(returns_tensor.mean().item()),
            "return_std": float(returns_tensor.std(unbiased=False).item()),
            "returns": returns,
            "rewards": list(self.rewards),
            "future_rewards": [],
            "reward_mean": sum(self.rewards) / self.batch_size,
            "unique_actions": unique_actions,
            "action_sample_counts": action_sample_counts,
            "action_mean_rewards": [float(value.item()) for value in action_returns_tensor],
            "normalized_action_rewards": [
                float(value.item()) for value in normalized_action_returns
            ],
            "evaluated_actions": len(unique_actions),
            "gradient_norm": gradient_norm,
            "parameter_norm_before": parameter_norm_before,
            "parameter_norm_after": parameter_norm_after,
            "learning_rate": self.optimizer.param_groups[0]["lr"],
            "buffer_before": self.buffer_size,
            "buffer_after": 0,
        }
        self.last_update = result
        self.log_probs.clear()
        self.rewards.clear()
        self.states.clear()
        self.actions.clear()
        return result

    def reset_rollout(self):
        """仿真轨迹结束时清理已经超出有效训练区的尾部。"""
        discarded = len(self.rewards)
        self.log_probs.clear()
        self.rewards.clear()
        self.states.clear()
        self.actions.clear()
        self.last_log_prob = None
        self.last_state = None
        self.last_action = None
        return discarded

    def save_policy(self, path):
        torch.save(self.policy.state_dict(), path)

    def load_policy(self, path):
        self.policy.load_state_dict(
            torch.load(path, map_location="cpu", weights_only=True)
        )

    def save_checkpoint(self, path, episode):
        # 不保存带计算图的滑动缓存；checkpoint只在episode边界写入。
        torch.save(
            {
                "episode": episode,
                "policy": self.policy.state_dict(),
                "optimizer": self.optimizer.state_dict(),
                "gradient_updates": self.gradient_updates,
            },
            path,
        )

    def load_checkpoint(self, path):
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
        self.policy.load_state_dict(checkpoint["policy"])
        self.optimizer.load_state_dict(checkpoint["optimizer"])
        self.gradient_updates = int(checkpoint.get("gradient_updates", 0))
        self.reset_rollout()
        return int(checkpoint["episode"])


# 保留旧导入名，避免现有辅助脚本因类名变化而失效。
BatchReinforceAgent = ReinRateAgent
