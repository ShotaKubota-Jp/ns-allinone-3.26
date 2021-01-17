/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Yufei Cheng
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
 * Author: Yufei Cheng   <yfcheng@ittc.ku.edu>
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

#define NS_LOG_APPEND_CONTEXT                                   \
  if (GetObject<Node> ()) { std::clog << "[node " << GetObject<Node> ()->GetId () << "] "; }

#include <list>
#include <ctime>
#include <map>
#include <limits>
#include <algorithm>
#include <iostream>

#include "ns3/config.h"
#include "ns3/enum.h"
#include "ns3/string.h"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/assert.h"
#include "ns3/uinteger.h"
#include "ns3/net-device.h"
#include "ns3/packet.h"
#include "ns3/boolean.h"
#include "ns3/node-list.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/timer.h"
#include "ns3/object-vector.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/icmpv4-l4-protocol.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/wifi-net-device.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/llc-snap-header.h"
#include "ns3/arp-header.h"
#include "ns3/ipv6-interface.h"

#include "dsr-rreq-table.h"
#include "dsr-rcache.h"
#include "dsr-routing.h"
#include "dsr-fs-header.h"
#include "dsr-options.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("DsrRouting");

namespace dsr {
NS_OBJECT_ENSURE_REGISTERED (DsrRouting);

/* see http://www.iana.org/assignments/protocol-numbers */
const uint8_t DsrRouting::PROT_NUMBER = 48; // ポート番号

/*
 * The extension header is the fixed size dsr header, it is response for recognizing DSR option types
 * and demux to right options to process the packet.
 * The header format with neighboring layers is as follows:
 * 
 * 拡張ヘッダーは固定サイズのdsrヘッダーであり、DSRオプションの種類を認識するための応答であり、
 * パケットを処理するためのdemuxからrightへのオプションです。
 * 隣接するレイヤーのヘッダー形式は次のとおりです。
 *
 +-+-+-+-+-+-+-+-+-+-+-+
 |  Application Header |
 +-+-+-+-+-+-+-+-+-+-+-+
 |   Transport Header  |
 +-+-+-+-+-+-+-+-+-+-+-+
 |   Fixed DSR Header  |
 +---------------------+
 |     DSR Options     |
 +-+-+-+-+-+-+-+-+-+-+-+
 |      IP Header      |
 +-+-+-+-+-+-+-+-+-+-+-+
 */

//----------------------------------------------------------------------------
// ここら辺で,DSRをオプションを設定している
TypeId 
DsrRouting::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::dsr::DsrRouting")
    .SetParent<IpL4Protocol> ()
    .SetGroupName ("Dsr")
    .AddConstructor<DsrRouting> ()
    .AddAttribute ("RouteCache",
                   "The route cache for saving routes from "
                   "route discovery process.",
                   PointerValue (0),
                   MakePointerAccessor (&DsrRouting::SetRouteCache,
                                        &DsrRouting::GetRouteCache),
                   MakePointerChecker<DsrRouteCache> ())
    .AddAttribute ("RreqTable",
                   "The request table to manage route requests.",
                   PointerValue (0),
                   MakePointerAccessor (&DsrRouting::SetRequestTable,
                                        &DsrRouting::GetRequestTable),
                   MakePointerChecker<DsrRreqTable> ())
    .AddAttribute ("PassiveBuffer",
                   "The passive buffer to manage "
                   "promisucously received passive ack.",
                   PointerValue (0),
                   MakePointerAccessor (&DsrRouting::SetPassiveBuffer,
                                        &DsrRouting::GetPassiveBuffer),
                   MakePointerChecker<DsrPassiveBuffer> ())
    .AddAttribute ("MaxSendBuffLen",
                   "Maximum number of packets that can be stored "
                   "in send buffer.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&DsrRouting::m_maxSendBuffLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxSendBuffTime",
                   "Maximum time packets can be queued in the send buffer .",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&DsrRouting::m_sendBufferTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("MaxMaintLen",
                   "Maximum number of packets that can be stored "
                   "in maintenance buffer.",
                   UintegerValue (50),
                   MakeUintegerAccessor (&DsrRouting::m_maxMaintainLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxMaintTime",
                   "Maximum time packets can be queued in maintenance buffer.",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&DsrRouting::m_maxMaintainTime),
                   MakeTimeChecker ())
    .AddAttribute ("MaxCacheLen",
                   "Maximum number of route entries that can be stored "
                   "in route cache.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&DsrRouting::m_maxCacheLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RouteCacheTimeout",
                   "Maximum time the route cache can be queued in "
                   "route cache.",
                   TimeValue (Seconds (300)),
                   MakeTimeAccessor (&DsrRouting::m_maxCacheTime),
                   MakeTimeChecker ())
    .AddAttribute ("MaxEntriesEachDst",
                   "Maximum number of route entries for a "
                   "single destination to respond.",
                   UintegerValue (20),
                   MakeUintegerAccessor (&DsrRouting::m_maxEntriesEachDst),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("SendBuffInterval",
                   "How often to check send buffer for packet with route.",
                   TimeValue (Seconds (500)),
                   MakeTimeAccessor (&DsrRouting::m_sendBuffInterval),
                   MakeTimeChecker ())
    .AddAttribute ("NodeTraversalTime",
                   "The time it takes to traverse two neighboring nodes.",
                   TimeValue (MilliSeconds (40)),                 
                   MakeTimeAccessor (&DsrRouting::m_nodeTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("RreqRetries",
                   "Maximum number of retransmissions for "
                   "request discovery of a route.",
                   UintegerValue (16),
                   MakeUintegerAccessor (&DsrRouting::m_rreqRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaintenanceRetries",
                   "Maximum number of retransmissions for "
                   "data packets from maintenance buffer.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&DsrRouting::m_maxMaintRexmt),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RequestTableSize",
                   "Maximum number of request entries in the request table, "
                   "set this as the number of nodes in the simulation.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&DsrRouting::m_requestTableSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RequestIdSize",
                   "Maximum number of request source Ids in "
                   "the request table.",
                   UintegerValue (16),
                   MakeUintegerAccessor (&DsrRouting::m_requestTableIds),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("UniqueRequestIdSize",
                   "Maximum number of request Ids in "
                   "the request table for a single destination.",
                   UintegerValue (256),
                   MakeUintegerAccessor (&DsrRouting::m_maxRreqId),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NonPropRequestTimeout",
                   "The timeout value for non-propagation request.",
                   TimeValue (MilliSeconds (30)),
                   MakeTimeAccessor (&DsrRouting::m_nonpropRequestTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("DiscoveryHopLimit",
                   "The max discovery hop limit for route requests.",
                   UintegerValue (255),
                   MakeUintegerAccessor (&DsrRouting::m_discoveryHopLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxSalvageCount",
                   "The max salvage count for a single data packet.",
                   UintegerValue (15),
                   MakeUintegerAccessor (&DsrRouting::m_maxSalvageCount),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("BlacklistTimeout",
                   "The time for a neighbor to stay in blacklist.",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&DsrRouting::m_blacklistTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("GratReplyHoldoff",
                   "The time for gratuitous reply entry to expire.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&DsrRouting::m_gratReplyHoldoff),
                   MakeTimeChecker ())
    .AddAttribute ("BroadcastJitter",
                   "The jitter time to avoid collision for broadcast packets.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&DsrRouting::m_broadcastJitter),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("LinkAckTimeout",
                   "The time a packet in maintenance buffer wait for "
                   "link acknowledgment.",
                   TimeValue (MilliSeconds (100)),
                   MakeTimeAccessor (&DsrRouting::m_linkAckTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("TryLinkAcks",
                   "The number of link acknowledgment to use.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DsrRouting::m_tryLinkAcks),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("PassiveAckTimeout",
                   "The time a packet in maintenance buffer wait for "
                   "passive acknowledgment.",
                   TimeValue (MilliSeconds (100)),
                   MakeTimeAccessor (&DsrRouting::m_passiveAckTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("TryPassiveAcks",
                   "The number of passive acknowledgment to use.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&DsrRouting::m_tryPassiveAcks),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RequestPeriod",
                   "The base time interval between route requests.",
                   TimeValue (MilliSeconds (500)),
                   MakeTimeAccessor (&DsrRouting::m_requestPeriod),
                   MakeTimeChecker ())
    .AddAttribute ("MaxRequestPeriod",
                   "The max time interval between route requests.",
                   TimeValue (Seconds (10)),
                   MakeTimeAccessor (&DsrRouting::m_maxRequestPeriod),
                   MakeTimeChecker ())
    .AddAttribute ("GraReplyTableSize",
                   "The gratuitous reply table size.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&DsrRouting::m_graReplyTableSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("CacheType",
                   "Use Link Cache or use Path Cache",
                   StringValue ("LinkCache"),
                   MakeStringAccessor (&DsrRouting::m_cacheType),
                   MakeStringChecker ())
    .AddAttribute ("StabilityDecrFactor",
                   "The stability decrease factor for link cache",
                   UintegerValue (2),
                   MakeUintegerAccessor (&DsrRouting::m_stabilityDecrFactor),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("StabilityIncrFactor",
                   "The stability increase factor for link cache",
                   UintegerValue (4),
                   MakeUintegerAccessor (&DsrRouting::m_stabilityIncrFactor),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("InitStability",
                   "The initial stability factor for link cache",
                   TimeValue (Seconds (25)),
                   MakeTimeAccessor (&DsrRouting::m_initStability),
                   MakeTimeChecker ())
    .AddAttribute ("MinLifeTime",
                   "The minimal life time for link cache",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&DsrRouting::m_minLifeTime),
                   MakeTimeChecker ())
    .AddAttribute ("UseExtends",
                   "The extension time for link cache",
                   TimeValue (Seconds (120)),
                   MakeTimeAccessor (&DsrRouting::m_useExtends),
                   MakeTimeChecker ())
    .AddAttribute ("EnableSubRoute",
                   "Enables saving of sub route when receiving "
                   "route error messages, only available when "
                   "using path route cache",
                   BooleanValue (true),
                   MakeBooleanAccessor (&DsrRouting::m_subRoute),
                   MakeBooleanChecker ())
    .AddAttribute ("RetransIncr",
                   "The increase time for retransmission timer "
                   "when facing network congestion",
                   TimeValue (MilliSeconds (20)),
                   MakeTimeAccessor (&DsrRouting::m_retransIncr),
                   MakeTimeChecker ())
    .AddAttribute ("MaxNetworkQueueSize",
                   "The max number of packet to save in the network queue.",
                   UintegerValue (400),
                   MakeUintegerAccessor (&DsrRouting::m_maxNetworkSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxNetworkQueueDelay",
                   "The max time for a packet to stay in the network queue.",
                   TimeValue (Seconds (30.0)),
                   MakeTimeAccessor (&DsrRouting::m_maxNetworkDelay),
                   MakeTimeChecker ())
    .AddAttribute ("NumPriorityQueues",
                   "The max number of packet to save in the network queue.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&DsrRouting::m_numPriorityQueues),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("LinkAcknowledgment",
                   "Enable Link layer acknowledgment mechanism",
                   BooleanValue (true),
                   MakeBooleanAccessor (&DsrRouting::m_linkAck),
                   MakeBooleanChecker ())
    .AddTraceSource ("Tx",
                     "Send DSR packet.",
                     MakeTraceSourceAccessor (&DsrRouting::m_txPacketTrace),
                     "ns3::dsr::DsrOptionSRHeader::TracedCallback")
    .AddTraceSource ("Drop",
                     "Drop DSR packet",
                     MakeTraceSourceAccessor (&DsrRouting::m_dropTrace),
                     "ns3::Packet::TracedCallback");
  return tid;
}

DsrRouting::DsrRouting ()
{
  NS_LOG_FUNCTION_NOARGS ();
  m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_myRreqLoad = m_myRrepLoad = 0;

  /*
   * The following Ptr statements created objects for all the options header for DSR, and each of them have
   * distinct option number assigned, when DSR Routing received a packet from higher layer, it will find
   * the following options based on the option number, and pass the packet to the appropriate option to
   * process it. After the option processing, it will pass the packet back to DSR Routing to send down layer.
   */
  Ptr<dsr::DsrOptionPad1> pad1Option = CreateObject<dsr::DsrOptionPad1> ();
  Ptr<dsr::DsrOptionPadn> padnOption = CreateObject<dsr::DsrOptionPadn> ();
  Ptr<dsr::DsrOptionRreq> rreqOption = CreateObject<dsr::DsrOptionRreq> ();
  Ptr<dsr::DsrOptionRrep> rrepOption = CreateObject<dsr::DsrOptionRrep> ();
  Ptr<dsr::DsrOptionSR>   srOption = CreateObject<dsr::DsrOptionSR> ();
  Ptr<dsr::DsrOptionRerr>   rerrOption = CreateObject<dsr::DsrOptionRerr> ();
  Ptr<dsr::DsrOptionAckReq> ackReq = CreateObject<dsr::DsrOptionAckReq> ();
  Ptr<dsr::DsrOptionAck> ack = CreateObject<dsr::DsrOptionAck> ();

  // キューにオプションを入れ込んでいる？？？
  Insert (pad1Option); // パッドを1個
  Insert (padnOption); // パッドをn個
  Insert (rreqOption); // RREQ(ルートリクエスト)
  Insert (rrepOption); // RREP(ルートリプライ)
  Insert (srOption);   // SR(ソースルート)
  Insert (rerrOption); // RERR(ルートエラー)
  Insert (ackReq);     // 
  Insert (ack);        // 

  // パケット送信用の送信バッファを確認する / Check the send buffer for sending packets
  m_sendBuffTimer.SetFunction (&DsrRouting::SendBuffTimerExpire, this); // バッファの有効期限を設定する
  m_sendBuffTimer.Schedule (Seconds (100)); 
}

DsrRouting::~DsrRouting ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

//---------------------------------------------------------------------------------------------
void
DsrRouting::NotifyNewAggregate ()
{
  NS_LOG_FUNCTION (this << "NotifyNewAggregate");
  //std::cout << "DsrRouting::NotifyNewAggregate" << std::endl;
  if (m_node == 0) {
    Ptr<Node> node = this->GetObject<Node> ();
    if (node != 0) {
      m_ipv4 = this->GetObject<Ipv4L3Protocol> ();
      if (m_ipv4 != 0) {
        this->SetNode (node);
        m_ipv4->Insert (this);
        this->SetDownTarget (MakeCallback (&Ipv4L3Protocol::Send, m_ipv4));
      }
      m_ip = node->GetObject<Ipv4> ();
      //if (m_ip != 0) std::cout << "DsrRouting::NotifyNewAggregate->Ipv4Started" << std::endl;
    }
  }
  IpL4Protocol::NotifyNewAggregate ();
  Simulator::ScheduleNow (&DsrRouting::Start, this);
}

//--------------------------------------------------------------
// ルーティングプロトコルの開始処理
void 
DsrRouting::Start ()
{
  NS_LOG_FUNCTION (this << "Start DSR Routing protocol");
  std::cout << "DsrRouting::Start->Start DSR Routing protocol" << std::endl;
  //std::cout << "DsrRouting::Start->The number of network queues " << m_numPriorityQueues << std::endl;

  for (uint32_t i = 0; i < m_numPriorityQueues; i++) {
    // ネットワークキューの最大サイズと遅延を設定する / Set the network queue max size and the delay
    //std::cout << "DsrRouting::Start->NetworkQueueSize:" << m_maxNetworkSize << ", QueueDelay:" << m_maxNetworkDelay.GetSeconds () << std::endl;
    Ptr<dsr::DsrNetworkQueue> queue_i = CreateObject<dsr::DsrNetworkQueue> (m_maxNetworkSize,m_maxNetworkDelay);
    std::pair<std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator, bool> result_i = m_priorityQueue.insert (std::make_pair (i, queue_i));
    NS_ASSERT_MSG (result_i.second, "Error in creating queues");
  }

  Ptr<dsr::DsrRreqTable> rreqTable = CreateObject<dsr::DsrRreqTable> (); // 空のRREQテーブルを作成

  // Set the initial hop limit / 初期ホップ限界を設定する
  rreqTable->SetInitHopLimit (m_discoveryHopLimit);

  // Configure the request table parameters / リクエストテーブルのパラメータを設定する
  rreqTable->SetRreqTableSize (m_requestTableSize); // RREQテーブルのサイズ
  rreqTable->SetRreqIdSize (m_requestTableIds);     // RREQテーブルIDのサイズ
  rreqTable->SetUniqueRreqIdSize (m_maxRreqId);     // RREQテーブルのユニークIDのサイズ
  SetRequestTable (rreqTable);                      // RREQテーブルを設定

  // Set the passive buffer parameters using just the send buffer parameters
  // 送信バッファのパラメータだけを使ってパッシブバッファのパラメータを設定する
  Ptr<dsr::DsrPassiveBuffer> passiveBuffer = CreateObject<dsr::DsrPassiveBuffer> ();
  passiveBuffer->SetMaxQueueLen (m_maxSendBuffLen);
  passiveBuffer->SetPassiveBufferTimeout (m_sendBufferTimeout);
  SetPassiveBuffer (passiveBuffer);

  // Set the send buffer parameters / 送信バッファのパラメータを設定する
  m_sendBuffer.SetMaxQueueLen (m_maxSendBuffLen);
  m_sendBuffer.SetSendBufferTimeout (m_sendBufferTimeout);

  // Set the error buffer parameters using just the send buffer parameters
  // 送信バッファのパラメータだけを使ってエラーバッファのパラメータを設定する
  m_errorBuffer.SetMaxQueueLen (m_maxSendBuffLen);
  m_errorBuffer.SetErrorBufferTimeout (m_sendBufferTimeout);

  // Set the maintenance buffer parameters / メンテナンスバッファのパラメータを設定する
  m_maintainBuffer.SetMaxQueueLen (m_maxMaintainLen);
  m_maintainBuffer.SetMaintainBufferTimeout (m_maxMaintainTime);
  
  // Set the gratuitous reply table size / 無償の応答テーブルサイズを設定する
  m_graReply.SetGraTableSize (m_graReplyTableSize);

  if (m_mainAddress == Ipv4Address ()){
    Ipv4Address loopback ("127.0.0.1");
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); i++) {
      // 複数の場合はプライマリアドレスを使用する /  Use primary address, if multiple
      Ipv4Address addr = m_ipv4->GetAddress (i, 0).GetLocal ();
      m_broadcast = m_ipv4->GetAddress (i, 0).GetBroadcast ();

      if (addr != loopback) {
        // Set dsr route cache / DSRのルートキャッシュを設定する
        Ptr<dsr::DsrRouteCache> routeCache = CreateObject<dsr::DsrRouteCache> ();

        // Configure the path cache parameters / パスキャッシュのパラメータを設定する
        routeCache->SetCacheType (m_cacheType);
        routeCache->SetSubRoute (m_subRoute);
        routeCache->SetMaxCacheLen (m_maxCacheLen);
        routeCache->SetCacheTimeout (m_maxCacheTime);
        routeCache->SetMaxEntriesEachDst (m_maxEntriesEachDst);

        // Parameters for link cache / リンクキャッシュのパラメータ
        routeCache->SetStabilityDecrFactor (m_stabilityDecrFactor);
        routeCache->SetStabilityIncrFactor (m_stabilityIncrFactor);
        routeCache->SetInitStability (m_initStability);
        routeCache->SetMinLifeTime (m_minLifeTime);
        routeCache->SetUseExtends (m_useExtends);
        routeCache->ScheduleTimer ();

        // The call back to handle link error and send error message to appropriate nodes
        /// TODO whether this SendRerrWhenBreaksLinkToNextHop is used or not
        SetRouteCache (routeCache);

        // Set the main address as the current ip address / メインアドレスを現在のIPアドレスに設定する
        m_mainAddress = addr;
        std::cout << "DsrRouting::Start->[Node's IpAddress]:" << addr << std::endl;

        // 無差別に他のノード宛のデータパケットを受信
        m_ipv4->GetNetDevice (1)->SetPromiscReceiveCallback (MakeCallback (&DsrRouting::PromiscReceive, this));

        // Allow neighbor manager use this interface for layer 2 feedback if possible
        // 可能であれば、ネイバーマネージャがレイヤ2のフィードバックにこのインターフェイスを使用できるようにする
        Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (addr));
        Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
        if (wifi == 0) break;
        Ptr<WifiMac> mac = wifi->GetMac ();
        if (mac == 0) break;

        // 全ノードの位置情報を把握する
        Ptr<ns3::MobilityModel> mobility = GetNode()->GetObject<ns3::MobilityModel> ();
        Vector vecPos = mobility->GetPosition ();
        //std::cout << "Positioning->X:" << vecPos.x << ", Y:" << vecPos.y << "Z:" << vecPos.z << std::endl;
        Ptr<ns3::Application> app;
        app->g_myNodeNum++;
        //std::cout << "Number of nodes:" << app->g_myNodeNum << std::endl;
        // グローバル変数にノードごとの負荷を入れる領域を確保する
        std::map<Ipv4Address, int>::iterator m_prov = app->g_myNodeLoad.find(m_mainAddress);
        if(m_prov == app->g_myNodeLoad.end()){
          std::vector<std::vector<Ipv4Address> > m_vv1;
          std::vector<Ipv4Address> m_v1;
          m_vv1.push_back(m_v1);
          app->g_myNodeLoad.insert(std::make_pair(m_mainAddress, 0)); // キーに自ノードのIPアドレスがなければ、領域を確保
          app->g_myRouteInfomation.insert(std::make_pair(m_mainAddress, m_vv1));  // 経路情報
          std::vector<int> m_vec1;
          m_vec1.push_back(vecPos.x); // X
          m_vec1.push_back(vecPos.y); // Y
          app->g_myNodePosition.insert(std::make_pair(m_mainAddress, m_vec1));
          //std::cout << "DsrRouting::Start(負荷情報を追加)->Add IPAddress:[" << m_mainAddress << "] to global variable." << std::endl;
        }

        /*std::cout << "[Load Information]->";
        for(std::map<Ipv4Address, uint8_t>::const_iterator it = app->g_myNodeLoad.begin(); it != app->g_myNodeLoad.end(); ++it) {
          std::cout << "IPAddress:" << it->first << ", Load:" <<  unsigned(it->second) << " / ";
        } std::cout << std::endl;
        std::cout << "[Route Information]->" << std::endl;
        for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address>>>::const_iterator i = app->g_myRouteInfomation.begin (); i != app->g_myRouteInfomation.end (); ++i){
          std::vector<std::vector<Ipv4Address>> m_vv2 = i->second;
          std::cout << "IPAddress:" << i->first << ", Load:";
          for(int j=0;j<m_vv2.size();j++){
            for(int z=0; z<m_vv2[j].size(); z++) std::cout << m_vv2[j][z] << ", ";
          } std::cout << std::endl;
        }*/

        routeCache->AddArpCache (m_ipv4->GetInterface (i)->GetArpCache ());
        std::cout << "DsrRouting::Start->Starting DSR on node:" << m_mainAddress << std::endl;
        break;
      }
    }
    NS_ASSERT (m_mainAddress != Ipv4Address () && m_broadcast != Ipv4Address ());
  }
}

//----------------------------------------------------------------------------------------------------
Ptr<NetDevice>
DsrRouting::GetNetDeviceFromContext (std::string context)
{
  //std::cout << "DsrRouting::GetNetDeviceFromContext" << std::endl;
  // Use "NodeList/*/DeviceList/*/ as reference.
  // where element [1] is the Node Id. element [2] is the NetDevice Id.
  std::vector <std::string> elements = GetElementsFromContext (context);
  Ptr<Node> n = NodeList::GetNode (atoi (elements[1].c_str ()));
  NS_ASSERT (n);
  return n->GetDevice (atoi (elements[3].c_str ()));
}

std::vector<std::string>
DsrRouting::GetElementsFromContext (std::string context)
{
  std::vector <std::string> elements;
  size_t pos1 = 0, pos2;
  while (pos1 != context.npos) {
    pos1 = context.find ("/",pos1);
    pos2 = context.find ("/",pos1 + 1);
    elements.push_back (context.substr (pos1 + 1,pos2 - (pos1 + 1)));
    pos1 = pos2;
  }
  return elements;
}

//---------------------------------------------------------------------------------------------
void
DsrRouting::DoDispose (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_node = 0;
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); i++) {
    // Disable layer 2 link state monitoring (if possible)
    Ptr<NetDevice> dev = m_ipv4->GetNetDevice (i);
    Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
    if (wifi != 0) {
      Ptr<WifiMac> mac = wifi->GetMac ()->GetObject<AdhocWifiMac> ();
      if (mac != 0) {
        mac->TraceDisconnectWithoutContext ("TxErrHeader", m_routeCache->GetTxErrorCallback ());
        m_routeCache->DelArpCache (m_ipv4->GetInterface (i)->GetArpCache ());
      }
    }
  }
  IpL4Protocol::DoDispose ();
}

//--------------------------------------------------------------
void
DsrRouting::SetNode (Ptr<Node> node)
{
  //std::cout << "DsrRouting::SetNode" << std::endl;
  m_node = node;
}

//--------------------------------------------------------------
Ptr<Node>
DsrRouting::GetNode () const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_node;
}

//--------------------------------------------------------------
void 
DsrRouting::SetRouteCache (Ptr<dsr::DsrRouteCache> r)
{
  // / Set the route cache to use
  m_routeCache = r;
}

//--------------------------------------------------------------
Ptr<dsr::DsrRouteCache>
DsrRouting::GetRouteCache () const
{
  // / Get the route cache to use
  return m_routeCache;
}

//--------------------------------------------------------------
void 
DsrRouting::SetRequestTable (Ptr<dsr::DsrRreqTable> q)
{
  //std::cout << "DsrRouting::SetRequestTable" << std::endl;
  // / Set the request table to use
  m_rreqTable = q;
}

//--------------------------------------------------------------
Ptr<dsr::DsrRreqTable>
DsrRouting::GetRequestTable () const
{
  // / Get the request table to use
  return m_rreqTable;
}

//--------------------------------------------------------------
void 
DsrRouting::SetPassiveBuffer (Ptr<dsr::DsrPassiveBuffer> p)
{
  //std::cout << "DsrRouting::SetPassiveBuffer" << std::endl;
  // / Set the request table to use
  m_passiveBuffer = p;
}

//--------------------------------------------------------------
Ptr<dsr::DsrPassiveBuffer>
DsrRouting::GetPassiveBuffer () const
{
  // / Get the request table to use
  return m_passiveBuffer;
}

//--------------------------------------------------------------
Ptr<Node>
DsrRouting::GetNodeWithAddress (Ipv4Address ipv4Address)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  int32_t nNodes = NodeList::GetNNodes ();
  for (int32_t i = 0; i < nNodes; ++i) {
    Ptr<Node> node = NodeList::GetNode (i);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    int32_t ifIndex = ipv4->GetInterfaceForAddress (ipv4Address);
    if (ifIndex != -1) return node;
  }
  return 0;
}

//--------------------------------------------------------------
bool 
DsrRouting::IsLinkCache ()
{
  return m_routeCache->IsLinkCache ();
}

//--------------------------------------------------------------
void 
DsrRouting::UseExtends (DsrRouteCacheEntry::IP_VECTOR rt)
{
  std::cout << "DsrRouting::UseExtends" << std::endl;
  m_routeCache->UseExtends (rt);
}

//--------------------------------------------------------------
bool 
DsrRouting::LookupRoute (Ipv4Address id, DsrRouteCacheEntry & rt)
{
  std::cout << "DsrRouting::LookupRoute->ID:" << id << std::endl;
  return m_routeCache->LookupRoute (id, rt);
}

//--------------------------------------------------------------
bool 
DsrRouting::AddRoute_Link (DsrRouteCacheEntry::IP_VECTOR nodelist, Ipv4Address source)
{
  Ipv4Address nextHop = SearchNextHop (source, nodelist);
  m_errorBuffer.DropPacketForErrLink (source, nextHop);
  std::cout << "DsrRouting::AddRoute_Link->Source:" << source << ", NextHop:" << nextHop << std::endl;
  return m_routeCache->AddRoute_Link (nodelist, source);
}

//--------------------------------------------------------------
bool 
DsrRouting::AddRoute (DsrRouteCacheEntry & rt)
{
  std::cout << "DsrRouting::AddRoute" << std::endl;
  std::vector<Ipv4Address> nodelist = rt.GetVector ();
  Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodelist);
  m_errorBuffer.DropPacketForErrLink (m_mainAddress, nextHop);
  return m_routeCache->AddRoute (rt);
}

//--------------------------------------------------------------
void 
DsrRouting::DeleteAllRoutesIncludeLink (Ipv4Address errorSrc, Ipv4Address unreachNode, Ipv4Address node)
{
  std::cout << "DsrRouting::DeleteAllRoutesIncludeLink" << std::endl;
  m_routeCache->DeleteAllRoutesIncludeLink (errorSrc, unreachNode, node);
}

//--------------------------------------------------------------
bool 
DsrRouting::UpdateRouteEntry (Ipv4Address dst)
{
  std::cout << "DsrRouting::UpdateRouteEntry->IPAddress:" << dst << std::endl;
  return m_routeCache->UpdateRouteEntry (dst);
}

//--------------------------------------------------------------
bool 
DsrRouting::FindSourceEntry (Ipv4Address src, Ipv4Address dst, uint16_t id)
{
  return m_rreqTable->FindSourceEntry (src, dst, id);
}

//--------------------------------------------------------------
Ipv4Address
DsrRouting::GetIPfromMAC (Mac48Address address)
{
  NS_LOG_FUNCTION (this << address);
  int32_t nNodes = NodeList::GetNNodes ();
  for (int32_t i = 0; i < nNodes; ++i) {
    Ptr<Node> node = NodeList::GetNode (i);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    Ptr<NetDevice> netDevice = ipv4->GetNetDevice (1);
    if (netDevice->GetAddress () == address) return ipv4->GetAddress (1, 0).GetLocal ();
  }
  return 0;
}

//--------------------------------------------------------------
void 
DsrRouting::PrintVector (std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this);
  if (!vec.size ()) {
    std::cout << "DsrRouting::PrintVector->The vector is empty." << std::endl;
  } else {
    //std::cout << "DsrRouting::PrintVector->Print all the elements in a vector" << std::endl;
    std::cout << "DsrRouting::PrintVector->IPAddress:[";
    for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i) {
      std::cout << *i << ", ";
    } std::cout << "]" << std::endl;
  }
}

//--------------------------------------------------------------
void 
DsrRouting::PrintMyLoad ()
{
  std::cout << "@@@@@ Node's_IPAddress" << m_mainAddress << /*", RREQLoad:" << unsigned(m_myRreqLoad) <<*/ ", RREPLoad:" << unsigned(m_myRrepLoad) << std::endl;
}

//--------------------------------------------------------------
// 経路の次のホップを取得する
Ipv4Address 
DsrRouting::SearchNextHop (Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this << ipv4Address);
  Ipv4Address nextHop;
  std::cout << "DsrRouting::SearchNextHop->IPAddress" << ipv4Address << ", VectorSize:" << vec.size () << std::endl;
  if (vec.size () == 2) {
    std::cout << "DsrRouting::SearchNextHop->The two nodes are neighbors" << std::endl;
    nextHop = vec[1];
    return nextHop;
  } else {
    if (ipv4Address == vec.back ()) { // 最終的な宛先にたどり着く 
      std::cout << "DsrRouting::SearchNextHop->We have reached to the final destination " << ipv4Address << " " << vec.back () << std::endl;
      return ipv4Address;
    }
    for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i) {
      if (ipv4Address == (*i)) {
        nextHop = *(++i);
        return nextHop;
      }
    }
  }
  std::cout << "DsrRouting::SearchNextHop->Next hop address not found" << std::endl;
  Ipv4Address none = "0.0.0.0";
  return none;
}

//--------------------------------------------------------------
Ptr<Ipv4Route>
DsrRouting::SetRoute (Ipv4Address nextHop, Ipv4Address srcAddress)
{
  NS_LOG_FUNCTION (this << nextHop << srcAddress);
  std::cout << "DsrRouting::SetRoute->Source:" << srcAddress << ", NextHop:" << nextHop << std::endl;
  m_ipv4Route = Create<Ipv4Route> ();
  m_ipv4Route->SetDestination (nextHop);
  m_ipv4Route->SetGateway (nextHop);
  m_ipv4Route->SetSource (srcAddress);
  return m_ipv4Route;
}

//--------------------------------------------------------------
int
DsrRouting::GetProtocolNumber (void) const
{
  return PROT_NUMBER;
}

//--------------------------------------------------------------
uint16_t
DsrRouting::GetIDfromIP (Ipv4Address address)
{
  int32_t nNodes = NodeList::GetNNodes ();
  for (int32_t i = 0; i < nNodes; ++i) {
    Ptr<Node> node = NodeList::GetNode (i);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    if (ipv4->GetAddress (1, 0).GetLocal () == address) {
      return uint16_t (i);
    }
  }
  return 256;
}

Ipv4Address
DsrRouting::GetIPfromID (uint16_t id)
{
  if (id >= 256) {
    std::cout << "DsrRouting::GetIPfromID->Exceed the node range." << std::endl;
    return "0.0.0.0";
  } else {
    Ptr<Node> node = NodeList::GetNode (uint32_t (id));
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    return ipv4->GetAddress (1, 0).GetLocal ();
  }
}

uint32_t
DsrRouting::GetPriority (DsrMessageType messageType)
{
  if (messageType == DSR_CONTROL_PACKET) return 0;
  else return 1;
}

//-----------------------------------------------------------------------
// 送信バッファタイマーの有効期限が切れる
void 
DsrRouting::SendBuffTimerExpire ()
{
  std::cout << "DsrRouting::SendBuffTimerExpire" << std::endl;
  if (m_sendBuffTimer.IsRunning ()) {
    m_sendBuffTimer.Cancel ();
  }
  m_sendBuffTimer.Schedule (m_sendBuffInterval); // 経路パケットの送信バッファのチェック間隔
  CheckSendBuffer ();
}

//----------------------------------------------------------------------
// 送信バッファタイマーの有効期限が切れたときに、経路が持つパケットの送信バッファをチェックする
void 
DsrRouting::CheckSendBuffer ()
{
  std::cout << "DsrRouting::CheckSendBuffer->SimulationTime:" << Simulator::Now ().GetSeconds () << ", CheckingSendBuffer_at:" << m_mainAddress << ", Size:" << m_sendBuffer.GetSize () << std::endl;
  
  for (std::vector<DsrSendBuffEntry>::iterator i = m_sendBuffer.GetBuffer ().begin (); i != m_sendBuffer.GetBuffer ().end (); ) {
    // ここでは、送信バッファ内のデータパケットを見つけようとする.
    std::cout << "DsrRouting::CheckSendBuffer->Here we try to find the data packet in the send buffer" << std::endl;
    Ipv4Address destination = i->GetDestination ();
    DsrRouteCacheEntry toDst;
    bool findRoute = m_routeCache->LookupRoute (destination, toDst);
    
    if (findRoute) { // パケットのルートを発見！！！
      std::cout << "DsrRouting::CheckSendBuffer->We have found a route for the packet" << std::endl;
      Ptr<const Packet> packet = i->GetPacket ();
      Ptr<Packet> cleanP = packet->Copy ();
      uint8_t protocol = i->GetProtocol ();
      i = m_sendBuffer.GetBuffer ().erase (i);

      DsrRoutingHeader dsrRoutingHeader;
      Ptr<Packet> copyP = packet->Copy ();
      Ptr<Packet> dsrPacket = packet->Copy ();
      dsrPacket->RemoveHeader (dsrRoutingHeader);
      uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset ();

      // Here the processed size is 8 bytes, which is the fixed sized extension header
      // ここでは、処理されたサイズは固定サイズの拡張ヘッダである8バイトです
      copyP->RemoveAtStart (offset);
      Ptr<Packet> ipv4P = copyP->Copy (); // ipv4ヘッダーを取得するパケット / The packet to get ipv4 header

      // Peek data to get the option type as well as length and segmentsLeft field
      // データをピークして、オプションのタイプとlengthとsegmentsLeftフィールドを取得する
      uint32_t size = copyP->GetSize ();
      uint8_t *data = new uint8_t[size];
      copyP->CopyData (data, size);
      uint8_t optionType = 0;
      optionType = *(data);

      if (optionType == 3) {
        Ptr<dsr::DsrOptions> dsrOption;
        DsrOptionHeader dsrOptionHeader;
        uint8_t errorType = *(data + 2);

        // ルートエラーオプション / This is the Route Error Option
        if (errorType == 1) { 
          DsrOptionRerrUnreachHeader rerr;
          copyP->RemoveHeader (rerr);
          NS_ASSERT (copyP->GetSize () == 0);

          DsrOptionRerrUnreachHeader newUnreach;
          newUnreach.SetErrorType (1);
          newUnreach.SetErrorSrc (rerr.GetErrorSrc ());
          newUnreach.SetUnreachNode (rerr.GetUnreachNode ());
          newUnreach.SetErrorDst (rerr.GetErrorDst ());
          // パケットをサルベージするかどうかの値を設定する / Set the value about whether to salvage a packet or not
          newUnreach.SetSalvage (rerr.GetSalvage ()); 

          DsrOptionSRHeader sourceRoute;
          std::vector<Ipv4Address> errorRoute = toDst.GetVector ();
          sourceRoute.SetNodesAddress (errorRoute);
          
          /// When found a route and use it, UseExtends to the link cache
          // ルートを見つけて使用すると、リンクキャッシュに拡張が使用(UseExtends)されます
          if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (errorRoute);
          sourceRoute.SetSegmentsLeft ((errorRoute.size () - 2));
          uint8_t salvage = 0;
          sourceRoute.SetSalvage (salvage);
          // Get the next hop address / ネクストホップアドレスを取得する
          Ipv4Address nextHop = SearchNextHop (m_mainAddress, errorRoute);

          if (nextHop == "0.0.0.0") {
            PacketNewRoute (dsrPacket, m_mainAddress, destination, protocol);
            return;
          }

          SetRoute (nextHop, m_mainAddress);
          uint8_t length = (sourceRoute.GetLength () + newUnreach.GetLength ());
          dsrRoutingHeader.SetNextHeader (protocol);
          dsrRoutingHeader.SetMessageType (1);
          dsrRoutingHeader.SetSourceId (GetIDfromIP (m_mainAddress));
          dsrRoutingHeader.SetDestId (255);
          dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 4);
          dsrRoutingHeader.AddDsrOption (newUnreach);
          dsrRoutingHeader.AddDsrOption (sourceRoute);

          Ptr<Packet> newPacket = Create<Packet> ();
          // RERRとソースルートを付けてルーティングヘッダを追加する / Add the routing header with rerr and sourceRoute attached to it
          newPacket->AddHeader (dsrRoutingHeader); 
          Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
          m_ipv4Route->SetOutputDevice (dev);

          uint32_t priority = GetPriority (DSR_CONTROL_PACKET); /// This will be priority 0
          std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
          Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
          std::cout << "DsrRouting::CheckSendBuffer->Will be inserting into priority queue number: " << priority << std::endl;
          //m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);(元から使用してない)

          /// \todo New DsrNetworkQueueEntry
          DsrNetworkQueueEntry newEntry (newPacket, m_mainAddress, nextHop, Simulator::Now (), m_ipv4Route);

          if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
          else std::cout << "DsrRouting::CheckSendBuffer->Packet dropped as dsr network queue is full" << std::endl;
        }
      }
      // optionType==3 でない場合
      else {
        // ルートエラー以外を通すので、多分ここで探索をしている？？？
        dsrRoutingHeader.SetNextHeader (protocol);
        dsrRoutingHeader.SetMessageType (2);
        dsrRoutingHeader.SetSourceId (GetIDfromIP (m_mainAddress));
        dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

        DsrOptionSRHeader sourceRoute;
        // 見つけたルートエントリからルートを取得する / Get the route from the route entry we found
        std::vector<Ipv4Address> nodeList = toDst.GetVector ();
        // ルートのネクストホップアドレスを取得する / Get the next hop address for the route
        Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodeList);
        if (nextHop == "0.0.0.0") {
          PacketNewRoute (dsrPacket, m_mainAddress, destination, protocol);
          return;
        }
        uint8_t salvage = 0;

        // Save the whole route in the source route header of the packet
        // パケットのソースルートヘッダにルート全体を保存する
        sourceRoute.SetNodesAddress (nodeList); 
        // The segmentsLeft field will indicate the hops to go
        // segmentsLeftフィールドはホップを表示します
        sourceRoute.SetSegmentsLeft ((nodeList.size () - 2)); 
        sourceRoute.SetSalvage (salvage);

        /// When found a route and use it, UseExtends to the link cache
        // ルートを見つけて使用すると、リンクキャッシュに拡張が使用(UseExtends)されます
        if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (nodeList);

        uint8_t length = sourceRoute.GetLength ();
        dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
        dsrRoutingHeader.AddDsrOption (sourceRoute);
        cleanP->AddHeader (dsrRoutingHeader);
        Ptr<const Packet> mtP = cleanP->Copy ();

        // Put the data packet in the maintenance queue for data packet retransmission
        // データパケットの再送信のためにデータパケットを保守キューに入れます
        DsrMaintainBuffEntry newEntry (
          /*Packet=*/ mtP,
          /*Ipv4Address=*/ m_mainAddress, 
          /*nextHop=*/ nextHop,
          /*source=*/ m_mainAddress, 
          /*destination=*/ destination, 
          /*ackId=*/ 0,
          /*SegsLeft=*/ nodeList.size () - 2,
          /*expire time=*/ m_maxMaintainTime
        );
        
        // パケットをメンテナンスバッファにエンキューする / Enqueue the packet the the maintenance buffer
        bool result = m_maintainBuffer.Enqueue (newEntry); 
        if (result) {
          NetworkKey networkKey;
          networkKey.m_ackId = newEntry.GetAckId ();
          networkKey.m_ourAdd = newEntry.GetOurAdd ();
          networkKey.m_nextHop = newEntry.GetNextHop ();
          networkKey.m_source = newEntry.GetSrc ();
          networkKey.m_destination = newEntry.GetDst ();

          PassiveKey passiveKey;
          passiveKey.m_ackId = 0;
          passiveKey.m_source = newEntry.GetSrc ();
          passiveKey.m_destination = newEntry.GetDst ();
          passiveKey.m_segsLeft = newEntry.GetSegsLeft ();

          LinkKey linkKey;
          linkKey.m_source = newEntry.GetSrc ();
          linkKey.m_destination = newEntry.GetDst ();
          linkKey.m_ourAdd = newEntry.GetOurAdd ();
          linkKey.m_nextHop = newEntry.GetNextHop ();

          m_addressForwardCnt[networkKey] = 0;
          m_passiveCnt[passiveKey] = 0;
          m_linkCnt[linkKey] = 0;

          if (m_linkAck) {
            ScheduleLinkPacketRetry (newEntry, protocol);
          } else {
            // リンク確認応答を使用しない
            std::cout << "DsrRouting::CheckSendBuffer->Not using link acknowledgment" << std::endl;
            if (nextHop != destination) SchedulePassivePacketRetry (newEntry, protocol);
            // これは最初のネットワーク再試行です / This is the first network retry
            else ScheduleNetworkPacketRetry (newEntry, true, protocol);
          }
        }
        // we need to suspend the normal timer that checks the send buffer until we are done sending packets
        // パケットの送信が完了するまで送信バッファをチェックする通常のタイマーを一時停止する必要があります
        if (!m_sendBuffTimer.IsSuspended ()) {
          m_sendBuffTimer.Suspend ();
        }
        Simulator::Schedule (m_sendBuffInterval, &DsrRouting::SendBuffTimerExpire, this);
        return;
      }
    }else {
      ++i;
    }
  }
  // after going through the entire send buffer and send all packets found route,
  // we need to resume the timer if it has been suspended
  // 送信バッファ全体を通過して経路を発見したすべてのパケットを送信した後、
  // 一時停止されている場合はタイマーを再開する必要があります
  if (m_sendBuffTimer.IsSuspended ()) {
    std::cout << "DsrRouting::CheckSendBuffer->Resume the send buffer timer" << std::endl;
    m_sendBuffTimer.Resume ();
  }
}

//----------------------------------------------------------------------------------------------------------------------
bool 
DsrRouting::PromiscReceive (
  Ptr<NetDevice> device, 
  Ptr<const Packet> packet, 
  uint16_t protocol, 
  const Address &from,
  const Address &to,
  NetDevice::PacketType packetType
  )
{
  std::cout << "##### DsrRouting::PromiscReceive(無作為に他ノード宛のパケットを受信)->[IPAddress]:" << m_mainAddress << /*", From:" << from << ", To:" <<  to << ", PacketType:" << packetType <<*/ std::endl;
  if (protocol != Ipv4L3Protocol::PROT_NUMBER) {
    //std::cout << "DsrRouting::PromiscReceive->FALSE(protocol)" << std::endl;
    return false;
  }

  Ptr<Packet> pktMinusIpHdr = packet->Copy (); // Remove the ipv4 header here
  Ipv4Header ipv4Header;
  pktMinusIpHdr->RemoveHeader (ipv4Header);
  if (ipv4Header.GetProtocol () != DsrRouting::PROT_NUMBER) {
    //std::cout << "DsrRouting::PromiscReceive->FALSE(プロトコルとポートが一致しない)" << std::endl;
    return false;
  }

  // DSRルーティングヘッダーを削除する / Remove the dsr routing header here
  Ptr<Packet> pktMinusDsrHdr = pktMinusIpHdr->Copy ();
  DsrRoutingHeader dsrRouting;
  pktMinusDsrHdr->RemoveHeader (dsrRouting);

  /*
   * Message type 2 means the data packet, we will further process the data
   * packet for delivery notification, safely ignore control packet
   * Another check here is our own address, if this is the data destinated for us,
   * process it further, otherwise, just ignore it
   * メッセージタイプ2はデータパケットを意味し、配信通知用のデータパケットをさらに処理し、
   * 安全に制御パケットを無視します。ここで別の確認が行われるのは、私たち宛のデータであれば、さらに処理します。
   */
  Ipv4Address ourAddress = m_ipv4->GetAddress (1, 0).GetLocal ();

  // check if the message type is 2 and if the ipv4 address matches
  // メッセージタイプが2で、ipv4アドレスが一致するかどうかを確認してください
  if (dsrRouting.GetMessageType () == 2 && ourAddress == m_mainAddress) {
    std::cout << "##### DsrRouting::PromiscReceive->DataPacketReceives:" << packet->GetUid () << std::endl;
    Ipv4Address sourceIp = GetIPfromID (dsrRouting.GetSourceId ());
    Ipv4Address destinationIp = GetIPfromID ( dsrRouting.GetDestId ());
    /// This is the ip address we just received data packet from
    Ipv4Address previousHop = GetIPfromMAC (Mac48Address::ConvertFrom (from));

    Ptr<Packet> p = Create<Packet> ();
    // Here the segments left value need to plus one to check the earlier hop maintain buffer entry
    // ここで、セグメントの左の値には、前のホップがバッファエントリを維持していることを確認するためにプラス1が必要です
    DsrMaintainBuffEntry newEntry;
    newEntry.SetPacket (p);
    newEntry.SetSrc (sourceIp);
    newEntry.SetDst (destinationIp);
    /// Remember this is the entry for previous node
    /// 前のノードのエントリであることを思い出してください
    newEntry.SetOurAdd (previousHop);
    newEntry.SetNextHop (ourAddress);
    /// Get the previous node's maintenance buffer and passive ack
    /// 直前のノードのメンテナンスバッファとパッシブACKを取得する
    Ptr<Node> node = GetNodeWithAddress (previousHop);
    //std::cout << "##### DsrRouting::PromiscReceive->PreviousNode:" << previousHop << std::endl;

    Ptr<dsr::DsrRouting> dsr = node->GetObject<dsr::DsrRouting> ();
    dsr->CancelLinkPacketTimer (newEntry);
  }

  // Receive only IP packets and packets destined for other hosts
  // 他のホスト宛てのIPパケットとパケットのみを受信する
  if (packetType == NetDevice::PACKET_OTHERHOST) {
    // デバッグ出力を最小限に抑える / just to minimize debug output
    //NS_LOG_INFO (this << from << to << packetType << *pktMinusIpHdr);
    
    // オプションヘッダのオフセットを取得する。この場合は4バイト / Get the offset for option header, 4 bytes in this case
    uint8_t offset = dsrRouting.GetDsrOptionsOffset ();
    uint8_t nextHeader = dsrRouting.GetNextHeader ();
    uint32_t sourceId = dsrRouting.GetSourceId ();
    Ipv4Address source = GetIPfromID (sourceId);

    // このパケットは、オプションの種類を調べるために使用されます / This packet is used to peek option type
    pktMinusIpHdr->RemoveAtStart (offset);

    // Peek data to get the option type as well as length and segmentsLeft field
    // データをピークして、オプションのタイプとlengthとsegmentsLeftフィールドを取得する
    uint32_t size = pktMinusIpHdr->GetSize ();
    uint8_t *data = new uint8_t[size];
    pktMinusIpHdr->CopyData (data, size);
    uint8_t optionType = 0;
    optionType = *(data);
    Ptr<dsr::DsrOptions> dsrOption;

    // This is the source route option
    // ソースルートオプションです
    if (optionType == 96) {
      Ipv4Address promiscSource = GetIPfromMAC (Mac48Address::ConvertFrom (from));
      // Get the relative DSR option and demux to the process function
      // Reactive DSRオプションを取得し、プロセス関数にdemuxする
      dsrOption = GetOption (optionType);       
      std::cout << "##### DsrRouting::PromiscReceive->" << Simulator::Now ().GetSeconds () << " DSR node " << m_mainAddress << 
                    " overhearing packet PID: " << pktMinusIpHdr->GetUid () << " from " << promiscSource << 
                    " to " << GetIPfromMAC (Mac48Address::ConvertFrom (to)) << " with source:" << ipv4Header.GetSource () << 
                    " and destination:" << ipv4Header.GetDestination () << std::endl;
                    //" and destination IP " << ipv4Header.GetDestination () << " and packet : " << *pktMinusDsrHdr << std::endl;
      /*NS_LOG_DEBUG (Simulator::Now ().GetSeconds () << " DSR node " << m_mainAddress << " overhearing packet PID: " << pktMinusIpHdr->GetUid () << " from " << promiscSource << " to " << GetIPfromMAC (Mac48Address::ConvertFrom (to)) << " with source IP " << ipv4Header.GetSource () << " and destination IP " << ipv4Header.GetDestination () << " and packet : " << *pktMinusDsrHdr);*/
      bool isPromisc = true; // ブール値isPromiscをtrueに設定する / Set the boolean value isPromisc as true
      dsrOption->Process (pktMinusIpHdr, pktMinusDsrHdr, m_mainAddress, source, ipv4Header, nextHeader, isPromisc, promiscSource);
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------------------------------------------------------
void
DsrRouting::PacketNewRoute (
  Ptr<Packet> packet,
  Ipv4Address source,
  Ipv4Address destination,
  uint8_t protocol)
{
  NS_LOG_FUNCTION (this << packet << source << destination << (uint32_t)protocol);
  std::cout << "DsrRouting::PacketNewRoute->Source: "<< source << ", Destination:" << destination << ", Packet:" << packet << std::endl;

  // Look up routes for the specific destination
  // 特定の目的地のルートを検索する
  DsrRouteCacheEntry toDst;
  bool findRoute = m_routeCache->LookupRoute (destination, toDst);
  
  // Queue the packet if there is no route pre-existing
  // 既存のルートがない場合は、パケットをキューに入れます
  if (!findRoute) {
    std::cout << "DsrRouting::PacketNewRoute->" << Simulator::Now ().GetSeconds () << "s " << m_mainAddress << " there is no route for this packet, queue the packet." << std::endl;
    
    Ptr<Packet> p = packet->Copy ();
    // 送信バッファ用の新しいエントリを作成する / Create a new entry for send buffer
    DsrSendBuffEntry newEntry (p, destination, m_sendBufferTimeout, protocol);

    // 送信バッファにパケットをエンキューする / Enqueue the packet in send buffer
    bool result = m_sendBuffer.Enqueue (newEntry);     
    if (result) {
      std::cout << "DsrRouting::PacketNewRoute->" << Simulator::Now ().GetSeconds () << "s Add packet PID: " << packet->GetUid () << " to queue. Packet: " << *packet << std::endl;
      std::cout << "DsrRouting::PacketNewRoute->Send RREQ to" << destination << std::endl;
      if ((m_addressReqTimer.find (destination) == m_addressReqTimer.end ()) && (m_nonPropReqTimer.find (destination) == m_nonPropReqTimer.end ())) {
        // Call the send request function, it will update the request table entry and ttl there
        SendInitialRequest (source, destination, protocol);
      }
    }
  } else {
    Ptr<Packet> cleanP = packet->Copy ();
    DsrRoutingHeader dsrRoutingHeader;
    dsrRoutingHeader.SetNextHeader (protocol);
    dsrRoutingHeader.SetMessageType (2);
    dsrRoutingHeader.SetSourceId (GetIDfromIP (source));
    dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

    DsrOptionSRHeader sourceRoute;
    std::vector<Ipv4Address> nodeList = toDst.GetVector ();     // Get the route from the route entry we found
    Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodeList);      // Get the next hop address for the route
    if (nextHop == "0.0.0.0") {
      PacketNewRoute (cleanP, source, destination, protocol);
      return;
    }
    uint8_t salvage = 0;
    sourceRoute.SetNodesAddress (nodeList); // Save the whole route in the source route header of the packet
    /// When found a route and use it, UseExtends to the link cache
    if (m_routeCache->IsLinkCache ()) { m_routeCache->UseExtends (nodeList); }
    sourceRoute.SetSegmentsLeft ((nodeList.size () - 2));     // The segmentsLeft field will indicate the hops to go
    sourceRoute.SetSalvage (salvage);

    uint8_t length = sourceRoute.GetLength ();
    dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
    dsrRoutingHeader.AddDsrOption (sourceRoute);
    cleanP->AddHeader (dsrRoutingHeader);
    Ptr<const Packet> mtP = cleanP->Copy ();
    SetRoute (nextHop, m_mainAddress);
    // Put the data packet in the maintenance queue for data packet retransmission
    DsrMaintainBuffEntry newEntry (
      /*Packet=*/ mtP, 
      /*Ipv4Address=*/ m_mainAddress, 
      /*nextHop=*/ nextHop,
      /*source=*/ source, 
      /*destination=*/ destination, 
      /*ackId=*/ 0,
      /*SegsLeft=*/ nodeList.size () - 2, 
      /*expire time=*/ m_maxMaintainTime
    );
    // パケットをメンテナンスバッファにエンキューする / Enqueue the packet the the maintenance buffer
    bool result = m_maintainBuffer.Enqueue (newEntry);

    if (result) {
      NetworkKey networkKey;
      networkKey.m_ackId = newEntry.GetAckId ();
      networkKey.m_ourAdd = newEntry.GetOurAdd ();
      networkKey.m_nextHop = newEntry.GetNextHop ();
      networkKey.m_source = newEntry.GetSrc ();
      networkKey.m_destination = newEntry.GetDst ();

      PassiveKey passiveKey;
      passiveKey.m_ackId = 0;
      passiveKey.m_source = newEntry.GetSrc ();
      passiveKey.m_destination = newEntry.GetDst ();
      passiveKey.m_segsLeft = newEntry.GetSegsLeft ();

      LinkKey linkKey;
      linkKey.m_source = newEntry.GetSrc ();
      linkKey.m_destination = newEntry.GetDst ();
      linkKey.m_ourAdd = newEntry.GetOurAdd ();
      linkKey.m_nextHop = newEntry.GetNextHop ();

      m_addressForwardCnt[networkKey] = 0;
      m_passiveCnt[passiveKey] = 0;
      m_linkCnt[linkKey] = 0;

      if (m_linkAck) {
        ScheduleLinkPacketRetry (newEntry, protocol);
      }
      else {
        std::cout << "Not using link acknowledgment" << std::endl;
        if (nextHop != destination) {
          SchedulePassivePacketRetry (newEntry, protocol);
        }
        else {
          // This is the first network retry
          ScheduleNetworkPacketRetry (newEntry, true, protocol);
        }
      }
    }
  }
}

void
DsrRouting::SendUnreachError (
  Ipv4Address unreachNode, 
  Ipv4Address destination, 
  Ipv4Address originalDst, 
  uint8_t salvage, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << unreachNode << destination << originalDst << (uint32_t)salvage << (uint32_t)protocol);
  std::cout << "DsrRouting::SendUnreachError->UnreachNode:" << unreachNode << ", OriginalDestination:" << originalDst << ", Destination:" << destination << std::endl;

  DsrRoutingHeader dsrRoutingHeader;
  dsrRoutingHeader.SetNextHeader (protocol);
  dsrRoutingHeader.SetMessageType (1);
  dsrRoutingHeader.SetSourceId (GetIDfromIP (m_mainAddress));
  dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

  DsrOptionRerrUnreachHeader rerrUnreachHeader;
  rerrUnreachHeader.SetErrorType (1);
  rerrUnreachHeader.SetErrorSrc (m_mainAddress);
  rerrUnreachHeader.SetUnreachNode (unreachNode);
  rerrUnreachHeader.SetErrorDst (destination);
  rerrUnreachHeader.SetOriginalDst (originalDst);
  rerrUnreachHeader.SetSalvage (salvage); // Set the value about whether to salvage a packet or not
  uint8_t rerrLength = rerrUnreachHeader.GetLength ();


  DsrRouteCacheEntry toDst;
  bool findRoute = m_routeCache->LookupRoute (destination, toDst);
  // Queue the packet if there is no route pre-existing
  Ptr<Packet> newPacket = Create<Packet> ();
  if (!findRoute){
    if (destination == m_mainAddress){
      std::cout << "DsrRouting::SendUnreachError->We are the error source, send request to original dst " << originalDst << std::endl;
      // Send error request message if we are the source node
      SendErrorRequest (rerrUnreachHeader, protocol);
    }
    else {
      std::cout << "DsrRouting::SendUnreachError->" << Simulator::Now ().GetSeconds () << "s " << m_mainAddress << " there is no route for this packet, queue the packet." << std::endl;
      dsrRoutingHeader.SetPayloadLength (rerrLength + 2);
      dsrRoutingHeader.AddDsrOption (rerrUnreachHeader);
      newPacket->AddHeader (dsrRoutingHeader);
      Ptr<Packet> p = newPacket->Copy ();
      // Save the error packet in the error buffer
      DsrErrorBuffEntry newEntry (p, destination, m_mainAddress, unreachNode, m_sendBufferTimeout, protocol);
      bool result = m_errorBuffer.Enqueue (newEntry);                  // Enqueue the packet in send buffer
      if (result) {
        //std::cout << "DsrRouting::SendUnreachError->" << Simulator::Now ().GetSeconds () << "s Add packet PID: " << p->GetUid () << " to queue. Packet: " << *p << std::endl;
        std::cout << "DsrRouting::SendUnreachError->Send RREQ to" << destination << std::endl;
        if ((m_addressReqTimer.find (destination) == m_addressReqTimer.end ()) && (m_nonPropReqTimer.find (destination) == m_nonPropReqTimer.end ())){
          std::cout << "DsrRouting::SendUnreachError->When there is no existing route request for " << destination << ", initialize one" << std::endl;
          // Call the send request function, it will update the request table entry and ttl there
          SendInitialRequest (m_mainAddress, destination, protocol);
        }
      }
    }
  }
  else{
    std::vector<Ipv4Address> nodeList = toDst.GetVector ();
    Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodeList);
    if (nextHop == "0.0.0.0"){
      std::cout << "DsrRouting::SendUnreachError->The route is not right" << std::endl;
      //NS_LOG_DEBUG ("The route is not right");
      PacketNewRoute (newPacket, m_mainAddress, destination, protocol);
      return;
    }
    DsrOptionSRHeader sourceRoute;
    sourceRoute.SetNodesAddress (nodeList);
    /// When found a route and use it, UseExtends to the link cache
    if (m_routeCache->IsLinkCache ()){ m_routeCache->UseExtends (nodeList); }
    sourceRoute.SetSegmentsLeft ((nodeList.size () - 2));
    uint8_t srLength = sourceRoute.GetLength ();
    uint8_t length = (srLength + rerrLength);

    dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 4);
    dsrRoutingHeader.AddDsrOption (rerrUnreachHeader);
    dsrRoutingHeader.AddDsrOption (sourceRoute);
    newPacket->AddHeader (dsrRoutingHeader);

    SetRoute (nextHop, m_mainAddress);
    Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
    m_ipv4Route->SetOutputDevice (dev);
    std::cout << "DsrRouting::SendUnreachError->Send the packet to the next hop address " << nextHop << " from " << m_mainAddress << " with the size " << newPacket->GetSize () << std::endl;

    uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
    std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
    Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
    std::cout << "DsrRouting::SendUnreachError->Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority << std::endl;

    //m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);
    /// \todo New DsrNetworkQueueEntry
    DsrNetworkQueueEntry newEntry (newPacket, m_mainAddress, nextHop, Simulator::Now (), m_ipv4Route);

    if (dsrNetworkQueue->Enqueue (newEntry)){
      Scheduler (priority);
    } else {
      std::cout << "DsrRouting::SendUnreachError->Packet dropped as dsr network queue is full" << std::endl;
    }
  }
}

void
DsrRouting::ForwardErrPacket (
  DsrOptionRerrUnreachHeader &rerr,
  DsrOptionSRHeader &sourceRoute,
  Ipv4Address nextHop,
  uint8_t protocol,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << rerr << sourceRoute << nextHop << (uint32_t)protocol << route);
  std::cout << "DsrRouting::ForwardErrPacket->NextHop:" << nextHop << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");
  DsrRoutingHeader dsrRoutingHeader;
  dsrRoutingHeader.SetNextHeader (protocol);
  dsrRoutingHeader.SetMessageType (1);
  dsrRoutingHeader.SetSourceId (GetIDfromIP (rerr.GetErrorSrc ()));
  dsrRoutingHeader.SetDestId (GetIDfromIP (rerr.GetErrorDst ()));

  uint8_t length = (sourceRoute.GetLength () + rerr.GetLength ());
  dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 4);
  dsrRoutingHeader.AddDsrOption (rerr);
  dsrRoutingHeader.AddDsrOption (sourceRoute);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (dsrRoutingHeader);
  Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
  route->SetOutputDevice (dev);

  uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
  std::cout << "DsrRouting::ForwardErrPacket->Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority << std::endl;

  //m_downTarget (packet, m_mainAddress, nextHop, GetProtocolNumber (), route);

  /// \todo New DsrNetworkQueueEntry
  DsrNetworkQueueEntry newEntry (packet, m_mainAddress, nextHop, Simulator::Now (), route);

  if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
  else std::cout << "DsrRouting::ForwardErrPacket->Packet dropped as dsr network queue is full" << std::endl;
}

//---------------------------------------------------------------------------------------------------------
// Higher-level layers call this method to send a packet down the stack to the MAC and PHY layers.
void
DsrRouting::Send (
  Ptr<Packet> packet,
  Ipv4Address source,
  Ipv4Address destination,
  uint8_t protocol,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << packet << source << destination << (uint32_t)protocol << route);
  std::cout << ">>>>> DsrRouting::Send->Source:" << source << ", Destination:" << destination << ", Protocol:" << (uint32_t)protocol << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");

  if (protocol == 1){
    std::cout << ">>>>> DsrRouting::Send->Drop packet. Not handling ICMP packet for now." << std::endl;
  } else {
    // 特定の目的地のルートを検索します / Look up routes for the specific destination
    DsrRouteCacheEntry toDst;
    bool findRoute = m_routeCache->LookupRoute (destination, toDst);
    findRoute = false; // adding by shotakubota.
    // Queue the packet if there is no route pre-existing
    if (!findRoute){
      std::cout << ">>>>> DsrRouting::Send->" << Simulator::Now ().GetSeconds () << "s, " << m_mainAddress << " there is no route for this packet, queue the packet" << std::endl;
      Ptr<Packet> p = packet->Copy ();
      DsrSendBuffEntry newEntry (p, destination, m_sendBufferTimeout, protocol);// Create a new entry for send buffer
      bool result = m_sendBuffer.Enqueue (newEntry); // Enqueue the packet in send buffer
      if (result){
        std::cout << ">>>>> DsrRouting::Send->Enqueue the packet in send buffer." << std::endl;
        std::cout << ">>>>> DsrRouting::Send->" << Simulator::Now ().GetSeconds () << "s Add packet PID: " << packet->GetUid () << " to send buffer. Packet: " << *packet << std::endl;
        // 新しい経路要求がスケジュールされているときに既存の経路要求タイマーがない場合のみ / Only when there is no existing route request timer when new route request is scheduled
        if ((m_addressReqTimer.find (destination) == m_addressReqTimer.end ()) && (m_nonPropReqTimer.find (destination) == m_nonPropReqTimer.end ())){
          //Call the send request function, it will update the request table entry and ttl value
          std::cout << ">>>>> DsrRouting::Send->Send initial RREQ to " << destination << std::endl;
          SendInitialRequest (source, destination, protocol); // サブネット内にRREQをブロードキャスト
        } else {
          std::cout << ">>>>> DsrRouting::Send->There is existing route request timer with request count " << m_rreqTable->GetRreqCnt (destination) << std::endl;
        }
      }
    } else {
      Ptr<Packet> cleanP = packet->Copy ();
      DsrRoutingHeader dsrRoutingHeader;
      dsrRoutingHeader.SetNextHeader (protocol);
      dsrRoutingHeader.SetMessageType (2);
      dsrRoutingHeader.SetSourceId (GetIDfromIP (source));
      dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

      DsrOptionSRHeader sourceRoute;
      std::vector<Ipv4Address> nodeList = toDst.GetVector (); // Get the route from the route entry we found
      Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodeList); // Get the next hop address for the route
      if (nextHop == "0.0.0.0"){
        PacketNewRoute (cleanP, source, destination, protocol);
        return;
      }
      uint8_t salvage = 0;
      sourceRoute.SetNodesAddress (nodeList);       // Save the whole route in the source route header of the packet
      /// When found a route and use it, UseExtends to the link cache
      if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (nodeList);
      sourceRoute.SetSegmentsLeft ((nodeList.size () - 2)); // The segmentsLeft field will indicate the hops to go
      sourceRoute.SetSalvage (salvage);

      uint8_t length = sourceRoute.GetLength ();

      dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
      dsrRoutingHeader.AddDsrOption (sourceRoute);
      cleanP->AddHeader (dsrRoutingHeader);

      Ptr<const Packet> mtP = cleanP->Copy ();
      std::cout << ">>>>> DsrRouting::Send->MaintainPacketSize:" << cleanP->GetSize () << std::endl;
      // Put the data packet in the maintenance queue for data packet retransmission
      DsrMaintainBuffEntry newEntry (
        /*Packet=*/ mtP, 
        /*ourAddress=*/ m_mainAddress, 
        /*nextHop=*/ nextHop,
        /*source=*/ source, 
        /*destination=*/ destination, 
        /*ackId=*/ 0,
        /*SegsLeft=*/ nodeList.size () - 2, 
        /*expire time=*/ m_maxMaintainTime
      );
      bool result = m_maintainBuffer.Enqueue (newEntry);       // Enqueue the packet the the maintenance buffer
      if (result){
        NetworkKey networkKey;
        networkKey.m_ackId = newEntry.GetAckId ();
        networkKey.m_ourAdd = newEntry.GetOurAdd ();
        networkKey.m_nextHop = newEntry.GetNextHop ();
        networkKey.m_source = newEntry.GetSrc ();
        networkKey.m_destination = newEntry.GetDst ();

        PassiveKey passiveKey;
        passiveKey.m_ackId = 0;
        passiveKey.m_source = newEntry.GetSrc ();
        passiveKey.m_destination = newEntry.GetDst ();
        passiveKey.m_segsLeft = newEntry.GetSegsLeft ();

        LinkKey linkKey;
        linkKey.m_source = newEntry.GetSrc ();
        linkKey.m_destination = newEntry.GetDst ();
        linkKey.m_ourAdd = newEntry.GetOurAdd ();
        linkKey.m_nextHop = newEntry.GetNextHop ();

        m_addressForwardCnt[networkKey] = 0;
        m_passiveCnt[passiveKey] = 0;
        m_linkCnt[linkKey] = 0;

        if (m_linkAck){
          ScheduleLinkPacketRetry (newEntry, protocol);
        } else {
          std::cout << ">>>>> DsrRouting::Send->Not using link acknowledgment" << std::endl;
          if (nextHop != destination) SchedulePassivePacketRetry (newEntry, protocol);
          // This is the first network retry
          else ScheduleNetworkPacketRetry (newEntry, true, protocol);
        }
      }

      if (m_sendBuffer.GetSize () != 0 && m_sendBuffer.Find (destination)){
        // Try to send packet from *previously* queued entries from send buffer if any
        Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0,100)), &DsrRouting::SendPacketFromBuffer, this, sourceRoute, nextHop, protocol);
      }
    }
  }
}

