#include "rl-env.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ns3
{

static_assert (sizeof (AiConstantRateEnv) == 42,
               "Observation layout must match Python ctypes _pack_=1");
static_assert (sizeof (AiConstantRateAct) == 2,
               "Action layout must match Python ctypes _pack_=1");

NS_LOG_COMPONENT_DEFINE ("RLRateEnv");
NS_OBJECT_ENSURE_REGISTERED (RLRateEnv);

TypeId
RLRateEnv::GetTypeId ()
{
  static TypeId tid =
      TypeId ("ns3::rl-rateWifiManager")
          .SetParent<WifiRemoteStationManager> ()
          .SetGroupName ("Wifi")
          .AddConstructor<RLRateEnv> ()
          .AddAttribute ("DataMode",
                         "Initial Wifi mode used for DATA frames",
                         StringValue ("OfdmRate6Mbps"),
                         MakeWifiModeAccessor (&RLRateEnv::m_dataMode),
                         MakeWifiModeChecker ())
          .AddAttribute ("ControlMode",
                         "Wifi mode used for RTS frames",
                         StringValue ("OfdmRate6Mbps"),
                         MakeWifiModeAccessor (&RLRateEnv::m_ctlMode),
                         MakeWifiModeChecker ())
          .AddAttribute ("EnableAi",
                         "Exchange observations and actions through ns3-ai",
                         BooleanValue (false),
                         MakeBooleanAccessor (&RLRateEnv::m_enableAi),
                         MakeBooleanChecker ())
          .AddAttribute ("DecisionPerAmpdu",
                         "Select the next MCS after each completed PPDU/A-MPDU attempt",
                         BooleanValue (false),
                         MakeBooleanAccessor (&RLRateEnv::m_decisionPerAmpdu),
                         MakeBooleanChecker ())
          .AddAttribute ("MeasurementStart",
                         "Time when application payload accounting begins",
                         TimeValue (Seconds (0.5)),
                         MakeTimeAccessor (&RLRateEnv::m_measurementStart),
                         MakeTimeChecker ())
          .AddAttribute ("DecisionInterval",
                         "Deprecated compatibility attribute; Paper20 uses DecisionPacketWindow",
                         TimeValue (MilliSeconds (12)),
                         MakeTimeAccessor (&RLRateEnv::m_decisionInterval),
                         MakeTimeChecker ())
          .AddAttribute ("DecisionPacketWindow",
                         "Number of completed DATA MPDUs sent with one selected MCS",
                         UintegerValue (20),
                         MakeUintegerAccessor (&RLRateEnv::m_decisionPacketWindow),
                         MakeUintegerChecker<std::uint32_t> (1, 65535))
          .AddAttribute ("PayloadSize",
                         "Effective UDP payload bytes represented by one successful MPDU",
                         UintegerValue (1420),
                         MakeUintegerAccessor (&RLRateEnv::m_payloadSize),
                         MakeUintegerChecker<std::uint32_t> (1, 65535))
          .AddAttribute ("Ber",
                         "Target BER used to estimate the highest feasible MCS",
                         DoubleValue (1e-6),
                         MakeDoubleAccessor (&RLRateEnv::m_ber),
                         MakeDoubleChecker<double> (0.0, 1.0));
  return tid;
}

RLRateEnv::RLRateEnv ()
{
  NS_LOG_FUNCTION (this);
}

RLRateEnv::~RLRateEnv ()
{
  NS_LOG_FUNCTION (this);
}

void
RLRateEnv::DoInitialize ()
{
  NS_LOG_FUNCTION (this);

  // 固定速率自检不创建共享内存；只有正式运行Python代理时才启用
  if (m_enableAi)
    {
      auto interface = Ns3AiMsgInterface::Get ();
      interface->SetIsMemoryCreator (false);//共享内存由Python侧创建
      interface->SetUseVector (false);//Observation和Action都是单个结构体
      interface->SetHandleFinish (true);//仿真结束时通知Python退出
      m_msgInterface = interface->GetInterface<AiConstantRateEnv, AiConstantRateAct> ();
    }
}

void
RLRateEnv::DoDispose ()
{
  NS_LOG_FUNCTION (this);

  if (m_cwTraceConnected && m_txop)
    {
      m_txop->TraceDisconnectWithoutContext ("CwTrace",
                                             MakeCallback (&RLRateEnv::UpdateCw, this));
    }
  m_cwTraceConnected = false;
  m_txop = nullptr;
  m_mac = nullptr;
  m_station = nullptr;
  m_msgInterface = nullptr;

  WifiRemoteStationManager::DoDispose ();
}

void
RLRateEnv::SetupMac (const Ptr<WifiMac> mac)
{
  NS_LOG_FUNCTION (this << mac);
  // 若SetupMac被重复调用，先断开旧对象，避免重复统计同一次CW变化
  if (m_cwTraceConnected && m_txop)
    {
      m_txop->TraceDisconnectWithoutContext ("CwTrace",
                                             MakeCallback (&RLRateEnv::UpdateCw, this));
    }
  WifiRemoteStationManager::SetupMac (mac);
  m_mac = mac;

  UintegerValue maxAmpduSize;
  m_mac->GetAttribute ("BE_MaxAmpduSize", maxAmpduSize);
  m_ampduEnabled = maxAmpduSize.Get () > 0;

  PointerValue txopValue;
  mac->GetAttribute (mac->GetQosSupported () ? "BE_Txop" : "Txop", txopValue);
  m_txop = txopValue.GetObject ();
  m_cwTraceConnected =
      m_txop && m_txop->TraceConnectWithoutContext ("CwTrace",
                                                    MakeCallback (&RLRateEnv::UpdateCw, this));
}

WifiRemoteStation*
RLRateEnv::DoCreateStation () const
{
  NS_LOG_FUNCTION (this);
  return new WifiRemoteStation ();
}

void
RLRateEnv::UpdateCw (std::uint32_t oldCw, std::uint32_t newCw)
{
  NS_LOG_FUNCTION (this << oldCw << newCw);
  m_cw = static_cast<std::uint16_t> (std::min<std::uint32_t> (newCw, 65535));
}

void
RLRateEnv::RecordSuccessfulMpdus (std::uint16_t count)
{
  if (Simulator::Now () < m_measurementStart)
    {
      return;//不把关联/管理阶段反馈计入UDP有效负载吞吐量
  }
  m_successfulPayloadBytes += static_cast<std::uint64_t> (count) * m_payloadSize;
  m_windowHadAttempt = true;
}

void
RLRateEnv::RecordTransmissionAirtime (WifiRemoteStation* station, std::uint16_t count)
{
  if (Simulator::Now () < m_measurementStart || count == 0)
    {
      return;
    }

  WifiTxVector txVector = BuildDataTxVector (station, m_dataMode);
  const std::uint32_t payloadBytes = static_cast<std::uint32_t> (count) * m_payloadSize;
  const Time duration =
      GetPhy ()->CalculateTxDuration (payloadBytes, txVector, GetPhy ()->GetPhyBand ());
  m_transmissionAirtime += duration;
  m_currentPacketAirtime += duration;
  m_windowHadAttempt = true;
}

void
RLRateEnv::CompletePacket (bool success)
{
  if (Simulator::Now () < m_measurementStart || m_observationReady)
    {
      return;
    }

  if (success)
    {
      ++m_windowSuccessfulPackets;
    }
  else
    {
      ++m_windowFailedPackets;
    }
  ++m_completedPackets;
  m_currentPacketAirtime = Seconds (0);

  if (m_completedPackets >= m_decisionPacketWindow)
    {
      PrepareWindowObservation ();
    }
}

void
RLRateEnv::PrepareWindowObservation ()
{
  const double elapsedSeconds = m_actionStartSet
                                    ? (Simulator::Now () - m_actionStart).GetSeconds ()
                                    : 0.0;

  // Use elapsed simulation time so retries, contention, inter-frame spacing,
  // and ACK handling all reduce the application-payload goodput.
  m_throughput = elapsedSeconds > 0.0
                     ? static_cast<double> (m_successfulPayloadBytes) * 8.0 /
                           elapsedSeconds / 1e6
                     : 0.0;
  // Fixed-MCS references calibrated by calibrate_mcs_references.py at 1 m.
  // A-MPDU aggregation materially changes wall-clock goodput, so never reuse
  // the disabled-A-MPDU table when BE_MaxAmpduSize is enabled.
  static constexpr std::array<double, 8> dataRatesAmpduOff20Mhz = {
      5.7, 10.6, 14.8, 18.5, 24.6, 29.4, 31.7, 33.6};
  static constexpr std::array<double, 8> dataRatesAmpduOn20Mhz = {
      5.9, 12.0, 18.1, 24.2, 36.4, 48.7, 54.8, 60.9};
  const std::uint8_t mcs = GetCurrentMcs ();
  const auto& dataRates20Mhz = m_ampduEnabled ? dataRatesAmpduOn20Mhz
                                               : dataRatesAmpduOff20Mhz;
  const double efficiency = m_throughput / dataRates20Mhz[mcs];
  // Preserve the source reward shape and 20-packet scale, but base it on
  // wall-clock goodput rather than DATA-only PHY airtime.
  m_rawReward = static_cast<double> (m_completedPackets) *
                ((static_cast<double> (mcs) + 2.0) / 9.0) *
                efficiency * efficiency * efficiency;
  m_observationAggregateMpdus = static_cast<std::uint16_t> (m_completedPackets);
  m_observationSuccessfulMpdus = m_windowSuccessfulPackets;
  m_observationFailedMpdus = m_windowFailedPackets;
  m_observationReady = true;
}

void
RLRateEnv::FlushPendingWindow ()
{
  if (!m_enableAi || m_station == nullptr || m_observationReady ||
      m_completedPackets == 0)
    {
      return;
    }
  PrepareWindowObservation ();
  ExchangeWithAgent (m_station);
}

void
RLRateEnv::PrepareAmpduObservation (WifiRemoteStation* station,
                                    std::uint16_t nSuccessfulMpdus,
                                    std::uint16_t nFailedMpdus)
{
  if (!m_initialActionSelected || !m_actionStartSet
      || Simulator::Now () < m_measurementStart
      || nSuccessfulMpdus + nFailedMpdus == 0)
    {
      return;
    }

  const double elapsedSeconds = (Simulator::Now () - m_actionStart).GetSeconds ();
  m_throughput = elapsedSeconds > 0.0
                     ? static_cast<double> (nSuccessfulMpdus) * m_payloadSize * 8.0 /
                           elapsedSeconds / 1e6
                     : 0.0;
  static constexpr std::array<double, 8> dataRatesAmpduOff20Mhz = {
      5.7, 10.6, 14.8, 18.5, 24.6, 29.4, 31.7, 33.6};
  static constexpr std::array<double, 8> dataRatesAmpduOn20Mhz = {
      5.9, 12.0, 18.1, 24.2, 36.4, 48.7, 54.8, 60.9};
  const auto& dataRates20Mhz = m_ampduEnabled ? dataRatesAmpduOn20Mhz
                                               : dataRatesAmpduOff20Mhz;
  const std::uint8_t mcs = GetCurrentMcs ();
  const double efficiency = m_throughput / dataRates20Mhz[mcs];
  m_rawReward = static_cast<double> (nSuccessfulMpdus + nFailedMpdus) *
                ((static_cast<double> (mcs) + 2.0) / 9.0) *
                efficiency * efficiency * efficiency;
  m_observationSuccessfulMpdus = nSuccessfulMpdus;
  m_observationFailedMpdus = nFailedMpdus;
  m_observationAggregateMpdus = nSuccessfulMpdus + nFailedMpdus;
  m_observationReady = true;
  ExchangeWithAgent (station);
}

void
RLRateEnv::ResetWindow ()
{
  m_successfulPayloadBytes = 0;
  m_transmissionAirtime = Seconds (0);
  m_currentPacketAirtime = Seconds (0);
  m_rawReward = 0.0;
  m_completedPackets = 0;
  m_windowSuccessfulPackets = 0;
  m_windowFailedPackets = 0;
  m_observationAggregateMpdus = 0;
  m_observationSuccessfulMpdus = 0;
  m_observationFailedMpdus = 0;
  m_windowHadAttempt = false;
}

std::uint8_t
RLRateEnv::GetCurrentMcs () const
{
  if (m_dataMode.GetModulationClass () == WIFI_MOD_CLASS_HT)
    {
      return std::min<std::uint8_t> (m_dataMode.GetMcsValue (), 7);
    }
  return 0;
}

std::uint8_t
RLRateEnv::GetMaxMcsForSnr (WifiRemoteStation* station) const
{
  if (!std::isfinite (m_snr) || m_snr <= 0.0)
    {
      return 0;
    }

  std::uint8_t maxMcs = 0;
  for (std::uint8_t index = 0; index < GetNMcsSupported (station); ++index)
    {
      WifiMode mode = GetMcsSupported (station, index);
      if (mode.GetModulationClass () != WIFI_MOD_CLASS_HT || mode.GetMcsValue () > 7)
        {
          continue;//场景1的Action空间只定义单空间流MCS0-7
        }

      WifiTxVector txVector = BuildDataTxVector (station, mode);
      const double threshold = GetPhy ()->CalculateSnr (txVector, m_ber);
      if (threshold <= m_snr)
        {
          maxMcs = std::max (maxMcs, mode.GetMcsValue ());
        }
    }
  return maxMcs;
}

bool
RLRateEnv::ApplyMcs (WifiRemoteStation* station, std::uint8_t requestedMcs)
{
  requestedMcs = std::min<std::uint8_t> (requestedMcs, 7);
  for (std::uint8_t index = 0; index < GetNMcsSupported (station); ++index)
    {
      WifiMode mode = GetMcsSupported (station, index);
      if (mode.GetModulationClass () == WIFI_MOD_CLASS_HT
          && mode.GetMcsValue () == requestedMcs)
        {
          m_dataMode = mode;
          return true;
        }
    }

  NS_LOG_WARN ("Requested HtMcs" << +requestedMcs << " is not supported by the peer");
  return false;
}

void
RLRateEnv::ExchangeWithAgent (WifiRemoteStation* station)
{
  if (!m_enableAi || !m_observationReady || m_msgInterface == nullptr)
    {
      return;
    }

  // C++写Observation，Python读取
  m_msgInterface->CppSendBegin ();
  AiConstantRateEnv* env = m_msgInterface->GetCpp2PyStruct ();
  env->mcs = GetCurrentMcs ();
  env->max_mcs = GetMaxMcsForSnr (station);
  env->cw = m_cw;
  env->throughput = std::isfinite (m_throughput) ? m_throughput : 0.0;
  env->snr = std::isfinite (m_snr) ? m_snr : 0.0;
  env->raw_reward = std::isfinite (m_rawReward) ? m_rawReward : 0.0;
  env->simulation_time = Simulator::Now ().GetSeconds ();
  env->aggregate_mpdus = m_observationAggregateMpdus;
  env->successful_mpdus = m_observationSuccessfulMpdus;
  env->failed_mpdus = m_observationFailedMpdus;
  m_msgInterface->CppSendEnd ();

  // Python写Action，C++读取；该等待由ns3-ai信号量同步
  m_msgInterface->CppRecvBegin ();
  const AiConstantRateAct* action = m_msgInterface->GetPy2CppStruct ();
  const std::uint8_t requestedMcs = action->next_mcs;
  // nss字段为原协议保留；场景1的MCS0-7全部使用1条空间流
  m_msgInterface->CppRecvEnd ();

  ApplyMcs (station, requestedMcs);
  m_observationReady = false;
  if (m_initialActionSelected)
    {
      ResetWindow ();
    }
  m_initialActionSelected = true;
  m_actionStart = Simulator::Now ();
  m_actionStartSet = true;
}

void
RLRateEnv::DoReportRxOk (WifiRemoteStation* station,
                         double rxSnr,
                         WifiMode txMode)
{
  NS_LOG_FUNCTION (this << station << rxSnr << txMode);
  if (std::isfinite (rxSnr) && rxSnr > 0.0)
    {
      m_snr = rxSnr;//发送端最近收到的有效ACK/CTS方向SNR
    }
}

void
RLRateEnv::DoReportRtsFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
}

