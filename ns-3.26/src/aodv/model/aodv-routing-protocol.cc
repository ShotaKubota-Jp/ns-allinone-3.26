/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * Based on 
 *      NS-2 AODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 * 
 *      AODV-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/AODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#define NS_LOG_APPEND_CONTEXT                                   \
  if (m_ipv4) { std::clog << "[node " << m_ipv4->GetObject<Node> ()->GetId () << "] "; } 

#include "aodv-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-header.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <algorithm>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("AodvRoutingProtocol");

namespace aodv
{
NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

/// UDP Port for AODV control traffic
const uint32_t RoutingProtocol::AODV_PORT = 654;

//-----------------------------------------------------------------------------
/// Tag used by AODV implementation
class DeferredRouteOutputTag : public Tag
{

public:
  DeferredRouteOutputTag (int32_t o = -1) : Tag (), m_oif (o) {}

  static TypeId GetTypeId ()
  {
    static TypeId tid = TypeId ("ns3::aodv::DeferredRouteOutputTag")
      .SetParent<Tag> ()
      .SetGroupName("Aodv")
      .AddConstructor<DeferredRouteOutputTag> ();
    return tid;
  }

  TypeId  GetInstanceTypeId () const 
  {
    return GetTypeId ();
  }

  int32_t GetInterface() const
  {
    return m_oif;
  }

  void SetInterface(int32_t oif)
  {
    m_oif = oif;
  }

  uint32_t GetSerializedSize () const
  {
    return sizeof(int32_t);
  }

  void  Serialize (TagBuffer i) const
  {
    i.WriteU32 (m_oif);
  }

  void  Deserialize (TagBuffer i)
  {
    m_oif = i.ReadU32 ();
  }

