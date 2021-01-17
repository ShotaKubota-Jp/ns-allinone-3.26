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
 *              Song Luan <lsuper@mail.ustc.edu.cn> (Implemented Link Cache using Dijsktra algorithm)
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

#include "dsr-rcache.h"
#include <map>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <list>
#include <vector>
#include <functional>
#include <iomanip>

#include "ns3/simulator.h"
#include "ns3/ipv4-route.h"
#include "ns3/socket.h"
#include "ns3/log.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("DsrRouteCache");
  
namespace dsr {

bool CompareRoutesBoth (const DsrRouteCacheEntry &a, const DsrRouteCacheEntry &b)
{
  // compare based on both with hop count considered priority
  return (a.GetVector ().size () < b.GetVector ().size ())|| ((a.GetVector ().size () == b.GetVector ().size ()) && (a.GetExpireTime () > b.GetExpireTime ()));
}

bool CompareRoutesHops (const DsrRouteCacheEntry &a, const DsrRouteCacheEntry &b)
{
  // compare based on hops
  return a.GetVector ().size () < b.GetVector ().size ();
}

bool CompareRoutesExpire (const DsrRouteCacheEntry &a, const DsrRouteCacheEntry &b)
{
  // compare based on expire time
  return a.GetExpireTime () > b.GetExpireTime ();
}

void Link::Print () const
{
  std::cout << m_low << "----" << m_high << std::endl;
}

DsrNodeStab::DsrNodeStab (Time nodeStab)
  : m_nodeStability (nodeStab + Simulator::Now ())
{
}

DsrNodeStab::~DsrNodeStab ()
{
}

DsrLinkStab::DsrLinkStab (Time linkStab)
  : m_linkStability (linkStab + Simulator::Now ())
{
}

DsrLinkStab::~DsrLinkStab ()
{
}

void DsrLinkStab::Print ( ) const
{
  std::cout << "LifeTime: " << (Time)GetLinkStability ().GetSeconds () << std::endl;
}

typedef std::list<DsrRouteCacheEntry>::value_type route_pair;

DsrRouteCacheEntry::DsrRouteCacheEntry (IP_VECTOR const  & ip, Ipv4Address dst, Time exp)
  : m_ackTimer (Timer::CANCEL_ON_DESTROY),
    m_dst (dst),
    m_path (ip),
    m_expire (exp + Simulator::Now ()),
    m_reqCount (0),
    m_blackListState (false),
    m_blackListTimeout (Simulator::Now ())
{
}

DsrRouteCacheEntry::~DsrRouteCacheEntry ()
{
}

void
DsrRouteCacheEntry::Invalidate (Time badLinkLifetime)
{
  m_reqCount = 0;
  m_expire = badLinkLifetime + Simulator::Now ();
}

void
DsrRouteCacheEntry::Print (std::ostream & os) const
{
  os << m_dst << "\t" << (m_expire - Simulator::Now ()).GetSeconds () << "\t";
}

NS_OBJECT_ENSURE_REGISTERED (DsrRouteCache);

TypeId DsrRouteCache::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::dsr::DsrRouteCache")
    .SetParent<Object> ()
    .SetGroupName ("Dsr")
    .AddConstructor<DsrRouteCache> ()
  ;
  return tid;
}

DsrRouteCache::DsrRouteCache ()
  : m_vector (0),
    m_maxEntriesEachDst (3),
    m_isLinkCache (false),
    m_ntimer (Timer::CANCEL_ON_DESTROY),
    m_delay (MilliSeconds (100))
{

  // The timer to set layer 2 notification, not fully supported by ns3 yet
  m_ntimer.SetDelay (m_delay);
  m_ntimer.SetFunction (&DsrRouteCache::PurgeMac, this);
  m_txErrorCallback = MakeCallback (&DsrRouteCache::ProcessTxError, this);
}

DsrRouteCache::~DsrRouteCache ()
{
  NS_LOG_FUNCTION_NOARGS ();
  // clear the route cache when done
  m_sortedRoutes.clear ();
}

void
DsrRouteCache::RemoveLastEntry (std::list<DsrRouteCacheEntry> & rtVector)
{
  NS_LOG_FUNCTION (this);
  // Release the last entry of route list
  rtVector.pop_back ();
}

bool
DsrRouteCache::UpdateRouteEntry (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::const_iterator i =
    m_sortedRoutes.find (dst);
  if (i == m_sortedRoutes.end ()){
    std::cout << "DsrRouteCache::UpdateRouteEntry->Failed to find the route entry for the destination " << dst << std::endl;
    return false;
  } else {
    std::list<DsrRouteCacheEntry> rtVector = i->second;
    DsrRouteCacheEntry successEntry = rtVector.front ();
    successEntry.SetExpireTime (RouteCacheTimeout);
    rtVector.pop_front ();
    rtVector.push_back (successEntry);
    rtVector.sort (CompareRoutesExpire);      // sort the route vector first
    m_sortedRoutes.erase (dst);               // erase the entry first
    
    // Save the new route cache along with the destination address in map
    std::pair<std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator, bool> result = m_sortedRoutes.insert (std::make_pair (dst, rtVector));
    return result.second;
  }
  return false;
}

