/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iostream>

using namespace ns3;

int
main (int argc, char *argv[])
{
  //定义仿真时间、初始距离、速度
  double simulationTime = 10.0;
  double startDistance = 5.0;
  double movingSpeed = 1.0;

  //addvalue注册这些参数给命令行，格式：参数名-描述-变量。parse才是正式调用给main
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("startDistance", "Initial distance between the two nodes in meters", startDistance);
  cmd.AddValue ("movingSpeed", "Speed of node 1 moving away from node 0 in m/s", movingSpeed);
  cmd.Parse (argc, argv);

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

  //创建wifi-mac配置器，设置mac类型adhoc
  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");

  NetDeviceContainer wifiDevices = wifi.Install (wifiPhy, wifiMac, wifiNodes);

  std::cout << "Created " << wifiNodes.GetN () << " Wi-Fi nodes" << std::endl;
  std::cout << "Installed " << wifiDevices.GetN () << " 802.11n Wi-Fi devices" << std::endl;
  std::cout << "Simulation time: " << simulationTime << " s" << std::endl;
  std::cout << "Node 0 starts at " << wifiNodes.Get (0)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 starts at " << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 velocity: " << movingNode->GetVelocity () << std::endl;

  Simulator::Stop (Seconds (simulationTime));
  Simulator::Run ();

  std::cout << "Node 0 ends at " << wifiNodes.Get (0)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;
  std::cout << "Node 1 ends at " << wifiNodes.Get (1)->GetObject<MobilityModel> ()->GetPosition ()
            << std::endl;

  Simulator::Destroy ();

  return 0;
}