void
RLRateEnv::DoReportDataFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
  if (!m_ampduEnabled && Simulator::Now () >= m_measurementStart)
    {
      RecordTransmissionAirtime (station, 1);
    }
}

void
RLRateEnv::DoReportRtsOk (WifiRemoteStation* station,
                          double ctsSnr,
                          WifiMode ctsMode,
                          double rtsSnr)
{
  NS_LOG_FUNCTION (this << station << ctsSnr << ctsMode << rtsSnr);
  if (std::isfinite (ctsSnr) && ctsSnr > 0.0)
    {
      m_snr = ctsSnr;
    }
}

void
RLRateEnv::DoReportDataOk (WifiRemoteStation* station,
                           double ackSnr,
                           WifiMode ackMode,
                           double dataSnr,
                           std::uint16_t dataChannelWidth,
                           std::uint8_t dataNss)
{
  NS_LOG_FUNCTION (this << station << ackSnr << ackMode << dataSnr
                        << dataChannelWidth << +dataNss);
  if (std::isfinite (ackSnr) && ackSnr > 0.0)
    {
      m_snr = ackSnr;//固定底噪下，ACK SNR与论文使用的ACK RSS一一对应
    }
  if (!m_ampduEnabled)
    {
      //禁用A-MPDU时，一个普通DATA成功对应一个1420字节有效负载
      RecordSuccessfulMpdus (1);
      RecordTransmissionAirtime (station, 1);
      CompletePacket (true);
    }
}

