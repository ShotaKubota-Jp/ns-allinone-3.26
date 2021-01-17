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

#include "aodv-rtable.h"
#include <algorithm>
#include <iomanip>
#include "ns3/simulator.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("AodvRoutingTable");

namespace aodv
{

//-------------------------------------------
// The Routing Table Entry
//-------------------------------------------
RoutingTableEntry::RoutingTableEntry (
  Ptr<NetDevice> dev,          // 不明(ネットデバイス)
  Ipv4Address dst,             // 宛先IPアドレス
  bool vSeqNo,                 // シーケンス番号の有効性
  uint32_t seqNo,              // シーケンス番号
  Ipv4InterfaceAddress iface,  // 不明
  uint16_t hops,               // ホップ数
  Ipv4Address nextHop,         // 次ホップのIPアドレス(ソース)  
  Time lifetime                // 生存時間
) :
  m_ackTimer (Timer::CANCEL_ON_DESTROY),
  m_validSeqNo (vSeqNo), 
  m_seqNo (seqNo), 
  m_hops (hops),
  m_lifeTime (lifetime + Simulator::Now ()), 
  m_iface (iface), 
  m_flag (VALID),
  m_reqCount (0), 
  m_blackListState (false), 
  m_blackListTimeout (Simulator::Now ()) 
{
  m_ipv4Route = Create<Ipv4Route> ();
  m_ipv4Route->SetDestination (dst);
  m_ipv4Route->SetGateway (nextHop);
  m_ipv4Route->SetSource (m_iface.GetLocal ());
  m_ipv4Route->SetOutputDevice (dev);
  std::cout << "[エントリ作成!]送信元:" << m_ipv4Route->GetSource() 
            << ", ゲートウェイ(次ホップ):" << m_ipv4Route->GetGateway()
            << ", 宛先:" << m_ipv4Route->GetDestination() << std::endl;
}

//-------------------------------------------
// デストラクタ
//-------------------------------------------
RoutingTableEntry::~RoutingTableEntry ()
{
}

//-------------------------------------------
// Precursorsリストにまだ存在しない場合は、Precursorsリストに挿入する
//-------------------------------------------
bool
RoutingTableEntry::InsertPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  std::cout << "RoutingTableEntry::InsertPrecursor" << std::endl;
  if (!LookupPrecursor (id)){
    m_precursorList.push_back (id); // 末尾に追加
    return true; // 追加することができた
  }
  else return false; // できなかった
}

//-----------------------------------------------------------------------------------
// アドレスによって、Precursorsを探索する
bool
RoutingTableEntry::LookupPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  std::cout << "RoutingTableEntry::LookupPrecursor" << std::endl;
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin (); i != m_precursorList.end (); ++i){
    if (*i == id){
      std::cout << "Precursor " << id << " found" << std::endl;
      //NS_LOG_LOGIC ("Precursor " << id << " found");
      return true;
    }
  }
  std::cout << "Precursor " << id << " not found" << std::endl;
  //NS_LOG_LOGIC ("Precursor " << id << " not found");
  return false;
}

//-----------------------------------------------------------------------------------
// Precursorsを削除する
bool
RoutingTableEntry::DeletePrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  std::cout << "RoutingTableEntry::DeletePrecursor" << std::endl;
  std::vector<Ipv4Address>::iterator i = std::remove (m_precursorList.begin (), m_precursorList.end (), id);
  if (i == m_precursorList.end ()){
    std::cout << "Precursor " << id << " not found" << std::endl;
    NS_LOG_LOGIC ("Precursor " << id << " not found");
    return false;
  } else {
    std::cout << "Precursor " << id << " found" << std::endl;
    NS_LOG_LOGIC ("Precursor " << id << " found");
    m_precursorList.erase (i, m_precursorList.end ());
  }
  return true;
}

//-----------------------------------------------------------------------------------
// Precursorsを全て削除する
void
RoutingTableEntry::DeleteAllPrecursors ()
{
  NS_LOG_FUNCTION (this);
  m_precursorList.clear ();
}

//-----------------------------------------------------------------------------------
// Precursorsリストが空であることを確認する
bool
RoutingTableEntry::IsPrecursorListEmpty () const
{
  return m_precursorList.empty ();
}

//-----------------------------------------------------------------------------------
// ベクターにprecursorsをまだ挿入していない場合は、ベクターprecursorsに挿入
void
RoutingTableEntry::GetPrecursors (std::vector<Ipv4Address> & prec) const
{
  NS_LOG_FUNCTION (this);
  if (IsPrecursorListEmpty ()) return; // 空
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin (); i != m_precursorList.end (); ++i){
    bool result = true;
    for (std::vector<Ipv4Address>::const_iterator j = prec.begin (); j != prec.end (); ++j){
      if (*j == *i) result = false; // ベクターに欲しているものあり.その場合、追加はしない.
    }
    if (result) prec.push_back (*i); // 末尾に追加
  }
}

