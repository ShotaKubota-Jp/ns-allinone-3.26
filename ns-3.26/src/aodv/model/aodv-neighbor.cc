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

#include "aodv-neighbor.h"
#include "ns3/log.h"
#include <algorithm>


namespace ns3
{
  
NS_LOG_COMPONENT_DEFINE ("AodvNeighbors");

namespace aodv
{

//-------------------------------------------------------
// コンストラクタ
Neighbors::Neighbors (Time delay) : 
  m_ntimer (Timer::CANCEL_ON_DESTROY)
{
  // コンストラクタの引数には、有効期間の箇所に遅延時間が入る？？？
  m_ntimer.SetDelay (delay); // 遅延？？？
  m_ntimer.SetFunction (&Neighbors::Purge, this); // 有効期限切れのエントリを削除するかどうか
  m_txErrorCallback = MakeCallback (&Neighbors::ProcessTxError, this); // コールバックで、送信エラーを
}

//-------------------------------------------------------
// アドレスを持つノードが隣接であることを確認する
bool
Neighbors::IsNeighbor (Ipv4Address addr)
{
  Purge ();// 解放するのか？？？
  // キューを回して、アドレスが存在するかどうか
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin ();
       i != m_nb.end (); ++i){
    if (i->m_neighborAddress == addr)
      return true; // 存在を確認
  }
  return false;
}

//-------------------------------------------------------
// アドレスを保有する隣接ノードの有効期間を取得
Time
Neighbors::GetExpireTime (Ipv4Address addr)
{
  Purge ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i
       != m_nb.end (); ++i){
    if (i->m_neighborAddress == addr)
      return (i->m_expireTime - Simulator::Now ()); // 有効期間を返す
  }
  return Seconds (0); // なければ、一律0を返す
}

//-------------------------------------------------------
// 期限切れのエントリを削除
void
Neighbors::Update (Ipv4Address addr, Time expire)
{
  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i)
    if (i->m_neighborAddress == addr){
      i->m_expireTime = std::max (expire + Simulator::Now (), i->m_expireTime);
      if (i->m_hardwareAddress == Mac48Address ())
        i->m_hardwareAddress = LookupMacAddress (i->m_neighborAddress);
      return;
    }

  NS_LOG_LOGIC ("Open link to " << addr);
  Neighbor neighbor (addr, LookupMacAddress (addr), expire + Simulator::Now ()); // 隣接ノードのデータ
  m_nb.push_back (neighbor); // キューの末尾に追加
  Purge (); // 期限切れのエントリーを解放
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//////// 期限が切れたエントリー？？？？をこの中に格納
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
struct CloseNeighbor
{
  bool operator() (const Neighbors::Neighbor & nb) const{
    return ((nb.m_expireTime < Simulator::Now ()) || nb.close);
  }
};

//-------------------------------------------------------
// 期限切れのエントリを削除
void
Neighbors::Purge ()
{
  if (m_nb.empty ()) return; // エントリーがなければ、何もしない
  CloseNeighbor pred; // 期限切れのエントリ
  if (!m_handleLinkFailure.IsNull ()){
    for (std::vector<Neighbor>::iterator j = m_nb.begin (); j != m_nb.end (); ++j){
      if (pred (*j)){
        NS_LOG_LOGIC ("Close link to " << j->m_neighborAddress);
        m_handleLinkFailure (j->m_neighborAddress);
      }
    }
  }
  m_nb.erase (std::remove_if (m_nb.begin (), m_nb.end (), pred), m_nb.end ());
  m_ntimer.Cancel (); // 隣接リストのキャンセル？？？
  m_ntimer.Schedule (); // 隣接リストのスケジュールリング？？？
}

//-------------------------------------------------------
void
Neighbors::ScheduleTimer ()
{
  m_ntimer.Cancel ();
  m_ntimer.Schedule ();
}

//-------------------------------------------------------
void
Neighbors::AddArpCache (Ptr<ArpCache> a)
{
  // ARPキャッシュリストの末尾に追加
  m_arp.push_back (a); 
}

//-------------------------------------------------------
void
Neighbors::DelArpCache (Ptr<ArpCache> a)
{
  // ARPキャッシュリストを全部削除？？？？？
  m_arp.erase (std::remove (m_arp.begin (), m_arp.end (), a), m_arp.end ());
}

//-------------------------------------------------------
// ARPキャッシュのリストを使用してIPアドレスでMACアドレスを検索する
Mac48Address
Neighbors::LookupMacAddress (Ipv4Address addr)
{
  Mac48Address hwaddr; // MACアドレス
  // ARPキャッシュリスト
  for (std::vector<Ptr<ArpCache> >::const_iterator i = m_arp.begin (); i != m_arp.end (); ++i){
    ArpCache::Entry * entry = (*i)->Lookup (addr);
    if (entry != 0 && (entry->IsAlive () || entry->IsPermanent ()) && !entry->IsExpired ()){
      hwaddr = Mac48Address::ConvertFrom (entry->GetMacAddress ());
      break;
    }
  }
  return hwaddr;
}

//-------------------------------------------------------
// プロセスLAYER2 送信エラー通知
void
Neighbors::ProcessTxError (WifiMacHeader const & hdr)
{
  Mac48Address addr = hdr.GetAddr1 (); // MACアドレスを取得
  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i){
    if (i->m_hardwareAddress == addr){
      i->close = true; // エントリーが閉じている？？？？そのため、送信がエラーを起こす？？？
    }
  }
  Purge ();
}

} // namespace aodv
} // namespace ns3
