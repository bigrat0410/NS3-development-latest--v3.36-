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
#include <cmath>

namespace ns3
{

static_assert (sizeof (AiConstantRateEnv) == 28,
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
          .AddAttribute ("RewardDiscount",
                         "Discount applied to successive DATA-packet rewards in one decision window",
                         DoubleValue (0.99),
                         MakeDoubleAccessor (&RLRateEnv::m_rewardDiscount),
                         MakeDoubleChecker<double> (0.0, 1.0))
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

  const double packetSeconds = m_currentPacketAirtime.GetSeconds ();
  const double packetThroughputMbps =
      success && packetSeconds > 0.0
          ? static_cast<double> (m_payloadSize) * 8.0 / packetSeconds / 1e6
          : 0.0;
  m_windowReward += std::pow (m_rewardDiscount, m_completedPackets) *
                    GetPacketReward (packetThroughputMbps);
  ++m_completedPackets;
  m_currentPacketAirtime = Seconds (0);

  if (m_completedPackets >= m_decisionPacketWindow)
    {
      PrepareWindowObservation ();
    }
}

double
RLRateEnv::GetPacketReward (double packetThroughputMbps) const
{
  static const double referenceThroughputMbps[] = {5.4, 9.8, 13.6, 16.9,
                                                    22.2, 26.1, 28.1, 29.6};
  const std::uint8_t mcs = GetCurrentMcs ();
  const double ratio = std::min (packetThroughputMbps / referenceThroughputMbps[mcs], 1.0);
  return (static_cast<double> (mcs) / 7.0) * ratio * ratio * ratio;
}

void
RLRateEnv::PrepareWindowObservation ()
{
  const double transmissionSeconds = m_transmissionAirtime.GetSeconds ();

  //成功有效载荷除以当前20包窗口DATA尝试的PHY传输时间；失败重传时间也包含在分母中。
  m_throughput = transmissionSeconds > 0.0
                     ? static_cast<double> (m_successfulPayloadBytes) * 8.0 /
                           transmissionSeconds /
                           (1024.0 * 1024.0)
                     : 0.0;
  m_observationReady = true;
}

void
RLRateEnv::ResetWindow ()
{
  m_successfulPayloadBytes = 0;
  m_transmissionAirtime = Seconds (0);
  m_currentPacketAirtime = Seconds (0);
  m_windowReward = 0.0;
  m_completedPackets = 0;
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
  env->reward = std::isfinite (m_windowReward) ? m_windowReward : 0.0;
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
}

void
RLRateEnv::DoReportRxOk (WifiRemoteStation* station,
                         double rxSnr,
                         WifiMode txMode)
{
  NS_LOG_FUNCTION (this << station << rxSnr << txMode);
  m_snr = rxSnr;//发送端最近收到的ACK/CTS方向SNR
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
  m_snr = ctsSnr;
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
  m_snr = ackSnr;//固定底噪下，ACK SNR与论文使用的ACK RSS一一对应
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
  m_snr = rxSnr;//A-MPDU使用Block ACK，因此保存Block ACK的SNR
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
  // 第一个业务包前选择初始动作；之后仅在20个DATA包完成时交换下一动作。
  m_station = station;
  if (m_enableAi && Simulator::Now () >= m_measurementStart
      && (!m_initialActionSelected || m_observationReady))
    {
      if (!m_initialActionSelected)
        {
          m_throughput = 0.0;
          m_windowReward = 0.0;
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
