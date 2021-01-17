/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006 Georgia Tech Research Corporation
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
 * Author: George F. Riley<riley@ece.gatech.edu>
 */

// Implementation for ns3 Application base class.
// George F. Riley, Georgia Tech, Fall 2006

#include "application.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Application");

NS_OBJECT_ENSURE_REGISTERED (Application);

// static 変数
std::map<Ipv4Address, std::vector<std::vector<Ipv4Address>>> Application::g_myRouteInfomation;
std::map<Ipv4Address, int> Application::g_myNodeLoad;
std::map<Ipv4Address, std::vector<int> > Application::g_myNodePosition;
int Application::g_myNodeNum;
std::ofstream Application::myOfsSLoad("data/data-app-sta-load.csv");
std::ofstream Application::myOfsSRoute("data/data-app-sta-route.csv");
std::ofstream Application::myOfsLoad("data/data-app-load.csv");
std::ofstream Application::myOfsSHop("data/data-app-sta-hop.csv");
//std::ofstream Application::myOfsDynamicLoad("data/data-schedule-load.csv");

// Application Methods
TypeId 
Application::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Application")
    .SetParent<Object> ()
    .SetGroupName("Network")
    .AddAttribute ("StartTime", "Time at which the application will start",
                   TimeValue (Seconds (0.0)),
                   MakeTimeAccessor (&Application::m_startTime),
                   MakeTimeChecker ())
    .AddAttribute ("StopTime", "Time at which the application will stop",
                   TimeValue (TimeStep (0)),
                   MakeTimeAccessor (&Application::m_stopTime),
                   MakeTimeChecker ());
  return tid;
}

//-------------------------------------------------------------------------------------------------
// \brief Application Constructor
Application::Application()
{
  NS_LOG_FUNCTION (this);
  std::cout << "Application::Application()" << std::endl;
  std::cout << "StartTime:" << m_startTime << "StopTime:" << m_stopTime << ", sStartEvent:" << m_startEvent.GetUid() << ", EndEvent:" << m_stopEvent.GetUid() << std::endl;
}

//-------------------------------------------------------------------------------------------------
// \brief Application Destructor
Application::~Application()
{
  NS_LOG_FUNCTION (this);
}

/* void 
Application::NetworkLoadFluctuation (
  Time myInterval
  )
{
  Application::MySchedule (myInterval);
}

void 
Application::MySchedule (
  Time myInterval
  )
{
  myInterval = Seconds (myInterval.GetSeconds () + 1);
  Simulator::Schedule (myInterval, &Application::MySchedule, myInterval);
}

void 
Application::NetworkLoadFluctuation () 
{
  int m_aveLoad = -1; // 平均負荷
  int m_maxLoad = -1; // 最大負荷
  int m_minLoad = 9999; // 最小負荷
  int m_totalLoad = 0; // 合計負荷
  int m_count = 0;
  
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); ++it) {
    if( m_maxLoad < it->second ) m_maxLoad = it->second;
    if( m_minLoad > it->second ) m_minLoad = it->second;
    m_totalLoad += it->second;
    m_count++;
  }
  m_aveLoad = m_totalLoad / m_count;
  //std::cout << "[Statistics_on_Node]->Max:" << m_maxLoad << ", Min:" << m_minLoad << ", Ave:" << m_aveLoad << std::endl;
  myOfsDynamicLoad << "Max," << m_maxLoad << ",Min," << m_minLoad << ",Ave," << m_aveLoad << std::endl;
}*/

//-------------------------------------------------------------------------------------------------
void
Application::ReceiveData (
  std::vector<Ipv4Address> nodeList
  )
{
  NS_LOG_FUNCTION (this);
  //std::cout << "Application::ReceiveData->Source:" << nodeList.front() << ", Destination:" << nodeList.back() << std::endl;
  //Ipv4Address m_mySrc = nodeList.front();
  //Ipv4Address m_myDst = nodeList.back();
  //bool m_myFlag = false;
  int m_myCnt = 0;

  std::cout << "[NODELIST]:";
  for(int i=0; i<(int)nodeList.size(); i++) std::cout << nodeList[i] << ", ";
  std::cout << std::endl;

  // 経路情報を追加する
  for(std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    if( it->first == nodeList.front() ){
      std::vector<std::vector<Ipv4Address> > m_vv1 = it->second;
      for( int j=0; j<(int)m_vv1.size(); j++ ){
        for( int z=0; z<(int)m_vv1[j].size(); z++ ) {
          //m_myFlag = m_vv1[j].size() == nodeList.size() && std::equal (m_vv1[j].cbegin(), m_vv1[j].cend(), nodeList.cbegin());
          m_myCnt++; // サイズを確認する
        }
      }
      if( m_myCnt == 0 ) { m_vv1.clear(); }
      //if( m_myFlag == false ){ // 経路情報に追加されていなければ、経路を追加する
        m_vv1.push_back(nodeList);
        g_myRouteInfomation.erase (nodeList.front());
        g_myRouteInfomation.insert (std::make_pair (nodeList.front(), m_vv1));
        std::cout << "Application::ReceiveData->経路情報を追加!!!!!" << std::endl;
      //}
      break;
    }
  }
  
  //PrintMyNodePosition (); // ノードの位置情報
  DijkstraMethod (); // ダイクストラ法に移行して、低コストの経路情報に変更する
  //DijkstraMethod (); // ダイクストラ法に移行して、低コストの経路情報に変更する
  //DijkstraMethod (); // ダイクストラ法に移行して、低コストの経路情報に変更する
  //DijkstraMethod (); // ダイクストラ法に移行して、低コストの経路情報に変更する
  //DijkstraMethod (); // ダイクストラ法に移行して、低コストの経路情報に変更する
  //PrintMyRouteInfo (); // 経路情報出力
  OutputMyNodeLoad ();
  GetStatisticalDataonRouteLoad ();
  GetStatisticalDataonNodeLoad ();
  GetStatisticalDataonHop ();
}

