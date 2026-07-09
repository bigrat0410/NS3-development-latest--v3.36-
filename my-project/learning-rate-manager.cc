/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "learning-rate-manager.h"

#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"
#include "ns3/wifi-utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("MyProjectLearningRateManager");
NS_OBJECT_ENSURE_REGISTERED (MyProjectLearningRateManager);

namespace
{
double
Clamp01 (double value)
{
  // 奖励函数里经常需要把比值限制在 [0, 1]，集中成一个小工具避免重复写边界判断。
  return std::max (0.0, std::min (1.0, value));
}
} // namespace

TypeId
MyProjectLearningRateManager::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::MyProjectLearningRateManager")
          .SetParent<WifiRemoteStationManager> ()
          .SetGroupName ("Wifi")
          .AddConstructor<MyProjectLearningRateManager> ()
          .AddAttribute ("Algorithm",
                         "Learning algorithm: mab or dqn.",
                         StringValue ("mab"),
                         MakeStringAccessor (&MyProjectLearningRateManager::m_algorithm),
                         MakeStringChecker ())
          .AddAttribute ("ControlMode",
                         "Mode used for RTS/control transmissions.",
                         StringValue ("HtMcs0"),
                         MakeWifiModeAccessor (&MyProjectLearningRateManager::m_controlMode),
                         MakeWifiModeChecker ())
          .AddAttribute ("TraceFile",
                         "Optional CSV file for learning feedback samples.",
                         StringValue (""),
                         MakeStringAccessor (&MyProjectLearningRateManager::m_traceFile),
                         MakeStringChecker ())
          .AddAttribute ("DecisionInterval",
                         "Seconds between Python/ns3-ai rate decisions.",
                         DoubleValue (0.001),
                         MakeDoubleAccessor (&MyProjectLearningRateManager::m_decisionIntervalSeconds),
                         MakeDoubleChecker<double> (0.0));
  return tid;
}

MyProjectLearningRateManager::MyProjectLearningRateManager ()
{
  // ns3-ai 的共享内存由 Python 进程创建；C++ 侧只连接并参与同步。
  // SetHandleFinish(true) 让 Python 能感知仿真结束，避免子进程一直阻塞在收发锁上。
  auto interface = Ns3AiMsgInterface::Get ();
  interface->SetIsMemoryCreator (false);
  interface->SetUseVector (false);
  interface->SetHandleFinish (true);
}

MyProjectLearningRateManager::~MyProjectLearningRateManager () = default;

WifiRemoteStation*
MyProjectLearningRateManager::DoCreateStation () const
{
  return new LearningWifiRemoteStation ();
}