//-----------------------------------------------------------------------------------
// エントリを無効にする
void
RoutingTableEntry::Invalidate (Time badLinkLifetime)
{
  NS_LOG_FUNCTION (this << badLinkLifetime.GetSeconds ());
  if (m_flag == INVALID) return; // 既にエントリが無効である
  m_flag = INVALID;
  m_reqCount = 0;
  m_lifeTime = badLinkLifetime + Simulator::Now ();
}

//-----------------------------------------------------------------------------------
// 出力
void
RoutingTableEntry::Print (Ptr<OutputStreamWrapper> stream) const
{
  std::ostream* os = stream->GetStream ();
  *os << m_ipv4Route->GetDestination () << "\t" << m_ipv4Route->GetGateway ()
      << "\t" << m_iface.GetLocal () << "\t";
  switch (m_flag){
    case VALID:{
      *os << "UP";
      break;
    }
    case INVALID:{
      *os << "DOWN";
      break;
    }
    case IN_SEARCH:{
      *os << "IN_SEARCH";
      break;
    }
  }
  *os << "\t";
  *os << std::setiosflags (std::ios::fixed) << 
  std::setiosflags (std::ios::left) << std::setprecision (2) <<
  std::setw (14) << (m_lifeTime - Simulator::Now ()).GetSeconds ();
  *os << "\t" << m_hops << "\n";
}

//-----------------------------------------------------------------------------------
// 出力
void
RoutingTableEntry::Print () const
{
  std::cout <<
  "IPV4Route:[Source:" << m_ipv4Route->GetSource() <<
  ",Destination:" << m_ipv4Route->GetDestination() << 
  ",NextHop:" << m_ipv4Route->GetGateway()<<
  "]" << std::endl;
  std::cout << "Entry:[" << 
  "DestinationAddress:" << GetDestination () <<
  ",Hops:" <<  GetHop() << 
  ",NextHop:" << GetNextHop() << 
  ",LifeTime:" <<  GetLifeTime() <<
  "]" << std::endl;
}

//*****************************************************************************
// The Routing Table
//*****************************************************************************
RoutingTable::RoutingTable (Time t) : m_badLinkLifetime (t)
{
}

//*********************************************************
// ルーティングテーブルエントリを宛先アドレスで検索します.
// 多分、エントリとIPアドレスを使用して、検索する.
//*********************************************************
bool
RoutingTable::LookupRoute (Ipv4Address id, RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  Purge (); // 有効期限が切れている場合、すべての古いエントリを削除し、有効なエントリを無効にします
  if (m_ipv4AddressEntry.empty ()){ // エントリが空
    std::cout << "RoutingTable::LookupRoute->(このIPアドレスのノードの経路表にはない)Route to " << id << " not found; エントリが空." << std::endl;
    //NS_LOG_LOGIC ("Route to " << id << " not found; m_ipv4AddressEntry is empty");
    return false; // エラー
  }
  // IPアドレスをキーにして、エントリをという値を探索
  std::map<Ipv4Address, RoutingTableEntry>::const_iterator i = m_ipv4AddressEntry.find (id);
  if (i == m_ipv4AddressEntry.end ()){ // エントリの中に一致するIPアドレスが見つからない
    std::cout << "RoutingTable::LookupRoute->(このIPアドレスのノードの経路表にはない)Route to " << id << " not found->経路表では経路不明!!!" << std::endl;
    //NS_LOG_LOGIC ("Route to " << id << " not found");
    return false; // エラー
  }
  rt = i->second; // エントリに、発見したエントリを入れ込む
  std::cout << "RoutingTable::LookupRoute->(このIPアドレスのノードの経路表にある)Route to " << id << " found->経路表において経路発見!!!" << std::endl;
  std::cout << "RoutingTable::LookupRoute->ID:" << id << ", エントリの送信元:" << rt.GetRoute()->GetSource() << ", エントリの宛先:" << rt.GetRoute()->GetDestination() << std::endl;
  //NS_LOG_LOGIC ("Route to " << id << " found");
  return true; // 経路発見
}

/********************************************************
 * VALID状態(有効状態)の経路を探索する
 * aodv-routing-protocol.ccからなる
 ********************************************************/