  void  Print (std::ostream &os) const
  {
    os << "DeferredRouteOutputTag: output interface = " << m_oif;
  }

private:
  /// Positive if output device is fixed in RouteOutput
  int32_t m_oif;
};

NS_OBJECT_ENSURE_REGISTERED (DeferredRouteOutputTag);

//-----------------------------------------------------------------------------
RoutingProtocol::RoutingProtocol () :
  m_rreqRetries (2), // RREQの最大転送回数
  m_ttlStart (1), // RREQのTTL
  m_ttlIncrement (2), // Expanding用のTTL
  m_ttlThreshold (7), // リング検索を拡張するための最大TTL値
  m_timeoutBuffer (2), // タイムアウトバッファ
  m_rreqRateLimit (10), // 1secあたりのRREQ最大数
  m_rerrRateLimit (10), // 1secあたりのRREP最大数
  m_activeRouteTimeout (Seconds (3)), // 経路有効時間(Active)
  m_netDiameter (35), // ネット直径は、ネットワーク内の2つのノード間のホップ数の最大値を測定します
  m_nodeTraversalTime (MilliSeconds (40)), // パケットの平均ホップトラバーサル時間を控えめに見積もったもの(遅延、割り込みなどなどが必要)
  m_netTraversalTime (Time ((2 * m_netDiameter) * m_nodeTraversalTime)), // 平均ネットトラバーサル時間の見積もり
  m_pathDiscoveryTime ( Time (2 * m_netTraversalTime)), // ネットワーク内のルートを見つけるのに必要な最大時間の見積もり
  m_myRouteTimeout (Time (2 * std::max (m_pathDiscoveryTime, m_activeRouteTimeout))), // このノートで生成されるRREPの有効期間フィールドの値
  m_helloInterval (Seconds (1)), 
  m_allowedHelloLoss (2), // 有効なリンクで失われる可能性があるhelloメッセージの数
  m_deletePeriod (Time (5 * std::max (m_activeRouteTimeout, m_helloInterval))),
  m_nextHopWait (m_nodeTraversalTime + MilliSeconds (10)), // 隣接RREP_ACKを待っている期間
  m_blackListTimeout (Time (m_rreqRetries * m_netTraversalTime)), // ノードがブラックリストに入れられる時間
  m_maxQueueLen (64), // ルーティングプロトコルがバッファできる最大パケット数
  m_maxQueueTime (Seconds (30)), // ルーティングプロトコルがパケットをバッファリングできる最大時間
  m_destinationOnly (false), // 多分Dフラグ(RREPの中間ノードに関すること)
  m_gratuitousReply (false), // 元々はtrue // 無償のRREPがノード起点のルート探索に対してユニキャストされるべきかどうかを示します
  m_enableHello (true), // Helloメッセージを有効にする(現在は無効状態)
  m_routingTable (m_deletePeriod), // ルーティングテーブル
  m_queue (m_maxQueueLen, m_maxQueueTime), // ルーティング層が経路を持たないパケットをバッファするために使用する「ドロップフロント」キュー
  m_requestId (0), // RREQid
  m_seqNo (0), // シーケンス番号
  m_rreqIdCache (m_pathDiscoveryTime), // 重複したRREQを処理
  m_dpd (m_pathDiscoveryTime), // 複製されたブロードキャスト/マルチキャストパケットを処理する
  m_nb (m_helloInterval), // 隣接を処理
  m_rreqCount (0), // RREQ回数
  m_rerrCount (0), // RREP回数
  m_myLoadReq (0), // 負荷(added by s.kubota.)
  m_myLoadRep (0), // 負荷(added by s.kubota.)
  m_htimer (Timer::CANCEL_ON_DESTROY), // Helloタイマー
  m_rreqRateLimitTimer (Timer::CANCEL_ON_DESTROY), // RREQレート制御タイマー
  m_rerrRateLimitTimer (Timer::CANCEL_ON_DESTROY), // RREPレート制御タイマー
  m_lastBcastTime (Seconds (0)) // 最後のブロードキャスト時間
{
  // コールバック関数 m_nb:隣接ノード / 次ホップがリンクが解除する時に、RERRを送る
  m_nb.SetCallback (MakeCallback (&RoutingProtocol::SendRerrWhenBreaksLinkToNextHop, this));
}

TypeId
RoutingProtocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::aodv::RoutingProtocol")
    .SetParent<Ipv4RoutingProtocol> ()
    .SetGroupName("Aodv")
    .AddConstructor<RoutingProtocol> ()
    .AddAttribute ("HelloInterval", "HELLO messages emission interval.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RoutingProtocol::m_helloInterval),
                   MakeTimeChecker ())
    .AddAttribute ("TtlStart", "Initial TTL value for RREQ.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&RoutingProtocol::m_ttlStart),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("TtlIncrement", "TTL increment for each attempt using the expanding ring search for RREQ dissemination.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_ttlIncrement),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("TtlThreshold", "Maximum TTL value for expanding ring search, TTL = NetDiameter is used beyond this value.",
                   UintegerValue (7),
                   MakeUintegerAccessor (&RoutingProtocol::m_ttlThreshold),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("TimeoutBuffer", "Provide a buffer for the timeout.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_timeoutBuffer),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("RreqRetries", "Maximum number of retransmissions of RREQ to discover a route",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RreqRateLimit", "Maximum number of RREQ per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RerrRateLimit", "Maximum number of RERR per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rerrRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NodeTraversalTime", "Conservative estimate of the average one hop traversal time for packets and should include "
                   "queuing delays, interrupt processing times and transfer times.",
                   TimeValue (MilliSeconds (40)),
                   MakeTimeAccessor (&RoutingProtocol::m_nodeTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("NextHopWait", "Period of our waiting for the neighbour's RREP_ACK = 10 ms + NodeTraversalTime",
                   TimeValue (MilliSeconds (50)),
                   MakeTimeAccessor (&RoutingProtocol::m_nextHopWait),
                   MakeTimeChecker ())
    .AddAttribute ("ActiveRouteTimeout", "Period of time during which the route is considered to be valid",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&RoutingProtocol::m_activeRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("MyRouteTimeout", "Value of lifetime field in RREP generating by this node = 2 * max(ActiveRouteTimeout, PathDiscoveryTime)",
                   TimeValue (Seconds (11.2)),
                   MakeTimeAccessor (&RoutingProtocol::m_myRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("BlackListTimeout", "Time for which the node is put into the blacklist = RreqRetries * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::m_blackListTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("DeletePeriod", "DeletePeriod is intended to provide an upper bound on the time for which an upstream node A "
                   "can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D."
                   " = 5 * max (HelloInterval, ActiveRouteTimeout)",
                   TimeValue (Seconds (15)),
                   MakeTimeAccessor (&RoutingProtocol::m_deletePeriod),
                   MakeTimeChecker ())
    .AddAttribute ("NetDiameter", "Net diameter measures the maximum possible number of hops between two nodes in the network",
                   UintegerValue (35),
                   MakeUintegerAccessor (&RoutingProtocol::m_netDiameter),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NetTraversalTime", "Estimate of the average net traversal time = 2 * NodeTraversalTime * NetDiameter",
                   TimeValue (Seconds (2.8)),
                   MakeTimeAccessor (&RoutingProtocol::m_netTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("PathDiscoveryTime", "Estimate of maximum time needed to find route in network = 2 * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::m_pathDiscoveryTime),
                   MakeTimeChecker ())
    .AddAttribute ("MaxQueueLen", "Maximum number of packets that we allow a routing protocol to buffer.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&RoutingProtocol::SetMaxQueueLen,
                                         &RoutingProtocol::GetMaxQueueLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxQueueTime", "Maximum time packets can be queued (in seconds)",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&RoutingProtocol::SetMaxQueueTime,
                                     &RoutingProtocol::GetMaxQueueTime),
                   MakeTimeChecker ())
    .AddAttribute ("AllowedHelloLoss", "Number of hello messages which may be loss for valid link.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_allowedHelloLoss),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("GratuitousReply", "Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.",
                   BooleanValue (false), // falseにした
                   MakeBooleanAccessor (&RoutingProtocol::SetGratuitousReplyFlag,
                                        &RoutingProtocol::GetGratuitousReplyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("DestinationOnly", "Indicates only the destination may respond to this RREQ.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RoutingProtocol::SetDesinationOnlyFlag,
                                        &RoutingProtocol::GetDesinationOnlyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableHello", "Indicates whether a hello messages enable.",
                   BooleanValue (false), // falseにした
                   MakeBooleanAccessor (&RoutingProtocol::SetHelloEnable,
                                        &RoutingProtocol::GetHelloEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBroadcast", "Indicates whether a broadcast data packets forwarding enable.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetBroadcastEnable,
                                        &RoutingProtocol::GetBroadcastEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("UniformRv", "Access to the underlying UniformRandomVariable",
                   StringValue ("ns3::UniformRandomVariable"),
                   MakePointerAccessor (&RoutingProtocol::m_uniformRandomVariable),
                   MakePointerChecker<UniformRandomVariable> ());
  return tid;
}

void
RoutingProtocol::SetMaxQueueLen (uint32_t len)
{
  m_maxQueueLen = len;
  m_queue.SetMaxQueueLen (len);
  std::cout << "RoutingProtocol::SetMaxQueueLen>" << m_queue.GetMaxQueueLen() << std::endl;
}
void
RoutingProtocol::SetMaxQueueTime (Time t)
{
  m_maxQueueTime = t;
  m_queue.SetQueueTimeout (t);
  std::cout << "RoutingProtocol::SetMaxQueueTime>" << m_queue.GetQueueTimeout() << std::endl;
}

RoutingProtocol::~RoutingProtocol ()
{
}

void
RoutingProtocol::DoDispose ()
{
  //std::cout << "RoutingProtocol::DoDispose" << std::endl;
  m_ipv4 = 0;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter = m_socketAddresses.begin (); iter != m_socketAddresses.end (); iter++) {
    iter->first->Close ();
  }
  m_socketAddresses.clear ();
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter = m_socketSubnetBroadcastAddresses.begin (); iter != m_socketSubnetBroadcastAddresses.end (); iter++){
    iter->first->Close ();
  }
  m_socketSubnetBroadcastAddresses.clear ();
  Ipv4RoutingProtocol::DoDispose ();
}

void
RoutingProtocol::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
  *stream->GetStream () << "Node: " << m_ipv4->GetObject<Node> ()->GetId ()
                        << "; Time: " << Now().As (Time::S)
                        << ", Local time: " << GetObject<Node> ()->GetLocalTime ().As (Time::S)
                        << ", AODV Routing table" << std::endl;
  m_routingTable.Print (stream);
  *stream->GetStream () << std::endl;
}

void
RoutingProtocol::PrintVariable () const
{
  std::cout << "Node: " << m_ipv4->GetObject<Node> ()->GetId () << 
  ", Time: " << Now().As (Time::S) << 
  ", Local time: " << GetObject<Node> ()->GetLocalTime ().As (Time::S) <<
  ", RREQ load:" << m_myLoadReq << 
  ", RREP load:" << m_myLoadRep << 
   std::endl;
}

int64_t
RoutingProtocol::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uniformRandomVariable->SetStream (stream); // 乱数系列の割り当て処理
  return 1;
}

void
RoutingProtocol::Start ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::Start->Routing Protocol START!!!" << std::endl;
  if (m_enableHello) m_nb.ScheduleTimer (); 
  m_rreqRateLimitTimer.SetFunction (&RoutingProtocol::RreqRateLimitTimerExpire, this);
  m_rreqRateLimitTimer.Schedule (Seconds (1));
  m_rerrRateLimitTimer.SetFunction (&RoutingProtocol::RerrRateLimitTimerExpire, this);
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

//----------------------------------------------------------------------------------
// パケットの転送経路を決定する
Ptr<Ipv4Route>
RoutingProtocol::RouteOutput (
  Ptr<Packet> p,               // ルーティングされるパケット
  const Ipv4Header &header,    // ヘッダ入力
  Ptr<NetDevice> oif,          // Output InterFace
  Socket::SocketErrno &sockerr // SOCKETERROR 出力パラメータ
)
{
  NS_LOG_FUNCTION (this << header << (oif ? oif->GetIfIndex () : 0));
  std::cout << ">>>>> RouteOutput <<<<<" << std::endl;

  if (!p){
    //NS_LOG_DEBUG("Packet is == 0");
    std::cout << "Packet is 0" << std::endl;
    return LoopbackRoute (header, oif); // later
  }

  if (m_socketAddresses.empty ()) { // 各ノードごとの未転送のユニキャスト用の
    sockerr = Socket::ERROR_NOROUTETOHOST;
    std::cout << "No aodv interfaces" << std::endl;
    Ptr<Ipv4Route> route;
    return route;
  }

  sockerr = Socket::ERROR_NOTERROR;
  Ptr<Ipv4Route> route;
  Ipv4Address dst = header.GetDestination ();
  std::cout << "[RouteOutput]>>>>>Source:" << header.GetSource () << ", Destination:" << header.GetDestination () << std::endl;
  RoutingTableEntry rt;
  if (m_routingTable.LookupValidRoute (dst, rt)){
    route = rt.GetRoute (); 
    NS_ASSERT (route != 0);
    std::cout << "[RouteOutput]>>>>>Exist route to " << route->GetDestination () << " from interface " << route->GetSource () << std::endl;
    if (oif != 0 && route->GetOutputDevice () != oif){
      std::cout << "Output device doesn't match. Dropped." << std::endl;
      //NS_LOG_DEBUG ("Output device doesn't match. Dropped.");
      sockerr = Socket::ERROR_NOROUTETOHOST;
      return Ptr<Ipv4Route> ();
    }

    //　エントリにおける生存時間
    UpdateRouteLifeTime (dst, m_activeRouteTimeout); // 経路が有効であると考えられる生存時間を更新
    UpdateRouteLifeTime (route->GetGateway (), m_activeRouteTimeout);
    return route;
  }
  // Valid route not found, in this case we return loopback. 
  // Actual route request will be deferred until packet will be fully formed, 
  // routed to loopback, received from loopback and passed to RouteInput (see below)
  // 有効なルートが見つかりません。この場合、ループバックが返されます.パケットが完全に形成され、
  // ループバックにルーティングされ、ループバックから受信され、RouteInputに渡されるまで、実際のルート要求は延期されます.
  uint32_t iif = (oif ? m_ipv4->GetInterfaceForDevice (oif) : -1);
  DeferredRouteOutputTag tag (iif);
  std::cout << "[RouteOutput]>>>>>Valid Route not found" << std::endl;
  if (!p->PeekPacketTag (tag)) p->AddPacketTag (tag);

  return LoopbackRoute (header, oif); // ループバック
}

//-------------------------------------------------------------------
// パケットをキューに入れ、ルート要求を送信する(パケットにタグがあった場合、ここに遷移)
void
RoutingProtocol::DeferredRouteOutput (
  Ptr<const Packet> p, 
  const Ipv4Header & header, 
  UnicastForwardCallback ucb, 
  ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << p << header);
  NS_ASSERT (p != 0 && p != Ptr<Packet> ());

  QueueEntry newEntry (p, header, ucb, ecb); // エントリーを生成
  bool result = m_queue.Enqueue (newEntry); // キューに、同じパケットと宛先エントリを持つエントリがキューあれば
  if (result){
    std::cout << "RoutingProtocol::DeferredRouteOutput->Add packet " << p->GetUid () << " to queue. Protocol " << (uint16_t) header.GetProtocol () << std::endl;;
    //NS_LOG_LOGIC ("Add packet " << p->GetUid () << " to queue. Protocol " << (uint16_t) header.GetProtocol ());
    RoutingTableEntry rt;
    bool result = m_routingTable.LookupRoute (header.GetDestination (), rt); // 経路の探索が可能かどうか
    if(!result || ((rt.GetFlag () != IN_SEARCH) && result)){
      std::cout << "RoutingProtocol::DeferredRouteOutput->Send new RREQ for outbound packet to " << header.GetDestination () << std::endl;
      //NS_LOG_LOGIC ("Send new RREQ for outbound packet to " << header.GetDestination ());
      SendRequest (header.GetDestination ()); // RREQを送信
    }
  }
}

//----------------------------------------------------------------------------
// 受信パケットの配送処理関数
bool
RoutingProtocol::RouteInput (
  Ptr<const Packet> p, 
  const Ipv4Header &header,
  Ptr<const NetDevice> idev, 
  UnicastForwardCallback ucb,
  MulticastForwardCallback mcb, 
  LocalDeliverCallback lcb,
  ErrorCallback ecb
)
{
  NS_LOG_FUNCTION (this << p->GetUid () << header.GetDestination () << idev->GetAddress ());
  std::cout << ">>>>> RouteInput <<<<<" << std::endl;
  
  if (m_socketAddresses.empty ()){
    std::cout << "No Aodv interfaces." << std::endl;
    return false;
  }
  NS_ASSERT (m_ipv4 != 0);
  NS_ASSERT (p != 0);

  // 入力デバイスがIPをサポートしているかどうかを確認する / Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);

  int32_t iif = m_ipv4->GetInterfaceForDevice (idev); // デバイス
  Ipv4Address dst = header.GetDestination (); // 宛先
  Ipv4Address origin = header.GetSource ();   // 送信元
  std::cout << "[RouteInput]>>>>>PacketID:" << p->GetUid () << "Source:" << origin << ", Destination:" << header.GetDestination () << ", NetDeviceAddress:" << idev->GetAddress () << std::endl;

  // ルートリクエストの延期？ / Deferred route request
  if (idev == m_lo){ // m_lo:パケットが完全に形成されるまでRREQを延期するために使用されるループバックデバイス
    DeferredRouteOutputTag tag;
    if (p->PeekPacketTag (tag)){
      DeferredRouteOutput (p, header, ucb, ecb); // RREQ転送に遷移するかも
      return true;
    }
  } // ここの処理が通った場合、パケットの経路が形成されている？？？

  // Duplicate of own packet
  if (IsMyOwnAddress (origin)) return true; 

  // AODV is not a multicast routing protocol
  if (dst.IsMulticast ()) {
    std::cout << "Packet is Multicast" << std::endl;
    return false; 
  }

  std::cout << "[RouteInput]>>>>>Broadcast local delivery/forwarding" << std::endl;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j){
    Ipv4InterfaceAddress iface = j->second; // イテレータのIPアドレスを取得
    if (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()) == iif){ // デバイス番号が一致するか？
      if (dst == iface.GetBroadcast () || dst.IsBroadcast ()){
        if (m_dpd.IsDuplicate (p, header)){ // 重複しているか？
          std::cout << "[RouteInput]>>>>>Duplicated packet " << p->GetUid () << " from " << origin << ". Drop." << std::endl;
          //NS_LOG_DEBUG ("Duplicated packet " << p->GetUid () << " from " << origin << ". Drop.");
          return true;
        }
        UpdateRouteLifeTime (origin, m_activeRouteTimeout); // 生存時間の更新
        Ptr<Packet> packet = p->Copy (); // コピー
        if (lcb.IsNull () == false){ 
          std::cout << "[RouteInput]>>>>>Broadcast local delivery to " << iface.GetLocal () << std::endl;
          //NS_LOG_LOGIC ("Broadcast local delivery to " << iface.GetLocal ());
          lcb (p, header, iif); // ローカル配信(パケットを下位層に渡す？？)
          // Fall through to additional processing
        } else { 
          std::cout << "[RouteInput]>>>>>Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin << std::endl;
          //NS_LOG_ERROR ("Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin);
          ecb (p, header, Socket::ERROR_NOROUTETOHOST); // エラー配信
        }
        if (!m_enableBroadcast) return true; // ブロードキャストが有効じゃない
        if (header.GetProtocol () == UdpL4Protocol::PROT_NUMBER){
          UdpHeader udpHeader; // UDPヘッダ
          p->PeekHeader (udpHeader);
          if (udpHeader.GetDestinationPort () == AODV_PORT){
            std::cout << "[RouteInput]>>>>>ブロードキャストで送信されたAODVパケットはすでに管理している(AODV packets sent in broadcast are already managed.)" << std::endl;
            return true;
          }
        }
        if (header.GetTtl () > 1){ // TTLが1以上である？？？
          std::cout << "[RouteInput]>>>>>Forward broadcast. TTL:" << (uint16_t) header.GetTtl () << std::endl;
          //NS_LOG_LOGIC ("Forward broadcast. TTL " << (uint16_t) header.GetTtl ());
          RoutingTableEntry toBroadcast;
          if (m_routingTable.LookupRoute (dst, toBroadcast)){
            Ptr<Ipv4Route> route = toBroadcast.GetRoute ();
            ucb (route, packet, header); // ユニキャスト(1対1)
          } else{
            std::cout << "No route to forward broadcast. Drop packet id:" << p->GetUid () << std::endl;
            //NS_LOG_DEBUG ("No route to forward broadcast. Drop packet " << p->GetUid ());
          }
        } else{
          // TTLを超えた
          std::cout << "TTL exceeded. Drop packet " << p->GetUid () << std::endl;
          //NS_LOG_DEBUG ("TTL exceeded. Drop packet " << p->GetUid ());
        }
        return true;
      }
    }
  }

  std::cout << "[RouteInput]>>>>>Unicast local delivery" << std::endl;
  if (m_ipv4->IsDestinationAddress (dst, iif)){ // 受信したパケットに対応するアドレスとインタフェースをローカル配信のために受け入れることができるかどうかを判断する
    UpdateRouteLifeTime (origin, m_activeRouteTimeout);
    RoutingTableEntry toOrigin;
    if (m_routingTable.LookupValidRoute (origin, toOrigin)){
      UpdateRouteLifeTime (toOrigin.GetNextHop (), m_activeRouteTimeout);
      m_nb.Update (toOrigin.GetNextHop (), m_activeRouteTimeout);
    }
    if (lcb.IsNull () == false){
      std::cout <<  "[RouteInput]>>>>>Unicast local delivery to " << dst << std::endl;
      lcb (p, header, iif);// RecvAodvに遷移
    }
    else{
      std::cout << "[RouteInput]>>>>>Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin << std::endl;
      ecb (p, header, Socket::ERROR_NOROUTETOHOST); // エラー
    }
    std::cout << "[RouteInput]>>>>>Unicast/local/Error delivery->EXIT" << std::endl;
    return true;
  }

  // Check if input device supports IP forwarding
  if (m_ipv4->IsForwarding (iif) == false){
    std::cout << "[RouteInput]>>>>>Forwarding disabled for this interface" << std::endl;
    //NS_LOG_LOGIC ("Forwarding disabled for this interface");
    ecb (p, header, Socket::ERROR_NOROUTETOHOST);
    return true;
  }

  // 他のノード向けの転送処理
  return Forwarding (p, header, ucb, ecb);
}

//------------------------------------------------
// 使用可能な経路があれば、パケットを転送する
bool
RoutingProtocol::Forwarding (
  Ptr<const Packet> p, 
  const Ipv4Header & header,
  UnicastForwardCallback ucb, 
  ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this);
  Ipv4Address dst = header.GetDestination ();
  Ipv4Address origin = header.GetSource ();
  std::cout << "+++ Forwarding!!!>>>>>Origin:" << origin << ", Destination:" << dst << std::endl;
  
  // ルーティングテーブルを参照して、UnicastForwardCallbackを利用してパケットを宛先へ転送する。
  m_routingTable.Purge (); // 有効期限が切れている全エントリを削除
  RoutingTableEntry toDst;
  if (m_routingTable.LookupRoute (dst, toDst)){
    if (toDst.GetFlag () == VALID){
      Ptr<Ipv4Route> route = toDst.GetRoute (); // Ipv4ルートを取り出す
      std::cout << route->GetSource ()<< " forwarding to " << dst << " from " << origin << " packet " << p->GetUid () << std::endl;
      NS_LOG_LOGIC (route->GetSource ()<<" forwarding to " << dst << " from " << origin << " packet " << p->GetUid ());
      /*
       * Each time a route is used to forward a data packet, its Active Route
       * Lifetime field of the source, destination and the next hop on the
       * path to the destination is updated to be no less than the current
       * time plus ActiveRouteTimeout.
       *******
       * ルートがデータパケットを転送するために使用されるたびに、送信元、宛先、および宛先へのパス上の次のホップの
       * ActiveRouteLifetimeフィールドは、現時刻+ ActiveRouteTimeout以上に更新されます.
       */
      UpdateRouteLifeTime (origin, m_activeRouteTimeout);
      UpdateRouteLifeTime (dst, m_activeRouteTimeout);
      UpdateRouteLifeTime (route->GetGateway (), m_activeRouteTimeout);
      /*
       *  Since the route between each originator and destination pair is expected to be symmetric, the
       *  Active Route Lifetime for the previous hop, along the reverse path back to the IP source, is also updated
       *  to be no less than the current time plus ActiveRouteTimeout
       ******
       * 各発信者と宛先のペアの間のルートが対称であることが予想されるので、
       * 前のホップのアクティブルートライフタイムは、IPソースに戻る逆パスに沿って、
       * 現在の時刻+ ActiveRouteTimeout以上に更新されます。
       */
      RoutingTableEntry toOrigin;
      m_routingTable.LookupRoute (origin, toOrigin); // 経路表から経路を探索する
      UpdateRouteLifeTime (toOrigin.GetNextHop (), m_activeRouteTimeout); // ライフタイムの更新

      m_nb.Update (route->GetGateway (), m_activeRouteTimeout); // 
      m_nb.Update (toOrigin.GetNextHop (), m_activeRouteTimeout);

      ucb (route, p, header); // ユニキャスト
      return true;
    }
    else{
      if (toDst.GetValidSeqNo ()){
        SendRerrWhenNoRouteToForward (dst, toDst.GetSeqNo (), origin); // ルートエラー
        NS_LOG_DEBUG ("Drop packet " << p->GetUid () << " because no route to forward it.");
        return false;
      }
    }
  }
  NS_LOG_LOGIC ("route not found to "<< dst << ". Send RERR message.");
  NS_LOG_DEBUG ("Drop packet " << p->GetUid () << " because no route to forward it.");
  SendRerrWhenNoRouteToForward (dst, 0, origin);
  return false;
}

//----------------------------------------------------------
void
RoutingProtocol::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_ASSERT (ipv4 != 0);
  NS_ASSERT (m_ipv4 == 0);
  std::cout << "RoutingProtocol::SetIpv4" << std::endl;
  
  // フォワーディングテーブルのインスタンスを取得する.プロトコルを開始させる.
  m_ipv4 = ipv4;

  // Create lo route. It is asserted that the only one interface up for now is loopback
  // loルートを作成します。 現在のところ、ただ1つのインタフェースがループバックであると主張されている
  NS_ASSERT (m_ipv4->GetNInterfaces () == 1 && m_ipv4->GetAddress (0, 0).GetLocal () == Ipv4Address ("127.0.0.1"));
  m_lo = m_ipv4->GetNetDevice (0);
  NS_ASSERT (m_lo != 0);
  // Remember lo route
  RoutingTableEntry rt (
    /*device=*/ m_lo, 
    /*dst=*/ Ipv4Address::GetLoopback (),
    /*know seqno=*/ true, 
    /*seqno=*/ 0,
    /*iface=*/ Ipv4InterfaceAddress (Ipv4Address::GetLoopback (), Ipv4Mask ("255.0.0.0")),
    /*hops=*/ 1,
    /*next hop=*/ Ipv4Address::GetLoopback (),
    /*lifetime=*/ Simulator::GetMaximumSimulationTime ()
  );
  m_routingTable.AddRoute (rt);
  Simulator::ScheduleNow (&RoutingProtocol::Start, this);
}

//****************************************************************************
// メソッドはネットワークインターフェースが有効になったときに、
// 自動的に呼び出されるもので、ルーティングプロトコルを実装する際に「起点」だと考えてよい
// 「起点」「起点」「起点」「起点」「起点」「起点」「起点」「起点」「起点」「起点」よ！よ！よ！
//****************************************************************************
void
RoutingProtocol::NotifyInterfaceUp (uint32_t i)
{
  NS_LOG_FUNCTION (this << m_ipv4->GetAddress (i, 0).GetLocal ());
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  std::cout << "##### NotifyInterfaceUp->Node" << m_ipv4->GetObject<Node> ()->GetId () << std::endl;

  if (l3->GetNAddresses (i) > 1){
    std::cout << "AODV does not work with more then one address per each interface." << std::endl;
    NS_LOG_WARN ("AODV does not work with more then one address per each interface.");
  }
  Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
  if (iface.GetLocal () == Ipv4Address ("127.0.0.1")) return;
 
  // Create a socket to listen only on this interface
  // 該当するI/Fの専用ソケットを生成する
  Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket != 0);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv, this));  // ここ大事よ！！！
  socket->Bind (InetSocketAddress (iface.GetLocal (), AODV_PORT)); // インタフェース
  socket->BindToNetDevice (l3->GetNetDevice (i));
  socket->SetAllowBroadcast (true);
  socket->SetIpRecvTtl (true);
  m_socketAddresses.insert (std::make_pair (socket, iface));

  // create also a subnet broadcast socket
  // サブネットブロードキャストソケットも作成する
  socket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket != 0);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv, this));
  socket->Bind (InetSocketAddress (iface.GetBroadcast (), AODV_PORT));
  socket->BindToNetDevice (l3->GetNetDevice (i));
  socket->SetAllowBroadcast (true);
  socket->SetIpRecvTtl (true);
  m_socketSubnetBroadcastAddresses.insert (std::make_pair (socket, iface));
  
  // Add local broadcast record to the routing table
  // ルーティングテーブルのエントリーを設定する
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
  std::string ifType = (dev->IsPointToPoint()) ? "p2p" : "csma"; // adding by shota
  // 経路エントリーを作成する
  RoutingTableEntry rt (
    /*device=*/ dev, 
    /*dst=*/ iface.GetBroadcast (), 
    /*know seqno=*/ true, 
    /*seqno=*/ 0, 
    /*iface=*/ iface,
    /*hops=*/ 1, 
    /*next hop=*/ iface.GetBroadcast (), 
    /*lifetime=*/ Simulator::GetMaximumSimulationTime ()
  );
  m_routingTable.AddRoute (rt); // ルーティングテーブルに登録

  if (l3->GetInterface (i)->GetArpCache ()){
    m_nb.AddArpCache (l3->GetInterface (i)->GetArpCache ());
  }

  // Allow neighbor manager use this interface for layer 2 feedback if possible
  Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
  if (wifi == 0) return;
  Ptr<WifiMac> mac = wifi->GetMac ();
  if (mac == 0) return;

  mac->TraceConnectWithoutContext ("TxErrHeader", m_nb.GetTxErrorCallback ());
}

