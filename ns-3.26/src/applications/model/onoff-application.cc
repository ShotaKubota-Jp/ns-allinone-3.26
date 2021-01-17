/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
//
// Copyright (c) 2006 Georgia Tech Research Corporation
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation;
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Author: George F. Riley<riley@ece.gatech.edu>
//

// ns3 - On/Off Data Source Application class
// George F. Riley, Georgia Tech, Spring 2007
// Adapted from ApplicationOnOff in GTNetS.

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "onoff-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("OnOffApplication");

NS_OBJECT_ENSURE_REGISTERED (OnOffApplication);

TypeId
OnOffApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::OnOffApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<OnOffApplication> ()
    .AddAttribute ("DataRate", "The data rate in on state.",
                   DataRateValue (DataRate ("500kb/s")),
                   MakeDataRateAccessor (&OnOffApplication::m_cbrRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PacketSize", "The size of packets sent in on state",
                   UintegerValue (512),
                   MakeUintegerAccessor (&OnOffApplication::m_pktSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&OnOffApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("OnTime", "A RandomVariableStream used to pick the duration of the 'On' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&OnOffApplication::m_onTime),
                   MakePointerChecker <RandomVariableStream>())
    .AddAttribute ("OffTime", "A RandomVariableStream used to pick the duration of the 'Off' state.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&OnOffApplication::m_offTime),
                   MakePointerChecker <RandomVariableStream>())
    .AddAttribute ("MaxBytes", 
                   "The total number of bytes to send. Once these bytes are sent, "
                   "no packet is sent again, even in on state. The value zero means "
                   "that there is no limit.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&OnOffApplication::m_maxBytes),
                   MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&OnOffApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&OnOffApplication::m_txTrace),
                     "ns3::Packet::TracedCallback");
  return tid;
}


OnOffApplication::OnOffApplication ()
  : m_socket (0),
    m_connected (false),
    m_residualBits (0),
    m_lastStartTime (Seconds (0)),
    m_totBytes (0)
{
  NS_LOG_FUNCTION (this);
}

OnOffApplication::~OnOffApplication()
{
  NS_LOG_FUNCTION (this);
}

void 
OnOffApplication::SetMaxBytes (uint64_t maxBytes)
{
  NS_LOG_FUNCTION (this << maxBytes);
  m_maxBytes = maxBytes;
}

Ptr<Socket>
OnOffApplication::GetSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

int64_t 
OnOffApplication::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_onTime->SetStream (stream);
  m_offTime->SetStream (stream + 1);
  return 2;
}

void
OnOffApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  Application::DoDispose (); // chain up
}

// Application Methods
void 
OnOffApplication::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);
  std::cout << "-----------------------------------------------------------------------------------------" << std::endl;
  std::cout << "OnOffApplication::StartApplication->Peer:" << m_peer << ", Socket:" << m_socket << std::endl;
  // Create the socket if not already
  if (!m_socket){
    m_socket = Socket::CreateSocket (GetNode (), m_tid);
    if (Inet6SocketAddress::IsMatchingType (m_peer))
      m_socket->Bind6 ();
    else if (InetSocketAddress::IsMatchingType (m_peer) || PacketSocketAddress::IsMatchingType (m_peer))
      m_socket->Bind ();
    m_socket->Connect (m_peer);
    m_socket->SetAllowBroadcast (true);
    m_socket->ShutdownRecv ();
    m_socket->SetConnectCallback (
      MakeCallback (&OnOffApplication::ConnectionSucceeded, this),
      MakeCallback (&OnOffApplication::ConnectionFailed, this));
  }
  m_cbrRateFailSafe = m_cbrRate;

  // Insure no pending event
  CancelEvents ();
  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;
  ScheduleStartEvent ();
}

void 
OnOffApplication::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);
  std::cout << "OnOffApplication::StopApplication->" << Simulator::Now() << std::endl;
  CancelEvents ();
  if(m_socket != 0){
    m_socket->Close ();
  }else{
    std::cout << "OnOffApplication found null socket to close in StopApplication" << std::endl;
    //NS_LOG_WARN ("OnOffApplication found null socket to close in StopApplication");
  }
}

void 
OnOffApplication::CancelEvents ()
{
  NS_LOG_FUNCTION (this);
  if (m_sendEvent.IsRunning () && m_cbrRateFailSafe == m_cbrRate ){ 
    // Cancel the pending send packet event
    // Calculate residual bits since last packet sent
    Time delta (Simulator::Now () - m_lastStartTime);
    int64x64_t bits = delta.To (Time::S) * m_cbrRate.GetBitRate ();
    m_residualBits += bits.GetHigh ();
  }
  m_cbrRateFailSafe = m_cbrRate;
  std::cout << "OnOffApplication::CancelEvents(保留中の全てのイベントをキャンセル)->CBR Rate:" << m_cbrRate.GetBitRate() << std::endl;
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_startStopEvent);
}