uint16_t
DsrRouting::AddAckReqHeader (Ptr<Packet>& packet, Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << packet << nextHop);
  std::cout << "DsrRouting::AddAckReqHeader->NextHop:" << nextHop << ", Packet:" << packet << std::endl;
  // This packet is used to peek option type
  Ptr<Packet> dsrP = packet->Copy ();
  Ptr<Packet> tmpP = packet->Copy ();

  DsrRoutingHeader dsrRoutingHeader;
  dsrP->RemoveHeader (dsrRoutingHeader); // Remove the DSR header in whole
  uint8_t protocol = dsrRoutingHeader.GetNextHeader ();
  uint32_t sourceId = dsrRoutingHeader.GetSourceId ();
  uint32_t destinationId = dsrRoutingHeader.GetDestId ();
  uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset ();
  tmpP->RemoveAtStart (offset); // Here the processed size is 8 bytes, which is the fixed sized extension header

  // Get the number of routers' address field
  uint8_t buf[2];
  tmpP->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 2) / 4;
  DsrOptionSRHeader sourceRoute;
  sourceRoute.SetNumberAddress (numberAddress);
  tmpP->RemoveHeader (sourceRoute); // this is a clean packet without any dsr involved headers

  DsrOptionAckReqHeader ackReq;
  m_ackId = m_routeCache->CheckUniqueAckId (nextHop);
  ackReq.SetAckId (m_ackId);
  uint8_t length = (sourceRoute.GetLength () + ackReq.GetLength ());
  DsrRoutingHeader newDsrRoutingHeader;
  newDsrRoutingHeader.SetNextHeader (protocol);
  newDsrRoutingHeader.SetMessageType (2);
  newDsrRoutingHeader.SetSourceId (sourceId);
  newDsrRoutingHeader.SetDestId (destinationId);
  newDsrRoutingHeader.SetPayloadLength (length + 4);
  newDsrRoutingHeader.AddDsrOption (sourceRoute);
  newDsrRoutingHeader.AddDsrOption (ackReq);
  dsrP->AddHeader (newDsrRoutingHeader);
  // give the dsrP value to packet and then return
  packet = dsrP;
  return m_ackId;
}

