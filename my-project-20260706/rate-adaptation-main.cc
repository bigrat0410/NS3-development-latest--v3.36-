/*
 * MATLAB RA project migration: ns-3 scratch entry point.
 *
 * Code overview
 * -------------
 * This program migrates the MATLAB project
 * `AAA_场景1_main_project` into an ns-3 scratch program.  It follows the
 * development style of `scratch/reinrate`: keep the research algorithm and
 * environment code inside the scratch project, expose algorithm selection
 * through `CommandLine`, and write plain result files for post-processing.
 *
 * Structure
 * ---------
 * - `RunOracle`: ports MATLAB `algorithm_select == 0`.
 * - `RunMinstrel`: ports MATLAB `algorithm_select == 1`.
 * - `RunDqn`: ports MATLAB `algorithm_select == 2`.
 * - `RunMab`: ports MATLAB `algorithm_select == 3`.
 * - `main`: parses ns-3 command-line arguments and dispatches one branch.
 *
 * Important migration notes
 * -------------------------
 * The original MATLAB environment is not a full node-level ns-3 topology.  It
 * is a packet-level RA environment with ns-3-derived PER curves.  Therefore this
 * migration implements the same packet-level model natively in C++ and ns-3
 * RNGs instead of forcing an artificial Wi-Fi topology that would change the
 * mathematical meaning of the experiment.  Where MATLAB used
 * `comm.RayleighChannel`, the port uses ns-3 random variables to sample
 * equivalent flat Rayleigh fading.
 *
 * Output files
 * ------------
 * The program writes CSV files under `my-project-results/`, preserving the core
 * arrays from MATLAB `.mat` outputs: actual throughput, ideal throughput, SNR,
 * selected action, best action, and DQN epsilon when applicable.
 */

#include "dqn-agent.h" // 引入 DQN 风格智能体。
#include "ideal-throughput-manager.h" // 引入 Oracle 理想吞吐管理器。
#include "mab-agent.h" // 引入 MAB 智能体。
#include "minstrel-agent.h" // 引入 Minstrel 风格智能体。
#include "wifi-environment.h" // 引入 Wi-Fi 环境。

#include "ns3/command-line.h" // 引入 ns-3 命令行解析。
#include "ns3/core-module.h" // 引入 ns-3 核心模块。

#include <algorithm> // 使用 max 和 min。
#include <cmath> // 使用 exp 和 sqrt。
#include <cstdint> // 使用明确位宽整数。
#include <fstream> // 使用文件输出。
#include <iomanip> // 使用输出精度控制。
#include <iostream> // 使用控制台输出。
#include <numeric> // 使用 accumulate。
#include <string> // 使用字符串。
#include <vector> // 使用动态数组。

using namespace ns3; // 使用 ns-3 命名空间以贴近 scratch 示例风格。
using namespace myproject; // 使用迁移项目命名空间。

