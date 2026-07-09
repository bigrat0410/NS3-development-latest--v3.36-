/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "rl-env.h"

#include "ns3/applications-module.h"
#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/ssid.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"

#include <fstream>
#include <cmath>
#include <string>
#include <sys/stat.h>

using namespace ns3;

namespace
{

struct SampleState
{
  Ptr<PacketSink> sink;
  Ptr<MobilityModel> apMobility;
  Ptr<MobilityModel> staMobility;
  std::ofstream* csv{nullptr};
  uint64_t lastRx{0};
  double interval{1.0};
  double stopTime{100.0};
  double txPowerDbm{20.0};
  double referenceLossDb{30.0};
  double pathLossExponent{3.0};
  double noiseFloorDbm{-101.0};
};

void
EnsureResultsDir ()
{
  mkdir ("my-project-results", 0755);
}

void
WriteSample (SampleState* state)
{
  const double now = Simulator::Now ().GetSeconds ();
  const uint64_t currentRx = state->sink->GetTotalRx ();
  const double throughputMbps =
      static_cast<double> (currentRx - state->lastRx) * 8.0 / state->interval / 1000000.0;
  state->lastRx = currentRx;
  const double distance =
      CalculateDistance (state->apMobility->GetPosition (), state->staMobility->GetPosition ());
  const double effectiveDistance = std::max (1.0, distance);
  const double pathLossDb =
      state->referenceLossDb + 10.0 * state->pathLossExponent * std::log10 (effectiveDistance);
  const double rxPowerDbm = state->txPowerDbm - pathLossDb;
  const double snrDb = rxPowerDbm - state->noiseFloorDbm;
  *state->csv << now << ',' << distance << ',' << rxPowerDbm << ',' << snrDb << ','
              << throughputMbps << '\n';
  if (now + state->interval <= state->stopTime)
    {
      Simulator::Schedule (Seconds (state->interval), &WriteSample, state);
    }
}

} // namespace

