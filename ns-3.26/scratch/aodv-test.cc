#include "ns3/aodv-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h" 
#include "ns3/applications-module.h"
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

void ReceivePacket (
  Ptr<Socket> socket
  )
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv ())) packetsReceived++;
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
    Simulator::Schedule (pktInterval, &GenerateTraffic, socket, pktSize,pktCount-1, pktInterval);
  } else socket->Close ();
}

int main(int argc, char **argv)
{
  int nodeNum = 5;
  int nodeN = 5; 
  int srcNode = 1;
  uint16_t port = 8080;
  double step = 90;
  double totalTime = 150;
  int packetSize = 1024;
  int totalPackets = totalTime-1;
  double interval = 30.0;
  Time interPacketInterval = Seconds (interval);
  std::string phyMode ("DsssRate2Mbps");
  
  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  nodes.Create (nodeNum);
  
  MobilityHelper mobility;
	mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
          "MinX"      , DoubleValue (0.0),
          "MinY"      , DoubleValue (0.0),
          "DeltaX"    , DoubleValue (step),
          "DeltaY"    , DoubleValue (step),
          "GridWidth" , UintegerValue (nodeN),
          "LayoutType", StringValue ("RowFirst"));
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (nodes);
  std::cout << "@@@Node Positioning.\n";

  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  WifiHelper wifi;
  wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",  // 801.11b
                                "DataMode", StringValue (phyMode), // データ通信速度
                                "ControlMode", StringValue (phyMode) // 制御通信速度
                                );
  WifiMacHelper wifiMac;
  wifiMac.SetType ("ns3::AdhocWifiMac");
  devices = wifi.Install (wifiPhy, wifiMac, nodes); 
  
  AodvHelper aodv;
  InternetStackHelper stack;
  stack.SetRoutingHelper (aodv); 
  stack.Install (nodes);
  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.0.0.0");
  interfaces = address.Assign (devices);

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

  FlowMonitorHelper flowmon;
  flowmon.SetMonitorAttribute("DelayBinWidth", ns3::DoubleValue(0.01)); // ヒストグラムの観測時間間隔
  flowmon.SetMonitorAttribute("JitterBinWidth", ns3::DoubleValue(0.01)); // ヒストグラムの時間間隔 
  flowmon.SetMonitorAttribute("PacketSizeBinWidth", ns3::DoubleValue(1)); // ヒストグラムのパケットサイズ間隔
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  
  Simulator::Stop (Seconds (totalTime-0.1));
  Simulator::Run ();
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  for(std::map<FlowId,FlowMonitor::FlowStats>::const_iterator i=stats.begin(); i!=stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    if( (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.5")) {
      std::cout << "Flow " << i->first  << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
      std::cout << " Transmission start:" << i->second.timeFirstTxPacket << std::endl;
      std::cout << "    Reception start:" << i->second.timeFirstRxPacket << std::endl;
      std::cout << "   Transmission end:" << i->second.timeLastTxPacket << std::endl;
      std::cout << "      Reception end:" << i->second.timeLastRxPacket << std::endl;
      std::cout << "          Delay sum:" << i->second.delaySum.GetSeconds() << std::endl;
      std::cout << "         Jitter sum:" << i->second.jitterSum.GetSeconds() << std::endl;
      std::cout << "           Tx Bytes:" << i->second.txBytes << std::endl;
      std::cout << "           Rx Bytes:" << i->second.rxBytes << std::endl;
      std::cout << "         Tx Packets:" << i->second.txPackets << std::endl;
      std::cout << "         Rx Packets:" << i->second.rxPackets << std::endl;
      std::cout << "       lost Packets:" << i->second.lostPackets << std::endl;
      std::cout << "    Times Forwarded:" << i->second.timesForwarded << std::endl;
      std::cout << "     Throughput:" << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - 
        i->second.timeFirstTxPacket.GetSeconds())/1024  << " Kbps" << std::endl;
        std::cout << "------------------------------------------------------" << std::endl;
    }
  }
  Simulator::Destroy ();  
  std::cout << "@@@ RESULT" << std::endl;
  std::cout << "    Total Packets Sent:" << packetsSent<<std::endl;
  std::cout << "Total Packets Received:" << packetsReceived<<std::endl;
  deliveryRatio = packetsReceived/packetsSent;
  std::cout << " Packet Delivery Ratio:" << deliveryRatio*100 << " %" << std::endl;
}
