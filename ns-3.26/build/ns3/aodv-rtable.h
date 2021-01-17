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
 
#ifndef AODV_RTABLE_H
#define AODV_RTABLE_H

#include <stdint.h>
#include <cassert>
#include <map>
#include <sys/types.h>
#include "ns3/ipv4.h"
#include "ns3/ipv4-route.h"
#include "ns3/timer.h"
#include "ns3/net-device.h"
#include "ns3/output-stream-wrapper.h"

namespace ns3 {
namespace aodv {

/**
 * \ingroup aodv
 * \brief Route record states
 */
enum RouteFlags
{
  VALID = 0,          //!< VALID 有効
  INVALID = 1,        //!< INVALID 無効
  IN_SEARCH = 2,      //!< IN_SEARCH 探索
};

/**
 * \ingroup aodv
 * \brief Routing table entry
 */
class RoutingTableEntry
{
public:
  /// コンストラクタ
  RoutingTableEntry (
    Ptr<NetDevice> dev = 0,
    Ipv4Address dst = Ipv4Address (),      // 宛先IPアドレス
    bool vSeqNo = false,                   // シーケンス番号の有効性
    uint32_t m_seqNo = 0,                  // シーケンス番号
    Ipv4InterfaceAddress iface = Ipv4InterfaceAddress (), // 次ホップのインタフェース
    uint16_t  hops = 0,                    // ホップ数
    Ipv4Address nextHop = Ipv4Address (),  // 次ホップのIPアドレス
    Time lifetime = Simulator::Now ()      // 生存時間
  );

  /// デストラクタ
  ~RoutingTableEntry ();

  /** \name Precursors management
   * Precursorsリストとは
   * Precursorsは、経路表エントリに含まれている。
   * Precursorsは、自ノードの主編ノードのIPアドレスが入っている
   * IPアドレスは、ベクター型で入っている。
   * */
  /**
   * Insert precursor in precursor list if it doesn't yet exist in the list
   * Precursorsリストにまだ存在しない場合は、Precursorsリストに挿入する
   * \param id precursor address
   * \return true on success
   */
  bool InsertPrecursor (Ipv4Address id);
  /**
   * Lookup precursor by address
   * アドレスによって、Precursorsを探索する
   * \param id precursor address
   * \return true on success
   */
  bool LookupPrecursor (Ipv4Address id);
  /**
   * \brief Delete precursor
   * Precursorsを削除する
   * \param id precursor address
   * \return true on success
   */
  bool DeletePrecursor (Ipv4Address id);
  /**
   * Delete all precursors
   * Precursorsを全て削除する
   */
  void DeleteAllPrecursors ();
  /**
   * Check that precursor list empty
   * Precursorsリストが空であることを確認する
   * \return true if precursor list empty
   */
  bool IsPrecursorListEmpty () const;
  /**
   * Inserts precursors in vector prec if they does not yet exist in vector.
   * ベクターにprecursorsをまだ挿入していない場合は、ベクターprecursorsに挿入します.
   */
  void GetPrecursors (std::vector<Ipv4Address> & prec) const;

  /// エントリを無効にする / Mark entry as "down" (i.e. disable it)
  void Invalidate (Time badLinkLifetime);
  
  // Fields
  Ipv4Address GetDestination () const { return m_ipv4Route->GetDestination (); }
  Ptr<Ipv4Route> GetRoute () const { return m_ipv4Route; }
  void SetRoute (Ptr<Ipv4Route> r) { m_ipv4Route = r; }
  void SetNextHop (Ipv4Address nextHop) { m_ipv4Route->SetGateway (nextHop); }
  Ipv4Address GetNextHop () const { return m_ipv4Route->GetGateway (); }
  void SetOutputDevice (Ptr<NetDevice> dev) { m_ipv4Route->SetOutputDevice (dev); }
  Ptr<NetDevice> GetOutputDevice () const { return m_ipv4Route->GetOutputDevice (); }
  Ipv4InterfaceAddress GetInterface () const { return m_iface; }
  void SetInterface (Ipv4InterfaceAddress iface) { m_iface = iface; }
  void SetValidSeqNo (bool s) { m_validSeqNo = s; }
  bool GetValidSeqNo () const { return m_validSeqNo; }
  void SetSeqNo (uint32_t sn) { m_seqNo = sn; }
  uint32_t GetSeqNo () const { return m_seqNo; }
  void SetHop (uint16_t hop) { m_hops = hop; }
  uint16_t GetHop () const { return m_hops; }
  void SetLifeTime (Time lt) { m_lifeTime = lt + Simulator::Now (); }
  Time GetLifeTime () const { return m_lifeTime - Simulator::Now (); }
  void SetFlag (RouteFlags flag) { m_flag = flag; }
  RouteFlags GetFlag () const { return m_flag; }
  void SetRreqCnt (uint8_t n) { m_reqCount = n; }
  uint8_t GetRreqCnt () const { return m_reqCount; }
  void IncrementRreqCnt () { m_reqCount++; }
  void SetUnidirectional (bool u) { m_blackListState = u; }
  bool IsUnidirectional () const { return m_blackListState; }
  void SetBalcklistTimeout (Time t) { m_blackListTimeout = t; }
  Time GetBlacklistTimeout () const { return m_blackListTimeout; }

  /// RREP_ACK timer
  Timer m_ackTimer;