void
RoutingProtocol::NotifyInterfaceDown (uint32_t i)
{
  NS_LOG_FUNCTION (this << m_ipv4->GetAddress (i, 0).GetLocal ());
  std::cout << "##### NotifyInterfaceDown->Node" << m_ipv4->GetObject<Node> ()->GetId () << std::endl;

  // Disable layer 2 link state monitoring (if possible)
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  Ptr<NetDevice> dev = l3->GetNetDevice (i);
  Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
  if (wifi != 0){
    Ptr<WifiMac> mac = wifi->GetMac ()->GetObject<AdhocWifiMac> ();
    if (mac != 0){
      mac->TraceDisconnectWithoutContext ("TxErrHeader",m_nb.GetTxErrorCallback ());
      m_nb.DelArpCache (l3->GetInterface (i)->GetArpCache ());
    }
  }

  // Close socket 
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (m_ipv4->GetAddress (i, 0));
  NS_ASSERT (socket);
  socket->Close ();
  m_socketAddresses.erase (socket);

  // Close socket
  socket = FindSubnetBroadcastSocketWithInterfaceAddress (m_ipv4->GetAddress (i, 0));
  NS_ASSERT (socket);
  socket->Close ();
  m_socketSubnetBroadcastAddresses.erase (socket);

  if (m_socketAddresses.empty ()){
    std::cout << "No aodv interfaces" << std::endl;
    m_htimer.Cancel ();
    m_nb.Clear ();
    m_routingTable.Clear ();
    return;
  }
  m_routingTable.DeleteAllRoutesFromInterface (m_ipv4->GetAddress (i, 0));
}

void
RoutingProtocol::NotifyAddAddress (uint32_t i, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << " interface " << i << " address " << address);
  std::cout << "##### NotifyAddAddress->Node" << m_ipv4->GetObject<Node> ()->GetId () << ", Interface->" << i << ", Address["<<address<<"]"<< std::endl;

  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  if (!l3->IsUp (i)) return;
  if (l3->GetNAddresses (i) == 1){
    std::cout << "##### NotifyAddAddress(l3->GetNAddresses (i) == 1)" << std::endl;
    Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
    Ptr<Socket> socket = FindSocketWithInterfaceAddress (iface);
    if (!socket){
      if (iface.GetLocal () == Ipv4Address ("127.0.0.1")) return;
      std::cout << "##### NotifyAddAddress(Not Socket)" << std::endl;
      // Create a socket to listen only on this interface
      Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
      NS_ASSERT (socket != 0);
      socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv,this));
      socket->Bind (InetSocketAddress (iface.GetLocal (), AODV_PORT));
      socket->BindToNetDevice (l3->GetNetDevice (i));
      socket->SetAllowBroadcast (true);
      m_socketAddresses.insert (std::make_pair (socket, iface));

      // create also a subnet directed broadcast socket
      socket = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
      NS_ASSERT (socket != 0);
      socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv, this));
      socket->Bind (InetSocketAddress (iface.GetBroadcast (), AODV_PORT));
      socket->BindToNetDevice (l3->GetNetDevice (i));
      socket->SetAllowBroadcast (true);
      socket->SetIpRecvTtl (true);
      m_socketSubnetBroadcastAddresses.insert (std::make_pair (socket, iface));

      // Add local broadcast record to the routing table
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (
          m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
      RoutingTableEntry rt (
        /*device=*/ dev, 
        /*dst=*/ iface.GetBroadcast (), 
        /*know seqno=*/ true,
        /*seqno=*/ 0, 
        /*iface=*/ iface, 
        /*hops=*/ 1,
        /*next hop=*/ iface.GetBroadcast (), 
        /*lifetime=*/ Simulator::GetMaximumSimulationTime ()
        );
      m_routingTable.AddRoute (rt);
    }
  }
  else{
    std::cout << "AODV does not work with more then one address per each interface. Ignore added address" << std::endl;
    NS_LOG_LOGIC ("AODV does not work with more then one address per each interface. Ignore added address");
  }
}

void
RoutingProtocol::NotifyRemoveAddress (uint32_t i, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this);
  std::cout << "##### NotifyRemoveAddress" << std::endl;
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (address);
  if (socket){
    m_routingTable.DeleteAllRoutesFromInterface (address);
    socket->Close ();
    m_socketAddresses.erase (socket);

    Ptr<Socket> unicastSocket = FindSubnetBroadcastSocketWithInterfaceAddress (address);
    if (unicastSocket){
      unicastSocket->Close ();
      m_socketAddresses.erase (unicastSocket);
    }

    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
    if (l3->GetNAddresses (i)){
      Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
      // Create a socket to listen only on this interface
      Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (),
                                                  UdpSocketFactory::GetTypeId ());
      NS_ASSERT (socket != 0);
      socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv, this));
      // Bind to any IP address so that broadcasts can be received
      socket->Bind (InetSocketAddress (iface.GetLocal (), AODV_PORT));
      socket->BindToNetDevice (l3->GetNetDevice (i));
      socket->SetAllowBroadcast (true);
      socket->SetIpRecvTtl (true);
      m_socketAddresses.insert (std::make_pair (socket, iface));

      // create also a unicast socket
      socket = Socket::CreateSocket (GetObject<Node> (),
                                                    UdpSocketFactory::GetTypeId ());
      NS_ASSERT (socket != 0);
      socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAodv, this));
      socket->Bind (InetSocketAddress (iface.GetBroadcast (), AODV_PORT));
      socket->BindToNetDevice (l3->GetNetDevice (i));
      socket->SetAllowBroadcast (true);
      socket->SetIpRecvTtl (true);
      m_socketSubnetBroadcastAddresses.insert (std::make_pair (socket, iface));

      // Add local broadcast record to the routing table
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
      RoutingTableEntry rt (/*device=*/ dev, /*dst=*/ iface.GetBroadcast (), /*know seqno=*/ true, /*seqno=*/ 0, /*iface=*/ iface,
                                        /*hops=*/ 1, /*next hop=*/ iface.GetBroadcast (), /*lifetime=*/ Simulator::GetMaximumSimulationTime ());
      m_routingTable.AddRoute (rt);
    }
    if (m_socketAddresses.empty ()){
      std::cout << "No aodv interfaces" << std::endl;
      //NS_LOG_LOGIC ("No aodv interfaces");
      m_htimer.Cancel ();
      m_nb.Clear ();
      m_routingTable.Clear ();
      return;
    }
  }
  else{
    std::cout << "Remove address not participating in AODV operation" << std::endl;
    //NS_LOG_LOGIC ("Remove address not participating in AODV operation");
  }
}

