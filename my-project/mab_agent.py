from collections import defaultdict, deque
import random


class MabAgent:
    """按 SNR 分桶的 epsilon-greedy 多臂老虎机。

    这里没有训练神经网络，也不预测未来状态。它把相近 SNR 看作同一种上下文，
    每个 SNR 桶里分别维护每个 action 的最近 reward 均值，然后在“探索”和
    “选择当前均值最高 action”之间切换。
    """

    def __init__(self, num_actions=8, epsilon=0.1, window_size=30):
        self.num_actions = num_actions
        # epsilon 是探索概率：每次决策有 epsilon 的概率随机选一个速率档位。
        self.epsilon = epsilon
        # 每个 action 只保留最近 window_size 个 reward，让算法能跟上移动场景的信道变化。
        self.window_size = window_size
        # SNR 桶边界：(-inf,-5], (-5,0], ... , (35,inf)。
        self.snr_edges = [edge for edge in range(-5, 40, 5)]
        # rewards[snr_bucket][action-1] 是一个 deque，保存该 SNR 桶下该 action 的近期收益。
        self.rewards = defaultdict(lambda: [deque(maxlen=self.window_size) for _ in range(self.num_actions)])

    def _bin(self, snr_db):
        """把连续 SNR 值映射到离散桶，减少学习表规模。"""
        for index, edge in enumerate(self.snr_edges):
            if snr_db <= edge:
                return index
        return len(self.snr_edges)

    def select_action(self, env):
        """根据当前 SNR 桶选择下一次发送速率。

        action 使用 1-based 编号，和 C++ 共享内存结构保持一致。
        """
        self.num_actions = max(1, int(env.numActions))
        bin_id = self._bin(float(env.snrDb))
        buffers = self.rewards[bin_id]

        # 先保证每个 action 至少试过一次，否则均值比较会永久忽略未测试档位。
        untested = [action for action in range(1, self.num_actions + 1) if not buffers[action - 1]]
        if untested:
            return random.choice(untested), self.epsilon

        # 探索：随机尝试一个 action，防止早期偶然 reward 导致算法锁死在次优速率。
        if random.random() < self.epsilon:
            return random.randint(1, self.num_actions), self.epsilon

        # 利用：选择当前 SNR 桶下近期平均 reward 最高的 action。
        means = [
            sum(buffers[action - 1]) / len(buffers[action - 1])
            for action in range(1, self.num_actions + 1)
        ]
        return int(max(range(1, self.num_actions + 1), key=lambda action: means[action - 1])), self.epsilon

    def observe(self, env):
        """把 C++ 上一轮计算出的 reward 写回表格。

        env.lastAction 是上一轮真正发送时用的 action；env.reward 是这个 action
        在当前链路反馈下得到的收益。
        """
        action = int(max(1, min(int(env.lastAction), max(1, int(env.numActions)))))
        self.rewards[self._bin(float(env.snrDb))][action - 1].append(float(env.reward))
