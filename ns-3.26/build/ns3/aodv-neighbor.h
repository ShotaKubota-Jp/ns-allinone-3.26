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

#ifndef AODVNEIGHBOR_H
#define AODVNEIGHBOR_H

#include "ns3/simulator.h"
#include "ns3/timer.h"
#include "ns3/ipv4-address.h"
#include "ns3/callback.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/arp-cache.h"
#include <vector>

namespace ns3
{
namespace aodv
{
class RoutingProtocol;
/**
 * \ingroup aodv
 * \brief maintain list of active neighbors
 *  アクティブな隣人のリストを維持する
 */
class Neighbors
{
public:
  /// コンストラクタ
  Neighbors (Time delay);
  /// 隣接説明 / Neighbor description
  struct Neighbor{
    Ipv4Address m_neighborAddress;  // IPアドレス
    Mac48Address m_hardwareAddress; // MACアドレス
    Time m_expireTime;              // 有効期間
    bool close;                     // 切れたかどうか

    Neighbor (Ipv4Address ip, Mac48Address mac, Time t) :
      m_neighborAddress (ip), 
      m_hardwareAddress (mac), 
      m_expireTime (t),
      close (false)
    {}
  };
  /// アドレスを持つ隣接ノードの有効期間を返します(存在する場合),そうでない場合は0を返します.
  /// Return expire time for neighbor node with address addr, if exists, else return 0.
  Time GetExpireTime (Ipv4Address addr);
  /// アドレスを持つノードが隣接であることを確認する. / Check that node with address addr is neighbor.
  bool IsNeighbor (Ipv4Address addr);
  /// アドレス付きエントリの有効期限を更新します(存在する場合).そうでない場合は、新しいエントリを追加します.
  /// Update expire time for entry with address addr, if it exists, else add new entry
  void Update (Ipv4Address addr, Time expire);
  /// 期限切れのエントリを削除 / Remove all expired entries
  void Purge ();
  /// Schedule m_ntimer.
  void ScheduleTimer ();
  /// 全てのエントリを削除 / Remove all entries
  void Clear () { m_nb.clear (); }

  /// レイヤ2通知処理を許可するために使用するARPキャッシュを追加する
  /// Add ARP cache to be used to allow layer 2 notifications processing
  void AddArpCache (Ptr<ArpCache>);
  ///与えられたARPキャッシュをそれ以上使用しない（インタフェースがダウンしている）
  /// Don't use given ARP cache any more (interface is down)
  void DelArpCache (Ptr<ArpCache>);
  /// ProcessTxErrorへのコールバックを取得します / Get callback to ProcessTxError
  Callback<void, WifiMacHeader const &> GetTxErrorCallback () const { return m_txErrorCallback; }
 
  /// Handle link failure callback
  void SetCallback (Callback<void, Ipv4Address> cb) { m_handleLinkFailure = cb; }
  /// Handle link failure callback
  Callback<void, Ipv4Address> GetCallback () const { return m_handleLinkFailure; }

private:
  /// link failure callback
  Callback<void, Ipv4Address> m_handleLinkFailure;
  /// TXエラーコールバック / TX error callback
  Callback<void, WifiMacHeader const &> m_txErrorCallback;
  /// 隣接のリストの時間. スケジュール解放(). / Timer for neighbor's list. Schedule Purge().
  Timer m_ntimer;
  /// エントリベクトル / vector of entries
  std::vector<Neighbor> m_nb;
  /// LAYER2の通知処理に使用するためにキャッシュされたARPのリスト
  /// list of ARP cached to be used for layer 2 notifications processing
  std::vector<Ptr<ArpCache> > m_arp;

  /// ARPキャッシュのリストを使用してIPアドレスでMACアドレスを検索する
  /// Find MAC address by IP using list of ARP caches
  Mac48Address LookupMacAddress (Ipv4Address);
  /// プロセスLAYER2 送信エラー通知 / Process layer 2 TX error notification
  void ProcessTxError (WifiMacHeader const &);
};

} // namespcae aodv
} // namespace ns3

#endif /* AODVNEIGHBOR_H */