//-----------------------------------------------------------------------------------------
// パケットが自分のインターフェイスから送信されていることを確認する
bool
RoutingProtocol::IsMyOwnAddress (Ipv4Address src)
{
  NS_LOG_FUNCTION (this << src);
  std::cout << "RoutingProtocol::IsMyOwnAddress->パケットが自分のインターフェイスから送信されていることを確認@@@@@";
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j){
    Ipv4InterfaceAddress iface = j->second;
    if (src == iface.GetLocal ()){ 
      std::cout << "出ている" << std::endl;
      return true; 
    }
  }
  std::cout << "出ていない" << std::endl;
  return false;
}

//--------------------------------------------------------------------------------
// 送信したものが自分に返ってくるようにする処理？？？
Ptr<Ipv4Route> 
RoutingProtocol::LoopbackRoute (const Ipv4Header & hdr, Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << hdr);
  NS_ASSERT (m_lo != 0);
  std::cout << "[LoopbackRoute]->Source:" << hdr.GetSource () << ", Destination:" << hdr.GetDestination () << std::endl;
  Ptr<Ipv4Route> rt = Create<Ipv4Route> (); // IPルートを生成(宛先IP、送信元IP、次ホップアドレス、出力デバイス)
  rt->SetDestination (hdr.GetDestination ()); // IPv4の経路に宛先の入れ込む

  //  Source address selection here is tricky. The loopback route is returned when AODV does not 
  // have a route; this causes the packet to be looped back and handled (cached) in RouteInput() 
  // method while a route is found. However, connection-oriented protocols like TCP need to create 
  // an endpoint four-tuple (src, src port, dst, dst port) and create a pseudo-header for 
  // checksumming. So, AODV needs to guess correctly what the eventual source address will be.
  //  For single interface, single address nodes, this is not a problem. When there are possibly multiple 
  // outgoing interfaces, the policy implemented here is to pick the first available AODV interface.
  // If RouteOutput() caller specified an outgoing interface, that further constrains the selection of source address
  std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin ();
  if (oif){
    // Iterate to find an address on the output interface device.
    for (j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j){
      Ipv4Address addr = j->second.GetLocal (); // IPアドレスのローカルを取得する
      int32_t interface = m_ipv4->GetInterfaceForAddress (addr); // IPv4のIFアドレスを取得
      if (oif == m_ipv4->GetNetDevice (static_cast<uint32_t> (interface))){
        rt->SetSource (addr); // 経路にソースを入れ込む
        break;
      }
    }
  } else {
    rt->SetSource (j->second.GetLocal ()); // 経路のソースに、IPv4IFアドレスのローカルを入れ込む
  }

  NS_ASSERT_MSG (rt->GetSource () != Ipv4Address (), "Valid AODV source address not found");
  rt->SetGateway (Ipv4Address ("127.0.0.1")); // 経路のゲートウェイに127.0.0.1(ローカルホスト)を入れる
  rt->SetOutputDevice (m_lo); // パケットが出来上がるまでのRREQルートを形成
  std::cout << "[LoopbackRoute]->NextHops" << rt->GetGateway() << std::endl;
  return rt;
}

//---------------------------------------------------------------------------------------------------
// RREQを転送
void
RoutingProtocol::SendRequest (
  Ipv4Address dst
)
{
  NS_LOG_FUNCTION ( this << dst);
  std::cout << "############### Send Request ##############" << std::endl;

  // ノードは、1秒あたりRREQ_RATELIMIT RREQメッセージ以上の発信をしてはなりません.
  // A node SHOULD NOT originate more than RREQ_RATELIMIT RREQ messages per second.
  if (m_rreqCount == m_rreqRateLimit){
    std::cout << "Scheduling:" << m_rreqRateLimitTimer.GetDelayLeft () + MicroSeconds (100) << std::endl;
    Simulator::Schedule (m_rreqRateLimitTimer.GetDelayLeft () + MicroSeconds (100), &RoutingProtocol::SendRequest, this, dst);
    return;
  } else { // RREQレート制御に使用されるRREQの数.
    m_rreqCount++; 
    std::cout << "RREQ Rate Control:" << m_rreqCount << std::endl;
  } 

  RreqHeader rreqHeader; // RREQヘッダを作成
  rreqHeader.SetDst (dst); // 宛先を指定

  // Using the Hop field in Routing Table to manage the expanding ring search.
  //std::cout << "Send Request->ルーティングテーブルのホップフィールドを使用して拡張リング検索を管理する." << std::endl;
  RoutingTableEntry rt;
  uint16_t ttl = m_ttlStart; // TTLを取得
  if (m_routingTable.LookupRoute (dst, rt)){ // 経路探索
    // エントリが探索中ではない
    if (rt.GetFlag () != IN_SEARCH) ttl = std::min<uint16_t> (rt.GetHop () + m_ttlIncrement, m_netDiameter);
    else {
      ttl = rt.GetHop () + m_ttlIncrement;
      if (ttl > m_ttlThreshold) ttl = m_netDiameter;
    }
    if (ttl == m_netDiameter) rt.IncrementRreqCnt ();
    if (rt.GetValidSeqNo ()) { 
      rreqHeader.SetDstSeqno (rt.GetSeqNo ()); 
    } else { rreqHeader.SetUnknownSeqno (true); }
    rt.SetHop (ttl);
    rt.SetFlag (IN_SEARCH);
    rt.SetLifeTime (m_pathDiscoveryTime);
    m_routingTable.Update (rt);
  } else { 
    rreqHeader.SetUnknownSeqno (true); 
    Ptr<NetDevice> dev = 0;
    RoutingTableEntry newEntry (
      dev, /*device=*/
      dst, /*dst=*/ 
      false, /*validSeqNo=*/  
      0, /*seqno=*/ 
      Ipv4InterfaceAddress (), /*iface=*/
      ttl, /*hop=*/
      Ipv4Address (), /*nextHop=*/
      m_pathDiscoveryTime /*lifeTime=*/
    );

    // Check if TtlStart == NetDiameter
    if (ttl == m_netDiameter) newEntry.IncrementRreqCnt ();
    newEntry.SetFlag (IN_SEARCH); 
    m_routingTable.AddRoute (newEntry); 
  }

  // 必要がない？？？RREPがノード起点のルート探索にユニキャストする必要があるかどうかを示します.
  if (m_gratuitousReply) {
    std::cout << "SendRequest->Gratuitous Reply" << std::endl;
    rreqHeader.SetGratiousRrep (true); // ヘッダフィールドの予約フラグについて
  }
  // 宛先のみがこのRREQに応答する可能性があることを示します.(多分、Dフラグ)
  if (m_destinationOnly) {
    std::cout << "SendRequest->Destination Only" << std::endl;
    rreqHeader.SetDestinationOnly (true);
  }

  m_seqNo++;                           // シーケンス番号要求
  rreqHeader.SetOriginSeqno (m_seqNo); // 送信元シーケンス番号
  m_requestId++;                       // ブロードキャストID
  rreqHeader.SetId (m_requestId);      // RREQ-ID

  // AODVで使用されている各インターフェイスからRREQをサブネット宛のブロードキャストとして送信します.
  // Send RREQ as subnet directed broadcast from each interface used by aodv
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j) {
    Ptr<Socket> socket = j->first;
    Ipv4InterfaceAddress iface = j->second;

    rreqHeader.SetOrigin (iface.GetLocal ()); // 発信元IPアドレス
    // 単純な重複検出に使用されるユニークなパケット識別キャッシュ.
    // エントリ（addr、id）がキャッシュに存在することを確認します。 エントリが存在しない場合は追加します.
    m_rreqIdCache.IsDuplicate (iface.GetLocal (), m_requestId); // 複製されたRREQを処理する

    Ptr<Packet> packet = Create<Packet> (); // パケット生成
    SocketIpTtlTag tag; // ソケット_IP_TTLタグ
    tag.SetTtl (ttl); // タグにTTLを設定
    packet->AddPacketTag (tag); // パケットにタグをつける
    packet->AddHeader (rreqHeader); // パケットにRREQヘッダを加える
    TypeHeader tHeader (AODVTYPE_RREQ); // メッセージタイプ(RREQ)********************RREQ***************
    packet->AddHeader (tHeader); // パケットにタイプを加える
    std::cout << "Send Request(TypeHeader+++RREQ.1,RREP.2,RERR.3,RREP-ACK.4)->" << tHeader.Get() << std::endl;
    
    // on/32アドレスの場合はすべてのホストブロードキャストに、そうでない場合はsubnet-directedに送信します. Send to all-hosts broadcast if on/32 addr, subnet-directed otherwise.
    Ipv4Address destination; // 宛先アドレス
    if (iface.GetMask () == Ipv4Mask::GetOnes ()){
      destination = Ipv4Address ("255.255.255.255"); // リミテッドブロードキャストアドレス（オール1ブロードキャストアドレス）
    } else {
      destination = iface.GetBroadcast (); // ブロードキャスト？？？
    }
    std::cout << "Send RREQ with id " << rreqHeader.GetId () << " to socket" << std::endl;
    //NS_LOG_DEBUG ("Send RREQ with id " << rreqHeader.GetId () << " to socket");
    m_lastBcastTime = Simulator::Now (); // 最後のブロードキャストを記録する.
    std::cout << "SendRequest->Send To:" << destination << std::endl;
    if(m_myLoadRep == 0)
      Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
    else
      Simulator::Schedule (Time (MilliSeconds (m_myLoadRep * m_uniformRandomVariable->GetInteger (5, 5))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
    //Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
  }
  ScheduleRreqRetry (dst); // 1つの宛先のルート探索時に送信元ノードが繰り返し試行すると、拡張リング検索技術が使用されます.
}

void
RoutingProtocol::SendTo (Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination)
{
  // 指定されたピアにデータを送信する
  std::cout << "##### Send To:" << destination << std::endl;
  socket->SendTo (packet, 0, InetSocketAddress (destination, AODV_PORT));
}

//----------------------------------------------------------------------------------------
// 1つの宛先のルート探索時に送信元ノードが繰り返し試行すると、拡張リング検索技術が使用されます
void
RoutingProtocol::ScheduleRreqRetry (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  std::cout << "RoutingProtocol::ScheduleRreqRetry[リング検索の拡張中...]" << std::endl;
  if (m_addressReqTimer.find (dst) == m_addressReqTimer.end ()) {
    Timer timer (Timer::CANCEL_ON_DESTROY);
    m_addressReqTimer[dst] = timer;
  }
  m_addressReqTimer[dst].SetFunction (&RoutingProtocol::RouteRequestTimerExpire, this);
  m_addressReqTimer[dst].Remove ();
  m_addressReqTimer[dst].SetArguments (dst);
  RoutingTableEntry rt;
  m_routingTable.LookupRoute (dst, rt);
  Time retry;
  if (rt.GetHop () < m_netDiameter){
    std::cout << "ScheduleRreqRetry[リング検索の拡張]->" << 2 * m_nodeTraversalTime * (rt.GetHop () + m_timeoutBuffer) << std::endl;
    retry = 2 * m_nodeTraversalTime * (rt.GetHop () + m_timeoutBuffer);
  } else {
    std::cout << "ScheduleRreqRetry[リング検索の拡張]->バイナリ指数バックオフ->" << std::pow<uint16_t> (2, rt.GetRreqCnt () - 1) * m_netTraversalTime << std::endl;
    // バイナリ指数バックオフ / Binary exponential backoff
    retry = std::pow<uint16_t> (2, rt.GetRreqCnt () - 1) * m_netTraversalTime;
  }
  m_addressReqTimer[dst].Schedule (retry);
  std::cout << "ScheduleRreqRetry[RREQの再試行時間]->Scheduled RREQ retry in " << retry.GetSeconds () << " seconds" << std::endl;
  NS_LOG_LOGIC ("Scheduled RREQ retry in " << retry.GetSeconds () << " seconds");
}

//--------------------------------------------------------------------------------
// ルーティングプロトコルのパケットを専用ソケットより受信した際の処理を行う
void
RoutingProtocol::RecvAodv (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  PrintVariable();
  // インタフェースが利用可能になったら。AODVを制御する、この関数に遷移する
  Address sourceAddress;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddress);
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
  Ipv4Address sender = inetSourceAddr.GetIpv4 ();
  Ipv4Address receiver;

  // IP取得
  if (m_socketAddresses.find (socket) != m_socketAddresses.end ()){
    receiver = m_socketAddresses[socket].GetLocal ();
  } else if (m_socketSubnetBroadcastAddresses.find (socket) != m_socketSubnetBroadcastAddresses.end ()){
    receiver = m_socketSubnetBroadcastAddresses[socket].GetLocal ();
  } else {
    NS_ASSERT_MSG (false, "Received a packet from an unknown socket");
  }
  std::cout << "##### AODVCONTROL->AODV node received a AODV packet from " << sender << " to " << receiver << std::endl;
  NS_LOG_DEBUG ("AODV node " << this << " received a AODV packet from " << sender << " to " << receiver);

  UpdateRouteToNeighbor (sender, receiver); // 隣接ノードのレコードを更新する
  TypeHeader tHeader (AODVTYPE_RREQ); // *******************************RREQに遷移する******************
  packet->RemoveHeader (tHeader); // 多分、ヘッダからTCPヘッダの部分を取り除く
  if (!tHeader.IsValid ()){ // TCPヘッダ見つからなかった場合
    std::cout << "##### AODVCONTROL->AODV message " << packet->GetUid () << " with unknown type received: " << tHeader.Get () << ". Drop" << std::endl;
    NS_LOG_DEBUG ("AODV message " << packet->GetUid () << " with unknown type received: " << tHeader.Get () << ". Drop");
    return;
  }
  switch (tHeader.Get ()){
    case AODVTYPE_RREQ:{
        std::cout << "##### AODVCONTROL->Type=RecvRequest!!!" << std::endl;
        RecvRequest (packet, receiver, sender);
        break;
      }
    case AODVTYPE_RREP:{
        std::cout << "##### AODVCONTROL->Type=RecvReply!!!" << std::endl;
        RecvReply (packet, receiver, sender);
        break;
      }
    case AODVTYPE_RERR:{
        std::cout << "##### AODVCONTROL->Type=RecvError!!!" << std::endl;
        RecvError (packet, sender);
        break;
      }
    case AODVTYPE_RREP_ACK:{
        std::cout << "##### AODVCONTROL->Type=RecvReplyAck!!!" << std::endl;
        RecvReplyAck (sender);
        break;
      }
    }
}

