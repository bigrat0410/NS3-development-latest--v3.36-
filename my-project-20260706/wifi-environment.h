/*
 * MATLAB RA project migration: stochastic Wi-Fi environment.
 *
 * Overview
 * --------
 * This file ports MATLAB `WifiEnvironment` into C++ for ns-3 scratch usage.
 * The original MATLAB model was a packet-level RA environment rather than a
 * full ns-3 PHY simulation.  It used a Rayleigh fading channel, a
 * Gaussian-Markov large-scale SNR process, AMPDU-style 64-subframe delivery,
 * exponential MAC retransmission/backoff, and an SNR/PER table calibrated from
 * ns-3.  This migration preserves that logic but implements it with ns-3 random
 * variables and standard C++.
 *
 * Structure
 * ---------
 * - `EnvironmentState`: the two-dimensional state `[current_snr, delta_snr]`.
 * - `StepInfo`: detailed feedback equivalent to MATLAB `info`.
 * - `WifiEnvironment`: owns channel evolution, MAC timing, and the step loop.
 *
 * Native ns-3 adaptation
 * ----------------------
 * MATLAB used `comm.RayleighChannel`.  ns-3 does not expose that MATLAB object,
 * so this file implements equivalent flat Rayleigh power fading by sampling two
 * independent normal variables through ns-3 RNG streams and converting
 * `|h|^2` to dB.  MAC constants are retained from the MATLAB experiment, while
 * random draws use ns-3 random variables for reproducible ns-3 runs.
 */

#ifndef SCRATCH_MY_PROJECT_WIFI_ENVIRONMENT_H // 防止头文件重复包含。
#define SCRATCH_MY_PROJECT_WIFI_ENVIRONMENT_H // 定义头文件保护宏。

#include "wifi-mcs-manager.h" // 引入动作和 PER 管理器。

#include "ns3/core-module.h" // 引入 ns-3 随机变量和时间工具。

#include <cstdint> // 使用明确位宽整数。
#include <vector> // 使用 vector 保存子帧 SNR。

namespace myproject // 使用项目独立命名空间。
{

struct EnvironmentState // 定义环境返回给算法的状态。
{
    double currentSnrDb; // 保存当前瞬时 SNR，单位 dB。
    double deltaSnrDb; // 保存当前 SNR 与上一状态 SNR 的差值，单位 dB。
};

struct StepInfo // 定义一次发包后的详细反馈。
{
    double snrDb; // 保存最后一个子帧的瞬时 SNR。
    uint32_t retryCount; // 保存已经发生的重传次数。
    bool isSuccess; // 保存是否至少有一个子帧成功，从而收到 Block ACK。
    double timeCostUs; // 保存本次传输总耗时，单位微秒。
    double payloadBits; // 保存真实成功交付的数据位数。
    std::vector<double> subframeSnrs; // 保存 64 个子帧的瞬时 SNR。
    uint8_t mcs; // 保存动作对应的 MCS。
    uint16_t bandwidthMHz; // 保存动作对应的带宽。
    uint8_t nss; // 保存动作对应的空间流数。
    double phyRateMbps; // 保存动作对应的 PHY 速率。
};

class WifiEnvironment // 定义 Wi-Fi 速率自适应环境。
{
  public:
    WifiEnvironment(); // 构造函数初始化 MCS 管理器和随机变量。

    EnvironmentState Reset(); // 重置环境并返回初始状态。
    std::pair<EnvironmentState, StepInfo> Step(uint32_t actionIndex); // 执行动作并返回下一状态和信息。
    EnvironmentState GetState() const; // 读取当前环境状态。
    WifiMcsManager& GetMcsManager(); // 返回 MCS 管理器可变引用。
    const WifiMcsManager& GetMcsManager() const; // 返回 MCS 管理器只读引用。
    double GetPayloadBits() const; // 返回一次 AMPDU 的总载荷比特。
    double GetFixedOverheadUs() const; // 返回固定 MAC/PHY 开销。
    double GetSlotTimeUs() const; // 返回 Slot 时间。
    double GetCurrentLargeScaleSnrDb() const; // 返回当前大尺度 SNR。
    std::vector<double> PeekSubframeSnrs(uint32_t numSubframes) const; // 不改变环境状态地采样一组候选 SNR，用于 Oracle。

  private:
    void AdvanceTime(double durationUs); // 推进小尺度时钟。
    void UpdateLargeScale(double durationUs); // 更新大尺度 SNR。
    double SampleRayleighPowerDb() const; // 采样一次 Rayleigh 小尺度功率增益，单位 dB。
    uint32_t UniformInteger(uint32_t minValue, uint32_t maxValue) const; // 采样整数随机数。
    double Uniform01() const; // 采样 [0,1) 均匀随机数。

    WifiMcsManager m_mcsManager; // 保存动作和 PER 管理器。
    double m_payloadBits = 1458.0 * 8.0 * 64.0; // 保存 64 个 1458 字节子帧的总载荷。
    double m_sampleRateHz = 1000.0 * 1000.0; // 保存 MATLAB 的 1 MHz 信道采样率。
    double m_referenceTimeUs = 1000.0 * 100.0; // 保存大尺度衰落参考时间，单位微秒。
    double m_slotUs = 9.0; // 保存 802.11 slot time。
    double m_sifsUs = 16.0; // 保存 SIFS 时间。
    double m_difsUs = 34.0; // 保存 DIFS 时间。
    double m_phyHeaderUs = 40.0; // 保存 PHY header 时间。
    double m_ackUs = 32.0; // 保存 ACK 时间。
    double m_fixedOverheadUs = 0.0; // 保存 DIFS + PHY header + SIFS + ACK。
    double m_baseSnrDb = 15.0; // 保存 reset 时的初始大尺度 SNR。
    uint64_t m_timeStep = 0; // 保存离散信道时钟步数。
    double m_currentLargeScaleSnrDb = 15.0; // 保存当前大尺度 SNR。
    double m_targetMeanSnrDb = 15.0; // 保存高斯-马尔可夫过程中心值。
    double m_snrStdDevDb = 9.0; // 保存高斯-马尔可夫过程标准差。
    double m_currentSmallScaleDb = 0.0; // 保存当前小尺度衰落 dB。
    double m_lastSnrDb = 15.0; // 保存上一个状态的瞬时 SNR。
    ns3::Ptr<ns3::NormalRandomVariable> m_normalRv; // 保存 ns-3 正态随机变量，用于 Rayleigh 和大尺度噪声。
    ns3::Ptr<ns3::UniformRandomVariable> m_uniformRv; // 保存 ns-3 均匀随机变量，用于 ACK/PER 判定和退避。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_WIFI_ENVIRONMENT_H
