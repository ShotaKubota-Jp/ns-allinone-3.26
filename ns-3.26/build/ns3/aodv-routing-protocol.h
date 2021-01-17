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
#ifndef AODVROUTINGPROTOCOL_H
#define AODVROUTINGPROTOCOL_H

#include "aodv-rtable.h"
#include "aodv-rqueue.h"
#include "aodv-packet.h"
#include "aodv-neighbor.h"
#include "aodv-dpd.h"
#include "ns3/node.h"
#include "ns3/random-variable-stream.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include <map>

namespace ns3
{
namespace aodv
{
/**
 * \ingroup aodv
 * \brief AODV routing protocol
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
public:
  static TypeId GetTypeId (void);
  static const uint32_t AODV_PORT;

  RoutingProtocol ();         // コンストラクタ
  virtual ~RoutingProtocol(); // デクストラクタ
  virtual void DoDispose ();  // 廃棄する処理

  // Inherited from Ipv4RoutingProtocol
  // パケットの転送経路を決定する処理関数
  Ptr<Ipv4Route> RouteOutput (
    Ptr<Packet> p,               // ルーティングされるパケット
    const Ipv4Header &header,    // ヘッダ入力(ルートを検索するためのキーを形成するために使用)
    Ptr<NetDevice> oif,          // Output Interface. ゼロでも使用可能.ソケットを介してバインドすることも可能
    Socket::SocketErrno &sockerr // socketerror出力.ソケットエラー
  );
  // 受信パケットの配送処理関数
  bool RouteInput (
    Ptr<const Packet> p,           // 受け取ったパケット
    const Ipv4Header &header,      // ヘッダ入力(ルートを検索するためのキーを形成するために使用)
    Ptr<const NetDevice> idev,     // 入力ネットワークデバイスのポインタ
    UnicastForwardCallback ucb,    // ucbパケットがユニキャストとして転送される場合コールバック
    MulticastForwardCallback mcb,  // mcbパケットがマルチキャストとして転送される場合コールバック
    LocalDeliverCallback lcb,      // lcbパケットがローカル転送として転送される場合コールバック
    ErrorCallback ecb              // ecb転送寺にがエラーが発生した場合コールバック
  );
  // I/Fが使用可能になったときの処理関数
  virtual void NotifyInterfaceUp (uint32_t interface);
  // I/Fが使用不可能になったときの処理関数
  virtual void NotifyInterfaceDown (uint32_t interface);
  // I/Fに新しいIPアドレスを指定したときの処理関数
  virtual void NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address);
  // I/FにIPアドレスを削除したときの処理関数
  virtual void NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address);
  // Ipv4のインスタンスを取得する関数
  virtual void SetIpv4 (Ptr<Ipv4> ipv4);
  // ルーティングテーブルを出力する関数
  virtual void PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const;
  virtual void PrintVariable () const;

  // Handle protocol parameters
  Time GetMaxQueueTime () const { return m_maxQueueTime; }
  void SetMaxQueueTime (Time t);
  uint32_t GetMaxQueueLen () const { return m_maxQueueLen; }
  void SetMaxQueueLen (uint32_t len);
  bool GetDesinationOnlyFlag () const { return m_destinationOnly; }
  void SetDesinationOnlyFlag (bool f) { m_destinationOnly = f; }
  bool GetGratuitousReplyFlag () const { return m_gratuitousReply; }
  void SetGratuitousReplyFlag (bool f) { m_gratuitousReply = f; }
  void SetHelloEnable (bool f) { m_enableHello = f; }
  bool GetHelloEnable () const { return m_enableHello; }
  void SetBroadcastEnable (bool f) { m_enableBroadcast = f; }
  bool GetBroadcastEnable () const { return m_enableBroadcast; }
  // added by skubota.
  void SetMyLoadReq(uint32_t myLoadReq){ m_myLoadReq = myLoadReq; }
  uint32_t GetMyLoadReq(){ return m_myLoadReq; }
  void IncrementMyLoadReqCnt (uint32_t m_myLoad) { m_myLoadReq += m_myLoad; }
  void SetMyLoadRep(uint32_t myLoadRep){ m_myLoadRep = myLoadRep; }
  uint32_t GetMyLoadRep(){ return m_myLoadRep; }
  void IncrementMyLoadRepCnt (uint32_t m_myLoad) { m_myLoadRep += m_myLoad; }
  void IncrementMyLoadRepCnt () { m_myLoadRep++; }

 /**
  * Assign a fixed random variable stream number to the random variables used by this model.  
  * Return the number of streams (possibly zero) that have been assigned.
  * \param stream first stream index to use
  * \return the number of stream indices assigned by this model
  * このモデルで使用されるランダム変数に固定のランダム変数ストリーム番号を割り当てます.
  * 割り当てられたストリームの数（場合によってはゼロ）を返します.
  */
  int64_t AssignStreams (int64_t stream);

protected:
  virtual void DoInitialize (void);

private:
  // Protocol parameters.
  uint32_t m_rreqRetries;       ///< Maximum number of retransmissions of RREQ with TTL = NetDiameter to discover a route
  uint16_t m_ttlStart;          ///< Initial TTL value for RREQ.
  uint16_t m_ttlIncrement;      ///< TTL increment for each attempt using the expanding ring search for RREQ dissemination.
  uint16_t m_ttlThreshold;      ///< Maximum TTL value for expanding ring search, TTL = NetDiameter is used beyond this value.
  uint16_t m_timeoutBuffer;     ///< Provide a buffer for the timeout.
  uint16_t m_rreqRateLimit;     ///< Maximum number of RREQ per second.
  uint16_t m_rerrRateLimit;     ///< Maximum number of REER per second.
  Time m_activeRouteTimeout;    ///< Period of time during which the route is considered to be valid.
  uint32_t m_netDiameter;       ///< Net diameter measures the maximum possible number of hops between two nodes in the network
  /**
   *  NodeTraversalTime is a conservative estimate of the average one hop traversal time for packets
   *  and should include queuing delays, interrupt processing times and transfer times.
   *  NodeTraversalTimeは、パケットの平均ホップトラバーサル時間を控えめに見積もったもので、
   *  キューイング遅延、割り込み処理時間、および転送時間を含める必要があります。
   */
  Time m_nodeTraversalTime;
  Time m_netTraversalTime;   ///< 平均ネットトラバーサル時間の見積もり / Estimate of the average net traversal time.
  Time m_pathDiscoveryTime;  ///< ネットワーク内のルートを見つけるのに必要な最大時間の見積もり / Estimate of maximum time needed to find route in network.
  Time m_myRouteTimeout;     ///< このノードによって生成されるRREPの有効期間フィールドの値 / Value of lifetime field in RREP generating by this node.
  /**
   * Every HelloInterval the node checks whether it has sent a broadcast  within the last HelloInterval.
   * If it has not, it MAY broadcast a  Hello message
   */
  Time m_helloInterval;
  uint32_t m_allowedHelloLoss;  ///< Number of hello messages which may be loss for valid link
  /**
   * DeletePeriod is intended to provide an upper bound on the time for which an upstream node A
   * can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D.
   * DeletePeriodは、上流ノードAが宛先Bのアクティブネクストホップとして
   * 隣接Bを有することができる時間の上限を提供することを意図しているが、BはDへの経路を無効化している。
   */
  Time m_deletePeriod;
  Time m_nextHopWait;       ///< Period of our waiting for the neighbour's RREP_ACK
  Time m_blackListTimeout;  ///< Time for which the node is put into the blacklist
  uint32_t m_maxQueueLen;   ///< The maximum number of packets that we allow a routing protocol to buffer.
  Time m_maxQueueTime;      ///< The maximum period of time that a routing protocol is allowed to buffer a packet for.
  bool m_destinationOnly;   ///< Indicates only the destination may respond to this RREQ.
  bool m_gratuitousReply;   ///< Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.
  bool m_enableHello;       ///< Indicates whether a hello messages enable
  bool m_enableBroadcast;   ///< Indicates whether a a broadcast data packets forwarding enable