int
main (int argc, char* argv[])
{
  std::string apManager = "ns3::MinstrelHtWifiManager";
  std::string label = "default";
  double simulationTime = 100.0;
  double startDistance = 0.0;
  double speed = 0.5;
  double sampleInterval = 1.0;
  uint32_t seed = 1;
  uint32_t run = 1;
  uint32_t rtsThreshold = 65535;
  uint32_t channelWidth = 20;
  bool shortGuardInterval = false;
  std::string dataRate = "60Mbps";
  uint32_t payloadSize = 1420;
  std::string lossModel = "log-distance";
  double matrixLoss = 30.0;
  double referenceLoss = 30.0;
  double pathLossExponent = 3.0;
  double txPower = 20.0;
  double ccaSensitivity = -110.0;
  double rxSensitivity = -110.0;
  double rxNoiseFigure = 0.0;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("apManager", "AP rate manager", apManager);
  cmd.AddValue ("label", "Output label", label);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("startDistance", "Initial AP-STA distance in meters", startDistance);
  cmd.AddValue ("speed", "STA speed away from AP in meters/second", speed);
  cmd.AddValue ("sampleInterval", "Throughput sample interval in seconds", sampleInterval);
  cmd.AddValue ("Seed", "RNG seed", seed);
  cmd.AddValue ("Run", "RNG run", run);
  cmd.AddValue ("rtsThreshold", "RTS/CTS threshold", rtsThreshold);
  cmd.AddValue ("channelWidth", "802.11n channel width in MHz", channelWidth);
  cmd.AddValue ("shortGuardInterval", "Enable short guard interval", shortGuardInterval);
  cmd.AddValue ("dataRate", "Offered UDP load", dataRate);
  cmd.AddValue ("payloadSize", "UDP payload size in bytes", payloadSize);
  cmd.AddValue ("lossModel", "matrix or log-distance", lossModel);
  cmd.AddValue ("matrixLoss", "Default loss in dB for MatrixPropagationLossModel", matrixLoss);
  cmd.AddValue ("referenceLoss", "Reference loss at 1 m for LogDistancePropagationLossModel", referenceLoss);
  cmd.AddValue ("pathLossExponent", "Path loss exponent for LogDistancePropagationLossModel", pathLossExponent);
  cmd.AddValue ("txPower", "Transmit power in dBm", txPower);
  cmd.AddValue ("ccaSensitivity", "CCA energy detection threshold in dBm", ccaSensitivity);
  cmd.AddValue ("rxSensitivity", "RX sensitivity in dBm", rxSensitivity);
  cmd.AddValue ("rxNoiseFigure", "RX noise figure in dB", rxNoiseFigure);
  cmd.Parse (argc, argv);

  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (run);

  NodeContainer apNode;
  apNode.Create (1);
  NodeContainer staNode;
  staNode.Create (1);

  YansWifiChannelHelper channel;
  channel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  if (lossModel == "matrix")
    {
      channel.AddPropagationLoss ("ns3::MatrixPropagationLossModel",
                                  "DefaultLoss",
                                  DoubleValue (matrixLoss));
    }
  else
    {
      channel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                  "Exponent",
                                  DoubleValue (pathLossExponent),
                                  "ReferenceLoss",
                                  DoubleValue (referenceLoss));
    }

  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());
  phy.SetErrorRateModel ("ns3::NistErrorRateModel");
  phy.Set ("TxPowerStart", DoubleValue (txPower));
  phy.Set ("TxPowerEnd", DoubleValue (txPower));
  phy.Set ("CcaEdThreshold", DoubleValue (ccaSensitivity));
  phy.Set ("RxSensitivity", DoubleValue (rxSensitivity));
  phy.Set ("RxNoiseFigure", DoubleValue (rxNoiseFigure));
  phy.Set ("ChannelSettings",
           StringValue ("{0, " + std::to_string (channelWidth) + ", BAND_5GHZ, 0}"));

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211n);
  WifiMacHelper mac;
  Ssid ssid = Ssid ("reinrate-scenario1");

  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",
                                StringValue ("HtMcs0"),
                                "ControlMode",
                                StringValue ("HtMcs0"),
                                "RtsCtsThreshold",
                                UintegerValue (rtsThreshold));
  mac.SetType ("ns3::StaWifiMac",
               "Ssid",
               SsidValue (ssid),
               "ActiveProbing",
               BooleanValue (false));
  NetDeviceContainer staDevice = wifi.Install (phy, mac, staNode);

  if (apManager == "ns3::rl-rateWifiManager")
    {
      wifi.SetRemoteStationManager (apManager,
                                    "DataMode",
                                    StringValue ("HtMcs7"),
                                    "ControlMode",
                                    StringValue ("HtMcs0"),
                                    "RtsCtsThreshold",
                                    UintegerValue (rtsThreshold));
    }
  else
    {
      wifi.SetRemoteStationManager (apManager, "RtsCtsThreshold", UintegerValue (rtsThreshold));
    }
  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevice = wifi.Install (phy, mac, apNode);

  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HtConfiguration/"
               "ShortGuardIntervalSupported",
               BooleanValue (shortGuardInterval));
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
               UintegerValue (0));

  MobilityHelper apMobilityHelper;
  Ptr<ListPositionAllocator> apPosition = CreateObject<ListPositionAllocator> ();
  apPosition->Add (Vector (0.0, 0.0, 0.0));
  apMobilityHelper.SetPositionAllocator (apPosition);
  apMobilityHelper.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  apMobilityHelper.Install (apNode);

  MobilityHelper staMobilityHelper;
  Ptr<ListPositionAllocator> staPosition = CreateObject<ListPositionAllocator> ();
  staPosition->Add (Vector (startDistance, 0.0, 0.0));
  staMobilityHelper.SetPositionAllocator (staPosition);
  staMobilityHelper.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  staMobilityHelper.Install (staNode);
  Ptr<ConstantVelocityMobilityModel> staVelocity =
      staNode.Get (0)->GetObject<ConstantVelocityMobilityModel> ();
  staVelocity->SetVelocity (Vector (speed, 0.0, 0.0));

  InternetStackHelper stack;
  stack.Install (apNode);
  stack.Install (staNode);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterface = address.Assign (staDevice);
  (void) apInterface;

  uint16_t port = 5000;
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (staNode.Get (0));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simulationTime + 1.0));

  OnOffHelper sourceHelper ("ns3::UdpSocketFactory",
                            InetSocketAddress (staInterface.GetAddress (0), port));
  sourceHelper.SetConstantRate (DataRate (dataRate), payloadSize);
  ApplicationContainer sourceApp = sourceHelper.Install (apNode.Get (0));
  sourceApp.Start (Seconds (1.0));
  sourceApp.Stop (Seconds (simulationTime));

  EnsureResultsDir ();
  std::ofstream csv ("my-project-results/reinrate-scenario1-" + label + ".csv");
  csv << "time_s,distance_m,rx_power_dbm,snr_db,throughput_mbps\n";

  SampleState sample;
  sample.sink = DynamicCast<PacketSink> (sinkApp.Get (0));
  sample.apMobility = apNode.Get (0)->GetObject<MobilityModel> ();
  sample.staMobility = staNode.Get (0)->GetObject<MobilityModel> ();
  sample.csv = &csv;
  sample.interval = sampleInterval;
  sample.stopTime = simulationTime;
  sample.txPowerDbm = txPower;
  sample.referenceLossDb = lossModel == "matrix" ? matrixLoss : referenceLoss;
  sample.pathLossExponent = lossModel == "matrix" ? 0.0 : pathLossExponent;
  sample.noiseFloorDbm = rxSensitivity;
  Simulator::Schedule (Seconds (sampleInterval), &WriteSample, &sample);

  Simulator::Stop (Seconds (simulationTime + 0.1));
  Simulator::Run ();

  const double activeDuration = std::max (sampleInterval, simulationTime - 1.0);
  const double avgThroughput =
      static_cast<double> (sample.sink->GetTotalRx ()) * 8.0 / activeDuration / 1000000.0;
  std::ofstream summary ("my-project-results/reinrate-scenario1-" + label + "-summary.csv");
  summary << "label,ap_manager,seed,run,simulation_time_s,start_distance_m,speed_mps,"
             "loss_model,matrix_loss_db,reference_loss_db,path_loss_exponent,tx_power_dbm,"
             "cca_sensitivity_dbm,rx_sensitivity_dbm,rx_noise_figure_db,rx_bytes,"
             "average_active_mbps\n";
  summary << label << ',' << apManager << ',' << seed << ',' << run << ',' << simulationTime
          << ',' << startDistance << ',' << speed << ',' << lossModel << ',' << matrixLoss << ','
          << referenceLoss << ',' << pathLossExponent << ',' << txPower << ',' << ccaSensitivity
          << ',' << rxSensitivity << ',' << rxNoiseFigure << ',' << sample.sink->GetTotalRx ()
          << ',' << avgThroughput << '\n';

  std::cout << "label=" << label << '\n'
            << "apManager=" << apManager << '\n'
            << "lossModel=" << lossModel << '\n'
            << "distanceRange=" << startDistance << '-' << startDistance + speed * simulationTime
            << " m\n"
            << "averageActiveThroughput=" << avgThroughput << " Mbps\n";

  csv.close ();
  summary.close ();
  Simulator::Destroy ();
  return 0;
}
