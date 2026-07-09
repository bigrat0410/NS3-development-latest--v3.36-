/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "learning-rate-manager.h"

#include "ns3/applications-module.h"
#include "ns3/boolean.h"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/ssid.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("MyProjectRateControlMain");

namespace
{

void
EnsureResultsDir ()
{
  mkdir ("my-project-results", 0755);
}

struct ThroughputStats
{
  double activeStart{1.0};
  double convergenceStart{0.0};
  double allSum{0.0};
  double activeSum{0.0};
  double convergenceSum{0.0};
  uint32_t allSamples{0};
  uint32_t activeSamples{0};
  uint32_t convergenceSamples{0};
};

struct ThroughputSampleState
{
  Ptr<PacketSink> sink;
  std::ofstream* csv{nullptr};
  uint64_t lastTotalRx{0};
  double interval{1.0};
  ThroughputStats stats;
  Ptr<MobilityModel> apMobility;
  Ptr<MobilityModel> staMobility;
};

struct FastMobilityState
{
  Ptr<MobilityModel> apMobility;
  Ptr<MobilityModel> staMobility;
  std::ofstream* csv{nullptr};
  double minDistance{120.0};
  double maxDistance{900.0};
  double period{8.0};
  double interval{0.05};
  double stopTime{0.0};
};

void
WriteThroughputSample (ThroughputSampleState* state)
{
  const uint64_t currentRx = state->sink->GetTotalRx ();
  const double now = Simulator::Now ().GetSeconds ();
  const double throughputMbps =
      static_cast<double> (currentRx - state->lastTotalRx) * 8.0 / state->interval / 1000000.0;
  state->lastTotalRx = currentRx;
  std::string phase = "warmup";
  ++state->stats.allSamples;
  state->stats.allSum += throughputMbps;
  if (now > state->stats.activeStart)
    {
      phase = "active";
      ++state->stats.activeSamples;
      state->stats.activeSum += throughputMbps;
    }
  if (now >= state->stats.convergenceStart)
    {
      phase = "convergence";
      ++state->stats.convergenceSamples;
      state->stats.convergenceSum += throughputMbps;
    }
  double distanceMeters = 0.0;
  if (state->apMobility && state->staMobility)
    {
      distanceMeters =
          CalculateDistance (state->apMobility->GetPosition (), state->staMobility->GetPosition ());
    }
  *state->csv << now << ',' << distanceMeters << ',' << throughputMbps << ',' << phase << '\n';
  Simulator::Schedule (Seconds (state->interval), &WriteThroughputSample, state);
}

double
MeanOrZero (double sum, uint32_t count)
{
  return count == 0 ? 0.0 : sum / static_cast<double> (count);
}

void
ApplyDistance (Ptr<MobilityModel> apMobility,
               Ptr<MobilityModel> staMobility,
               double distanceMeters,
               double angleRadians = 0.0)
{
  const double halfDistance = distanceMeters * 0.5;
  const double x = halfDistance * std::cos (angleRadians);
  const double y = halfDistance * std::sin (angleRadians);
  apMobility->SetPosition (Vector (-x, -y, 0.0));
  staMobility->SetPosition (Vector (x, y, 0.0));
}

double
LogInterpolate (double minValue, double maxValue, double normalized)
{
  normalized = std::max (0.0, std::min (1.0, normalized));
  const double logMin = std::log (std::max (1e-6, minValue));
  const double logMax = std::log (std::max (minValue, maxValue));
  return std::exp (logMin + (logMax - logMin) * normalized);
}

void
UpdateFastDistanceMotion (FastMobilityState* state)
{
  const double now = Simulator::Now ().GetSeconds ();
  const double phase = std::fmod (now, state->period) / state->period;
  const double profile[] = {0.00, 0.72, 0.18, 0.95, 0.37, 0.08,
                            0.62, 1.00, 0.27, 0.82, 0.48, 0.12};
  const uint32_t profileSize = sizeof (profile) / sizeof (profile[0]);
  const double scaled = phase * static_cast<double> (profileSize);
  const uint32_t index = static_cast<uint32_t> (std::floor (scaled)) % profileSize;
  const uint32_t nextIndex = (index + 1) % profileSize;
  const double local = scaled - std::floor (scaled);
  const double smooth = local * local * (3.0 - 2.0 * local);
  const double normalized = profile[index] + (profile[nextIndex] - profile[index]) * smooth;
  const double distance = LogInterpolate (state->minDistance, state->maxDistance, normalized);
  constexpr double pi = 3.14159265358979323846;
  const double angle = 2.0 * pi * now / std::max (0.1, state->period * 0.5);
  ApplyDistance (state->apMobility, state->staMobility, distance, angle);
  *state->csv << now << ',' << distance << '\n';

  if (now + state->interval <= state->stopTime)
    {
      Simulator::Schedule (Seconds (state->interval), &UpdateFastDistanceMotion, state);
    }
}

} // namespace