WifiTxVector
MyProjectLearningRateManager::DoGetDataTxVector (WifiRemoteStation* station)
{
  LearningWifiRemoteStation* learningStation = static_cast<LearningWifiRemoteStation*> (station);
  std::vector<WifiMode> modes = GetHtModes (station);
  if (modes.empty ())
    {
      // 极少数配置下站点没有暴露 HT MCS，此时退回 ns-3 默认模式，保证仿真不中断。
      return BuildDataTxVector (station, GetDefaultModeForSta (station));
    }

  const uint32_t numActions = static_cast<uint32_t> (modes.size ());
  uint32_t action = learningStation->lastAction;
  const double bestRateMbps =
      static_cast<double> (modes.back ().GetDataRate (BuildDataTxVector (station, modes.back ()))) / 1e6;
  const double now = Simulator::Now ().GetSeconds ();
  if ((UseMab () || UseDqn ()) &&
      (now >= learningStation->nextDecisionTime || learningStation->lastPhyRateMbps <= 0.0))
    {
      // MAB/DQN 模式下，C++ 把最近的链路状态发给 Python，并等待 Python 返回下一档速率。
      // decisionInterval 控制同步频率；首次发送 lastPhyRateMbps 为 0，所以必须立即决策一次。
      action = RequestPythonAction (learningStation, modes, bestRateMbps);
      learningStation->nextDecisionTime = now + m_decisionIntervalSeconds;
    }
  else if (!UseMab () && !UseDqn ())
    {
      // 非学习模式只作为简单兜底：逐步升档。真正对比基线在主程序里使用 MinstrelHt。
      action = std::min<uint32_t> (learningStation->lastAction + 1, numActions);
    }

  // Python 返回值可能因为探索或 bug 越界，应用前必须裁剪到 ns-3 当前支持的 MCS 范围。
  action = std::max<uint32_t> (1, std::min<uint32_t> (action, numActions));
  learningStation->lastAction = action;
  learningStation->lastAppliedAction = action;
  learningStation->lastNumActions = numActions;
  learningStation->currentAction = action;
  WifiTxVector txVector = BuildDataTxVector (station, modes[action - 1]);
  // 缓存本次实际发送速率。后续 ACK/失败回调只带反馈，不会重新告诉我们刚才用了哪个模式。
  learningStation->lastPhyRateMbps = static_cast<double> (modes[action - 1].GetDataRate (txVector)) / 1e6;
  learningStation->bestPhyRateMbps = bestRateMbps;
  learningStation->currentMcs = modes[action - 1].GetMcsValue ();
  return txVector;
}

WifiTxVector
MyProjectLearningRateManager::DoGetRtsTxVector (WifiRemoteStation* station)
{
  return BuildControlTxVector (station);
}

void
MyProjectLearningRateManager::DoReportRxOk (WifiRemoteStation* station,
                                            double rxSnr,
                                            WifiMode txMode)
{
  // 普通接收成功回调只更新 SNR；真正的速率学习反馈在 DATA/AMPDU 发送结果回调里处理。
  LearningWifiRemoteStation* learningStation = static_cast<LearningWifiRemoteStation*> (station);
  learningStation->lastSnrDb = LinearSnrToDb (rxSnr);
  (void) txMode;
}

void
MyProjectLearningRateManager::DoReportRtsFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
}

void
MyProjectLearningRateManager::DoReportDataFailed (WifiRemoteStation* station)
{
  UpdateFromFeedback (static_cast<LearningWifiRemoteStation*> (station), false, 0.0);
}

void
MyProjectLearningRateManager::DoReportRtsOk (WifiRemoteStation* station,
                                             double ctsSnr,
                                             WifiMode ctsMode,
                                             double rtsSnr)
{
  LearningWifiRemoteStation* learningStation = static_cast<LearningWifiRemoteStation*> (station);
  learningStation->lastSnrDb = LinearSnrToDb (rtsSnr > 0.0 ? rtsSnr : ctsSnr);
  (void) ctsMode;
}

void
MyProjectLearningRateManager::DoReportDataOk (WifiRemoteStation* station,
                                              double ackSnr,
                                              WifiMode ackMode,
                                              double dataSnr,
                                              uint16_t dataChannelWidth,
                                              uint8_t dataNss)
{
  // ACK 成功说明刚才的数据速率在当前信道下可用，success=true 会进入奖励函数。
  UpdateFromFeedback (static_cast<LearningWifiRemoteStation*> (station), true, dataSnr > 0.0 ? dataSnr : ackSnr);
  (void) ackMode;
  (void) dataChannelWidth;
  (void) dataNss;
}

void
MyProjectLearningRateManager::DoReportAmpduTxStatus (WifiRemoteStation* station,
                                                     uint16_t nSuccessfulMpdus,
                                                     uint16_t nFailedMpdus,
                                                     double rxSnr,
                                                     double dataSnr,
                                                     uint16_t dataChannelWidth,
                                                     uint8_t dataNss)
{
  // A-MPDU 是聚合发送：只要有 MPDU 成功，就把这次视作正反馈。
  // 更细粒度的成功比例可以继续扩展，但这里保持状态空间简单。
  const bool success = nSuccessfulMpdus > 0;
  UpdateFromFeedback (static_cast<LearningWifiRemoteStation*> (station), success, dataSnr > 0.0 ? dataSnr : rxSnr);
  (void) nFailedMpdus;
  (void) dataChannelWidth;
  (void) dataNss;
}