bool
RoutingTable::LookupValidRoute (Ipv4Address id, RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  if (!LookupRoute (id, rt)){ // 経路探索の関数 IDとエントリ
    std::cout << "RoutingTable::LookupValidRoute(有効な経路を探索した結果)>Route to " << id << " not found->経路が発見できない" << std::endl;
    return false;
  }
  std::cout << "RoutingTable::LookupValidRoute(有効な経路を探索した結果)>Route to " << id << " flag is " << ((rt.GetFlag () == VALID) ? "valid->有効" : "not valid->無効") << std::endl;
  //NS_LOG_LOGIC ("Route to " << id << " flag is " << ((rt.GetFlag () == VALID) ? "valid" : "not valid"));
  return (rt.GetFlag () == VALID); // routing-rtable.hのenumに定義されている(有効、無効、探索中のフラグがある)
}

//-------------------------------------------
// 宛先アドレスを持つルーティングテーブルエントリが存在する場合は削除します.
//-------------------------------------------
bool
RoutingTable::DeleteRoute (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  std::cout << "RoutingTable::DeleteRoute" << std::endl;
  Purge (); // 有効期限が切れている場合、全ての古いエントリを削除し、有効なエントリを無効にします
  if (m_ipv4AddressEntry.erase (dst) != 0){
    std::cout << "Route deletion to " << dst << " successful" << std::endl;
    //NS_LOG_LOGIC ("Route deletion to " << dst << " successful");
    return true;
  }
  std::cout << "Route deletion to " << dst << " not successful" << std::endl;
  //NS_LOG_LOGIC ("Route deletion to " << dst << " not successful");
  return false;
}

//-------------------------------------------
//  ルーティングテーブルエントリがまだルーティングテーブルに存在しない場合は追加する.
//-------------------------------------------
bool
RoutingTable::AddRoute (RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this);
  //std::cout<< "RoutingTable::AddRoute->Source:" << rt.GetRoute ()->GetSource() << ", Destination:" << rt.GetRoute ()->GetDestination() << std::endl;
  std::cout<< "RoutingTable::AddRoute->Source:" << rt.GetRoute ()->GetDestination() << ", Destination:" << rt.GetRoute ()->GetSource() << std::endl;
  Purge ();
  if (rt.GetFlag () != IN_SEARCH) rt.SetRreqCnt (0);
  std::pair<std::map<Ipv4Address, RoutingTableEntry>::iterator, bool> result = m_ipv4AddressEntry.insert (std::make_pair (rt.GetDestination (), rt));
  return result.second;
}

//-------------------------------------------
// ルーティングテーブルを更新する
//-------------------------------------------
bool
RoutingTable::Update (RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingTable::Update->";
  std::map<Ipv4Address, RoutingTableEntry>::iterator i = m_ipv4AddressEntry.find (rt.GetDestination ());
  if (i == m_ipv4AddressEntry.end ()){
    std::cout << "Route update to " << rt.GetDestination () << " fails; not found" << std::endl;
    //NS_LOG_LOGIC ("Route update to " << rt.GetDestination () << " fails; not found");
    return false;
  }
  i->second = rt;
  if (i->second.GetFlag () != IN_SEARCH){
    std::cout << "Route update to " << rt.GetDestination () << " Set RREQ count to 0" << std::endl;
    //NS_LOG_LOGIC ("Route update to " << rt.GetDestination () << " set RreqCnt to 0");
    i->second.SetRreqCnt (0);
  }
  return true;
}

//-------------------------------------------
// ルーティングテーブルのエントリフラグを設定する
//-------------------------------------------
bool
RoutingTable::SetEntryState (Ipv4Address id, RouteFlags state)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingTable::SetEntryState" << std::endl;
  std::map<Ipv4Address, RoutingTableEntry>::iterator i = m_ipv4AddressEntry.find (id);
  if (i == m_ipv4AddressEntry.end ()){
    std::cout << "Route set entry state to " << id << " fails; not found" << std::endl;
    //NS_LOG_LOGIC ("Route set entry state to " << id << " fails; not found");
    return false;
  }
  i->second.SetFlag (state);
  i->second.SetRreqCnt (0);
  std::cout << "Route set entry state to " << id << ": new state is " << state << std::endl;
  //NS_LOG_LOGIC ("Route set entry state to " << id << ": new state is " << state);
  return true;
}

//-------------------------------------------
// 次のホップアドレスを持つルーティングエントリを検索し、空でないプリサータのリスト
//-------------------------------------------
void
RoutingTable::GetListOfDestinationWithNextHop (Ipv4Address nextHop, std::map<Ipv4Address, uint32_t> & unreachable )
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingTable::GetListOfDestinationWithNextHop" <<std::endl;
  Purge ();
  unreachable.clear ();
  for (std::map<Ipv4Address, RoutingTableEntry>::const_iterator i = m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); ++i){
    if (i->second.GetNextHop () == nextHop){
      std::cout << "Unreachable insert " << i->first << " " << i->second.GetSeqNo () << std::endl;
      NS_LOG_LOGIC ("Unreachable insert " << i->first << " " << i->second.GetSeqNo ());
      unreachable.insert (std::make_pair (i->first, i->second.GetSeqNo ()));
    }
  }
}

