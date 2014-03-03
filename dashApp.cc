
#include <fstream>
#include <vector>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DashApplication");

// ===========================================================================
//deal with it later
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 2 ms
//
//
// We want to look at changes in the ns-3 TCP congestion window.  We need
// to crank up a flow and hook the CongestionWindow attribute on the socket
// of the sender.  Normally one would use an on-off application to generate a
// flow, but this has a couple of problems.  First, the socket of the on-off 
// application is not created until Application Start time, so we wouldn't be 
// able to hook the socket (now) at configuration time.  Second, even if we 
// could arrange a call after start time, the socket is not public so we 
// couldn't get at it.
//
// So, we can cook up a simple version of the on-off application that does what
// we want.  On the plus side we don't need all of the complexity of the on-off
// application.  On the minus side, we don't have a helper, so we have to get
// a little more involved in the details, but this is trivial.
//
// So first, we create a socket and do the trace connect on it; then we pass 
// this socket into the constructor of our simple application which we then 
// install in the source node.
// ===========================================================================
//

class DashServerApp: public Application
{
public:
  DashServerApp();
  virtual ~DashServerApp();
  void Setup (Address address);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);
  
  void RxCallback(Ptr<Socket> socket);
  bool ConnectionCallback(Ptr<Socket> s, const Address& ad);
  void AcceptCallback(Ptr<Socket> s, const Address& ad);
  void SendData(); 
 
  Ptr<Socket> m_socket;
  Ptr<Socket> m_peer_socket;
  Address ads;
  Address m_peer_address;
  uint32_t m_remainingData;
  EventId         m_sendEvent;
};

DashServerApp::DashServerApp()
: m_socket(0),
  m_peer_socket(0),
  ads(),
  m_peer_address(),
  m_remainingData(0),
  m_sendEvent()
{
  
}

DashServerApp::~DashServerApp()
{
  m_socket = 0;
}

void
DashServerApp::Setup(Address address)
{
  ads = address;
}

void
DashServerApp::StartApplication()
{
  m_socket = Socket::CreateSocket(GetNode(),TcpSocketFactory::GetTypeId ());
  m_socket->Bind(ads);
  m_socket->Listen();
  m_socket->SetAcceptCallback( MakeCallback(&DashServerApp::ConnectionCallback,this),
                               MakeCallback(&DashServerApp::AcceptCallback,this));
  m_socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback,this));
}

void
DashServerApp::StopApplication()
{
  if (m_socket)
    m_socket->Close();
  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }
}

bool
DashServerApp::ConnectionCallback(Ptr<Socket> socket, const Address& ads)
{
  NS_LOG_UNCOND("Server: connection callback ");
  return true;
}

void
DashServerApp::AcceptCallback(Ptr<Socket> socket, const Address& ads)
{
  NS_LOG_UNCOND("Server: accept callback ");
  m_peer_address = ads;
  m_peer_socket = socket;
  socket->SetRecvCallback(MakeCallback(&DashServerApp::RxCallback,this));
}

void
DashServerApp::RxCallback(Ptr<Socket> socket)
{
   Address ads;
   Ptr<Packet> pckt = socket->RecvFrom(ads);
   if (ads == m_peer_address)
     {
       uint32_t data = 0;
       pckt->CopyData((uint8_t*)&data,4);
       NS_LOG_UNCOND("Server: pckt size requested " << data);
       m_remainingData = data;
       NS_LOG_UNCOND("Server: pckt send start time " << Simulator::Now().GetMilliSeconds());
       SendData();
     } 
}

void
DashServerApp::SendData()
{
  uint32_t available = (m_peer_socket->GetTxAvailable() * 8) / 10 ;
  if (available >= m_remainingData) 
    {
       Ptr<Packet> sendPckt = Create<Packet> (m_remainingData);
       m_peer_socket->Send(sendPckt);
       m_remainingData = 0;
       NS_LOG_UNCOND("Server: pckt send end time " << Simulator::Now().GetMilliSeconds());
    }
  else
    {
       Ptr<Packet> sendPckt = Create<Packet> (available);
       m_peer_socket->Send(sendPckt);
       m_remainingData = m_remainingData - available;
       Time t("100ms");
       m_sendEvent = Simulator::Schedule(t,&DashServerApp::SendData,this); 
    }
}

//Client Application
class DashClientApp : public Application 
{
public:

  DashClientApp ();
  virtual ~DashClientApp();
  enum {MAX_BUFFER_SIZE = 30000}; // 30 seconds