//-------------------------------------------------------------------------------------------------
void 
Application::DijkstraMethod ()
{
  uint8_t m_mySrc;
  uint8_t m_myDst;
  int m_mySource; // 送信元ノード(ノード番号)
  int m_myDestination; // 宛先ノード(ノード番号)
  const int m_nodeNum = 91; // ノード数
  //int m_myNodeParagraph = 9; // 区切り数
  int m_myCost[m_nodeNum][m_nodeNum] = { }; // 接続行列(本来はノード数あるとよい)
  int m_myMultipleArray[m_nodeNum][3] = { }; // 負荷,X座標,Y座標
  int m_range = 130; // 電波の到達範囲
  int i = 0;
  int j = 0;
  int m_z = 99999; // 無限大の値
  int m_min = 0;
  int m_minId = -1;
  int m_peakLoadRouteRow = -1; // 最大負荷値を持つ経路の行
  int m_peakLoadRouteColumn = -1; // 最大負荷値を持つ経路の列
  
  Ipv4Address m_myIpv4; // 変換用のIPアドレス
  std::vector<Ipv4Address> m_objectRoute; // 切り替える対象の経路
  std::vector<int> m_bestRoute; // 最適経路(ノード番号)
  std::vector<Ipv4Address> m_bestRouteIP; // 最適経路(IP) 

  i = 0;
  // ノードの位置情報
  for( std::map<Ipv4Address, std::vector<int> >::const_iterator it = g_myNodePosition.begin (); it != g_myNodePosition.end (); ++it ){
    std::vector<int> m_pos = it->second;
    m_myMultipleArray[i][1] = m_pos[0]; // X座標
    m_myMultipleArray[i][2] = m_pos[1]; // Y座標
    //std::cout << m_myMultipleArray[i][1] << ", " << m_myMultipleArray[i][2] << std::endl;
    i++;
  }

  //************************************************************************************************
  //RouteDetectionBasedonTotalCost (&m_objectRoute, &m_peakLoadRouteRow, &m_peakLoadRouteColumn);
  RouteDetectionBasedonAverageCost (&m_objectRoute, &m_peakLoadRouteRow, &m_peakLoadRouteColumn);
  MyDecreaseNodeLoad (m_objectRoute); // 対象経路を減分
  m_mySrc = MyConvertFromIPto8 (m_objectRoute.front()); 
  m_myDst = MyConvertFromIPto8 (m_objectRoute.back());
  m_mySource = (int)m_mySrc - 1; // ノード番号
  m_myDestination = (int)m_myDst - 1; // ノード番号
  m_myIpv4 = m_objectRoute.front(); // 変換するために必要なIP
  std::cout << "DijkstraAlgorithm->Source:" << unsigned(m_mySrc) << ", NodeNumber->" << m_mySource << ", Destination:" << unsigned(m_myDst) << ", NodeNumber->" << m_myDestination << std::endl; 
  //************************************************************************************************

  // ノードの負荷情報(ついでに、最大の負荷を抽出)
  i=0;
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); it++ ){
    m_myMultipleArray[i][0] = it->second;
    /*if( m_peakLoadValue < it->second ){ // 大きければ
      m_peakLoadValue = it->second; // 最大の負荷値
      m_peakLoadNumber = i; // ノード番号
    }*/
    i++;
  }

  // 隣接行列への重み付け
	for( i=0; i<m_nodeNum; i++ ){
		for( j=0; j<m_nodeNum; j++ ){
      m_myCost[i][j] = m_z; // 無限大
      if(i == j) continue;
			if( (m_myMultipleArray[i][1]-m_myMultipleArray[j][1]) * (m_myMultipleArray[i][1]-m_myMultipleArray[j][1]) +
          (m_myMultipleArray[i][2]-m_myMultipleArray[j][2]) * (m_myMultipleArray[i][2]-m_myMultipleArray[j][2]) < m_range * m_range){
        //m_myCost[i][j] = 1; // 移動するときのコストは必ず1になるので、隣接行列は0にしてはいけない.
        m_myCost[i][j] = 100; // 1では意味がないので、ベースラインを100くらいに増加してみる
        m_myCost[i][j] += m_myMultipleArray[j][0];
			}
		}
	}

  // 接続行列の出力
  /*for( i=0; i<m_nodeNum; i++ ){
    std::cout << "Link of Node[" << i << "]." << std::endl;
		for( j=0; j<m_nodeNum; j++ ){
      std::cout << "[" << std::setw(5) << m_myCost[i][j] << "]";
			if( (j%m_myNodeParagraph)+1 == m_myNodeParagraph )
        std::cout << std::endl;
		}
  }*/

  // 探索集合の宣言と初期化 
  int m_P[m_nodeNum]; // 探索集合
  for( i=0; i<m_nodeNum; i++ ) m_P[i] = 0;
  m_P[m_mySource] = 1;

  // ラベル配列の作成と初期化
  int m_L[m_nodeNum][2]; // ラベル　L[id][0]:経路長、 L[i][1]:経路の直前ノード

  // ラベル初期化
  for( i=0; i<m_nodeNum; i++ ){
    m_L[i][0] = m_z;
    m_L[i][1] = m_mySource;
  }

  m_L[m_mySource][0] = 0; // 出発ノードの経路長は0に

  // 経路探索プロセス
  while(true){
    // P集合所属ノードから出る枝のノードのラベル更新
    for( i=0; i<m_nodeNum; i++ ){
      if( m_P[i] != 0 ){
        for( j=0; j<m_nodeNum; j++ ){
          if( m_L[i][0] + m_myCost[i][j] < m_L[j][0] ){
            m_L[j][0] = m_L[i][0] + m_myCost[i][j];
            m_L[j][1] = i;
          }
        }
      }
    }

    // P集合未所属のノードのラベルで最少値のノードをPに入れる
    m_min = 9999;
    for( i=0; i<m_nodeNum; i++ ){
      if( m_P[i] != 1 ){
        if(m_min > m_L[i][0]){
          m_min = m_L[i][0];
          m_minId = i;
        }
      }
    }
    m_P[m_minId] = 1; // P集合に所属させる

    // 目的ノードがPに所属したら探索終了
    if( m_P[m_myDestination] == 1 ) break;
  }
  
  // ルートのノードリスト
  int m_R[m_nodeNum]; // 逆ルート
  int m_RR[m_nodeNum]; // 正ルート

  // 逆ルートの初期化
  for(i=0; i<m_nodeNum; i++) m_R[i] = -1;
    
  i = 0;
  m_R[0] = m_myDestination;
  while( i < m_nodeNum ){
    // ラベルから逆ルート抽出
    m_R[i+1] = m_L[m_R[i]][1];
    if( m_R[i+1] == m_mySource ) break;
    i++;
  }

  // 正ルートの初期化
  for( i=0; i<m_nodeNum; i++ ) m_RR[i] = -1;

  j = 0;
  // 逆ルートから正ルートへのコピー
  for( i=m_nodeNum-1; i>=0; i-- ){
    if( m_R[i] != -1 ){
      m_RR[j] = m_R[i];
      j++;
    }	
  }

  // 準最適経路のvectorに入れ直す
  for( i=0; i<m_nodeNum; i++ ){
    if( m_RR[i] == -1) break;
    m_bestRoute.push_back( m_RR[i] );
  }

  /*std::cout << "The best route(NodeNumber):";
  for(int i=0; i<m_nodeNum; i++) {
    if( m_RR[i] == -1) break;
    std::cout << m_RR[i] << "-";
  } std::cout << std::endl;

  std::cout << "The best route(IPAddress):";
  for(int i=0; i<m_nodeNum; i++) {
    if( m_RR[i] == -1) break;
    std::cout << m_RR[i]+1 << "-";
  } std::cout << std::endl;*/

  m_bestRouteIP = MyConvertFrom8toIP (m_bestRoute, m_myIpv4);
  std::cout << "対象経路->"; MyPrintVectorIP (m_objectRoute); // 変換する前の経路
  std::cout << "ダイクストラ経路->"; MyPrintVectorIP (m_bestRouteIP); // 変換した後の経路

  // ダイクストラ経路を増分
  MyIncreaseNodeLoad (m_bestRouteIP);
  // 対象の経路をダイクストラ経路に移行
  SwitchTheRoute (m_objectRoute, m_bestRouteIP, m_peakLoadRouteRow, m_peakLoadRouteColumn);

}