void
RLRateEnv::DoReportAmpduTxStatus (WifiRemoteStation* station,
                                  std::uint16_t nSuccessfulMpdus,
                                  std::uint16_t nFailedMpdus,
                                  double rxSnr,
                                  double dataSnr,
                                  std::uint16_t dataChannelWidth,
                                  std::uint8_t dataNss)
{
  NS_LOG_FUNCTION (this << station << nSuccessfulMpdus << nFailedMpdus
                        << rxSnr << dataSnr << dataChannelWidth << +dataNss);
  if (std::isfinite (rxSnr) && rxSnr > 0.0)
    {
      m_snr = rxSnr;//丢失Block ACK时保留最近一次有效SNR
    }
  if (m_decisionPerAmpdu)
    {
      PrepareAmpduObservation (station, nSuccessfulMpdus, nFailedMpdus);
      return;
    }
  RecordTransmissionAirtime (station, nSuccessfulMpdus + nFailedMpdus);
  if (nSuccessfulMpdus > 0)
    {
      RecordSuccessfulMpdus (nSuccessfulMpdus);
      for (std::uint16_t index = 0; index < nSuccessfulMpdus; ++index)
        {
          CompletePacket (true);
        }
    }
  if (nFailedMpdus > 0)
    {
      m_windowHadAttempt = Simulator::Now () >= m_measurementStart;
      for (std::uint16_t index = 0; index < nFailedMpdus; ++index)
        {
          CompletePacket (false);
        }
    }
}