//--------------------------------------------------------------------------------------------
// 実際にパケットを送信する
void
DsrRouting::SendPacket (
  Ptr<Packet> packet, 
  Ipv4Address source, 
  Ipv4Address nextHop, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << packet << source << nextHop << (uint32_t)protocol);
  std::cout << "+++++ DsrRouting::SendPacket->Source:" << source << ", NextHop:" << nextHop << /*", Packet:" << *packet <<*/ std::endl;
  // Send out the data packet
  m_ipv4Route = SetRoute (nextHop, m_mainAddress);
  Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
  m_ipv4Route->SetOutputDevice (dev);

  uint32_t priority = GetPriority (DSR_DATA_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
  std::cout << "+++++ DsrRouting::SendPacket->Will be inserting into priority queue number: " << priority << std::endl;

  /// \todo New DsrNetworkQueueEntry
  DsrNetworkQueueEntry newEntry (packet, source, nextHop, Simulator::Now (), m_ipv4Route);
  if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
  else std::cout << "+++++ Packet dropped as dsr network queue is full" << std::endl;
}

void
DsrRouting::Scheduler (
  uint32_t priority
  )
{
  NS_LOG_FUNCTION (this);
  PriorityScheduler (priority, true);
}

void
DsrRouting::PriorityScheduler (
  uint32_t priority, 
  bool continueWithFirst
  )
{
  NS_LOG_FUNCTION (this << priority << continueWithFirst);
  std::cout << "<<<<< DsrRouting::PriorityScheduler->キューの優先的なスケジューリング[IPAddress]:" << m_mainAddress <<  ", SimulationTime:" << Simulator::Now () << std::endl;
  uint32_t numPriorities;
  if (continueWithFirst) numPriorities = 0;
  else numPriorities = priority;
  // 優先度は0からm_numPriorityQueuesの範囲で、0が最も高い優先度 / priorities ranging from 0 to m_numPriorityQueues, with 0 as the highest priority
  for (uint32_t i = priority; numPriorities < m_numPriorityQueues; numPriorities++){
    std::map<uint32_t, Ptr<DsrNetworkQueue> >::iterator q = m_priorityQueue.find (i);
    Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = q->second;
    uint32_t queueSize = dsrNetworkQueue->GetSize ();
    if (queueSize == 0){
      if ((i == (m_numPriorityQueues - 1)) && continueWithFirst) i = 0;
      else i++;
    } else {
      uint32_t totalQueueSize = 0;
      for (std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator j = m_priorityQueue.begin (); j != m_priorityQueue.end (); j++){
        //std::cout << "<<<<< DsrRouting::PriorityScheduler->Size of NetworkQueue for " << j->first << " is " << j->second->GetSize () << std::endl;
        totalQueueSize += j->second->GetSize ();
        //std::cout << "<<<<< DsrRouting::PriorityScheduler->TotalNetworkQueueSize:" << totalQueueSize << std::endl;
      }
      // Here the queue size is larger than 5, we need to increase the retransmission timer for each packet in the network queue
      // ここでキューのサイズが5より大きい場合、ネットワークキュー内の各パケットの再送信タイマーを増やす必要があります / 
      if (totalQueueSize > 5) IncreaseRetransTimer ();

      DsrNetworkQueueEntry newEntry;
      dsrNetworkQueue->Dequeue (newEntry);
      if (SendRealDown (newEntry)){
        //std::cout << "DsrRouting::PriorityScheduler->Packet sent by DSR. Calling PriorityScheduler after some time." << std::endl;
        // パケットが正常に送信された. しばらくしてからスケジューラを呼ぶ. / packet was successfully sent down. call scheduler after some time
        //std::cout << "<<<<< DsrRouting::PriorityScheduler->DSRによって送信されたパケット. しばらくするとPrioritySchedulerが呼び出される." << std::endl;
        Simulator::Schedule (MicroSeconds (m_uniformRandomVariable->GetInteger (0, 1000)), &DsrRouting::PriorityScheduler,this, i, false);
      } else {
        // packet was dropped by Dsr. Call scheduler immediately so that we can send another packet immediately.
        // パケットはDSRによって廃棄された. すぐに別のパケットを送信できるように、スケジューラをすぐに呼び出してください
        //std::cout << "<<<<< DsrRouting::PriorityScheduler->Packet dropped by DSR. Calling PriorityScheduler immediately." << std::endl;
        //std::cout << "DSRによってパケットは廃棄された. 別のパケットを送信できるように、PrioritySchedulerを呼び出す." << std::endl;
        Simulator::Schedule (Seconds (0), &DsrRouting::PriorityScheduler, this, i, false);
      }
      if ((i == (m_numPriorityQueues - 1)) && continueWithFirst) i = 0; 
      else i++; 
    }
  }
}

void
DsrRouting::IncreaseRetransTimer ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "DsrRouting::IncreaseRetransTimer" << std::endl;
  // We may want to get the queue first and then we need to save a vector of the entries here and then find
  uint32_t priority = GetPriority (DSR_DATA_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;

  std::vector<DsrNetworkQueueEntry> newNetworkQueue = dsrNetworkQueue->GetQueue ();
  for (std::vector<DsrNetworkQueueEntry>::iterator i = newNetworkQueue.begin (); i != newNetworkQueue.end (); i++){
    Ipv4Address nextHop = i->GetNextHopAddress ();
    for (std::map<NetworkKey, Timer>::iterator j = m_addressForwardTimer.begin (); j != m_addressForwardTimer.end (); j++){
      if (nextHop == j->first.m_nextHop){
        std::cout << "DsrRouting::IncreaseRetransTimer->The network delay left is " << j->second.GetDelayLeft () << std::endl;
        j->second.SetDelay (j->second.GetDelayLeft () + m_retransIncr);
      }
    }
  }
}

bool
DsrRouting::SendRealDown (
  DsrNetworkQueueEntry & newEntry
  )
{
  NS_LOG_FUNCTION (this);
  Ipv4Address source = newEntry.GetSourceAddress ();
  Ipv4Address nextHop = newEntry.GetNextHopAddress ();
  Ptr<Packet> packet = newEntry.GetPacket ()->Copy ();
  Ptr<Ipv4Route> route = newEntry.GetIpv4Route ();
  //std::cout << "DsrRouting::SendRealDown->([パケットを送信する]スタックを呼び出す)Source:" << source << ", NextHop:" << nextHop << std::endl;
  m_downTarget (packet, source, nextHop, GetProtocolNumber (), route); // 下位層にコールバック
  return true;
}