//-------------------------------------------------------------------------------------------------
void
Application::RouteDetectionBasedonAverageCost (
  std::vector<Ipv4Address>* ipv4Route, // 最大負荷を持つ、切り替えるべき対象の経路を渡す
  int* peakLoadRouteRow, // 行
  int* peakLoadRouteColumn // 列
)
{
  std::cout << "Application::RouteDetectionBasedonAverageCost" << std::endl;
  int m_myHop = 0; // ホップ数
  int m_myLoad = 0; // 負荷値
  double m_1Hop = 0.0;
  //std::vector<std::vector<int> > m_vVecLoad; // 負荷値の二次元ベクター
  //std::vector<int> m_vecLoad; // 負荷値のベクター
  //std::vector<std::vector<int> > m_vVecHop; // ホップ数の二次元ベクター
  //std::vector<int> m_vecHop; // ホップ数のベクター
  std::vector<std::vector<double> > m_vVec1Hop; // 1ホップの負荷値の二次元ベクター
  std::vector<double> m_vec1Hop; // 1ホップの負荷値
  int i = 0;
  int m_rows = -1; // 行
  int m_columns = -1; // 列
  double m_peakLoadValue = -1; // 最大の負荷値
  std::vector<Ipv4Address> m_ipv4Route; // 経路
  
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    std::vector<std::vector<Ipv4Address> > m_vv2 = it->second; // 二次元の配列になっている
    for( int j=0; j<(int)m_vv2.size(); j++ ){
      for( int z=0; z<(int)m_vv2[j].size(); z++ ){
        for (std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin (); it != g_myNodeLoad.end (); ++it){
          if( m_vv2[j][z] == it->first ){
            m_myHop++; // ホップ数
            m_myLoad += it->second + 1; // 負荷値
            //std::cout << it->second + 1 << ", ";
            break;
          }
        } 
      }
      m_1Hop = (double)m_myLoad / m_myHop;
      if( std::isnan(m_1Hop) ) m_1Hop = 0.0;
      m_vec1Hop.push_back (m_1Hop); // 追加
      //m_vecLoad.push_back (m_myLoad);
      //m_vecHop.push_back (m_myHop);
      m_1Hop = 0.0;
      m_myLoad = 0;
      m_myHop = 0;
    }
    m_vVec1Hop.push_back (m_vec1Hop);
    m_vec1Hop.clear ();
    /*m_vVecLoad.push_back (m_vecLoad);
    m_vecLoad.clear ();
    m_vVecHop.push_back (m_vecHop);
    m_vecHop.clear ();*/
  }

  //std::cout << "[各経路の平均負荷値]" << std::endl; 
  //MyPrintVectorVectorDouble (m_vVec1Hop);

  // 最大の負荷を持つ経路の検出を行う
  for(int j=0;j<(int)m_vVec1Hop.size();j++){
    for(int z=0; z<(int)m_vVec1Hop[j].size(); z++){
      if( m_peakLoadValue < m_vVec1Hop[j][z] ){
        m_rows = j; // 行
        m_columns = z; // 列
        m_peakLoadValue = m_vVec1Hop[j][z]; // ルーティングにかかる、負荷値が最も大きい経路を検出
      }
    }
  }
  //std::cout << "(" << m_rows << ", " << m_columns << ")" << ":Peak load->" << m_peakLoadValue << std::endl;
  i = 0;
  // 最大の負荷を持つ経路の検出に入る
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    if( i == m_rows ){
      std::vector<std::vector<Ipv4Address> > m_vv = it->second;
      m_ipv4Route = m_vv[m_columns];
      break;
    }
    i++;
  }
  
  *peakLoadRouteRow = m_rows; // 行
  *peakLoadRouteColumn = m_columns; // 列
  *ipv4Route = m_ipv4Route; // 切り替える対象の経路を返す
}

