#include "ns3/dsr-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h" 
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/applications-module.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>

using namespace ns3;

float packetsSent = 0;
float packetsReceived = 0;
float deliveryRatio = 0.0;
std::string phyMode ("DsssRate2Mbps"); 

void ReceivePacket (
  Ptr<Socket> socket
  )
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv ())){
    packetsReceived++;
    std::cout<<"@@@ReceivedPacket->"<<packetsReceived<<" and Size is "<<packet->GetSize ()<<" Bytes. SimulationTime:"<<Simulator::Now().GetMilliSeconds()<<"ms."<<std::endl;
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
    Simulator::Schedule (pktInterval, &GenerateTraffic, socket, pktSize, pktCount-1, pktInterval);
  } 
  else { socket->Close (); }
} 

int 
main(int argc, char **argv)
{
  int nodeNum = 100; // ノード数
  int nodeN = 10; // 区切り数
  int srcNode = 7; // 送信ノード数(実際は-3された値が入る)
  uint16_t port = 8080; // ポート番号
  double step = 90; // 配置間隔
  double totalTime = 300.1; // シミュレーション時間
  //double dataTime = 200.1; // パケット送信をやめる時間
  int packetSize = 1024; // パケットサイズ
  int totalPackets = totalTime;
  double interval = 25.0; // パケット送出間隔
  Time interPacketInterval = Seconds (interval);

  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  nodes.Create (nodeNum);
  
  MobilityHelper mobility;
	mobility.SetPositionAllocator ("ns3::GridPositionAllocator", // 格子状
          "MinX"      , DoubleValue (100.0), // ここら辺を0にすると、ダイクストラアルゴリズムがいかれる....
          "MinY"      , DoubleValue (100.0),
          "DeltaX"    , DoubleValue (step),
          "DeltaY"    , DoubleValue (step),
          "GridWidth" , UintegerValue (nodeN),
          "LayoutType", StringValue ("RowFirst"));
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel"); // ノード移動なし
  mobility.Install (nodes);

  //YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel",
                                  "Exponent", DoubleValue(3.0),
                                  "ReferenceDistance", DoubleValue(1.0),
                                  "ReferenceLoss", DoubleValue(46.6777));
  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
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

  InternetStackHelper stack;
  DsrMainHelper dsrMain;
  DsrHelper dsr;
  stack.Install (nodes);
  dsrMain.Install (dsr, nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.0.0.0");
  interfaces = address.Assign (devices);

  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  // 受信側
  for( int i=3; i<srcNode; i++ ){
    Ptr<Socket> recvSink = Socket::CreateSocket (nodes.Get (nodeNum - 1 - i), tid);
    InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), port);
    recvSink->Bind (local);
    recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));
  }
  // 送信側
  for( int i=3; i<srcNode; i++ ){
    Ptr<Socket> source = Socket::CreateSocket (nodes.Get (i), tid);
    InetSocketAddress remote = InetSocketAddress (interfaces.GetAddress (nodeNum - 1 - i,0), port);
    source->Connect (remote);
    Simulator::Schedule (Seconds (1), &GenerateTraffic, source, packetSize, totalPackets, interPacketInterval);/*
    for(int j=3; j<srcNode; j++){
      Ptr<Socket> source = Socket::CreateSocket (nodes.Get (i), tid);
      InetSocketAddress remote = InetSocketAddress (interfaces.GetAddress (nodeNum - 1 - j,0), port);
      source->Connect (remote);
      Simulator::Schedule (Seconds (1), &GenerateTraffic, source, packetSize, totalPackets, interPacketInterval);
    }*/
  }

  std::cout << "@@@Starting simulation for " << totalTime << " s ...\n";
  //AnimationInterface anim ("data/data-output.xml");
  //AsciiTraceHelper ascii;
  //wifiPhy.EnableAsciiAll (ascii.CreateFileStream ("data/data-trace.tr"));
  //wifiPhy.EnablePcapAll ("data/data-pcap");
  //Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("data/data-wifi.tr");
  //wifiPhy.EnableAsciiAll (stream);

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll(); // 全てのノードにフローモニターを使用する
  
  Simulator::Stop (Seconds (totalTime));
  Simulator::Run ();
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  std::cout << "------------------------------------------------------" << std::endl;
  for(std::map<FlowId,FlowMonitor::FlowStats>::const_iterator i=stats.begin(); i!=stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    if(  (t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.100")
      || (t.sourceAddress=="10.0.0.2" && t.destinationAddress == "10.0.0.99")
    ) {
      std::cout << "Flow " << i->first  << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
      std::cout << " Transmission start:" << i->second.timeFirstTxPacket << std::endl;
      //totalTimeFirstTxPacket += i->second.timeFirstTxPacket.GetSeconds();
      std::cout << "    Reception start:" << i->second.timeFirstRxPacket << std::endl;
      //totalTimeFirstRxPacket += i->second.timeFirstRxPacket.GetSeconds();
      std::cout << "   Transmission end:" << i->second.timeLastTxPacket << std::endl;
      //totalTimeLastTxPacket += i->second.timeLastTxPacket.GetSeconds();
      std::cout << "      Reception end:" << i->second.timeLastRxPacket << std::endl;
      //totalTimeLastRxPacket += i->second.timeLastRxPacket.GetSeconds();
      std::cout << "          Delay sum:" << i->second.delaySum.GetSeconds() << std::endl;
      //totalDelay += i->second.delaySum.GetSeconds(); 
      std::cout << "         Jitter sum:" << i->second.jitterSum.GetSeconds() << std::endl;
      //totalJitter += i->second.jitterSum.GetSeconds(); 
      std::cout << "           Tx Bytes:" << i->second.txBytes << std::endl;
      //totalTxBytes += i->second.txBytes; 
      std::cout << "           Rx Bytes:" << i->second.rxBytes << std::endl;
      //totalRxBytes += i->second.rxBytes; 
      std::cout << "         Tx Packets:" << i->second.txPackets << std::endl;
      //totalTxPackets += i->second.txPackets; 
      std::cout << "         Rx Packets:" << i->second.rxPackets << std::endl;
      //totalRxPackets += i->second.rxPackets; 
      std::cout << "       lost Packets:" << i->second.lostPackets << std::endl;
      //totalLostPackets += i->second.lostPackets; 
      std::cout << "    Times Forwarded:" << i->second.timesForwarded << std::endl;
      //totalTimesForwarded += i->second.timesForwarded;
      std::cout << "     Throughput:" << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - 
        i->second.timeFirstTxPacket.GetSeconds())/1024  << " Kbps" << std::endl;
        //totalThroughput += i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1024; 
        std::cout << "--------------------------------------------------------------------------------" << std::endl;
    }
  }
  Simulator::Destroy ();  

  /*std::cout << "@@@ TOTAL FLOW" << std::endl;
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
  std::cout << "--------------------------------------------------------------------------------" << std::endl;*/
  std::cout << "@@@ RESULT" << std::endl;
  std::cout << "    Total Packets Sent," << packetsSent<<std::endl;
  std::cout << "Total Packets Received," << packetsReceived<<std::endl;
  deliveryRatio = packetsReceived/packetsSent;
  std::cout << " Packet Delivery Ratio," << deliveryRatio*100 << " %" << std::endl;
}