  void Setup (Address address, uint32_t chunkSize, uint32_t numChunks);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void SendRequest (void);
  void ScheduleFetch (void);
  void GetStatistics (void);
  uint32_t GetNextBitrate();
  void RxCallback(Ptr<Socket> socket);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_chunkSize;
  uint32_t        m_numChunks;
  uint32_t        m_chunkCount;
  int32_t        m_bufferSize;
  uint32_t        m_bufferPercent;
  uint32_t        m_bpsAvg;
  uint32_t        m_bpsLastChunk;
  std::vector<uint32_t>  m_bitrate_array; 
  EventId         m_fetchEvent;
  EventId         m_statisticsEvent;
  bool            m_running;
  bool            m_requestState;
  uint32_t        m_commulativeTime;
  uint32_t        m_commulativeSize;
  uint32_t        m_lastRequestedSize;
  Time            m_requestStartTime;
  uint32_t        m_sessionData;
  uint32_t        m_sessionTime;
};

DashClientApp::DashClientApp ()
  : m_socket (0), 
    m_peer (), 
    m_chunkSize (0), 
    m_numChunks (0), 
    m_chunkCount (0), 
    m_bufferSize (0), 
    m_bufferPercent (0), 
    m_bpsAvg (0), 
    m_bpsLastChunk (0), 
    m_fetchEvent (), 
    m_statisticsEvent (), 
    m_running (false), 
    m_requestState (false), 
    m_commulativeTime(0), 
    m_commulativeSize (0), 
    m_lastRequestedSize (0), 
    m_requestStartTime (),
    m_sessionData(0),
    m_sessionTime(0) 
{
}

DashClientApp::~DashClientApp()
{
  m_socket = 0;
}

void
DashClientApp::Setup (Address address, uint32_t chunkSize, uint32_t numChunks)
{
  m_peer = address;
  m_chunkSize = chunkSize;
  m_numChunks = numChunks;
  //bitrate profile of the contnet
  //need to think of better configuration means to capture this static data.
  m_bitrate_array.push_back(46881);
  m_bitrate_array.push_back(92310);
  m_bitrate_array.push_back(137796);
  m_bitrate_array.push_back(186276);
  m_bitrate_array.push_back(232403);
  m_bitrate_array.push_back(277536);
  m_bitrate_array.push_back(367110);
  m_bitrate_array.push_back(464254);
  m_bitrate_array.push_back(555312);
  m_bitrate_array.push_back(646172);
  m_bitrate_array.push_back(828868);
  m_bitrate_array.push_back(1008982);
  m_bitrate_array.push_back(1264958);
  m_bitrate_array.push_back(1513458);
  m_bitrate_array.push_back(1754697);
  m_bitrate_array.push_back(2141349);
  m_bitrate_array.push_back(2501338);
  m_bitrate_array.push_back(3161750);
  m_bitrate_array.push_back(3732333);
}

void
DashClientApp::StartApplication (void)
{
  m_socket = Socket::CreateSocket (GetNode(),TcpSocketFactory::GetTypeId ());
  m_running = true;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  m_socket->SetRecvCallback(MakeCallback(&DashClientApp::RxCallback, this));
  Time tS("500ms");
  m_statisticsEvent = Simulator::Schedule(tS,&DashClientApp::GetStatistics,this);
  Time tF(Seconds (m_chunkSize));
  m_fetchEvent = Simulator::Schedule(tF,&DashClientApp::ScheduleFetch,this);
  SendRequest ();
}

void
DashClientApp::RxCallback(Ptr<Socket> socket)
{
  Ptr<Packet> packet = socket->Recv();
  m_commulativeSize += packet->GetSize();
  //NS_LOG_UNCOND("Client received packet size " << packet->GetSize() << " Commulative size: " << m_commulativeSize << " at time " << Simulator::Now().GetMilliSeconds());
  if (m_commulativeSize >= m_lastRequestedSize)
    {
      //received the complete chunk
      //update the buffer size and initiate the next request.
      NS_LOG_UNCOND("Client received complete chunk at " << Simulator::Now().GetMilliSeconds());
      m_chunkCount++;
      m_requestState = false;
      Time t = Simulator::Now();
      m_bpsLastChunk = (m_commulativeSize*8)/(t.GetMilliSeconds() - m_requestStartTime.GetMilliSeconds());
      m_bpsLastChunk = m_bpsLastChunk*1000;
      /*if (m_bpsAvg == 0)
        m_bpsAvg = m_bpsLastChunk;
      m_bpsAvg = (m_bpsAvg * 75)/100 + (m_bpsLastChunk*25)/100;
      */
      m_sessionData += m_commulativeSize;
      m_sessionTime += (t.GetMilliSeconds() - m_requestStartTime.GetMilliSeconds());
      m_bpsAvg = (m_sessionData/m_sessionTime) * 8 * 1000;

      m_bufferSize += m_chunkSize*1000;
      m_bufferPercent = (m_bufferSize * 100) / MAX_BUFFER_SIZE;
      if (m_bufferPercent < 100)
        {
          SendRequest();
        }
    } 
}

