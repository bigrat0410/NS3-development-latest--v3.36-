# 【中文注释总览】
# 本文件是 Python AI 大脑和 ns-3 仿真之间的桥梁。
# 它通过 py_interface.Ns3AIRL 连接 C++ 侧 Ns3AIRL<AiConstantRateEnv, AiConstantRateAct> 共享数据块。
#
# 强化学习三要素：
# - Observation / 状态空间：ReinrateEnv，必须与 C++ AiConstantRateEnv 字段顺序和二进制布局保持一致。
# - Action / 动作空间：ReinrateAct，必须与 C++ AiConstantRateAct 字段顺序和二进制布局保持一致。
# - Reward / 奖励函数：train_reinforce / train_dqn 中调用 calculate_reward、reward_tpt_gain 等函数计算。
#
# 通信流程：with c.rl as data 读取 C++ 写入的 data.env；Python 推理/训练后设置 data.act；
# 退出上下文后 ns3-ai 将 act 写回，C++ 下一次速率选择时读取 next_mcs。
from ctypes import *
from py_interface import *
from model import DQNAgent, ReinforceAgent
import numpy as np
import time
import os
from tqdm import tqdm

# 【Observation 定义】Python 侧状态结构体，字段和 C++ AiConstantRateEnv 逐字节对应。
class ReinrateEnv(Structure):
    _pack_ = 1
    _fields_ = [
        # 【状态字段】mcs：当前 MCS 索引，代表 802.11n HtMcs0-HtMcs7。
        ('mcs', c_ubyte),
        # 【状态字段】cw：MAC 层竞争窗口，反映退避和拥塞强度。
        ('cw', c_ushort),
        # 【状态字段】throughput：统计窗口内吞吐量，是 Reward 设计的核心反馈。
        ('throughput', c_double),
        # 【状态字段】snr：C++ 从 PHY 监听信号/噪声得到的链路质量。
        ('snr', c_double)
    ]


# 【Action 定义】Python 侧动作结构体，字段和 C++ AiConstantRateAct 逐字节对应。
class ReinrateAct(Structure):
    _pack_ = 1
    _fields_ = [
        ('nss', c_ubyte),
        # 【动作字段】next_mcs：智能体选择的下一 MCS，C++ 将其落到 WifiTxVector。
        ('next_mcs', c_ubyte)
    ]


