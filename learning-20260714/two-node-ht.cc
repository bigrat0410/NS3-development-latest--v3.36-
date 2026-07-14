/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iostream>
#include <string>

using namespace ns3;

int
main (int argc, char *argv[])
{
  //定义仿真时间、初始距离、速度
  double simulationTime = 80.0;
  double startDistance = 0.0;
  double movingSpeed = 0.5;

  //udp客户端从1秒开始发送
  double trafficStartTime = 1.0;
  //每0.01秒发送一个数据包
  double packetInterval = 0.01;
  //每个数据包的大小
  uint32_t packetSize = 1024;
  //最大发送数据包数
  uint32_t maxPackets = 1000000;
  //默认使用 ns-3 的理想速率控制器，可由命令行覆盖
  std::string rateManagerType = "ns3::IdealWifiManager";

  //addvalue注册这些参数给命令行，格式：参数名-描述-变量。parse才是正式调用给main
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("startDistance", "Initial distance between the two nodes in meters", startDistance);
  cmd.AddValue ("movingSpeed", "Speed of node 1 moving away from node 0 in m/s", movingSpeed);
  cmd.AddValue ("trafficStartTime", "UDP traffic start time in seconds", trafficStartTime);
  cmd.AddValue ("packetInterval", "Interval between UDP packets in seconds", packetInterval);
  cmd.AddValue ("packetSize", "UDP payload size in bytes", packetSize);
  cmd.AddValue ("maxPackets", "Maximum number of UDP packets to send", maxPackets);
  cmd.AddValue ("rateManager", "Wi-Fi rate control manager TypeId", rateManagerType);
  cmd.Parse (argc, argv);

  //仿真参数逻辑性检查（时序是否正确）
  if (simulationTime <= trafficStartTime || packetInterval <= 0.0 || packetSize == 0 || maxPackets == 0)
    {
      std::cerr << "Invalid traffic parameters: require simulationTime > trafficStartTime > 0, "
                << "packetInterval > 0, packetSize > 0, and maxPackets > 0" << std::endl;
      return 1;
    }

  //创建wifinodes容器，容器里创建2个节点
  NodeContainer wifiNodes;
  wifiNodes.Create (2);

  //设置移动性，list positionallocator位置分配器，添加两个坐标，设置到分配器
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  positionAlloc->Add (Vector (startDistance, 0.0, 0.0));
  mobility.SetPositionAllocator (positionAlloc);

  //设置静态位置模型，安装到node0、node1
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiNodes.Get (0));
  mobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
  mobility.Install (wifiNodes.Get (1));

  //取出node1的静态模型，调用里面的setv，设置速度
  Ptr<ConstantVelocityMobilityModel> movingNode =
      wifiNodes.Get (1)->GetObject<ConstantVelocityMobilityModel> ();
  movingNode->SetVelocity (Vector (movingSpeed, 0.0, 0.0));

  //创建信道配置器，设置固定光速传播时延模型，设置路径传播损耗，Exponent = 2.0 近似自由空间传播
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                  "Exponent",
                                  DoubleValue (2.0));

  //创建wifi-phy配置器,绑定无线信道
  YansWifiPhyHelper wifiPhy;
  wifiPhy.SetChannel (wifiChannel.Create ());

  //设置误码率模型，nisterrorratemodel
  wifiPhy.SetErrorRateModel ("ns3::NistErrorRateModel");

  //设置wifi工作信道，0表示自动选信道，如果写36就是选择5g-channel36，20Mhz带宽，5G频段，最后primary20Index = 0表示子信道位置
  wifiPhy.Set ("ChannelSettings", StringValue ("{0, 20, BAND_5GHZ, 0}"));

  //创建wifihelper，确定标准
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211n);
  wifi.SetRemoteStationManager (rateManagerType);

  //创建wifi-mac配置器，设置mac类型adhoc
  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");
  NetDeviceContainer wifiDevices = wifi.Install (wifiPhy, wifiMac, wifiNodes);

  //取出 node 0 的第一个 Wi-Fi 网络设备，并转换为 WifiNetDevice，以便读取其内部的速率控制器
  Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice> (wifiDevices.Get (0));

  //获取该 Wi-Fi 设备的 WifiRemoteStationManager，读取其实际类型名，例如 ns3::IdealWifiManager
  std::string rateManager = wifiDevice->GetRemoteStationManager ()->GetInstanceTypeId ().GetName ();

  // 给两个节点安装 Internet 协议栈，并为 Wi-Fi 设备分配 IPv4 地址
  InternetStackHelper internet;
  internet.Install (wifiNodes);

  // 设置 IPv4 地址
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer wifiInterfaces = ipv4.Assign (wifiDevices);

  //定义UDP接收窗口为5000
  uint16_t udpPort = 5000;

  //创建UDP接收端，表示接收任意ipv4地址，端口为5000的udp数据，然后安装到node1
  PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), udpPort));
  ApplicationContainer sinkApps = sinkHelper.Install (wifiNodes.Get (1));

  //接收端运行时间：0-仿真结束
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (simulationTime));

  //创建UDP客户端，表示向节点1发送数据，应用定义的设置参数
  UdpClientHelper client (wifiInterfaces.GetAddress (1), udpPort);
  client.SetAttribute ("MaxPackets", UintegerValue (maxPackets));
  client.SetAttribute ("Interval", TimeValue (Seconds (packetInterval)));
  client.SetAttribute ("PacketSize", UintegerValue (packetSize));

  // 安装UDP客户端到node0
  ApplicationContainer clientApps = client.Install (wifiNodes.Get (0));
  clientApps.Start (Seconds (trafficStartTime));
  clientApps.Stop (Seconds (simulationTime));

  std::cout << "Created " << wifiNodes.GetN () << " Wi-Fi nodes" << std::endl;
  std::cout << "Installed " << wifiDevices.GetN () << " Wi-Fi devices" << std::endl;
  std::cout << "Wi-Fi rate control manager: " << rateManager << std::endl;
  std::cout << "Node 0 IPv4 address: " << wifiInterfaces.GetAddress (0) << std::endl;
  std::cout << "Node 1 IPv4 address: " << wifiInterfaces.GetAddress (1) << std::endl;
  std::cout << "UDP packet size: " << packetSize << " bytes" << std::endl;
  std::cout << "UDP packet interval: " << packetInterval << " s" << std::endl;
  std::cout << "Simulation time: " << simulationTime << " s" << std::endl;
  std::cout << "Node 0 starts at " << wifiNodes.Get (0)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 starts at " << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 velocity: " << movingNode->GetVelocity () << std::endl;

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

  Simulator::Destroy ();

  return 0;
}
