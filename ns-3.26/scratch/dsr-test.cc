#include "ns3/dsr-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h" 
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>

using namespace ns3;

float packetsSent = 0;
float packetsReceived = 0;
float deliveryRatio = 0.0;
std::string CSVfileName("data/data-flow.csv");
std::string OutputfileName("data/data-output.csv");
std::ofstream ofs(CSVfileName);
std::ofstream ofs2(OutputfileName);
std::string phyMode ("DsssRate2Mbps");

void ReceivePacket (
  Ptr<Socket> socket
  )
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv ())){
    packetsReceived++;
    std::cout<<"@@@ReceivedPacket->"<<packetsReceived<<" and Size is["<<packet->GetSize ()<<"]Bytes. SimulationTime:"<<Simulator::Now().GetMilliSeconds()<<"ms."<<std::endl;
    ofs2 << "ReceivePacket ," << packetsReceived << "," << Simulator::Now().GetMilliSeconds()<<",ms,Packet Size, "<<packet->GetSize ()<<std::endl;
  }
}

static void GenerateTraffic (
  Ptr<Socket> socket, 
  uint32_t pktSize, 
  uint32_t pktCount, 
  Time pktInterval 
  )
{
  if (pktCount > 0){
    socket->Send (Create<Packet> (pktSize));
    packetsSent++;
    std::cout<<"@@@PacketSent->"<<packetsSent<<", SimulationTime:"<<Simulator::Now().GetMilliSeconds()<<"ms."<<std::endl;
    ofs2 << "GeneratePacket ," << packetsSent << "," << Simulator::Now().GetMilliSeconds() << ",ms,,";
    Simulator::Schedule (pktInterval, &GenerateTraffic, socket, pktSize,pktCount-1, pktInterval);
  } 
  else { socket->Close (); }
}