//-----------------------------------------------------
// 経路の維持時間を更新する
bool
RoutingProtocol::UpdateRouteLifeTime (Ipv4Address addr, Time lifetime)
{
  NS_LOG_FUNCTION (this << addr << lifetime);
  std::cout << "RoutingProtocol::UpdateRouteLifeTime" << std::endl;
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (addr, rt)){
    if (rt.GetFlag () == VALID){ // 有効な状態
      std::cout << "[UpdateRouteLifeTime]->Updating VALID route" << std::endl;
      rt.SetRreqCnt (0); 
      rt.SetLifeTime (std::max (lifetime, rt.GetLifeTime ()));
      m_routingTable.Update (rt);
      //std::cout << "RoutingProtocol::UpdateRouteLifeTime->EXIT" << std::endl;
      return true;
    }
  }
  //std::cout << "RoutingProtocol::UpdateRouteLifeTime->EXIT" << std::endl;
  return false;
}

void
RoutingProtocol::UpdateRouteToNeighbor (Ipv4Address sender, Ipv4Address receiver)
{
  NS_LOG_FUNCTION (this << "sender " << sender << " receiver " << receiver);
  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (sender, toNeighbor)){
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    RoutingTableEntry newEntry (
      dev, /*device=*/ 
      sender, /*dst=*/ 
      false, /*know seqno=*/ 
      0, /*seqno=*/ 
      m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0), /*iface=*/
      1, /*hops=*/ 
      sender, /*next hop=*/ 
      m_activeRouteTimeout /*lifetime=*/ 
    );
    m_routingTable.AddRoute (newEntry);
  } else {
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    if (toNeighbor.GetValidSeqNo () && (toNeighbor.GetHop () == 1) && (toNeighbor.GetOutputDevice () == dev)){
      toNeighbor.SetLifeTime (std::max (m_activeRouteTimeout, toNeighbor.GetLifeTime ()));
    } else {
      RoutingTableEntry newEntry (
        /*device=*/ dev, 
        /*dst=*/ sender, 
        /*know seqno=*/ false, 
        /*seqno=*/ 0,
        /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
        /*hops=*/ 1,
        /*next hop=*/ sender, 
        /*lifetime=*/ std::max (m_activeRouteTimeout, toNeighbor.GetLifeTime ())
      );
      m_routingTable.Update (newEntry);
    }
  }
}

//---------------------------------------------------------------------------------------------
// RREQを受信した時の処理
void
RoutingProtocol::RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
{
  NS_LOG_FUNCTION (this);
  std::cout << "Receive Request! RREQ Source:" << src << ", Receiver:" << receiver << std::endl;
  RreqHeader rreqHeader;
  p->RemoveHeader (rreqHeader);

  // A node ignores all RREQs received from any node in its blacklist.
  // ノードはブラックリスト内のノードから受信したすべてのRREQを無視します.
  RoutingTableEntry toPrev;
  std::cout << "RecvRequest->エントリがブラックリストに存在するか確認中..." << std::endl;
  if (m_routingTable.LookupRoute (src, toPrev)){ // 経路の探索
    if (toPrev.IsUnidirectional ()){ // エントリがブラックリストにあるかどうかを判別する
      std::cout << "Ignoring RREQ from node in blacklist:エントリがブラックリストに存在" << std::endl;
      return;
    }
  }

  uint32_t id = rreqHeader.GetId ();
  Ipv4Address origin = rreqHeader.GetOrigin ();

  /*
   *  Node checks to determine whether it has received a RREQ with the same Originator IP Address and RREQ ID.
   *  If such a RREQ has been received, the node silently discards the newly received RREQ.
   *  ノードは、同じOriginator IPアドレスとRREQ IDを持つRREQを受信したかどうかを調べます。
   *  このようなRREQが受信された場合、ノードは新しく受信したRREQを黙って破棄します。
   */
  if (m_rreqIdCache.IsDuplicate (origin, id)){ // 重複しているかどうか？？？
    std::cout << "Ignoring RREQ due to duplicateRREQパケットが重複しているか???" << std::endl;
    //NS_LOG_DEBUG ("Ignoring RREQ due to duplicate");
    return;
  }

  // Increment RREQ hop count
  uint8_t hop = rreqHeader.GetHopCount () + 1;
  rreqHeader.SetHopCount (hop);

  /*
   *  When the reverse route is created or updated, the following actions on the route are also carried out:
   *  1. the Originator Sequence Number from the RREQ is compared to the corresponding destination sequence number
   *     in the route table entry and copied if greater than the existing value there
   *  2. the valid sequence number field is set to true;
   *  3. the next hop in the routing table becomes the node from which the  RREQ was received
   *  4. the hop count is copied from the Hop Count in the RREQ message;
   *  5. the Lifetime is set to be the maximum of (ExistingLifetime, MinimalLifetime), where
   *     MinimalLifetime = current time + 2*NetTraversalTime - 2*HopCount*NodeTraversalTime
   */
  RoutingTableEntry toOrigin;
  if (!m_routingTable.LookupRoute (origin, toOrigin)){
    // 経路が見つからなければ逆ルートを作成
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    RoutingTableEntry newEntry (
      dev, /*device=*/ 
      origin, /*dst=*/  
      true, /*validSeno=*/ 
      rreqHeader.GetOriginSeqno (), /*seqNo=*/
      m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0), /*iface=*/
      hop, /*hops=*/
      src, /*nextHop*/ 
      Time ((2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime)) /*timeLife=*/
    );
    m_routingTable.AddRoute (newEntry);
  } else { 
    // 経路が見つかれば、逆ルートの更新を行う
    if (toOrigin.GetValidSeqNo ()){
      if (int32_t (rreqHeader.GetOriginSeqno ()) - int32_t (toOrigin.GetSeqNo ()) > 0)
        toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());
      } else { toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());}
    toOrigin.SetValidSeqNo (true);
    toOrigin.SetNextHop (src);
    toOrigin.SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
    toOrigin.SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
    toOrigin.SetHop (hop);
    toOrigin.SetLifeTime (std::max (Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime),toOrigin.GetLifeTime ()));
    m_routingTable.Update (toOrigin);
    //m_nb.Update (src, Time (AllowedHelloLoss * HelloInterval));
  }

  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (src, toNeighbor)){
    std::cout << "Neighbor:" << src << " not found in routing table. Creating an entry" << std::endl;
    //NS_LOG_DEBUG ("Neighbor:" << src << " not found in routing table. Creating an entry"); 
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    RoutingTableEntry newEntry (
      dev, 
      src, 
      false, 
      rreqHeader.GetOriginSeqno (),
      m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
      1, 
      src, 
      m_activeRouteTimeout);
    m_routingTable.AddRoute (newEntry);
  } else {
    toNeighbor.SetLifeTime (m_activeRouteTimeout);
    toNeighbor.SetValidSeqNo (false);
    toNeighbor.SetSeqNo (rreqHeader.GetOriginSeqno ()); 
    toNeighbor.SetFlag (VALID);
    toNeighbor.SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
    toNeighbor.SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
    toNeighbor.SetHop (1);
    toNeighbor.SetNextHop (src);
    m_routingTable.Update (toNeighbor);
  }
  m_nb.Update (src, Time (m_allowedHelloLoss * m_helloInterval));

  std::cout << "RecvRequest->" << receiver << " receive RREQ with hop count " << static_cast<uint32_t>(rreqHeader.GetHopCount ()) << ", ID" << rreqHeader.GetId () << " to destination " << rreqHeader.GetDst () << std::endl;
  //  A node generates a RREP if either: (i)  it is itself the destination,
  // 探索したノードが目的地ならば、RREPを返す
  if (IsMyOwnAddress (rreqHeader.GetDst ())){
    m_routingTable.LookupRoute (origin, toOrigin);
    std::cout << "Send reply since I am the destination." << std::endl;
    SendReply (rreqHeader, toOrigin);
    return;
  }

  /*
   * (ii) or it has an active route to the destination, the destination sequence number in the node's existing route table entry for the destination
   *      is valid and greater than or equal to the Destination Sequence Number of the RREQ, and the "destination only" flag is NOT set.
   * または宛先へのアクティブルートを有している場合、宛先のノードの既存のルートテーブルエントリ内の宛先シーケンス番号は有効であり、RREQの宛先シーケンス番号以上であり、「宛先のみ」フラグはセットされていない 。
   */
  RoutingTableEntry toDst;
  Ipv4Address dst = rreqHeader.GetDst ();
  if (m_routingTable.LookupRoute (dst, toDst)){
    // Drop RREQ, This node RREP wil make a loop.
    if (toDst.GetNextHop () == src){
      std::cout <<  "RecvRequest->Drop RREQ from " << src << ", Destination NextHop " << toDst.GetNextHop () << std::endl;
      //NS_LOG_DEBUG ("Drop RREQ from " << src << ", dest next hop " << toDst.GetNextHop ());
      return;
    }
    /*
      * The Destination Sequence number for the requested destination is set to the maximum of the corresponding value
      * received in the RREQ message, and the destination sequence value currently maintained by the node for the requested destination.
      * However, the forwarding node MUST NOT modify its maintained value for the destination sequence number, even if the value
      * received in the incoming RREQ is larger than the value currently maintained by the forwarding node.
      * 要求された宛先の宛先シーケンス番号は、RREQメッセージで受信された対応する値の最大値と、要求された宛先のノードによって現在維持されている宛先シーケンス値とに設定される。
      * しかしながら、着信RREQで受信された値が、転送ノードによって現在維持されている値よりも大きい場合であっても、転送ノードは、宛先シーケンス番号に対するその維持された値を変更してはならない（MUST NOT）。
      */
    if ((rreqHeader.GetUnknownSeqno () || (int32_t (toDst.GetSeqNo ()) - int32_t (rreqHeader.GetDstSeqno ()) >= 0)) && toDst.GetValidSeqNo () ) {
      if (!rreqHeader.GetDestinationOnly () && toDst.GetFlag () == VALID){
        m_routingTable.LookupRoute (origin, toOrigin);
        // 中間ノードによる返信！！！
        SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratiousRrep ());
        return;
      }
      rreqHeader.SetDstSeqno (toDst.GetSeqNo ());
      rreqHeader.SetUnknownSeqno (false);
    }
  }

  SocketIpTtlTag tag;
  p->RemovePacketTag (tag);
  if (tag.GetTtl () < 2){ // TTLをオーバー.
    std::cout << "RecvRequest->TTL exceeded. Drop RREQ origin:" << src << ", Destination:" << dst << std::endl;
    //NS_LOG_DEBUG ("TTL exceeded. Drop RREQ origin " << src << " destination " << dst );
    return;
  }

  std::cout << "RecvRequest->Bload Cast Process" << std::endl;
  // 再度フラッティングするのかな？？？
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j) {
    Ptr<Socket> socket = j->first;
    Ipv4InterfaceAddress iface = j->second;
    Ptr<Packet> packet = Create<Packet> ();
    SocketIpTtlTag ttl;
    ttl.SetTtl (tag.GetTtl () - 1);
    packet->AddPacketTag (ttl);
    packet->AddHeader (rreqHeader);
    TypeHeader tHeader (AODVTYPE_RREQ); // ***********************************RREQ************************
    packet->AddHeader (tHeader);
    // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
    Ipv4Address destination;
    if (iface.GetMask () == Ipv4Mask::GetOnes ()){
      destination = Ipv4Address ("255.255.255.255");
      std::cout << "宛先決定->" << destination << std::endl;
    } else { 
      destination = iface.GetBroadcast ();
      std::cout << "宛先決定->" << destination << std::endl;
    }
    m_lastBcastTime = Simulator::Now (); // ブロードキャスト時間を記録
    std::cout << "RecvRequest->Load:" << m_myLoadRep<< ", Send To:" << destination << std::endl;
    if(m_myLoadRep == 0)
      Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
    else
      Simulator::Schedule (Time (MilliSeconds (m_myLoadRep * m_uniformRandomVariable->GetInteger (5, 5))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
    //Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, packet, destination); 
  }
}

//---------------------------------------------------------------------------------------------
// RREPを送信
void
RoutingProtocol::SendReply (RreqHeader const & rreqHeader, RoutingTableEntry const & toOrigin)
{
  NS_LOG_FUNCTION (this << toOrigin.GetDestination ());
  std::cout << "@@@@@@@@@@@@@@@@@@@@@@@@@ Send Reply!!! @@@@@@@@@@@@@@@@@@@@@@@@@" << std::endl;
  std::cout << "RREQHeader->Source:" << rreqHeader.GetOrigin() << ", Destination:" << rreqHeader.GetDst() << std::endl;

  /*
   * Destination node MUST increment its own sequence number by one if the sequence number in the RREQ packet is equal to that
   * incremented value. Otherwise, the destination does not change its sequence number before generating the  RREP message.
   * 宛先ノードは、RREQパケット内のシーケンス番号がその増分値と等しい場合、自身のシーケンス番号を1だけ増分しなければならない（MUST）。 
   * それ以外の場合、宛先はRREPメッセージを生成する前にシーケンス番号を変更しません。
   */
  if (!rreqHeader.GetUnknownSeqno () && (rreqHeader.GetDstSeqno () == m_seqNo + 1)) m_seqNo++;
  RrepHeader rrepHeader ( 
    0, /*prefixSize=*/
    0, /*hops=*/
    rreqHeader.GetDst (),/*dst=*/
    m_seqNo, /*dstSeqNo=*/
    toOrigin.GetDestination (), /*origin=*/
    m_myRouteTimeout, /*lifeTime=*/
    Ipv4Address() // とりあえず追加
  );
  
  Ptr<Packet> packet = Create<Packet> ();
  SocketIpTtlTag tag;
  tag.SetTtl (toOrigin.GetHop ());
  packet->AddPacketTag (tag);
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AODVTYPE_RREP);
  packet->AddHeader (tHeader);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  NS_ASSERT (socket);
  std::cout << "SendReply->Send To:" << toOrigin.GetNextHop () << std::endl;
  socket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), AODV_PORT));
}