//-------------------------------------------------------------------------------------------------
void
Application::RouteDetectionBasedonTotalCost (
  std::vector<Ipv4Address>* ipv4Route, // 最大負荷を持つ、切り替えるべき対象の経路を渡す
  int* peakLoadRouteRow, // 行
  int* peakLoadRouteColumn // 列
)
{
  std::cout << "Application::RouteDetectionBasedonTotalCost" << std::endl;
  int m_myLoad = 0; // 負荷値
  std::vector<std::vector<int> > m_vVecLoad; // 負荷値の二次元ベクター
  std::vector<int> m_vecLoad; // 負荷値のベクター
  int i = 0;
  int m_rows = -1; // 行
  int m_columns = -1; // 列
  int m_peakLoadValue = -1; // 最大の負荷値
  std::vector<Ipv4Address> m_ipv4Route; // 経路
  
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    std::vector<std::vector<Ipv4Address> > m_vv2 = it->second; // 二次元の配列になっている
    for( int j=0; j<(int)m_vv2.size(); j++ ){
      for( int z=0; z<(int)m_vv2[j].size(); z++ ) {
        for (std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin (); it != g_myNodeLoad.end (); ++it){
          if( m_vv2[j][z] == it->first ){
            m_myLoad += it->second + 1; // 負荷値
            //std::cout << it->second + 1 << ", ";
            break;
          }
        } 
      }
      m_vecLoad.push_back (m_myLoad);
      m_myLoad = 0;
    }
    m_vVecLoad.push_back (m_vecLoad);
    m_vecLoad.clear ();
  }

  //std::cout << "[各経路における合計の負荷値]" << std::endl; 
  //MyPrintVectorVector (m_vVecLoad);
  //std::cout << "[各経路におけるホップ数]" << std::endl; 
  //MyPrintVectorVector (m_vVecHop);

  // 最大の負荷を持つ経路の検出を行う
  for(int j=0;j<(int)m_vVecLoad.size();j++){
    for(int z=0; z<(int)m_vVecLoad[j].size(); z++){
      if( m_peakLoadValue < m_vVecLoad[j][z] ){
        m_rows = j; // 行
        m_columns = z; // 列
        m_peakLoadValue = m_vVecLoad[j][z]; // ルーティングにかかる、負荷値が最も大きい経路を検出
      }
    } 
  } 

  //std::cout << "(" << m_rows << ", " << m_columns << ")" << ":Peek load->" << m_peakLoadValue << std::endl;
  i = 0;
  // 最大の負荷を持つ経路の検出に入る
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    if( i == m_rows ){
      std::vector<std::vector<Ipv4Address> > m_vv = it->second;
      m_ipv4Route = m_vv[m_columns];
      break;
    }
    i++;
  }

  *peakLoadRouteRow = m_rows; // 行
  *peakLoadRouteColumn = m_columns; // 列
  *ipv4Route = m_ipv4Route; // 切り替える対象の経路を返す
} 

//-------------------------------------------------------------------------------------------------
void 
Application::SwitchTheRoute (
  std::vector<Ipv4Address> objectRoute,  // 切り替えるべき対象の経路
  std::vector<Ipv4Address> bestRouteIP,  // ダイクストラアルゴリズムで算出した経路
  int peakLoadRouteRow, // 行
  int peakLoadRouteColumn // 列
  )
{
  int i = 0;
  std::vector<std::vector<Ipv4Address> > m_myVVec;
  Ipv4Address m_myIpv4;

  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    m_myIpv4 = it->first;
    //MyPrintVectorIP (m_vv[peakLoadRouteColumn]);
    if( i == peakLoadRouteRow ){
      std::vector<std::vector<Ipv4Address> > m_myVVecFor = it->second;
      for( int j=0; j<(int)m_myVVecFor.size(); j++ ){
        if( j != peakLoadRouteColumn ) {
          m_myVVec.push_back (m_myVVecFor[j]);
        }
      }
      m_myVVec.push_back (bestRouteIP);
      g_myRouteInfomation.erase (m_myIpv4);
      g_myRouteInfomation.insert (std::make_pair (m_myIpv4, m_myVVec));
      break;
    }
    i++;
  }
}

