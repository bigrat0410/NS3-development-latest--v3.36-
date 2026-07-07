/*
 * MATLAB RA project migration: lightweight DQN-style implementation.
 */

#include "dqn-agent.h" // 引入类声明。

#include <algorithm> // 使用 max。
#include <cmath> // 使用 exp。
#include <limits> // 使用数值边界。

namespace myproject // 实现项目命名空间。
{

DqnAgent::DqnAgent(uint32_t numActions) // 构造 DQN 智能体。
    : m_numActions(numActions) // 保存动作数量。
{
    m_uniformRv = ns3::CreateObject<ns3::UniformRandomVariable>(); // 创建均匀随机变量。
    m_uniformRv->SetAttribute("Min", ns3::DoubleValue(0.0)); // 设置下界。
    m_uniformRv->SetAttribute("Max", ns3::DoubleValue(1.0)); // 设置上界。
    m_normalRv = ns3::CreateObject<ns3::NormalRandomVariable>(); // 创建正态随机变量。
    m_normalRv->SetAttribute("Mean", ns3::DoubleValue(0.0)); // 设置均值。
    m_normalRv->SetAttribute("Variance", ns3::DoubleValue(0.0001)); // 设置小方差以初始化线性权重。
    m_weights.assign(m_numActions, std::vector<double>(InputDim + 1, 0.0)); // 创建在线权重矩阵，额外一维为偏置。
    for (uint32_t action = 0; action < m_numActions; ++action) // 遍历动作。
    {
        for (double& weight : m_weights[action]) // 遍历该动作的权重。
        {
            weight = m_normalRv->GetValue(); // 使用小随机数初始化。
        }
    }
    SyncTarget(); // 初始化目标权重。
    m_memory.reserve(m_memoryCapacity); // 预留经验池容量。
}

std::pair<uint32_t, DqnAgent::State> DqnAgent::SelectAction(const RawFeatures& rawFeatures) // 选择动作。
{
    const RawFeatures normalized = NormalizeFeatures(rawFeatures); // 归一化 6 维输入。
    const State currentState = StackState(normalized, m_stateBuffer); // 拼接当前特征和历史 12 维状态。
    m_stateBuffer = currentState; // 更新状态缓冲区。
    uint32_t actionIndex = 1; // 初始化动作。
    if (m_uniformRv->GetValue() < m_epsilon) // 按 epsilon 判断探索。
    {
        actionIndex = m_uniformRv->GetInteger(1, m_numActions); // 探索阶段随机动作。
    }
    else // 否则进入利用阶段。
    {
        actionIndex = GreedyAction(currentState, false); // 使用在线 Q 权重选择动作。
    }
    m_epsilon = std::max(m_minEpsilon, m_epsilon * m_epsilonDecay); // 按 MATLAB 逻辑衰减 epsilon。
    m_learningRate = std::max(m_minLearningRate, 0.0003 * std::exp(-static_cast<double>(m_stepCounter) / 5000.0)); // 按 MATLAB 逻辑衰减学习率。
    ++m_stepCounter; // 更新步数。
    return std::make_pair(actionIndex, currentState); // 返回动作和当前堆叠状态。
}

void DqnAgent::Update(const State& state, uint32_t actionIndex, double reward, const RawFeatures& nextRawFeatures) // 写入经验并学习。
{
    const RawFeatures nextNormalized = NormalizeFeatures(nextRawFeatures); // 归一化下一原始特征。
    const State nextState = StackState(nextNormalized, state); // 构造下一堆叠状态。
    Transition transition{state, actionIndex, reward, nextState}; // 构造经验条目。
    if (m_memory.size() < m_memoryCapacity) // 如果经验池尚未满。
    {
        m_memory.push_back(transition); // 追加经验。
    }
    else // 如果经验池已经满。
    {
        m_memory[m_memoryPtr] = transition; // 循环覆盖旧经验。
    }
    m_memoryPtr = (m_memoryPtr + 1) % m_memoryCapacity; // 更新循环写指针。
    if (m_memory.size() > m_batchSize && m_epsilon < 0.999) // 对应 MATLAB 的学习启动条件。
    {
        Learn(); // 执行学习。
    }
}

double DqnAgent::GetEpsilon() const // 返回 epsilon。
{
    return m_epsilon; // 返回当前探索率。
}

DqnAgent::RawFeatures DqnAgent::NormalizeFeatures(const RawFeatures& rawFeatures) const // 归一化原始特征。
{
    RawFeatures normalized = rawFeatures; // 复制原始特征。
    normalized[0] = rawFeatures[0] / 45.0; // 归一化 mean_snr。
    normalized[1] = rawFeatures[1] / 45.0; // 归一化 inst_snr。
    normalized[2] = rawFeatures[2] / 6.0; // 归一化 delta_snr。
    normalized[3] = rawFeatures[3] / 6.0; // 归一化 std_snr。
    normalized[4] = rawFeatures[4] / 78.0; // 归一化 last_action。
    normalized[5] = rawFeatures[5]; // ack_ratio 本来就是 0 到 1。
    return normalized; // 返回归一化特征。
}

DqnAgent::State DqnAgent::StackState(const RawFeatures& normalizedFeatures, const State& previousState) const // 构造堆叠状态。
{
    State state{}; // 创建 18 维状态。
    for (uint32_t i = 0; i < RawDim; ++i) // 写入当前 6 维特征。
    {
        state[i] = normalizedFeatures[i]; // 当前特征放在最前。
    }
    for (uint32_t i = 0; i < 12; ++i) // 复制上一状态前 12 维。
    {
        state[RawDim + i] = previousState[i]; // 与 MATLAB `[v_n, StateBuffer(1:12)]` 一致。
    }
    return state; // 返回堆叠状态。
}

double DqnAgent::PredictQ(const State& state, uint32_t actionIndex, bool useTarget) const // 预测 Q 值。
{
    const std::vector<std::vector<double>>& weights = useTarget ? m_targetWeights : m_weights; // 选择在线或目标权重。
    const std::vector<double>& actionWeights = weights[actionIndex - 1]; // 获取该动作的权重。
    double q = actionWeights[InputDim]; // 读取偏置项。
    for (uint32_t i = 0; i < InputDim; ++i) // 遍历状态维度。
    {
        q += actionWeights[i] * state[i]; // 累加线性 Q 值。
    }
    return q; // 返回 Q 值。
}

uint32_t DqnAgent::GreedyAction(const State& state, bool useTarget) const // 选择最大 Q 动作。
{
    double bestQ = -std::numeric_limits<double>::infinity(); // 初始化最佳 Q。
    uint32_t bestAction = 1; // 初始化最佳动作。
    for (uint32_t actionIndex = 1; actionIndex <= m_numActions; ++actionIndex) // 遍历动作。
    {
        const double q = PredictQ(state, actionIndex, useTarget); // 计算该动作 Q 值。
        if (q > bestQ) // 判断是否更大。
        {
            bestQ = q; // 更新最佳 Q。
            bestAction = actionIndex; // 更新最佳动作。
        }
    }
    return bestAction; // 返回最佳动作。
}

void DqnAgent::Learn() // 学习更新。
{
    for (uint32_t sample = 0; sample < m_batchSize; ++sample) // 采样 batchSize 条经验。
    {
        const uint32_t index = m_uniformRv->GetInteger(0, static_cast<uint32_t>(m_memory.size() - 1)); // 随机经验索引。
        const Transition& transition = m_memory[index]; // 读取经验。
        const uint32_t bestNextAction = GreedyAction(transition.nextState, false); // Double DQN：在线网选下一动作。
        const double targetNextQ = PredictQ(transition.nextState, bestNextAction, true); // 目标网评价下一动作。
        const double target = transition.reward + m_gamma * targetNextQ; // 计算 TD 目标。
        const double prediction = PredictQ(transition.state, transition.actionIndex, false); // 计算当前预测。
        const double error = target - prediction; // 计算 TD 误差。
        std::vector<double>& weights = m_weights[transition.actionIndex - 1]; // 获取当前动作权重。
        for (uint32_t i = 0; i < InputDim; ++i) // 遍历状态维度。
        {
            weights[i] += m_learningRate * error * transition.state[i]; // 线性函数近似梯度更新。
        }
        weights[InputDim] += m_learningRate * error; // 更新偏置项。
    }
    ++m_learnCounter; // 学习计数加一。
    if (m_learnCounter % m_targetUpdateFreq == 0) // 判断是否需要同步目标网络。
    {
        SyncTarget(); // 同步目标权重。
    }
}

void DqnAgent::SyncTarget() // 同步目标网络。
{
    m_targetWeights = m_weights; // 复制在线权重到目标权重。
}

} // namespace myproject