bool
DsrRouteCache::LookupRoute (Ipv4Address id, DsrRouteCacheEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  //std::cout << "DsrRouteCache::LookupRoute:Ipv4Address->" << id << std::endl;
  if (IsLinkCache ()){
    //std::cout << "DsrRouteCache::LookupRoute->IsLinkCache/LookupRoute_Link" << std::endl;
    return LookupRoute_Link (id, rt);
  } else {
    Purge ();  // Purge first to remove expired entries
    if (m_sortedRoutes.empty ()){
      std::cout << "DsrRouteCache::LookupRoute->Route to " << id << " NOT found. m_sortedRoutes is empty." << std::endl;
      return false;
    }
    std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::const_iterator i = m_sortedRoutes.find (id);
    if (i == m_sortedRoutes.end ()){
      std::cout << "DsrRouteCache::LookupRoute->No Direct Route to " << id << " found" << std::endl;
      for (std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::const_iterator j = m_sortedRoutes.begin (); j != m_sortedRoutes.end (); ++j){
        std::list<DsrRouteCacheEntry> rtVector = j->second; // The route cache vector linked with destination address
        
        // Loop through the possibly multiple routes within the route vector
        for (std::list<DsrRouteCacheEntry>::const_iterator k = rtVector.begin (); k != rtVector.end (); ++k) {
          // return the first route in the route vector
          DsrRouteCacheEntry::IP_VECTOR routeVector = k->GetVector ();
          DsrRouteCacheEntry::IP_VECTOR changeVector;
          for (DsrRouteCacheEntry::IP_VECTOR::iterator l = routeVector.begin (); l != routeVector.end (); ++l){
            if (*l != id) changeVector.push_back (*l);
            else {
              changeVector.push_back (*l);
              break;
            }
          }
          /*
            * When the changed vector is smaller in size and larger than 1, which means we have found a route with the destination
            * address we are looking for
            */
          if ((changeVector.size () < routeVector.size ())  && (changeVector.size () > 1)) {
            DsrRouteCacheEntry changeEntry; // Create the route entry
            changeEntry.SetVector (changeVector);
            changeEntry.SetDestination (id);
            // Use the expire time from original route entry
            changeEntry.SetExpireTime (k->GetExpireTime ());
            // We need to add new route entry here
            std::list<DsrRouteCacheEntry> newVector;
            newVector.push_back (changeEntry);
            newVector.sort (CompareRoutesExpire);  // sort the route vector first
            m_sortedRoutes[id] = newVector;   // Only get the first sub route and add it in route cache
            std::cout << "DsrRouteCache::LookupRoute->We have a sub-route to " << id << " add it in route cache" << std::endl;
          }
        }
      }
    }
    std::cout << "DsrRouteCache::LookupRoute->Here we check the route cache again after updated the sub routes" << std::endl;
    std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::const_iterator m = m_sortedRoutes.find (id);
    if (m == m_sortedRoutes.end ()){
      std::cout << "DsrRouteCache::LookupRoute->No updated route till last time" << std::endl;
      return false;
    }

    // We have a direct route to the destination address
    std::list<DsrRouteCacheEntry> rtVector = m->second;
    rt = rtVector.front ();  // use the first entry in the route vector
    std::cout << "DsrRouteCache::LookupRoute->Route to " << id << " with route size " << rtVector.size () << std::endl;
    return true;
  }
}

void
DsrRouteCache::SetCacheType (std::string type)
{
  NS_LOG_FUNCTION (this << type);
  if (type == std::string ("LinkCache")){
    m_isLinkCache = true;
  }else if (type == std::string ("PathCache")){
    m_isLinkCache = false;
  }else{
    m_isLinkCache = true;             // use link cache as default
    std::cout << "DsrRouteCache::SetCacheType->Error Cache Type." << std::endl;
  }
}

bool
DsrRouteCache::IsLinkCache ()
{
  NS_LOG_FUNCTION (this);
  return m_isLinkCache;
}