//-------------------------------------------------------------------------------------------------------
// 経路がある場合にデータパケットを送信する役割を担い、経路が見つからなければパケットをキャッシュし経路要求を送信する
void
DsrRouting::SendPacketFromBuffer (
  DsrOptionSRHeader const &sourceRoute, 
  Ipv4Address nextHop, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << nextHop << (uint32_t)protocol);
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");

  // ルートを再構築し、データパケットを再送信する / Reconstruct the route and Retransmit the data packet
  std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress ();
  Ipv4Address destination = nodeList.back ();
  Ipv4Address source = nodeList.front ();// Get the source address
  std::cout << "+++++ DsrRouting::SendPacketFromBuffer->Source:" << source << ", Nexthop:" << nextHop << ", Destination:" << destination << std::endl;
  
  // Here we try to find data packet from send buffer, if packet with this destination found, send it out
  // ここでは、宛先へのパケットが見つかった場合は、送信バッファからデータパケットを見つけようする.
  if (m_sendBuffer.Find (destination)){
    //std::cout << "DsrRouting::SendPacketFromBuffer->destination over here " << destination << std::endl;
    /// ルートを見つけて使用すると、UseExtendsをリンクキャッシュに追加する. / When found a route and use it, UseExtends to the link cache
    if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (nodeList);
    DsrSendBuffEntry entry;
    if (m_sendBuffer.Dequeue (destination, entry)){
      Ptr<Packet> packet = entry.GetPacket ()->Copy ();
      Ptr<Packet> p = packet->Copy ();  // get a copy of the packet
      // Set the source route option
      DsrRoutingHeader dsrRoutingHeader;
      dsrRoutingHeader.SetNextHeader (protocol);
      dsrRoutingHeader.SetMessageType (2);
      dsrRoutingHeader.SetSourceId (GetIDfromIP (source));
      dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

      uint8_t length = sourceRoute.GetLength ();
      dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
      dsrRoutingHeader.AddDsrOption (sourceRoute);

      p->AddHeader (dsrRoutingHeader);
      Ptr<const Packet> mtP = p->Copy ();
      // データパケットの再送信のために、データパケットをメンテナンスキューに入れる. Put the data packet in the maintenance queue for data packet retransmission
      DsrMaintainBuffEntry newEntry (
        /*Packet=*/ mtP,
        /*ourAddress=*/ m_mainAddress, 
        /*nextHop=*/ nextHop,
        /*source=*/ source, 
        /*destination=*/ destination, 
        /*ackId=*/ 0,
        /*SegsLeft=*/ nodeList.size () - 2, 
        /*expire time=*/ m_maxMaintainTime
      );
      bool result = m_maintainBuffer.Enqueue (newEntry);// Enqueue the packet the the maintenance buffer

      if (result){
        NetworkKey networkKey;
        networkKey.m_ackId = newEntry.GetAckId ();
        networkKey.m_ourAdd = newEntry.GetOurAdd ();
        networkKey.m_nextHop = newEntry.GetNextHop ();
        networkKey.m_source = newEntry.GetSrc ();
        networkKey.m_destination = newEntry.GetDst ();

        PassiveKey passiveKey;
        passiveKey.m_ackId = 0;
        passiveKey.m_source = newEntry.GetSrc ();
        passiveKey.m_destination = newEntry.GetDst ();
        passiveKey.m_segsLeft = newEntry.GetSegsLeft ();

        LinkKey linkKey;
        linkKey.m_source = newEntry.GetSrc ();
        linkKey.m_destination = newEntry.GetDst ();
        linkKey.m_ourAdd = newEntry.GetOurAdd ();
        linkKey.m_nextHop = newEntry.GetNextHop ();

        m_addressForwardCnt[networkKey] = 0;
        m_passiveCnt[passiveKey] = 0;
        m_linkCnt[linkKey] = 0;

        // リンク確認を使用するかどうか...
        if (m_linkAck){
          // リンク層確認応答に基づいてパケット再送信をスケジュール.
          ScheduleLinkPacketRetry (newEntry, protocol);
        } else {
          std::cout << "+++++ DsrRouting::SendPacketFromBuffer->Not using link acknowledgment" << std::endl;
          if (nextHop != destination){
            // 受動的肯定応答に基づいてパケット再送信をスケジュール.
            SchedulePassivePacketRetry (newEntry, protocol); // ここが動く????
          } else {
            // This is the first network retry
            // ネットワーク層の肯定応答に基づいて、パケット再送信をスケジュール
            ScheduleNetworkPacketRetry (newEntry, true, protocol); 
          }
        }
      }

      std::cout << "+++++ DsrRouting::SendPacketFromBuffer->SendBufferSize:" << m_sendBuffer.GetSize () << ", Destination:" << destination <<std::endl;
      if (m_sendBuffer.GetSize () != 0 && m_sendBuffer.Find (destination)){
        std::cout << "+++++ DsrRouting::SendPacketFromBuffer->送信バッファに次のパケットを送信するようにスケジュール / Schedule sending the next packet in send buffer" << std::endl;
        Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0,100)),  &DsrRouting::SendPacketFromBuffer, this, sourceRoute, nextHop, protocol);
      }
    } else {
      std::cout << "+++++ DsrRouting::SendPacketFromBuffer->All queued packets are out-dated for the destination in send buffer" << std::endl;
    }
  }
  
  // Here we try to find data packet from send buffer, if packet with this destiantion found, send it out
  else if (m_errorBuffer.Find (destination)){
    DsrErrorBuffEntry entry;
    if (m_errorBuffer.Dequeue (destination, entry)){
      Ptr<Packet> packet = entry.GetPacket ()->Copy ();
      std::cout << "+++++ DsrRouting::SendPacketFromBuffer->QueuedPacketSize:" << packet->GetSize () << std::endl;

      DsrRoutingHeader dsrRoutingHeader;
      Ptr<Packet> copyP = packet->Copy ();
      Ptr<Packet> dsrPacket = packet->Copy ();
      dsrPacket->RemoveHeader (dsrRoutingHeader);
      uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset ();
      copyP->RemoveAtStart (offset);       // Here the processed size is 8 bytes, which is the fixed sized extension header
      
      // Peek data to get the option type as well as length and segmentsLeft field
      uint32_t size = copyP->GetSize ();
      uint8_t *data = new uint8_t[size];
      copyP->CopyData (data, size);

      uint8_t optionType = 0;
      optionType = *(data);
      std::cout << "+++++ DsrRouting::SendPacketFromBuffer->The option type value in send packet " << (uint32_t)optionType << std::endl;
      if (optionType == 3){
        std::cout << "+++++ DsrRouting::SendPacketFromBuffer->The packet is error packet" << std::endl;
        Ptr<dsr::DsrOptions> dsrOption;
        DsrOptionHeader dsrOptionHeader;

        uint8_t errorType = *(data + 2);
        std::cout << "+++++ DsrRouting::SendPacketFromBuffer->The error type" << std::endl;
        if (errorType == 1){
          std::cout << "+++++ DsrRouting::SendPacketFromBuffer->The packet is route error unreach packet" << std::endl;
          DsrOptionRerrUnreachHeader rerr;
          copyP->RemoveHeader (rerr);
          NS_ASSERT (copyP->GetSize () == 0);
          uint8_t length = (sourceRoute.GetLength () + rerr.GetLength ());

          DsrOptionRerrUnreachHeader newUnreach;
          newUnreach.SetErrorType (1);
          newUnreach.SetErrorSrc (rerr.GetErrorSrc ());
          newUnreach.SetUnreachNode (rerr.GetUnreachNode ());
          newUnreach.SetErrorDst (rerr.GetErrorDst ());
          newUnreach.SetOriginalDst (rerr.GetOriginalDst ());
          newUnreach.SetSalvage (rerr.GetSalvage ());       // Set the value about whether to salvage a packet or not

          std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress ();
          DsrRoutingHeader newRoutingHeader;
          newRoutingHeader.SetNextHeader (protocol);
          newRoutingHeader.SetMessageType (1);
          newRoutingHeader.SetSourceId (GetIDfromIP (rerr.GetErrorSrc ()));
          newRoutingHeader.SetDestId (GetIDfromIP (rerr.GetErrorDst ()));
          newRoutingHeader.SetPayloadLength (uint16_t (length) + 4);
          newRoutingHeader.AddDsrOption (newUnreach);
          newRoutingHeader.AddDsrOption (sourceRoute);
          /// When found a route and use it, UseExtends to the link cache
          if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (nodeList);
          SetRoute (nextHop, m_mainAddress);
          Ptr<Packet> newPacket = Create<Packet> ();
          newPacket->AddHeader (newRoutingHeader);       // Add the extension header with rerr and sourceRoute attached to it
          Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
          m_ipv4Route->SetOutputDevice (dev);

          uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
          std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
          Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
          //std::cout << "+++++ DsrRouting::SendPacketFromBuffer->Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority << std::endl;

          //m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);

          /// \todo New DsrNetworkQueueEntry
          DsrNetworkQueueEntry newEntry (newPacket, m_mainAddress, nextHop, Simulator::Now (), m_ipv4Route);

          if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
          else std::cout << "+++++ DsrRouting::SendPacketFromBuffer->Packet dropped as dsr network queue is full" << std::endl;
        }
      }

      if (m_errorBuffer.GetSize () != 0 && m_errorBuffer.Find (destination)){
        std::cout << "+++++ DsrRouting::SendPacketFromBuffer->Schedule sending the next packet in error buffer" << std::endl;
        Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0,100)), &DsrRouting::SendPacketFromBuffer, this, sourceRoute, nextHop, protocol);
      }
    }
  } else {
    std::cout << "+++++ DsrRouting::SendPacketFromBuffer->送信バッファ or エラーバッファパケットが見つからない / Packet not found in either the send or error buffer" << std::endl;
  }
}

bool
DsrRouting::PassiveEntryCheck (
  Ptr<Packet> packet, 
  Ipv4Address source, 
  Ipv4Address destination, 
  uint8_t segsLeft,
  uint16_t fragmentOffset, 
  uint16_t identification, 
  bool saveEntry
  )
{
  NS_LOG_FUNCTION (this << packet << source << destination << (uint32_t)segsLeft);

  Ptr<Packet> p = packet->Copy ();
  // Here the segments left value need to plus one to check the earlier hop maintain buffer entry
  DsrPassiveBuffEntry newEntry;
  newEntry.SetPacket (p);
  newEntry.SetSource (source);
  newEntry.SetDestination (destination);
  newEntry.SetIdentification (identification);
  newEntry.SetFragmentOffset (fragmentOffset);
  newEntry.SetSegsLeft (segsLeft);  // We try to make sure the segments left is larger for 1
  //std::cout << "DsrRouting::PassiveEntryCheck->PassiveBufferSize:" << m_passiveBuffer->GetSize () << std::endl;
  //std::cout << "DsrRouting::PassiveEntryCheck->Source:" << source << ", Destination:" << destination << ", Packet:" << packet << std::endl;

  if (m_passiveBuffer->AllEqual (newEntry) && (!saveEntry)){
    // The PromiscEqual function will remove the maintain buffer entry if equal value found
    // It only compares the source and destination address, ackId, and the segments left value
    //std::cout << "DsrRouting::PassiveEntryCheck->We get the all equal for passive buffer here" << std::endl;

    DsrMaintainBuffEntry mbEntry;
    mbEntry.SetPacket (p);
    mbEntry.SetSrc (source);
    mbEntry.SetDst (destination);
    mbEntry.SetAckId (0);
    mbEntry.SetSegsLeft (segsLeft + 1);

    CancelPassivePacketTimer (mbEntry);
    return true;
  }
  /// Save this passive buffer entry for later check
  if (saveEntry)  m_passiveBuffer->Enqueue (newEntry);

  return false;
}

bool
DsrRouting::CancelPassiveTimer (
  Ptr<Packet> packet, 
  Ipv4Address source, 
  Ipv4Address destination,
  uint8_t segsLeft
  )
{
  NS_LOG_FUNCTION (this << packet << source << destination << (uint32_t)segsLeft);
  std::cout << "DsrRouting::CancelPassiveTimer->Cancel the passive timer." << std::endl;

  Ptr<Packet> p = packet->Copy ();
  // Here the segments left value need to plus one to check the earlier hop maintain buffer entry
  DsrMaintainBuffEntry newEntry;
  newEntry.SetPacket (p);
  newEntry.SetSrc (source);
  newEntry.SetDst (destination);
  newEntry.SetAckId (0);
  newEntry.SetSegsLeft (segsLeft + 1);

  if (m_maintainBuffer.PromiscEqual (newEntry)){
    // The PromiscEqual function will remove the maintain buffer entry if equal value found
    // It only compares the source and destination address, ackId, and the segments left value
    CancelPassivePacketTimer (newEntry);
    return true;
  }
  return false;
}

void
DsrRouting::CallCancelPacketTimer (
  uint16_t ackId, 
  Ipv4Header const& ipv4Header, 
  Ipv4Address realSrc, 
  Ipv4Address realDst
  )
{
  NS_LOG_FUNCTION (this << (uint32_t)ackId << ipv4Header << realSrc << realDst);
  Ipv4Address sender = ipv4Header.GetDestination ();
  Ipv4Address receiver = ipv4Header.GetSource ();
  
  // Create a packet to fill maintenance buffer, not used to compare with maintainance entry
  // The reason is ack header doesn't have the original packet copy
  Ptr<Packet> mainP = Create<Packet> ();
  DsrMaintainBuffEntry newEntry (
    /*Packet=*/ mainP, 
    /*ourAddress=*/ sender, 
    /*nextHop=*/ receiver,
    /*source=*/ realSrc, 
    /*destination=*/ realDst, 
    /*ackId=*/ ackId,
    /*SegsLeft=*/ 0, 
    /*expire time=*/ Simulator::Now ()
  );
  CancelNetworkPacketTimer (newEntry);  // Only need to cancel network packet timer
}

void
DsrRouting::CancelPacketAllTimer (DsrMaintainBuffEntry & mb)
{
  NS_LOG_FUNCTION (this);
  //std::cout << "DsrRouting::CancelPacketAllTimer" << std::endl;
  CancelLinkPacketTimer (mb);
  CancelNetworkPacketTimer (mb);
  CancelPassivePacketTimer (mb);
}

void
DsrRouting::CancelLinkPacketTimer (DsrMaintainBuffEntry & mb)
{
  NS_LOG_FUNCTION (this);
  LinkKey linkKey;
  linkKey.m_ourAdd = mb.GetOurAdd ();
  linkKey.m_nextHop = mb.GetNextHop ();
  linkKey.m_source = mb.GetSrc ();
  linkKey.m_destination = mb.GetDst ();
  /*
   * Here we have found the entry for send retries, so we get the value and increase it by one
   */
  /// TODO need to think about this part
  m_linkCnt[linkKey] = 0;
  m_linkCnt.erase (linkKey);

  // TODO if find the linkkey, we need to remove it

  // Find the network acknowledgment timer
  std::map<LinkKey, Timer>::const_iterator i =
    m_linkAckTimer.find (linkKey);
  if (i == m_linkAckTimer.end ()){
    std::cout << "DsrRouting::CancelLinkPacketTimer->NOT find the link timer." << std::endl;
  } else {
    std::cout << "DsrRouting::CancelLinkPacketTimer->find the link timer." << std::endl;
    /*
      * Schedule the packet retry
      * Push back the nextHop, source, destination address
      */
    m_linkAckTimer[linkKey].Cancel ();
    m_linkAckTimer[linkKey].Remove ();
    if (m_linkAckTimer[linkKey].IsRunning ()){
      std::cout << "DsrRouting::CancelLinkPacketTimerTimer not canceled" << std::endl;
    }
    m_linkAckTimer.erase (linkKey);
  }

  // Erase the maintenance entry
  // yet this does not check the segments left value here
  std::cout << "DsrRouting::CancelLinkPacketTimer->LinkBufferSize:" << m_maintainBuffer.GetSize () << std::endl;
  if (m_maintainBuffer.LinkEqual (mb)){
    std::cout << "DsrRouting::CancelLinkPacketTimer->Link acknowledgment received, remove same maintenance buffer entry" << std::endl;
  }
}

void
DsrRouting::CancelNetworkPacketTimer (DsrMaintainBuffEntry & mb)
{
  NS_LOG_FUNCTION (this);
  NetworkKey networkKey;
  networkKey.m_ackId = mb.GetAckId ();
  networkKey.m_ourAdd = mb.GetOurAdd ();
  networkKey.m_nextHop = mb.GetNextHop ();
  networkKey.m_source = mb.GetSrc ();
  networkKey.m_destination = mb.GetDst ();
  /*
   * Here we have found the entry for send retries, so we get the value and increase it by one
   */
  m_addressForwardCnt[networkKey] = 0;
  m_addressForwardCnt.erase (networkKey);

  std::cout << "DsrRouting::CancelNetworkPacketTimer->ACKId " << mb.GetAckId () << " ourAdd " << mb.GetOurAdd () << ", NextHop " << mb.GetNextHop ()
                        << ", Source " << mb.GetSrc () << ", Destination " << mb.GetDst ()
                        << ", SegsLeft " << (uint32_t)mb.GetSegsLeft () << std::endl;
                        
  // Find the network acknowledgment timer
  std::map<NetworkKey, Timer>::const_iterator i =
    m_addressForwardTimer.find (networkKey);
  if (i == m_addressForwardTimer.end ()){
    std::cout << "DsrRouting::CancelNetworkPacketTimer->did NOT find the packet timer" << std::endl;
  } else {
    std::cout << "DsrRouting::CancelNetworkPacketTimer->did find the packet timer" << std::endl;
    
    // Schedule the packet retry
    // Push back the nextHop, source, destination address
    m_addressForwardTimer[networkKey].Cancel ();
    m_addressForwardTimer[networkKey].Remove ();
    if (m_addressForwardTimer[networkKey].IsRunning ())
      std::cout << "DsrRouting::CancelNetworkPacketTimer->Timer not canceled" << std::endl;
    m_addressForwardTimer.erase (networkKey);
  }
  // Erase the maintenance entry
  // yet this does not check the segments left value here
  if (m_maintainBuffer.NetworkEqual (mb)){
    std::cout << "DsrRouting::CancelNetworkPacketTimer->Remove same maintenance buffer entry based on network acknowledgment" << std::endl;
  }
}

void
DsrRouting::CancelPassivePacketTimer (DsrMaintainBuffEntry & mb)
{
  NS_LOG_FUNCTION (this);
  PassiveKey passiveKey;
  passiveKey.m_ackId = 0;
  passiveKey.m_source = mb.GetSrc ();
  passiveKey.m_destination = mb.GetDst ();
  passiveKey.m_segsLeft = mb.GetSegsLeft ();

  m_passiveCnt[passiveKey] = 0;
  m_passiveCnt.erase (passiveKey);

  // Find the passive acknowledgment timer
  std::map<PassiveKey, Timer>::const_iterator j =
    m_passiveAckTimer.find (passiveKey);
  if (j == m_passiveAckTimer.end ()){
    std::cout << "DsrRouting::CancelPassivePacketTimer->did not find the passive timer" << std::endl;
  } else {
    std::cout << "DsrRouting::CancelPassivePacketTimer->find the passive timer" << std::endl;
    // Cancel passive acknowledgment timer
    m_passiveAckTimer[passiveKey].Cancel ();
    m_passiveAckTimer[passiveKey].Remove ();
    if (m_passiveAckTimer[passiveKey].IsRunning ()) std::cout << "DsrRouting::CancelPassivePacketTimer->Timer not canceled" << std::endl;
    m_passiveAckTimer.erase (passiveKey);
  }
}

void
DsrRouting::CancelPacketTimerNextHop (Ipv4Address nextHop, uint8_t protocol)
{
  NS_LOG_FUNCTION (this << nextHop << (uint32_t)protocol);
  std::cout << "DsrRouting::CancelPacketTimerNextHop" << std::endl;

  DsrMaintainBuffEntry entry;
  std::vector<Ipv4Address> previousErrorDst;
  if (m_maintainBuffer.Dequeue (nextHop, entry)){
    Ipv4Address source = entry.GetSrc ();
    Ipv4Address destination = entry.GetDst ();

    Ptr<Packet> dsrP = entry.GetPacket ()->Copy ();
    Ptr<Packet> p = dsrP->Copy ();
    Ptr<Packet> packet = dsrP->Copy ();
    DsrRoutingHeader dsrRoutingHeader;
    dsrP->RemoveHeader (dsrRoutingHeader);          // Remove the dsr header in whole
    uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset ();
    p->RemoveAtStart (offset);

    // Get the number of routers' address field
    uint8_t buf[2];
    p->CopyData (buf, sizeof(buf));
    uint8_t numberAddress = (buf[1] - 2) / 4;
    std::cout << "DsrRouting::CancelPacketTimerNextHop->The number of addresses " << (uint32_t)numberAddress << std::endl;
    DsrOptionSRHeader sourceRoute;
    sourceRoute.SetNumberAddress (numberAddress);
    p->RemoveHeader (sourceRoute);
    std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress ();
    uint8_t salvage = sourceRoute.GetSalvage ();
    Ipv4Address address1 = nodeList[1];
    PrintVector (nodeList);

    // If the salvage is not 0, use the first address in the route as the error dst in error header
    // otherwise use the source of packet as the error destination
    Ipv4Address errorDst;
    if (salvage) errorDst = address1;
    else errorDst = source;

    /// TODO if the errorDst is not seen before
    if (std::find (previousErrorDst.begin (), previousErrorDst.end (), destination) == previousErrorDst.end ()){
      std::cout << "DsrRouting::CancelPacketTimerNextHop->have not seen this dst before " << errorDst << " in " << previousErrorDst.size () << std::endl;
      SendUnreachError (nextHop, errorDst, destination, salvage, protocol);
      previousErrorDst.push_back (errorDst);
    }

    // Cancel the packet timer and then salvage the data packet
    CancelPacketAllTimer (entry);
    SalvagePacket (packet, source, destination, protocol);

    if (m_maintainBuffer.GetSize () && m_maintainBuffer.Find (nextHop)){
      std::cout << "DsrRouting::CancelPacketTimerNextHop->Cancel the packet timer for next maintenance entry" << std::endl;
      Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0,100)), &DsrRouting::CancelPacketTimerNextHop,this,nextHop,protocol);
    }
  }
  else{
    std::cout << "Maintenance buffer entry not found" << std::endl;
  }
  /// TODO need to think about whether we need the network queue entry or not
}