namespace // 使用匿名命名空间限制辅助函数作用域。
{

double Mean(const std::vector<double>& values) // 计算向量均值。
{
    if (values.empty()) // 如果输入为空。
    {
        return 0.0; // 返回 0。
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0); // 累加所有值。
    return sum / static_cast<double>(values.size()); // 返回平均值。
}

double StdDev(const std::vector<double>& values) // 计算样本标准差近似。
{
    if (values.empty()) // 如果输入为空。
    {
        return 0.0; // 返回 0。
    }
    const double mean = Mean(values); // 先计算均值。
    double sq = 0.0; // 初始化平方差求和。
    for (double value : values) // 遍历所有样本。
    {
        const double diff = value - mean; // 计算偏差。
        sq += diff * diff; // 累加平方差。
    }
    return std::sqrt(sq / static_cast<double>(values.size())); // 返回总体标准差。
}

void EnsureResultsDir() // 确保输出目录存在。
{
    std::system("mkdir -p my-project-results"); // 在 ns-3 Linux 环境中创建结果目录。
}

std::ofstream OpenCsv(const std::string& filename, const std::string& header) // 打开 CSV 并写表头。
{
    EnsureResultsDir(); // 确保结果目录存在。
    std::ofstream file("my-project-results/" + filename); // 打开目标 CSV 文件。
    file << header << '\n'; // 写入 CSV 表头。
    file << std::fixed << std::setprecision(8); // 固定浮点输出精度。
    return file; // 返回文件流。
}

void RunOracle(uint32_t totalPackets, uint32_t progressInterval) // 运行 Oracle 分支。
{
    WifiEnvironment env; // 创建 Wi-Fi 环境。
    env.Reset(); // 重置环境。
    IdealThroughputManager idealMgr(env.GetMcsManager(), env); // 创建 Oracle 管理器。
    std::ofstream csv = OpenCsv("oracle-results.csv", "step,actual_tp_mbps,base_snr_20mhz,eff_snr_db,best_action,best_mcs,best_bw_mhz,best_nss"); // 创建 Oracle 输出文件。
    double sumThroughput = 0.0; // 初始化吞吐求和。
    for (uint32_t t = 1; t <= totalPackets; ++t) // 遍历所有包。
    {
        std::vector<double> currentSnrs = env.PeekSubframeSnrs(64); // 生成 64 个 Oracle 候选子帧 SNR。
        auto oracle = idealMgr.Calculate(currentSnrs); // 计算理论最优动作。
        const uint32_t bestAction = oracle.second; // 读取最优动作。
        auto stepResult = env.Step(bestAction); // 在真实环境中执行最优动作。
        const StepInfo& info = stepResult.second; // 读取环境反馈。
        const WifiAction& action = env.GetMcsManager().GetAction(bestAction); // 读取动作详情。
        const double actualTp = info.timeCostUs > 0.0 ? info.payloadBits / info.timeCostUs : 0.0; // bits/us 等于 Mbps。
        const double penalty = 10.0 * std::log10(static_cast<double>(action.bandwidthMHz) / 20.0) + 3.0 * static_cast<double>(action.nss - 1); // 计算有效 SNR 惩罚。
        const double effectiveSnr = env.GetCurrentLargeScaleSnrDb() - penalty; // 计算动作有效 SNR。
        sumThroughput += actualTp; // 累加吞吐。
        csv << t << ',' << actualTp << ',' << env.GetCurrentLargeScaleSnrDb() << ',' << effectiveSnr << ',' << bestAction << ',' << static_cast<uint32_t>(action.mcs) << ',' << action.bandwidthMHz << ',' << static_cast<uint32_t>(action.nss) << '\n'; // 写一行结果。
        if (progressInterval > 0 && t % progressInterval == 0) // 判断是否打印进度。
        {
            std::cout << "[Oracle] Step " << t << " actual_tp=" << actualTp << " Mbps best_action=" << bestAction << std::endl; // 打印进度。
        }
    }
    std::cout << "[Oracle] mean_actual_tp=" << (sumThroughput / static_cast<double>(totalPackets)) << " Mbps" << std::endl; // 打印平均吞吐。
}

void RunMinstrel(uint32_t totalPackets, uint32_t progressInterval) // 运行 Minstrel 分支。
{
    WifiEnvironment env; // 创建环境。
    env.Reset(); // 重置环境。
    IdealThroughputManager idealMgr(env.GetMcsManager(), env); // 创建 Oracle 旁路管理器。
    MinstrelAgent agent(env.GetMcsManager()); // 创建 Minstrel 智能体。
    std::ofstream csv = OpenCsv("minstrel-results.csv", "step,actual_tp_mbps,ideal_tp_mbps,snr_db,action,best_action,is_success,retry_count"); // 创建输出文件。
    for (uint32_t t = 1; t <= totalPackets; ++t) // 遍历所有包。
    {
        const uint32_t action = agent.SelectAction(); // Minstrel 选择动作。
        auto stepResult = env.Step(action); // 环境执行动作。
        const StepInfo& info = stepResult.second; // 读取反馈。
        auto oracle = idealMgr.Calculate(info.subframeSnrs); // 用实际子帧 SNR 计算 Oracle。
        const double idealTp = oracle.first; // 读取理论吞吐。
        const uint32_t bestAction = oracle.second; // 读取最优动作。
        const double actualTp = info.isSuccess && info.timeCostUs > 0.0 ? info.payloadBits / info.timeCostUs : 0.0; // 成功才结算吞吐。
        agent.UpdateStat(action, info.isSuccess); // 用 ACK 反馈更新 Minstrel 统计。
        csv << t << ',' << actualTp << ',' << idealTp << ',' << Mean(info.subframeSnrs) << ',' << action << ',' << bestAction << ',' << (info.isSuccess ? 1 : 0) << ',' << info.retryCount << '\n'; // 写一行结果。
        if (progressInterval > 0 && t % progressInterval == 0) // 判断是否打印进度。
        {
            std::cout << "[Minstrel] Step " << t << " actual_tp=" << actualTp << " Mbps ideal_tp=" << idealTp << " Mbps" << std::endl; // 打印进度。
        }
    }
}

void RunMab(uint32_t totalPackets, uint32_t progressInterval) // 运行 MAB 分支。
{
    WifiEnvironment env; // 创建环境。
    EnvironmentState state = env.Reset(); // 重置环境并保存状态。
    IdealThroughputManager idealMgr(env.GetMcsManager(), env); // 创建 Oracle 管理器。
    MabAgent agent(env.GetMcsManager().GetNumActions(), 30, 0.1); // 创建 MATLAB 参数的 MAB 智能体。
    std::ofstream csv = OpenCsv("mab-results.csv", "step,actual_tp_mbps,ideal_tp_mbps,snr_db,action,best_action,is_success,retry_count"); // 创建输出文件。
    for (uint32_t t = 1; t <= totalPackets; ++t) // 遍历所有包。
    {
        const double currentSnr = state.currentSnrDb; // 读取当前 SNR。
        const uint32_t action = agent.SelectAction(currentSnr); // MAB 根据 SNR 选动作。
        auto stepResult = env.Step(action); // 环境执行动作。
        state = stepResult.first; // 更新下一状态。
        const StepInfo& info = stepResult.second; // 读取反馈。
        auto oracle = idealMgr.Calculate(info.subframeSnrs); // 计算 Oracle。
        const double idealTp = oracle.first; // 读取理论吞吐。
        const uint32_t bestAction = oracle.second; // 读取最佳动作。
        const double actualTp = info.isSuccess && info.timeCostUs > 0.0 ? info.payloadBits / info.timeCostUs : 0.0; // 计算实际吞吐。
        agent.Update(currentSnr, action, actualTp); // 用真实吞吐更新 MAB。
        csv << t << ',' << actualTp << ',' << idealTp << ',' << Mean(info.subframeSnrs) << ',' << action << ',' << bestAction << ',' << (info.isSuccess ? 1 : 0) << ',' << info.retryCount << '\n'; // 写一行结果。
        if (progressInterval > 0 && t % progressInterval == 0) // 判断是否打印进度。
        {
            std::cout << "[MAB] Step " << t << " actual_tp=" << actualTp << " Mbps ideal_tp=" << idealTp << " Mbps" << std::endl; // 打印进度。
        }
    }
}

void RunDqn(uint32_t totalPackets, uint32_t progressInterval) // 运行 DQN 分支。
{
    WifiEnvironment env; // 创建环境。
    EnvironmentState state = env.Reset(); // 重置环境并保存状态。
    IdealThroughputManager idealMgr(env.GetMcsManager(), env); // 创建 Oracle 管理器。
    DqnAgent agent(env.GetMcsManager().GetNumActions()); // 创建 DQN 风格智能体。
    std::ofstream csv = OpenCsv("dqn-results.csv", "step,actual_tp_mbps,ideal_tp_mbps,snr_db,action,best_action,epsilon,reward,is_success,retry_count"); // 创建输出文件。
    double prevMeanSnr = state.currentSnrDb; // 初始化上一窗口均值 SNR。
    double prevStdSnr = 0.0; // 初始化上一窗口 SNR 标准差。
    double prevAckRatio = 1.0; // 初始化上一窗口 ACK 比例。
    uint32_t lastAction = 1; // 初始化上一动作。
    for (uint32_t t = 1; t <= totalPackets; ++t) // 遍历所有包。
    {
        DqnAgent::RawFeatures raw = {prevMeanSnr, state.currentSnrDb, state.deltaSnrDb, prevStdSnr, static_cast<double>(lastAction), prevAckRatio}; // 构造 MATLAB 六维特征。
        auto selected = agent.SelectAction(raw); // 选择动作并返回堆叠状态。
        const uint32_t action = selected.first; // 读取动作。
        const DqnAgent::State stackedState = selected.second; // 读取堆叠状态。
        auto stepResult = env.Step(action); // 环境执行动作。
        const EnvironmentState nextState = stepResult.first; // 读取下一状态。
        const StepInfo& info = stepResult.second; // 读取反馈。
        auto oracle = idealMgr.Calculate(info.subframeSnrs); // 计算 Oracle。
        const double idealTp = oracle.first; // 读取理论吞吐。
        const uint32_t bestAction = oracle.second; // 读取最佳动作。
        const double actualTp = info.isSuccess && info.timeCostUs > 0.0 ? info.payloadBits / info.timeCostUs : 0.0; // 计算实际吞吐。
        double efficiency = actualTp / (idealTp + 1e-6); // 计算效率。
        efficiency = std::min(1.0, efficiency); // 限制效率上界为 1。
        const double rewardBase = 10.0 / (1.0 + std::exp(-12.0 * (efficiency - 0.85))); // 迁移 MATLAB sigmoid reward。
        const double penaltySmooth = 0.5 * (std::abs(static_cast<int32_t>(action) - static_cast<int32_t>(lastAction)) / 78.0); // 迁移动作平滑惩罚。
        double reward = rewardBase - penaltySmooth; // 组合奖励。
        if (efficiency < 0.05) // 如果效率极低。
        {
            reward = -1.5; // 使用 MATLAB 的失败惩罚。
        }
        const double nextMeanSnr = Mean(info.subframeSnrs); // 计算下一窗口均值 SNR。
        const double nextStdSnr = StdDev(info.subframeSnrs); // 计算下一窗口 SNR 标准差。
        const double nextAckRatio = info.payloadBits / (64.0 * 12000.0); // 迁移 MATLAB ack_ratio 公式。
        DqnAgent::RawFeatures nextRaw = {nextMeanSnr, nextState.currentSnrDb, nextState.deltaSnrDb, nextStdSnr, static_cast<double>(action), nextAckRatio}; // 构造下一原始特征。
        agent.Update(stackedState, action, reward, nextRaw); // 更新 DQN 经验与权重。
        csv << t << ',' << actualTp << ',' << idealTp << ',' << nextMeanSnr << ',' << action << ',' << bestAction << ',' << agent.GetEpsilon() << ',' << reward << ',' << (info.isSuccess ? 1 : 0) << ',' << info.retryCount << '\n'; // 写一行结果。
        prevMeanSnr = nextMeanSnr; // 更新上一均值 SNR。
        prevStdSnr = nextStdSnr; // 更新上一 SNR 标准差。
        prevAckRatio = nextAckRatio; // 更新上一 ACK 比例。
        state = nextState; // 更新环境状态。
        lastAction = action; // 更新上一动作。
        if (progressInterval > 0 && t % progressInterval == 0) // 判断是否打印进度。
        {
            std::cout << "[DQN] Step " << t << " actual_tp=" << actualTp << " Mbps epsilon=" << agent.GetEpsilon() << " action=" << action << std::endl; // 打印进度。
        }
    }
}

} // namespace

