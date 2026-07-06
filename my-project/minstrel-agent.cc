/*
 * MATLAB RA project migration: Minstrel-style baseline implementation.
 */

#include "minstrel-agent.h" // 引入类声明。

#include <algorithm> // 使用 max_element。

namespace myproject // 实现项目命名空间。
{

MinstrelAgent::MinstrelAgent(const WifiMcsManager& mcsManager) // 构造 Minstrel 智能体。
    : m_mcsManager(mcsManager), // 保存 MCS 管理器引用。
      m_numActions(mcsManager.GetNumActions()), // 读取动作数量。
      m_successProb(mcsManager.GetNumActions(), 0.5) // 将成功率初始化为 MATLAB 的 0.5。
{
    m_uniformRv = ns3::CreateObject<ns3::UniformRandomVariable>(); // 创建均匀随机变量。
    m_uniformRv->SetAttribute("Min", ns3::DoubleValue(0.0)); // 设置下界。
    m_uniformRv->SetAttribute("Max", ns3::DoubleValue(1.0)); // 设置上界。
}

uint32_t MinstrelAgent::SelectAction() // 选择动作。
{
    ++m_packetCount; // 包计数加一。
    if (m_packetCount % m_probeInterval == 0) // 判断是否到达探测周期。
    {
        return m_uniformRv->GetInteger(1, m_numActions); // 探测阶段随机选择动作。
    }
    double bestExpectedThroughput = -1.0; // 初始化最佳期望吞吐。
    uint32_t bestAction = 1; // 初始化最佳动作。
    for (uint32_t actionIndex = 1; actionIndex <= m_numActions; ++actionIndex) // 遍历所有动作。
    {
        const double expectedThroughput = m_mcsManager.GetPhyRateMbps(actionIndex) * m_successProb[actionIndex - 1]; // 计算 PHY 速率乘成功率。
        if (expectedThroughput > bestExpectedThroughput) // 判断是否更优。
        {
            bestExpectedThroughput = expectedThroughput; // 更新最佳期望吞吐。
            bestAction = actionIndex; // 更新最佳动作。
        }
    }
    return bestAction; // 返回最优动作。
}

void MinstrelAgent::UpdateStat(uint32_t actionIndex, bool isSuccess) // 更新动作统计。
{
    NS_ABORT_MSG_IF(actionIndex == 0 || actionIndex > m_numActions, "Action index must be in [1, NumActions]"); // 防止越界。
    const double oldProb = m_successProb[actionIndex - 1]; // 读取旧成功率。
    const double currentResult = isSuccess ? 1.0 : 0.0; // 将 ACK 结果转为 0/1。
    const double newProb = (1.0 - m_ewmaAlpha) * oldProb + m_ewmaAlpha * currentResult; // 按 MATLAB EWMA 更新。
    m_successProb[actionIndex - 1] = newProb; // 写回新成功率。
}

} // namespace myproject
