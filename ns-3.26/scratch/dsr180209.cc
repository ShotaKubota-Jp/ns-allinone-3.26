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
std::string OutputfileNameTX("data/data-output-tx.csv");
std::ofstream ofsTx(OutputfileNameTX);
std::string OutputfileNameRX("data/data-output-rx.csv");
std::ofstream ofsRx(OutputfileNameRX);
std::string phyMode ("DsssRate2Mbps");

void ReceivePacket (
  Ptr<Socket> socket
  )
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv ())){
    packetsReceived++;
    std::cout<<"@@@ReceivedPacket->"<<packetsReceived<<", Size:"<<packet->GetSize ()<<" Bytes. SimulationTime:"<<Simulator::Now().GetMilliSeconds()<<"millisecond."<<std::endl;
    ofsRx << "Rx," << packetsReceived << ",ID," << socket->GetNode ()->GetId () << "," << Simulator::Now().GetMilliSeconds()<<",MilliSecond"<< std::endl;
  }
}

static void GenerateTraffic (
  Ptr<Socket> socket, 
  uint32_t pktSize, 
  uint32_t pktCount, 
  Time pktInterval 
  )
{
  Ptr<UniformRandomVariable> myURV = CreateObject<UniformRandomVariable> ();
  myURV->SetAttribute ("Min", DoubleValue (100));
  myURV->SetAttribute ("Max", DoubleValue (200));
  if (pktCount > 0){
    packetsSent++;
    pktInterval = NanoSeconds (pktInterval.GetNanoSeconds() + myURV->GetValue()); 
    socket->Send (Create<Packet> (pktSize));
    std::cout<<"@@@PacketSent->"<<packetsSent<<", SimulationTime:"<<Simulator::Now().GetMilliSeconds()<<"millisecond."<<std::endl;
    ofsTx << "Tx," << packetsSent << ",ID," << socket->GetNode ()->GetId () << "," << Simulator::Now().GetMilliSeconds()<<",MilliSecond"<<std::endl;
    Simulator::Schedule (pktInterval, &GenerateTraffic, socket, pktSize, pktCount-1, pktInterval );
  } 
  else { socket->Close (); }
}

int 
main(int argc, char **argv)
{ 
  int nodeNum = 81;
  int nodeN = 9;
  int srcNode = 6; // 送信ノード数(実際は-3されてる数が入る)
  uint16_t port = 8080;
  double step = 90;
  int packetSize = 1024;
  double totalTime = 300.1;
  int totalPackets = totalTime;
  double interval = 1;
  Time interPacketInterval = Seconds (interval);
  Time interPacketInterval2 = Seconds (interval/2);
  
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
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
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
                                "DataMode", StringValue (phyMode), // データ通信
                                "ControlMode", StringValue (phyMode)); // 制御通信
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
    Simulator::Schedule (Seconds (1), &GenerateTraffic, source, packetSize, totalPackets, interPacketInterval);
  }

  Ptr<Socket> recvSink = Socket::CreateSocket (nodes.Get (49), tid);
  InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), port);
  recvSink->Bind (local);
  recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));
  Ptr<Socket> source = Socket::CreateSocket (nodes.Get (31), tid);
  InetSocketAddress remote = InetSocketAddress (interfaces.GetAddress (49, 0), port);
  source->Connect (remote);
  Simulator::Schedule (Seconds (totalTime/2), &GenerateTraffic, source, packetSize, totalPackets, interPacketInterval2);

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
  // なお、DSRではフローモニターは使用できない
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  std::cout << "------------------------------------------------------" << std::endl;
  for(std::map<FlowId,FlowMonitor::FlowStats>::const_iterator i=stats.begin(); i!=stats.end(); ++i) {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
    if(  t.sourceAddress=="10.0.0.1" && t.destinationAddress == "10.0.0.100" ) {
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
      std::cout << "     Throughput:" << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds())/1024  << " Kbps" << std::endl;
      std::cout << "--------------------------------------------------------------------------------" << std::endl;
    }
  }
  //flowmon.SerializeToXmlFile("data/data_flow.xml", true, true);
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
  std::cout << "    Total Packets Sent," << packetsSent<<std::endl;
  std::cout << "Total Packets Received," << packetsReceived<<std::endl;
  deliveryRatio = packetsReceived/packetsSent;
  std::cout << " Packet Delivery Ratio," << deliveryRatio*100 << " %" << std::endl;
}