int main(int argc, char* argv[]) // ns-3 scratch 程序入口。
{
    uint32_t algorithm = 0; // 默认运行 Oracle，对应 MATLAB algorithm_select=0。
    uint32_t totalPackets = 1000; // 默认步数较小，确保验收时快速运行。
    uint32_t progressInterval = 1; // 默认不打印周期进度，避免输出过多。
    uint32_t seed = 1; // 默认随机种子。
    uint32_t run = 1; // 默认随机 run。
    CommandLine cmd(__FILE__); // 创建 ns-3 命令行对象。
    cmd.AddValue("algorithm", "0=Oracle, 1=Minstrel, 2=DQN, 3=MAB", algorithm); // 添加算法选择参数。
    cmd.AddValue("totalPackets", "Number of packet-level interaction steps", totalPackets); // 添加总步数参数。
    cmd.AddValue("progressInterval", "Print progress every N steps, 0 disables progress logs", progressInterval); // 添加进度打印参数。
    cmd.AddValue("Seed", "ns-3 RNG seed", seed); // 添加随机种子参数。
    cmd.AddValue("Run", "ns-3 RNG run", run); // 添加随机 run 参数。
    cmd.Parse(argc, argv); // 解析命令行参数。
    RngSeedManager::SetSeed(seed); // 设置 ns-3 随机种子。
    RngSeedManager::SetRun(run); // 设置 ns-3 随机 run。
    if (algorithm == 0) // 判断 Oracle 分支。
    {
        RunOracle(totalPackets, progressInterval); // 运行 Oracle。
    }
    else if (algorithm == 1) // 判断 Minstrel 分支。
    {
        RunMinstrel(totalPackets, progressInterval); // 运行 Minstrel。
    }
    else if (algorithm == 2) // 判断 DQN 分支。
    {
        RunDqn(totalPackets, progressInterval); // 运行 DQN。
    }
    else if (algorithm == 3) // 判断 MAB 分支。
    {
        RunMab(totalPackets, progressInterval); // 运行 MAB。
    }
    else // 处理非法算法编号。
    {
        NS_ABORT_MSG("Unsupported algorithm value; use 0, 1, 2, or 3"); // 抛出 ns-3 错误。
    }
    return 0; // 正常退出。
}
