/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MY_PROJECT_LEARNING_RATE_MANAGER_H
#define MY_PROJECT_LEARNING_RATE_MANAGER_H

#include "rate-control-ai-interface.h"

#include "ns3/wifi-remote-station-manager.h"

#include <deque>
#include <fstream>
#include <string>
#include <vector>

namespace ns3
{

/**
 * A small Wi-Fi rate manager used by scratch/my-project.
 *
 * The manager is intentionally attached at the same ns-3 hook point used by
 * native rate-control algorithms: DoGetDataTxVector chooses the next data
 * mode, and DoReportDataOk/DoReportDataFailed feed the learning algorithm with
 * real MAC/PHY feedback.  Packet success and PER are not simulated here; they
 * remain inside the configured ns-3 PHY and ErrorRateModel.
 */
class MyProjectLearningRateManager : public WifiRemoteStationManager
{
public:
  static TypeId GetTypeId ();

  MyProjectLearningRateManager ();
  ~MyProjectLearningRateManager () override;

private:
  struct LearningWifiRemoteStation : public WifiRemoteStation
  {
    // 最近一次有效 action。所有 action 都是 1-based，方便和 Python 端一致。
    uint32_t lastAction{1};
    // 当前正在尝试的 action，保留给日志/调试使用。
    uint32_t currentAction{1};
    // 最近一次接收反馈换算出的 SNR(dB)。没有反馈前给一个中等默认值。
    double lastSnrDb{15.0};
    // 上一次反馈后计算好的滑动窗口统计量，会作为下一次环境状态发给 Python。
    double previousMeanSnrDb{15.0};
    double previousStdSnrDb{0.0};
    double previousAckRatio{1.0};
    // 上一次发送向量对应的 PHY 速率。为 0 表示还没有可用于学习的发送记录。
    double lastPhyRateMbps{0.0};
    // 当前候选 MCS 中的最高 PHY 速率，用于把奖励归一化到“效率”。
    double bestPhyRateMbps{0.0};
    // 最近一次 C++ 计算出的 reward，Python 下一次决策时读取它。
    double lastReward{0.0};
    // Python 当前探索率，仅用于输出 trace，C++ 不使用它做决策。
    double lastEpsilon{0.0};
    // Python 请求的 action 和 C++ 裁剪后真正应用的 action，便于排查越界。
    uint32_t lastRequestedAction{1};
    uint32_t lastAppliedAction{1};
    // 最近一次可用 action 数，反馈回调里无法重新拿 modes，所以需要缓存。
    uint32_t lastNumActions{1};
    // 当前 MCS 值，和 action 一起写入日志。
    uint32_t currentMcs{0};
    // 累计成功/失败次数，以及连续成功/失败次数，用于构造可靠性特征。
    uint32_t successes{0};
    uint32_t failures{0};
    uint32_t consecutiveSuccesses{0};
    uint32_t consecutiveFailures{0};
    // 发给 Python 的决策步编号。
    uint32_t decisionStep{0};
    // 下一个允许请求 Python 决策的仿真时间，防止每个包都同步一次共享内存。
    double nextDecisionTime{0.0};
    // 最近 SNR 与 ACK 结果窗口。SNR 窗口较短，ACK 窗口较长，以兼顾灵敏度和稳定性。
    std::deque<double> snrWindow;
    std::deque<bool> ackWindow;
  };

  WifiRemoteStation* DoCreateStation () const override;
  WifiTxVector DoGetDataTxVector (WifiRemoteStation* station) override;
  WifiTxVector DoGetRtsTxVector (WifiRemoteStation* station) override;

  void DoReportRxOk (WifiRemoteStation* station, double rxSnr, WifiMode txMode) override;
  void DoReportRtsFailed (WifiRemoteStation* station) override;
  void DoReportDataFailed (WifiRemoteStation* station) override;
  void DoReportRtsOk (WifiRemoteStation* station,
                      double ctsSnr,
                      WifiMode ctsMode,
                      double rtsSnr) override;
  void DoReportDataOk (WifiRemoteStation* station,
                       double ackSnr,
                       WifiMode ackMode,
                       double dataSnr,
                       uint16_t dataChannelWidth,
                       uint8_t dataNss) override;
  void DoReportAmpduTxStatus (WifiRemoteStation* station,
                              uint16_t nSuccessfulMpdus,
                              uint16_t nFailedMpdus,
                              double rxSnr,
                              double dataSnr,
                              uint16_t dataChannelWidth,
                              uint8_t dataNss) override;
  void DoReportFinalRtsFailed (WifiRemoteStation* station) override;
  void DoReportFinalDataFailed (WifiRemoteStation* station) override;

  std::vector<WifiMode> GetHtModes (WifiRemoteStation* station) const;
  WifiTxVector BuildDataTxVector (WifiRemoteStation* station, WifiMode mode) const;
  WifiTxVector BuildControlTxVector (WifiRemoteStation* station) const;
  void UpdateFromFeedback (LearningWifiRemoteStation* station, bool success, double snrLinear);
  void TraceFeedback (const LearningWifiRemoteStation* station,
                      bool success,
                      uint32_t action,
                      uint32_t mcsValue,
                      double reward);
  uint32_t RequestPythonAction (LearningWifiRemoteStation* station,
                                const std::vector<WifiMode>& modes,
                                double bestRateMbps);
  double CalculateReward (const LearningWifiRemoteStation* station,
                          bool success,
                          uint32_t action,
                          uint32_t numActions,
                          double selectedRateMbps,
                          double bestRateMbps) const;
  double LinearSnrToDb (double snrLinear) const;
  double MeanSnr (const LearningWifiRemoteStation* station) const;
  double StdSnr (const LearningWifiRemoteStation* station, double mean) const;
  double AckRatio (const LearningWifiRemoteStation* station) const;
  bool UseDqn () const;
  bool UseMab () const;

  std::string m_algorithm{"mab"};
  std::string m_traceFile;
  double m_decisionIntervalSeconds{0.001};
  WifiMode m_controlMode;
  std::ofstream m_trace;
};

} // namespace ns3

#endif // MY_PROJECT_LEARNING_RATE_MANAGER_H