//-------------------------------------------------------------------------------------------------
void
Application::GetStatisticalDataonHop ()
{
  int m_myHop = 0; // ホップ数
  int m_totalHop = 0;
  int m_count = 0;
  double m_averageHop = -1;
  int m_peakHop = -1;
  int m_abyssHop = 9999;
  std::vector<std::vector<int> > m_vVecHop; // ホップ数の二次元ベクター
  std::vector<int> m_vecHop; // ホップ数のベクター
  
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    std::vector<std::vector<Ipv4Address> > m_vv2 = it->second; // 二次元の配列になっている
    for( int j=0; j<(int)m_vv2.size(); j++ ){
      for( int z=0; z<(int)m_vv2[j].size(); z++ ){
        for (std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin (); it != g_myNodeLoad.end (); ++it){
          if( m_vv2[j][z] == it->first ){
            m_myHop++; // ホップ数
            break;
          }
        } 
      }
      m_vecHop.push_back (m_myHop);
      m_myHop = 0;
    }
    m_vVecHop.push_back (m_vecHop);
    m_vecHop.clear ();
  }

  //std::cout << "HOPSSSSSS" << std::endl;
  //MyPrintVectorVector (m_vVecHop);

  //for(int j=0;j<(int)m_vVecHop.size();j++){
  for(int j=0;j<10;j++){
    for(int z=0; z<(int)m_vVecHop[j].size(); z++){
      if( m_vVecHop[j][z] != 0 ){
        m_totalHop += m_vVecHop[j][z];
        if( m_peakHop < m_vVecHop[j][z] ) m_peakHop = m_vVecHop[j][z];
        if( m_abyssHop > m_vVecHop[j][z] ) m_abyssHop = m_vVecHop[j][z];
        m_count++;
      }
    } 
  } 

  m_averageHop = (double)m_totalHop / m_count;
  //std::cout << "Hop->Average:" << m_averageHop << ", Max:" << m_peakHop << ", Min:" << m_abyssHop << std::endl; 
  myOfsSHop << "Max," << m_peakHop << ",Min," << m_abyssHop << ",Ave," << m_averageHop << std::endl;

}

//-------------------------------------------------------------------------------------------------
void
Application::GetStatisticalDataonRouteLoad ()
{
  int m_myLoad = 0; // 負荷値
  int m_peakLoad = -1; // 最大負荷値
  int m_abyssLoad = 9999; // 最小負荷値
  int m_totalLoad = 0; // 合計負荷値
  int m_aveLoad = -1; // 平均負荷値
  int m_count = 0;
  std::vector<std::vector<int> > m_vVecLoad; // 負荷値の二次元ベクター
  std::vector<int> m_vecLoad; // 負荷値のベクター
  
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address> > >::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    std::vector<std::vector<Ipv4Address> > m_vv2 = it->second; // 二次元の配列になっている
    for( int j=0; j<(int)m_vv2.size(); j++ ){
      for( int z=0; z<(int)m_vv2[j].size(); z++ ) {
        for (std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin (); it != g_myNodeLoad.end (); ++it){
          if( m_vv2[j][z] == it->first ){
            m_myLoad += it->second + 1; // 負荷値
            break;
          }
        } 
      }
      m_vecLoad.push_back (m_myLoad);
      m_myLoad = 0;
    }
    m_vVecLoad.push_back (m_vecLoad);
    m_vecLoad.clear ();
  }

  // 最大の負荷を持つ経路の検出を行う
  //for(int j=0;j<(int)m_vVecLoad.size();j++){
  for(int j=0;j<10;j++){
    for(int z=0; z<(int)m_vVecLoad[j].size(); z++){
      m_totalLoad += m_vVecLoad[j][z];
      if( m_peakLoad < m_vVecLoad[j][z] ) m_peakLoad = m_vVecLoad[j][z];
      if( m_abyssLoad > m_vVecLoad[j][z] ) m_abyssLoad = m_vVecLoad[j][z];
      m_count++;
    } 
  } 

  m_aveLoad = m_totalLoad / m_count;
  //std::cout << "[Statistics_on_Route]->Max:" << m_peakLoad << ", Min:" << m_abyssLoad << ", Ave:" << m_aveLoad << std::endl;
  myOfsSRoute << "Max," << m_peakLoad << ",Min," << m_abyssLoad << ",Ave," << m_aveLoad << std::endl;
} 

//-------------------------------------------------------------------------------------------------
void 
Application::GetStatisticalDataonNodeLoad ()
{
  int m_aveLoad = -1; // 平均負荷
  int m_maxLoad = -1; // 最大負荷
  int m_minLoad = 9999; // 最小負荷
  int m_totalLoad = 0; // 合計負荷
  int m_count = 0;
  
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); ++it) {
    if( m_maxLoad < it->second ) m_maxLoad = it->second;
    if( m_minLoad > it->second ) m_minLoad = it->second;
    m_totalLoad += it->second;
    m_count++;
  }
  m_aveLoad = m_totalLoad / m_count;
  //std::cout << "[Statistics_on_Node]->Max:" << m_maxLoad << ", Min:" << m_minLoad << ", Ave:" << m_aveLoad << std::endl;
  myOfsSLoad << "Max," << m_maxLoad << ",Min," << m_minLoad << ",Ave," << m_aveLoad << std::endl;
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyIncreaseNodeLoad (
  std::vector<Ipv4Address> myIpv4Route // 経路
)
{
  int m_variable = 0;
  int m_z = 0;
  for(std::vector<Ipv4Address>::const_iterator i = myIpv4Route.begin(); i != myIpv4Route.end(); i++){
    m_variable = 0;
    std::map<Ipv4Address, int>::iterator m_prov = g_myNodeLoad.find (myIpv4Route[m_z]);
    if(m_prov != g_myNodeLoad.end()){
      m_variable = m_prov->second;
      m_variable++; // 負荷を変化
      g_myNodeLoad.erase (myIpv4Route[m_z]); // 一時的に削除して、
      g_myNodeLoad.insert (std::make_pair (myIpv4Route[m_z], m_variable)); // 増やした、値を入れ直す
    }
    m_z++;
  }
}