void
MyProjectLearningRateManager::DoReportFinalRtsFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
}

void
MyProjectLearningRateManager::DoReportFinalDataFailed (WifiRemoteStation* station)
{
  UpdateFromFeedback (static_cast<LearningWifiRemoteStation*> (station), false, 0.0);
}

std::vector<WifiMode>
MyProjectLearningRateManager::GetHtModes (WifiRemoteStation* station) const
{
  // 只保留单流 HT MCS0..MCS7，让 action 空间固定且容易和 Python 的 8 个动作对应。
  std::vector<WifiMode> modes;
  const uint8_t nMcs = GetNMcsSupported (station);
  for (uint8_t i = 0; i < nMcs; ++i)
    {
      WifiMode mode = GetMcsSupported (station, i);
      if (mode.GetModulationClass () == WIFI_MOD_CLASS_HT && mode.GetMcsValue () < 8)
        {
          modes.push_back (mode);
        }
    }
  std::sort (modes.begin (), modes.end (), [] (const WifiMode& lhs, const WifiMode& rhs) {
    return lhs.GetMcsValue () < rhs.GetMcsValue ();
  });
  return modes;
}

WifiTxVector
MyProjectLearningRateManager::BuildDataTxVector (WifiRemoteStation* station, WifiMode mode) const
{
  // 数据帧使用学习器选出的 HT MCS，同时继承当前 PHY 的功率、保护间隔、信道宽度等配置。
  uint8_t nss = 1;
  if (mode.GetModulationClass () == WIFI_MOD_CLASS_HT)
    {
      nss = 1 + (mode.GetMcsValue () / 8);
    }
  nss = std::min<uint8_t> (nss, std::min (GetMaxNumberOfTransmitStreams (), GetNumberOfSupportedStreams (station)));
  const uint16_t guardInterval = ConvertGuardIntervalToNanoSeconds (
      mode,
      GetShortGuardIntervalSupported (station),
      NanoSeconds (GetGuardInterval (station)));
  return WifiTxVector (mode,
                       GetDefaultTxPowerLevel (),
                       GetPreambleForTransmission (mode.GetModulationClass (), GetShortPreambleEnabled ()),
                       guardInterval,
                       GetNumberOfAntennas (),
                       nss,
                       0,
                       GetChannelWidthForTransmission (mode, GetPhy ()->GetChannelWidth (), GetChannelWidth (station)),
                       GetAggregation (station));
}

WifiTxVector
MyProjectLearningRateManager::BuildControlTxVector (WifiRemoteStation* station) const
{
  // RTS/CTS/ACK 等控制帧保持在稳定的低速率 HtMcs0，避免控制帧速率本身干扰数据速率学习。
  return WifiTxVector (m_controlMode,
                       GetDefaultTxPowerLevel (),
                       GetPreambleForTransmission (m_controlMode.GetModulationClass (), GetShortPreambleEnabled ()),
                       ConvertGuardIntervalToNanoSeconds (
                           m_controlMode,
                           GetShortGuardIntervalSupported (station),
                           NanoSeconds (GetGuardInterval (station))),
                       1,
                       1,
                       0,
                       GetChannelWidthForTransmission (m_controlMode, GetPhy ()->GetChannelWidth (), GetChannelWidth (station)),
                       GetAggregation (station));
}

