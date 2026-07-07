/*
 * MATLAB RA project migration: Wi-Fi MCS/action manager.
 *
 * Overview
 * --------
 * This file ports the MATLAB class `WifiMcsManager` into a self-contained
 * ns-3 scratch component.  The original MATLAB code built a 78-action rate
 * table from MCS, channel bandwidth, and spatial-stream combinations, then
 * used ns-3-derived SNR/PER reference samples to evaluate each action.  This
 * C++ version keeps the same modeling content while replacing MATLAB arrays
 * and interpolation calls with standard C++ containers and deterministic
 * helper methods that compile inside ns-3.
 *
 * Structure
 * ---------
 * - `WifiAction`: one row of the MATLAB ActionTable.
 * - `WifiMcsManager`: owns the full action table, PHY rates, NDBPS values,
 *   SNR axis, and PER lookup table.
 * - `GetPerForAction`: applies the same effective-SNR penalty model used by
 *   MATLAB before looking up the PER curve.
 *
 * Role
 * ----
 * All RA algorithms and the channel environment depend on this class for the
 * physical action definition.  The table intentionally keeps the MATLAB
 * 802.11ac/Wi-Fi 5 action space: bandwidths 20/40/80/160 MHz, NSS 1/2, and
 * MCS 0-9 with 20 MHz MCS 9 removed, giving 78 actions.
 */

#ifndef SCRATCH_MY_PROJECT_WIFI_MCS_MANAGER_H // 防止头文件被重复包含。
#define SCRATCH_MY_PROJECT_WIFI_MCS_MANAGER_H // 定义头文件保护宏。

#include "ns3/core-module.h" // 引入 ns-3 核心模块，使用日志、断言和基础类型。

#include <array> // 使用固定长度数组保存 MCS 基础参数。
#include <cstdint> // 使用明确位宽的整数类型。
#include <vector> // 使用 vector 保存动作表和 PER 表。

namespace myproject // 使用独立命名空间，避免污染 ns-3 全局命名空间。
{

struct WifiAction // 定义一个动作表条目，对应 MATLAB ActionTable 的一行。
{
    uint8_t mcs; // 保存 MCS 索引，范围为 0 到 9。
    uint16_t bandwidthMHz; // 保存信道带宽，单位 MHz。
    uint8_t nss; // 保存空间流数量，当前迁移原始代码的 1 或 2。
    double phyRateMbps; // 保存该动作对应的物理层速率，单位 Mbps。
};

class WifiMcsManager // 定义 MCS 管理器类，对应 MATLAB 的 WifiMcsManager。
{
  public:
    WifiMcsManager(); // 构造函数会生成动作表、NDBPS 表和 PER 表。

    uint32_t GetNumActions() const; // 返回动作数量，迁移结果应为 78。
    const WifiAction& GetAction(uint32_t actionIndex) const; // 按 1 基动作索引读取动作详情。
    double GetPhyRateMbps(uint32_t actionIndex) const; // 返回动作的 PHY 速率。
    double GetNdbps(uint32_t actionIndex) const; // 返回动作的每 OFDM 符号数据比特数。
    double GetPerForAction(double baseSnrDb, uint32_t actionIndex) const; // 按 MATLAB 有效 SNR 模型返回 PER。
    double GetBasePer(double snrDb, uint8_t mcs) const; // 查询某个 MCS 的基准 PER。
    double GetSnrAxisMin() const; // 返回 SNR 坐标轴下界。
    double GetSnrAxisMax() const; // 返回 SNR 坐标轴上界。
    double GetSnrAxisStep() const; // 返回 SNR 坐标轴步长。

  private:
    void BuildActionTable(); // 从 MCS、带宽、NSS 组合生成 78 种动作。
    void BuildPerTable(); // 使用 MATLAB 中的 ns-3 PER 样本生成查找表。
    double InterpolateLinear(const std::vector<double>& xs, const std::vector<double>& ys, double x) const; // 线性插值辅助函数。
    uint32_t SnrToIndex(double snrDb) const; // 将连续 SNR 映射为 PER 表行索引。

    std::vector<WifiAction> m_actions; // 保存排序后的动作表。
    std::vector<double> m_ndbps; // 保存每个动作的 NDBPS。
    std::vector<double> m_snrAxis; // 保存 -10:0.01:50 的 SNR 坐标轴。
    std::vector<std::array<double, 10>> m_basePerTable; // 保存每个 SNR 点下 MCS0-9 的 PER。
    const double m_symbolTimeUs = 4.0; // 保留 MATLAB long GI 的 4 微秒 OFDM 符号时间。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_WIFI_MCS_MANAGER_H
