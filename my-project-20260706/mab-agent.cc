/*
 * MATLAB RA project migration: MAB implementation.
 */

#include "mab-agent.h" // 引入类声明。

#include <algorithm> // 使用 max。
#include <numeric> // 使用 accumulate。

namespace myproject // 实现项目命名空间。
{

MabAgent::MabAgent(uint32_t numActions, uint32_t windowSize, double epsilon) // 构造 MAB 智能体。
    : m_numActions(numActions), // 保存动作数量。
      m_windowSize(windowSize), // 保存滑动窗口长度。
      m_epsilon(epsilon) // 保存探索概率。
{
    for (double edge = -5.0; edge <= 35.0; edge += 5.0) // 构建 MATLAB 的 -5:5:35 边界。
    {
        m_snrEdges.push_back(edge); // 保存一个边界。
    }
    m_numBins = static_cast<uint32_t>(m_snrEdges.size()) + 1; // MATLAB NumBins=length(edges)+1。
    m_uniformRv = ns3::CreateObject<ns3::UniformRandomVariable>(); // 创建均匀随机变量。
    m_uniformRv->SetAttribute("Min", ns3::DoubleValue(0.0)); // 设置下界。
    m_uniformRv->SetAttribute("Max", ns3::DoubleValue(1.0)); // 设置上界。
    Reset(); // 初始化奖励缓冲区。
}

void MabAgent::Reset() // 重置 MAB 缓冲区。
{
    m_rewardBuffers.assign(m_numBins, std::vector<std::deque<double>>(m_numActions)); // 创建桶数乘动作数的空 deque 表。
}

uint32_t MabAgent::SelectAction(double snrDb) // 选择动作。
{
    const uint32_t bin = GetStateBin(snrDb); // 计算当前 SNR 桶。
    std::vector<uint32_t> untested; // 保存当前桶中尚未尝试的动作。
    for (uint32_t actionIndex = 1; actionIndex <= m_numActions; ++actionIndex) // 遍历所有动作。
    {
        if (m_rewardBuffers[bin][actionIndex - 1].empty()) // 如果该动作没有样本。
        {
            untested.push_back(actionIndex); // 记录为未测试动作。
        }
    }
    if (!untested.empty()) // 如果存在未测试动作。
    {
        const uint32_t pick = m_uniformRv->GetInteger(0, static_cast<uint32_t>(untested.size() - 1)); // 随机选择一个未测试动作。
        return untested[pick]; // 返回该动作。
    }
    if (m_uniformRv->GetValue() < m_epsilon) // 按 epsilon 判断是否探索。
    {
        return m_uniformRv->GetInteger(1, m_numActions); // 探索阶段随机选择动作。
    }
    double bestReward = -1.0; // 初始化最佳平均奖励。
    uint32_t bestAction = 1; // 初始化最佳动作。
    for (uint32_t actionIndex = 1; actionIndex <= m_numActions; ++actionIndex) // 遍历所有动作。
    {
        const double reward = MeanReward(bin, actionIndex); // 计算当前动作平均奖励。
        if (reward > bestReward) // 判断是否更优。
        {
            bestReward = reward; // 更新最佳奖励。
            bestAction = actionIndex; // 更新最佳动作。
        }
    }
    return bestAction; // 返回最佳动作。
}

void MabAgent::Update(double snrDb, uint32_t actionIndex, double reward) // 更新奖励窗口。
{
    NS_ABORT_MSG_IF(actionIndex == 0 || actionIndex > m_numActions, "Action index must be in [1, NumActions]"); // 防止动作越界。
    const uint32_t bin = GetStateBin(snrDb); // 计算 SNR 桶。
    std::deque<double>& buffer = m_rewardBuffers[bin][actionIndex - 1]; // 获取对应奖励缓冲区。
    buffer.push_back(reward); // 压入新奖励。
    if (buffer.size() > m_windowSize) // 如果超过滑动窗口长度。
    {
        buffer.pop_front(); // 删除最旧样本。
    }
}

uint32_t MabAgent::GetStateBin(double snrDb) const // 映射 SNR 桶。
{
    for (uint32_t i = 0; i < m_snrEdges.size(); ++i) // 遍历分桶边界。
    {
        if (snrDb <= m_snrEdges[i]) // MATLAB discretize 的边界近似处理。
        {
            return i; // 返回 0 基桶索引。
        }
    }
    return m_numBins - 1; // 高于最高边界时返回最后一桶。
}

double MabAgent::MeanReward(uint32_t bin, uint32_t actionIndex) const // 计算平均奖励。
{
    const std::deque<double>& buffer = m_rewardBuffers[bin][actionIndex - 1]; // 获取奖励窗口。
    if (buffer.empty()) // 如果没有样本。
    {
        return 0.0; // 返回 0。
    }
    const double sum = std::accumulate(buffer.begin(), buffer.end(), 0.0); // 计算奖励和。
    return sum / static_cast<double>(buffer.size()); // 返回平均值。
}

} // namespace myproject