void
DsrRouting::SalvagePacket (
  Ptr<const Packet> packet,
   Ipv4Address source, 
   Ipv4Address dst, 
   uint8_t protocol
   )
{
  NS_LOG_FUNCTION (this << packet << source << dst << (uint32_t)protocol);
  //std::cout << "DsrRouting::SalvagePacket" << std::endl;
  // Create two copies of packet
  Ptr<Packet> p = packet->Copy ();
  Ptr<Packet> newPacket = packet->Copy ();
  // Remove the routing header in a whole to get a clean packet
  DsrRoutingHeader dsrRoutingHeader;
  p->RemoveHeader (dsrRoutingHeader);
  // Remove offset of dsr routing header
  uint8_t offset = dsrRoutingHeader.GetDsrOptionsOffset ();
  newPacket->RemoveAtStart (offset);

  // Get the number of routers' address field
  uint8_t buf[2];
  newPacket->CopyData (buf, sizeof(buf));
  uint8_t numberAddress = (buf[1] - 2) / 4;

  DsrOptionSRHeader sourceRoute;
  sourceRoute.SetNumberAddress (numberAddress);
  newPacket->RemoveHeader (sourceRoute);
  uint8_t salvage = sourceRoute.GetSalvage ();
  /*
   * Look in the route cache for other routes for this destination
   */
  DsrRouteCacheEntry toDst;
  bool findRoute = m_routeCache->LookupRoute (dst, toDst);
  if (findRoute && (salvage < m_maxSalvageCount)){
    std::cout << "DsrRouting::SalvagePacket->We have found a route for the packet" << std::endl;
    //NS_LOG_DEBUG ("We have found a route for the packet");
    DsrRoutingHeader newDsrRoutingHeader;
    newDsrRoutingHeader.SetNextHeader (protocol);
    newDsrRoutingHeader.SetMessageType (2);
    newDsrRoutingHeader.SetSourceId (GetIDfromIP (source));
    newDsrRoutingHeader.SetDestId (GetIDfromIP (dst));

    std::vector<Ipv4Address> nodeList = toDst.GetVector ();         // Get the route from the route entry we found
    Ipv4Address nextHop = SearchNextHop (m_mainAddress, nodeList);  // Get the next hop address for the route
    if (nextHop == "0.0.0.0"){
      PacketNewRoute (p, source, dst, protocol);
      return;
    }
    // Increase the salvage count by 1
    salvage++;
    DsrOptionSRHeader sourceRoute;
    sourceRoute.SetSalvage (salvage);
    sourceRoute.SetNodesAddress (nodeList);     // Save the whole route in the source route header of the packet
    sourceRoute.SetSegmentsLeft ((nodeList.size () - 2));     // The segmentsLeft field will indicate the hops to go
    /// When found a route and use it, UseExtends to the link cache
    if (m_routeCache->IsLinkCache ()){
      m_routeCache->UseExtends (nodeList);
    }
    uint8_t length = sourceRoute.GetLength ();
    std::cout << "DsrRouting::SalvagePacket->Length of source route header " << (uint32_t)(sourceRoute.GetLength ()) << std::endl;
    //NS_LOG_INFO ("length of source route header " << (uint32_t)(sourceRoute.GetLength ()));
    newDsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
    newDsrRoutingHeader.AddDsrOption (sourceRoute);
    p->AddHeader (newDsrRoutingHeader);

    SetRoute (nextHop, m_mainAddress);
    Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
    m_ipv4Route->SetOutputDevice (dev);

    // Send out the data packet
    uint32_t priority = GetPriority (DSR_DATA_PACKET);
    std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
    Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
    //std::cout << "DsrRouting::SalvagePacket->Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority << std::endl;
    //m_downTarget (p, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);

    /// \todo New DsrNetworkQueueEntry
    DsrNetworkQueueEntry newEntry (p, m_mainAddress, nextHop, Simulator::Now (), m_ipv4Route);

    if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
    else std::cout << "DsrRouting::SalvagePacket->Packet dropped as dsr network queue is full" << std::endl;

    /// Mark the next hop address in blacklist
    //NS_LOG_DEBUG ("Save the next hop node in blacklist");
    //m_rreqTable->MarkLinkAsUnidirectional (nextHop, m_blacklistTimeout);
  }
  else{
    std::cout << "DsrRouting::SalvagePacket->Will not salvage this packet, silently drop" << std::endl;
  }
}

void
DsrRouting::ScheduleLinkPacketRetry (
  DsrMaintainBuffEntry & mb,
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << (uint32_t) protocol);
  Ptr<Packet> p = mb.GetPacket ()->Copy ();
  Ipv4Address source = mb.GetSrc ();
  Ipv4Address nextHop = mb.GetNextHop ();
  std::cout << "DsrRouting::ScheduleLinkPacketRetry->Source:" << source << ", NextHop:" << nextHop << std::endl;

  // Send the data packet out before schedule the next packet transmission
  SendPacket (p, source, nextHop, protocol);

  LinkKey linkKey;
  linkKey.m_source = mb.GetSrc ();
  linkKey.m_destination = mb.GetDst ();
  linkKey.m_ourAdd = mb.GetOurAdd ();
  linkKey.m_nextHop = mb.GetNextHop ();

  if (m_linkAckTimer.find (linkKey) == m_linkAckTimer.end ()){
    Timer timer (Timer::CANCEL_ON_DESTROY);
    m_linkAckTimer[linkKey] = timer;
  }
  m_linkAckTimer[linkKey].SetFunction (&DsrRouting::LinkScheduleTimerExpire, this);
  m_linkAckTimer[linkKey].Remove ();
  m_linkAckTimer[linkKey].SetArguments (mb, protocol);
  m_linkAckTimer[linkKey].Schedule (m_linkAckTimeout);
}

void
DsrRouting::SchedulePassivePacketRetry (
  DsrMaintainBuffEntry & mb,
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << (uint32_t)protocol);

  Ptr<Packet> p = mb.GetPacket ()->Copy ();
  Ipv4Address source = mb.GetSrc ();
  Ipv4Address nextHop = mb.GetNextHop ();
  std::cout << "DsrRouting::SchedulePassivePacketRetry->Source:" << source << ", NextHop:" << nextHop << std::endl;

  // Send the data packet out before schedule the next packet transmission
  SendPacket (p, source, nextHop, protocol);

  PassiveKey passiveKey;
  passiveKey.m_ackId = 0;
  passiveKey.m_source = mb.GetSrc ();
  passiveKey.m_destination = mb.GetDst ();
  passiveKey.m_segsLeft = mb.GetSegsLeft ();

  if (m_passiveAckTimer.find (passiveKey) == m_passiveAckTimer.end ()){
    Timer timer (Timer::CANCEL_ON_DESTROY);
    m_passiveAckTimer[passiveKey] = timer;
  }
  std::cout << "DsrRouting::SchedulePassivePacketRetry->The passive acknowledgment option for data packet" << std::endl;
  m_passiveAckTimer[passiveKey].SetFunction (&DsrRouting::PassiveScheduleTimerExpire, this);
  m_passiveAckTimer[passiveKey].Remove ();
  m_passiveAckTimer[passiveKey].SetArguments (mb, protocol);
  m_passiveAckTimer[passiveKey].Schedule (m_passiveAckTimeout);
}

void
DsrRouting::ScheduleNetworkPacketRetry (
  DsrMaintainBuffEntry & mb,
  bool isFirst,
  uint8_t protocol
  )
{
  Ptr<Packet> p = Create<Packet> ();
  Ptr<Packet> dsrP = Create<Packet> ();
  // The new entry will be used for retransmission
  NetworkKey networkKey;
  Ipv4Address nextHop = mb.GetNextHop ();
  std::cout << "DsrRouting::ScheduleNetworkPacketRetry->is the first retry or not " << isFirst << std::endl;
  if (isFirst){
    // This is the very first network packet retry
    p = mb.GetPacket ()->Copy ();
    // Here we add the ack request header to the data packet for network acknowledgement
    uint16_t ackId = AddAckReqHeader (p, nextHop);

    Ipv4Address source = mb.GetSrc ();
    Ipv4Address nextHop = mb.GetNextHop ();
    // Send the data packet out before schedule the next packet transmission
    SendPacket (p, source, nextHop, protocol);

    dsrP = p->Copy ();
    DsrMaintainBuffEntry newEntry = mb;
    // The function AllEqual will find the exact entry and delete it if found
    m_maintainBuffer.AllEqual (mb);
    newEntry.SetPacket (dsrP);
    newEntry.SetAckId (ackId);
    newEntry.SetExpireTime (m_maxMaintainTime);

    networkKey.m_ackId = newEntry.GetAckId ();
    networkKey.m_ourAdd = newEntry.GetOurAdd ();
    networkKey.m_nextHop = newEntry.GetNextHop ();
    networkKey.m_source = newEntry.GetSrc ();
    networkKey.m_destination = newEntry.GetDst ();

    m_addressForwardCnt[networkKey] = 0;
    if (!m_maintainBuffer.Enqueue (newEntry)){
      std::cout << "DsrRouting::ScheduleNetworkPacketRetry->Failed to enqueue packet retry" << std::endl;
    }

    if (m_addressForwardTimer.find (networkKey) == m_addressForwardTimer.end ()){
      Timer timer (Timer::CANCEL_ON_DESTROY);
      m_addressForwardTimer[networkKey] = timer;
    }

    // After m_tryPassiveAcks, schedule the packet retransmission using network acknowledgment option
    m_addressForwardTimer[networkKey].SetFunction (&DsrRouting::NetworkScheduleTimerExpire, this);
    m_addressForwardTimer[networkKey].Remove ();
    m_addressForwardTimer[networkKey].SetArguments (newEntry, protocol);
    std::cout << "DsrRouting::ScheduleNetworkPacketRetry->The packet retries time for " << newEntry.GetAckId () << " is " << m_sendRetries << " and the delay time is " << Time (2 * m_nodeTraversalTime).GetSeconds () << std::endl;
    // Back-off mechanism
    m_addressForwardTimer[networkKey].Schedule (Time (2 * m_nodeTraversalTime));
  } else {
    networkKey.m_ackId = mb.GetAckId ();
    networkKey.m_ourAdd = mb.GetOurAdd ();
    networkKey.m_nextHop = mb.GetNextHop ();
    networkKey.m_source = mb.GetSrc ();
    networkKey.m_destination = mb.GetDst ();
    
    // Here we have found the entry for send retries, so we get the value and increase it by one
    m_sendRetries = m_addressForwardCnt[networkKey];
    std::cout << "DsrRouting::ScheduleNetworkPacketRetry->The packet retry we have done " << m_sendRetries << std::endl;

    p = mb.GetPacket ()->Copy ();
    dsrP = mb.GetPacket ()->Copy ();

    Ipv4Address source = mb.GetSrc ();
    Ipv4Address nextHop = mb.GetNextHop ();
    // Send the data packet out before schedule the next packet transmission
    SendPacket (p, source, nextHop, protocol);

    std::cout << "DsrRouting::ScheduleNetworkPacketRetry->The packet with dsr header " << dsrP->GetSize () << std::endl;
    networkKey.m_ackId = mb.GetAckId ();
    networkKey.m_ourAdd = mb.GetOurAdd ();
    networkKey.m_nextHop = mb.GetNextHop ();
    networkKey.m_source = mb.GetSrc ();
    networkKey.m_destination = mb.GetDst ();
    /*
      *  If a data packet has been attempted SendRetries times at the maximum TTL without
      *  receiving any ACK, all data packets destined for the corresponding destination SHOULD be
      *  dropped from the send buffer
      *
      *  The maxMaintRexmt also needs to decrease one for the passive ack packet
      */

    /*
      * Check if the send retry time for a certain packet has already passed max maintenance retransmission
      * time or not
      */

    // After m_tryPassiveAcks, schedule the packet retransmission using network acknowledgment option
    m_addressForwardTimer[networkKey].SetFunction (&DsrRouting::NetworkScheduleTimerExpire, this);
    m_addressForwardTimer[networkKey].Remove ();
    m_addressForwardTimer[networkKey].SetArguments (mb, protocol);
    std::cout << "DsrRouting::ScheduleNetworkPacketRetry->The packet retries time for " << mb.GetAckId () << " is " << m_sendRetries << " and the delay time is " << Time (2 * m_sendRetries *  m_nodeTraversalTime).GetSeconds () << std::endl;
    // Back-off mechanism
    m_addressForwardTimer[networkKey].Schedule (Time (2 * m_sendRetries * m_nodeTraversalTime));
  }
}

void
DsrRouting::LinkScheduleTimerExpire (
  DsrMaintainBuffEntry & mb,
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << (uint32_t)protocol);
  Ipv4Address nextHop = mb.GetNextHop ();
  Ptr<const Packet> packet = mb.GetPacket ();
  SetRoute (nextHop, m_mainAddress);
  Ptr<Packet> p = packet->Copy ();

  LinkKey lk;
  lk.m_source = mb.GetSrc ();
  lk.m_destination = mb.GetDst ();
  lk.m_ourAdd = mb.GetOurAdd ();
  lk.m_nextHop = mb.GetNextHop ();

  // Cancel passive ack timer
  m_linkAckTimer[lk].Cancel ();
  m_linkAckTimer[lk].Remove ();
  if (m_linkAckTimer[lk].IsRunning ()) std::cout << "DsrRouting::LinkScheduleTimerExpire->Timer not canceled" << std::endl;
  m_linkAckTimer.erase (lk);

  // Increase the send retry times
  m_linkRetries = m_linkCnt[lk];
  if (m_linkRetries < m_tryLinkAcks){
    m_linkCnt[lk] = ++m_linkRetries;
    ScheduleLinkPacketRetry (mb, protocol);
  } else {
    std::cout << "DsrRouting::LinkScheduleTimerExpire->We need to send error messages now" << std::endl;

    // Delete all the routes including the links
    m_routeCache->DeleteAllRoutesIncludeLink (m_mainAddress, nextHop, m_mainAddress);
    /*
      * here we cancel the packet retransmission time for all the packets have next hop address as nextHop
      * Also salvage the packet for the all the packet destined for the nextHop address
      * this is also responsible for send unreachable error back to source
      */
    CancelPacketTimerNextHop (nextHop, protocol);
  }
}

void
DsrRouting::PassiveScheduleTimerExpire  (
  DsrMaintainBuffEntry & mb,
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << (uint32_t)protocol);
  //std::cout << "DsrRouting::PassiveScheduleTimerExpire" << std::endl;
  Ipv4Address nextHop = mb.GetNextHop ();
  Ptr<const Packet> packet = mb.GetPacket ();
  SetRoute (nextHop, m_mainAddress);
  Ptr<Packet> p = packet->Copy ();

  PassiveKey pk;
  pk.m_ackId = 0;
  pk.m_source = mb.GetSrc ();
  pk.m_destination = mb.GetDst ();
  pk.m_segsLeft = mb.GetSegsLeft ();

  // Cancel passive ack timer
  m_passiveAckTimer[pk].Cancel ();
  m_passiveAckTimer[pk].Remove ();
  if (m_passiveAckTimer[pk].IsRunning ()) std::cout << "DsrRouting::PassiveScheduleTimerExpire->Timer not canceled" << std::endl;
  m_passiveAckTimer.erase (pk);

  // Increase the send retry times
  m_passiveRetries = m_passiveCnt[pk];
  if (m_passiveRetries < m_tryPassiveAcks){
    m_passiveCnt[pk] = ++m_passiveRetries;
    SchedulePassivePacketRetry (mb, protocol);
  } else {
    // This is the first network acknowledgement retry
    // Cancel the passive packet timer now and remove maintenance buffer entry for it
    CancelPassivePacketTimer (mb);
    ScheduleNetworkPacketRetry (mb, true, protocol);
  }
}

int64_t
DsrRouting::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uniformRandomVariable->SetStream (stream);
  return 1;
}

void
DsrRouting::NetworkScheduleTimerExpire  (
  DsrMaintainBuffEntry & mb,
  uint8_t protocol
  )
{
  Ptr<Packet> p = mb.GetPacket ()->Copy ();
  Ipv4Address source = mb.GetSrc ();
  Ipv4Address nextHop = mb.GetNextHop ();
  Ipv4Address dst = mb.GetDst ();

  NetworkKey networkKey;
  networkKey.m_ackId = mb.GetAckId ();
  networkKey.m_ourAdd = mb.GetOurAdd ();
  networkKey.m_nextHop = nextHop;
  networkKey.m_source = source;
  networkKey.m_destination = dst;

  // Increase the send retry times
  m_sendRetries = m_addressForwardCnt[networkKey];

  if (m_sendRetries >= m_maxMaintRexmt){
    // Delete all the routes including the links
    m_routeCache->DeleteAllRoutesIncludeLink (m_mainAddress, nextHop, m_mainAddress);
    
    // here we cancel the packet retransmission time for all the packets have next hop address as nextHop
    // Also salvage the packet for the all the packet destined for the nextHop address
    CancelPacketTimerNextHop (nextHop, protocol);
  }
  else{
    m_addressForwardCnt[networkKey] = ++m_sendRetries;
    ScheduleNetworkPacketRetry (mb, false, protocol);
  }
}

void
DsrRouting::ForwardPacket (
  Ptr<const Packet> packet,
  DsrOptionSRHeader &sourceRoute,
  Ipv4Header const& ipv4Header,
  Ipv4Address source,
  Ipv4Address nextHop,
  Ipv4Address targetAddress,
  uint8_t protocol,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << packet << sourceRoute << source << nextHop << targetAddress << (uint32_t)protocol << route);
  std::cout << "DsrRouting::ForwardPacket->TargetAddress:" << targetAddress << ", Source:" << source << ", NextHop:" << nextHop << ", Protocol:" << (uint32_t)protocol << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");

  DsrRoutingHeader dsrRoutingHeader;
  dsrRoutingHeader.SetNextHeader (protocol);
  dsrRoutingHeader.SetMessageType (2);
  dsrRoutingHeader.SetSourceId (GetIDfromIP (source));
  dsrRoutingHeader.SetDestId (GetIDfromIP (targetAddress));

  // We get the salvage value in sourceRoute header and set it to route error header if triggered error
  Ptr<Packet> p = packet->Copy ();
  uint8_t length = sourceRoute.GetLength ();
  dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
  dsrRoutingHeader.AddDsrOption (sourceRoute);
  p->AddHeader (dsrRoutingHeader);

  Ptr<const Packet> mtP = p->Copy ();

  DsrMaintainBuffEntry newEntry (
    /*Packet=*/ mtP, 
    /*ourAddress=*/ m_mainAddress, 
    /*nextHop=*/ nextHop,
    /*source=*/ source, 
    /*destination=*/ targetAddress, 
    /*ackId=*/ m_ackId,
    /*SegsLeft=*/ sourceRoute.GetSegmentsLeft (), 
    /*expire time=*/ m_maxMaintainTime
  );
  bool result = m_maintainBuffer.Enqueue (newEntry);

  if (result){
    NetworkKey networkKey;
    networkKey.m_ackId = newEntry.GetAckId ();
    networkKey.m_ourAdd = newEntry.GetOurAdd ();
    networkKey.m_nextHop = newEntry.GetNextHop ();
    networkKey.m_source = newEntry.GetSrc ();
    networkKey.m_destination = newEntry.GetDst ();

    PassiveKey passiveKey;
    passiveKey.m_ackId = 0;
    passiveKey.m_source = newEntry.GetSrc ();
    passiveKey.m_destination = newEntry.GetDst ();
    passiveKey.m_segsLeft = newEntry.GetSegsLeft ();

    LinkKey linkKey;
    linkKey.m_source = newEntry.GetSrc ();
    linkKey.m_destination = newEntry.GetDst ();
    linkKey.m_ourAdd = newEntry.GetOurAdd ();
    linkKey.m_nextHop = newEntry.GetNextHop ();

    m_addressForwardCnt[networkKey] = 0;
    m_passiveCnt[passiveKey] = 0;
    m_linkCnt[linkKey] = 0;

    if (m_linkAck){
      ScheduleLinkPacketRetry (newEntry, protocol);
    }
    else{
      std::cout << "DsrRouting::ForwardPacket->Not using link acknowledgment" << std::endl;
      if (nextHop != targetAddress) SchedulePassivePacketRetry (newEntry, protocol);
      else ScheduleNetworkPacketRetry (newEntry, true, protocol); // This is the first network retry
    }
  }
}