void
DsrRouteCache::RebuildBestRouteTable (Ipv4Address source)
{
  NS_LOG_FUNCTION (this << source);
  /**
   * \brief 以下は、単一ソースを初期化している. The followings are initialize-single-source.
   * Current network graph state for this node, double is weight, which is calculated by the node information
   * and link information, any time some changes of link cache and node cache
   * change the weight and then recompute the best choice for each node
   * このノードの現在のネットワークグラフの状態は、リンクキャッシュとノードキャッシュの変更によってウェイトが変更され、
   * 各ノードの最適な選択が再計算されるたびに、ノード情報とリンク情報によって計算される重み.
   */
  std::map<Ipv4Address, uint32_t> d; // @d shortest-path estimate 最短経路推定
  std::map<Ipv4Address, Ipv4Address> pre; // @pre preceeding node 先行ノード
  // 各ノードのルートキャッシュのホップ数がuint32_tが入っている。
  for (std::map<Ipv4Address, std::map<Ipv4Address, uint32_t> >::iterator i = m_netGraph.begin (); i != m_netGraph.end (); ++i){
    if (i->second.find (source) != i->second.end ()){
      d[i->first] = i->second[source]; // 
      pre[i->first] = source; // 推定経路にソースノードを入れる
    } else {
      d[i->first] = MAXWEIGHT; // 無限大の値
      pre[i->first] = Ipv4Address ("255.255.255.255"); // ブロードキャストアドレス
    }
  }
  d[source] = 0;
  /**
   * \briefダイクストラアルゴリズム.  The followings are core of dijskra algorithm.
   * the node set which shortest distance has been calculated, if true calculated
   */
  std::map<Ipv4Address, bool> s;
  double temp = MAXWEIGHT; // 無限値の値
  Ipv4Address tempip = Ipv4Address ("255.255.255.255");
  for (uint32_t i = 0; i < m_netGraph.size (); i++){
    temp = MAXWEIGHT;
    for (std::map<Ipv4Address,uint32_t>::const_iterator j = d.begin (); j != d.end (); ++j){
      Ipv4Address ip = j->first;
      if (s.find (ip) == s.end ()){
        // \brief 以下は比較のためのもの / The followings are for comparison
        if (j->second <= temp){
          temp = j->second;
          tempip = ip;
        }
      }
    }
    if (!tempip.IsBroadcast ()) { // ブロードキャストじゃなければ、処理を進める
      s[tempip] = true;
      for (std::map<Ipv4Address, uint32_t>::const_iterator k = m_netGraph[tempip].begin (); k != m_netGraph[tempip].end (); ++k){
        if (s.find (k->first) == s.end () && d[k->first] > d[tempip] + k->second){
          d[k->first] = d[tempip] + k->second;
          pre[k->first] = tempip;
        }
        /*
          *  Selects the shortest-length route that has the longest expected lifetime (highest minimum timeout of any link in the route)
          *  For the computation overhead and complexity
          *  Here I just implement kind of greedy strategy to select link with the longest expected lifetime when there is two options
          * 期待寿命が最長の最短ルート（ルート内のリンクの最小タイムアウト値）を選択します。
          * 計算のオーバーヘッドと複雑さ。
          * ここでは、2つのオプションがある場合に最も長い期待寿命を持つリンクを選択するための貪欲な戦略を実装しています。
          */
        else if (d[k->first] == d[tempip] + k->second){
          std::map<Link, DsrLinkStab>::iterator oldlink = m_linkCache.find (Link (k->first, pre[k->first]));
          std::map<Link, DsrLinkStab>::iterator newlink = m_linkCache.find (Link (k->first, tempip));
          if (oldlink != m_linkCache.end () && newlink != m_linkCache.end ()){
            if (oldlink->second.GetLinkStability () < newlink->second.GetLinkStability ()){
              std::cout << "DsrRouteCache::RebuildBestRouteTable->ライフタイムが最も長いものを選択する.Select the link with longest expected lifetime." << std::endl;
              d[k->first] = d[tempip] + k->second;
              pre[k->first] = tempip;
            }
          } else {
            std::cout << "DsrRouteCache::RebuildBestRouteTable->リンク安定性情報破損 / Link Stability Info Corrupt." << std::endl;
          }
        }
      }
    }
  }
  m_bestRoutesTable_link.clear (); // clean the best route table
  for (std::map<Ipv4Address, Ipv4Address>::iterator i = pre.begin (); i != pre.end (); ++i){
    // 全ての頂点のループ / loop for all vertexes
    DsrRouteCacheEntry::IP_VECTOR route;
    Ipv4Address iptemp = i->first;

    if (!i->second.IsBroadcast () && iptemp != source){
      while (iptemp != source){
        route.push_back (iptemp);
        iptemp = pre[iptemp];
      }
      route.push_back (source);
      DsrRouteCacheEntry::IP_VECTOR reverseroute; // 逆ルート / Reverse the route
      for (DsrRouteCacheEntry::IP_VECTOR::reverse_iterator j = route.rbegin (); j != route.rend (); ++j)
        reverseroute.push_back (*j);
      //std::cout << "DsrRouteCache::RebuildBestRouteTable->新しく計算された最適ルートを追加 / Add newly calculated best routes." << std::endl;
      PrintVector (reverseroute);
      m_bestRoutesTable_link[i->first] = reverseroute;
    }
  }
}

bool
DsrRouteCache::LookupRoute_Link (Ipv4Address id, DsrRouteCacheEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  PurgeLinkNode (); /// We need to purge the link node cache
  std::map<Ipv4Address, DsrRouteCacheEntry::IP_VECTOR>::const_iterator i = m_bestRoutesTable_link.find (id);
  if (i == m_bestRoutesTable_link.end ()){
    std::cout << "DsrRouteCache::LookupRoute_Link->No route find to " << id << std::endl;
    return false;
  } else {
    if (i->second.size () < 2){
      std::cout << "DsrRouteCache::LookupRoute_Link->Route to " << id << " error." << std::endl;
      return false;
    }

    DsrRouteCacheEntry newEntry; // Create the route entry
    newEntry.SetVector (i->second);
    newEntry.SetDestination (id);
    newEntry.SetExpireTime (RouteCacheTimeout);
    std::cout << "DsrRouteCache::LookupRoute_Link->Route to " << id << " found with the length " << i->second.size () << std::endl;
    rt = newEntry;
    std::vector<Ipv4Address> path = rt.GetVector ();
    PrintVector (path);
    return true;
  }
}

