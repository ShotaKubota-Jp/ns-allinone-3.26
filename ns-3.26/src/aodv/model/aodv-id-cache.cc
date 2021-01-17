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
#include "aodv-id-cache.h"
#include <algorithm>

//  m_idCache; / Already seen IDs
// m_lifetime; Default lifetime for ID records

namespace ns3
{
namespace aodv
{

//-------------------------------------------------------
// エントリ（addr、id）がキャッシュに存在することを確認します
// エントリが存在しない場合は追加します
bool
IdCache::IsDuplicate (Ipv4Address addr, uint32_t id)
{
  Purge ();
  // キャッシュを確認
  for (std::vector<UniqueId>::const_iterator i = m_idCache.begin (); i != m_idCache.end (); ++i){
    if (i->m_context == addr && i->m_id == id) return true; // キューを確認し、エントリーあればフラグを返す
  }
  struct UniqueId uniqueId = { addr, id, m_lifetime + Simulator::Now () }; // ユニークIDを作成
  m_idCache.push_back (uniqueId); // キューの末尾に追加(既にみたユニークID)
  return false;
}

//-------------------------------------------------------
// 期限切れのエントリをすべて削除する
void
IdCache::Purge ()
{
  m_idCache.erase (remove_if (m_idCache.begin (), m_idCache.end (), IsExpired ()), m_idCache.end ());
}

//-------------------------------------------------------
// キャッシュ内のエントリの数を返します
uint32_t
IdCache::GetSize ()
{
  Purge ();
  return m_idCache.size ();
}

//-------------------------------------------------------
// ヘッダファイル内で、ライフタイム(生存時間)の存続時間の取得と有効時間の設定を行っている
//-------------------------------------------------------

} // namespace aodv
} // namespace ns3
