/*
 * MATLAB RA project migration: sliding-window epsilon-greedy MAB agent.
 *
 * Overview
 * --------
 * This file ports MATLAB `MAB_Agent`.  The algorithm discretizes SNR into
 * bins, stores a sliding window of recent throughput rewards for each
 * `(bin, action)` pair, ensures every action is tried at least once per bin,
 * and then follows epsilon-greedy selection over mean rewards.
 */

#ifndef SCRATCH_MY_PROJECT_MAB_AGENT_H // 防止头文件重复包含。
#define SCRATCH_MY_PROJECT_MAB_AGENT_H // 定义头文件保护宏。

#include "ns3/core-module.h" // 引入 ns-3 随机变量。

#include <cstdint> // 使用明确位宽整数。
#include <deque> // 使用 deque 实现滑动窗口。
#include <vector> // 使用 vector 构造二维表。

namespace myproject // 使用项目命名空间。
{

class MabAgent // 定义 MAB 智能体。
{
  public:
    MabAgent(uint32_t numActions, uint32_t windowSize, double epsilon); // 构造函数对应 MATLAB MAB_Agent(num_actions,30,0.1)。
    void Reset(); // 清空所有奖励缓冲区。
    uint32_t SelectAction(double snrDb); // 根据当前 SNR 选择动作。
    void Update(double snrDb, uint32_t actionIndex, double reward); // 更新滑动窗口奖励。

  private:
    uint32_t GetStateBin(double snrDb) const; // 将连续 SNR 映射到离散桶。
    double MeanReward(uint32_t bin, uint32_t actionIndex) const; // 计算某桶某动作的平均奖励。

    uint32_t m_numActions; // 保存动作数量。
    uint32_t m_windowSize; // 保存滑动窗口长度。
    double m_epsilon; // 保存 epsilon 探索概率。
    std::vector<double> m_snrEdges; // 保存 SNR 分桶边界。
    uint32_t m_numBins; // 保存桶数量。
    std::vector<std::vector<std::deque<double>>> m_rewardBuffers; // 保存每个桶每个动作的奖励窗口。
    ns3::Ptr<ns3::UniformRandomVariable> m_uniformRv; // 保存 ns-3 均匀随机变量。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_MAB_AGENT_H
