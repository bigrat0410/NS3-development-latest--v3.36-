from collections import defaultdict, deque
import random


class MabAgent:
    def __init__(self, num_actions=8, epsilon=0.1, window_size=30):
        self.num_actions = num_actions
        self.epsilon = epsilon
        self.window_size = window_size
        self.snr_edges = [edge for edge in range(-5, 40, 5)]
        self.rewards = defaultdict(lambda: [deque(maxlen=self.window_size) for _ in range(self.num_actions)])

    def _bin(self, snr_db):
        for index, edge in enumerate(self.snr_edges):
            if snr_db <= edge:
                return index
        return len(self.snr_edges)

    def select_action(self, env):
        self.num_actions = max(1, int(env.numActions))
        bin_id = self._bin(float(env.snrDb))
        buffers = self.rewards[bin_id]

        untested = [action for action in range(1, self.num_actions + 1) if not buffers[action - 1]]
        if untested:
            return random.choice(untested), self.epsilon

        if random.random() < self.epsilon:
            return random.randint(1, self.num_actions), self.epsilon

        means = [
            sum(buffers[action - 1]) / len(buffers[action - 1])
            for action in range(1, self.num_actions + 1)
        ]
        return int(max(range(1, self.num_actions + 1), key=lambda action: means[action - 1])), self.epsilon

    def observe(self, env):
        action = int(max(1, min(int(env.lastAction), max(1, int(env.numActions)))))
        self.rewards[self._bin(float(env.snrDb))][action - 1].append(float(env.reward))
