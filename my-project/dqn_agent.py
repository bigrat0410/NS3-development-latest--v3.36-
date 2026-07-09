from collections import deque
import math
import random

import numpy as np


class DqnAgent:
    """一个轻量级 DQN 速率选择器。

    为了避免引入 PyTorch/TensorFlow 依赖，这里用 numpy 手写两层全连接网络和
    Q-learning 更新。输入是 C++ 汇总后的链路状态，输出是每个 action 的 Q 值。
    """

    def __init__(self, num_actions=8, state_dim=7, hidden_dim=32):
        self.num_actions = num_actions
        self.state_dim = state_dim
        self.hidden_dim = hidden_dim
        # Q-learning 折扣因子。值越大，越重视未来 reward；这里略偏向当前链路反馈。
        self.gamma = 0.85
        # 手写梯度下降的学习率。移动场景噪声较大，学习率保持较小以避免 Q 值发散。
        self.lr = 1e-4
        self.batch_size = 32
        # 经验回放池，保存 (state, action, reward, next_state)。
        self.memory = deque(maxlen=5000)
        self.steps = 0
        # epsilon 从 1.0 指数衰减到 0.02：前期多探索，后期主要利用。
        self.epsilon_start = 1.0
        self.epsilon_end = 0.02
        self.epsilon_decay = 900.0
        # 固定随机种子让实验更容易复现。
        rng = np.random.default_rng(7)
        # 两层网络：state_dim -> hidden_dim -> num_actions，隐藏层使用 ReLU。
        self.w1 = rng.normal(0.0, 0.08, (self.state_dim, self.hidden_dim))
        self.b1 = np.zeros(self.hidden_dim)
        self.w2 = rng.normal(0.0, 0.08, (self.hidden_dim, self.num_actions))
        self.b2 = np.zeros(self.num_actions)
        self.last_state = None
        self.last_action = None

    def _epsilon(self):
        """计算当前步的探索率。"""
        return self.epsilon_end + (self.epsilon_start - self.epsilon_end) * math.exp(
            -float(self.steps) / self.epsilon_decay
        )

    def _state(self, env):
        """把 ns-3 环境结构体转换成神经网络输入向量。

        各字段做简单归一化，避免 SNR、reward、action 编号的数值尺度差异过大，
        影响手写梯度下降的稳定性。
        """
        num_actions = max(1, int(env.numActions))
        self.num_actions = num_actions
        return np.array(
            [
                # 瞬时 SNR、平均 SNR、SNR 波动程度。
                float(env.snrDb) / 35.0,
                float(env.meanSnrDb) / 35.0,
                float(env.stdSnrDb) / 10.0,
                # 最近 ACK 成功率，直接反映当前速率是否可靠。
                float(env.ackRatio),
                # 上一次 action 的相对位置，帮助网络知道当前是否处在高/低 MCS。
                float(env.lastAction) / float(num_actions),
                # 上一轮 reward，提供最近动作的收益反馈。
                float(env.reward) / 10.0,
                # 连续失败次数，裁剪到 10 次以内，避免异常值主导输入。
                min(float(env.consecutiveFailures), 10.0) / 10.0,
            ],
            dtype=np.float64,
        )

    def _forward(self, state):
        """前向传播，返回中间值以便 _learn 中复用做反向传播。"""
        z1 = state @ self.w1 + self.b1
        h1 = np.maximum(z1, 0.0)
        q = h1 @ self.w2 + self.b2
        return z1, h1, q

    def _snr_guided_action(self, snr_db):
        """后期探索率较低时使用的 SNR 经验规则。

        纯 DQN 在短仿真里可能还没学稳；这个规则把常识性的“SNR 越高 MCS 越高”
        注入策略，减少后期在明显不合适速率上的试错。
        """
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
            # 当前 env.reward 是“上一轮 action”得到的反馈，因此这里把上一状态和当前状态连成经验。
            self.memory.append((self.last_state, self.last_action, float(env.reward), state))
            self._learn()

        if int(env.consecutiveFailures) >= 6:
            # 连续失败过多时强制降到最低速率，优先恢复链路可用性。
            action = 1
        elif epsilon < 0.2:
            action = self._snr_guided_action(float(env.snrDb))
        elif random.random() < epsilon:
            # epsilon-greedy 探索。
            action = random.randint(1, self.num_actions)
        else:
            # 利用网络预测的 Q 值，选择当前状态下估计长期收益最高的 action。
            _, _, q = self._forward(state)
            action = int(np.argmax(q[: self.num_actions])) + 1

        self.last_state = state
        self.last_action = action
        self.steps += 1
        return action, epsilon

    def _learn(self):
        """从经验回放池采样并做一小步 Q-learning 更新。"""
        if len(self.memory) < self.batch_size:
            return
        batch = random.sample(self.memory, self.batch_size)
        for state, action, reward, next_state in batch:
            z1, h1, q = self._forward(state)
            _, _, next_q = self._forward(next_state)
            # Q-learning 目标：当前 reward + 折扣后的下一状态最大 Q 值。
            target = reward + self.gamma * float(np.max(next_q[: self.num_actions]))
            target = float(np.clip(target, -5.0, 10.0))
            error = float(np.clip(q[action - 1] - target, -5.0, 5.0))

            # 对输出层构造梯度，只更新本次 action 对应的 Q 值。
            grad_q = np.zeros_like(q)
            grad_q[action - 1] = error
            # 下面是两层全连接网络的手写反向传播：w2/b2 -> ReLU -> w1/b1。
            grad_w2 = np.outer(h1, grad_q)
            grad_b2 = grad_q
            grad_h1 = self.w2 @ grad_q
            grad_z1 = grad_h1 * (z1 > 0.0)
            grad_w1 = np.outer(state, grad_z1)
            grad_b1 = grad_z1

            # 梯度裁剪可以抑制偶发大 reward/大误差导致的权重爆炸。
            self.w2 -= self.lr * np.clip(grad_w2, -2.0, 2.0)
            self.b2 -= self.lr * np.clip(grad_b2, -2.0, 2.0)
            self.w1 -= self.lr * np.clip(grad_w1, -2.0, 2.0)
            self.b1 -= self.lr * np.clip(grad_b1, -2.0, 2.0)
