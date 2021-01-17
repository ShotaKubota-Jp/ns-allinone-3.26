/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 University of Kansas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Justin Rohrer <rohrej@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */

/*
 * This example program allows one to run ns-3 DSDV, AODV, or OLSR under a typical random waypoint mobility model.
 *
 * By default, the simulation runs for 200 simulated seconds, of which the first 50 are used for start-up time.  
 * The number of nodes is 50. Nodes move according to RandomWaypointMobilityModel  with a speed of
 * 20 m/s and no pause time within a 300x1500 m region.  The WiFi is in ad hoc mode with a  2 Mb/s rate (802.11b) 
 * and a  Friis loss model. The transmit power is set to 7.5 dBm.
 *
 * It is possible to change the mobility and density of the network by directly modifying the speed and 
 * the number of nodes.  It is also possible to change the characteristics of the network by changing
 * the transmit power (as power increases, the impact of mobility decreases and the effective density increases).
 *
 * By default, OLSR is used, but specifying a value of 2 for the protocol will cause AODV to be used, 
 * and specifying a value of 3 will cause DSDV to be used.
 *
 * By default, there are 10 source/sink data pairs sending UDP data at an application rate of 2.048 Kb/s each.
 * This is typically done at a rate of 4 64-byte packets per second.  Application data is started at a 
 * random time between 50 and 51 seconds and continues to the end of the simulation.
 *
 * The program outputs a few items:
 * - packet receptions are notified to stdout such as: <timestamp> <node-id> received one packet from <src-address>
 * - each second, the data reception statistics are tabulated and output to a comma-separated value (csv) file
 * - some tracing and flow monitor configuration that used to work is left commented inline in the program
 */

#include <fstream>
#include <iostream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
#include "ns3/dsdv-module.h"
#include "ns3/dsr-module.h"
#include "ns3/applications-module.h"

using namespace ns3;
using namespace dsr;

NS_LOG_COMPONENT_DEFINE ("manet01");

class RoutingExperiment
{
public:
  RoutingExperiment ();
  void Run (int nSinks, double txp, int step, int xSize, std::string CSVfileName);
  //static void SetMACParam (ns3::NetDeviceContainer & devices, int slotDistance);
  std::string CommandSetup (int argc, char **argv);

private:
  Ptr<Socket> SetupPacketReceive (Ipv4Address addr, Ptr<Node> node);
  void ReceivePacket (Ptr<Socket> socket);
  void CheckThroughput ();

  uint32_t port;
  uint32_t bytesTotal;
  uint32_t packetsReceived;
  //unit32_t totalDetected; // 追加、パケットの合計到達率を評価したい

  std::string m_CSVfileName;  // XMLファイル名
  int m_nSinks;               // シンクノード数(送信)
  std::string m_protocolName; // プロトコル名
  double m_txp;               // 送信電力
  bool m_traceMobility;       
  uint32_t m_protocol;        // プロトコル判別 
  int m_step;                 // 配置間隔
  int m_xSize;                // 軸に配置する個数
};

RoutingExperiment::RoutingExperiment ()
  : port (9),
    bytesTotal (0),
    packetsReceived (0),
    m_CSVfileName ("manet-routing.output.csv"),
    m_traceMobility (false),
    m_protocol (2) // 1.OLSR 2.AODV 3.DSDV 4.DSR
{
}

static inline std::string
PrintReceivedPacket (Ptr<Socket> socket, Ptr<Packet> packet, Address senderAddress)
{
  std::ostringstream oss;

  oss << Simulator::Now ().GetSeconds () << " " << socket->GetNode ()->GetId ();

  if (InetSocketAddress::IsMatchingType (senderAddress)){
    InetSocketAddress addr = InetSocketAddress::ConvertFrom (senderAddress);
    oss << " received one packet from " << addr.GetIpv4 ();
    std::cout << Simulator::Now ().GetSeconds () << " " << socket->GetNode ()->GetId () << " received one packet from " << addr.GetIpv4 () << std::endl;
  } else {
    oss << " received one packet!";
  }
  return oss.str ();
}