void
DsrRouteCache::PurgeLinkNode ()
{
  NS_LOG_FUNCTION (this);
  //std::cout << "DsrRouteCache::PurgeLinkNode->LinkStability:[";
  for (std::map<Link, DsrLinkStab>::iterator i = m_linkCache.begin (); i != m_linkCache.end (); ){
    //std::cout << i->second.GetLinkStability ().GetSeconds () << ", ";
    std::map<Link, DsrLinkStab>::iterator itmp = i;
    if (i->second.GetLinkStability () <= Seconds (0)){
      ++i;
      m_linkCache.erase (itmp);
    } else {
      ++i;
    }
  } 
  //std::cout << "]" << std::endl;

  /// may need to remove them after verify
  //std::cout << "DsrRouteCache::PurgeLinkNode->NodeStability:[";
  for (std::map<Ipv4Address, DsrNodeStab>::iterator i = m_nodeCache.begin (); i != m_nodeCache.end (); ){
    //std::cout << i->second.GetNodeStability ().GetSeconds () << ", ";
    std::map<Ipv4Address, DsrNodeStab>::iterator itmp = i;
    if (i->second.GetNodeStability () <= Seconds (0)){
      ++i;
      m_nodeCache.erase (itmp);
    } else {
      ++i;
    }
  } 
  //std::cout << "]" << std::endl;
}

void
DsrRouteCache::UpdateNetGraph ()
{
  NS_LOG_FUNCTION (this);
  m_netGraph.clear ();
  for (std::map<Link, DsrLinkStab>::iterator i = m_linkCache.begin (); i != m_linkCache.end (); ++i){
    // Here the weight is set as 1
    /// \todo May need to set different weight for different link here later
    uint32_t weight = 1;
    m_netGraph[i->first.m_low][i->first.m_high] = weight;
    m_netGraph[i->first.m_high][i->first.m_low] = weight;
  }
}

bool
DsrRouteCache::IncStability (Ipv4Address node)
{
  NS_LOG_FUNCTION (this << node);
  std::map<Ipv4Address, DsrNodeStab>::const_iterator i = m_nodeCache.find (node);
  if (i == m_nodeCache.end ()){
    std::cout << "DsrRouteCache::IncStability->The initial stability " << m_initStability.GetSeconds () << std::endl;
    DsrNodeStab ns (m_initStability);
    m_nodeCache[node] = ns;
    return false;
  }
  else{
    /// \todo get rid of the debug here
    std::cout << "DsrRouteCache::IncStability->The node stability " << i->second.GetNodeStability ().GetSeconds () << std::endl;
    std::cout << "DsrRouteCache::IncStability->The stability here " << Time (i->second.GetNodeStability () * m_stabilityIncrFactor).GetSeconds () << std::endl;
    DsrNodeStab ns (Time (i->second.GetNodeStability () * m_stabilityIncrFactor));
    m_nodeCache[node] = ns;
    return true;
  }
  return false;
}

bool
DsrRouteCache::DecStability (Ipv4Address node)
{
  NS_LOG_FUNCTION (this << node);
  std::map<Ipv4Address, DsrNodeStab>::const_iterator i = m_nodeCache.find (node);
  if (i == m_nodeCache.end ()){
    DsrNodeStab ns (m_initStability);
    m_nodeCache[node] = ns;
    return false;
  }else{
    /// \todo remove it here
    std::cout << "DsrRouteCache::DecStability->The stability here " << i->second.GetNodeStability ().GetSeconds () << std::endl;
    std::cout << "DsrRouteCache::DecStability->The stability here " << Time (i->second.GetNodeStability () / m_stabilityDecrFactor).GetSeconds () << std::endl;
    DsrNodeStab ns (Time (i->second.GetNodeStability () / m_stabilityDecrFactor));
    m_nodeCache[node] = ns;
    return true;
  }
  return false;
}

bool
DsrRouteCache::AddRoute_Link (DsrRouteCacheEntry::IP_VECTOR nodelist, Ipv4Address source)
{
  NS_LOG_FUNCTION (this << source);
  //std::cout << "DsrRouteCache::AddRoute_Link->Use Link Cache[Source]:" << source << std::endl;
  PurgeLinkNode (); /// Purge the link node cache first

  for (uint32_t i = 0; i < nodelist.size () - 1; i++){
    DsrNodeStab ns; /// This is the node stability
    ns.SetNodeStability (m_initStability);

    if (m_nodeCache.find (nodelist[i]) == m_nodeCache.end ()) m_nodeCache[nodelist[i]] = ns;
    if (m_nodeCache.find (nodelist[i + 1]) == m_nodeCache.end ()) m_nodeCache[nodelist[i + 1]] = ns;
    Link link (nodelist[i], nodelist[i + 1]); /// Link represent the one link for the route
    DsrLinkStab stab;  /// Link stability
    stab.SetLinkStability (m_initStability);

    /// Set the link stability as the smallest node stability
    if (m_nodeCache[nodelist[i]].GetNodeStability () < m_nodeCache[nodelist[i + 1]].GetNodeStability ()){
      stab.SetLinkStability (m_nodeCache[nodelist[i]].GetNodeStability ());
    } else {
      stab.SetLinkStability (m_nodeCache[nodelist[i + 1]].GetNodeStability ());
    }

    if (stab.GetLinkStability () < m_minLifeTime){
      std::cout << "DsrRouteCache::AddRoute_LinkStability: " << stab.GetLinkStability ().GetSeconds () << std::endl;
      stab.SetLinkStability (m_minLifeTime); /// Set the link stability as the m)minLifeTime, default is 1 second
    }
    
    m_linkCache[link] = stab;
    std::cout << "DsrRouteCache::AddRoute_Link->Add a new link:";
    link.Print ();
    std::cout << "DsrRouteCache::AddRoute_Link->Link Info:";
    stab.Print ();
  }
  UpdateNetGraph ();
  RebuildBestRouteTable (source);
  return true;
}