//------------------------------------------------------------------------------------
void
RoutingProtocol::SendReplyByIntermediateNode (RoutingTableEntry & toDst, RoutingTableEntry & toOrigin, bool gratRep)
{
  NS_LOG_FUNCTION (this);

  std::cout << "*** Send Reply By Intermediate Node->中間ノードによってRREPを返す(現在不使用なハズ...)" << std::endl;
  std::cout << "Source:" << toOrigin.GetDestination () <<  ", Destination:" << toDst.GetDestination () << std::endl;

  RrepHeader rrepHeader (
    /*prefix size=*/ 0, 
    /*hops=*/ toDst.GetHop (), 
    /*dst=*/ toDst.GetDestination (), 
    /*dst seqno=*/ toDst.GetSeqNo (),
    /*origin=*/ toOrigin.GetDestination (), 
    /*lifetime=*/ toDst.GetLifeTime (),
    Ipv4Address () // とりあえず追加
  );

  //If the node we received a RREQ for is a neighbor we are
  // probably facing a unidirectional link... Better request a RREP-ack
  if (toDst.GetHop () == 1){
    rrepHeader.SetAckRequired (true);
    RoutingTableEntry toNextHop;
    m_routingTable.LookupRoute (toOrigin.GetNextHop (), toNextHop);
    toNextHop.m_ackTimer.SetFunction (&RoutingProtocol::AckTimerExpire, this);
    toNextHop.m_ackTimer.SetArguments (toNextHop.GetDestination (), m_blackListTimeout);
    toNextHop.m_ackTimer.SetDelay (m_nextHopWait);
  }
  toDst.InsertPrecursor (toOrigin.GetNextHop ());
  toOrigin.InsertPrecursor (toDst.GetNextHop ());
  m_routingTable.Update (toDst);
  m_routingTable.Update (toOrigin);

  Ptr<Packet> packet = Create<Packet> ();
  SocketIpTtlTag tag;
  tag.SetTtl (toOrigin.GetHop ());
  packet->AddPacketTag (tag);
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AODVTYPE_RREP); //******************RREP*********************************
  packet->AddHeader (tHeader);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  NS_ASSERT (socket);
  std::cout << "SendReplyByIntermediateNode->Send To:" << toOrigin.GetNextHop () << std::endl;
  socket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), AODV_PORT));

  // Generating gratuitous RREPs
  if (gratRep){
    RrepHeader gratRepHeader (
      /*prefix size=*/ 0, 
      /*hops=*/ toOrigin.GetHop (), 
      /*dst=*/ toOrigin.GetDestination (),
      /*dst seqno=*/ toOrigin.GetSeqNo (), 
      /*origin=*/ toDst.GetDestination (),
      /*lifetime=*/ toOrigin.GetLifeTime (),
      Ipv4Address ()  // とりあえず追加
    );
    Ptr<Packet> packetToDst = Create<Packet> ();
    SocketIpTtlTag gratTag;
    gratTag.SetTtl (toDst.GetHop ());
    packetToDst->AddPacketTag (gratTag);
    packetToDst->AddHeader (gratRepHeader);
    TypeHeader type (AODVTYPE_RREP);
    packetToDst->AddHeader (type);
    Ptr<Socket> socket = FindSocketWithInterfaceAddress (toDst.GetInterface ());
    NS_ASSERT (socket);
    std::cout << "Send gratuitous RREP " << packet->GetUid () << std::endl;
    NS_LOG_LOGIC ("Send gratuitous RREP " << packet->GetUid ());
    std::cout << "SendReplyByIntermediateNode->Send To:" << toDst.GetNextHop () << std::endl;
    socket->SendTo (packetToDst, 0, InetSocketAddress (toDst.GetNextHop (), AODV_PORT));
  }
}

void
RoutingProtocol::SendReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this << " to " << neighbor);
  std::cout << "RoutingProtocol::SendReplyAck->to " << neighbor << std::endl;
  RrepAckHeader h;
  TypeHeader typeHeader (AODVTYPE_RREP_ACK);
  Ptr<Packet> packet = Create<Packet> ();
  SocketIpTtlTag tag;
  tag.SetTtl (1);
  packet->AddPacketTag (tag);
  packet->AddHeader (h);
  packet->AddHeader (typeHeader);
  RoutingTableEntry toNeighbor;
  m_routingTable.LookupRoute (neighbor, toNeighbor);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toNeighbor.GetInterface ());
  NS_ASSERT (socket);
  std::cout << "SendReplyAck->Send To:" << neighbor << std::endl;
  socket->SendTo (packet, 0, InetSocketAddress (neighbor, AODV_PORT));
}

//----------------------------------------------------------------------------------------------
// RREPを受信
void
RoutingProtocol::RecvReply (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address sender)
{
  NS_LOG_FUNCTION (this << " src " << sender);
  std::cout << "############### Receive Reply!!! ###############" << std::endl;
  IncrementMyLoadRepCnt(); 
  PrintVariable();

  RrepHeader rrepHeader;
  p->RemoveHeader (rrepHeader);
  Ipv4Address dst = rrepHeader.GetDst ();
  std::cout << "RecvReply->Sender:" << sender << ", Receiver:" << receiver << ", Destination:" << dst  << std::endl;
  
  uint8_t hop = rrepHeader.GetHopCount () + 1;
  std::cout << "HopCount:" << unsigned(hop) << std::endl;
  rrepHeader.SetHopCount (hop);
  rrepHeader.AddRouteInfo (sender);
  rrepHeader.PrintRouteInfo ();
  
  if (dst == rrepHeader.GetOrigin ()){ // If RREP is Hello message(もし、Helloマッセージならば...)
    std::cout << "RecvReply->ProcessHello!!!" << std::endl;
    ProcessHello (rrepHeader, receiver); // Helloメッセージの処理に移る
    return;
  }

  /*
   * If the route table entry to the destination is created or updated, then the following actions occur:
   * -  the route is marked as active,
   * -  the destination sequence number is marked as valid,
   * -  the next hop in the route entry is assigned to be the node from which the RREP is received, which is indicated by the source IP address field in the IP header,
   * -  the hop count is set to the value of the hop count from RREP message + 1
   * -  the expiry time is set to the current time plus the value of the Lifetime in the RREP message, and the destination sequence number is the Destination Sequence Number in the RREP message.
   */
  std::cout << "RecvReply->宛先へのルートテーブルエントリが作成または更新される、アクションが発生" << std::endl;
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
  RoutingTableEntry newEntry (
    dev, // device
    dst, // dst 
    true, // validSeqNo 
    rrepHeader.GetDstSeqno (), // seqno
    m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0), // iface
    hop, // hop
    sender, // nextHop 
    rrepHeader.GetLifeTime () // lifeTime
  );
  
  RoutingTableEntry toDst;
  if (m_routingTable.LookupRoute (dst, toDst)){
    // The existing entry is updated only in the following circumstances:
    // (i) the sequence number in the routing table is marked as invalid in route table entry.
    if (!toDst.GetValidSeqNo ()){
      std::cout << "RecvReply->エントリが有効ではない/ルーティングテーブル内のシーケンス番号は、ルートテーブルエントリにおいて無効であるとマーク" << std::endl;
      m_routingTable.Update (newEntry);
    } else if ((int32_t (rrepHeader.GetDstSeqno ()) - int32_t (toDst.GetSeqNo ())) > 0) {
      // (ii)the Destination Sequence Number in the RREP is greater than the node's copy of the destination sequence number and the known value is valid,
      std::cout << "RecvReply->RREPの宛先シーケンス番号が、宛先シーケンス番号のノードのコピーよりも大きく、既知の値が有効" << std::endl;
      m_routingTable.Update (newEntry);
    } else {
      // (iii)the sequence numbers are the same, but the route is marked as inactive.
      if ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (toDst.GetFlag () != VALID)){
        std::cout << "RecvReply->シーケンス番号は同じであるが、経路は非アクティブとしてマーク" << std::endl;
        m_routingTable.Update (newEntry);
      }
      // (iv) the sequence numbers are the same, and the New Hop Count is smaller than the hop count in route table entry.
      else if ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (hop < toDst.GetHop ())){
        std::cout << "RecvReply->シーケンス番号は同じであり、ニューホップカウントはルートテーブルエントリのホップカウントよりも小さい" << std::endl;
        m_routingTable.Update (newEntry);
      }
    }
  } else {
    // The forward route for this destination is created if it does not already exist.
    // この宛先のフォワードルートがまだ存在しない場合は作成されます
    std::cout << "RecvReply->エントリに新しい経路を追加." << std::endl;
    m_routingTable.AddRoute (newEntry);
  }

  // Acknowledge receipt of the RREP by sending a RREP-ACK message back
  // RREP-ACKメッセージを送り返してRREPの受信を確認する
  if (rrepHeader.GetAckRequired ()){
    std::cout << "ReceReply->RREP-ACKメッセージを送り返してRREPの受信を確認.";
    SendReplyAck (sender);
    rrepHeader.SetAckRequired (false);
  }

  std::cout << "RecvReply->Receiver:" << receiver << ", Origin:" << rrepHeader.GetOrigin () << std::endl;
  if (IsMyOwnAddress (rrepHeader.GetOrigin ())){
    if (toDst.GetFlag () == IN_SEARCH){
      m_routingTable.Update (newEntry);
      m_addressReqTimer[dst].Remove ();
      m_addressReqTimer.erase (dst);
    }
    m_routingTable.LookupRoute (dst, toDst);
    std::cout << "RecvReply->SendPacketFromQueue" << std::endl;
    SendPacketFromQueue (dst, toDst.GetRoute ()); // キューからパケットを取り出す
    return;
  }

  RoutingTableEntry toOrigin;
  if (!m_routingTable.LookupRoute (rrepHeader.GetOrigin (), toOrigin) || toOrigin.GetFlag () == IN_SEARCH){
    std::cout << "RecvReply->Impossible! drop." << std::endl;
    return; // Impossible! drop.
  }
  toOrigin.SetLifeTime (std::max (m_activeRouteTimeout, toOrigin.GetLifeTime ()));
  m_routingTable.Update (toOrigin);

  std::cout << "RecvReply->リスト情報の更新(Update information about precursors.)" << std::endl;
  if (m_routingTable.LookupValidRoute (rrepHeader.GetDst (), toDst)){
    toDst.InsertPrecursor (toOrigin.GetNextHop ());
    m_routingTable.Update (toDst);

    RoutingTableEntry toNextHopToDst;
    m_routingTable.LookupRoute (toDst.GetNextHop (), toNextHopToDst);
    toNextHopToDst.InsertPrecursor (toOrigin.GetNextHop ());
    m_routingTable.Update (toNextHopToDst);

    toOrigin.InsertPrecursor (toDst.GetNextHop ());
    m_routingTable.Update (toOrigin);

    RoutingTableEntry toNextHopToOrigin;
    m_routingTable.LookupRoute (toOrigin.GetNextHop (), toNextHopToOrigin);
    toNextHopToOrigin.InsertPrecursor (toDst.GetNextHop ());
    m_routingTable.Update (toNextHopToOrigin);
  }
  SocketIpTtlTag tag;
  p->RemovePacketTag(tag);
  if (tag.GetTtl () < 2) {  // TTLを超えてしまった
    std::cout << "TTL" << unsigned(tag.GetTtl()) << " exceeded. Drop RREP destination " << dst << " origin " << rrepHeader.GetOrigin () << std::endl;
    NS_LOG_DEBUG ("TTL exceeded. Drop RREP destination " << dst << " origin " << rrepHeader.GetOrigin ());
    return;
  }

  Ptr<Packet> packet = Create<Packet> ();
  SocketIpTtlTag ttl;
  ttl.SetTtl (tag.GetTtl() - 1);
  packet->AddPacketTag (ttl);
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AODVTYPE_RREP);
  packet->AddHeader (tHeader);

  Ptr<Packet> p2 = Create<Packet> ();
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigin.GetInterface ());
  NS_ASSERT (socket);
  std::cout << "RecvReply->Send To:" << toOrigin.GetNextHop () << std::endl;
  socket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), AODV_PORT));
}

