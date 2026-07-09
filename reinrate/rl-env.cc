#include "rl-env.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-tx-vector.h"
#include "ns3/wifi-psdu.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-utils.h"
#include "ns3/node.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-mac-header.h"
#include <ctime>
#include "ns3/config.h"
#include "ns3/simulator.h"
#include <cmath>
#include <string>


#define Min(a,b) ((a < b) ? a : b)
#define Max(a,b) ((a > b) ? a : b) 

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RLRateEnv");

NS_OBJECT_ENSURE_REGISTERED (RLRateEnv);

TypeId
RLRateEnv::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::rl-rateWifiManager")
    .SetParent<WifiRemoteStationManager> ()
    .SetGroupName ("Wifi")
    .AddConstructor<RLRateEnv> ()
    .AddAttribute ("DataMode", "The transmission mode to use for every data packet transmission",
                   StringValue ("OfdmRate6Mbps"),
                   MakeWifiModeAccessor (&RLRateEnv::m_dataMode),
                   MakeWifiModeChecker ())
    .AddAttribute ("ControlMode", "The transmission mode to use for every RTS packet transmission.",
                   StringValue ("OfdmRate6Mbps"),
                   MakeWifiModeAccessor (&RLRateEnv::m_ctlMode),
                   MakeWifiModeChecker ())
  ;
  return tid;
}

RLRateEnv::RLRateEnv (uint16_t id)
{
  auto interface = Ns3AiMsgInterface::Get ();
  interface->SetIsMemoryCreator (false);
  interface->SetUseVector (false);
  interface->SetHandleFinish (true);
  m_ns3ai_mod = interface->GetInterface<AiConstantRateEnv, AiConstantRateAct> ();
  m_startTime = Simulator::Now ();
  NS_LOG_FUNCTION (this);
}

RLRateEnv::~RLRateEnv ()
{
  NS_LOG_FUNCTION (this);
}

WifiRemoteStation *
RLRateEnv::DoCreateStation (void) const
{
  NS_LOG_FUNCTION (this);
  WifiRemoteStation *station = new WifiRemoteStation ();
  return station;
}

void
RLRateEnv::DoInitialize()
{
    BuildSnrThresholds();
}

void
RLRateEnv::BuildSnrThresholds()
{
  m_thresholds.clear();
  WifiTxVector txVector;
  uint8_t nss = 1;
  for (const auto& mode : GetPhy()->GetModeList())
    {
      txVector.SetChannelWidth(20);
      txVector.SetNss(nss);
      txVector.SetMode(mode);
      AddSnrThreshold(txVector, GetPhy()->CalculateSnr(txVector, m_ber));
    }
  if (GetHtSupported())
    {
      for (const auto& mode : GetPhy()->GetMcsList())
        {
          for (uint16_t width = 20; width <= GetPhy()->GetChannelWidth(); width *= 2)
            {
              txVector.SetChannelWidth(width);
              uint16_t guardInterval = GetShortGuardIntervalSupported() ? 400 : 800;
              txVector.SetGuardInterval(guardInterval);
              nss = (mode.GetMcsValue() / 8) + 1;
              txVector.SetNss(nss);
              txVector.SetMode(mode);
              AddSnrThreshold(txVector, GetPhy()->CalculateSnr(txVector, m_ber));
            }
        }
    }
}

double
RLRateEnv::GetSnrThreshold(WifiTxVector txVector) const
{
  auto it = std::find_if(m_thresholds.begin(), m_thresholds.end(),
      [&txVector](const std::pair<double, WifiTxVector>& p) {
        return txVector.GetMode() == p.second.GetMode()
            && txVector.GetNss() == p.second.GetNss()
            && txVector.GetChannelWidth() == p.second.GetChannelWidth();
      });
  NS_ASSERT_MSG(it != m_thresholds.end(), "SNR threshold not found");
  return it->first;
}

void
RLRateEnv::AddSnrThreshold(WifiTxVector txVector, double snr)
{
  m_thresholds.push_back(std::make_pair(snr, txVector));
}

