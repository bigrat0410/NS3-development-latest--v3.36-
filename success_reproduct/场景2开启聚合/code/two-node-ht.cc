/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//调试规则，第一次创建或修改CMakeLists时，./ns3ai_env/bin/python ./ns3 configure
//1.编译cmake --build cmake-cache --target scratch_reproduction_two-node-ht -j2
//2.查看参数./build/scratch/reproduction/ns3.36.1-two-node-ht-default --PrintHelp
//3.运行./build/scratch/reproduction/ns3.36.1-two-node-ht-default \
    --simulationTime=2 \
    --trafficStartTime=0.5 \
    --startDistance=5 \
    --movingSpeed=1 \
    --lossModel=log-distance \
    --pathLossExponent=2 \
    --referenceLoss=46.6777 \
    --packetInterval=0.001

//./build/scratch/reproduction/ns3.36.1-two-node-ht-default \
    --simulationTime=80 \
    --trafficStartTime=0.1 \
    --startDistance=35 \
    --movingSpeed=0 \
    --lossModel=log-distance \
    --pathLossExponent=4 \
    --referenceLoss=46.6777 \
    --packetInterval=0.00011



//场景1复现命令
//./build/scratch/reproduction/ns3.36.1-two-node-ht-default \
    --simulationTime=80 \
    --trafficStartTime=0.5 \
    --startDistance=1 \
    --movingSpeed=0.5 \
    --rateManager=ns3::IdealWifiManager \
    --lossModel=log-distance \
    --pathLossExponent=3 \
    --referenceLoss=66.6777 \
    --dataRate=60Mbps \
    --packetSize=1420 \
    --sampleInterval=1 \
    --outputFile=my-project-results/reproduction-scenario1-ideal-ref66.csv

//./build/scratch/reproduction/ns3.36.1-two-node-ht-default \
    --simulationTime=80 \
    --trafficStartTime=0.5 \
    --startDistance=1 \
    --movingSpeed=0.5 \
    --rateManager=ns3::MinstrelHtWifiManager \
    --lossModel=log-distance \
    --pathLossExponent=3 \
    --referenceLoss=66.6777 \
    --dataRate=60Mbps \
    --packetSize=1420 \
    --sampleInterval=0.5 \
    --outputFile=my-project-results/reproduction-scenario1-ideal-ref66.csv

    //画图：python3 scratch/reproduction/result_figure.py \
    my-project-results/reproduction-scenario1-ideal-ref66.csv


#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include "rl-env.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

using namespace ns3;

namespace
{
//采样相关定义
struct ThroughputSampler
{
  Ptr<PacketSink> sink;
  Ptr<MobilityModel> apMobility;
  Ptr<MobilityModel> staMobility;
  std::ofstream* output{nullptr};
  uint64_t previousRxBytes{0};
  double interval{1.0};
  double stopTime{80.0};
};

//采样相关定义
void
SampleThroughput (ThroughputSampler* sampler)
{
  const double now = Simulator::Now ().GetSeconds ();
  const uint64_t currentRxBytes = sampler->sink->GetTotalRx ();
  const double throughputMbps =
      static_cast<double> (currentRxBytes - sampler->previousRxBytes) * 8.0 /
      sampler->interval / 1e6;
  sampler->previousRxBytes = currentRxBytes;

  const double distance = CalculateDistance (sampler->apMobility->GetPosition (),
                                             sampler->staMobility->GetPosition ());
  *sampler->output << now << ',' << distance << ',' << throughputMbps << '\n';

  if (now + sampler->interval <= sampler->stopTime)
    {
      Simulator::Schedule (Seconds (sampler->interval), &SampleThroughput, sampler);
    }
}

} // namespace