class ReinrateContainer:
    use_ns3ai = True

    def __init__(self, uid: int = 2335,model_name='',retrain=False) -> None:
        # 【ns3-ai 通信桥梁】uid 必须与 C++ memblock id 一致；Ns3AIRL 负责同步 env/act。
        self.rl = Ns3AIRL(uid, ReinrateEnv, ReinrateAct)
        # 【策略模型】默认使用 REINFORCE，输入维度 1、动作维度 8，表示基于 SNR 选择 MCS0-MCS7。
        self.agent = ReinforceAgent(1,8)
        self.isFinished = False
        self.startTime = time.time()
        self.packetTotal = 0
        self.packetLastSecond = 0
        self.last_tpt = 0
        self.last_state = {'throughput': 0, 'cw': 0, 'mcs': 0}
        self.cur_state = {'throughput': 0, 'cw': 0, 'mcs': 0}
        self.last_action = {'nss': 0, 'next_mcs': 0}
        self.log_snr = 0
        self.isInference = False
        self.isFirstEpisode = True
        self.snrPool = []
        self.packet_count = 0
        # check if model file exists then load the model
        if os.path.exists(model_name):
            self.load_model(model_name, retrain)
            self.isInference = True if not retrain else False


    # 【DQN 训练路径】备用 DQN 逻辑：读取 env 作为状态，计算 reward，选择 next_mcs，并存入经验回放。
    def train_dqn(self, env: ReinrateEnv, act: ReinrateAct) -> ReinrateAct:
        if self.isInference:
            return self.inference_dqn(env, act)
        self.cur_state['throughput'] = env.throughput
        self.cur_state['cw'] = 0
        self.cur_state['mcs'] = env.mcs
        self.cur_state['snr'] = np.log(env.snr+1)
        one_hot = [0]*8  # Initialize the array with zeros

        if self.last_state['mcs'] == 0 and self.last_state['cw'] == 0 and self.last_state['throughput'] == 0:
            self.last_state['mcs'] = env.mcs
            self.last_state['cw'] = 0
            self.last_state['throughput'] = env.throughput
            self.last_state['snr'] = np.log(env.snr+1)
            self.last_action['nss'] = act.nss
            self.last_action['next_mcs'] = act.next_mcs
            return act
        rl_input = [self.last_state['snr']]
        reward = self.reward_function(self.cur_state)
        # 【Action 产生】策略网络输出动作分布或 Q 值，最终选择 0-7 中的一个 MCS。
        next_mcs = self.agent.choose_action(rl_input)
        # 【动作下发】把 Python 决策写入共享 Action；C++ 侧随后读取并更新发送速率。
        act.next_mcs = next_mcs
        print(f'[Training] mcs: {self.last_state["mcs"]} -> {self.last_action["next_mcs"]}\t, cur_tpt: {self.cur_state["throughput"]:.2f} Mbps, FER: {self.cur_state["fer"]:.2f}, reward: {reward:.2f}, Snr: {self.cur_state["snr"]:.2f}')

        last_rl_input = [self.last_state['snr']]
        self.agent.remember(last_rl_input,
                            self.last_action['next_mcs'], 
                            reward, 
                            rl_input,
                            self.isFinished)
        self.agent.rewards.append(reward)



        for key in self.cur_state:
            self.last_state[key] = self.cur_state[key]
        self.last_action['next_mcs'] = next_mcs

        return act
    # 【REINFORCE 训练路径】主路径：读取 C++ 状态，计算上一动作奖励，更新策略，再输出下一 MCS。
    def train_reinforce(self, env: ReinrateEnv, act: ReinrateAct) -> ReinrateAct:
        if self.isInference:
            return self.inference_reinforce(env, act)

        # 【状态预处理】对 SNR 做 log(snr+1) 缩放，降低数值动态范围，作为策略网络输入。
        scaled_snr = np.log(env.snr+1).astype(np.float32)
        if self.isFirstEpisode == True :
            self.last_state['mcs'] = env.mcs
            rl_input = [scaled_snr]
            next_mcs = self.agent.choose_action(rl_input)
            self.last_action['next_mcs'] = next_mcs
            act.next_mcs = next_mcs
            self.isFirstEpisode = False
            return act

        # 【Reward 计算】奖励由上一动作/当前吞吐或理论速率关系得到，用来强化高吞吐且合适的 MCS。
        reward = self.calculate_reward(self.last_action['next_mcs'], self.last_state['mcs'])
        self.agent.rewards.append(reward)
        self.agent.update()
        print(f'[Training] mcs: {self.last_state["mcs"]} -> {self.last_action["next_mcs"]}\t, cur_tpt: {env.throughput:.2f} Mbps, reward: {reward:.2f}, Snr: {scaled_snr:.2f}')
        rl_input = [scaled_snr,env.throughput,env.cw,env.fer]
        next_mcs = self.agent.choose_action(rl_input)
        
        act.next_mcs = next_mcs

        self.last_state['mcs'] = env.mcs
        self.last_state['throughput'] = env.throughput
        self.last_action['next_mcs'] = next_mcs
        return act

    def inference_reinforce(self, env: ReinrateEnv, act: ReinrateAct) -> ReinrateAct:
            scaled_snr = np.log(env.snr+1).astype(np.float32)
            # push scaled_snr to self.snrPool, keep length of 5
            self.snrPool.append(scaled_snr)
            if len(self.snrPool) > 5:
                self.snrPool.pop(0)
            # remove the outliers in snrPool
            if len(self.snrPool) == 5:
                _mean = np.mean(self.snrPool)
                _std = np.std(self.snrPool)
                _max = max(self.snrPool)
                _min = min(self.snrPool)
                if abs(_max-_mean) > 1.5*_std:
                    self.snrPool.remove(_max)
                elif abs(_min-_mean) > 1.5*_std:
                    self.snrPool.remove(_min)
            rl_input = [np.mean(self.snrPool)]

            next_mcs = self.agent.choose_action(rl_input)
            if next_mcs < env.max_mcs:
                next_mcs = env.max_mcs
            act.next_mcs = next_mcs
            if self.packet_count > 10:
                self.packet_count = 0
            self.packet_count += 1
            return act        
    def inference_dqn(self, env: ReinrateEnv, act: ReinrateAct) -> ReinrateAct:
            # scale snr by 100 
            self.cur_state['throughput'] = env.throughput
            self.cur_state['cw'] = 0
            self.cur_state['mcs'] = env.mcs
            self.cur_state['snr'] = np.log(env.snr+1)

            one_hot = [0]*8  # Initialize the array with zeros
            rl_input = [self.cur_state['snr']]
            next_mcs = self.agent.choose_action(rl_input)
            
            act.next_mcs = next_mcs

            return act

    # 【802.11n 速率表】给出不同 MCS/带宽下的理论最大吞吐，用于 Reward 归一化。
    def calculate_max_throughput(self, mcs_index, channel_bandwidth):
        """
        Calculate the theoretical max throughput for 802.11n with a single spatial stream and a long guard interval.
        
        Args:
        mcs_index: The MCS index (0-7 for 802.11n with a single spatial stream)
        channel_bandwidth: The channel bandwidth in MHz (20 or 40 for 802.11n)
        
        Returns:
        The theoretical max throughput in Mbps.
        """
        
        # Data rates for 802.11n with a single spatial stream and a long guard interval
        data_rates_20MHz = [5.4, 9.8, 13.6, 16.9, 22.2, 26.1, 28.1, 29.6]  # in Mbps
        data_rates_40MHz = [13.5, 27, 40.5, 54, 81, 108, 121.5, 135]  # in Mbps

        if channel_bandwidth == 20:
            max_data_rate = data_rates_20MHz[mcs_index]
        elif channel_bandwidth == 40:
            max_data_rate = data_rates_40MHz[mcs_index]
        else:
            raise ValueError("Invalid channel bandwidth. It should be either 20 or 40 MHz for 802.11n.")

        return max_data_rate


    # 【Reward 函数】将实际吞吐与理论最大吞吐比较，并用 MCS/7 加权，鼓励高阶且有效的 MCS。
    def calculate_reward(self, achieved_throughput, mcs_index, channel_bandwidth):
        """
        Calculate the reward based on the difference between the achieved and theoretical max throughput.
        
        Args:
        achieved_throughput: The achieved throughput in Mbps
        mcs_index: The MCS index (0-7 for 802.11n with a single spatial stream)
        channel_bandwidth: The channel bandwidth in MHz (20 or 40 for 802.11n)
        
        Returns:
        The reward, a number between 0 and 1.
        """
        # Calculate the theoretical max throughput
        max_throughput = self.calculate_max_throughput(mcs_index, channel_bandwidth)

        # Calculate the reward as 1 divided by (1 + difference)
        reward = achieved_throughput / max_throughput 
        reward = mcs_index/7.0 * reward**3
        return reward

        
    def reward_tpt_gain(self, tpt):
        if tpt - self.last_state['throughput']>0:
            reward = 1
        else:
            reward = -1

        reward = self.cur_state['mcs']/7.0 * reward**3
        return reward

    def save_model(self, filename):
        self.agent.save_model(filename)
    def load_model(self, filename, retrain=False):
        self.agent.load_model(filename)
        if retrain:
            self.agent.train()
        else:
            self.agent.eval()


