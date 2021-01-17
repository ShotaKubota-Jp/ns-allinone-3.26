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
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "aodv-dpd.h"

namespace ns3
{
namespace aodv
{

//-------------------------------------------------------------------
// パケットが重複しているかを確認.重複していなければ、パケット情報が保存される.
bool
DuplicatePacketDetection::IsDuplicate  (Ptr<const Packet> p, const Ipv4Header & header)
{
  // これは何を返している？？
  // 重複を返す？？ はたまた、保存をしているのか？？
  // このヘッダー情報ははRREQ？？それても包括してるのか？
  return m_idCache.IsDuplicate (header.GetSource (), p->GetUid () );
}

//-------------------------------------------------------------------
// 重複するレコードのライフタイム(生存時間)を設定
void
DuplicatePacketDetection::SetLifetime (Time lifetime)
{
  m_idCache.SetLifetime (lifetime);
}

//-------------------------------------------------------------------
// 重複するレコードのライフタイム(生存時間)を取得
Time
DuplicatePacketDetection::GetLifetime () const
{
  return m_idCache.GetLifeTime ();
}

} // namespace aodv
} // namespace ns3