int 
main(int argc, char **argv)
{
  /*LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_ALL);
  LogComponentEnable ("UdpL4Protocol", LOG_LEVEL_ALL);
  LogComponentEnable ("UdpSocketImpl", LOG_LEVEL_ALL);
  LogComponentEnable ("NetDevice", LOG_LEVEL_ALL);
  LogComponentEnable ("Ipv4L3Protocol", LOG_LEVEL_ALL);
  LogComponentEnable ("Ipv4EndPointDemux", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrOptions", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrHelper", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrRouting", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrOptionHeader", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrFsHeader", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrGraReplyTable", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrSendBuffer", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrRouteCache", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrMaintainBuffer", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrRreqTable", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrErrorBuffer", LOG_LEVEL_ALL);
  LogComponentEnable ("DsrNetworkQueue", LOG_LEVEL_ALL);*/

  int nodeNum = 5; // ノード数
  int nodeN = 5; // 区切り数
  int srcNode = 1; // 送信ノード数
  uint16_t port = 8080; // ポート番号
  double step = 90; // 配置間隔
  double totalTime = 130; // シミュレーション時間
  int packetSize = 1024; // パケットサイズ
  int totalPackets = totalTime-1;
  double interval = 50.0; // パケット送出間隔
  /*double nodeMaxSpeed = 60; // ノードの最高移動速度
  double nodeMinSpeed = 10; // ノードの最低移動速度
  double nodeMaxPause = 5; // ノードの最大停止時間
  double nodeMinPause = 2; // ノードの最小停止時間
  double nodeMaxX = 50; // X方向の最大移動距離
  double nodeMinX = 10; // X方向の最小移動距離
  double nodeMaxY = 50; // Y方向の最大移動距離
  double nodeMinY = 10; // Y方向の最小移動距離
  double nodeZ = 1; // Z(固定)*/
  Time interPacketInterval = Seconds (interval);

  // 統計
  double totalTimeFirstTxPacket = 0.0;
  double totalTimeFirstRxPacket = 0.0;
  double totalTimeLastTxPacket = 0.0;
  double totalTimeLastRxPacket = 0.0;
  double totalDelay = 0.0;
  double totalJitter = 0.0;
  double totalTxBytes = 0.0;
  double totalRxBytes = 0.0;
  double totalTxPackets = 0.0;
  double totalRxPackets = 0.0;
  double totalLostPackets = 0.0;
  double totalTimesForwarded = 0.0;
  double totalThroughput = 0.0;

  ofs << "Simulation Time, Number of Node, Node Placement Interval,Packet Size,Packet Transmission Interval" << std::endl;
  ofs << totalTime << "," << nodeNum << "," << step << "," << packetSize << "," << interval << std::endl << std::endl;

  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  nodes.Create (nodeNum);
  
  MobilityHelper mobility;
	mobility.SetPositionAllocator ("ns3::GridPositionAllocator", // 格子状
          "MinX"      , DoubleValue (0.0),
          "MinY"      , DoubleValue (0.0),
          "DeltaX"    , DoubleValue (step),
          "DeltaY"    , DoubleValue (step),
          "GridWidth" , UintegerValue (nodeN),
          "LayoutType", StringValue ("RowFirst"));
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel"); // ノード移動なし
  /*mobility.SetPositionAllocator ("ns3::RandomRectanglePositionAllocation",
          "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"),
          "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"));
  mobility.SetMobilityModel("ns3::SteadyStateRandomWaypointMobilityModel",
          "MinSpeed", DoubleValue (nodeMinSpeed),
          "MaxSpeed", DoubleValue (nodeMaxSpeed),
          "MinPause", DoubleValue (nodeMinPause),
          "MaxPause", DoubleValue (nodeMaxPause),
          "MinX", DoubleValue (nodeMinX),
          "MaxX", DoubleValue (nodeMaxX),
          "MinY", DoubleValue (nodeMinY),
          "MaxY", DoubleValue (nodeMaxY),
          "Z", DoubleValue(nodeZ));*/
  mobility.Install (nodes);
  std::cout << "@@@Node Positioning.\n";

  //YansWifiChannelHelper wifiChannel;
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  //wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  //wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
  //wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  /*wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                  "Exponent", DoubleValue(3.0),
                                  "ReferenceDistance", DoubleValue(1.0),
                                  "ReferenceLoss", DoubleValue(46.6777));*/
  /*wifiChannel.AddPropagationLoss ("ns3::TwoRayGroundPropagationLossModel",
                                  "Frequency", DoubleValue (5.150e9),
                                  "SystemLoss", DoubleValue (1.0),
                                  "MinDistance", DoubleValue (0.5),
                                  "HeightAboveZ", DoubleValue (1)); // 高さ0mから1mに変更*/
  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  //wifiPhy.Set ("TxPowerStart", DoubleValue (txp)); // for 802.11b, 100m 利用可能な最大送信レベル(dBm)
  //wifiPhy.Set ("TxPowerEnd", DoubleValue (txp)); // for 802.11b, 100m 利用可能な最小送信レベル(dBm)
  wifiPhy.SetChannel (wifiChannel.Create ());
  WifiHelper wifi;
  wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",  // 801.11b
                                "DataMode", StringValue (phyMode), // データ通信速度
                                "ControlMode", StringValue (phyMode)); // 制御通信速度
  //NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");
  devices = wifi.Install (wifiPhy, wifiMac, nodes); 
  std::cout << "@@@MAC Layer.\n";

  InternetStackHelper stack;
  DsrMainHelper dsrMain;
  DsrHelper dsr;
  stack.Install (nodes);
  dsrMain.Install (dsr, nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.0.0.0");
  interfaces = address.Assign (devices);
  std::cout << "@@@Internet Layer.\n";

  //TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  Ptr<Socket> recvSink = Socket::CreateSocket (nodes.Get (nodeNum-1), tid);
  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), port);
  recvSink->Bind (local);
  recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));

  for(int i=0;i<srcNode;i++){
    Ptr<Socket> source = Socket::CreateSocket (nodes.Get (i), tid);
    InetSocketAddress remote = InetSocketAddress (interfaces.GetAddress (nodeNum-1,0), port);
    source->Connect (remote);
    Simulator::Schedule (Seconds (1), &GenerateTraffic, source, packetSize, totalPackets, interPacketInterval);
  } 
  std::cout << "@@@Application Layer.\n";

  std::cout << "@@@Starting simulation for " << totalTime << " s ...\n";
  /*AnimationInterface anim ("data/data-output.xml");
  AsciiTraceHelper ascii;
  wifiPhy.EnableAsciiAll (ascii.CreateFileStream ("data/data-trace.tr"));
  wifiPhy.EnablePcapAll ("data/data-pcap");
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("data/data-wifi.tr");
  wifiPhy.EnableAsciiAll (stream);*/

  FlowMonitorHelper flowmon;
  flowmon.SetMonitorAttribute("DelayBinWidth", ns3::DoubleValue(0.01)); // ヒストグラムの観測時間間隔
  flowmon.SetMonitorAttribute("JitterBinWidth", ns3::DoubleValue(0.01)); // ヒストグラムの時間間隔 
  flowmon.SetMonitorAttribute("PacketSizeBinWidth", ns3::DoubleValue(1)); // ヒストグラムのパケットサイズ間隔
  Ptr<FlowMonitor> monitor = flowmon.InstallAll(); // 全てのノードにフローモニターを使用する
  ofs << "Flow,Total Time First Tx Packet,Total Time First Rx Packet,Total Time Last Tx Packet,Total Time Last Rx Packet,Total Delay,Total Jitter,Total Tx Bytes,Total Rx Bytes,Total Tx Packets, Total Rx Packets,Total Lost Packets,Total Times Forwarded,Total Throughput" << std::endl;

  Simulator::Stop (Seconds (totalTime - 0.1));
  Simulator::Run ();
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  std::cout << "------------------------------------------------------" << std::endl;
  for(std::map<FlowId,FlowMonitor::FlowStats>::const_iterator i=stats.begin(); i!=stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    if(  (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.2" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.3" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.4" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.5" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.6" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.7" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.8" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.9" && t.destinationAddress == "10.0.0.100")
      
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.36")
      || (t.sourceAddress=="10.0.0.2" && t.destinationAddress == "10.0.0.36")
      || (t.sourceAddress=="10.0.0.3" && t.destinationAddress == "10.0.0.36")
      || (t.sourceAddress=="10.0.0.4" && t.destinationAddress == "10.0.0.36")
      || (t.sourceAddress=="10.0.0.5" && t.destinationAddress == "10.0.0.36")
      || (t.sourceAddress=="10.0.0.6" && t.destinationAddress == "10.0.0.36")

      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.25")
      || (t.sourceAddress=="10.0.0.2" && t.destinationAddress == "10.0.0.25")
      || (t.sourceAddress=="10.0.0.3" && t.destinationAddress == "10.0.0.25")
      || (t.sourceAddress=="10.0.0.4" && t.destinationAddress == "10.0.0.25")
      || (t.sourceAddress=="10.0.0.5" && t.destinationAddress == "10.0.0.25")

      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.16")
      || (t.sourceAddress=="10.0.0.2" && t.destinationAddress == "10.0.0.16")
      || (t.sourceAddress=="10.0.0.3" && t.destinationAddress == "10.0.0.16")
      || (t.sourceAddress=="10.0.0.4" && t.destinationAddress == "10.0.0.16")

      //|| (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.2")
      //|| (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.3")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.4")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.5")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.6")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.7")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.8")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.9")
      || (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.10")
    ) {
      std::cout << "Flow " << i->first  << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
      ofs <<  "Flow" << i->first << ",";
      std::cout << " Transmission start:" << i->second.timeFirstTxPacket << std::endl;
      ofs << i->second.timeFirstTxPacket.GetSeconds() << ","; 
      totalTimeFirstTxPacket += i->second.timeFirstTxPacket.GetSeconds();
      std::cout << "    Reception start:" << i->second.timeFirstRxPacket << std::endl;
      ofs << i->second.timeFirstRxPacket.GetSeconds() << ","; 
      totalTimeFirstRxPacket += i->second.timeFirstRxPacket.GetSeconds();
      std::cout << "   Transmission end:" << i->second.timeLastTxPacket << std::endl;
      ofs << i->second.timeLastTxPacket.GetSeconds() << ","; 
      totalTimeLastTxPacket += i->second.timeLastTxPacket.GetSeconds();
      std::cout << "      Reception end:" << i->second.timeLastRxPacket << std::endl;
      ofs << i->second.timeLastRxPacket.GetSeconds() << ","; 
      totalTimeLastRxPacket += i->second.timeLastRxPacket.GetSeconds();
      std::cout << "          Delay sum:" << i->second.delaySum.GetSeconds() << std::endl;
      ofs << i->second.delaySum.GetSeconds() << ","; 
      totalDelay += i->second.delaySum.GetSeconds(); 
      std::cout << "         Jitter sum:" << i->second.jitterSum.GetSeconds() << std::endl;
      ofs << i->second.jitterSum.GetSeconds() << ","; 
      totalJitter += i->second.jitterSum.GetSeconds(); 
      std::cout << "           Tx Bytes:" << i->second.txBytes << std::endl;
      ofs << i->second.txBytes << ","; 
      totalTxBytes += i->second.txBytes; 
      std::cout << "           Rx Bytes:" << i->second.rxBytes << std::endl;
      ofs << i->second.rxBytes << ","; 
      totalRxBytes += i->second.rxBytes; 
      std::cout << "         Tx Packets:" << i->second.txPackets << std::endl;
      ofs << i->second.txPackets << ","; 
      totalTxPackets += i->second.txPackets; 
      std::cout << "         Rx Packets:" << i->second.rxPackets << std::endl;
      ofs << i->second.rxPackets << ","; 
      totalRxPackets += i->second.rxPackets; 
      std::cout << "       lost Packets:" << i->second.lostPackets << std::endl;
      ofs << i->second.lostPackets << ","; 
      totalLostPackets += i->second.lostPackets; 
      std::cout << "    Times Forwarded:" << i->second.timesForwarded << std::endl;
      ofs << i->second.timesForwarded << ",";
      totalTimesForwarded += i->second.timesForwarded;
      std::cout << "     Throughput:" << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - 
        i->second.timeFirstTxPacket.GetSeconds())/1024  << " Kbps" << std::endl;
        ofs << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds())/1024 << "," << std::endl;
        totalThroughput += i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1024; 
        std::cout << "------------------------------------------------------" << std::endl;
    }
  }
  ofs << std::endl << std::endl << ", TOTAL" << std::endl;
  ofs << ",Total Time First Tx Packet,Total Time First Rx Packet,Total Time Last Tx Packet,Total Time Last Rx Packet,Total Delay,Total Jitter,Total Tx Bytes,Total Rx Bytes,Total Tx Packets, Total Rx Packets,Total Lost Packets,Total Times Forwarded,Total Throughput" << std::endl;
  ofs << "," << totalTimeFirstTxPacket << "," << totalTimeFirstRxPacket << "," << totalTimeLastTxPacket << "," << totalTimeLastRxPacket << "," << totalDelay << "," << totalJitter << "," << totalTxBytes << "," << totalRxBytes << "," << totalTxPackets << "," << totalRxPackets << "," << totalLostPackets << "," << totalTimesForwarded << "," << totalThroughput;
  flowmon.SerializeToXmlFile("data/data_flow.xml", true, true);
  Simulator::Destroy ();  

  std::cout << "@@@ TOTAL FLOW" << std::endl;
  std::cout << " Transmission start:" << totalTimeFirstTxPacket << std::endl;
  std::cout << "    Reception start:" << totalTimeFirstRxPacket << std::endl;
  std::cout << "   Transmission end:" << totalTimeLastTxPacket << std::endl;
  std::cout << "      Reception end:" << totalTimeLastRxPacket << std::endl;
  std::cout << "         Jitter sum:" << totalJitter << std::endl;
  std::cout << "           Tx Bytes:" << totalTxBytes << std::endl;
  std::cout << "           Rx Bytes:" << totalRxBytes << std::endl;
  std::cout << "         Tx Packets:" << totalTxPackets << std::endl;
  std::cout << "         Rx Packets:" << totalRxPackets << std::endl;
  std::cout << "       lost Packets:" << totalLostPackets << std::endl;
  std::cout << "    Times Forwarded:" << totalTimesForwarded << std::endl;
  std::cout << "     Throughput:" << totalThroughput << " Kbps" << std::endl;
  std::cout << "------------------------------------------------------" << std::endl;
  std::cout << "@@@ RESULT" << std::endl;
  std::cout << "    Total Packets Sent:" << packetsSent<<std::endl;
  std::cout << "Total Packets Received:" << packetsReceived<<std::endl;
  deliveryRatio = packetsReceived/packetsSent;
  std::cout << " Packet Delivery Ratio:" << deliveryRatio*100 << " %" << std::endl;
}