void
RoutingProtocol::RecvReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this);
  std::cout << "Receive Reply Ack!!!" << std::endl;
  RoutingTableEntry rt;
  if(m_routingTable.LookupRoute (neighbor, rt)){
    rt.m_ackTimer.Cancel ();
    rt.SetFlag (VALID);
    m_routingTable.Update (rt);
  }
}

void
RoutingProtocol::ProcessHello (RrepHeader const & rrepHeader, Ipv4Address receiver )
{
  NS_LOG_FUNCTION (this << "from " << rrepHeader.GetDst ());
  std::cout << "RoutingProtocol::Process Hello from " << rrepHeader.GetDst() << std::endl;
  /*
   *  Whenever a node receives a Hello message from a neighbor, the node
   * SHOULD make sure that it has an active route to the neighbor, and
   * create one if necessary.
   */
  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (rrepHeader.GetDst (), toNeighbor)){
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
    RoutingTableEntry newEntry (
      /*device=*/ dev, 
      /*dst=*/ rrepHeader.GetDst (),
      /*validSeqNo=*/ true, 
      /*seqno=*/ rrepHeader.GetDstSeqno (),
      /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0),
      /*hop=*/ 1, 
      /*nextHop=*/ rrepHeader.GetDst (), 
      /*lifeTime=*/ rrepHeader.GetLifeTime ()
    );
    m_routingTable.AddRoute (newEntry);
  } else {
    toNeighbor.SetLifeTime (std::max (Time (m_allowedHelloLoss * m_helloInterval), toNeighbor.GetLifeTime ()));
    toNeighbor.SetSeqNo (rrepHeader.GetDstSeqno ());
    toNeighbor.SetValidSeqNo (true);
    toNeighbor.SetFlag (VALID);
    toNeighbor.SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
    toNeighbor.SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
    toNeighbor.SetHop (1);
    toNeighbor.SetNextHop (rrepHeader.GetDst ());
    m_routingTable.Update (toNeighbor);
  }
  if (m_enableHello) {
    // Helloメッセージが有効ならば、隣接ノードを更新する
    m_nb.Update (rrepHeader.GetDst (), Time (m_allowedHelloLoss * m_helloInterval));
  }
}

void
RoutingProtocol::RecvError (Ptr<Packet> p, Ipv4Address src )
{
  NS_LOG_FUNCTION (this << " from " << src);
  std::cout << "########### Receive Error!!! from " << src << std::endl;
  RerrHeader rerrHeader;
  p->RemoveHeader (rerrHeader);
  std::map<Ipv4Address, uint32_t> dstWithNextHopSrc;
  std::map<Ipv4Address, uint32_t> unreachable;
  m_routingTable.GetListOfDestinationWithNextHop (src, dstWithNextHopSrc);
  std::pair<Ipv4Address, uint32_t> un;
  while (rerrHeader.RemoveUnDestination (un)){
    for (std::map<Ipv4Address, uint32_t>::const_iterator i = dstWithNextHopSrc.begin (); i != dstWithNextHopSrc.end (); ++i){
      if (i->first == un.first){
        unreachable.insert (un);
      }
    }
  }

  std::vector<Ipv4Address> precursors;
  for (std::map<Ipv4Address, uint32_t>::const_iterator i = unreachable.begin (); i != unreachable.end ();){
    if (!rerrHeader.AddUnDestination (i->first, i->second)){
      TypeHeader typeHeader (AODVTYPE_RERR);
      Ptr<Packet> packet = Create<Packet> ();
      SocketIpTtlTag tag;
      tag.SetTtl (1);
      packet->AddPacketTag (tag);
      packet->AddHeader (rerrHeader);
      packet->AddHeader (typeHeader);
      SendRerrMessage (packet, precursors);
      rerrHeader.Clear ();
    } else {
      RoutingTableEntry toDst;
      m_routingTable.LookupRoute (i->first, toDst);
      toDst.GetPrecursors (precursors);
      ++i;
    }
  }
  if (rerrHeader.GetDestCount () != 0){
    TypeHeader typeHeader (AODVTYPE_RERR);
    Ptr<Packet> packet = Create<Packet> ();
    SocketIpTtlTag tag;
    tag.SetTtl (1);
    packet->AddPacketTag (tag);
    packet->AddHeader (rerrHeader);
    packet->AddHeader (typeHeader);
    SendRerrMessage (packet, precursors);
  }
  m_routingTable.InvalidateRoutesWithDst (unreachable);
}

void
RoutingProtocol::RouteRequestTimerExpire (Ipv4Address dst)
{
  NS_LOG_LOGIC (this);
  std::cout << "RoutingProtocol::RouteRequestTimerExpire->経路探索プロセスを処理" << std::endl;
  RoutingTableEntry toDst;
  if (m_routingTable.LookupValidRoute (dst, toDst)){
    SendPacketFromQueue (dst, toDst.GetRoute ());
    std::cout << "Route to " << dst << " found" << std::endl;
    return;
  }

  // If a route discovery has been attempted RreqRetries times at the maximum TTL without
  // receiving any RREP, all data packets destined for the corresponding destination SHOULD be
  // dropped from the buffer and a Destination Unreachable message SHOULD be delivered to the application.
  if (toDst.GetRreqCnt () == m_rreqRetries) {
    std::cout << "Route discovery to " << dst << " has been attempted RreqRetries (" << m_rreqRetries << ") times with ttl " << m_netDiameter << std::endl;
    //NS_LOG_LOGIC ("route discovery to " << dst << " has been attempted RreqRetries (" << m_rreqRetries << ") times with ttl " << m_netDiameter);
    m_addressReqTimer.erase (dst);
    m_routingTable.DeleteRoute (dst);
    std::cout << "Route not found. Drop all packets with dst " << dst << std::endl;
    //NS_LOG_DEBUG ("Route not found. Drop all packets with dst " << dst);
    m_queue.DropPacketWithDst (dst);
    return;
  }

  if (toDst.GetFlag () == IN_SEARCH){
    std::cout << "Resend RREQ to " << dst << ", Previous TTL->" << toDst.GetHop () << std::endl;
    //NS_LOG_LOGIC ("Resend RREQ to " << dst << " previous ttl " << toDst.GetHop ());
    SendRequest (dst); // ここで、TTLが切れたり、宛先？？が見つからなかった場合、再度再送処理を施している
  } else {
    std::cout << "Route down. Stop search. Drop packet with destination " << dst << std::endl;
    //NS_LOG_DEBUG ("Route down. Stop search. Drop packet with destination " << dst);
    m_addressReqTimer.erase (dst);
    m_routingTable.DeleteRoute (dst);
    m_queue.DropPacketWithDst (dst);
  }
}

void
RoutingProtocol::HelloTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::HelloTimerExpire" << std::endl;
  Time offset = Time (Seconds (0));
  if (m_lastBcastTime > Time (Seconds (0))){
    offset = Simulator::Now () - m_lastBcastTime;
    std::cout << "Hello deferred due to last bcast at:" << m_lastBcastTime << std::endl;
    NS_LOG_DEBUG ("Hello deferred due to last bcast at:" << m_lastBcastTime);
  }
  else{
    SendHello ();
  }
  m_htimer.Cancel ();
  Time diff = m_helloInterval - offset;
  m_htimer.Schedule (std::max (Time (Seconds (0)), diff));
  m_lastBcastTime = Time (Seconds (0));
}

void
RoutingProtocol::RreqRateLimitTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "RREQRateLimitTimerExpire->RREQレート制限タイマをスケジューリング." << 
            "Node:" << m_ipv4->GetObject<Node> ()->GetId () << ", Time:" << Now().As (Time::S) << 
            ", Local time:" << GetObject<Node> ()->GetLocalTime ().As (Time::S) << 
            ", RREQ Load:" << GetMyLoadReq() << ", RREP Load:" << GetMyLoadRep() << std::endl;
  m_rreqCount = 0;
  m_rreqRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::RerrRateLimitTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "RERRRateLimitTimerExpire->RERRレート制限タイマをスケジューリング." << 
              "Node:" << m_ipv4->GetObject<Node> ()->GetId () << ", Time:" << Now().As (Time::S) << 
              ", Local time:" << GetObject<Node> ()->GetLocalTime ().As (Time::S) << std::endl;
  m_rerrCount = 0;
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::AckTimerExpire (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::AckTimerExpire" << std::endl;
  m_routingTable.MarkLinkAsUnidirectional (neighbor, blacklistTimeout);
}

