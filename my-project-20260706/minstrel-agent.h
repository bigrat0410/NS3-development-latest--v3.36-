/*
 * MATLAB RA project migration: simplified Minstrel-HT agent.
 *
 * Overview
 * --------
 * This file ports MATLAB `Lwlwifi_Minstrel_HT`.  It is intentionally a compact
 * Minstrel-style baseline: keep an EWMA success probability per action, probe
 * randomly every fixed interval, and otherwise choose the action with maximum
 * `phy_rate * success_probability`.
 */

#ifndef SCRATCH_MY_PROJECT_MINSTREL_AGENT_H // 防止重复包含。
#define SCRATCH_MY_PROJECT_MINSTREL_AGENT_H // 定义头文件保护宏。

#include "wifi-mcs-manager.h" // 引入 MCS 管理器。

#include "ns3/core-module.h" // 引入 ns-3 随机变量。

#include <vector> // 使用 vector 保存成功率。

namespace myproject // 使用项目命名空间。
{

class MinstrelAgent // 定义 Minstrel 风格智能体。
{
  public:
    explicit MinstrelAgent(const WifiMcsManager& mcsManager); // 构造函数读取动作数量和 PHY 速率。
    uint32_t SelectAction(); // 选择动作。
    void UpdateStat(uint32_t actionIndex, bool isSuccess); // 用 ACK 反馈更新成功率。

  private:
    const WifiMcsManager& m_mcsManager; // 保存 MCS 管理器引用。
    uint32_t m_numActions; // 保存动作数量。
    std::vector<double> m_successProb; // 保存每个动作的成功率估计。
    double m_ewmaAlpha = 0.125; // 保存 MATLAB 的 EWMA 因子。
    uint32_t m_probeInterval = 10; // 保存每 10 包探测一次。
    uint32_t m_packetCount = 0; // 保存包计数器。
    ns3::Ptr<ns3::UniformRandomVariable> m_uniformRv; // 保存 ns-3 均匀随机变量。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_MINSTREL_AGENT_H
