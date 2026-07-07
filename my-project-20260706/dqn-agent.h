/*
 * MATLAB RA project migration: lightweight DQN-style agent.
 *
 * Overview
 * --------
 * MATLAB `DQN_Agent` used Deep Learning Toolbox networks.  A direct dependency
 * on PyTorch or libtorch would make this ns-3 scratch migration fragile and
 * non-native.  This file preserves the surrounding DQN control logic:
 * six-feature normalization, 18-dimensional state stacking, epsilon decay,
 * replay memory, target values, and online Q updates.  The neural network is
 * represented by a linear Q approximator, which keeps the code self-contained
 * and compileable inside ns-3 while retaining the algorithmic data flow.
 *
 * State layout
 * ------------
 * raw input: [mean_snr, inst_snr, delta_snr, std_snr, last_action, ack_ratio]
 * stacked state: [current 6 normalized features, previous 12 features]
 */

#ifndef SCRATCH_MY_PROJECT_DQN_AGENT_H // 防止头文件重复包含。
#define SCRATCH_MY_PROJECT_DQN_AGENT_H // 定义头文件保护宏。

#include "ns3/core-module.h" // 引入 ns-3 随机变量。

#include <array> // 使用固定长度状态数组。
#include <cstdint> // 使用明确位宽整数。
#include <vector> // 使用 vector 保存权重和经验。

namespace myproject // 使用项目命名空间。
{

class DqnAgent // 定义 DQN 风格智能体。
{
  public:
    static constexpr uint32_t InputDim = 18; // 保存堆叠状态维度。
    static constexpr uint32_t RawDim = 6; // 保存原始特征维度。
    using State = std::array<double, InputDim>; // 定义状态数组类型。
    using RawFeatures = std::array<double, RawDim>; // 定义原始特征数组类型。

    explicit DqnAgent(uint32_t numActions); // 构造函数初始化权重、状态和经验池。
    std::pair<uint32_t, State> SelectAction(const RawFeatures& rawFeatures); // 选择动作并返回当前状态。
    void Update(const State& state, uint32_t actionIndex, double reward, const RawFeatures& nextRawFeatures); // 写入经验并学习。
    double GetEpsilon() const; // 返回当前 epsilon。

  private:
    struct Transition // 定义经验回放条目。
    {
        State state; // 保存当前状态。
        uint32_t actionIndex; // 保存动作索引。
        double reward; // 保存奖励。
        State nextState; // 保存下一状态。
    };

    RawFeatures NormalizeFeatures(const RawFeatures& rawFeatures) const; // 按 MATLAB 逻辑归一化 6 维特征。
    State StackState(const RawFeatures& normalizedFeatures, const State& previousState) const; // 构造 18 维堆叠状态。
    double PredictQ(const State& state, uint32_t actionIndex, bool useTarget) const; // 预测某动作 Q 值。
    uint32_t GreedyAction(const State& state, bool useTarget) const; // 选择最大 Q 的动作。
    void Learn(); // 从经验池采样并更新权重。
    void SyncTarget(); // 同步目标网络权重。

    uint32_t m_numActions; // 保存动作数量。
    State m_stateBuffer{}; // 保存上一时刻堆叠状态。
    std::vector<std::vector<double>> m_weights; // 保存在线 Q 线性权重。
    std::vector<std::vector<double>> m_targetWeights; // 保存目标 Q 线性权重。
    double m_gamma = 0.7; // 保存 MATLAB Gamma。
    double m_epsilon = 1.0; // 保存当前 epsilon。
    double m_epsilonDecay = 0.9998; // 保存 epsilon 衰减系数。
    double m_minEpsilon = 0.0005; // 保存最小 epsilon。
    double m_learningRate = 0.0003; // 保存初始学习率。
    double m_minLearningRate = 1e-6; // 保存最小学习率。
    uint32_t m_targetUpdateFreq = 1000; // 保存目标网络同步频率。
    uint32_t m_stepCounter = 0; // 保存总选择步数。
    uint32_t m_learnCounter = 0; // 保存学习步数。
    uint32_t m_memoryCapacity = 6000; // 保存经验池容量。
    uint32_t m_memoryPtr = 0; // 保存循环写入指针。
    uint32_t m_batchSize = 128; // 保存 batch 大小。
    std::vector<Transition> m_memory; // 保存经验池。
    ns3::Ptr<ns3::UniformRandomVariable> m_uniformRv; // 保存 ns-3 均匀随机变量。
    ns3::Ptr<ns3::NormalRandomVariable> m_normalRv; // 保存 ns-3 正态随机变量。
};

} // namespace myproject

#endif // SCRATCH_MY_PROJECT_DQN_AGENT_H
