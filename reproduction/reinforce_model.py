#!/usr/bin/env python3

import numpy as np
import torch
import torch.nn as nn #导入神经网络模块
import torch.optim as optim #导入优化器模块
from torch.distributions import Categorical #导入分类分布，可以理解为根据一组概率分布，按照概率抽签


#源码策略网络：输入1个处理后的SNR，输出HtMcs0-HtMcs7的8个动作概率
class PolicyNetwork(nn.Module):
    def __init__(self, state_size, action_size):
        super().__init__()
        self.fc = nn.Sequential(
            nn.Linear(state_size, 16),
            nn.ReLU(),
            nn.Linear(16, 16),
            nn.ReLU(),
            nn.Linear(16, 16),
            nn.ReLU(),
            nn.Linear(16, action_size),
        )

    def forward(self, state): #前向传播，定义如何计算输出
        return torch.softmax(self.fc(state), dim=-1)


#源码REINFORCE智能体：保留epsilon随机动作、Categorical采样和逐反馈更新
class ReinforceAgent:
    def __init__(self, state_size, action_size, lr=1e-4):
        self.policy = PolicyNetwork(state_size, action_size)
        self.optimizer = optim.Adam(self.policy.parameters(), lr=lr)
        self.saved_log_probs = [] #注意这里保存对数概率，例如log(0.45) ≈ -0.799
        self.rewards = []
        self.eps = 1e-8
        self.train_mode = True #训练模式

        #源码只对四个Linear层的权重执行Xavier初始化，bias保持PyTorch默认值
        nn.init.xavier_uniform_(self.policy.fc[0].weight)
        nn.init.xavier_uniform_(self.policy.fc[2].weight)
        nn.init.xavier_uniform_(self.policy.fc[4].weight)
        nn.init.xavier_uniform_(self.policy.fc[6].weight)


# 切换推理模式
    def eval(self):
        self.train_mode = False
        self.policy.eval()
# 切换训练模式
    def train(self):
        self.train_mode = True
        self.policy.train()


# 保存模型
    def save_model(self, model_name):
        torch.save(self.policy.state_dict(), model_name)
# 加载模型
    def load_model(self, model_name):
        self.policy.load_state_dict(torch.load(model_name))

#动作选择函数
    def choose_action(self, state, max_mcs=None, epsilon=0.3, greedy=False):
        if not torch.is_tensor(state):
            state = torch.tensor(state, dtype=torch.float32).reshape(1, -1)#张量转换
        probabilities = self.policy(state)#计算策略概率

        if self.train_mode:
            distribution = Categorical(probabilities)
            #源码逻辑：30%概率均匀随机探索，否则仍从策略分布随机采样
            #返回概率最大的动作索引，保存它的对数概率
            if greedy:
                action = torch.argmax(probabilities, dim=-1)
                self.saved_log_probs.append(distribution.log_prob(action))
            elif np.random.rand() < epsilon:
                action = torch.tensor([np.random.choice(len(probabilities[0]))])
                self.saved_log_probs.append(distribution.log_prob(action))
                #如果提供了max_mcs,就强制使用
            elif max_mcs:
                action = torch.tensor(max_mcs)
                self.saved_log_probs.append(distribution.log_prob(action))
            else:
                action = distribution.sample()
                self.saved_log_probs.append(distribution.log_prob(action))
        else:
            #加载模型推理时选择概率最大的动作
            action = torch.argmax(probabilities)

        return action.item()
    
# 更新函数
    def update(self, gamma=0.99):
        #源码会在每个新反馈到来时调用，因此通常只有一个reward和一个log_prob
        cumulative_return = 0
        policy_loss = []
        returns = []
        #reward的倒序处理
        for reward in self.rewards[::-1]:
            cumulative_return = reward + gamma * cumulative_return
            returns.insert(0, cumulative_return)

#reward标准化
        returns = torch.tensor(returns)
        if returns.std() > 0:
            returns = (returns - returns.mean()) / (returns.std() + self.eps)
            
#计算策略损失
        for log_probability, cumulative_return in zip(self.saved_log_probs, returns):
            policy_loss.append(-log_probability * cumulative_return)

        self.optimizer.zero_grad()
        loss = torch.cat(policy_loss).sum()
        loss.backward()
        self.optimizer.step()

        del self.rewards[:]
        del self.saved_log_probs[:]
        return loss.item()