void
RoutingProtocol::SendHello ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::Send Hello!!!!!!" << std::endl;
  /* Broadcast a RREP with TTL = 1 with the RREP message fields set as follows:
   *   Destination IP Address         The node's IP address.
   *   Destination Sequence Number    The node's latest sequence number.
   *   Hop Count                      0
   *   Lifetime                       AllowedHelloLoss * HelloInterval
   */
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j){
    // ブロードキャストしている
    Ptr<Socket> socket = j->first;
    Ipv4InterfaceAddress iface = j->second;
    RrepHeader helloHeader (
      /*prefix size=*/ 0, 
      /*hops=*/ 0, 
      /*dst=*/ iface.GetLocal (), 
      /*dst seqno=*/ m_seqNo,
      /*origin=*/ iface.GetLocal (),
      /*lifetime=*/ Time (m_allowedHelloLoss * m_helloInterval),
      Ipv4Address ()  // とりあえず追加
    );
    Ptr<Packet> packet = Create<Packet> ();
    SocketIpTtlTag tag;
    tag.SetTtl (1);
    packet->AddPacketTag (tag);
    packet->AddHeader (helloHeader);
    TypeHeader tHeader (AODVTYPE_RREP);
    packet->AddHeader (tHeader);
    // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
    Ipv4Address destination;
    if (iface.GetMask () == Ipv4Mask::GetOnes ()){
      destination = Ipv4Address ("255.255.255.255");
    }
    else{ 
      destination = iface.GetBroadcast ();
    }
    Time jitter = Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10)));
    std::cout << "Hello Message Destination:" << iface.GetLocal() << std::endl;
    std::cout << "Jitter:" << jitter << std::endl;
    std::cout << "&&&&&& RoutingProtocol::SendTo" << std::endl;
    Simulator::Schedule (jitter, &RoutingProtocol::SendTo, this , socket, packet, destination);
  }
}

//-----------------------------------------------------------------------------------
void
RoutingProtocol::SendPacketFromQueue (Ipv4Address dst, Ptr<Ipv4Route> route)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::SendPacketFromQueue->パケット転送開始!!!" << std::endl;
  QueueEntry queueEntry;
  while (m_queue.Dequeue (dst, queueEntry)){
    DeferredRouteOutputTag tag;
    Ptr<Packet> p = ConstCast<Packet> (queueEntry.GetPacket ());
    if (p->RemovePacketTag (tag) && tag.GetInterface() != -1 && tag.GetInterface() != m_ipv4->GetInterfaceForDevice (route->GetOutputDevice ())){
      std::cout << "RoutingProtocol::SendPacketFromQueue->Output device doesn't match. Dropped." << std::endl;
      return;
    }
    UnicastForwardCallback ucb = queueEntry.GetUnicastForwardCallback ();
    Ipv4Header header = queueEntry.GetIpv4Header ();
    header.SetSource (route->GetSource ());
    header.SetTtl (header.GetTtl () + 1); // compensate extra TTL decrement by fake loopback routing
    ucb (route, p, header); // ユニキャスト
  }
}

void
RoutingProtocol::SendRerrWhenBreaksLinkToNextHop (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);
  std::cout << "RoutingProtocol::SendRerrWhenBreaksLinkToNextHop" << std::endl;
  RerrHeader rerrHeader;
  std::vector<Ipv4Address> precursors;
  std::map<Ipv4Address, uint32_t> unreachable;

  RoutingTableEntry toNextHop;
  if (!m_routingTable.LookupRoute (nextHop, toNextHop)) return;
  toNextHop.GetPrecursors (precursors);
  rerrHeader.AddUnDestination (nextHop, toNextHop.GetSeqNo ());
  m_routingTable.GetListOfDestinationWithNextHop (nextHop, unreachable);
  for (std::map<Ipv4Address, uint32_t>::const_iterator i = unreachable.begin (); i
       != unreachable.end ();){
    if (!rerrHeader.AddUnDestination (i->first, i->second)){
      NS_LOG_LOGIC ("Send RERR message with maximum size.");
      std::cout << "Send RERR message with maximum size." << std::endl;
      TypeHeader typeHeader (AODVTYPE_RERR);
      std::cout << "Type Header(RREQ:1,RREP:2,RERR:3,RREP_ACK:4)->" << typeHeader.Get() << std::endl;
      Ptr<Packet> packet = Create<Packet> ();
      SocketIpTtlTag tag;
      tag.SetTtl (1);
      packet->AddPacketTag (tag);
      packet->AddHeader (rerrHeader);
      packet->AddHeader (typeHeader);
      SendRerrMessage (packet, precursors);
      rerrHeader.Clear ();
    } else {
      RoutingTableEntry toDst;
      m_routingTable.LookupRoute (i->first, toDst);
      toDst.GetPrecursors (precursors);
      ++i;
    }
  }
  if (rerrHeader.GetDestCount () != 0){
    TypeHeader typeHeader (AODVTYPE_RERR);
    std::cout << "Type Header->" << typeHeader.Get() << std::endl;
    Ptr<Packet> packet = Create<Packet> ();
    SocketIpTtlTag tag;
    tag.SetTtl (1);
    packet->AddPacketTag (tag);
    packet->AddHeader (rerrHeader);
    packet->AddHeader (typeHeader);
    SendRerrMessage (packet, precursors);
  }
  unreachable.insert (std::make_pair (nextHop, toNextHop.GetSeqNo ()));
  std::cout << "SendRerrWhenBreaksLinkToNextHop->ルーティングテーブル更新" << std::endl;
  m_routingTable.InvalidateRoutesWithDst (unreachable);
}

void
RoutingProtocol::SendRerrWhenNoRouteToForward (Ipv4Address dst,
                                               uint32_t dstSeqNo, Ipv4Address origin)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::SendRerrWhenNoRouteToForward" << std::endl;
  // A node SHOULD NOT originate more than RERR_RATELIMIT RERR messages per second.
  if (m_rerrCount == m_rerrRateLimit){
    // Just make sure that the RerrRateLimit timer is running and will expire
    NS_ASSERT (m_rerrRateLimitTimer.IsRunning ());
    // discard the packet and return
    std::cout << "RerrRateLimit reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                              << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds ()
                                              << "; suppressing RERR" << std::endl;
    NS_LOG_LOGIC ("RerrRateLimit reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                              << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds () << "; suppressing RERR");
    return;
  }
  RerrHeader rerrHeader;
  rerrHeader.AddUnDestination (dst, dstSeqNo);
  RoutingTableEntry toOrigin;
  Ptr<Packet> packet = Create<Packet> ();
  SocketIpTtlTag tag;
  tag.SetTtl (1);
  packet->AddPacketTag (tag);
  packet->AddHeader (rerrHeader);
  packet->AddHeader (TypeHeader (AODVTYPE_RERR));
  if (m_routingTable.LookupValidRoute (origin, toOrigin)){
    Ptr<Socket> socket = FindSocketWithInterfaceAddress (
        toOrigin.GetInterface ());
    NS_ASSERT (socket);
    std::cout << "Unicast RERR to the source of the data transmission" << std::endl;
    NS_LOG_LOGIC ("Unicast RERR to the source of the data transmission");
    std::cout << "SendRerrWhenNoRouteToForward->Send To:" << toOrigin.GetNextHop () << std::endl;
    socket->SendTo (packet, 0, InetSocketAddress (toOrigin.GetNextHop (), AODV_PORT));
  } else {
    for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i =
            m_socketAddresses.begin (); i != m_socketAddresses.end (); ++i){
      Ptr<Socket> socket = i->first;
      Ipv4InterfaceAddress iface = i->second;
      NS_ASSERT (socket);
      NS_LOG_LOGIC ("Broadcast RERR message from interface " << iface.GetLocal ());
      // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ()){
        destination = Ipv4Address ("255.255.255.255");
      }
      else{ 
        destination = iface.GetBroadcast ();
      }
      std::cout << "SendRerrWhenNoRouteToForward->Send To:" << destination << std::endl;
      socket->SendTo (packet->Copy (), 0, InetSocketAddress (destination, AODV_PORT));
    }
  }
}

void
RoutingProtocol::SendRerrMessage (Ptr<Packet> packet, std::vector<Ipv4Address> precursors)
{
  NS_LOG_FUNCTION (this);

  std::cout << "RoutingProtocol::SendRerrMessage" << std::endl;

  if (precursors.empty ()){
    std::cout << "SendRerrMessage->NoPrecursors" << std::endl;
    NS_LOG_LOGIC ("No precursors");
    return;
  }
  // A node SHOULD NOT originate more than RERR_RATELIMIT RERR messages per second.
  if (m_rerrCount == m_rerrRateLimit){
    // Just make sure that the RerrRateLimit timer is running and will expire
    NS_ASSERT (m_rerrRateLimitTimer.IsRunning ());
    // discard the packet and return
    std::cout << "RerrRateLimit Reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                              << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds () << "; Suppressing RERR" << std::endl;
    /*NS_LOG_LOGIC ("RerrRateLimit reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                              << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds () << "; suppressing RERR");*/
    return;
  }
  // If there is only one precursor, RERR SHOULD be unicast toward that precursor
  if (precursors.size () == 1){
    std::cout << "前駆体が1つしかない場合、RERRはその前駆体に対してユニキャストされるべきである" << std::endl;
    RoutingTableEntry toPrecursor;
    if (m_routingTable.LookupValidRoute (precursors.front (), toPrecursor)){
      Ptr<Socket> socket = FindSocketWithInterfaceAddress (toPrecursor.GetInterface ());
      NS_ASSERT (socket);
      std::cout << "one precursor => unicast RERR to " << toPrecursor.GetDestination () << " from " << toPrecursor.GetInterface ().GetLocal () << std::endl;
      NS_LOG_LOGIC ("one precursor => unicast RERR to " << toPrecursor.GetDestination () << " from " << toPrecursor.GetInterface ().GetLocal ());
      std::cout << "SendRerrMessage->Send To:" << precursors.front () << std::endl;
      Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, packet, precursors.front ());
      m_rerrCount++;
    }
    return;
  }

  //  Should only transmit RERR on those interfaces which have precursor nodes for the broken route.
  std::cout << "壊れたルートの先行ノードを持つインタフェース上でのみRERRを送信する必要がある." << std::endl;
  std::vector<Ipv4InterfaceAddress> ifaces;
  RoutingTableEntry toPrecursor;
  for (std::vector<Ipv4Address>::const_iterator i = precursors.begin (); i != precursors.end (); ++i){
    if (m_routingTable.LookupValidRoute (*i, toPrecursor) && 
        std::find (ifaces.begin (), ifaces.end (), toPrecursor.GetInterface ()) == ifaces.end ()){
      ifaces.push_back (toPrecursor.GetInterface ());
    }
  }

  for (std::vector<Ipv4InterfaceAddress>::const_iterator i = ifaces.begin (); i != ifaces.end (); ++i){
    Ptr<Socket> socket = FindSocketWithInterfaceAddress (*i);
    NS_ASSERT (socket);
    std::cout << "Broadcast RERR message from interface " << i->GetLocal () << std::endl;
    //NS_LOG_LOGIC ("Broadcast RERR message from interface " << i->GetLocal ());
    // std::cout << "Broadcast RERR message from interface " << i->GetLocal () << std::endl;
    // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
    Ptr<Packet> p = packet->Copy ();
    Ipv4Address destination;
    if (i->GetMask () == Ipv4Mask::GetOnes ()){
      destination = Ipv4Address ("255.255.255.255");
    }
    else{ 
      destination = i->GetBroadcast ();
    }
    std::cout << "SendRerrMessage->Send To:" << destination << std::endl;
    Simulator::Schedule (Time (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10))), &RoutingProtocol::SendTo, this, socket, p, destination);
  }
}

Ptr<Socket>
RoutingProtocol::FindSocketWithInterfaceAddress (Ipv4InterfaceAddress addr ) const
{
  NS_LOG_FUNCTION (this << addr);
  std::cout << "RoutingProtocol::FindSocketWithInterfaceAddress" << std::endl;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
         m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j){
    Ptr<Socket> socket = j->first;
    Ipv4InterfaceAddress iface = j->second;
    if (iface == addr)
      return socket;
  }
  Ptr<Socket> socket;
  return socket;
}

Ptr<Socket>
RoutingProtocol::FindSubnetBroadcastSocketWithInterfaceAddress (Ipv4InterfaceAddress addr ) const
{
  NS_LOG_FUNCTION (this << addr);
  std::cout << "RoutingProtocol::FindSubnetBroadcastSocketWithInterfaceAddress" << std::endl;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
         m_socketSubnetBroadcastAddresses.begin (); j != m_socketSubnetBroadcastAddresses.end (); ++j){
    Ptr<Socket> socket = j->first;
    Ipv4InterfaceAddress iface = j->second;
    if (iface == addr)
      return socket;
  }
  Ptr<Socket> socket;
  return socket;
}

void
RoutingProtocol::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingProtocol::DoInitialize(初期化を実行)" << std::endl;
  uint32_t startTime;
  if (m_enableHello){
    m_htimer.SetFunction (&RoutingProtocol::HelloTimerExpire, this);
    startTime = m_uniformRandomVariable->GetInteger (0, 100);
    NS_LOG_DEBUG ("Starting at time " << startTime << "ms");
    std::cout << "Starting at time " << startTime << "ms" << std::endl;
    m_htimer.Schedule (MilliSeconds (startTime));
  }
  Ipv4RoutingProtocol::DoInitialize ();
}

} //namespace aodv
} //namespace ns3