int
main (int argc, char *argv[])
{
  //定义仿真时间、初始距离、速度
  double simulationTime = 80.0;
  double startDistance = 1.0;
  double movingSpeed = 0.5;
  uint32_t scenario = 1;
  double randomWalkMinDistance = 0.0;
  double randomWalkMaxDistance = 24.0;
  double randomWalkSpeed = 3.0;
  double randomWalkDirectionInterval = 2.0;

  //论文场景使用持续的 60 Mbps UDP 下行业务
  double trafficStartTime = 0.5;
  std::string offeredDataRate = "60Mbps";
  //采样间隔（数据统计和画图）
  double sampleInterval = 0.5;
  std::string outputFile = "my-project-results/reproduction-scenario1-ideal.csv";
  uint32_t seed = 1;
  uint32_t run = 1;
  //每个数据包的大小
  uint32_t packetSize = 1420;
  //默认使用 ns-3 的理想速率控制器，可由命令行覆盖
  std::string rateManagerType = "ns3::IdealWifiManager";
  //默认保持原始代码的对数距离损耗模型，也可切换为固定链路损耗
  std::string lossModel = "log-distance";
  //LogDistance:loss(d) = referenceLoss + 10 * pathLossExponent *log10(d)
  //路径损耗指数，
  double pathLossExponent = 3.0;
  //802.11协议信号传递一米时的自然衰减值
  double referenceLoss = 46.6777;

  //定义矩阵模型损耗
  double matrixLoss = 30.0;
  

  //addvalue注册这些参数给命令行，格式：参数名-描述-变量。parse才是正式调用给main
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("startDistance", "Initial distance between the two nodes in meters", startDistance);
  cmd.AddValue ("movingSpeed", "Speed of node 1 moving away from node 0 in m/s", movingSpeed);
  cmd.AddValue ("scenario", "Mobility scenario: 1=linear outward, 2=1D random walk", scenario);
  cmd.AddValue ("randomWalkMinDistance", "Scenario 2 minimum STA distance in meters", randomWalkMinDistance);
  cmd.AddValue ("randomWalkMaxDistance", "Scenario 2 maximum STA distance in meters", randomWalkMaxDistance);
  cmd.AddValue ("randomWalkSpeed", "Scenario 2 constant STA speed in m/s", randomWalkSpeed);
  cmd.AddValue ("randomWalkDirectionInterval", "Scenario 2 random direction decision interval in seconds", randomWalkDirectionInterval);
  cmd.AddValue ("trafficStartTime", "UDP traffic start time in seconds", trafficStartTime);
  cmd.AddValue ("dataRate", "Offered UDP load", offeredDataRate);
  cmd.AddValue ("sampleInterval", "Throughput sampling interval in seconds", sampleInterval);
  cmd.AddValue ("outputFile", "CSV file for distance-throughput samples", outputFile);
  cmd.AddValue ("Seed", "RNG seed", seed);
  cmd.AddValue ("Run", "RNG run number", run);
  cmd.AddValue ("packetSize", "UDP payload size in bytes", packetSize);
  cmd.AddValue ("rateManager", "Wi-Fi rate control manager TypeId-ns3::MinstrelHtWifiManager or ns3::IdealWifiManager", rateManagerType);
  cmd.AddValue ("lossModel", "Propagation loss model: log-distance or matrix", lossModel);
  cmd.AddValue ("matrixLoss", "Fixed loss in dB used by the matrix model", matrixLoss);
  cmd.AddValue ("pathLossExponent", "Exponent used by the log-distance model", pathLossExponent);
  cmd.AddValue ("referenceLoss", "Loss in dB at 1 m used by the log-distance model", referenceLoss);
  cmd.Parse (argc, argv);

  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (run);

  //仿真参数逻辑性检查（时序是否正确）
  if (simulationTime <= trafficStartTime || sampleInterval <= 0.0 || packetSize == 0)
    {
      std::cerr << "Invalid traffic parameters: require simulationTime > trafficStartTime > 0, "
                << "sampleInterval > 0, and packetSize > 0" << std::endl;
      return 1;
    }
  if (lossModel != "log-distance" && lossModel != "matrix")
    {
      std::cerr << "Invalid lossModel: use 'log-distance' or 'matrix'" << std::endl;
      return 1;
    }
  if (matrixLoss < 0.0 || pathLossExponent <= 0.0 || referenceLoss < 0.0)
    {
      std::cerr << "Invalid propagation parameters: losses must be non-negative and "
                << "pathLossExponent must be positive" << std::endl;
      return 1;
    }
  if (scenario != 1 && scenario != 2)
    {
      std::cerr << "Invalid scenario: use 1 or 2" << std::endl;
      return 1;
    }
  if (scenario == 2 &&
      (randomWalkMinDistance < 0.0 ||
       randomWalkMaxDistance <= randomWalkMinDistance ||
       startDistance < randomWalkMinDistance ||
       startDistance > randomWalkMaxDistance ||
       randomWalkSpeed <= 0.0 || randomWalkDirectionInterval <= 0.0))
    {
      std::cerr << "Invalid Scenario 2 mobility parameters" << std::endl;
      return 1;
    }
  //创建两个节点：node 0 是 AP，node 1 是移动 STA
  NodeContainer wifiNodes;
  wifiNodes.Create (2);

  //设置移动性，list positionallocator位置分配器，添加两个坐标，设置到分配器
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  positionAlloc->Add (Vector (startDistance, 0.0, 0.0));
  mobility.SetPositionAllocator (positionAlloc);

  // AP stays fixed in both scenarios.
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiNodes.Get (0));
  if (scenario == 1)
    {
      mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
      mobility.Install (wifiNodes.Get (1));
      Ptr<ConstantVelocityMobilityModel> movingNode =
          wifiNodes.Get (1)->GetObject<ConstantVelocityMobilityModel> ();
      Simulator::ScheduleNow (&ConstantVelocityMobilityModel::SetVelocity,
                              movingNode,
                              Vector (movingSpeed, 0.0, 0.0));
    }
  else
    {
      // RandomWalk2d normally samples any angle in [0, 2*pi).  Scenario 2 is
      // strictly one-dimensional, so sample only +x (0) or -x (pi).  A new
      // draw is made every two seconds and may equal the previous draw; a
      // direction decision therefore does not imply a mandatory reversal.
      Ptr<EmpiricalRandomVariable> direction = CreateObject<EmpiricalRandomVariable> ();
      direction->CDF (0.0, 0.5);
      direction->CDF (std::acos (-1.0), 1.0);
      constexpr double oneDimensionalTolerance = 1e-7;
      mobility.SetMobilityModel (
          "ns3::RandomWalk2dMobilityModel",
          "Bounds", RectangleValue (Rectangle (randomWalkMinDistance,
                                                randomWalkMaxDistance,
                                                -oneDimensionalTolerance,
                                                oneDimensionalTolerance)),
          "Mode", StringValue ("Time"),
          "Time", TimeValue (Seconds (randomWalkDirectionInterval)),
          "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=" +
                                std::to_string (randomWalkSpeed) + "]"),
          "Direction", PointerValue (direction));
      mobility.Install (wifiNodes.Get (1));
      NodeContainer movingSta;
      movingSta.Add (wifiNodes.Get (1));
      mobility.AssignStreams (movingSta, 0);
    }

  //创建信道配置器，并根据命令行参数选择随距离变化或固定的传播损耗
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  if (lossModel == "matrix")
    {
      wifiChannel.AddPropagationLoss ("ns3::MatrixPropagationLossModel",
                                      "DefaultLoss",
                                      DoubleValue (matrixLoss));
    }
  else
    {
      wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                      "Exponent",
                                      DoubleValue (pathLossExponent),
                                      "ReferenceLoss",
                                      DoubleValue (referenceLoss));
    }

  //创建wifi-phy配置器,绑定无线信道
  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetChannel (wifiChannel.Create ());

  //设置误码率模型，nisterrorratemodel
  wifiPhy.SetErrorRateModel ("ns3::NistErrorRateModel");
  //设置wifi工作信道，0表示自动选信道，如果写36就是选择5g-channel36，20Mhz带宽，5G频段，最后primary20Index = 0表示子信道位置
  wifiPhy.Set ("ChannelSettings", StringValue ("{36, 20, BAND_5GHZ, 0}"));
  //设置发射功率的start-end
  wifiPhy.Set ("TxPowerStart", DoubleValue (20.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (20.0));
  //发射功率仅1个挡位
  wifiPhy.Set ("TxPowerLevels", UintegerValue (1));
  //定义理想接收机
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (0.0));
  //与开源实验代码保持一致，弱信号帧直接交给后续 PHY 解码模型判断
  wifiPhy.DisablePreambleDetectionModel ();
  //定义cca门限
  //wifiPhy.Set ("CcaEdThreshold", DoubleValue (-110.0));

  //创建wifihelper，确定标准
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211n);
  WifiMacHelper wifiMac;
  Ssid ssid = Ssid ("reproduction-scenario1");

  //接收端 STA 只发送 ACK/管理帧，使用固定的稳健基础速率
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",
                                StringValue ("HtMcs0"),
                                "ControlMode",
                                StringValue ("HtMcs0"));
  wifiMac.SetType ("ns3::StaWifiMac",
                   "Ssid",
                   SsidValue (ssid),
                   "ActiveProbing",
                   BooleanValue (false));
  NetDeviceContainer staDevice = wifi.Install (wifiPhy, wifiMac, wifiNodes.Get (1));

  //论文中的速率自适应算法运行在发送下行业务的 AP 上
  wifi.SetRemoteStationManager (rateManagerType);
  wifiMac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevice = wifi.Install (wifiPhy, wifiMac, wifiNodes.Get (0));

  //设置两个wifi设备
  NetDeviceContainer wifiDevices;
  wifiDevices.Add (apDevice);
  wifiDevices.Add (staDevice);

  //取出 node 0 的第一个 Wi-Fi 网络设备，并转换为 WifiNetDevice，以便读取其内部的速率控制器
  Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice> (apDevice.Get (0));

  // Preserve the final incomplete 20-packet learning window at the episode boundary.
  Ptr<RLRateEnv> rlManager = DynamicCast<RLRateEnv> (wifiDevice->GetRemoteStationManager ());
  if (rlManager)
    {
      Simulator::Schedule (Seconds (simulationTime), &RLRateEnv::FlushPendingWindow, rlManager);
    }

  //获取该 Wi-Fi 设备的 WifiRemoteStationManager，读取其实际类型名，例如 ns3::IdealWifiManager
  std::string rateManager = wifiDevice->GetRemoteStationManager ()->GetInstanceTypeId ().GetName ();

  // 给两个节点安装 Internet 协议栈，并为 Wi-Fi 设备分配 IPv4 地址
  InternetStackHelper internet;
  internet.Install (wifiNodes);

  // 设置 IPv4 地址
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterface = ipv4.Assign (apDevice);
  Ipv4InterfaceContainer staInterface = ipv4.Assign (staDevice);

  //定义UDP接收窗口为5000
  uint16_t udpPort = 5000;

  //创建UDP接收端，表示接收任意ipv4地址，端口为5000的udp数据，然后安装到node1
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), udpPort));
  ApplicationContainer sinkApps = sinkHelper.Install (wifiNodes.Get (1));

  //接收端运行时间：0-仿真结束
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (simulationTime));

  //持续 CBR 下行业务，与论文开源代码的 OnOffHelper 配置一致
  OnOffHelper source ("ns3::UdpSocketFactory",
                      InetSocketAddress (staInterface.GetAddress (0), udpPort));
  source.SetConstantRate (DataRate (offeredDataRate), packetSize);
  ApplicationContainer sourceApps = source.Install (wifiNodes.Get (0));
  sourceApps.Start (Seconds (trafficStartTime));
  sourceApps.Stop (Seconds (simulationTime));

  mkdir ("my-project-results", 0755);
  std::ofstream samples (outputFile);
  if (!samples.is_open ())
    {
      std::cerr << "Unable to open output file: " << outputFile << std::endl;
      return 1;
    }
  samples << "time_s,distance_m,throughput_mbps\n";

  //采样器细节定义
  ThroughputSampler sampler;
  sampler.sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  sampler.apMobility = wifiNodes.Get (0)->GetObject<MobilityModel> ();
  sampler.staMobility = wifiNodes.Get (1)->GetObject<MobilityModel> ();
  sampler.output = &samples;
  sampler.interval = sampleInterval;
  sampler.stopTime = simulationTime;
  Simulator::Schedule (Seconds (trafficStartTime + sampleInterval),
                       &SampleThroughput,
                       &sampler);

  std::cout << "Created " << wifiNodes.GetN () << " Wi-Fi nodes" << std::endl;
  std::cout << "Installed " << wifiDevices.GetN () << " Wi-Fi devices" << std::endl;
  std::cout << "Wi-Fi rate control manager: " << rateManager << std::endl;
  std::cout << "Propagation loss model: " << lossModel;
  if (lossModel == "matrix")
    {
      std::cout << " (fixed loss=" << matrixLoss << " dB)";
    }
  else
    {
      std::cout << " (reference loss=" << referenceLoss << " dB, exponent="
                << pathLossExponent << ")";
    }

  std::cout << std::endl;
  std::cout << "AP IPv4 address: " << apInterface.GetAddress (0) << std::endl;
  std::cout << "STA IPv4 address: " << staInterface.GetAddress (0) << std::endl;
  std::cout << "UDP packet size: " << packetSize << " bytes" << std::endl;
  std::cout << "Offered UDP load: " << offeredDataRate << std::endl;
  std::cout << "Throughput sample interval: " << sampleInterval << " s" << std::endl;
  std::cout << "Preamble detection model: disabled" << std::endl;
  std::cout << "Simulation time: " << simulationTime << " s" << std::endl;
  std::cout << "Node 0 starts at " << wifiNodes.Get (0)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 starts at " << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Mobility scenario: " << scenario << std::endl;
  std::cout << "Node 1 velocity: "
            << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetVelocity () << std::endl;
  if (scenario == 2)
    {
      std::cout << "Scenario 2 RandomWalk2d: x=[" << randomWalkMinDistance << ','
                << randomWalkMaxDistance << "] m, speed=" << randomWalkSpeed
                << " m/s, direction decision every " << randomWalkDirectionInterval
                << " s, direction in {+x,-x}" << std::endl;
    }
  std::cout << "rateManagerType " << rateManagerType << std::endl;
  std::cout << "Distance-throughput CSV: " << outputFile << std::endl;
  // 设置仿真停止时间
  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();


  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  uint64_t totalRxBytes = sink->GetTotalRx ();
  double measurementTime = simulationTime - trafficStartTime;
  double throughputMbps = static_cast<double> (totalRxBytes) * 8.0 / measurementTime / 1e6;

  std::cout << "Received UDP bytes: " << totalRxBytes << std::endl;
  std::cout << "Measured UDP throughput: " << throughputMbps << " Mbps" << std::endl;
  std::cout << "Measurement summary: initialDistance=" << startDistance << " m, rateManager="
            << rateManager << ", receivedBytes=" << totalRxBytes << ", throughputMbps="
            << throughputMbps << std::endl;

  std::cout << "Node 0 ends at " << wifiNodes.Get (0)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 ends at " << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;

  samples.close ();
  Simulator::Destroy ();

  return 0;
}
