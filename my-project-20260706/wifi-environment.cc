/*
 * MATLAB RA project migration: implementation of the packet-level environment.
 *
 * The timing and reward-facing feedback follow the MATLAB code exactly where
 * possible.  The only intentional modeling replacement is MATLAB
 * `comm.RayleighChannel`, which is represented here by independent complex
 * Gaussian fading samples generated from ns-3 RNGs.  That replacement is
 * appropriate for this scratch migration because the MATLAB code used only the
 * instantaneous path gain magnitude in the step loop.
 */

#include "wifi-environment.h" // 引入环境类声明。

#include <algorithm> // 使用 max 和 min。
#include <cmath> // 使用 ceil、exp、sqrt 和 log10。
#include <limits> // 使用最小正数保护 log10。

namespace myproject // 实现项目命名空间。
{

WifiEnvironment::WifiEnvironment() // 构造环境对象。
{
    m_fixedOverheadUs = m_difsUs + m_phyHeaderUs + m_sifsUs + m_ackUs; // 计算 MATLAB FixedOverhead。
    m_normalRv = ns3::CreateObject<ns3::NormalRandomVariable>(); // 创建 ns-3 正态随机变量。
    m_normalRv->SetAttribute("Mean", ns3::DoubleValue(0.0)); // 设置均值为 0。
    m_normalRv->SetAttribute("Variance", ns3::DoubleValue(1.0)); // 设置方差为 1。
    m_uniformRv = ns3::CreateObject<ns3::UniformRandomVariable>(); // 创建 ns-3 均匀随机变量。
    m_uniformRv->SetAttribute("Min", ns3::DoubleValue(0.0)); // 设置均匀分布下界。
    m_uniformRv->SetAttribute("Max", ns3::DoubleValue(1.0)); // 设置均匀分布上界。
}

EnvironmentState WifiEnvironment::Reset() // 重置环境状态。
{
    m_timeStep = 0; // 清空内部信道时钟。
    m_baseSnrDb = 15.0; // 恢复 MATLAB 初始 SNR。
    m_currentLargeScaleSnrDb = m_baseSnrDb; // 将当前大尺度 SNR 置为初始值。
    m_currentSmallScaleDb = 0.0; // 将初始小尺度衰落置为 0 dB。
    m_lastSnrDb = m_currentLargeScaleSnrDb + m_currentSmallScaleDb; // 初始化上一状态 SNR。
    m_currentSmallScaleDb = SampleRayleighPowerDb(); // 预跑一次小尺度采样，对应 MATLAB reset 里的 Rayleigh 预跑。
    return GetState(); // 返回初始状态。
}

std::pair<EnvironmentState, StepInfo> WifiEnvironment::Step(uint32_t actionIndex) // 执行一次动作。
{
    m_lastSnrDb = m_currentLargeScaleSnrDb + m_currentSmallScaleDb; // 在每跳开始前保存上一瞬时 SNR。
    const uint32_t maxRetries = 6; // 保留 MATLAB 最大重传次数 6。
    bool isSuccess = false; // 初始化本次传输成功标志。
    double totalTimeUs = 0.0; // 初始化累计耗时。
    const WifiAction& action = m_mcsManager.GetAction(actionIndex); // 获取动作详情。
    const double ndbps = m_mcsManager.GetNdbps(actionIndex); // 获取每符号数据比特数。
    const double numSymbols = std::ceil(m_payloadBits / ndbps); // 计算发送整个 AMPDU 需要的 OFDM 符号数。
    const double symbolTimeUs = 4.0; // 保留 MATLAB long GI 4 微秒符号时间。
    const double dataTimeUs = numSymbols * symbolTimeUs; // 计算整个 AMPDU 的数据传输时长。
    uint32_t retryCount = 0; // 初始化重传次数。
    const uint32_t numSubframes = 64; // 保留 MATLAB 聚合 64 个子帧。
    const double subframeBits = 1458.0 * 8.0; // 保留每个子帧 1458 字节。
    double actualDeliveredBits = 0.0; // 初始化真实交付比特。
    const double subframeTimeUs = dataTimeUs / static_cast<double>(numSubframes); // 平均分配每个子帧的 PHY 时间。
    double lastInstantSnrDb = 0.0; // 保存最后子帧 SNR。
    std::vector<double> subframeSnrs(numSubframes, 0.0); // 保存最后一次尝试的 64 个子帧 SNR。
    while (retryCount <= maxRetries) // 执行最多 1 次初传加 6 次重传。
    {
        const uint32_t cw = std::min<uint32_t>(1023, 15u * (1u << retryCount)); // 使用 MATLAB cw=min(1023,15*2^retry)。
        const double initialBackoffUs = static_cast<double>(UniformInteger(0, cw)) * m_slotUs; // 随机退避 randi([0,cw])*slot。
        const double waitTimeUs = m_fixedOverheadUs + initialBackoffUs; // 累加固定开销和退避。
        totalTimeUs += waitTimeUs; // 将等待时间计入总耗时。
        AdvanceTime(waitTimeUs); // 推进信道小尺度时钟。
        uint32_t numSuccess = 0; // 初始化本次尝试成功子帧数。
        for (uint32_t i = 0; i < numSubframes; ++i) // 遍历 64 个子帧。
        {
            AdvanceTime(subframeTimeUs); // 推进一个子帧时间。
            totalTimeUs += subframeTimeUs; // 累加子帧传输耗时。
            m_currentSmallScaleDb = SampleRayleighPowerDb(); // 采样新的小尺度 Rayleigh 衰落。
            const double baseInstantSnrDb = m_currentLargeScaleSnrDb + m_currentSmallScaleDb; // 合成当前基准瞬时 SNR。
            lastInstantSnrDb = baseInstantSnrDb; // 记录最后子帧 SNR。
            subframeSnrs[i] = baseInstantSnrDb; // 保存子帧 SNR。
            const double per = m_mcsManager.GetPerForAction(baseInstantSnrDb, actionIndex); // 查询该动作在当前 SNR 下的 PER。
            if (Uniform01() > per) // 若随机数大于 PER，则认为子帧成功。
            {
                ++numSuccess; // 成功子帧计数加一。
            }
        }
        if (numSuccess > 0) // MATLAB 逻辑中只要有一个子帧成功就认为收到 ACK。
        {
            isSuccess = true; // 标记本次传输成功。
            actualDeliveredBits = static_cast<double>(numSuccess) * subframeBits; // 按成功子帧数计算实际交付比特。
            break; // 成功后退出重传循环。
        }
        ++retryCount; // 全部子帧失败时进入下一次重传。
    }
    UpdateLargeScale(totalTimeUs); // 用本次真实耗时推进大尺度 SNR。
    StepInfo info; // 创建反馈结构体。
    info.snrDb = lastInstantSnrDb; // 写入最后瞬时 SNR。
    info.retryCount = retryCount; // 写入重传次数。
    info.isSuccess = isSuccess; // 写入 ACK 成功标志。
    info.timeCostUs = totalTimeUs; // 写入总耗时。
    info.payloadBits = actualDeliveredBits; // 写入真实成功交付比特。
    info.subframeSnrs = subframeSnrs; // 写入子帧 SNR 数组。
    info.mcs = action.mcs; // 写入 MCS。
    info.bandwidthMHz = action.bandwidthMHz; // 写入带宽。
    info.nss = action.nss; // 写入空间流数。
    info.phyRateMbps = action.phyRateMbps; // 写入 PHY 速率。
    return std::make_pair(GetState(), info); // 返回下一状态和详细信息。
}

EnvironmentState WifiEnvironment::GetState() const // 获取当前状态。
{
    const double currentSnrDb = m_currentLargeScaleSnrDb + m_currentSmallScaleDb; // 合成当前瞬时 SNR。
    const double deltaSnrDb = currentSnrDb - m_lastSnrDb; // 计算相对上一状态的 SNR 变化。
    return EnvironmentState{currentSnrDb, deltaSnrDb}; // 返回二维状态。
}

WifiMcsManager& WifiEnvironment::GetMcsManager() // 获取可变 MCS 管理器。
{
    return m_mcsManager; // 返回成员引用。
}

const WifiMcsManager& WifiEnvironment::GetMcsManager() const // 获取只读 MCS 管理器。
{
    return m_mcsManager; // 返回成员引用。
}

double WifiEnvironment::GetPayloadBits() const // 获取载荷比特。
{
    return m_payloadBits; // 返回 AMPDU 总载荷。
}

double WifiEnvironment::GetFixedOverheadUs() const // 获取固定开销。
{
    return m_fixedOverheadUs; // 返回固定 MAC/PHY 开销。
}

double WifiEnvironment::GetSlotTimeUs() const // 获取 Slot 时间。
{
    return m_slotUs; // 返回 slot time。
}

double WifiEnvironment::GetCurrentLargeScaleSnrDb() const // 获取当前大尺度 SNR。
{
    return m_currentLargeScaleSnrDb; // 返回大尺度状态。
}

std::vector<double> WifiEnvironment::PeekSubframeSnrs(uint32_t numSubframes) const // 采样 Oracle 候选 SNR。
{
    std::vector<double> snrs; // 创建返回数组。
    snrs.reserve(numSubframes); // 预留子帧数量。
    for (uint32_t i = 0; i < numSubframes; ++i) // 遍历需要的样本数。
    {
        snrs.push_back(m_currentLargeScaleSnrDb + SampleRayleighPowerDb()); // 采样小尺度并叠加当前大尺度 SNR。
    }
    return snrs; // 返回候选 SNR。
}

void WifiEnvironment::AdvanceTime(double durationUs) // 推进小尺度时间。
{
    const double samplePeriodUs = 1.0e6 / m_sampleRateHz; // 将采样率转换为采样周期。
    const uint64_t steps = static_cast<uint64_t>(std::ceil(durationUs / samplePeriodUs)); // 对应 MATLAB ceil(duration_us/(1e6/SampleRate))。
    m_timeStep += steps; // 更新离散时间步。
}

void WifiEnvironment::UpdateLargeScale(double durationUs) // 更新大尺度 SNR。
{
    if (durationUs == 0.0) // 若没有经过时间。
    {
        return; // 不更新大尺度过程。
    }
    const double alpha = std::exp(-durationUs / m_referenceTimeUs); // 计算高斯-马尔可夫相关系数。
    const double noise = m_normalRv->GetValue(); // 采样标准正态噪声。
    m_currentLargeScaleSnrDb = alpha * m_currentLargeScaleSnrDb + (1.0 - alpha) * m_targetMeanSnrDb + m_snrStdDevDb * std::sqrt(1.0 - alpha * alpha) * noise; // 按 MATLAB 公式更新。
    if (m_currentLargeScaleSnrDb < 0.0) // 若大尺度 SNR 低于 0。
    {
        m_currentLargeScaleSnrDb = 0.0; // 按 MATLAB 逻辑截断为 0。
    }
}

double WifiEnvironment::SampleRayleighPowerDb() const // 采样 Rayleigh 功率增益 dB。
{
    const double real = m_normalRv->GetValue() / std::sqrt(2.0); // 采样复高斯实部。
    const double imag = m_normalRv->GetValue() / std::sqrt(2.0); // 采样复高斯虚部。
    const double power = std::max(real * real + imag * imag, std::numeric_limits<double>::min()); // 计算 |h|^2 并防止 log10(0)。
    return 10.0 * std::log10(power); // 转换为 dB。
}

uint32_t WifiEnvironment::UniformInteger(uint32_t minValue, uint32_t maxValue) const // 采样闭区间整数。
{
    return minValue + static_cast<uint32_t>(m_uniformRv->GetInteger(0, maxValue - minValue)); // 使用 ns-3 整数随机变量。
}

double WifiEnvironment::Uniform01() const // 采样均匀随机数。
{
    return m_uniformRv->GetValue(); // 返回 [0,1) 浮点数。
}

} // namespace myproject