double 
RLRateEnv::CalculateFER(const std::unordered_set<int>& A, const std::unordered_set<int>& B) {
    std::vector<int> vA(A.begin(), A.end());
    std::vector<int> vB(B.begin(), B.end());

    std::sort(vA.begin(), vA.end());
    std::sort(vB.begin(), vB.end());

    std::vector<int> vC;
    std::set_intersection(vA.begin(), vA.end(), vB.begin(), vB.end(), std::back_inserter(vC));

    std::unordered_set<int> C(vC.begin(), vC.end());

    return 1-float(Max(A.size(), B.size()) - C.size())/float(Max(A.size(), B.size()));
}

void 
RLRateEnv::TraceTxOk (Ptr<const Packet> packet)
{
  packetsSentPerStep.insert(packet->GetUid());
  m_txPerStep++;
  m_txTotal_ap++;
  m_txPerMetreRead_sta1++;
  if (m_txPerStep == 1)
  {
    Time cur_time = Simulator::Now();
    double elapsed = cur_time.GetSeconds() - m_startTime.GetSeconds();
    m_throughput_sta1 = elapsed > 0
      ? (m_txPerMetreRead_sta1 *1420 * 8.0) / (1024*1024) / elapsed
      : 0.0;
    m_rxPerMetreRead_sta1 = 0;
    m_txPerMetreRead_sta1 = 0;
    m_startTime = Simulator::Now ();
    m_txPerStep = 0;
    m_rxPerStep = 0;
    m_txPhyPerStep = 0;
    packetsSentPerStep.clear();
    packetsReceivedPerStep.clear();
    m_readyToUpdate = true;
  }
}

void
RLRateEnv::TrackRxOk (Ptr<const Packet> packet)
{
  packetsReceivedPerStep.insert(packet->GetUid());
  m_rxTotal_ap++;
  m_rxPerMetreRead_sta1++;
  m_rxPerStep++;
}

void
RLRateEnv::TrackPhyTxOk (Ptr<const Packet> packet, double txPowerDbm)
{
  m_txPhyPerStep++;
}
void
RLRateEnv::TrackCw (uint32_t cw, uint32_t slot)
{
  if (cw != 15)
  {
    m_cw = cw;
  }
}

bool isMacTxTraced = false;
bool isMacRxTraced = false;
bool isPhyTxTraced = false;
bool isCwTraced = false;
bool isMpduTraced = false;
void
RLRateEnv::DoReportRxOk (WifiRemoteStation *station,
                                       double rxSnr, WifiMode txMode)
{
  m_snr = rxSnr;
  if (!isCwTraced)
  {
    std::string txop = GetMac()->GetQosSupported()
          ? "BE_Txop"
          : "Txop";
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::WifiMac/" + txop + "/CwTrace", MakeCallback (&RLRateEnv::TrackCw, this));
    std::cout<<"create cw trace at mac"<<GetMac()->GetAddress()<<std::endl;
    isCwTraced = true;
  }
  
  if (!isMacTxTraced)
  {
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback (&RLRateEnv::TraceTxOk, this));
    std::cout<<"create phytx trace at mac"<<GetMac()->GetAddress()<<std::endl;
    isMacTxTraced = true;
  }
  if (!isMacRxTraced)
  {
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacRx", MakeCallback (&RLRateEnv::TrackRxOk, this));
    std::cout<<"create macrx trace at mac"<<GetMac()->GetAddress()<<std::endl;
    isMacRxTraced = true;
  }
  if (!isPhyTxTraced)
  {
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxBegin", MakeCallback (&RLRateEnv::TrackPhyTxOk, this));
    isPhyTxTraced = true;
  }
  
  NS_LOG_FUNCTION (this << station << rxSnr << txMode);
}

void
RLRateEnv::DoReportRtsFailed (WifiRemoteStation *station)
{
  NS_LOG_FUNCTION (this << station);
}

void
RLRateEnv::DoReportDataFailed (WifiRemoteStation *station)
{
  NS_LOG_FUNCTION (this << station);
  m_txFail_ap++;
}


void
RLRateEnv::DoReportAmpduTxStatus(WifiRemoteStation* st,
                                        uint16_t nSuccessfulMpdus,
                                        uint16_t nFailedMpdus,
                                        double rxSnr,
                                        double dataSnr,
                                        uint16_t dataChannelWidth,
                                        uint8_t dataNss)
{
  m_snr = dataSnr;
}