void
DsrRouteCache::UseExtends (DsrRouteCacheEntry::IP_VECTOR rt)
{
  NS_LOG_FUNCTION (this);
  /// Purge the link node cache first
  PurgeLinkNode ();
  if (rt.size () < 2){
    //std::cout << "DsrRouteCache::UseExtends->The route is too short." << std::endl;
    return;
  }
  for (DsrRouteCacheEntry::IP_VECTOR::iterator i = rt.begin (); i != rt.end () - 1; ++i){
    Link link (*i, *(i + 1));
    if (m_linkCache.find (link) != m_linkCache.end ()){
      if (m_linkCache[link].GetLinkStability () < m_useExtends){
        m_linkCache[link].SetLinkStability (m_useExtends);
        /// \todo remove after debug
        std::cout << "DsrRouteCache::UseExtends->The time of the link " << m_linkCache[link].GetLinkStability ().GetSeconds () << std::endl;
      }
    } else {
      std::cout << "DsrRouteCache::UseExtends->We cannot find a link in cache." << std::endl;
    }
  }
  /// Increase the stability of the node cache
  for (DsrRouteCacheEntry::IP_VECTOR::iterator i = rt.begin (); i != rt.end (); ++i){
    if (m_nodeCache.find (*i) != m_nodeCache.end ()){
      //std::cout << "DsrRouteCache::UseExtends->Increase the stability." << std::endl;
      if (m_nodeCache[*i].GetNodeStability () <= m_initStability){
        IncStability (*i);
      } else {
        //std::cout << "DsrRouteCache::UseExtends->The node stability has already been increased." << std::endl;
      }
    }
  }
}

bool
DsrRouteCache::AddRoute (DsrRouteCacheEntry & rt)
{
  NS_LOG_FUNCTION (this);
  Purge ();
  std::list<DsrRouteCacheEntry> rtVector;   // Declare the route cache entry vector
  Ipv4Address dst = rt.GetDestination ();
  std::vector<Ipv4Address> route = rt.GetVector ();

  std::cout << "DsrRouteCache::AddRoute->The route destination we have " << dst << std::endl;
  std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::const_iterator i =
    m_sortedRoutes.find (dst);

  if (i == m_sortedRoutes.end ()){
    rtVector.push_back (rt);
    m_sortedRoutes.erase (dst);   // Erase the route entries for dst first
    /**
     * Save the new route cache along with the destination address in map
     */
    std::pair<std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator, bool> result =
      m_sortedRoutes.insert (std::make_pair (dst, rtVector));
    return result.second;
  }
  else{
    rtVector = i->second;
    std::cout << "DsrRouteCache::AddRoute->The existing route size " << rtVector.size () << " for destination address " << dst << std::endl;
    /**
     * \brief Drop the most aged packet when buffer reaches to max
     */
    if (rtVector.size () >= m_maxEntriesEachDst){
      RemoveLastEntry (rtVector);         // Drop the last entry for the sorted route cache, the route has already been sorted
    }

    if (FindSameRoute (rt, rtVector)){
      std::cout << "DsrRouteCache::AddRoute->Find same vector, the FindSameRoute function will update the route expire time" << std::endl;
      return true;
    }
    else{
      // Check if the expire time for the new route has expired or not
      if (rt.GetExpireTime () > Time (0)){
        rtVector.push_back (rt);
        // This sort function will sort the route cache entries based on the size of route in each of the
        // route entries
        rtVector.sort (CompareRoutesExpire);
        /*NS_LOG_DEBUG ("The first time" << rtVector.front ().GetExpireTime ().GetSeconds () << " The second time "  << rtVector.back ().GetExpireTime ().GetSeconds ());
        NS_LOG_DEBUG ("The first hop" << rtVector.front ().GetVector ().size () << " The second hop " << rtVector.back ().GetVector ().size ());*/
        std::cout << "DsrRouteCache::AddRoute->The first time" << rtVector.front ().GetExpireTime ().GetSeconds () << 
                    " The second time " << rtVector.back ().GetExpireTime ().GetSeconds () << std::endl;
        std::cout << "DsrRouteCache::AddRoute->->The first hop" << rtVector.front ().GetVector ().size () << 
                    " The second hop " << rtVector.back ().GetVector ().size () << std::endl;

        m_sortedRoutes.erase (dst); // erase the route entries for dst first
        /**
         * Save the new route cache along with the destination address in map
         */
        std::pair<std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator, bool> result =
          m_sortedRoutes.insert (std::make_pair (dst, rtVector));
        return result.second;
      }
      else{
        std::cout << "DsrRouteCache::AddRoute->The newly found route is already expired" << std::endl;
      }
    }
  }
  return false;
}