//-------------------------------------------------------------------------------------------------
void
Application::MyDecreaseNodeLoad (
  std::vector<Ipv4Address> myIpv4Route // 経路
)
{
  int m_variable = 0;
  int m_z = 0;
  for(std::vector<Ipv4Address>::const_iterator i = myIpv4Route.begin(); i != myIpv4Route.end(); i++){
    m_variable = 0;
    std::map<Ipv4Address, int>::iterator m_prov = g_myNodeLoad.find (myIpv4Route[m_z]);
    if(m_prov != g_myNodeLoad.end()){
      m_variable = m_prov->second;
      m_variable--; // 負荷を変化
      g_myNodeLoad.erase (myIpv4Route[m_z]); // 一時的に削除して、
      g_myNodeLoad.insert (std::make_pair (myIpv4Route[m_z], m_variable)); // 増やした、値を入れ直す
    }
    m_z++;
  }
}

//-------------------------------------------------------------------------------------------------
std::vector<Ipv4Address>
Application::MyConvertFrom8toIP (
  std::vector<int> myIntRoute,
  Ipv4Address myIpv4
)
{
  std::vector<Ipv4Address> m_myIpv4Route;
  Ipv4Address m_myIpv4;
  uint32_t m_combine;
  uint8_t m_myBuffer[4];
  int m_z = 0;

  for( std::vector<int>::const_iterator it = myIntRoute.begin(); it != myIntRoute.end() ; ++it ){
    m_myBuffer[0] = (myIpv4.Get() >> 24) & 0xff;
    m_combine = 0;
    m_combine |= m_myBuffer[0];
    m_myBuffer[1] = (myIpv4.Get() >> 16) & 0xff;
    m_combine <<= 8;
    m_combine |= m_myBuffer[1];
    m_myBuffer[2] = (myIpv4.Get() >> 8) & 0xff;
    m_combine <<= 8;
    m_combine |= m_myBuffer[2];
    m_myBuffer[3] = myIntRoute[m_z] + 1;
    m_combine <<= 8;
    m_combine |= m_myBuffer[3];

    m_myIpv4.Set (m_combine);
    m_myIpv4Route.push_back (m_myIpv4);
    m_z++;
  } 

  return m_myIpv4Route;
}

//-------------------------------------------------------------------------------------------------
uint8_t 
Application::MyConvertFromIPto8 (
  Ipv4Address id 
  )
{
  uint8_t m_myBuffer[4];
  m_myBuffer[0] = (id.Get() >> 24) & 0xff;
  m_myBuffer[1] = (id.Get() >> 16) & 0xff;
  m_myBuffer[2] = (id.Get() >> 8) & 0xff;
  m_myBuffer[3] = (id.Get() >> 0) & 0xff;
  //std::cout << "Application::DijkstraMethod->Buffer[0]:" << unsigned(m_myBuffer[0]) << "Buffer[1]:" << unsigned(m_myBuffer[1])  << "Buffer[2]:" << unsigned(m_myBuffer[2]) << "Buffer[3]:" << unsigned(m_myBuffer[3]) << std::endl;
  return m_myBuffer[3];
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyPrintVectorIP (
  std::vector<Ipv4Address> myVec
  )
{
  for( std::vector<Ipv4Address>::const_iterator it = myVec.begin(); it != myVec.end() ; ++it ){
    std::cout << *it << ", ";
  } std::cout << std::endl;
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyPrintVectorVectorIP (
  std::vector<std::vector<Ipv4Address> > myVVec
  )
{
  for( int j=0; j<(int)myVVec.size(); j++ ){
    std::cout << " | ";
    for( int z=0; z<(int)myVVec[j].size(); z++ ){
      std::cout << myVVec[j][z] << ", ";
    } std::cout << std::endl;
  } 
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyPrintVector (
  std::vector<int> myVec
  )
{
  for( std::vector<int>::const_iterator it = myVec.begin(); it != myVec.end() ; ++it ){
    std::cout << *it << ", ";
  } std::cout << std::endl;
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyPrintVectorVector (
  std::vector<std::vector<int> > myVVec
  )
{
  for(int j=0;j<(int)myVVec.size();j++){
    std::cout << "[IPAddress]:10.0.0." << j+1 << " | ";
    for(int z=0; z<(int)myVVec[j].size(); z++){
      std::cout << myVVec[j][z] << ", ";
    } std::cout << std::endl;
  } 
}

//-------------------------------------------------------------------------------------------------
void 
Application::MyPrintVectorVectorDouble (
  std::vector<std::vector<double> > myVVec
  )
{
  for(int j=0;j<(int)myVVec.size();j++){
    std::cout << "[IPAddress]:10.0.0." << j+1 << " | ";
    for(int z=0; z<(int)myVVec[j].size(); z++){
      std::cout << myVVec[j][z] << ", ";
    } std::cout << std::endl;
  } 
}

//-------------------------------------------------------------------------------------------------
// 経路情報の表示
void 
Application::PrintMyRouteInfo (){
  std::cout << "[Route Information]" << std::endl;
  for (std::map<Ipv4Address, std::vector<std::vector<Ipv4Address>>>::const_iterator it = g_myRouteInfomation.begin (); it != g_myRouteInfomation.end (); ++it){
    std::vector<std::vector<Ipv4Address> > m_vv = it->second;
    std::cout << "[IPAddress]:" << it->first << ", [IPList]:";
    for( int j=0; j<(int)m_vv.size(); j++ ){
      for( int z=0; z<(int)m_vv[j].size(); z++ ){
        std::cout << m_vv[j][z] << ", ";
      } std::cout << " |";
    } std::cout << std::endl;
  } 
}

//-------------------------------------------------------------------------------------------------
void 
Application::OutputMyNodeLoad (){
  std::cout << "[Statistics_on_Load]->";
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); ++it) {
    std::cout << it->second << ", ";
    myOfsLoad << it->second << ", ";
  } 
  std::cout << std::endl;
  myOfsLoad << std::endl;
}

//-------------------------------------------------------------------------------------------------
void 
Application::PrintMyNodeLoad (){
  std::cout << "[Load Information]->";
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); ++it) {
    std::cout << "IPAddress:" << it->first << ", Load:" <<  it->second << std::endl;
  } 
}

