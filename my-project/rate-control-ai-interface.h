/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H
#define MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H

#include "ns3/ai-module.h"

#include <cstdint>

namespace ns3
{

/**
 * C++ -> Python 的环境状态。
 *
 * 这个结构体会被 ns3-ai 放进共享内存。C++ 速率管理器每到一个决策时刻
 * 写入这些字段，Python 进程读取后选择下一次要使用的速率档位。
 *
 * 注意：action 使用 1 开始的编号，action=1 对应最低的 HT MCS，action=N
 * 对应当前设备支持的第 N 个 HT MCS。这样做可以把 0 留给默认/未初始化语义，
 * 也更容易在 Python 端做边界裁剪。
 */
struct MyProjectRateControlEnv
{
  // Python 侧看到的决策序号，用于日志和学习过程排序。
  uint32_t step{0};
  // 当前站点可选的速率档位数量，通常是 802.11n 单流 HtMcs0..HtMcs7。
  uint32_t numActions{0};
  // 上一次真正应用到 ns-3 Wi-Fi 发送向量的 action。
  uint32_t lastAction{1};
  // 上一次 action 对应的 MCS 值，便于日志直接观察 Wi-Fi 档位。
  uint32_t lastMcs{0};
  // 最近一次反馈是否成功；1 表示当前没有连续失败，0 表示已有失败链。
  uint32_t lastSuccess{1};
  // 连续失败次数。Python 可用它快速降速，避免长时间卡在过高 MCS。
  uint32_t consecutiveFailures{0};
  // 最近一次从 PHY/ACK 反馈得到的瞬时 SNR，单位 dB。
  double snrDb{15.0};
  // 滑动窗口内的平均 SNR，用来平滑瞬时抖动。
  double meanSnrDb{15.0};
  // 滑动窗口内 SNR 标准差，用来表示信道变化剧烈程度。
  double stdSnrDb{0.0};
  // 滑动窗口内 ACK 成功比例，是链路可靠性的直接反馈。
  double ackRatio{1.0};
  // 上一次已选速率的物理层速率，单位 Mbps。
  double selectedRateMbps{0.0};
  // 当前候选集合中最高物理层速率，奖励函数会用它做归一化。
  double bestRateMbps{0.0};
  // C++ 根据“是否成功”和“速率效率”计算出的上一轮奖励。
  double reward{0.0};
};

/**
 * Python -> C++ 的动作。
 *
 * Python 写入 nextAction 后，C++ 会把它裁剪到 [1, numActions]，再转换成
 * 对应的 WifiMode。nextMcs 目前主要用于日志/调试，真正生效的是 nextAction。
 */
struct MyProjectRateControlAct
{
  // 下一次数据帧发送应使用的 1-based action。
  double nextAction{1.0};
  // action 对应的 MCS 值；Python 侧用 action - 1 填充。
  double nextMcs{0.0};
  // 学习器当前探索率，回传给 C++ 只用于 trace 记录。
  double epsilon{0.0};
};

using MyProjectRateControlInterface =
    Ns3AiMsgInterfaceImpl<MyProjectRateControlEnv, MyProjectRateControlAct>;

} // namespace ns3

#endif // MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H