//----------------------------------------------------------------------------------------------------
// Broadcast the route request packet in subnet
void
DsrRouting::SendInitialRequest (
  Ipv4Address source,
  Ipv4Address destination,
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << source << destination << (uint32_t)protocol);
  std::cout << "##### DsrRouting::SendInitialRequest->サブネット内にRREQパケットをブロードキャスト" << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");
  
  Ptr<Packet> packet = Create<Packet> ();
  Ptr<Ipv4Route> route; // Create an empty Ipv4 route ptr
  // Construct the route request option header
  std::cout << "##### DsrRouting::SendInitialRequest->Create Route REQuest header:Source:" << source << ", Destination:" << destination << std::endl;
  DsrRoutingHeader dsrRoutingHeader;
  dsrRoutingHeader.SetNextHeader (protocol);
  dsrRoutingHeader.SetMessageType (1);
  dsrRoutingHeader.SetSourceId (GetIDfromIP (source));
  dsrRoutingHeader.SetDestId (255);

  DsrOptionRreqHeader rreqHeader;              // has an alignment of 4n+0
  rreqHeader.AddNodeAddress (m_mainAddress);   // Add our own address in the header
  rreqHeader.SetTarget (destination); 
  m_requestId = m_rreqTable->CheckUniqueRreqId (destination); // Check the Id cache for duplicate ones
  rreqHeader.SetId (m_requestId);

  dsrRoutingHeader.AddDsrOption (rreqHeader); // Add the rreqHeader to the dsr extension header
  uint8_t length = rreqHeader.GetLength ();
  dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
  packet->AddHeader (dsrRoutingHeader);

  // 非伝番をしないようにする？？？ / Schedule the route requests retry with non-propagation set true
  bool nonProp = true;
  std::vector<Ipv4Address> address;
  address.push_back (source);
  address.push_back (destination);
  
  // RREQのTTLを制御 / Add the socket ip ttl tag to the packet to limit the scope of route requests
  SocketIpTtlTag tag;
  tag.SetTtl (0);
  Ptr<Packet> nonPropPacket = packet->Copy ();
  nonPropPacket->AddPacketTag (tag);
  // Increase the request count
  m_rreqTable->FindAndUpdate (destination); // Find the entry in the route request queue to see if already exists
  SendRequest (nonPropPacket, source); // RREQをフラッティング
  // Schedule the next route request
  ScheduleRreqRetry (packet, address, nonProp, m_requestId, protocol); // 次のRREQをスケジューリング
}

void
DsrRouting::SendErrorRequest (
  DsrOptionRerrUnreachHeader &rerr, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << (uint32_t)protocol);
  std::cout << "DsrRouting::SendErrorRequest." << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");
  uint8_t salvage = rerr.GetSalvage ();
  Ipv4Address dst = rerr.GetOriginalDst ();
  std::cout << "DsrRouting::SendErrorRequest->Our own address here " << m_mainAddress << " ErrorSource " << rerr.GetErrorSrc () << " ErrorDestination " << rerr.GetErrorDst () << " ErrorNextHop " << rerr.GetUnreachNode () << " OriginalDst " << rerr.GetOriginalDst () << std::endl;
  DsrRouteCacheEntry toDst;

  if (m_routeCache->LookupRoute (dst, toDst)){  
    // Found a route the dst, construct the source route option header
    DsrOptionSRHeader sourceRoute;
    std::vector<Ipv4Address> ip = toDst.GetVector ();
    sourceRoute.SetNodesAddress (ip);

    /// When found a route and use it, UseExtends to the link cache
    if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (ip);

    sourceRoute.SetSegmentsLeft ((ip.size () - 2));
    sourceRoute.SetSalvage (salvage);
    Ipv4Address nextHop = SearchNextHop (m_mainAddress, ip);       // Get the next hop address
    std::cout << "DsrRouting::SendErrorRequest->The nextHop address " << nextHop << std::endl;
    Ptr<Packet> packet = Create<Packet> ();
    if (nextHop == "0.0.0.0"){
      std::cout << "DsrRouting::SendErrorRequest->Error next hop address" << std::endl;
      PacketNewRoute (packet, m_mainAddress, dst, protocol);
      return;
    }
    SetRoute (nextHop, m_mainAddress);
    CancelRreqTimer (dst, true);
    /// Try to send out the packet from the buffer once we found one route
    if (m_sendBuffer.GetSize () != 0 && m_sendBuffer.Find (dst)){
      SendPacketFromBuffer (sourceRoute, nextHop, protocol);
    }
    std::cout << "DsrRouting::SendErrorRequest->Route to " << dst << " found" << std::endl;
    //NS_LOG_LOGIC ("Route to " << dst << " found");
    return;
  }else{
    std::cout << "DsrRouting::SendErrorRequest->No route found, initiate route error request" << std::endl;

    Ptr<Packet> packet = Create<Packet> ();
    Ipv4Address originalDst = rerr.GetOriginalDst ();
    Ptr<Ipv4Route> route = 0; // Create an empty route ptr

    // Construct the route request option header
    DsrRoutingHeader dsrRoutingHeader;
    dsrRoutingHeader.SetNextHeader (protocol);
    dsrRoutingHeader.SetMessageType (1);
    dsrRoutingHeader.SetSourceId (GetIDfromIP (m_mainAddress));
    dsrRoutingHeader.SetDestId (255);

    Ptr<Packet> dstP = Create<Packet> ();
    DsrOptionRreqHeader rreqHeader;                                // has an alignment of 4n+0
    rreqHeader.AddNodeAddress (m_mainAddress);                     // Add our own address in the header
    rreqHeader.SetTarget (originalDst);
    m_requestId = m_rreqTable->CheckUniqueRreqId (originalDst);       // Check the Id cache for duplicate ones
    rreqHeader.SetId (m_requestId);

    dsrRoutingHeader.AddDsrOption (rreqHeader);         // Add the rreqHeader to the dsr extension header
    dsrRoutingHeader.AddDsrOption (rerr);
    uint8_t length = rreqHeader.GetLength () + rerr.GetLength ();
    dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 4);
    dstP->AddHeader (dsrRoutingHeader);
    // Schedule the route requests retry, propagate the route request message as it contains error
    bool nonProp = false;
    std::vector<Ipv4Address> address;
    address.push_back (m_mainAddress);
    address.push_back (originalDst);

    // Add the socket ip ttl tag to the packet to limit the scope of route requests
    SocketIpTtlTag tag;
    tag.SetTtl ((uint8_t)m_discoveryHopLimit);
    Ptr<Packet> propPacket = dstP->Copy ();
    propPacket->AddPacketTag (tag);

    if ((m_addressReqTimer.find (originalDst) == m_addressReqTimer.end ()) && (m_nonPropReqTimer.find (originalDst) == m_nonPropReqTimer.end ())){
      std::cout << "DsrRouting::SendErrorRequest->Only when there is no existing route request time when the initial route request is scheduled" << std::endl;
      //NS_LOG_INFO ("Only when there is no existing route request time when the initial route request is scheduled");
      SendRequest (propPacket, m_mainAddress);
      ScheduleRreqRetry (dstP, address, nonProp, m_requestId, protocol);
    } else {
      std::cout << "DsrRouting::SendErrorRequest->There is existing route request, find the existing route request entry" << std::endl;
      // Cancel the route request timer first before scheduling the route request
      // in this case, we do not want to remove the route request entry, so the isRemove value is false
      CancelRreqTimer (originalDst, false);
      ScheduleRreqRetry (dstP, address, nonProp, m_requestId, protocol);
    }
  }
}

void
DsrRouting::CancelRreqTimer (Ipv4Address dst, bool isRemove)
{
  NS_LOG_FUNCTION (this << dst << isRemove);
  std::cout << "DsrRouting::CancelRreqTimer(経路要求タイマーをキャンセル)->Destination:" << dst << ", Flag:" << isRemove << std::endl;
  
  // Cancel the non propagation request timer if found
  if (m_nonPropReqTimer.find (dst) == m_nonPropReqTimer.end ()){
    std::cout << "DsrRouting::CancelRreqTimer->The non-propagation timer is NOT found." << std::endl;
  } else { 
    std::cout << "DsrRouting::CancelRreqTimer->The non-Propagation timer is found." << std::endl;
  }m_nonPropReqTimer[dst].Cancel ();
  m_nonPropReqTimer[dst].Remove ();

  if (m_nonPropReqTimer[dst].IsRunning ()) std::cout << "DsrRouting::CancelRreqTimer->Timer not canceled" << std::endl;
  m_nonPropReqTimer.erase (dst);

  // Cancel the address request timer if found
  if (m_addressReqTimer.find (dst) == m_addressReqTimer.end ()){
    std::cout << "DsrRouting::CancelRreqTimer->The propagation timer is NOT find." << std::endl;
  } else {
    std::cout << "DsrRouting::CancelRreqTimer->The propagation timer is find." << std::endl;
  }

  m_addressReqTimer[dst].Cancel ();
  m_addressReqTimer[dst].Remove ();
  if (m_addressReqTimer[dst].IsRunning ()) std::cout << "DsrRouting::CancelRreqTimer->Timer not canceled" << std::endl;
  m_addressReqTimer.erase (dst);
  
  // If the route request is scheduled to remove the route request entry
  // Remove the route request entry with the route retry times done for certain destination
  // 経路要求が経路要求エントリを削除するようにスケジュールされている場合、特定の宛先に対して行われたルート再試行時間で経路要求エントリを削除.
  if (isRemove) m_rreqTable->RemoveRreqEntry (dst); // remove the route request entry from route request table
}

//------------------------------------------------------------------------------------------------------------------------------
//経路要求タイマーを再試行する
void
DsrRouting::ScheduleRreqRetry (
  Ptr<Packet> packet, 
  std::vector<Ipv4Address> address, 
  bool nonProp, 
  uint32_t requestId, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << packet << nonProp << requestId << (uint32_t)protocol);
  std::cout << "DsrRouting::ScheduleRreqRetry->RequestID:" << requestId << ", Packet:" << *packet << std::endl;
  Ipv4Address source = address[0]; // 送信元
  Ipv4Address dst = address[1]; // 宛先

  if (nonProp){
    // The nonProp route request is only sent out only and is already used.
    if (m_nonPropReqTimer.find (dst) == m_nonPropReqTimer.end ()){
      Timer timer (Timer::CANCEL_ON_DESTROY);
      m_nonPropReqTimer[dst] = timer;
    }
    std::vector<Ipv4Address> address;
    address.push_back (source);
    address.push_back (dst);
    m_nonPropReqTimer[dst].SetFunction (&DsrRouting::RouteRequestTimerExpire, this);
    m_nonPropReqTimer[dst].Remove ();
    m_nonPropReqTimer[dst].SetArguments (packet, address, requestId, protocol);
    m_nonPropReqTimer[dst].Schedule (m_nonpropRequestTimeout);
  } else {
    // Cancel the non propagation request timer if found
    m_nonPropReqTimer[dst].Cancel ();
    m_nonPropReqTimer[dst].Remove ();
    if (m_nonPropReqTimer[dst].IsRunning ()) std::cout << "DsrRouting::ScheduleRreqRetry->Timer not canceled." << std::endl;
    m_nonPropReqTimer.erase (dst);

    if (m_addressReqTimer.find (dst) == m_addressReqTimer.end ()){
      Timer timer (Timer::CANCEL_ON_DESTROY);
      m_addressReqTimer[dst] = timer;
    }
    std::vector<Ipv4Address> address;
    address.push_back (source);
    address.push_back (dst);
    m_addressReqTimer[dst].SetFunction (&DsrRouting::RouteRequestTimerExpire, this);
    m_addressReqTimer[dst].Remove ();
    m_addressReqTimer[dst].SetArguments (packet, address, requestId, protocol);
    Time rreqDelay;
    // back off mechanism for sending route requests
    if (m_rreqTable->GetRreqCnt (dst)){
      // When the route request count is larger than 0.
      // This is the exponential back-off mechanism for route request.
      rreqDelay = Time (std::pow (static_cast<double> (m_rreqTable->GetRreqCnt (dst)), 2.0) * m_requestPeriod);
    } else {
      // This is the first route request retry
      rreqDelay = m_requestPeriod;
    }
    std::cout << "DsrRouting::ScheduleRreqRetry->Request count for " << dst << ", [Couter]:" << m_rreqTable->GetRreqCnt (dst) << ", DelayTime:" << rreqDelay.GetSeconds () << " second" << std::endl;
    if (rreqDelay > m_maxRequestPeriod){ // use the max request period
      std::cout << "DsrRouting::ScheduleRreqRetry->MaxRequestDelayTime:" << m_maxRequestPeriod.GetSeconds () << std::endl;
      m_addressReqTimer[dst].Schedule (m_maxRequestPeriod);
    } else {
      std::cout << "DsrRouting::ScheduleRreqRetry->RequestDelayTime:" << rreqDelay.GetSeconds () << " second." << std::endl;
      m_addressReqTimer[dst].Schedule (rreqDelay);
    }
  }
}

//----------------------------------------------------------------------------------------------------------------
// 経路要求タイマー処理
void
DsrRouting::RouteRequestTimerExpire (
  Ptr<Packet> packet, 
  std::vector<Ipv4Address> address, 
  uint32_t requestId, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << packet << requestId << (uint32_t)protocol);
  std::cout << "DsrRouting::RouteRequestTimerExpire->RequestID:" << requestId  << ". Packet:" << *packet << std::endl;

  // Get a clean packet without dsr header
  Ptr<Packet> dsrP = packet->Copy ();
  DsrRoutingHeader dsrRoutingHeader;
  dsrP->RemoveHeader (dsrRoutingHeader); // Remove the dsr header in whole

  Ipv4Address source = address[0];
  Ipv4Address dst = address[1];
  DsrRouteCacheEntry toDst;
  // 追加
  bool flagUseCache = m_routeCache->LookupRoute (dst, toDst);
  flagUseCache = false; // adding by shotakubota.
  if (flagUseCache){ // ここまで！！！
  //if(m_routeCache->LookupRoute (dst, toDst)){
    std::cout << "DsrRouting::RouteRequestTimerExpire->ルートキャッシュが宛先への経路を持っている" << std::endl;
    // Found a route the dst, construct the source route option header
    DsrOptionSRHeader sourceRoute;
    std::vector<Ipv4Address> ip = toDst.GetVector ();
    sourceRoute.SetNodesAddress (ip);
    // When we found the route and use it, UseExtends for the link cache
    if (m_routeCache->IsLinkCache ()) m_routeCache->UseExtends (ip);

    sourceRoute.SetSegmentsLeft ((ip.size () - 2));
    sourceRoute.SetSalvage (0); /// Set the salvage value to 0
    Ipv4Address nextHop = SearchNextHop (m_mainAddress, ip); // Get the next hop address
    std::cout << "DsrRouting::RouteRequestTimerExpire->The nextHop address is " << nextHop << std::endl;
    if (nextHop == "0.0.0.0"){
      std::cout << "DsrRouting::RouteRequestTimerExpire->Error next hop address" << std::endl;
      PacketNewRoute (dsrP, source, dst, protocol);
      return;
    }
    SetRoute (nextHop, m_mainAddress);
    CancelRreqTimer (dst, true);
    /// Try to send out data packet from the send buffer if found
    if (m_sendBuffer.GetSize () != 0 && m_sendBuffer.Find (dst)) SendPacketFromBuffer (sourceRoute, nextHop, protocol);
    std::cout << "DsrRouting::RouteRequestTimerExpire->Route to " << dst << " found" << std::endl;
    return;
  }
  
  // If a route discovery has been attempted m_rreqRetries times at the maximum TTL without
  //  receiving any RREP, all data packets destined for the corresponding destination SHOULD be
  //  dropped from the buffer and a Destination Unreachable message SHOULD be delivered to the application.
  std::cout << "DsrRouting::RouteRequestTimerExpire->The new request count for:" << dst << " is [RREQCounter]:" << m_rreqTable->GetRreqCnt (dst) << " the max[RREQCountMAX] " << m_rreqRetries << std::endl;
  if (m_rreqTable->GetRreqCnt (dst) >= m_rreqRetries){
    std::cout << "DsrRouting::RouteRequestTimerExpire->宛先に到達する前にRREQの試行回数が最大に達した!!!" << std::endl;
    //std::cout << "DsrRouting::RouteRequestTimerExpire->Route discovery to " << dst << " has been attempted " << m_rreqRetries << " times" << std::endl;
    CancelRreqTimer (dst, true);
    std::cout << "DsrRouting::RouteRequestTimerExpire->Route not found. Drop packet with dst " << dst << std::endl;
    m_sendBuffer.DropPacketWithDst (dst);
  } else {
    SocketIpTtlTag tag;
    tag.SetTtl ((uint8_t)m_discoveryHopLimit);
    Ptr<Packet> propPacket = packet->Copy ();
    propPacket->AddPacketTag (tag);
    // Increase the request count
    m_rreqTable->FindAndUpdate (dst);
    SendRequest (propPacket, source);
    ScheduleRreqRetry (packet, address, false, requestId, protocol);
  }
  return;
}

//------------------------------------------------------------------------------------------------------
// ノードが宛先でない場合、RREQを転送
void
DsrRouting::SendRequest (
  Ptr<Packet> packet,
  Ipv4Address source
  )
{
  NS_LOG_FUNCTION (this << packet << source);
  std::cout << "##### SendRequest->Source:" << source << std::endl;
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");

  // The destination address here is directed broadcast address
  uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second; // ネットワークキューを入れ込む
  //std::cout << "##### SendRequest->Inserting into PriorityQueueNumber:" << priority << std::endl;

  // 追加
  /*Time myInterval;
  int m_myLoad = 0;
  Ptr<ns3::Application> app;
  for(std::map<Ipv4Address, int>::iterator it = app->g_myNodeLoad.begin (); it != app->g_myNodeLoad.end (); it++){
    if( it->first == source ){
      m_myLoad = it->second;
      break;
    }
  } if (m_myLoad <= 0) {
    myInterval = MicroSeconds (m_uniformRandomVariable->GetInteger (0, 10)) + Simulator::Now ();
  } else {
    myInterval = MicroSeconds (m_uniformRandomVariable->GetInteger (0, 10)) + MicroSeconds (500 * m_myLoad) + Simulator::Now ();
  }

  std::cout << "SendRequest->Src" << source << ", Load:" << m_myLoad << ", Time now"<<Simulator::Now ()<<", Time:" << myInterval << std::endl;
  DsrNetworkQueueEntry newEntry (packet, source, m_broadcast, myInterval, 0);
  */

  /// \todo New DsrNetworkQueueEntry
  DsrNetworkQueueEntry newEntry (packet, source, m_broadcast, Simulator::Now (), 0);
  if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
  else std::cout << "##### SendRequest->Packet dropped as dsr network queue is FULL." << std::endl;
}

