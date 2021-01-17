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

#ifndef APPLICATION_H
#define APPLICATION_H

#include <map>
#include <vector>
#include <iomanip> 
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/ipv4-address.h" // 追加

namespace ns3 {

class Node;

/**
 * \addtogroup applications Applications
 *
 * Class ns3::Application can be used as a base class for ns3 applications.
 * Applications are associated with individual nodes.  Each node
 * holds a list of references (smart pointe¥rs) to its applications.
 * 
 * Conceptually, an application has zero or more ns3::Socket
 * objects associated with it, that are created using the Socket
 * creation API of the Kernel capability.  The Socket object
 * API is modeled after the
 * well-known BSD sockets interface, although it is somewhat 
 * simplified for use with ns3.  Further, any socket call that
 * would normally "block" in normal sockets will return immediately
 * in ns3.  A set of "upcalls" are defined that will be called when
 * the previous blocking call would normally exit.  THis is documented
 * in more detail Socket class in socket.h.
 *
 * The main purpose of the base class application public API is to
 * provide a uniform way to start and stop applications.
 */

/**
 * \brief The base class for all ns3 applications
 */
class Application : public Object
{
public:

  static std::map<Ipv4Address, std::vector<std::vector<Ipv4Address>>> g_myRouteInfomation; // 経路情報
  static std::map<Ipv4Address, int> g_myNodeLoad; // コントローラからみた、各ノードの負荷
  static std::map<Ipv4Address, std::vector<int> > g_myNodePosition; // ノードの位置情報(x軸,y軸)を推定したい...
  static int g_myNodeNum; // ノード数
  static std::ofstream myOfsSLoad;
  static std::ofstream myOfsSRoute;
  static std::ofstream myOfsLoad;
  static std::ofstream myOfsSHop;
  //static std::ofstream myOfsDynamicLoad;

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  Application ();
  virtual ~Application ();

  /**
   * 引数に入っているベクターを経路情報変数に追加する 
   **/
  void ReceiveData (std::vector<Ipv4Address>); // 経路情報を追加
  /**
   * ダイクストラアルゴリズム
   **/
  void DijkstraMethod ();
  /**
   * 変換用(IP→ノード番号)
   *  この関数では、送信元と宛先ノードだけ変換すれば良いので、引数がIPの変数で良い.
   **/ 
  uint8_t MyConvertFromIPto8 (Ipv4Address); 
  /**
   * 変換用(ノード番号→IP)
   *  第一引数の可変長配列を全て、IPに変換する.
   *  intからIPの変換は難しいので、第二引数のIPを使用して元のIPに戻す.
   **/
  std::vector<Ipv4Address> MyConvertFrom8toIP (std::vector<int>, Ipv4Address);
  
  /**
   * ノードの負荷情報を減少させる
   **/
  void MyDecreaseNodeLoad (std::vector<Ipv4Address>);
  /**
   * ノードの負荷情報を減少させる
   **/
  void MyIncreaseNodeLoad (std::vector<Ipv4Address>);
  /**
   * 第一引数の可変長配列経路を、第二引数の可変長配列の経路に移行する
   **/
  void SwitchTheRoute (std::vector<Ipv4Address>, std::vector<Ipv4Address>, int, int);
  /**
   * 経路の検出
   *  合計のコストが最も高い経路を検出
   **/
  void RouteDetectionBasedonTotalCost (std::vector<Ipv4Address>*, int*, int*);
  /**
   * 経路の検出
   *  合計のコストをホップ数で割った、1ホップにかかるコストが最も高い経路を検出
   **/
  void RouteDetectionBasedonAverageCost (std::vector<Ipv4Address>*, int*, int*);
  /**
   * 統計データを出力するための関数
   **/
  void GetStatisticalDataonNodeLoad (); // ネットワーク全体の負荷>最大、最小、平均
  void GetStatisticalDataonRouteLoad (); // 経路全体の負荷>最大、最小、平均
  void GetStatisticalDataonHop (); // 経路のホップ数
  
  void PrintMyRouteInfo (); // 経路情報出力
  void PrintMyNodeLoad (); // ノードの負荷情報の出力
  void OutputMyNodeLoad (); // ノードの負荷情報の出力
  void PrintMyNodePosition (); // ノードの位置情報を出力
  void MyPrintVector (std::vector<int>);
  void MyPrintVectorIP (std::vector<Ipv4Address>);
  void MyPrintVectorVector (std::vector<std::vector<int> >);
  void MyPrintVectorVectorDouble (std::vector<std::vector<double> >);
  void MyPrintVectorVectorIP (std::vector<std::vector<Ipv4Address> >);

  //void NetworkLoadFluctuation ();
  //void NetworkLoadFluctuation (Time);
  //void MySchedule (Time);
  //---------------------------------------------------------------------------------------------------------

  /**
   * \brief Specify application start time
   * \param start Start time for this application, relative to the current simulation time.
   *
   * Applications start at various times in the simulation scenario.
   * The Start method specifies when the application should be
   * started.  The application subclasses should override the
   * private "StartApplication" method defined below, which is called at the
   * time specified, to cause the application to begin.
   */
  void SetStartTime (Time start);

  /**
   * \brief Specify application stop time
   * \param stop Stop time for this application, relative to the
   *        current simulation time.
   *
   * Once an application has started, it is sometimes useful
   * to stop the application.  The Stop method specifies when an
   * application is to stop.  The application subclasses should override
   * the private StopApplication method, to be notified when that
   * time has come.
   */
  void SetStopTime (Time stop);

  /**
   * \returns the Node to which this Application object is attached.
   */
  Ptr<Node> GetNode () const;

  /**
   * \param node the node to which this Application object is attached.
   */
  void SetNode (Ptr<Node> node);

private:
  /**
   * \brief Application specific startup code
   *
   * The StartApplication method is called at the start time specified by Start
   * This method should be overridden by all or most application
   * subclasses.
   */
  virtual void StartApplication (void);
  /**
   * \brief Application specific shutdown code
   *
   * The StopApplication method is called at the stop time specified by Stop
   * This method should be overridden by all or most application
   * subclasses.
   */
  virtual void StopApplication (void);

  // 比較演算子のため
  //friend bool operator == (Ipv4Address const &a, Ipv4Address const &b);
  //friend bool operator != (Ipv4Address const &a, Ipv4Address const &b);
  //friend bool operator < (Ipv4Address const &a, Ipv4Address const &b);

protected:
  virtual void DoDispose (void);
  virtual void DoInitialize (void);

  //Ipv4Address m_mySrc; // ダイクストラアルゴリズムで使用する送信元ノード
  //Ipv4Address m_myDst; // ダイクストラアルゴリズムで使用する宛先ノード

  Ptr<Node> m_node;         //!< The node that this application is installed on
  Time m_startTime;         //!< The simulation time that the application will start
  Time m_stopTime;          //!< The simulation time that the application will end
  EventId m_startEvent;     //!< The event that will fire at m_startTime to start the application
  EventId m_stopEvent;      //!< The event that will fire at m_stopTime to end the application
};

} // namespace ns3

#endif /* APPLICATION_H */
