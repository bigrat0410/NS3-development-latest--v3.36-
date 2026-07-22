/*
 * 【中文注释总览】
 * 本文件实现 ns3::rl-rateWifiManager，是该项目最核心的 C++ 侧速率自适应模块。
 * 它继承/接入 ns-3 WiFi RemoteStationManager 体系，在 802.11 MAC 层每次选择 DataTxVector 时，
 * 将当前链路状态组织为 Observation，交给 Python DRL 智能体，并读取 Python 返回的 Action 更新 MCS。
 *
 * 关注点：
 * 1. Observation：mcs、cw、throughput、snr 等字段在 DoGetDataTxVector / MetreRead / Trace 回调中更新。
 * 2. Action：Python 返回 next_mcs，C++ 将其转换为 HtMcsX WifiMode 并写入 WifiTxVector。
 * 3. Reward：C++ 不直接给奖励；它提供吞吐、SNR、当前 MCS 等环境反馈，Reward 在 ns3ai.py/model.py 中计算。
 * 4. 802.11 MAC/PHY：该类挂在 WiFi RemoteStationManager 上，核心切入点是 DoGetDataTxVector，
 *    成功/失败/接收/监听通过 TrackTxOk、TrackRxOk、TrackMonitorSniffRx 等 Trace 回调更新统计。
 */
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
#include <string>


#define Min(a,b) ((a < b) ? a : b)
#define Max(a,b) ((a > b) ? a : b) 

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RLRateEnv");

// 【ns-3 类型注册】将 rl-rateWifiManager 注册进 ns-3 TypeId 系统，使 rate-control.cc 可通过字符串创建该速率控制器。
NS_OBJECT_ENSURE_REGISTERED (RLRateEnv);

// 【类型与属性】这里暴露 ns-3 属性，允许脚本配置 DataMode、ControlMode、BER 等参数。
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
// 【通信桥梁】m_ns3ai_mod 指向 ns3-ai 共享环境；C++ 写 env、读 act，Python 读 env、写 act。
  m_ns3ai_mod = new Ns3AIRL<AiConstantRateEnv, AiConstantRateAct> (id);
  m_ns3ai_mod->SetCond (2, 0);
  NS_LOG_FUNCTION (this);
}

RLRateEnv::~RLRateEnv ()
{
  delete m_ns3ai_mod;
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
// 【初始化】建立 ns3-ai 通信对象、初始化 MCS/阈值/定时统计，是 C++ 环境启动点。
RLRateEnv::DoInitialize()
{
// 【速率候选集】构建各 MCS 对应的 SNR 阈值表，为速率选择提供可行性参考。
    BuildSnrThresholds();
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
// 【发包成功/尝试统计】记录发送侧事件，配合接收集合估计 Frame Error Rate。
RLRateEnv::TraceTxOk (Ptr<const Packet> packet)
{
  if (m_wifiMacAddress == Mac48Address("00:00:00:00:00:04"))
  { 
    packetsSentPerStep.insert(packet->GetUid());
    m_txPerStep++;
    m_txTotal_ap++;
// 【状态采样周期】周期性汇总吞吐、发包/收包/SNR/CW 等观测量，并准备交给 Python。
    m_txPerMetreRead_sta1++;
    if (m_txPerStep == 1)
    {
      Time cur_time = Simulator::Now();
      m_throughput_sta1 = (m_rxPerMetreRead_sta1 *1420 * 8.0) / (1024*1024)/(cur_time.GetSeconds() - m_startTime.GetSeconds());
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
}

void
// 【收包成功统计】记录成功接收的数据包，用于吞吐和 FER/丢包判断。
RLRateEnv::TrackRxOk (Ptr<const Packet> packet)
{
  if (m_wifiMacAddress == Mac48Address("00:00:00:00:00:04"))
  {
    packetsReceivedPerStep.insert(packet->GetUid());
    m_rxTotal_ap++;
    m_rxPerMetreRead_sta1++;
    m_rxPerStep++;
  } 
}

void
RLRateEnv::TrackPhyTxOk (Ptr<const Packet> packet, double txPowerDbm)
{
  if (m_wifiMacAddress == Mac48Address("00:00:00:00:00:04"))
  {
    m_txPhyPerStep++;
  } 
}
void
// 【MAC 退避状态】记录竞争窗口 CW，反映信道竞争和碰撞压力，可作为 Observation 的一部分。
RLRateEnv::TrackCw (uint32_t cw, uint8_t slot)
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
  if (GetMac()->GetAddress() == Mac48Address("00:00:00:00:00:04"))
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
  if (GetMac()->GetAddress() == Mac48Address("00:00:00:00:00:04"))
  {
    m_snr = dataSnr;
  }
  
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
// 【802.11 核心动作落点】MAC 层即将发送数据帧时调用；这里把 Python 返回的 next_mcs 转成 WifiTxVector 的 DataMode。
RLRateEnv::DoGetDataTxVector (WifiRemoteStation *st, uint16_t allowedWidth)
{
  NS_LOG_FUNCTION (this << st);
  m_wifiMacAddress = GetMac()->GetAddress ();
  
  if (m_readyToUpdate)
  {
      if (m_wifiMacAddress == Mac48Address("00:00:00:00:00:04"))
      {
      }
    double maxThreshold = 0.0;
    WifiMode maxMode = GetDefaultModeForSta (st);
    WifiTxVector txVector;
    txVector.SetChannelWidth (20);
    for (uint32_t i = 0; i < GetNMcsSupported(st); i++)
      {
        WifiMode mode = GetMcsSupported (st, i);
        txVector.SetMode (mode);
// 【物理层门限】根据 WifiTxVector 查询达到目标 BER 所需 SNR，可辅助判断某个 MCS 是否可靠。
        double threshold = GetSnrThreshold (txVector);
        if (threshold > maxThreshold
            && threshold < m_snr)
          {
            maxThreshold = threshold;
            maxMode = mode;
          }
      }

    auto env = m_ns3ai_mod->EnvSetterCond ();
    if (m_dataMode.GetModulationClass () == WIFI_MOD_CLASS_HT)
      env->mcs = m_dataMode.GetMcsValue ();
    env->cw = m_cw;
    env->throughput = m_throughput_sta1;
    env->snr = m_snr;
    m_ns3ai_mod->SetCompleted ();
    
    auto act = m_ns3ai_mod->ActionGetterCond ();
    m_next_mcs = act->next_mcs;
    m_ns3ai_mod->GetCompleted ();
    m_readyToUpdate = false;
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
      GetPhy()->GetTxBandwidth(
          m_dataMode,
          std::min(allowedWidth, GetChannelWidth(st))),
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
      GetPhy()->GetTxBandwidth(
          m_dataMode,
          GetChannelWidth(st)),
      GetAggregation (st));
}


} //namespace ns3