void
RLRateEnv::DoReportFinalRtsFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
}

void
RLRateEnv::DoReportFinalDataFailed (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
  if (m_ampduEnabled && m_decisionPerAmpdu
      && Simulator::Now () >= m_measurementStart)
    {
      // A completely failed A-MPDU may not produce DoReportAmpduTxStatus.
      // Emit a zero-throughput observation so the agent can lower the MCS
      // instead of becoming permanently stuck after losing the Block ACK.
      PrepareAmpduObservation (station, 0, 1);
      return;
    }
  if (!m_ampduEnabled && Simulator::Now () >= m_measurementStart)
    {
      //每次失败已由DoReportDataFailed计入，最终失败回调不重复累计PHY时间。
      m_windowHadAttempt = true;
      CompletePacket (false);
    }
}

WifiTxVector
RLRateEnv::BuildDataTxVector (WifiRemoteStation* station, WifiMode mode) const
{
  std::uint8_t nss = std::min (GetMaxNumberOfTransmitStreams (),
                               GetNumberOfSupportedStreams (station));
  if (mode.GetModulationClass () == WIFI_MOD_CLASS_HT)
    {
      nss = 1 + (mode.GetMcsValue () / 8);
    }

  return WifiTxVector (
      mode,
      GetDefaultTxPowerLevel (),
      GetPreambleForTransmission (mode.GetModulationClass (), GetShortPreambleEnabled ()),
      ConvertGuardIntervalToNanoSeconds (mode,
                                         GetShortGuardIntervalSupported (station),
                                         NanoSeconds (GetGuardInterval (station))),
      GetNumberOfAntennas (),
      nss,
      0,
      GetChannelWidthForTransmission (mode,
                                      GetPhy ()->GetChannelWidth (),
                                      GetChannelWidth (station)),
      GetAggregation (station));
}