  // \brief Compare destination address
  // \return true if equal
  bool operator== (Ipv4Address const  dst) const {
    return (m_ipv4Route->GetDestination () == dst);
  }
  void Print (Ptr<OutputStreamWrapper> stream) const;
  void Print () const;

private:
  /// Valid Destination Sequence Number flag
  bool m_validSeqNo;
  /// Destination Sequence Number, if m_validSeqNo = true
  uint32_t m_seqNo;
  /// Hop Count (number of hops needed to reach destination)
  uint16_t m_hops;
  /**
  * \brief Expiration or deletion time of the route
  *	Lifetime field in the routing table plays dual role --
  *	for an active route it is the expiration time, and for an invalid route
  *	it is the deletion time.
  */
  Time m_lifeTime;
  /** Ip route, include
  *   - destination address
  *   - source address
  *   - next hop address (gateway)
  *   - output device
  */
  Ptr<Ipv4Route> m_ipv4Route;
  /// Output interface address
  Ipv4InterfaceAddress m_iface;
  /// Routing flags: valid, invalid or in search
  RouteFlags m_flag;

  /// List of precursors
  std::vector<Ipv4Address> m_precursorList;
  /// When I can send another request
  Time m_routeRequestTimout;
  /// Number of route requests
  uint8_t m_reqCount;
  /// Indicate if this entry is in "blacklist"
  bool m_blackListState;
  /// Time for which the node is put into the blacklist
  Time m_blackListTimeout;
};

/**
 * \ingroup aodv
 * \brief The Routing table used by AODV protocol
 */
class RoutingTable
{
public:
  /// コンストラクタ
  RoutingTable (Time t);
  /// name Handle life time of invalid route
  Time GetBadLinkLifetime () const { return m_badLinkLifetime; }
  void SetBadLinkLifetime (Time t) { m_badLinkLifetime = t; }

  /**
   * Add routing table entry if it doesn't yet exist in routing table
   * ルーティングテーブルエントリがまだルーティングテーブルに存在しない場合は追加する
   * \param r routing table entry
   * \return true in success
   */
  bool AddRoute (RoutingTableEntry & r);
  /**
   * Delete routing table entry with destination address dst, if it exists.
   * 宛先アドレスを持つルーティングテーブルエントリが存在する場合は削除します.
   * 　↑よくわからん。とりあえず、期限が切れたエントリを削除
   * \param dst destination address
   * \return true on success
   */
  bool DeleteRoute (Ipv4Address dst);
  /**
   * Lookup routing table entry with destination address dst.
   * ルーティングテーブルエントリを宛先アドレスで検索します.
   * 経路の探索
   * \param dst destination address
   * \param rt entry with destination address dst, if exists
   * \return true on success
   */
  bool LookupRoute (Ipv4Address dst, RoutingTableEntry & rt);
  /// 有効な状態の経路を探索する / Lookup route in VALID state
  bool LookupValidRoute (Ipv4Address dst, RoutingTableEntry & rt);
  /// ルーティングテーブルを更新する / Update routing table
  bool Update (RoutingTableEntry & rt);
  /// ルーティングテーブルのエントリフラグを設定する / Set routing table entry flags
  bool SetEntryState (Ipv4Address dst, RouteFlags state);
  /// 次のホップアドレスを持つルーティングエントリを検索し、空でないプリサータのリスト
  /// Lookup routing entries with next hop Address dst and not empty list of precursors.
  void GetListOfDestinationWithNextHop (Ipv4Address nextHop, std::map<Ipv4Address, uint32_t> & unreachable);
  /**
   *  Update routing entries with this destinations as follows:
   *  1. The destination sequence number of this routing entry, if it exists and is valid, is incremented.
   *  2. The entry is invalidated by marking the route entry as invalid
   *  3. The Lifetime field is updated to current time plus DELETE_PERIOD.
   * この宛先のルーティングエントリを次のように更新します.
   * 1.このルーティングエントリの宛先シーケンス番号が存在し、有効であれば、それが増分されます。
   * 2.ルートエントリーを無効とマークすることによりエントリーが無効になります
   * 3.ライフタイムフィールドは、現在の時刻 + DELETE_PERIODに更新されます。
   */
  void InvalidateRoutesWithDst (std::map<Ipv4Address, uint32_t> const & unreachable);
  /// アドレスifaceを持つインターフェイスからすべてのルートを削除します
  /// Delete all route from interface with address iface
  void DeleteAllRoutesFromInterface (Ipv4InterfaceAddress iface);
  /// ルーティングテーブルからすべてのエントリを削除する / Delete all entries from routing table
  void Clear () { m_ipv4AddressEntry.clear (); }
  /// 有効期限が切れている場合、すべての古いエントリを削除し、有効なエントリを無効にします
  /// Delete all outdated entries and invalidate valid entry if Lifetime is expired
  void Purge ();
  /** 
   * Mark entry as unidirectional (e.g. add this neighbor to "blacklist" for blacklistTimeout period)
   * エントリを単方向としてマークします（例：blacklistTimeoutの期間、このブラックリストをブラックリストに追加します）.
   * \param neighbor - neighbor address link to which assumed to be unidirectional
   * \param blacklistTimeout - time for which the neighboring node is put into the blacklist
   * \return true on success
   */
  bool MarkLinkAsUnidirectional (Ipv4Address neighbor, Time blacklistTimeout);
  /// Print routing table
  void Print (Ptr<OutputStreamWrapper> stream) const;

private:
  // IPアドレスと「ルーティングテーブルエントリー」を関連づける
  // 関連づける理由として、エントリーには次ホップのIPアドレスや宛先IPアドレスを保有はしているものの、
  // どのノードの情報のデータは保有していない。どのデータのエントリーかを分からせるため、IPアドレスに関連づける。
  std::map<Ipv4Address, RoutingTableEntry> m_ipv4AddressEntry;
  /// Deletion time for invalid routes.
  Time m_badLinkLifetime;
  /// const version of Purge, for use by Print() method.
  void Purge (std::map<Ipv4Address, RoutingTableEntry> &table) const;
};

} // namespave aodv
} // namespace ns3

#endif /* AODV_RTABLE_H */