int
main (int argc, char* argv[])
{
  std::string algorithm = "mab";
  double simulationTime = 10.0;
  double distance = 5.0;
  uint32_t channelWidth = 20;
  bool shortGuardInterval = false;
  uint32_t rtsThreshold = 65535;
  std::string dataRate = "60Mbps";
  uint32_t payloadSize = 1420;
  uint32_t seed = 1;
  uint32_t run = 1;
  double sampleInterval = 1.0;
  double convergenceStart = 0.0;
  bool fastMobility = false;
  double minDistance = 8.0;
  double maxDistance = 1200.0;
  double mobilityPeriod = 2.4;
  double mobilityInterval = 0.02;
  double decisionInterval = 0.001;
  bool linearMobility = false;
  double startDistance = 0.0;
  double mobilitySpeed = 0.5;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("algorithm", "Rate manager: mab, dqn, or minstrel", algorithm);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("distance", "Distance between AP and STA in meters", distance);
  cmd.AddValue ("channelWidth", "802.11n channel width in MHz", channelWidth);
  cmd.AddValue ("shortGuardInterval", "Enable HT short guard interval", shortGuardInterval);
  cmd.AddValue ("rtsThreshold", "RTS/CTS threshold", rtsThreshold);
  cmd.AddValue ("dataRate", "Offered UDP load", dataRate);
  cmd.AddValue ("payloadSize", "UDP payload size in bytes", payloadSize);
  cmd.AddValue ("sampleInterval", "Throughput sampling interval in seconds", sampleInterval);
  cmd.AddValue ("convergenceStart",
                "Start time for convergence-segment throughput; 0 means half of simulationTime",
                convergenceStart);
  cmd.AddValue ("fastMobility", "Move AP and STA quickly through a wide SNR range", fastMobility);
  cmd.AddValue ("minDistance", "Minimum AP/STA distance for fast mobility", minDistance);
  cmd.AddValue ("maxDistance", "Maximum AP/STA distance for fast mobility", maxDistance);
  cmd.AddValue ("mobilityPeriod", "Seconds for one near-far-near mobility cycle", mobilityPeriod);
  cmd.AddValue ("mobilityInterval", "Seconds between fast mobility position updates", mobilityInterval);
  cmd.AddValue ("decisionInterval", "Seconds between Python/ns3-ai rate decisions", decisionInterval);
  cmd.AddValue ("linearMobility", "Move the STA away from a fixed AP at constant speed", linearMobility);
  cmd.AddValue ("startDistance", "Initial AP/STA distance for linear mobility", startDistance);
  cmd.AddValue ("mobilitySpeed", "STA speed in meters/second for linear mobility", mobilitySpeed);
  cmd.AddValue ("Seed", "RNG seed", seed);
  cmd.AddValue ("Run", "RNG run", run);
  cmd.Parse (argc, argv);

  if (convergenceStart <= 0.0)
    {
      convergenceStart = std::max (1.0, simulationTime * 0.5);
    }

  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (run);

  NodeContainer wifiStaNode;
  wifiStaNode.Create (1);
  NodeContainer wifiApNode;
  wifiApNode.Create (1);

  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                  "Exponent",
                                  DoubleValue (2.0),
                                  "ReferenceLoss",
                                  DoubleValue (46.6777));

  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetChannel (wifiChannel.Create ());
  wifiPhy.SetErrorRateModel ("ns3::NistErrorRateModel");
  wifiPhy.Set ("TxPowerStart", DoubleValue (20.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (20.0));
  wifiPhy.Set ("ChannelSettings",
               StringValue ("{0, " + std::to_string (channelWidth) + ", BAND_5GHZ, 0}"));

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211n);

  WifiMacHelper wifiMac;
  Ssid ssid = Ssid ("my-project-ap");

  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",
                                StringValue ("HtMcs0"),
                                "ControlMode",
                                StringValue ("HtMcs0"),
                                "RtsCtsThreshold",
                                UintegerValue (rtsThreshold));
  wifiMac.SetType ("ns3::StaWifiMac",
                   "Ssid",
                   SsidValue (ssid),
                   "ActiveProbing",
                   BooleanValue (false));
  NetDeviceContainer staDevice = wifi.Install (wifiPhy, wifiMac, wifiStaNode);

  if (algorithm == "minstrel")
    {
      wifi.SetRemoteStationManager ("ns3::MinstrelHtWifiManager",
                                    "RtsCtsThreshold",
                                    UintegerValue (rtsThreshold));
    }
  else
    {
      wifi.SetRemoteStationManager ("ns3::MyProjectLearningRateManager",
                                    "Algorithm",
                                    StringValue (algorithm),
                                    "ControlMode",
                                    StringValue ("HtMcs0"),
                                    "TraceFile",
                                    StringValue ("my-project-results/manager-" + algorithm + ".csv"),
                                    "DecisionInterval",
                                    DoubleValue (decisionInterval),
                                    "RtsCtsThreshold",
                                    UintegerValue (rtsThreshold));
    }
  wifiMac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevice = wifi.Install (wifiPhy, wifiMac, wifiApNode);

  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HtConfiguration/"
               "ShortGuardIntervalSupported",
               BooleanValue (shortGuardInterval));
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_MaxAmpduSize",
               UintegerValue (0));

  MobilityHelper apMobilityHelper;
  Ptr<ListPositionAllocator> apPositionAlloc = CreateObject<ListPositionAllocator> ();
  apPositionAlloc->Add (Vector (0.0, 0.0, 0.0));
  apMobilityHelper.SetPositionAllocator (apPositionAlloc);
  apMobilityHelper.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  apMobilityHelper.Install (wifiApNode);

  MobilityHelper staMobilityHelper;
  Ptr<ListPositionAllocator> staPositionAlloc = CreateObject<ListPositionAllocator> ();
  staPositionAlloc->Add (Vector (linearMobility ? startDistance : distance, 0.0, 0.0));
  staMobilityHelper.SetPositionAllocator (staPositionAlloc);
  if (linearMobility)
    {
      staMobilityHelper.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    }
  else
    {
      staMobilityHelper.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    }
  staMobilityHelper.Install (wifiStaNode);
  Ptr<MobilityModel> apMobility = wifiApNode.Get (0)->GetObject<MobilityModel> ();
  Ptr<MobilityModel> staMobility = wifiStaNode.Get (0)->GetObject<MobilityModel> ();
  if (linearMobility)
    {
      Ptr<ConstantVelocityMobilityModel> staVelocity =
          DynamicCast<ConstantVelocityMobilityModel> (staMobility);
      staVelocity->SetVelocity (Vector (mobilitySpeed, 0.0, 0.0));
    }
  if (fastMobility)
    {
      minDistance = std::max (1.0, minDistance);
      maxDistance = std::max (minDistance, maxDistance);
      mobilityPeriod = std::max (0.1, mobilityPeriod);
      mobilityInterval = std::max (0.001, mobilityInterval);
      ApplyDistance (apMobility, staMobility, minDistance);
    }

  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNode);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer staInterface = address.Assign (staDevice);
  (void) apInterface;

  const uint16_t port = 5000;
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (wifiStaNode.Get (0));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simulationTime + 1.0));

  OnOffHelper sourceHelper ("ns3::UdpSocketFactory",
                            InetSocketAddress (staInterface.GetAddress (0), port));
  sourceHelper.SetConstantRate (DataRate (dataRate), payloadSize);
  ApplicationContainer sourceApp = sourceHelper.Install (wifiApNode.Get (0));
  sourceApp.Start (Seconds (1.0));
  sourceApp.Stop (Seconds (simulationTime));

  EnsureResultsDir ();
  std::ofstream mobilityCsv;
  FastMobilityState mobilityState;
  if (fastMobility)
    {
      mobilityState.apMobility = apMobility;
      mobilityState.staMobility = staMobility;
      mobilityState.csv = &mobilityCsv;
      mobilityState.minDistance = minDistance;
      mobilityState.maxDistance = maxDistance;
      mobilityState.period = mobilityPeriod;
      mobilityState.interval = mobilityInterval;
      mobilityState.stopTime = simulationTime;
      mobilityCsv.open ("my-project-results/mobility-" + algorithm + ".csv");
      mobilityCsv << "time_s,distance_m\n";
      Simulator::Schedule (Seconds (0.0), &UpdateFastDistanceMotion, &mobilityState);
    }
  std::ofstream csv ("my-project-results/throughput-" + algorithm + ".csv");
  csv << "time_s,distance_m,throughput_mbps,phase\n";
  ThroughputSampleState throughputState;
  throughputState.csv = &csv;
  throughputState.interval = sampleInterval;
  throughputState.stats.activeStart = 1.0;
  throughputState.stats.convergenceStart = convergenceStart;
  throughputState.apMobility = apMobility;
  throughputState.staMobility = staMobility;
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (0));
  throughputState.sink = sink;
  Simulator::Schedule (Seconds (sampleInterval), &WriteThroughputSample, &throughputState);

  Simulator::Stop (Seconds (simulationTime + 0.1));
  Simulator::Run ();

  const double activeDuration = std::max (sampleInterval, simulationTime - 1.0);
  const double averageThroughputMbps =
      static_cast<double> (sink->GetTotalRx ()) * 8.0 / activeDuration / 1000000.0;
  const double allSampleThroughputMbps =
      MeanOrZero (throughputState.stats.allSum, throughputState.stats.allSamples);
  const double activeSampleThroughputMbps =
      MeanOrZero (throughputState.stats.activeSum, throughputState.stats.activeSamples);
  const double convergenceThroughputMbps =
      MeanOrZero (throughputState.stats.convergenceSum, throughputState.stats.convergenceSamples);

  std::ofstream summary ("my-project-results/summary-" + algorithm + ".csv");
  summary << "algorithm,seed,run,simulation_time_s,convergence_start_s,rx_bytes,"
             "average_active_mbps,sample_all_mbps,sample_active_mbps,"
             "sample_convergence_mbps,all_samples,active_samples,convergence_samples,"
             "fast_mobility,min_distance_m,max_distance_m,mobility_period_s,"
             "linear_mobility,start_distance_m,mobility_speed_mps\n";
  summary << algorithm << ',' << seed << ',' << run << ',' << simulationTime << ','
          << convergenceStart << ',' << sink->GetTotalRx () << ',' << averageThroughputMbps
          << ',' << allSampleThroughputMbps << ',' << activeSampleThroughputMbps << ','
          << convergenceThroughputMbps << ',' << throughputState.stats.allSamples << ','
          << throughputState.stats.activeSamples << ',' << throughputState.stats.convergenceSamples
          << ',' << (fastMobility ? 1 : 0) << ','
          << minDistance << ',' << maxDistance << ',' << mobilityPeriod << ','
          << (linearMobility ? 1 : 0) << ',' << startDistance << ',' << mobilitySpeed << '\n';
  summary.close ();

  std::cout << "algorithm=" << algorithm << '\n'
            << "standard=802.11n-5GHz\n"
            << "channelWidth=" << channelWidth << " MHz\n"
            << "distance=" << distance << " m\n"
            << "fastMobility=" << (fastMobility ? "true" : "false") << '\n'
            << "linearMobility=" << (linearMobility ? "true" : "false") << '\n'
            << "mobilityDistanceRange=" << minDistance << '-' << maxDistance << " m\n"
            << "linearDistanceRange=" << startDistance << '-'
            << (startDistance + mobilitySpeed * simulationTime) << " m\n"
            << "mobilityPeriod=" << mobilityPeriod << " s\n"
            << "convergenceStart=" << convergenceStart << " s\n"
            << "rxBytes=" << sink->GetTotalRx () << '\n'
            << "averageActiveThroughput=" << averageThroughputMbps << " Mbps\n"
            << "sampleAllThroughput=" << allSampleThroughputMbps << " Mbps\n"
            << "sampleConvergenceThroughput=" << convergenceThroughputMbps << " Mbps\n";

  csv.close ();
  if (mobilityCsv.is_open ())
    {
      mobilityCsv.close ();
    }
  Simulator::Destroy ();
  return 0;
}