if __name__ == '__main__':
    mempool_key = 1347 # memory pool key, arbitrary integer large than 1000
    mem_size = 4096 # memory pool size in bytes

    memblock_key = 2335 # memory block key in the memory pool, arbitrary integer, and need to keep the same in the ns-3 script
    iters_count = 0

    sim_runs = 20
    for i in range(sim_runs):
        rand_seed = np.random.randint(0, 100000)
        ns3Settings = {"apManager":"ns3::rl-rateWifiManager","staManager":"ns3::rl-rateWifiManager","Run":i+1}
        exp = Experiment(mempool_key, mem_size, 'rate-control', '../../', using_waf=False)
        exp.reset()
        c = ReinrateContainer(memblock_key,model_name='model.pt',retrain=False)
        pro = exp.run(setting=ns3Settings, show_output=True)
        print("run rate-control", ns3Settings)
        epochs = 1000
        with tqdm(total=epochs) as pbar:
            while not c.rl.isFinish():
                # 【双端交互时序】进入上下文时读取 C++ env；退出上下文时把 Python 修改后的 act 同步回 C++。
                with c.rl as data:
                    if data == None:
                        break
                    # 【AI 推理/训练闭环】对一个状态样本执行策略更新并产生下一速率动作。
                    data.act = c.train_reinforce(data.env, data.act)
                    pbar.update(1)
                    if iters_count > epochs:
                        c.save_model('model.pt')
                        print('model saved')
                        break
                    iters_count += 1
        pro.wait()
    del exp