void
RoutingExperiment::ReceivePacket (Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address senderAddress;
  while ((packet = socket->RecvFrom (senderAddress))){
    bytesTotal += packet->GetSize ();
    packetsReceived += 1;
    NS_LOG_UNCOND (PrintReceivedPacket (socket, packet, senderAddress));
  }
}

void
RoutingExperiment::CheckThroughput ()
{
  double kbs = (bytesTotal * 8.0) / 1000;
  bytesTotal = 0;

  std::ofstream out (m_CSVfileName.c_str (), std::ios::app);

  out << (Simulator::Now ()).GetSeconds () << ","
      << kbs << ","
      << packetsReceived << ","
      << m_nSinks << ","
      << m_protocolName << ","
      << m_txp << ""
      << std::endl;

  out.close ();
  packetsReceived = 0;
  Simulator::Schedule (Seconds (1.0), &RoutingExperiment::CheckThroughput, this);
}

Ptr<Socket>
RoutingExperiment::SetupPacketReceive (Ipv4Address addr, Ptr<Node> node)
{
  TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
  Ptr<Socket> sink = Socket::CreateSocket (node, tid);
  InetSocketAddress local = InetSocketAddress (addr, port);
  sink->Bind (local);
  sink->SetRecvCallback (MakeCallback (&RoutingExperiment::ReceivePacket, this));

  return sink;
}

std::string
RoutingExperiment::CommandSetup (int argc, char **argv)
{
  CommandLine cmd;
  cmd.AddValue ("CSVfileName", "The name of the CSV output file name", m_CSVfileName);
  cmd.AddValue ("traceMobility", "Enable mobility tracing", m_traceMobility);
  cmd.AddValue ("protocol", "1=OLSR;2=AODV;3=DSDV;4=DSR", m_protocol);
  cmd.Parse (argc, argv);
  return m_CSVfileName;
}

int
main (int argc, char *argv[])
{
  RoutingExperiment experiment;
  std::string CSVfileName = experiment.CommandSetup (argc,argv);
  // ソース　→　シンク
  //blank out the last output file and write the column headers
  std::ofstream out (CSVfileName.c_str ());
  out << "SimulationSecond," <<
  "ReceiveRate," <<
  "PacketsReceived," <<
  "NumberOfSinks," <<
  "RoutingProtocol," <<
  "TransmissionPower" <<
  std::endl;
  out.close ();

  int nSinks = 8; // シンク数
  double txp = 7.5; // 送信電力
  int step = 100; // 配置間隔
  int xSize = 5; // 配置数

  experiment.Run (nSinks, txp, step, xSize, CSVfileName);
}

