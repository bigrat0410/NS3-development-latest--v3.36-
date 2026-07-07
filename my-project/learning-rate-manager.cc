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
      action = RequestPythonAction (learningStation, modes, bestRateMbps);
      learningStation->nextDecisionTime = now + m_decisionIntervalSeconds;
    }
  else if (!UseMab () && !UseDqn ())
    {
      action = std::min<uint32_t> (learningStation->lastAction + 1, numActions);
    }

  action = std::max<uint32_t> (1, std::min<uint32_t> (action, numActions));
  learningStation->lastAction = action;
  learningStation->lastAppliedAction = action;
  learningStation->lastNumActions = numActions;
  learningStation->currentAction = action;
  WifiTxVector txVector = BuildDataTxVector (station, modes[action - 1]);
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
  double efficiency = success && bestRateMbps > 0.0 ? selectedRateMbps / bestRateMbps : 0.0;
  efficiency = Clamp01 (efficiency);
  const double rewardBase = 10.0 / (1.0 + std::exp (-12.0 * (efficiency - 0.85)));
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