//------------------------------------------------------------------------------------------------------
void
DsrRouting::ScheduleInterRequest (
  Ptr<Packet> packet
  )
{
  NS_LOG_FUNCTION (this << packet);
  // This is a forwarding case when sending route requests, a random delay time [0, m_broadcastJitter] used before forwarding as link-layer broadcast
  int m_myLoad = 0;
  Time myInterval;
  Ptr<ns3::Application> app;
  for(std::map<Ipv4Address, int>::iterator it = app->g_myNodeLoad.begin (); it != app->g_myNodeLoad.end (); ++it){
    if( it->first == m_mainAddress ){
      m_myLoad = it->second;
      break;
    }
  }
  std::cout << "<<<<< DsrRouting::ScheduleInterRequest(中間経路要求)->IP:" << m_mainAddress << ", Load:" << m_myLoad << std::endl;
  if (m_myLoad <= 0) {
    myInterval = MilliSeconds (9) + MicroSeconds (m_uniformRandomVariable->GetInteger (0, m_broadcastJitter));
  } else {
    myInterval = MilliSeconds (9) + MicroSeconds (m_uniformRandomVariable->GetInteger (0, m_broadcastJitter)) + MicroSeconds (500 * m_myLoad);
  }
  Simulator::Schedule (myInterval, &DsrRouting::SendRequest, this, packet, m_mainAddress);

  //Simulator::Schedule (MilliSeconds (9) + MicroSeconds (m_uniformRandomVariable->GetInteger (0, m_broadcastJitter)), &DsrRouting::SendRequest, this, packet, m_mainAddress);
  //Simulator::Schedule (MilliSeconds(m_uniformRandomVariable->GetInteger (0, m_broadcastJitter)), &DsrRouting::SendRequest, this, packet, m_mainAddress);
  
}

//------------------------------------------------------------------------------------------------------
void
DsrRouting::SendGratuitousReply (
  Ipv4Address source, 
  Ipv4Address srcAddress, 
  std::vector<Ipv4Address> &nodeList, 
  uint8_t protocol
  )
{
  NS_LOG_FUNCTION (this << source << srcAddress << (uint32_t)protocol);
  if (!(m_graReply.FindAndUpdate (source, srcAddress, m_gratReplyHoldoff))){     // Find the gratuitous reply entry
    std::cout << "DsrRouting::SendGratuitousReply->UpdateGratuitousReply:Source" << source << ", SourceAddress:" << srcAddress << std::endl;
    GraReplyEntry graReplyEntry (source, srcAddress, m_gratReplyHoldoff + Simulator::Now ());
    m_graReply.AddEntry (graReplyEntry);
    
    // Automatic route shortening
    m_finalRoute.clear (); // Clear the final route vector
    
    // Push back the node addresses other than those between srcAddress and our own ip address
    std::vector<Ipv4Address>::iterator before = find (nodeList.begin (), nodeList.end (), srcAddress);
    for (std::vector<Ipv4Address>::iterator i = nodeList.begin (); i != before; ++i){
      m_finalRoute.push_back (*i);
    }
    m_finalRoute.push_back (srcAddress);
    std::vector<Ipv4Address>::iterator after = find (nodeList.begin (), nodeList.end (), m_mainAddress);
    for (std::vector<Ipv4Address>::iterator j = after; j != nodeList.end (); ++j){
      m_finalRoute.push_back (*j);
    }
    DsrOptionRrepHeader rrep;
    rrep.SetNodesAddress (m_finalRoute);           // Set the node addresses in the route reply header
    // Get the real reply source and destination
    Ipv4Address replySrc = m_finalRoute.back ();
    Ipv4Address replyDst = m_finalRoute.front ();
    
    // Set the route and use it in send back route reply
    m_ipv4Route = SetRoute (srcAddress, m_mainAddress);
    
    // This part adds DSR header to the packet and send reply
    DsrRoutingHeader dsrRoutingHeader;
    dsrRoutingHeader.SetNextHeader (protocol);
    dsrRoutingHeader.SetMessageType (1);
    dsrRoutingHeader.SetSourceId (GetIDfromIP (replySrc));
    dsrRoutingHeader.SetDestId (GetIDfromIP (replyDst));

    uint8_t length = rrep.GetLength (); // Get the length of the rrep header excluding the type header
    dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
    dsrRoutingHeader.AddDsrOption (rrep);
    Ptr<Packet> newPacket = Create<Packet> ();
    newPacket->AddHeader (dsrRoutingHeader);
    //std::cout << "DsrRouting::SendGratuitousReply->Send back gratuitous route reply." << std::endl;
    SendReply (newPacket, m_mainAddress, srcAddress, m_ipv4Route); // Send gratuitous reply
  } else {
    std::cout << "DsrRouting::SendGratuitousReply->The same gratuitous route reply has already sent." << std::endl;
  }
}

void
DsrRouting::SendReply (
  Ptr<Packet> packet,
  Ipv4Address source,
  Ipv4Address nextHop,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << packet << source << nextHop);
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");

  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (m_mainAddress));
  route->SetOutputDevice (dev);
  std::cout << "##### DsrRouting::SendReply->Source" << source << ", Nexthop:" << nextHop << ", OutputDevice:" << dev << ", Packet:" << *packet << std::endl;

  uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
  //std::cout << "##### DsrRouting::SendReply->Inserting into priority queue number: " << priority << std::endl;

  IncreaseMyAppLoad(source); // グローバル変数のRREPを増加

  /// \todo New DsrNetworkQueueEntry
  DsrNetworkQueueEntry newEntry (packet, source, nextHop, Simulator::Now (), route);
  if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
  else std::cout << "##### DsrRouting::SendReply->Packet dropped as dsr network queue is full." << std::endl;
}

//----------------------------------------------------------------------------------------------------------
void
DsrRouting::ScheduleInitialReply (
  Ptr<Packet> packet,
  Ipv4Address source,
  Ipv4Address nextHop,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << packet << source << nextHop);
  std::cout << "<<<<< DsrRouting::ScheduleInitialReply->Source:" << source << ", NextHop:" << nextHop << ", Packet:" << *packet << std::endl;
  Simulator::ScheduleNow (&DsrRouting::SendReply, this, packet, source, nextHop, route);
}

void
DsrRouting::ScheduleCachedReply (
  Ptr<Packet> packet,
  Ipv4Address source,
  Ipv4Address destination,
  Ptr<Ipv4Route> route,
  double hops
  )
{
  NS_LOG_FUNCTION (this << packet << source << destination);
  std::cout << "DsrRouting::ScheduleCachedReply->Source:" << source << ", Destination:" << destination << std::endl;
  Simulator::Schedule (Time (2 * m_nodeTraversalTime * (hops - 1 + m_uniformRandomVariable->GetValue (0,1))), &DsrRouting::SendReply, this, packet, source, destination, route);
}

void
DsrRouting::SendAck (
  uint16_t ackId,
  Ipv4Address destination,
  Ipv4Address realSrc,
  Ipv4Address realDst,
  uint8_t protocol,
  Ptr<Ipv4Route> route
  )
{
  NS_LOG_FUNCTION (this << ackId << destination << realSrc << realDst << (uint32_t)protocol << route);
  NS_ASSERT_MSG (!m_downTarget.IsNull (), "Error, DsrRouting cannot send downward");
  std::cout << "DsrRouting::SendAck->RealSource" << realSrc << ", RealDestination:" << realDst << ", Destination:" << destination << std::endl;

  // This is a route reply option header
  DsrRoutingHeader dsrRoutingHeader;
  dsrRoutingHeader.SetNextHeader (protocol);
  dsrRoutingHeader.SetMessageType (1);
  dsrRoutingHeader.SetSourceId (GetIDfromIP (m_mainAddress));
  dsrRoutingHeader.SetDestId (GetIDfromIP (destination));

  DsrOptionAckHeader ack;

  // Set the ack Id and set the ack source address and destination address
  ack.SetAckId (ackId);
  ack.SetRealSrc (realSrc);
  ack.SetRealDst (realDst);

  uint8_t length = ack.GetLength ();
  dsrRoutingHeader.SetPayloadLength (uint16_t (length) + 2);
  dsrRoutingHeader.AddDsrOption (ack);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (dsrRoutingHeader);
  Ptr<NetDevice> dev = m_ip->GetNetDevice (m_ip->GetInterfaceForAddress (m_mainAddress));
  route->SetOutputDevice (dev);

  uint32_t priority = GetPriority (DSR_CONTROL_PACKET);
  std::map<uint32_t, Ptr<dsr::DsrNetworkQueue> >::iterator i = m_priorityQueue.find (priority);
  Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
  //std::cout << "Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority << std::endl;

  /// \todo New DsrNetworkQueueEntry
  DsrNetworkQueueEntry newEntry (packet, m_mainAddress, destination, Simulator::Now (), route);
  if (dsrNetworkQueue->Enqueue (newEntry)) Scheduler (priority);
  else std::cout << "Packet dropped as dsr network queue is full" << std::endl;
}

//----------------------------------------------------------------------------------------------------
// 下位レベルのレイヤーから呼び出され、パケットをスタックに送信
// Lower layer calls this method after calling L3Demux::Lookup. The ARP subclass needs to know from which NetDevice this packet is coming to:
enum IpL4Protocol::RxStatus
DsrRouting::Receive (
  Ptr<Packet> p,
  Ipv4Header const &ip,
  Ptr<Ipv4Interface> incomingInterface
  )
{
  NS_LOG_FUNCTION (this << p << ip << incomingInterface);
  //std::cout << "DsrRouting::Receive->Our own IP address " << m_mainAddress << ". The incoming interface address " << incomingInterface << ", Packet" << *p << std::endl;
  m_node = GetNode ();             // Get the node
  Ptr<Packet> packet = p->Copy (); // Save a copy of the received packet
  
  // When forwarding or local deliver packets, this one should be used always!!
  DsrRoutingHeader dsrRoutingHeader;
  packet->RemoveHeader (dsrRoutingHeader); // Remove the DSR header in whole
  Ptr<Packet> copy = packet->Copy ();

  uint8_t protocol = dsrRoutingHeader.GetNextHeader ();
  uint32_t sourceId = dsrRoutingHeader.GetSourceId ();
  Ipv4Address source = GetIPfromID (sourceId);
  
  Ipv4Address src = ip.GetSource (); // Get the IP source and destination address
  bool isPromisc = false;
  uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset (); // Get the offset for option header, 8 bytes in this case

  // This packet is used to peek option type
  p->RemoveAtStart (offset);
  Ptr<dsr::DsrOptions> dsrOption;
  DsrOptionHeader dsrOptionHeader;
  
  // Peek data to get the option type as well as length and segmentsLeft field
  uint32_t size = p->GetSize ();
  uint8_t *data = new uint8_t[size];
  p->CopyData (data, size);

  uint8_t optionType = 0;
  uint8_t optionLength = 0;
  uint8_t segmentsLeft = 0;

  optionType = *(data); // パケットのサイズ
  dsrOption = GetOption (optionType); // Get the relative dsr option and demux to the process function
  Ipv4Address promiscSource; /// this is just here for the sake of passing in the promisc source
  std::cout << "##### DsrRouting::Receive->Source:" << source << ", ReceiverIP:" << m_mainAddress << ", OPTIONTYPE:" << (uint32_t)optionType << ",RREQ:1,RREP:2,RERR:3,SR:96,ACK:32." << std::endl;
  
  if (optionType == 1) { // This is the request option(RREQ)
    BlackList *blackList = m_rreqTable->FindUnidirectional (src); // 片方向リンクの検出.
    if (blackList){
      std::cout << "##### DsrRouting::Receive->(片方向リンクの検出)Discard this packet due to unidirectional link" << std::endl;
      m_dropTrace (p);
    }
    dsrOption = GetOption (optionType);
    optionLength = dsrOption->Process (p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);
    if (optionLength == 0){
      std::cout << "##### DsrRouting::Receive->Discard this packet." << std::endl;
      m_dropTrace (p);
    }
  }
  else if (optionType == 2) { // RREP
    dsrOption = GetOption (optionType);
    optionLength = dsrOption->Process (p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);
    if (optionLength == 0){
      std::cout << "##### DsrRouting::Receive->Discard this packet." << std::endl;
      m_dropTrace (p);
    }
  }
  else if (optionType == 32) { // ACK option
    std::cout << "##### DsrRouting::Receive->This is the ack option." << std::endl;
    dsrOption = GetOption (optionType);
    optionLength = dsrOption->Process (p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);
    if (optionLength == 0){
      std::cout << "##### DsrRouting::Receive->Discard this packet." << std::endl;
      m_dropTrace (p);
    }
  }
  else if (optionType == 3){       // RERR header
    // populate this route error
    dsrOption = GetOption (optionType);
    optionLength = dsrOption->Process (p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);
    if (optionLength == 0){
      std::cout << "##### DsrRouting::Receive->Discard this packet" << std::endl;
      m_dropTrace (p);
    }
    //std::cout << "##### DsrRouting::Receive->The option type value " << (uint32_t)optionType << std::endl;
  }
  else if (optionType == 96){ // This is the source route option
    dsrOption = GetOption (optionType);
    optionLength = dsrOption->Process (p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);
    segmentsLeft = *(data + 3);
    if (optionLength == 0) {
      std::cout << "##### DsrRouting::Receive->Discard this packet." << std::endl;
      m_dropTrace (p);
    }
    else{
      if (segmentsLeft == 0){
        // / Get the next header
        uint8_t nextHeader = dsrRoutingHeader.GetNextHeader ();
        Ptr<Ipv4L3Protocol> l3proto = m_node->GetObject<Ipv4L3Protocol> ();
        Ptr<IpL4Protocol> nextProto = l3proto->GetProtocol (nextHeader);
        if (nextProto != 0){
          // we need to make a copy in the unlikely event we hit the RX_ENDPOINT_UNREACH code path.
          // Here we can use the packet that has been get off whole DSR header.
          enum IpL4Protocol::RxStatus status = nextProto->Receive (copy, ip, incomingInterface);
          //std::cout << "##### DsrRouting::Receive->The receive status " << status << std::endl;
          switch (status){
            case IpL4Protocol::RX_OK:
              std::cout << "##### DsrRouting::Receive->IpL4Protocol::RX_OK" << std::endl;
            // fall through
            case IpL4Protocol::RX_ENDPOINT_CLOSED:
              std::cout << "##### DsrRouting::Receive->IpL4Protocol::RX_ENDPOINT_CLOSED" << std::endl;
            // fall through
            case IpL4Protocol::RX_CSUM_FAILED:
              std::cout << "##### DsrRouting::Receive->IpL4Protocol::RX_CSUM_FAILED" << std::endl;
              break;
            case IpL4Protocol::RX_ENDPOINT_UNREACH:
              std::cout << "##### DsrRouting::Receive->IpL4Protocol::RX_ENDPOINT_UNREACH" << std::endl;
              if (ip.GetDestination ().IsBroadcast () == true || ip.GetDestination ().IsMulticast () == true){
                break; // Do not reply to broadcast or multicast
              }
              // Another case to suppress ICMP is a subnet-directed broadcast
          }
          return status;
        }
        else{ 
          std::cout << "##### DsrRouting::Receive->Should not have 0 next protocol value" << std::endl;
        }
      }
      else{
        std::cout << "##### DsrRouting::Receive->This is not the final destination, the packet has already been forward to next hop" << std::endl;
      }
    }
  }else{
    std::cout << "##### DsrRouting::Receive->Unknown Option. Drop!" << std::endl;
    // Initialize the salvage value to 0
    uint8_t salvage = 0;

    DsrOptionRerrUnsupportHeader rerrUnsupportHeader;
    rerrUnsupportHeader.SetErrorType (3);               // The error type 3 means Option not supported
    rerrUnsupportHeader.SetErrorSrc (m_mainAddress);    // The error source address is our own address
    rerrUnsupportHeader.SetUnsupported (optionType);    // The unsupported option type number
    rerrUnsupportHeader.SetErrorDst (src);              // Error destination address is the destination of the data packet
    rerrUnsupportHeader.SetSalvage (salvage);           // Set the value about whether to salvage a packet or not

    // The unknow option error is not supported currently in this implementation, and it's also not likely to happen in simulations
    //SendError (rerrUnsupportHeader, 0, protocol); // Send the error packet
  }
  return IpL4Protocol::RX_OK;
}

enum IpL4Protocol::RxStatus
DsrRouting::Receive (
  Ptr<Packet> p,
  Ipv6Header const &ip,
  Ptr<Ipv6Interface> incomingInterface
  )
{
  std::cout << "##### DsrRouting::Receive->" << p << ip.GetSourceAddress () << ip.GetDestinationAddress () << incomingInterface << std::endl;
  return IpL4Protocol::RX_ENDPOINT_UNREACH;
}

void 
DsrRouting::IncreaseMyAppLoad (
  Ipv4Address id
  )
{
  // キーを削除して、値を入れ直す....
  int m_variable = 0;
  Ptr<ns3::Application> app;
  std::map<Ipv4Address, int>::iterator m_prov = app->g_myNodeLoad.find(id);
  if(m_prov != app->g_myNodeLoad.end()){
    m_variable = m_prov->second;
    m_variable++; // 負荷を増加
    app->g_myNodeLoad.erase (id); // 一時的に削除して、
    app->g_myNodeLoad.insert (std::make_pair (id, m_variable)); // 増やした、値を入れ直す
    std::cout << "DsrRouting::IncreaseMyAppLoad->IPAddress:" << id << ", Load:" << m_variable << std::endl; 
  }
}

void 
DsrRouting::DecreaseMyAppLoad (
  Ipv4Address id
  )
{
  int m_variable = 0;
  Ptr<ns3::Application> app;
  std::map<Ipv4Address, int>::iterator m_prov = app->g_myNodeLoad.find(id);
  if(m_prov != app->g_myNodeLoad.end()){
    m_variable = m_prov->second;
    m_variable--;
    app->g_myNodeLoad.erase (id);
    app->g_myNodeLoad.insert (std::make_pair (id,m_variable));
    std::cout << "DsrRouting::IncreaseMyAppLoad->IPAddress:" << id << ", Load:" << m_variable << std::endl; 
  }
}

void
DsrRouting::SetDownTarget (DownTargetCallback callback)
{
  //std::cout << "DsrRouting::SetDownTarget" << std::endl;
  m_downTarget = callback;
}

void
DsrRouting::SetDownTarget6 (DownTargetCallback6 callback)
{
  //std::cout << "DsrRouting::SetDownTarget6" << std::endl;
  NS_FATAL_ERROR ("Unimplemented");
}

IpL4Protocol::DownTargetCallback
DsrRouting::GetDownTarget (void) const
{
  //std::cout << "DsrRouting::GetDownTarget" << std::endl;
  return m_downTarget;
}

IpL4Protocol::DownTargetCallback6
DsrRouting::GetDownTarget6 (void) const
{
  //std::cout << "DsrRouting::GetDownTarget6" << std::endl;
  NS_FATAL_ERROR ("Unimplemented"); 
  return MakeNullCallback<void,Ptr<Packet>, Ipv6Address, Ipv6Address, uint8_t, Ptr<Ipv6Route> > ();
}

//--------------------------------------------------------------
void 
DsrRouting::Insert (Ptr<dsr::DsrOptions> option)
{
  //std::cout << "DsrRouting::Insert->送信キューにDSRオプションを挿入" << std::endl;
  m_options.push_back (option);
}

Ptr<dsr::DsrOptions> DsrRouting::GetOption (int optionNumber)
{
  //std::cout << "DsrRouting::GetOption" << std::endl;
  for (DsrOptionList_t::iterator i = m_options.begin (); i != m_options.end (); ++i) {
    if ((*i)->GetOptionNumber () == optionNumber) {
        return *i;
    }
  }
  return 0;
}
}  /* namespace dsr */
}  /* namespace ns3 */
