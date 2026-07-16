#ifndef REPRODUCTION_RL_ENV_H
#define REPRODUCTION_RL_ENV_H

#include "ns3/ai-module.h"
#include "ns3/nstime.h"
#include "ns3/wifi-remote-station-manager.h"

#include <cstdint>

namespace ns3
{

class Object;
class Packet;
class WifiMac;

// C++发送给Python的Observation；字段顺序和Python ctypes结构必须完全一致
struct AiConstantRateEnv
{
  std::uint8_t mcs;//当前DATA使用的HT MCS
  std::uint8_t max_mcs;//按当前SNR门限估计的最高可用MCS
  std::uint16_t cw;//BE队列当前竞争窗口
  double throughput;//统计窗口内有效载荷吞吐量，单位Mbps
  double snr;//发送端收到ACK或Block ACK时的线性SNR
  double reward;//当前动作覆盖的20个DATA包对应的折扣累计奖励
} __attribute__ ((packed));

// Python返回给C++的Action；当前复现只实际使用next_mcs
struct AiConstantRateAct
{
  std::uint8_t nss;//保留字段；场景1的MCS0-7固定对应1条空间流
  std::uint8_t next_mcs;//下一发送机会使用的HT MCS，合法范围0-7
} __attribute__ ((packed));

// C++侧RL环境：负责采集状态、交换Action并把MCS写入WifiTxVector
class RLRateEnv : public WifiRemoteStationManager
{
public:
  static TypeId GetTypeId ();

  RLRateEnv ();
  ~RLRateEnv () override;

private:
  using MsgInterface = Ns3AiMsgInterfaceImpl<AiConstantRateEnv, AiConstantRateAct>;

  // ns-3对象生命周期和MAC绑定
  void DoInitialize () override;
  void DoDispose () override;
  void SetupMac (const Ptr<WifiMac> mac) override;
  WifiRemoteStation* DoCreateStation () const override;

  // MAC竞争窗口与固定DATA包决策窗口内的吞吐量统计
  void UpdateCw (std::uint32_t oldCw, std::uint32_t newCw);
  void RecordSuccessfulMpdus (std::uint16_t count);
  void RecordTransmissionAirtime (WifiRemoteStation* station, std::uint16_t count);
  void CompletePacket (bool success);
  void PrepareWindowObservation ();
  void ResetWindow ();
  double GetPacketReward (double packetThroughputMbps) const;

  // 与Python交换一次Observation/Action；EnableAi=false时不会进入共享内存
  void ExchangeWithAgent (WifiRemoteStation* station);
  std::uint8_t GetCurrentMcs () const;
  std::uint8_t GetMaxMcsForSnr (WifiRemoteStation* station) const;
  bool ApplyMcs (WifiRemoteStation* station, std::uint8_t requestedMcs);

  // ns-3发送结果反馈
  void DoReportRxOk (WifiRemoteStation* station,
                     double rxSnr,
                     WifiMode txMode) override;
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
                       std::uint16_t dataChannelWidth,
                       std::uint8_t dataNss) override;
  void DoReportAmpduTxStatus (WifiRemoteStation* station,
                              std::uint16_t nSuccessfulMpdus,
                              std::uint16_t nFailedMpdus,
                              double rxSnr,
                              double dataSnr,
                              std::uint16_t dataChannelWidth,
                              std::uint8_t dataNss) override;
  void DoReportFinalRtsFailed (WifiRemoteStation* station) override;
  void DoReportFinalDataFailed (WifiRemoteStation* station) override;

  // DATA和RTS发送参数入口
  WifiTxVector DoGetDataTxVector (WifiRemoteStation* station) override;
  WifiTxVector DoGetRtsTxVector (WifiRemoteStation* station) override;
  WifiTxVector BuildDataTxVector (WifiRemoteStation* station, WifiMode mode) const;

  // TypeId可配置参数
  WifiMode m_dataMode;//DATA初始模式；AI动作会更新它
  WifiMode m_ctlMode;//RTS使用的控制模式
  bool m_enableAi{false};//固定速率自检时关闭，运行Python代理时开启
  bool m_ampduEnabled{false};//从MAC的BE_MaxAmpduSize读取
  Time m_measurementStart{Seconds (0.5)};//与场景业务开始时间保持一致
  Time m_decisionInterval{MilliSeconds (12)};//保留属性以兼容旧命令行；Paper20不使用定时决策
  std::uint32_t m_decisionPacketWindow{20};//一个动作固定覆盖的完成DATA包数
  double m_rewardDiscount{0.99};
  std::uint32_t m_payloadSize{1420};//每个成功MPDU对应的有效UDP负载字节数
  double m_ber{1e-6};//估计max_mcs时使用的目标BER

  // Observation运行状态
  double m_snr{0.0};
  std::uint16_t m_cw{15};
  double m_throughput{0.0};//当前20包窗口内按PHY airtime计算的有效负载吞吐量
  double m_windowReward{0.0};
  std::uint64_t m_successfulPayloadBytes{0};
  Time m_transmissionAirtime{Seconds (0)};//当前20包窗口内DATA尝试占用的PHY时间
  Time m_currentPacketAirtime{Seconds (0)};
  std::uint32_t m_completedPackets{0};
  bool m_windowHadAttempt{false};
  bool m_observationReady{false};
  bool m_initialActionSelected{false};
  WifiRemoteStation* m_station{nullptr};

  // 只连接本速率管理器所属MAC的Txop，避免全局通配Trace污染
  Ptr<Object> m_txop;
  Ptr<WifiMac> m_mac;
  bool m_cwTraceConnected{false};

  // 该指针由ns3-ai单例持有，RLRateEnv不负责delete
  MsgInterface* m_msgInterface{nullptr};
};

} // namespace ns3

#endif // REPRODUCTION_RL_ENV_H