void
MyProjectLearningRateManager::UpdateFromFeedback (LearningWifiRemoteStation* station,
                                                  bool success,
                                                  double snrLinear)
{
  if (snrLinear > 0.0)
    {
      station->lastSnrDb = LinearSnrToDb (snrLinear);
    }
  if (station->lastPhyRateMbps <= 0.0)
    {
      // 还没有记录过数据发送速率时，无法判断“这个 action 的收益”，直接等待下一次发送。
      return;
    }
  if (success)
    {
      ++station->successes;
      ++station->consecutiveSuccesses;
      station->consecutiveFailures = 0;
    }
  else
    {
      ++station->failures;
      station->consecutiveSuccesses = 0;
      ++station->consecutiveFailures;
    }
  station->snrWindow.push_back (station->lastSnrDb);
  if (station->snrWindow.size () > 30)
    {
      station->snrWindow.pop_front ();
    }
  station->ackWindow.push_back (success);
  if (station->ackWindow.size () > 60)
    {
      station->ackWindow.pop_front ();
    }

  const uint32_t numActions = std::max<uint32_t> (1, station->lastNumActions);
  const uint32_t action = std::max<uint32_t> (1, station->lastAction);
  const uint32_t mcsValue = station->currentMcs;
  const double bestRateMbps = std::max (station->bestPhyRateMbps, station->lastPhyRateMbps);
  // reward 只在 C++ 计算，因为 C++ 最清楚本次是否真的发送成功、选中速率是多少。
  // Python 只把 reward 当成强化学习反馈，不需要知道 ns-3 内部回调细节。
  const double reward = CalculateReward (station, success, action, numActions, station->lastPhyRateMbps, bestRateMbps);
  station->lastReward = reward;

  station->previousMeanSnrDb = MeanSnr (station);
  station->previousStdSnrDb = StdSnr (station, station->previousMeanSnrDb);
  station->previousAckRatio = AckRatio (station);
  TraceFeedback (station, success, action, mcsValue, reward);
}

void
MyProjectLearningRateManager::TraceFeedback (const LearningWifiRemoteStation* station,
                                             bool success,
                                             uint32_t action,
                                             uint32_t mcsValue,
                                             double reward)
{
  if (m_traceFile.empty ())
    {
      return;
    }
  if (!m_trace.is_open ())
    {
      m_trace.open (m_traceFile);
      m_trace << "time_s,algorithm,success,action,mcs,phy_rate_mbps,snr_db,mean_snr_db,"
                 "ack_ratio,reward,epsilon,consecutive_successes,requested_action,applied_action\n";
    }
  m_trace << Simulator::Now ().GetSeconds () << ',' << m_algorithm << ',' << (success ? 1 : 0)
          << ',' << action << ',' << mcsValue << ',' << station->lastPhyRateMbps << ','
          << station->lastSnrDb << ',' << station->previousMeanSnrDb << ','
          << station->previousAckRatio << ',' << reward << ',' << station->lastEpsilon << ','
          << station->consecutiveSuccesses << ',' << station->lastRequestedAction << ','
          << station->lastAppliedAction << '\n';
}

uint32_t
MyProjectLearningRateManager::RequestPythonAction (LearningWifiRemoteStation* station,
                                                   const std::vector<WifiMode>& modes,
                                                   double bestRateMbps)
{
  MyProjectRateControlInterface* msgInterface =
      Ns3AiMsgInterface::Get ()->GetInterface<MyProjectRateControlEnv, MyProjectRateControlAct> ();

  const uint32_t numActions = static_cast<uint32_t> (modes.size ());
  // CppSendBegin/End 之间写入 C++ -> Python 结构体；Python 端会在 PyRecvBegin 后读到同一块共享内存。
  msgInterface->CppSendBegin ();
  MyProjectRateControlEnv* env = msgInterface->GetCpp2PyStruct ();
  env->step = station->decisionStep++;
  env->numActions = numActions;
  env->lastAction = std::max<uint32_t> (1, station->lastAction);
  env->lastMcs = station->currentMcs;
  env->lastSuccess = station->consecutiveFailures == 0 ? 1 : 0;
  env->consecutiveFailures = station->consecutiveFailures;
  env->snrDb = station->lastSnrDb;
  env->meanSnrDb = station->previousMeanSnrDb;
  env->stdSnrDb = station->previousStdSnrDb;
  env->ackRatio = station->previousAckRatio;
  env->selectedRateMbps = station->lastPhyRateMbps;
  env->bestRateMbps = bestRateMbps;
  env->reward = station->lastReward;
  msgInterface->CppSendEnd ();

  // CppRecvBegin 会等待 Python 填好动作。这里是同步调用，所以 Python 进程必须和 ns-3 一起运行。
  msgInterface->CppRecvBegin ();
  const MyProjectRateControlAct* act = msgInterface->GetPy2CppStruct ();
  const uint32_t action =
      std::max<uint32_t> (1, static_cast<uint32_t> (std::lround (act->nextAction)));
  station->lastEpsilon = act->epsilon;
  station->lastRequestedAction = action;
  msgInterface->CppRecvEnd ();

  return std::min<uint32_t> (action, numActions);
}

