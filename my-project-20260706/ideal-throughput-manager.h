/*
 * MATLAB RA project migration: Oracle throughput manager.
 *
 * Overview
 * --------
 * This file ports MATLAB `IdealThroughputManager`.  The manager receives a
 * vector of 64 subframe SNR samples and evaluates every action from
 * `WifiMcsManager`.  It computes the expected delivered bits and expected MAC
 * time under the same AMPDU, PER, and retry/backoff assumptions used in the
 * MATLAB Oracle branch.
 *
 * Structure
 * ---------
 * - Constructor stores references to the MCS table and environment timing.
 * - `Calculate` returns `(ideal throughput Mbps, best action index)`.
 *
 * Role
 * ----
 * This is the benchmark path used by Oracle simulation and by Minstrel/MAB/DQN
 * training loops to measure action error or reward efficiency.
 */

#ifndef SCRATCH_MY_PROJECT_IDEAL_THROUGHPUT_MANAGER_H // 防止头文件重复包含。
#define SCRATCH_MY_PROJECT_IDEAL_THROUGHPUT_MANAGER_H // 定义头文件保护宏。

#include "wifi-environment.h" // 引入环境与 MCS 管理器类型。

#include <utility> // 使用 std::pair 作为返回值。
#include <vector> // 使用 vector 保存 SNR 序列。

namespace myproject // 使用项目命名空间。
{

class IdealThroughputManager // 定义 Oracle 理想吞吐管理器。
{
  public:
    IdealThroughputManager(const WifiMcsManager& mcsManager, const WifiEnvironment& environment); // 构造函数绑定 MCS 表和环境参数。
    std::pair<double, uint32_t> Calculate(const std::vector<double>& baseSnrsDb) const; // 计算最优吞吐和最优动作。

  private:
    const WifiMcsManager& m_mcsManager; // 保存 MCS 管理器引用。
    double m_fixedOverheadUs; // 保存固定开销。
    double m_slotUs; // 保存 slot 时间。
    double m_payloadBits; // 保存 AMPDU 总载荷。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_IDEAL_THROUGHPUT_MANAGER_H
