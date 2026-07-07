/*
 * MATLAB RA project migration: implementation of the Wi-Fi MCS/action manager.
 *
 * This source file keeps the MATLAB numerical model intact where it is part of
 * the research logic: MCS modulation/coding parameters, the 78-action
 * construction rule, the bandwidth/NSS effective-SNR penalties, and the
 * ns-3-derived SNR/PER reference samples.  MATLAB `interp1(..., 'pchip')` is
 * replaced by bounded linear interpolation because it is dependency-free,
 * monotone-safe for probabilities, and native to this C++ ns-3 scratch port.
 */

#include "wifi-mcs-manager.h" // 引入本类声明。

#include <algorithm> // 使用 sort、clamp 和 lower_bound。
#include <cmath> // 使用 log10 和 round。
#include <limits> // 使用数值边界。

namespace myproject // 实现 myproject 命名空间中的类。
{

WifiMcsManager::WifiMcsManager() // 构造函数负责完整初始化。
{
    BuildActionTable(); // 先构建动作表，因为后续环境和算法都依赖它。
    BuildPerTable(); // 再构建 SNR/PER 查找表。
}

uint32_t WifiMcsManager::GetNumActions() const // 返回动作数量。
{
    return static_cast<uint32_t>(m_actions.size()); // 将 vector 长度转换为 ns-3 常用的 uint32_t。
}

const WifiAction& WifiMcsManager::GetAction(uint32_t actionIndex) const // 根据 1 基索引读取动作。
{
    NS_ABORT_MSG_IF(actionIndex == 0 || actionIndex > m_actions.size(), "Action index must be in [1, NumActions]"); // 防止 MATLAB 1 基索引迁移时越界。
    return m_actions[actionIndex - 1]; // MATLAB 动作索引从 1 开始，C++ vector 从 0 开始。
}

double WifiMcsManager::GetPhyRateMbps(uint32_t actionIndex) const // 返回 PHY 速率。
{
    return GetAction(actionIndex).phyRateMbps; // 速率保存在动作表中。
}

double WifiMcsManager::GetNdbps(uint32_t actionIndex) const // 返回每符号数据比特数。
{
    NS_ABORT_MSG_IF(actionIndex == 0 || actionIndex > m_ndbps.size(), "Action index must be in [1, NumActions]"); // 防止访问无效 NDBPS。
    return m_ndbps[actionIndex - 1]; // 使用同样的 1 基到 0 基映射。
}

double WifiMcsManager::GetPerForAction(double baseSnrDb, uint32_t actionIndex) const // 实现 MATLAB getPerForAction。
{
    const WifiAction& action = GetAction(actionIndex); // 取出动作物理配置。
    const double penaltyBw = 10.0 * std::log10(static_cast<double>(action.bandwidthMHz) / 20.0); // 带宽变宽带来的热噪声惩罚。
    const double penaltyNss = static_cast<double>(action.nss - 1) * 3.0; // 第二空间流带来的 3 dB 功率分摊惩罚。
    const double effectiveSnr = baseSnrDb - penaltyBw - penaltyNss; // 得到与 MATLAB 一致的有效 SNR。
    return GetBasePer(effectiveSnr, action.mcs); // 在基准 PER 表中查询该 MCS 的 PER。
}

double WifiMcsManager::GetBasePer(double snrDb, uint8_t mcs) const // 查询基准 PER。
{
    NS_ABORT_MSG_IF(mcs > 9, "MCS must be in [0, 9]"); // MATLAB 基准曲线只有 MCS 0-9。
    const uint32_t row = SnrToIndex(snrDb); // 将 SNR 映射到离散坐标轴。
    return m_basePerTable[row][mcs]; // 读取对应 MCS 列的 PER。
}

double WifiMcsManager::GetSnrAxisMin() const // 返回 SNR 下界。
{
    return -10.0; // MATLAB 使用 -10 dB 作为扩展下界。
}

double WifiMcsManager::GetSnrAxisMax() const // 返回 SNR 上界。
{
    return 50.0; // MATLAB 使用 50 dB 作为扩展上界。
}

double WifiMcsManager::GetSnrAxisStep() const // 返回 SNR 步长。
{
    return 0.01; // MATLAB 使用 0.01 dB 的查表分辨率。
}

void WifiMcsManager::BuildActionTable() // 构建 78 个动作。
{
    const std::array<std::array<double, 2>, 10> baseMcsParams = {{{1.0, 1.0 / 2.0}, {2.0, 1.0 / 2.0}, {2.0, 3.0 / 4.0}, {4.0, 1.0 / 2.0}, {4.0, 3.0 / 4.0}, {6.0, 2.0 / 3.0}, {6.0, 3.0 / 4.0}, {6.0, 5.0 / 6.0}, {8.0, 3.0 / 4.0}, {8.0, 5.0 / 6.0}}}; // 对应 MATLAB BaseMcsParams。
    const std::array<uint16_t, 4> bandwidths = {{20, 40, 80, 160}}; // 对应 MATLAB bw_list。
    for (uint16_t bandwidth : bandwidths) // 遍历每一种信道带宽。
    {
        uint16_t dataSubcarriers = 52; // 默认使用 20 MHz 的数据子载波数。
        if (bandwidth == 40) // 判断 40 MHz 带宽。
        {
            dataSubcarriers = 108; // 设置 40 MHz 的数据子载波数。
        }
        else if (bandwidth == 80) // 判断 80 MHz 带宽。
        {
            dataSubcarriers = 234; // 设置 80 MHz 的数据子载波数。
        }
        else if (bandwidth == 160) // 判断 160 MHz 带宽。
        {
            dataSubcarriers = 468; // 设置 160 MHz 的数据子载波数。
        }
        for (uint8_t nss = 1; nss <= 2; ++nss) // 遍历 1 到 2 条空间流。
        {
            for (uint8_t mcs = 0; mcs <= 9; ++mcs) // 遍历 MCS 0 到 MCS 9。
            {
                if (bandwidth == 20 && mcs == 9) // MATLAB 过滤 20 MHz 下不可用的 MCS 9。
                {
                    continue; // 跳过该组合。
                }
                const double bps = baseMcsParams[mcs][0]; // 读取每子载波调制比特数。
                const double codeRate = baseMcsParams[mcs][1]; // 读取编码码率。
                const double bitsPerSymbol = static_cast<double>(dataSubcarriers) * static_cast<double>(nss) * bps * codeRate; // 计算 OFDM 符号有效数据比特。
                const double rateMbps = bitsPerSymbol / m_symbolTimeUs; // 因为 Mbps * us 等于 bit，所以除以 4 us 得到 Mbps。
                m_actions.push_back(WifiAction{mcs, bandwidth, nss, rateMbps}); // 保存一个动作。
            }
        }
    }
    std::sort(m_actions.begin(), m_actions.end(), [](const WifiAction& a, const WifiAction& b) { // 按 MATLAB sortrows(raw_actions,[4,1,3]) 排序。
        if (a.phyRateMbps != b.phyRateMbps) // 先按物理速率升序。
        {
            return a.phyRateMbps < b.phyRateMbps; // 速率小的排前。
        }
        if (a.mcs != b.mcs) // 速率相同时按 MCS 升序。
        {
            return a.mcs < b.mcs; // MCS 小的排前。
        }
        return a.nss < b.nss; // 最后按空间流数升序。
    });
    for (const WifiAction& action : m_actions) // 遍历排序后的动作表。
    {
        m_ndbps.push_back(action.phyRateMbps * m_symbolTimeUs); // 计算并保存 NDBPS。
    }
}

void WifiMcsManager::BuildPerTable() // 构建 PER 查找表。
{
    const uint32_t rows = static_cast<uint32_t>(std::round((GetSnrAxisMax() - GetSnrAxisMin()) / GetSnrAxisStep())) + 1; // 计算 -10:0.01:50 的行数。
    m_snrAxis.reserve(rows); // 预留 SNR 坐标轴空间。
    m_basePerTable.reserve(rows); // 预留 PER 表空间。
    std::array<std::vector<double>, 10> ns3Snr; // 保存 MATLAB 中的 ns3_snr cell。
    std::array<std::vector<double>, 10> ns3Per; // 保存 MATLAB 中的 ns3_per cell。
    ns3Snr[0] = {-1.0, -0.5, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5}; // MCS0 的 SNR 样本。
    ns3Per[0] = {1.0, 0.994, 0.8185, 0.2908, 0.0663, 0.0112, 0.0015, 0.00015, 1e-5, 0.0}; // MCS0 的 PER 样本。
    ns3Snr[1] = {2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0}; // MCS1 的 SNR 样本。
    ns3Per[1] = {1.0, 0.997, 0.7944, 0.3008, 0.0728, 0.0120, 0.0015, 0.00023, 0.0}; // MCS1 的 PER 样本。
    ns3Snr[2] = {4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0}; // MCS2 的 SNR 样本。
    ns3Per[2] = {1.0, 0.998, 0.7578, 0.3010, 0.0676, 0.0122, 0.0023, 0.00035, 4e-5, 0.0}; // MCS2 的 PER 样本。
    ns3Snr[3] = {7.5, 8.0, 8.5, 9.0, 9.5, 10.0, 10.5, 11.0, 11.5, 12.0, 12.5}; // MCS3 的 SNR 样本。
    ns3Per[3] = {1.0, 0.994, 0.8405, 0.4341, 0.1419, 0.0374, 0.0086, 0.0019, 0.00036, 5e-5, 0.0}; // MCS3 的 PER 样本。
    ns3Snr[4] = {11.0, 11.5, 12.0, 12.5, 13.0, 13.5, 14.0, 14.5, 15.0, 15.5}; // MCS4 的 SNR 样本。
    ns3Per[4] = {1.0, 0.9269, 0.5139, 0.1826, 0.0465, 0.0110, 0.0026, 0.00041, 1e-4, 0.0}; // MCS4 的 PER 样本。
    ns3Snr[5] = {14.5, 15.0, 15.5, 16.0, 16.5, 17.0, 17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5}; // MCS5 的 SNR 样本。
    ns3Per[5] = {1.0, 0.999, 0.9479, 0.6625, 0.2978, 0.1058, 0.0334, 0.0091, 0.0023, 0.00064, 1.7e-4, 2e-5, 0.0}; // MCS5 的 PER 样本。
    ns3Snr[6] = {16.0, 16.5, 17.0, 17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 21.5}; // MCS6 的 SNR 样本。
    ns3Per[6] = {1.0, 0.995, 0.8796, 0.5139, 0.2091, 0.0639, 0.0186, 0.0046, 0.0013, 0.00023, 2e-5, 0.0}; // MCS6 的 PER 样本。
    ns3Snr[7] = {17.5, 18.0, 18.5, 19.0, 19.5, 20.0, 20.5, 21.0, 21.5, 22.0, 22.5, 23.0}; // MCS7 的 SNR 样本。
    ns3Per[7] = {1.0, 0.9785, 0.7393, 0.3375, 0.1234, 0.0355, 0.0100, 0.0027, 0.0005, 9e-5, 1e-5, 0.0}; // MCS7 的 PER 样本。
    ns3Snr[8] = {21.0, 21.5, 22.0, 22.5, 23.0, 23.5, 24.0, 24.5, 25.0, 25.5, 26.0, 26.5, 27.0}; // MCS8 的 SNR 样本。
    ns3Per[8] = {1.0, 0.998, 0.9399, 0.6709, 0.3525, 0.1376, 0.0475, 0.0154, 0.0052, 0.0015, 0.00036, 7e-5, 0.0}; // MCS8 的 PER 样本。
    ns3Snr[9] = {22.5, 23.0, 23.5, 24.0, 24.5, 25.0, 25.5, 26.0, 26.5, 27.0, 27.5, 28.0, 28.5, 29.0}; // MCS9 的 SNR 样本。
    ns3Per[9] = {1.0, 0.999, 0.9506, 0.6847, 0.3261, 0.1248, 0.0409, 0.0130, 0.0036, 0.00082, 1e-4, 2e-5, 1e-5, 0.0}; // MCS9 的 PER 样本。
    for (uint32_t row = 0; row < rows; ++row) // 遍历每个 SNR 查表点。
    {
        const double snr = GetSnrAxisMin() + static_cast<double>(row) * GetSnrAxisStep(); // 计算当前 SNR。
        m_snrAxis.push_back(snr); // 保存当前 SNR 坐标。
        std::array<double, 10> perRow{}; // 创建当前行的 10 个 MCS PER。
        for (uint8_t mcs = 0; mcs <= 9; ++mcs) // 遍历 MCS0-9。
        {
            double per = InterpolateLinear(ns3Snr[mcs], ns3Per[mcs], snr); // 对样本做线性插值。
            if (snr < ns3Snr[mcs].front()) // 低于样本最低 SNR。
            {
                per = 1.0; // MATLAB 将左边界外 PER 截断为 1。
            }
            if (snr > ns3Snr[mcs].back()) // 高于样本最高 SNR。
            {
                per = 0.0; // MATLAB 将右边界外 PER 截断为 0。
            }
            perRow[mcs] = std::clamp(per, 0.0, 1.0); // 将概率限制在 [0,1]。
        }
        m_basePerTable.push_back(perRow); // 将当前行写入 PER 表。
    }
}

double WifiMcsManager::InterpolateLinear(const std::vector<double>& xs, const std::vector<double>& ys, double x) const // 线性插值实现。
{
    if (x <= xs.front()) // 处理左边界。
    {
        return ys.front(); // 返回最左样本值。
    }
    if (x >= xs.back()) // 处理右边界。
    {
        return ys.back(); // 返回最右样本值。
    }
    auto upper = std::upper_bound(xs.begin(), xs.end(), x); // 找到第一个大于 x 的样本点。
    const uint32_t hi = static_cast<uint32_t>(std::distance(xs.begin(), upper)); // 得到右端索引。
    const uint32_t lo = hi - 1; // 得到左端索引。
    const double ratio = (x - xs[lo]) / (xs[hi] - xs[lo]); // 计算插值比例。
    return ys[lo] + ratio * (ys[hi] - ys[lo]); // 返回线性插值结果。
}

uint32_t WifiMcsManager::SnrToIndex(double snrDb) const // 将 SNR 映射为查表索引。
{
    const double boundedSnr = std::clamp(snrDb, GetSnrAxisMin(), GetSnrAxisMax()); // 先进行边界保护。
    const double rawIndex = std::round((boundedSnr - GetSnrAxisMin()) / GetSnrAxisStep()); // 按 MATLAB round 方式映射。
    return static_cast<uint32_t>(rawIndex); // 返回 0 基行索引。
}

} // namespace myproject