bool 
DsrRouteCache::FindSameRoute (DsrRouteCacheEntry & rt, std::list<DsrRouteCacheEntry> & rtVector)
{
  NS_LOG_FUNCTION (this);
  for (std::list<DsrRouteCacheEntry>::iterator i = rtVector.begin (); i != rtVector.end (); ++i){
    // return the first route in the route vector
    DsrRouteCacheEntry::IP_VECTOR routeVector = i->GetVector ();
    DsrRouteCacheEntry::IP_VECTOR newVector = rt.GetVector ();

    if (routeVector == newVector){
      /*NS_LOG_DEBUG ("Found same routes in the route cache with the vector size "<< rt.GetDestination () << " " << rtVector.size ());
      NS_LOG_DEBUG ("The new route expire time " << rt.GetExpireTime ().GetSeconds () << " the original expire time " << i->GetExpireTime ().GetSeconds ());*/
      std::cout << "DsrRouteCache::FindSameRoute->Found same routes in the route cache with the vector size "
                    << rt.GetDestination () << " " << rtVector.size () << std::endl;
      std::cout << "DsrRouteCache::FindSameRoute->The new route expire time " << rt.GetExpireTime ().GetSeconds ()
                    << " the original expire time " << i->GetExpireTime ().GetSeconds () << std::endl;
      if (rt.GetExpireTime () > i->GetExpireTime ()){ i->SetExpireTime (rt.GetExpireTime ()); }
      m_sortedRoutes.erase (rt.GetDestination ()); // erase the entry first
      rtVector.sort (CompareRoutesExpire);  // sort the route vector first
      
      // Save the new route cache along with the destination address in map
      std::pair<std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator, bool> result =
        m_sortedRoutes.insert (std::make_pair (rt.GetDestination (), rtVector));
      return result.second;
    }
  }
  return false;
}

bool
DsrRouteCache::DeleteRoute (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  Purge (); // purge the route cache first to remove timeout entries
  if (m_sortedRoutes.erase (dst) != 0){
    std::cout << "DsrRouteCache::DeleteRoute->Route deletion to " << dst << " successful" << std::endl;
    return true;
  }
  std::cout << "DsrRouteCache::DeleteRoute->Route deletion to " << dst << " not successful" << std::endl;
  return false;
}

void
DsrRouteCache::DeleteAllRoutesIncludeLink (Ipv4Address errorSrc, Ipv4Address unreachNode, Ipv4Address node)
{
  NS_LOG_FUNCTION (this << errorSrc << unreachNode << node);
  if (IsLinkCache ()){
    // Purge the link node cache first
    PurgeLinkNode ();
    /*
      * The followings are for cleaning the broken link in link cache
      * We basically remove the link between errorSrc and unreachNode
      */
    Link link1 (errorSrc, unreachNode);
    Link link2 (unreachNode, errorSrc);
    // erase the two kind of links to make sure the link is removed from the link cache
    //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->Erase the route" << std::endl;
    m_linkCache.erase (link1);
    /// \todo get rid of this one
    //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->The link cache size " << m_linkCache.size() << std::endl;
    m_linkCache.erase (link2);
    //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->The link cache size " << m_linkCache.size() << std::endl;

    std::map<Ipv4Address, DsrNodeStab>::iterator i = m_nodeCache.find (errorSrc);
    if (i == m_nodeCache.end ()){
      //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->Update the node stability unsuccessfully" << std::endl;
    } else {
      DecStability (i->first);
    }

    i = m_nodeCache.find (unreachNode);
    if (i == m_nodeCache.end ()){
      //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->Update the node stability unsuccessfully" << std::endl;
    } else {
      DecStability (i->first);
    }

    UpdateNetGraph ();
    RebuildBestRouteTable (node);

  } else {
    // the followings are for cleaning the broken link in pathcache
    Purge ();
    if (m_sortedRoutes.empty ()) return;

    // Loop all the routes saved in the route cache
    for (std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator j = m_sortedRoutes.begin (); j != m_sortedRoutes.end (); ){
      std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator jtmp = j;
      Ipv4Address address = j->first;
      std::list<DsrRouteCacheEntry> rtVector = j->second;

      // Loop all the routes for a single destination
      for (std::list<DsrRouteCacheEntry>::iterator k = rtVector.begin (); k != rtVector.end (); ){
        // return the first route in the route vector
        DsrRouteCacheEntry::IP_VECTOR routeVector = k->GetVector ();
        DsrRouteCacheEntry::IP_VECTOR changeVector;
        
        // Loop the ip addresses within a single route entry
        for (DsrRouteCacheEntry::IP_VECTOR::iterator i = routeVector.begin (); i != routeVector.end (); ++i){
          if (*i != errorSrc){
            changeVector.push_back (*i);
          } else {
            if (*(i + 1) == unreachNode){
              changeVector.push_back (*i);
              break;
            } else {
              changeVector.push_back (*i);
            }
          }
        }

        // Verify if need to remove some affected links
        if (changeVector.size () == routeVector.size ()){
          //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->The route does not contain the broken link." << std::endl;
          ++k;
        }
        else if ((changeVector.size () < routeVector.size ()) && (changeVector.size () > 1)){
          //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->Sub route " << m_subRoute << std::endl;
          if (m_subRoute){
            Time expire = k->GetExpireTime ();
            
            // Remove the route first
            k = rtVector.erase (k);
            DsrRouteCacheEntry changeEntry;
            changeEntry.SetVector (changeVector);
            Ipv4Address destination = changeVector.back ();
            //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->The destination of the newly formed route " << destination << " and the size of the route " << changeVector.size () << std::endl;
            changeEntry.SetDestination (destination);
            changeEntry.SetExpireTime (expire); // Initialize the timeout value to the one it has
            rtVector.push_back (changeEntry);  // Add the route entry to the route list
            //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->We have a sub-route to " << destination << std::endl;
          }
          else{
            // Remove the route
            k = rtVector.erase (k);
          }
        }
        else{
          //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->Cut route unsuccessful and erase the route" << std::endl;
          // Remove the route
          k = rtVector.erase (k);
        }
      }
      ++j;
      if (!IsLinkCache ()){
        m_sortedRoutes.erase (jtmp);
      }
      if (rtVector.size ()){
        // Save the new route cache along with the destination address in map
        rtVector.sort (CompareRoutesExpire);
        m_sortedRoutes[address] = rtVector;
      }
      else{
        //std::cout << "DsrRouteCache::DeleteAllRoutesIncludeLink->There is no route left for that destination " << address << std::endl;
      }
    }
  }
}