void 
DashClientApp::StopApplication (void)
{
  m_running = false;

  if (m_fetchEvent.IsRunning ())
    {
      Simulator::Cancel (m_fetchEvent);
    }

  if (m_statisticsEvent.IsRunning ())
    {
      Simulator::Cancel (m_statisticsEvent);
    }
  if (m_socket)
    {
      m_socket->Close ();
    }
}

void 
DashClientApp::SendRequest (void)
{
  uint32_t nextRate = GetNextBitrate();
  if (nextRate == 0) 
    {
      // all chunks requested out.just return
      return;
    }
  uint32_t bytesReq = (nextRate * m_chunkSize) /8;
  Ptr<Packet> packet = Create<Packet> ((uint8_t*)&bytesReq,4);
  m_commulativeSize = 0;
  m_lastRequestedSize = bytesReq; 
  m_socket->Send (packet);
  m_requestState = true; 
  m_requestStartTime = Simulator::Now();
  NS_LOG_UNCOND ("send request client bitrate " << nextRate << " at " << Simulator::Now().GetMilliSeconds()) ;
}

uint32_t DashClientApp::GetNextBitrate()
{
  if (m_chunkCount >= m_numChunks)
    {
      return 0;
    }
  uint32_t bandwidth = m_bpsAvg;
  if (m_bufferPercent < 30 ) 
    {
      bandwidth = 0;
    }
  uint32_t requestBitrate = m_bitrate_array[0];
  for (uint32_t i = 1; i < m_bitrate_array.size(); i++)
    {
      if (bandwidth <= m_bitrate_array[i])
        {
          break;
        }
      requestBitrate = m_bitrate_array[i];
    }
  return requestBitrate;
}
void 
DashClientApp::ScheduleFetch (void)
{
  if (m_running)
    {
      m_bufferSize -= 1000;
      if (m_bufferSize < 0)
        m_bufferSize = 0;
      m_bufferPercent = (m_bufferSize * 100) / MAX_BUFFER_SIZE;
      if (m_bufferPercent < 100 && m_requestState == false)
        SendRequest();
      Time tNext (Seconds (1));
      m_fetchEvent = Simulator::Schedule (tNext, &DashClientApp::ScheduleFetch, this);
    }
}

void
DashClientApp::GetStatistics ()
{
  NS_LOG_UNCOND ("=======START===========" << GetNode() << "=================");
  NS_LOG_UNCOND ("Time: " << Simulator::Now ().GetMilliSeconds () << 
                 " BufferPercent: " << m_bufferPercent <<
                 " bps Average: " << m_bpsAvg <<
                 " bps Last Chunk: " << m_bpsLastChunk <<
                 " last Bitrate: " << (m_lastRequestedSize * 8)/m_chunkSize <<
                 " Chunk count: " << m_chunkCount <<
                 " Total chunks: " << m_numChunks );
  NS_LOG_UNCOND ("===================" << GetNode() << "========END=========");
  Time tNext ("500ms");
  m_statisticsEvent = Simulator::Schedule (tNext, &DashClientApp::GetStatistics, this);
}