  /// IP protocol
  Ptr<Ipv4> m_ipv4;
  /// 各IPインタフェースごとの未処理のユニキャストソケット、マップソケット - > ifaceアドレス（IP +マスク）
  /// Raw unicast socket per each IP interface, map socket -> iface address (IP + mask)
  std::map< Ptr<Socket>, Ipv4InterfaceAddress > m_socketAddresses;
  /// 各IPインタフェースごとに未処理のサブネット指向のブロードキャストソケット、マップソケット - ifaceアドレス（IP +マスク）
  /// Raw subnet directed broadcast socket per each IP interface, map socket -> iface address (IP + mask)
  std::map< Ptr<Socket>, Ipv4InterfaceAddress > m_socketSubnetBroadcastAddresses;
  /// パケットが完全に形成されるまでRREQを延期するために使用されるループバックデバイス
  /// Loopback device used to defer RREQ until packet will be fully formed
  Ptr<NetDevice> m_lo; 

  /// Routing table
  RoutingTable m_routingTable;
  /// A "drop-front" queue used by the routing layer to buffer packets to which it does not have a route.
  RequestQueue m_queue;
  /// Broadcast ID
  uint32_t m_requestId;
  /// Request sequence number
  uint32_t m_seqNo;
  /// Handle duplicated RREQ
  IdCache m_rreqIdCache;
  /// Handle duplicated broadcast/multicast packets
  DuplicatePacketDetection m_dpd;
  /// Handle neighbors
  Neighbors m_nb;
  /// Number of RREQs used for RREQ rate control
  uint16_t m_rreqCount;
  /// Number of RERRs used for RERR rate control
  uint16_t m_rerrCount;
  // 負荷(RREQ)
  uint32_t m_myLoadReq;
  // 負荷(RREP)
  uint32_t m_myLoadRep;

private:
  /// プロトコルの開始処理 / Start protocol operation
  void Start ();
  /// Queue packet and send route request
  void DeferredRouteOutput (Ptr<const Packet> p, const Ipv4Header & header, UnicastForwardCallback ucb, ErrorCallback ecb);
  /// 使用可能な経路があれば、パケットを転送する / If route exists and valid, forward packet.
  bool Forwarding (Ptr<const Packet> p, const Ipv4Header & header, UnicastForwardCallback ucb, ErrorCallback ecb);
  /**
   * Repeated attempts by a source node at route discovery for a single destination use the expanding ring search technique.
   * 1つの宛先のルート探索時に送信元ノードが繰り返し試行すると、拡張リング検索技術が使用されます.
   */
  void ScheduleRreqRetry (Ipv4Address dst);
  /**
   * Set lifetime field in routing table entry to the maximum of existing lifetime and lt, if the entry exists
   * \param addr - destination address
   * \param lt - proposed time for lifetime field in routing table entry for destination with address addr.
   * \return true if route to destination address addr exist
   * ルーティングテーブルエントリの有効期間フィールドを既存の有効期間の最大値に設定し、エントリが存在する場合はltに設定
   * \param addr - 宛先アドレス
   * \param lt - アドレスaddrを持つ宛先のルーティングテーブルエントリの有効期間フィールドの提案時間。
   * \宛先アドレスaddrへのルートが存在する場合はtrueを返します。
   */
  bool UpdateRouteLifeTime (Ipv4Address addr, Time lt);
  /**
   * Update neighbor record.
   * \param receiver is supposed to be my interface
   * \param sender is supposed to be IP address of my neighbor.
   */
  void UpdateRouteToNeighbor (Ipv4Address sender, Ipv4Address receiver);
  /// Check that packet is send from own interface
  bool IsMyOwnAddress (Ipv4Address src);
  /// ローカルI/Fアドレスifaceに対応するソケットを参照する / Find unicast socket with local interface address iface
  Ptr<Socket> FindSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const;
  /// Find subnet directed broadcast socket with local interface address iface
  Ptr<Socket> FindSubnetBroadcastSocketWithInterfaceAddress (Ipv4InterfaceAddress iface) const;
  /// Process hello message
  void ProcessHello (RrepHeader const & rrepHeader, Ipv4Address receiverIfaceAddr);
  /// 渡されたヘッダ情報によりループバック経路を生成する / Create loopback route for given header
  Ptr<Ipv4Route> LoopbackRoute (const Ipv4Header & header, Ptr<NetDevice> oif) const;