// Event handlers
void 
OnOffApplication::StartSending ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "OnOffApplication::StartSending->SimulationTime:" << Simulator::Now () << std::endl;
  m_lastStartTime = Simulator::Now ();
  ScheduleNextTx ();  // Schedule the send packet event
  ScheduleStopEvent ();
}

void 
OnOffApplication::StopSending ()
{
  NS_LOG_FUNCTION (this);
  std::cout << "OnOffApplication::StopSending" << std::endl;
  CancelEvents ();
  ScheduleStartEvent ();
}

// Private helpers
void 
OnOffApplication::ScheduleNextTx ()
{
  NS_LOG_FUNCTION (this);
  if (m_maxBytes == 0 || m_totBytes < m_maxBytes){
    uint32_t bits = m_pktSize * 8 - m_residualBits;
    //NS_LOG_LOGIC ("bits = " << bits);
    Time nextTime (Seconds (bits / static_cast<double>(m_cbrRate.GetBitRate ()))); // Time till next packet
    //NS_LOG_LOGIC ("nextTime = " << nextTime);
    std::cout << "OnOffApplication::ScheduleNextTx(パケット送信をスケジューリング)->Bits=" << bits << ", NextTime=" << nextTime << std::endl;
    m_sendEvent = Simulator::Schedule (nextTime, &OnOffApplication::SendPacket, this);
  }else{ 
    std::cout << "OnOffApplication::ScheduleNextTx(パケット送信をスケジューリング)" << std::endl;
    StopApplication (); // All done, cancel any pending events
  }
}

void 
OnOffApplication::ScheduleStartEvent ()
{  
  // Schedules the event to start sending data (switch to the "On" state)
  NS_LOG_FUNCTION (this);
  Time offInterval = Seconds (m_offTime->GetValue ());
  std::cout << "OnOffApplication::ScheduleStartEvent(データ送信を開始するようにイベントをスケジューリング)->Start at " << offInterval << std::endl;
  //NS_LOG_LOGIC ("start at " << offInterval);
  m_startStopEvent = Simulator::Schedule (offInterval, &OnOffApplication::StartSending, this);
}

void 
OnOffApplication::ScheduleStopEvent ()
{  
  // Schedules the event to stop sending data (switch to "Off" state)
  NS_LOG_FUNCTION (this);
  Time onInterval = Seconds (m_onTime->GetValue ());
  std::cout << "OnOffApplication::ScheduleStopEvent(パケット送信を停止)->Stop at " << onInterval << std::endl;
  //NS_LOG_LOGIC ("stop at " << onInterval);
  m_startStopEvent = Simulator::Schedule (onInterval, &OnOffApplication::StopSending, this);
}

void 
OnOffApplication::SendPacket ()
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_sendEvent.IsExpired ());
  Ptr<Packet> packet = Create<Packet> (m_pktSize);
  m_txTrace (packet);
  m_socket->Send (packet);
  m_totBytes += m_pktSize;
  if (InetSocketAddress::IsMatchingType (m_peer)){
    std::cout << "===================================================================================" << std::endl;
    std::cout << "OnOffApplication::SendPacket->At time " << Simulator::Now ().GetSeconds ()
                  << "s On-Off Application Sent "
                  <<  packet->GetSize () << " bytes to "
                  << InetSocketAddress::ConvertFrom(m_peer).GetIpv4 ()
                  << ", Port " << InetSocketAddress::ConvertFrom (m_peer).GetPort ()
                  << ", Total Tx " << m_totBytes << " bytes." << std::endl;
  }else if (Inet6SocketAddress::IsMatchingType (m_peer)){
    std::cout << "===================================================================================" << std::endl;
    std::cout << "OnOffApplication::SendPacket->At time " << Simulator::Now ().GetSeconds ()
                  << "s On-Off Application Sent "
                  <<  packet->GetSize () << " bytes to "
                  << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6 ()
                  << ", Port " << Inet6SocketAddress::ConvertFrom (m_peer).GetPort ()
                  << ", Total Tx " << m_totBytes << " bytes." << std::endl;
  }
  m_lastStartTime = Simulator::Now ();
  m_residualBits = 0;
  ScheduleNextTx ();
}


void 
OnOffApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  std::cout << "OnOffApplication::ConnectionSucceeded->Socket" << socket << std::endl;
  m_connected = true;
}

void 
OnOffApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  std::cout << "OnOffApplication::ConnectionFailed->Socket" << socket << std::endl;
}

} // Namespace ns3
