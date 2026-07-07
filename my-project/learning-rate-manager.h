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
    uint32_t lastAction{1};
    uint32_t currentAction{1};
    double lastSnrDb{15.0};
    double previousMeanSnrDb{15.0};
    double previousStdSnrDb{0.0};
    double previousAckRatio{1.0};
    double lastPhyRateMbps{0.0};
    double bestPhyRateMbps{0.0};
    double lastReward{0.0};
    double lastEpsilon{0.0};
    uint32_t lastRequestedAction{1};
    uint32_t lastAppliedAction{1};
    uint32_t lastNumActions{1};
    uint32_t currentMcs{0};
    uint32_t successes{0};
    uint32_t failures{0};
    uint32_t consecutiveSuccesses{0};
    uint32_t consecutiveFailures{0};
    uint32_t decisionStep{0};
    double nextDecisionTime{0.0};
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