void
DsrRouteCache::PrintVector (std::vector<Ipv4Address>& vec)
{
  NS_LOG_FUNCTION (this);
  // Check elements in a route vector, used when one wants to check the IP addresses saved in
  if (!vec.size ()){
    //std::cout << "DsrRouteCache::PrintVector->The vector is empty" << std::endl;
  } else {
    //std::cout << "DsrRouteCache::PrintVector->Print all the elements in a vector" << std::endl;
    /*std::cout << "===== DsrRouteCache->VectorIPAddress:[";
    for (std::vector<Ipv4Address>::const_iterator i = vec.begin (); i != vec.end (); ++i){
      std::cout << *i << ", ";
    } std::cout << "]" << std::endl;*/
  }
}

void
DsrRouteCache::PrintRouteVector (std::list<DsrRouteCacheEntry> route)
{
  NS_LOG_FUNCTION (this);
  for (std::list<DsrRouteCacheEntry>::iterator i = route.begin (); i != route.end (); i++){
    std::vector<Ipv4Address> path = i->GetVector ();
    std::cout << "DsrRouteCache::PrintRouteVector->Route NO." << std::endl;
    PrintVector (path);
  }
}

void
DsrRouteCache::Purge ()
{
  NS_LOG_FUNCTION (this);
  //Trying to purge the route cache
  if (m_sortedRoutes.empty ()){
    //std::cout << "DsrRouteCache::Purge->The route cache is empty" << std::endl;
    return;
  }
  for (std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator i = m_sortedRoutes.begin (); i != m_sortedRoutes.end (); ){
    // Loop of route cache entry with the route size
    std::map<Ipv4Address, std::list<DsrRouteCacheEntry> >::iterator itmp = i;
    
    // The route cache entry vector
    Ipv4Address dst = i->first;
    std::list<DsrRouteCacheEntry> rtVector = i->second;
    //std::cout << "DsrRouteCache::Purge->The route vector size of 1 " << dst << " " << rtVector.size () << std::endl;
    if (rtVector.size ()){
      for (std::list<DsrRouteCacheEntry>::iterator j = rtVector.begin (); j != rtVector.end (); ){
        //std::cout << "DsrRouteCache::Purge->The expire time of every entry with expire time " << j->GetExpireTime () << std::endl;
        // First verify if the route has expired or not
        if (j->GetExpireTime () <= Seconds (0)){
          // When the expire time has passed, erase the certain route
          //std::cout << "DsrRouteCache::Purge->Erase the expired route for " << dst << " with expire time " << j->GetExpireTime () << std::endl;
          j = rtVector.erase (j);
        }else{
          ++j;
        }
      }
      //std::cout << "DsrRouteCache::Purge->The route vector size of 2 " << dst << " " << rtVector.size () << std::endl;
      if (rtVector.size ()){
        ++i;
        m_sortedRoutes.erase (itmp); // erase the entry first
        // Save the new route cache along with the destination address in map
        m_sortedRoutes.insert (std::make_pair (dst, rtVector));
      } else {
        ++i;
        m_sortedRoutes.erase (itmp);
      }
    } else {
      ++i;
      m_sortedRoutes.erase (itmp);
    }
  }
  return;
}

void
DsrRouteCache::Print (std::ostream &os)
{
  NS_LOG_FUNCTION (this);
  Purge ();
  os << "\nDSR Route Cache\n" << "Destination\tGateway\t\tInterface\tFlag\tExpire\tHops\n";
  for (std::list<DsrRouteCacheEntry>::const_iterator i = m_routeEntryVector.begin (); i != m_routeEntryVector.end (); ++i){
    i->Print (os);
  }
  os << "\n";
}

// ----------------------------------------------------------------------------------------------------------
/**
 * This part of code maintains an Acknowledgment id cache for next hop and remove duplicate ids
 */
uint16_t
DsrRouteCache::CheckUniqueAckId (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this);
  std::map<Ipv4Address, uint16_t>::const_iterator i =
    m_ackIdCache.find (nextHop);
  if (i == m_ackIdCache.end ()){
    std::cout << "DsrRouteCache::CheckUniqueAckId->No Ack id for " << nextHop << " found and use id 1 for the first network ack id" << std::endl;
    m_ackIdCache[nextHop] = 1;
    return 1;
  }else{
    uint16_t ackId = m_ackIdCache[nextHop];
    std::cout << "DsrRouteCache::CheckUniqueAckId->Ack id for " << nextHop << " found in the cache has value " << ackId << std::endl;
    ackId++;
    m_ackIdCache[nextHop] = ackId;
    return ackId;
  }
}

