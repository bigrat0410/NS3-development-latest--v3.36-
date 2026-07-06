/*
 * MATLAB RA project migration: Oracle throughput implementation.
 *
 * The formulas are direct translations of `IdealThroughputManager.calculate`:
 * each action is evaluated over 64 subframe PER values, full-packet failure is
 * the product of all subframe PERs, useful bits are proportional to
 * `1 - mean(PER)`, and expected time includes fixed overhead, average
 * exponential backoff, PHY data time, and retry reach probability.
 */

#include "ideal-throughput-manager.h" // 引入 Oracle 管理器声明。

#include <algorithm> // 使用 max。
#include <cmath> // 使用 ceil。
#include <limits> // 使用最小正数保护除法。

namespace myproject // 实现项目命名空间。
{

IdealThroughputManager::IdealThroughputManager(const WifiMcsManager& mcsManager, const WifiEnvironment& environment) // 构造函数。
    : m_mcsManager(mcsManager), // 保存 MCS 管理器引用。
      m_fixedOverheadUs(environment.GetFixedOverheadUs()), // 从环境读取固定开销。
      m_slotUs(environment.GetSlotTimeUs()), // 从环境读取 slot 时间。
      m_payloadBits(environment.GetPayloadBits()) // 从环境读取 AMPDU 载荷。
{
}

std::pair<double, uint32_t> IdealThroughputManager::Calculate(const std::vector<double>& baseSnrsDb) const // 计算 Oracle 最优动作。
{
    double bestThroughputMbps = -1.0; // 初始化最优吞吐量。
    uint32_t bestAction = 1; // 初始化最优动作为 1。
    for (uint32_t actionIndex = 1; actionIndex <= m_mcsManager.GetNumActions(); ++actionIndex) // 遍历所有 78 个动作。
    {
        double perSum = 0.0; // 初始化 PER 求和。
        double pFailPacket = 1.0; // 初始化全包失败概率为各子帧 PER 乘积。
        for (double snrDb : baseSnrsDb) // 遍历 64 个子帧 SNR。
        {
            const double per = m_mcsManager.GetPerForAction(snrDb, actionIndex); // 查询当前动作和 SNR 的 PER。
            perSum += per; // 累加 PER。
            pFailPacket *= per; // 乘入全包失败概率。
        }
        const double meanPer = baseSnrsDb.empty() ? 1.0 : perSum / static_cast<double>(baseSnrsDb.size()); // 计算平均 PER。
        const double expectedBitsOneShot = m_payloadBits * (1.0 - meanPer); // 计算单次尝试期望成功比特。
        const double ndbps = m_mcsManager.GetNdbps(actionIndex); // 读取 NDBPS。
        const double dataTimeUs = std::ceil(m_payloadBits / ndbps) * 4.0; // 计算 PHY 数据传输时间。
        double expectedTimeTotalUs = 0.0; // 初始化期望耗时。
        double expectedBitsTotal = 0.0; // 初始化期望比特。
        double pReachAttempt = 1.0; // 初始化到达第 k 次尝试的概率。
        for (uint32_t retry = 0; retry <= 6; ++retry) // 遍历初传加 6 次重传。
        {
            const uint32_t cw = std::min<uint32_t>(1023, 15u * (1u << retry)); // 计算当前重传轮次 CW。
            const double averageBackoffUs = (static_cast<double>(cw) / 2.0) * m_slotUs; // 计算平均退避时间。
            const double attemptTimeUs = m_fixedOverheadUs + averageBackoffUs + dataTimeUs; // 计算该次尝试耗时。
            expectedTimeTotalUs += pReachAttempt * attemptTimeUs; // 按到达概率累加期望时间。
            expectedBitsTotal += pReachAttempt * (1.0 - pFailPacket) * expectedBitsOneShot; // 按到达概率和非全灭概率累加期望比特。
            pReachAttempt *= pFailPacket; // 下一轮只有全包失败才会到达。
            if (pReachAttempt < 1e-8) // 若后续概率已极小。
            {
                break; // 停止无意义循环。
            }
        }
        const double throughputMbps = expectedBitsTotal / std::max(expectedTimeTotalUs, std::numeric_limits<double>::min()); // bits/us 数值等于 Mbps。
        if (throughputMbps > bestThroughputMbps) // 判断是否优于当前最优。
        {
            bestThroughputMbps = throughputMbps; // 更新最优吞吐。
            bestAction = actionIndex; // 更新最优动作。
        }
    }
    return std::make_pair(bestThroughputMbps, bestAction); // 返回 Oracle 结果。
}

} // namespace myproject