void
RoutingExperiment::Run (int nSinks, double txp, int step, int xSize, std::string CSVfileName)
{
  Packet::EnablePrinting ();
  m_nSinks = nSinks; // シンク数
  m_txp = txp; // 送信電力
  m_CSVfileName = CSVfileName; //XMLファイル名
  m_step = step; // 配置間隔
  m_xSize = xSize; // 配置数

  int nWifis = 64; //合計ノード数

  double TotalTime = 200.0;  // シミュレーション時間
  std::string rate ("2048bps");
  std::string phyMode ("DsssRate11Mbps");
  std::string tr_name ("manet-routing-compare");
  int nodeSpeed = 5; // ノードスピード
  int nodePause = 0; // 停止スピード
  m_protocolName = "protocol"; // プロトコル

  Config::SetDefault ("ns3::OnOffApplication::PacketSize",StringValue ("64")); // パケットサイズ
  Config::SetDefault ("ns3::OnOffApplication::DataRate",  StringValue (rate));  // データレート

  //Set Non-unicastMode rate to unicast mode
  Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",StringValue (phyMode));

  NodeContainer adhocNodes;
  adhocNodes.Create (nWifis);

  // setting up wifi phy and channel using helpers
  WifiHelper wifi; // Wi-Fi
  wifi.SetStandard (WIFI_PHY_STANDARD_80211b); // MACプロトコル
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue (phyMode),
                                "ControlMode",StringValue (phyMode));

  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default ();
  wifiPhy.Set ("TxPowerStart", DoubleValue (txp)); // 利用可能な最大送信レベル(dBm)
  wifiPhy.Set ("TxPowerEnd", DoubleValue (txp)); // 利用可能な最小送信レベル(dBm)

  /* // ここらの値はデフォルトの値が入っている
  wifiPhy.Set ("Frequency", UintegerValue (0)); 
  wifiPhy.Set ("ChannelWidth", UintegerValue (20)); 
  wifiPhy.Set ("ChannelNumber", UintegerValue (0)); 
  wifiPhy.Set ("EnergyDetectionThreshold", DoubleValue (-96.0)); 
  wifiPhy.Set ("CcaMode1Threshold", DoubleValue (-99.0)); 
  wifiPhy.Set ("TxGain", DoubleValue (1.0)); 
  wifiPhy.Set ("RxGain", DoubleValue (1.0)); 
  wifiPhy.Set ("TxPowerLevels", UintegerValue (1)); 
  //wifiPhy.Set ("TxPowerEnd", DoubleValue (16.0206)); 
  //wifiPhy.Set ("TxPowerStart", DoubleValue (16.0206)); 
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (7)); 
  wifiPhy.Set ("State", PointerValue ()); 
  wifiPhy.Set ("ChannelSwitchDelay", TimeValue (MicroSeconds (250))); 
  wifiPhy.Set ("TxAntennas", UintegerValue (1)); 
  wifiPhy.Set ("RxAntennas", UintegerValue (1)); 
  wifiPhy.Set ("ShortGuardEnabled", BooleanValue (false)); 
  wifiPhy.Set ("LdpcEnabled", BooleanValue (false)); 
  wifiPhy.Set ("STBCEnabled", BooleanValue (false)); 
  wifiPhy.Set ("GreenfieldEnabled", BooleanValue (false)); 
  wifiPhy.Set ("ShortPlcpPreambleSupported", BooleanValue (false)); */

  YansWifiChannelHelper wifiChannel;
  //wifiChannel.SetPropagationDelay ("ns3::RandomPropagationDelayModel");  // 伝搬遅延モデル
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  //wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");  // 伝搬損失モデル
  //wifiChannel.AddPropagationLoss ("ns3::LogDistancePropagationLossModel");
  wifiChannel.AddPropagationLoss ("ns3::TwoRayGroundPropagationLossModel",
                                  "Frequency", DoubleValue (5.150e9), // デフォルト
                                  "SystemLoss", DoubleValue (1.0),    // デフォルト
                                  "MinDistance", DoubleValue (0.5),   // デフォルト
                                  "HeightAboveZ", DoubleValue (1));   // 高さ0mから1mに変更
  wifiPhy.SetChannel (wifiChannel.Create ());

  // Add a mac and disable rate control.
  WifiMacHelper wifiMac;
  // wifiMac.SetType("ns3::QosSupported"); // QoSありになる
  wifiMac.SetType ("ns3::AdhocWifiMac");   // アドホックモード
  NetDeviceContainer adhocDevices = wifi.Install (wifiPhy, wifiMac, adhocNodes);

  /* // 適当に配置して、ランダムウェイポイントモデルで移動する
  MobilityHelper mobilityAdhoc;
  int64_t streamIndex = 0; // used to get consistent mobility across scenarios
  ObjectFactory pos;
  pos.SetTypeId ("ns3::RandomRectanglePositionAllocator");
  pos.Set ("X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"));
  pos.Set ("Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=1000.0]"));

  Ptr<PositionAllocator> taPositionAlloc = pos.Create ()->GetObject<PositionAllocator> ();
  streamIndex += taPositionAlloc->AssignStreams (streamIndex);
  std::stringstream ssSpeed;
  ssSpeed << "ns3::UniformRandomVariable[Min=0.0|Max=" << nodeSpeed << "]";
  std::stringstream ssPause;
  ssPause << "ns3::ConstantRandomVariable[Constant=" << nodePause << "]";
  mobilityAdhoc.SetMobilityModel ("ns3::RandomWaypointMobilityModel",
                                  "Speed", StringValue (ssSpeed.str ()),
                                  "Pause", StringValue (ssPause.str ()),
                                  "PositionAllocator", PointerValue (taPositionAlloc));
  mobilityAdhoc.SetPositionAllocator (taPositionAlloc);
  mobilityAdhoc.Install (adhocNodes);
  streamIndex += mobilityAdhoc.AssignStreams (adhocNodes, streamIndex);*/

  // グリッド型の配置
  MobilityHelper mobility;
	mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                "MinX"      , DoubleValue (0.0),
                "MinY"      , DoubleValue (0.0),
                "DeltaX"    , DoubleValue (m_step),
                "DeltaY"    , DoubleValue (m_step),
                "GridWidth" , UintegerValue (m_xSize),
                "LayoutType", StringValue ("RowFirst"));
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (adhocNodes);

  // ルーティングヘルパーをインストールする
  AodvHelper aodv;
  OlsrHelper olsr;
  DsdvHelper dsdv;
  DsrHelper dsr;
  DsrMainHelper dsrMain;
  Ipv4ListRoutingHelper list;
  InternetStackHelper internet;

  switch (m_protocol){
    case 1:
      list.Add (olsr, 100);
      m_protocolName = "OLSR";
      break;
    case 2:
      list.Add (aodv, 100);
      m_protocolName = "AODV";
      break;
    case 3:
      list.Add (dsdv, 100);
      m_protocolName = "DSDV";
      break;
    case 4:
      m_protocolName = "DSR";
      break;
    default:
      NS_FATAL_ERROR ("No such protocol:" << m_protocol);
    }

  if (m_protocol < 4){
    internet.SetRoutingHelper (list);
    internet.Install (adhocNodes);
  }
  else if (m_protocol == 4){
    internet.Install (adhocNodes);
    dsrMain.Install (dsr, adhocNodes);
  }

  // IPアドレスの装着
  NS_LOG_INFO ("assigning ip address");
  Ipv4AddressHelper addressAdhoc;
  addressAdhoc.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer adhocInterfaces;
  adhocInterfaces = addressAdhoc.Assign (adhocDevices);

  // CBRトラフィックを生成
  OnOffHelper onoff1 ("ns3::UdpSocketFactory", Address());
  onoff1.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]")); // 一様分布の確率
  onoff1.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));

  // 宛先ノード？？？(シンクの数による)
  for (int i = 0; i < nSinks; i++){
    Ptr<Socket> sink = SetupPacketReceive (adhocInterfaces.GetAddress (i), adhocNodes.Get (i)); // シンクの数による

    AddressValue remoteAddress (InetSocketAddress (adhocInterfaces.GetAddress (i), port));
    onoff1.SetAttribute ("Remote", remoteAddress);

    Ptr<UniformRandomVariable> var = CreateObject<UniformRandomVariable> ();
    ApplicationContainer temp = onoff1.Install (adhocNodes.Get (i + nSinks)); // 何かをインストール
    temp.Start (Seconds (var->GetValue (100.0,101.0)));
    temp.Stop (Seconds (TotalTime));
  }

  std::stringstream ss;
  ss << nWifis;
  std::string nodes = ss.str ();

  std::stringstream ss2;
  ss2 << nodeSpeed;
  std::string sNodeSpeed = ss2.str ();

  std::stringstream ss3;
  ss3 << nodePause;
  std::string sNodePause = ss3.str ();

  std::stringstream ss4;
  ss4 << rate;
  std::string sRate = ss4.str ();

  AsciiTraceHelper ascii;
  //MobilityHelper::EnableAsciiAll (ascii.CreateFileStream (tr_name + ".mob"));

  NS_LOG_INFO ("Run Simulation.");
  CheckThroughput ();
  Simulator::Stop (Seconds (TotalTime));
  Simulator::Run ();
  Simulator::Destroy ();
}
