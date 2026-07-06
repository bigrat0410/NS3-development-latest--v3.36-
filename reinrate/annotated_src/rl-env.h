/*
 * 【中文注释总览】
 * 本头文件定义强化学习速率控制器 rl-rateWifiManager 的数据结构、类接口与成员状态。
 * 它是 C++/ns-3 侧强化学习环境的核心声明文件。
 *
 * 强化学习三要素在这里的静态定义：
 * - Observation / 状态空间：AiConstantRateEnv，当前包含 mcs、cw、throughput、snr。
 * - Action / 动作空间：AiConstantRateAct，当前包含 nss、next_mcs；实际主要使用 next_mcs 选择下一次 MCS。
 * - Reward / 奖励函数：不在 C++ 中计算，Python 端 ns3ai.py 根据 C++ 发送的状态和上一步动作计算。
 *
 * 通信桥梁：Ns3AIRL<AiConstantRateEnv, AiConstantRateAct> 使用 ns3-ai 共享内存/Protobuf 风格接口，
 * C++ 将 env 写入共享块，Python 将 act 写回共享块。
 */
#ifndef AI_CONSTANT_RATE_WIFI_MANAGER_H
#define AI_CONSTANT_RATE_WIFI_MANAGER_H

#include "ns3/wifi-remote-station-manager.h"
#include "ns3/ns3-ai-module.h"
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace ns3 {

// 【Observation 定义】C++ 发送给 Python 的状态结构体；字段顺序必须与 ns3ai.py 的 ReinrateEnv 完全一致。
struct AiConstantRateEnv
{
// 【状态字段】当前/最近使用的 MCS 索引，反映 802.11 调制编码等级。
  uint8_t mcs;
  uint8_t max_mcs;
// 【状态字段】竞争窗口 Contention Window，反映 MAC 层退避/拥塞程度。
  uint16_t cw;
// 【状态字段】统计窗口内吞吐量，Python 端可用它构造 Reward。
  double throughput;
// 【状态字段】监听/接收测得的信噪比，是当前实现最主要的策略输入。
  double snr;
} Packed;

// 【Action 定义】Python 返回给 C++ 的动作结构体；字段顺序必须与 ns3ai.py 的 ReinrateAct 完全一致。
struct AiConstantRateAct
{
  uint8_t nss;
// 【动作字段】下一次数据帧要使用的 MCS，动作空间通常为 {0,1,...,7}。
  uint8_t next_mcs;
} Packed;

/**
 * \ingroup wifi
 * \brief use constant rates for data and RTS transmissions
 *
 * This class uses always the same transmission rate for every
 * packet sent.
 */
class RLRateEnv : public WifiRemoteStationManager
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  RLRateEnv (uint16_t id = 2335);
  virtual ~RLRateEnv ();


private:
  WifiRemoteStation* DoCreateStation (void) const override;
  void DoReportRxOk (WifiRemoteStation *station,
                     double rxSnr, WifiMode txMode) override;
  void DoReportRtsFailed (WifiRemoteStation *station) override;
  void DoReportDataFailed (WifiRemoteStation *station) override;
  void DoReportRtsOk (WifiRemoteStation *station,
                      double ctsSnr, WifiMode ctsMode, double rtsSnr) override;
  void DoReportAmpduTxStatus(WifiRemoteStation* st,
                                        uint16_t nSuccessfulMpdus,
                                        uint16_t nFailedMpdus,
                                        double rxSnr,
                                        double dataSnr,
                                        uint16_t dataChannelWidth,
                                        uint8_t dataNss);
  void DoReportDataOk (WifiRemoteStation *station, double ackSnr, WifiMode ackMode,
                       double dataSnr, uint16_t dataChannelWidth, uint8_t dataNss) override;
  void DoReportFinalRtsFailed (WifiRemoteStation *station) override;
  void DoReportFinalDataFailed (WifiRemoteStation *station) override;
// 【MAC 层决策入口】ns-3 发送数据帧前调用此函数获取 WifiTxVector，本项目在这里应用 DRL 选择的 MCS。
  WifiTxVector DoGetDataTxVector (WifiRemoteStation *station, uint16_t allowedWidth) override;
  WifiTxVector DoGetRtsTxVector (WifiRemoteStation *station) override;
  WifiMode DoGetDataMode (WifiRemoteStation *station, uint32_t size);
  void CalculateThroughput (void);
  void EnqueueTimestamp (Ptr<const Packet> packet);
  void TrackRxOk (Ptr<const Packet> packet);
  void TraceTxOk (Ptr<const Packet> p);
  void TrackCw (uint32_t cw, uint8_t slot);
  // void TrackTimeout (uint8_t reason, Ptr<const WifiPsdu> psdu, const WifiTxVector &txVector);
  void TrackPhyTxOk (Ptr<const Packet> packet, double txPowerDbm);
// 【PHY 监听回调】从 MonitorSniffer 获取 signal/noise，计算 SNR 并更新 Observation。
  void TrackMonitorSniffRx(Ptr<const Packet> packet, uint16_t channelFreqMhz, uint16_t channelNumber, uint32_t rate, bool isShortPreamble, double signalDbm, double noiseDbm);
  void MetreRead(void);
  void AddSnrThreshold(WifiTxVector txVector, double snr);
  void BuildSnrThresholds();
  void DoInitialize();
  double GetSnrThreshold (WifiTxVector txVector) const;
  double CalculateFER (const std::unordered_set<int>& A, const std::unordered_set<int>& B);

  WifiMode m_dataMode; //!< Wifi mode for unicast Data frames
  WifiMode m_ctlMode;  //!< Wifi mode for RTS frames
  double m_snr=0;
  double m_throughput_sta1=0;
  double m_throughput_sta8=0;
  double m_packetTotal=0;
  double m_fer=0;
  uint32_t m_packetRxPerStep=0;
  uint32_t m_txPerStep=0;
  uint32_t m_txPhyPerStep=0;
  uint32_t m_rxPerStep=0;
  uint32_t m_rxPerMetreRead_sta1 = 0;
  uint32_t m_txPerMetreRead_sta1 = 0;
  uint32_t m_bytesTotal_sta8 = 0;
  uint32_t m_rxTotal = 0;
  uint32_t m_txTotal = 0;
  uint32_t m_rxTotal_ap = 0;
  uint32_t m_txTotal_ap = 0;
  uint32_t m_txFail_ap = 0;
  bool m_readyToUpdate = false;
  uint8_t m_nss = 1;
  uint16_t m_cw = 15;
  uint8_t m_next_mcs = 0;
  std::unordered_set<int> packetsSentPerStep;
  std::unordered_set<int> packetsReceivedPerStep;
  Time m_startTime;
  Time m_timeStep{MilliSeconds(12)};
  double m_ber=1e-6;            //!< The maximum Bit Error Rate acceptable at any transmission mode
  typedef std::vector<std::pair<double, WifiTxVector>> Thresholds;
  Thresholds m_thresholds; 
  ns3::Mac48Address m_wifiMacAddress;
  WifiRemoteStation *m_station;

// 【ns3-ai 通信对象】模板参数明确绑定 Observation 和 Action，通过共享内存/同步接口与 Python 交换数据。
  Ns3AIRL<AiConstantRateEnv, AiConstantRateAct> * m_ns3ai_mod;
  uint16_t m_ns3ai_id;
};

} //namespace ns3

#endif /* AI_CONSTANT_RATE_WIFI_MANAGER_H */