WifiTxVector
RLRateEnv::DoGetDataTxVector (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
  // 第一个业务包前选择初始动作；后续由所选决策模式触发下一次交换。
  m_station = station;
  if (m_enableAi && Simulator::Now () >= m_measurementStart
      && (!m_initialActionSelected || m_observationReady))
    {
      if (!m_initialActionSelected)
        {
          m_throughput = 0.0;
          m_rawReward = 0.0;
          m_observationReady = true;
        }
      ExchangeWithAgent (station);
    }
  return BuildDataTxVector (station, m_dataMode);
}

WifiTxVector
RLRateEnv::DoGetRtsTxVector (WifiRemoteStation* station)
{
  NS_LOG_FUNCTION (this << station);
  return WifiTxVector (
      m_ctlMode,
      GetDefaultTxPowerLevel (),
      GetPreambleForTransmission (m_ctlMode.GetModulationClass (),
                                  GetShortPreambleEnabled ()),
      ConvertGuardIntervalToNanoSeconds (m_ctlMode,
                                         GetShortGuardIntervalSupported (station),
                                         NanoSeconds (GetGuardInterval (station))),
      1,
      1,
      0,
      GetChannelWidthForTransmission (m_ctlMode,
                                      GetPhy ()->GetChannelWidth (),
                                      GetChannelWidth (station)),
      false);//RTS控制帧不进行A-MPDU聚合
}

} // namespace ns3