void
RLRateEnv::DoReportRtsOk (WifiRemoteStation *st,
                                        double ctsSnr, WifiMode ctsMode, double rtsSnr)
{
  NS_LOG_FUNCTION (this << st << ctsSnr << ctsMode << rtsSnr);
}

void
RLRateEnv::DoReportDataOk (WifiRemoteStation *st, double ackSnr, WifiMode ackMode,
                                         double dataSnr, uint16_t dataChannelWidth, uint8_t dataNss)
{
  NS_LOG_FUNCTION (this << st << ackSnr << ackMode << dataSnr << dataChannelWidth << +dataNss);
}

void
RLRateEnv::DoReportFinalRtsFailed (WifiRemoteStation *station)
{
  NS_LOG_FUNCTION (this << station);
}

void
RLRateEnv::DoReportFinalDataFailed (WifiRemoteStation *station)
{
  NS_LOG_FUNCTION (this << station);
}


WifiMode
RLRateEnv::DoGetDataMode (WifiRemoteStation *st, uint32_t size)
{
  std::cout<<"DoGetDataMode"<<"packet sent: "<<m_txTotal_ap<<"failed: "<<m_txFail_ap<<std::endl;
  return m_dataMode;
}

bool isMacSetup = false;
WifiTxVector
RLRateEnv::DoGetDataTxVector (WifiRemoteStation *st)
{
  NS_LOG_FUNCTION (this << st);
  m_wifiMacAddress = GetMac()->GetAddress ();
  
  if (m_readyToUpdate)
  {
    double maxThreshold = 0.0;
    WifiMode maxMode = GetDefaultModeForSta (st);
    WifiTxVector txVector;
    txVector.SetChannelWidth (20);
    for (uint32_t i = 0; i < GetNMcsSupported(st); i++)
      {
        WifiMode mode = GetMcsSupported (st, i);
        txVector.SetMode (mode);
        double threshold = GetSnrThreshold (txVector);
        if (threshold > maxThreshold
            && threshold < m_snr)
          {
            maxThreshold = threshold;
            maxMode = mode;
          }
      }

    m_ns3ai_mod->CppSendBegin ();
    auto env = m_ns3ai_mod->GetCpp2PyStruct ();
    if (m_dataMode.GetModulationClass () == WIFI_MOD_CLASS_HT)
      env->mcs = m_dataMode.GetMcsValue ();
    env->max_mcs = maxMode.GetMcsValue ();
    env->cw = m_cw;
    env->throughput = std::isfinite(m_throughput_sta1) ? m_throughput_sta1 : 0.0;
    env->snr = std::isfinite(m_snr) ? m_snr : 0.0;
    m_ns3ai_mod->CppSendEnd ();
    
    m_ns3ai_mod->CppRecvBegin ();
    auto act = m_ns3ai_mod->GetPy2CppStruct ();
    m_next_mcs = act->next_mcs;
    m_ns3ai_mod->CppRecvEnd ();
    m_readyToUpdate = true;
    if (m_next_mcs > 7)
    {
      m_next_mcs = 7;
    }
    
    m_dataMode = GetMcsSupported (st, m_next_mcs);
  }
  return WifiTxVector (
      m_dataMode,
      GetDefaultTxPowerLevel (),
      GetPreambleForTransmission (
          m_dataMode.GetModulationClass (),
          GetShortPreambleEnabled ()),
      ConvertGuardIntervalToNanoSeconds (
          m_dataMode,
          GetShortGuardIntervalSupported (st),
          NanoSeconds (GetGuardInterval (st))),
      GetNumberOfAntennas (),
      m_nss,
      0,
      GetChannelWidth(st),
      GetAggregation (st));
}

WifiTxVector
RLRateEnv::DoGetRtsTxVector (WifiRemoteStation *st)
{
  NS_LOG_FUNCTION (this << st);
  return WifiTxVector (
      m_ctlMode,
      GetDefaultTxPowerLevel (),
      GetPreambleForTransmission (
          m_ctlMode.GetModulationClass (),
          GetShortPreambleEnabled ()),
      800,
      1,
      1,
      0,
      GetChannelWidth(st),
      GetAggregation (st));
}


} //namespace ns3