  ///\name Receive control packets
  /// Receive and process control packet
  void RecvAodv (Ptr<Socket> socket);
  /// Receive RREQ
  void RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
  /// Receive RREP
  void RecvReply (Ptr<Packet> p, Ipv4Address my,Ipv4Address src);
  /// Receive RREP_ACK
  void RecvReplyAck (Ipv4Address neighbor);
  /// Receive RERR from node with address src
  void RecvError (Ptr<Packet> p, Ipv4Address src);

  ///\name Send
  /// Forward packet from route request queue
  void SendPacketFromQueue (Ipv4Address dst, Ptr<Ipv4Route> route);
  /// Send hello
  void SendHello ();
  /// Send RREQ
  void SendRequest (Ipv4Address dst);
  /// Send RREP
  void SendReply (RreqHeader const & rreqHeader, RoutingTableEntry const & toOrigin);
  /** Send RREP by intermediate node
   * \param toDst routing table entry to destination
   * \param toOrigin routing table entry to originator
   * \param gratRep indicates whether a gratuitous RREP should be unicast to destination
   *****
   * 中間ノードによってRREPを送信する
   * \param 宛先へのtoDstルーティングテーブルエントリ
   * \param 発信者へのルーティングテーブルエントリtoOrigin
   * \param gratRepは、無償のRREPを宛先にユニキャストする必要があるかどうかを示します
   */
  void SendReplyByIntermediateNode (RoutingTableEntry & toDst, RoutingTableEntry & toOrigin, bool gratRep);
  /// Send RREP_ACK
  void SendReplyAck (Ipv4Address neighbor);
  /// Initiate RERR
  void SendRerrWhenBreaksLinkToNextHop (Ipv4Address nextHop);
  /// Forward RERR
  void SendRerrMessage (Ptr<Packet> packet,  std::vector<Ipv4Address> precursors);
  /**
   * Send RERR message when no route to forward input packet. Unicast if there is reverse route to originating node, broadcast otherwise.
   * \param dst - destination node IP address
   * \param dstSeqNo - destination node sequence number
   * \param origin - originating node IP address
   ******
   * 入力パケットを転送するルートがない場合は、RERRメッセージを送信します。 元のノードへの逆ルートがある場合はユニキャスト、それ以外の場合はブロードキャスト
   * \param dst - 宛先ノードのIPアドレス
   * \param dstSeqNo - 宛先ノードのシーケンス番号
   * \param origin - 発信元ノードのIPアドレス
   */
  void SendRerrWhenNoRouteToForward (Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin);

  void SendTo (Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination);

  /// Hello timer
  Timer m_htimer;
  /// Schedule next send of hello message
  void HelloTimerExpire ();
  /// RREQ rate limit timer
  Timer m_rreqRateLimitTimer;
  /// Reset RREQ count and schedule RREQ rate limit timer with delay 1 sec.
  void RreqRateLimitTimerExpire ();
  /// RERR rate limit timer
  Timer m_rerrRateLimitTimer;
  /// Reset RERR count and schedule RERR rate limit timer with delay 1 sec.
  void RerrRateLimitTimerExpire ();
  /// Map IP address + RREQ timer.
  std::map<Ipv4Address, Timer> m_addressReqTimer;
  /// Handle route discovery process
  void RouteRequestTimerExpire (Ipv4Address dst);
  /// Mark link to neighbor node as unidirectional for blacklistTimeout
  void AckTimerExpire (Ipv4Address neighbor,  Time blacklistTimeout);

  /// Provides uniform random variables.
  Ptr<UniformRandomVariable> m_uniformRandomVariable;  
  /// Keep track of the last bcast time
  Time m_lastBcastTime;
};

} // namespace aodv
} // namespace ns3
#endif /* AODVROUTINGPROTOCOL_H */
