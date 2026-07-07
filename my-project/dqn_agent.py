from collections import deque
import math
import random

import numpy as np


class DqnAgent:
    def __init__(self, num_actions=8, state_dim=7, hidden_dim=32):
        self.num_actions = num_actions
        self.state_dim = state_dim
        self.hidden_dim = hidden_dim
        self.gamma = 0.85
        self.lr = 1e-4
        self.batch_size = 32
        self.memory = deque(maxlen=5000)
        self.steps = 0
        self.epsilon_start = 1.0
        self.epsilon_end = 0.02
        self.epsilon_decay = 900.0
        rng = np.random.default_rng(7)
        self.w1 = rng.normal(0.0, 0.08, (self.state_dim, self.hidden_dim))
        self.b1 = np.zeros(self.hidden_dim)
        self.w2 = rng.normal(0.0, 0.08, (self.hidden_dim, self.num_actions))
        self.b2 = np.zeros(self.num_actions)
        self.last_state = None
        self.last_action = None

    def _epsilon(self):
        return self.epsilon_end + (self.epsilon_start - self.epsilon_end) * math.exp(
            -float(self.steps) / self.epsilon_decay
        )

    def _state(self, env):
        num_actions = max(1, int(env.numActions))
        self.num_actions = num_actions
        return np.array(
            [
                float(env.snrDb) / 35.0,
                float(env.meanSnrDb) / 35.0,
                float(env.stdSnrDb) / 10.0,
                float(env.ackRatio),
                float(env.lastAction) / float(num_actions),
                float(env.reward) / 10.0,
                min(float(env.consecutiveFailures), 10.0) / 10.0,
            ],
            dtype=np.float64,
        )

    def _forward(self, state):
        z1 = state @ self.w1 + self.b1
        h1 = np.maximum(z1, 0.0)
        q = h1 @ self.w2 + self.b2
        return z1, h1, q

    def _snr_guided_action(self, snr_db):
        thresholds = [8.0, 11.0, 14.0, 17.0, 20.0, 23.0, 26.0]
        action = 1
        for threshold in thresholds:
            if snr_db >= threshold:
                action += 1
        return max(1, min(action, self.num_actions))

    def select_action(self, env):
        state = self._state(env)
        epsilon = self._epsilon()

        if self.last_state is not None and self.last_action is not None:
            self.memory.append((self.last_state, self.last_action, float(env.reward), state))
            self._learn()

        if int(env.consecutiveFailures) >= 6:
            action = 1
        elif epsilon < 0.2:
            action = self._snr_guided_action(float(env.snrDb))
        elif random.random() < epsilon:
            action = random.randint(1, self.num_actions)
        else:
            _, _, q = self._forward(state)
            action = int(np.argmax(q[: self.num_actions])) + 1

        self.last_state = state
        self.last_action = action
        self.steps += 1
        return action, epsilon

    def _learn(self):
        if len(self.memory) < self.batch_size:
            return
        batch = random.sample(self.memory, self.batch_size)
        for state, action, reward, next_state in batch:
            z1, h1, q = self._forward(state)
            _, _, next_q = self._forward(next_state)
            target = reward + self.gamma * float(np.max(next_q[: self.num_actions]))
            target = float(np.clip(target, -5.0, 10.0))
            error = float(np.clip(q[action - 1] - target, -5.0, 5.0))

            grad_q = np.zeros_like(q)
            grad_q[action - 1] = error
            grad_w2 = np.outer(h1, grad_q)
            grad_b2 = grad_q
            grad_h1 = self.w2 @ grad_q
            grad_z1 = grad_h1 * (z1 > 0.0)
            grad_w1 = np.outer(state, grad_z1)
            grad_b1 = grad_z1

            self.w2 -= self.lr * np.clip(grad_w2, -2.0, 2.0)
            self.b2 -= self.lr * np.clip(grad_b2, -2.0, 2.0)
            self.w1 -= self.lr * np.clip(grad_w1, -2.0, 2.0)
            self.b1 -= self.lr * np.clip(grad_b1, -2.0, 2.0)