//-------------------------------------------
// この宛先のルーティングエントリを次のように更新します.
//-------------------------------------------
void
RoutingTable::InvalidateRoutesWithDst (const std::map<Ipv4Address, uint32_t> & unreachable)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingTable::InvalidateRoutesWithDst" << std::endl;
  Purge ();
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i = m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); ++i){
    for (std::map<Ipv4Address, uint32_t>::const_iterator j = unreachable.begin (); j != unreachable.end (); ++j){
      if ((i->first == j->first) && (i->second.GetFlag () == VALID)){
        std::cout << "Invalidate route with destination address " << i->first << std::endl;
        NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
        i->second.Invalidate (m_badLinkLifetime);
      }
    }
  }
}

//-------------------------------------------
// アドレスifaceを持つインターフェイスからすべてのルートを削除します
//-------------------------------------------
void
RoutingTable::DeleteAllRoutesFromInterface (Ipv4InterfaceAddress iface)
{
  NS_LOG_FUNCTION (this);
  std::cout << "RoutingTable::DeleteAllRoutesFromInterface" << std::endl;
  if (m_ipv4AddressEntry.empty ())
    return;
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end ();){
    if (i->second.GetInterface () == iface){
      std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
      ++i;
      m_ipv4AddressEntry.erase (tmp);
    }
    else ++i;
  }
}

//-------------------------------------------
// 有効期限が切れている場合、すべての古いエントリを削除し、有効なエントリを無効にします
//-------------------------------------------
void
RoutingTable::Purge ()
{
  NS_LOG_FUNCTION (this);
  //std::cout << "RoutingTable::Purge->有効期限が切れている場合、古いエントリを削除し、有効なエントリを無効にする" << std::endl;
  if (m_ipv4AddressEntry.empty ()) return;
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i = m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end ();){
    if (i->second.GetLifeTime () < Seconds (0)){
      if (i->second.GetFlag () == INVALID){
        std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
        ++i;
        m_ipv4AddressEntry.erase (tmp);
      } else if (i->second.GetFlag () == VALID){
        NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
        i->second.Invalidate (m_badLinkLifetime);
        ++i;
      }
      else { ++i; }
    }
    else { ++i; }
  }
}

//-------------------------------------------
// Printメソッドで使用するためのPurgeのconstバージョン.
//-------------------------------------------
void
RoutingTable::Purge (std::map<Ipv4Address, RoutingTableEntry> &table) const
{
  NS_LOG_FUNCTION (this);
  if (table.empty ()) return;
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         table.begin (); i != table.end ();){
    if (i->second.GetLifeTime () < Seconds (0)){
      if (i->second.GetFlag () == INVALID){
        std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
        ++i;
        table.erase (tmp);
      }
      else if (i->second.GetFlag () == VALID){
        NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
        i->second.Invalidate (m_badLinkLifetime);
        ++i;
      }
      else { ++i; }
    }
    else { ++i; }
  }
}

//-------------------------------------------
// エントリを単方向としてマークします（例：blacklistTimeoutの期間、このブラックリストをブラックリストに追加します）.
//-------------------------------------------
bool
RoutingTable::MarkLinkAsUnidirectional (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this << neighbor << blacklistTimeout.GetSeconds ());
  std::map<Ipv4Address, RoutingTableEntry>::iterator i =
    m_ipv4AddressEntry.find (neighbor);
  if (i == m_ipv4AddressEntry.end ()){
    std::cout << "Mark link unidirectional to  " << neighbor << " fails; not found" << std::endl;
    NS_LOG_LOGIC ("Mark link unidirectional to  " << neighbor << " fails; not found");
    return false;
  }
  i->second.SetUnidirectional (true);
  i->second.SetBalcklistTimeout (blacklistTimeout);
  i->second.SetRreqCnt (0);
  NS_LOG_LOGIC ("Set link to " << neighbor << " to unidirectional");
  return true;
}

//-------------------------------------------
// 出力
//-------------------------------------------
void
RoutingTable::Print (Ptr<OutputStreamWrapper> stream) const
{
  std::map<Ipv4Address, RoutingTableEntry> table = m_ipv4AddressEntry;
  Purge (table);
  *stream->GetStream () << "\nAODV Routing table\n"
                        << "Destination\tGateway\t\tInterface\tFlag\tExpire\t\tHops\n";
  for (std::map<Ipv4Address, RoutingTableEntry>::const_iterator i =
         table.begin (); i != table.end (); ++i){
    i->second.Print (stream);
  }
  *stream->GetStream () << "\n";
}

} // namespace aodv
} // namespace ns3