//-------------------------------------------------------------------------------------------------
void 
Application::PrintMyNodePosition (){
  for (std::map<Ipv4Address, std::vector<int> >::const_iterator it = g_myNodePosition.begin (); it != g_myNodePosition.end (); ++it){
    std::cout << "[IPAddress]:" << it->first << ", [Position]:";
    std::vector<int> m_pos = it->second;
    for(int i=0; i<(int)m_pos.size(); i++) std::cout << m_pos[i] << ", ";
    std::cout << " | ";
  } std::cout << std::endl;
}

/*
//-------------------------------------------------------------------------------------------------
// ダイクストラ法による経路探索アルゴリズム 
// 多分、使用することはないだろう....
// 1.まずは、接続行列から最大の負荷を持つノードの抽出を行う
// 2.最大負荷ノードを通過する経路を一本(もしかしたら複数)抽出する
// 3.ダイクストラアルゴリズムで、経路全体の負荷を低下させようとする
void 
Application::DijkstraMethod (
  Ipv4Address mySrc, 
  Ipv4Address myDst,
  std::vector<Ipv4Address> nodeList
  )
{
  uint8_t m_mySrc = MyConvertFromIPto8(mySrc);
  uint8_t m_myDst = MyConvertFromIPto8(myDst);
  int m_mySource = (int)m_mySrc-1; // 送信元ノード(ノード番号)
  int m_myDestination = (int)m_myDst-1; // 宛先ノード(ノード番号)
  const int m_nodeNum = 36; // ノード数
  //int m_myNodeParagraph = 6; // 区切り数
  int m_myCost[m_nodeNum][m_nodeNum] = { }; // 接続行列(本来はノード数あるとよい)
  int m_myMultipleArray[m_nodeNum][3] = { }; // 負荷,X座標,Y座標
  int m_range = 130; // 電波の到達範囲
  int i = 0;
  int j = 0;
  int m_z = 9999; // 無限大の値
  int m_min = 0;
  int m_minId = -1;
  int m_peakLoadValue = -1; // 最大の負荷値
  int m_peakLoadNumber = -1; // 最大の負荷値を持つノード
  std::vector<int> m_bestRoute;

  std::cout << "Application::DijkstraMethod->Source:" << mySrc //<< ", 変換後->" << (unsigned)m_mySrc
            << ", Node number->" << m_mySource
            << ", Destination:" << myDst  //<< ", 変換後->" << (unsigned)m_myDst 
            << ", Node number->" << m_myDestination << std::endl; 
  
  // ノードの負荷情報(ついでに、最大の負荷を抽出)
  for(std::map<Ipv4Address, int>::const_iterator it = g_myNodeLoad.begin(); it != g_myNodeLoad.end(); ++it ){
    m_myMultipleArray[i][0] = it->second;
    if( m_mySource == i || m_myDestination == i){
      i++;
      continue; // 送信元ノードと宛先ノードだった場合、スキップ
    }
    if( m_peakLoadValue < it->second ){ // 大きければ
      m_peakLoadValue = it->second; // 最大の負荷値
      m_peakLoadNumber = i; // ノード番号
    }
    i++;
  } 

  //std::cout << "Peek node:" << m_peakLoadNumber << ", Peek load:" << m_peakLoadValue << std::endl;
  i = 0;

  // ノードの位置情報
  for( std::map<Ipv4Address, std::vector<int> >::const_iterator it = g_myNodePosition.begin (); it != g_myNodePosition.end (); ++it ){
    std::vector<int> m_pos = it->second;
    m_myMultipleArray[i][1] = m_pos[0]; // X座標
    m_myMultipleArray[i][2] = m_pos[1]; // Y座標
    //std::cout << m_myMultipleArray[i][1] << ", " << m_myMultipleArray[i][2] << std::endl;
    i++;
  }

  // 隣接行列への重み付け
	for( i=0; i<m_nodeNum; i++ ){
		for( j=0; j<m_nodeNum; j++ ){
      m_myCost[i][j] = m_z; // 無限大
      if(i == j) continue;
			if((m_myMultipleArray[i][1]-m_myMultipleArray[j][1])*(m_myMultipleArray[i][1]-m_myMultipleArray[j][1]) +
         (m_myMultipleArray[i][2]-m_myMultipleArray[j][2])*(m_myMultipleArray[i][2]-m_myMultipleArray[j][2]) < m_range * m_range){
        m_myCost[i][j] = 1; // 移動するときのコストは必ず1になるので、隣接行列は0にしてはいけない.
        m_myCost[i][j] += m_myMultipleArray[j][0];
			}
		}
	}

  // 接続行列の出力
  for( i=0; i<m_nodeNum; i++ ){
    std::cout << "Link of Node[" << i << "]." << std::endl;
		for( j=0; j<m_nodeNum; j++ ){
      std::cout << "[" << std::setw(4) << m_myCost[i][j] << "]";
			if( (j%m_myNodeParagraph)+1 == m_myNodeParagraph )
        std::cout << std::endl;
		}
  }

  // 探索集合の宣言と初期化 
  int m_P[m_nodeNum]; // 探索集合
  for( i=0; i<m_nodeNum; i++ ) m_P[i] = 0;
  m_P[m_mySource] = 1;

  // ラベル配列の作成と初期化
  int m_L[m_nodeNum][2]; // ラベル　L[id][0]:経路長、 L[i][1]:経路の直前ノード

  // ラベル初期化
  for( i=0; i<m_nodeNum; i++ ){
    m_L[i][0] = m_z;
    m_L[i][1] = m_mySource;
  }

  m_L[m_mySource][0] = 0; // 出発ノードの経路長は0に

  // 経路探索プロセス
  while(true){
    // P集合所属ノードから出る枝のノードのラベル更新
    for( i=0; i<m_nodeNum; i++ ){
      if( m_P[i] != 0 ){
        for( j=0; j<m_nodeNum; j++ ){
          if( m_L[i][0] + m_myCost[i][j] < m_L[j][0] ){
            m_L[j][0] = m_L[i][0] + m_myCost[i][j];
            m_L[j][1] = i;
          }
        }
      }
    }

    // P集合未所属のノードのラベルで最少値のノードをPに入れる
    m_min = 9999;
    for( i=0; i<m_nodeNum; i++ ){
      if( m_P[i] != 1 ){
        if(m_min > m_L[i][0]){
          m_min = m_L[i][0];
          m_minId = i;
        }
      }
    }
    m_P[m_minId] = 1; // P集合に所属させる

    // 目的ノードがPに所属したら探索終了
    if( m_P[m_myDestination] == 1 ) break;
  }
  
  // ルートのノードリスト
  int m_R[m_nodeNum]; // 逆ルート
  int m_RR[m_nodeNum]; // 正ルート

  // 逆ルートの初期化
  for(i=0; i<m_nodeNum; i++) m_R[i] = -1;
    
  i = 0;
  m_R[0] = m_myDestination;
  while( i < m_nodeNum ){
    // ラベルから逆ルート抽出
    m_R[i+1] = m_L[m_R[i]][1];
    if( m_R[i+1] == m_mySource ) break;
    i++;
  }

  // 正ルートの初期化
  for( i=0; i<m_nodeNum; i++ ) m_RR[i] = -1;

  j = 0;
  // 逆ルートから正ルートへのコピー
  for( i=m_nodeNum-1; i>=0; i-- ){
    if( m_R[i] != -1 ){
      m_RR[j] = m_R[i];
      j++;
    }	
  }

  // 準最適経路のvectorに入れ直す
  for( i=0; i<m_nodeNum; i++ ){
    if( m_RR[i] == -1) break;
    m_bestRoute.push_back( m_RR[i] );
  }

  std::cout << "The best route(NodeNumber):";
  for(int i=0; i<m_nodeNum; i++) {
    if( m_RR[i] == -1) break;
    std::cout << m_RR[i] << "-";
  } std::cout << std::endl;

  std::cout << "The best route(IPAddress):";
  for(int i=0; i<m_nodeNum; i++) {
    if( m_RR[i] == -1) break;
    std::cout << m_RR[i]+1 << "-";
  } std::cout << std::endl;
}
*/