double
MyProjectLearningRateManager::CalculateReward (const LearningWifiRemoteStation* station,
                                               bool success,
                                               uint32_t action,
                                               uint32_t numActions,
                                               double selectedRateMbps,
                                               double bestRateMbps) const
{
  // 奖励目标：成功时鼓励接近最高 PHY 速率，失败或极低效率时给明显负反馈。
  // sigmoid 让 85% 以上效率的动作收益迅速变高，同时不会让最高档奖励无限放大。
  double efficiency = success && bestRateMbps > 0.0 ? selectedRateMbps / bestRateMbps : 0.0;
  efficiency = Clamp01 (efficiency);
  const double rewardBase = 10.0 / (1.0 + std::exp (-12.0 * (efficiency - 0.85)));
  // 轻微惩罚大幅跳档，减少在临界 SNR 附近频繁上下抖动。
  const double penaltySmooth =
      0.5 * (std::abs (static_cast<int32_t> (action) - static_cast<int32_t> (station->lastAction)) /
             static_cast<double> (std::max<uint32_t> (1, numActions)));
  if (efficiency < 0.05)
    {
      return -1.5;
    }
  return rewardBase - penaltySmooth;
}

double
MyProjectLearningRateManager::LinearSnrToDb (double snrLinear) const
{
  return 10.0 * std::log10 (std::max (snrLinear, 1e-12));
}

double
MyProjectLearningRateManager::MeanSnr (const LearningWifiRemoteStation* station) const
{
  if (station->snrWindow.empty ())
    {
      return station->lastSnrDb;
    }
  const double sum = std::accumulate (station->snrWindow.begin (), station->snrWindow.end (), 0.0);
  return sum / static_cast<double> (station->snrWindow.size ());
}

double
MyProjectLearningRateManager::StdSnr (const LearningWifiRemoteStation* station, double mean) const
{
  if (station->snrWindow.empty ())
    {
      return 0.0;
    }
  double squareSum = 0.0;
  for (double snr : station->snrWindow)
    {
      const double diff = snr - mean;
      squareSum += diff * diff;
    }
  return std::sqrt (squareSum / static_cast<double> (station->snrWindow.size ()));
}

double
MyProjectLearningRateManager::AckRatio (const LearningWifiRemoteStation* station) const
{
  if (station->ackWindow.empty ())
    {
      return 1.0;
    }
  const uint32_t successes =
      static_cast<uint32_t> (std::count (station->ackWindow.begin (), station->ackWindow.end (), true));
  return static_cast<double> (successes) / static_cast<double> (station->ackWindow.size ());
}

bool
MyProjectLearningRateManager::UseDqn () const
{
  return m_algorithm == "dqn" || m_algorithm == "DQN";
}

bool
MyProjectLearningRateManager::UseMab () const
{
  return m_algorithm == "mab" || m_algorithm == "MAB";
}

} // namespace ns3