int 
main (int argc, char *argv[])
{
  LogComponentEnable("DashApplication",LOG_LEVEL_ALL);
  //LogComponentEnable("BulkSendApplication",LOG_LEVEL_LOGIC);
  
  // create point to point link helpers
  PointToPointHelper bottleNeck;
  bottleNeck.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
  bottleNeck.SetChannelAttribute("Delay", StringValue("10ms"));
 

  PointToPointHelper pointToPointLeaf;
  pointToPointLeaf.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPointLeaf.SetChannelAttribute ("Delay", StringValue ("2ms"));

  PointToPointDumbbellHelper dB ( 10, pointToPointLeaf,
                                  10, pointToPointLeaf,
                                  bottleNeck);
  
  // install stack
  InternetStackHelper stack;
  dB.InstallStack (stack);

  // assign IP addresses
  dB.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"),
                         Ipv4AddressHelper ("10.2.1.0", "255.255.255.0"),
                         Ipv4AddressHelper ("10.3.1.0", "255.255.255.0"));

  
  //Install an On/Off client/server pair in the dumbbell topology
  // these applications are used as cross-traffic source and sink

  uint16_t serverPort = 8080;
  BulkSendHelper crossTrafficSrc("ns3::TcpSocketFactory", Address ());
  //OnOffHelper crossTrafficSrc("ns3::TcpSocketFactory", Address ());
  //crossTrafficSrc.SetAttribute ("OnTime", StringValue ("ns3::UniformRandomVariable[Min=0.,Max=1.]"));
  //crossTrafficSrc.SetAttribute ("OffTime", StringValue ("ns3::UniformRandomVariable[Min=0.,Max=1.]"));
  crossTrafficSrc.SetAttribute ("SendSize", UintegerValue(32768));
 
  AddressValue remoteAddress (InetSocketAddress (dB.GetRightIpv4Address (0), serverPort));
  crossTrafficSrc.SetAttribute ("Remote", remoteAddress);
  ApplicationContainer srcApp;
  srcApp.Add ( crossTrafficSrc.Install (dB.GetLeft (0))) ;  
  srcApp.Start(Seconds (0.0));
  srcApp.Stop(Seconds (2400.0));

  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  ApplicationContainer sinkApps;
  sinkApps.Add( packetSinkHelper.Install (dB.GetRight (0)) );
  sinkApps.Start(Seconds (0.0));
  sinkApps.Stop(Seconds (2400.0));

  ApplicationContainer sinkApps2;
  for (int j = 3; j < 4 ; j++) {
    sinkApps2.Add( packetSinkHelper.Install (dB.GetRight (j)) );
  }
  sinkApps2.Start(Seconds (0.0));
  sinkApps2.Stop(Seconds (2400.0));
 
  ApplicationContainer srcApps2;
  for (int j = 3; j < 4 ; j++) {
    AddressValue remoteAddress (InetSocketAddress (dB.GetRightIpv4Address (j), serverPort));
    crossTrafficSrc.SetAttribute ("Remote", remoteAddress);
    //srcApps2.Add ( crossTrafficSrc.Install (dB.GetLeft (j))) ;  
  }
  //srcApps2.Start(Seconds (800.0));
  //srcApps2.Stop(Seconds (2400.0));
  
  Address bindAddress1 (InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  Ptr<DashServerApp> serverApp1 = CreateObject<DashServerApp> ();
  serverApp1->Setup (bindAddress1);
  dB.GetLeft (1)->AddApplication (serverApp1);
  serverApp1->SetStartTime (Seconds (0.0));
  serverApp1->SetStopTime (Seconds (2400.0));

  Address serverAddress1 (InetSocketAddress (dB.GetLeftIpv4Address (1), serverPort));
  Ptr<DashClientApp> clientApp1 = CreateObject<DashClientApp> ();
  clientApp1->Setup (serverAddress1, 6,500);
  dB.GetRight (1)->AddApplication (clientApp1);
  clientApp1->SetStartTime (Seconds (5.0));
  clientApp1->SetStopTime (Seconds (2400.0));

  Address bindAddress2 (InetSocketAddress (Ipv4Address::GetAny (), serverPort));
  Ptr<DashServerApp> serverApp2 = CreateObject<DashServerApp> ();
  serverApp2->Setup (bindAddress2);
  dB.GetLeft (2)->AddApplication (serverApp2);
  serverApp2->SetStartTime (Seconds (0.0));
  serverApp2->SetStopTime (Seconds (2400.0));

  Address serverAddress2 (InetSocketAddress (dB.GetLeftIpv4Address (2), serverPort));
  Ptr<DashClientApp> clientApp2 = CreateObject<DashClientApp> ();
  clientApp2->Setup (serverAddress2, 6,500);
  dB.GetRight (2)->AddApplication (clientApp2);
  clientApp2->SetStartTime (Seconds (800.0));
  clientApp2->SetStopTime (Seconds (2400.0));
  
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Stop (Seconds (2400.0));
  Simulator::Run ();

  uint32_t totalRxBytesCounter = 0;
  for (uint32_t i = 0; i < sinkApps.GetN (); i++)
    {
      Ptr <Application> app = sinkApps.Get (i);
      Ptr <PacketSink> pktSink = DynamicCast <PacketSink> (app);
      totalRxBytesCounter += pktSink->GetTotalRx ();
    }
  NS_LOG_UNCOND ("\nGoodput-1 Bytes/sec:"
                 << totalRxBytesCounter/Simulator::Now ().GetSeconds ());
  NS_LOG_UNCOND ("----------------------------");
  totalRxBytesCounter = 0;
  for (uint32_t i = 0; i < sinkApps2.GetN (); i++)
    {
      Ptr <Application> app = sinkApps2.Get (i);
      Ptr <PacketSink> pktSink = DynamicCast <PacketSink> (app);
      totalRxBytesCounter += pktSink->GetTotalRx ();
    }
  NS_LOG_UNCOND ("\nGoodput-2 Bytes/sec:"
                 << totalRxBytesCounter/1600);
  NS_LOG_UNCOND ("----------------------------");
  

  Simulator::Destroy ();

  return 0;
}