//-------------------------------------------------------------------------------------------------
void
Application::SetStartTime (Time start)
{
  NS_LOG_FUNCTION (this << start);
  m_startTime = start;
}
void
Application::SetStopTime (Time stop)
{
  NS_LOG_FUNCTION (this << stop);
  m_stopTime = stop;
}


void
Application::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_node = 0;
  m_startEvent.Cancel ();
  m_stopEvent.Cancel ();
  Object::DoDispose ();
}

void
Application::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  m_startEvent = Simulator::Schedule (m_startTime, &Application::StartApplication, this);
  if (m_stopTime != TimeStep (0)){
    m_stopEvent = Simulator::Schedule (m_stopTime, &Application::StopApplication, this);
  }
  Object::DoInitialize ();
}

Ptr<Node> Application::GetNode () const
{
  NS_LOG_FUNCTION (this);
  return m_node;
}

void 
Application::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this);
  m_node = node;
}

// Protected methods
// StartApp and StopApp will likely be overridden by application subclasses
void 
Application::StartApplication ()
{ // Provide null functionality in case subclass is not interested
  NS_LOG_FUNCTION (this);
  std::cout << "Application::StartApplication" << std::endl;
}

void 
Application::StopApplication ()
{ // Provide null functionality in case subclass is not interested
  NS_LOG_FUNCTION (this);
}

} // namespace ns3


