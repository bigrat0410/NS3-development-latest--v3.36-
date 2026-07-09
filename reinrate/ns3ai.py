import argparse
import sys
import traceback
from ctypes import *
from pathlib import Path
import reinrate_py
from ns3ai_utils import Experiment
from model import DQNAgent, ReinforceAgent
import numpy as np
import time
import os
from tqdm import tqdm

class ReinrateEnv(Structure):
    _pack_ = 1
    _fields_ = [
        ('mcs', c_ubyte),
        ('max_mcs', c_ubyte),
        ('cw', c_ushort),
        ('throughput', c_double),
        ('snr', c_double)
    ]


class ReinrateAct(Structure):
    _pack_ = 1
    _fields_ = [
        ('nss', c_ubyte),
        ('next_mcs', c_ubyte)
    ]


class ReinrateContainer:
    use_ns3ai = True

    def __init__(self, uid: int = 2335,model_name='',retrain=False) -> None:
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
        next_mcs = self.agent.choose_action(rl_input)
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
    def train_reinforce(self, env: ReinrateEnv, act: ReinrateAct) -> ReinrateAct:
        if self.isInference:
            return self.inference_reinforce(env, act)

        throughput = env.throughput if np.isfinite(env.throughput) else 0.0
        snr = env.snr if np.isfinite(env.snr) and env.snr > -1 else 0.0
        scaled_snr = np.log(snr+1).astype(np.float32)
        if self.isFirstEpisode == True :
            self.last_state['mcs'] = env.mcs
            rl_input = [scaled_snr]
            next_mcs = self.agent.choose_action(rl_input)
            self.last_action['next_mcs'] = next_mcs
            act.next_mcs = next_mcs
            self.isFirstEpisode = False
            return act

        reward = self.calculate_reward(throughput, self.last_state['mcs'], 20)
        if not np.isfinite(reward):
            reward = 0.0
        self.agent.rewards.append(reward)
        self.agent.update()
        print(f'[Training] mcs: {self.last_state["mcs"]} -> {self.last_action["next_mcs"]}\t, cur_tpt: {throughput:.2f} Mbps, reward: {reward:.2f}, Snr: {scaled_snr:.2f}')
        rl_input = [scaled_snr]
        next_mcs = self.agent.choose_action(rl_input)
        
        act.next_mcs = next_mcs

        self.last_state['mcs'] = env.mcs
        self.last_state['throughput'] = throughput
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim-runs", type=int, default=20)
    parser.add_argument("--epochs", type=int, default=1000)
    parser.add_argument("--steps", type=int, default=80)
    parser.add_argument("--model", default="model.pt")
    parser.add_argument("--retrain", action="store_true")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    iters_count = 0

    for i in range(args.sim_runs):
        rand_seed = np.random.randint(0, 100000)
        ns3Settings = {
            "apManager": "ns3::rl-rateWifiManager",
            "staManager": "ns3::rl-rateWifiManager",
            "Run": i + 1,
            "Seed": rand_seed,
            "steps": args.steps,
        }
        exp = Experiment("rate-control", str(script_dir / "../.."), reinrate_py, handleFinish=True)
        c = ReinrateContainer(model_name=str(script_dir / args.model), retrain=args.retrain)
        msgInterface = exp.run(setting=ns3Settings, show_output=True)
        print("run rate-control", ns3Settings)
        try:
            with tqdm(total=args.epochs) as pbar:
                while True:
                    msgInterface.PyRecvBegin()
                    if msgInterface.PyGetFinished():
                        break
                    env = msgInterface.GetCpp2PyStruct()
                    msgInterface.PyRecvEnd()

                    msgInterface.PySendBegin()
                    act = msgInterface.GetPy2CppStruct()
                    act.nss = 1
                    if iters_count <= args.epochs:
                        c.train_reinforce(env, act)
                        pbar.update(1)
                    else:
                        act.next_mcs = c.last_action['next_mcs']
                    msgInterface.PySendEnd()

                    iters_count += 1
                c.save_model(str(script_dir / args.model))
                print('model saved')
        except Exception as e:
            print("Exception occurred: {}".format(e))
            traceback.print_tb(sys.exc_info()[2])
            raise
        finally:
            del exp