uint16_t
DsrRouteCache::GetAckSize ()
{
  return m_ackIdCache.size ();
}

// ----------------------------------------------------------------------------------------------------------
/**
 * This part maintains a neighbor list to handle unidirectional links and link-layer acks
 */
bool
DsrRouteCache::IsNeighbor (Ipv4Address addr)
{
  NS_LOG_FUNCTION (this);
  PurgeMac ();  // purge the mac cache
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i != m_nb.end (); ++i){
    if (i->m_neighborAddress == addr){
      return true;
    }
  }
  return false;
}

Time
DsrRouteCache::GetExpireTime (Ipv4Address addr)
{
  NS_LOG_FUNCTION (this);
  PurgeMac ();
  for (std::vector<Neighbor>::const_iterator i = m_nb.begin (); i != m_nb.end (); ++i){
    if (i->m_neighborAddress == addr){
      return (i->m_expireTime - Simulator::Now ());
    }
  }
  return Seconds (0);
}

void
DsrRouteCache::UpdateNeighbor (std::vector<Ipv4Address> nodeList, Time expire)
{
  NS_LOG_FUNCTION (this);
  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i){
    for (std::vector<Ipv4Address>::iterator j = nodeList.begin (); j != nodeList.end (); ++j){
      if (i->m_neighborAddress == (*j)){
        i->m_expireTime
          = std::max (expire + Simulator::Now (), i->m_expireTime);
        if (i->m_hardwareAddress == Mac48Address ()){
          i->m_hardwareAddress = LookupMacAddress (i->m_neighborAddress);
        }
        return;
      }
    }
  }

  Ipv4Address addr;
  std::cout << "DsrRouteCache::UpdateNeighbor->Open link to " << addr << std::endl;
  Neighbor neighbor (addr, LookupMacAddress (addr), expire + Simulator::Now ());
  m_nb.push_back (neighbor);
  PurgeMac ();
}

void
DsrRouteCache::AddNeighbor (std::vector<Ipv4Address> nodeList, Ipv4Address ownAddress, Time expire)
{
  std::cout << "DsrRouteCache::AddNeighbor->Add neighbor number " << nodeList.size () << std::endl;
  for (std::vector<Ipv4Address>::iterator j = nodeList.begin (); j != nodeList.end ();){
    Ipv4Address addr = *j;
    if (addr == ownAddress){
      j = nodeList.erase (j);
      std::cout << "DsrRouteCache::AddNeighbor->The node list size " << nodeList.size () << std::endl;
    }else{
      ++j;
    }
    Neighbor neighbor (addr, LookupMacAddress (addr), expire + Simulator::Now ());
    m_nb.push_back (neighbor);
    PurgeMac ();
  }
}

struct CloseNeighbor
{
  bool operator() (const DsrRouteCache::Neighbor & nb) const {
    return ((nb.m_expireTime < Simulator::Now ()) || nb.close);
  }
};

void
DsrRouteCache::PurgeMac ()
{
  if (m_nb.empty ()) return;

  CloseNeighbor pred;
  if (!m_handleLinkFailure.IsNull ()){
    for (std::vector<Neighbor>::iterator j = m_nb.begin (); j != m_nb.end (); ++j){
      if (pred (*j)){
        std::cout << "DsrRouteCache::PurgeMac->Close link to " << j->m_neighborAddress << std::endl;
        /// \todo disable temporarily
//              m_handleLinkFailure (j->m_neighborAddress);
      }
    }
  }
  m_nb.erase (std::remove_if (m_nb.begin (), m_nb.end (), pred), m_nb.end ());
  m_ntimer.Cancel ();
  m_ntimer.Schedule ();
}

void
DsrRouteCache::ScheduleTimer ()
{
  m_ntimer.Cancel ();
  m_ntimer.Schedule ();
}

void
DsrRouteCache::AddArpCache (Ptr<ArpCache> a)
{
  m_arp.push_back (a);
}

void
DsrRouteCache::DelArpCache (Ptr<ArpCache> a)
{
  m_arp.erase (std::remove (m_arp.begin (), m_arp.end (), a), m_arp.end ());
}

Mac48Address
DsrRouteCache::LookupMacAddress (Ipv4Address addr)
{
  Mac48Address hwaddr;
  for (std::vector<Ptr<ArpCache> >::const_iterator i = m_arp.begin (); i != m_arp.end (); ++i){
    ArpCache::Entry * entry = (*i)->Lookup (addr);
    if (entry != 0 && (entry->IsAlive () || entry->IsPermanent ()) && !entry->IsExpired ()){
      hwaddr = Mac48Address::ConvertFrom (entry->GetMacAddress ());
      break;
    }
  }
  return hwaddr;
}

void
DsrRouteCache::ProcessTxError (WifiMacHeader const & hdr)
{
  Mac48Address addr = hdr.GetAddr1 ();

  for (std::vector<Neighbor>::iterator i = m_nb.begin (); i != m_nb.end (); ++i){
    if (i->m_hardwareAddress == addr){
      i->close = true;
    }
  }
  PurgeMac ();
}
} // namespace dsr
} // namespace ns3
