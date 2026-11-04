////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 RWS Inc, All Rights Reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of version 2 of the GNU General Public License as published by
// the Free Software Foundation
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
//
// ProtoBSDIP.cpp
// Project: Postal
// 
// History:
//		08/04/97 BRH	Started.
//
//		08/06/97 BRH	Debugged - restored to working order.
//					MJR
//
//		08/07/97 BRH	Fixed error message that referred to RSocket
//
//		08/08/97 MJR	Upgraded to latest revision of plugin stuff.
//
//		08/09/97 MJR	Added broadcast support.
//
//		08/14/97 MJR	Fixed GetAddress() to avoid database function when
//							working with a dotted address.
//
//		08/18/97 MJR	Fixed stupid error in GetAddress() that caused crashes.
//
//		08/20/97 MJR	Added support for setting whether socket blocks or not.
//
//		09/02/97 MJR	Got rid of TRACE() on "would block" situations.
//
//					MJR	Fixed serious bug that caused functions to return -1
//							as the number of actual bytes sent/received if an
//							error occurred -- they now properly return 0 instead.
//
//		09/09/97 MJR	Added RSocket::errNotSupported return value to Open().
//
//		11/18/97	JMI	Added message and Close() in destructor for case when
//							an opened socket is never closed.  Trying to find memory
//							leak but this was apparently not the problem.
//
//////////////////////////////////////////////////////////////////////////////
//
// WINSOCK NOTES
// -------------
//
//
// Windows NT WorkStation 4.0 only allow a maximum of 5 backlogs on listen().
// Earlier versions of NT were also limited to 5.  NT servers defaults to 200.
// No idea what Win95 does.
//
// Sockets allow for "stream" (TCP) and "datagram" (UDP).  TCP is referred to
// as "connected", while UDP is normally connectionless.
//
// You can use UDP in connected mode, which saves you from having to specify
// the destination for each send operation and also automatically discards any
// data received from anywhere other than that same destination.  This may be
// faster, depending on the WinSock implimentation.
//
// I suppose one important advantage of being "unconnected" is that you can
// send and receive to/from multiple remote systems, which may be more suitable
// if you were doing some sort of "star" network, where any system can talk to
// any other system.  It seems, though, that you could simulate this with TCP
// or connected UDP by using multiple ports, one per remote system.
//
// TCP is reliable, UDP is not.
//
// With TCP, data may be "coalesced" into larger packets before transmission,
// which is why you need to keep calling receive until you get the amount of
// data you were expecting.  (I also read that you might have to send until
// all the data has been sent, which may make some sense -- you send 100 bytes,
// but the system only needed 50 to "complete" the current data buffer, and so
// it sends only 50 of the 100 you gave it and returns, so you have to call it
// again to send the remaining 50).
//
// With UDP, data is NEVER coalesced, as it may be with TCP.  In both cases,
// data may be split up into multiple blocks based on the maximum transmission
// unit (MTU).  This is supposed to be transparent to the app, and it's obvious
// how it would be with TCP, but I don't see how it could be with UDP since
// it's supposedly unreliable.
//
// We can check the MTU at some point when the socket stuff hands back info
// about the selected protocol (can't remember exact spot, but it exists).
// Other documentation claims that there is no defined way to determine the
// MTU.
//
// We might want to consider the user of broadcasting on LANs.  We might also
// consider using UDP on LAN's (doing our own sequencing and resending of data)
// and TCP on WAN's.  This may work well for another reason, which is that
// I've read several times that TCP headers are usually compressed over PPP
// links, which most people use to connect to WAN's.
//
// PUSH Bit Interpretation
// By default, Windows NT versions 4.0 and 3.5x complete a recv() call when:
// �	Data arrives with the PUSH bit set.
// �	The user recv() buffer is full.
// �	0.5 seconds have elapsed since any data arrived.
// If a client program is run on a computer with a TCP/IP implementation that
// does not set the PUSH bit on sends, response delays may result.  It�s best
//  to correct this on the client side; however, a configuration parameter
// (IgnorePushBitOnReceives) is added to Afd.sys to force it to treat all
// arriving packets as though the PUSH bit were set. 
//
// Delayed Acknowledgments
// Per RFC 1122, TCP uses delayed acknowledgments to reduce the number of
// packets sent on the media. The Microsoft stack takes a common approach
// to implementing delayed acknowledgments. The following conditions cause
// an acknowledgment to be sent as data is received by TCP on a given
// connection:
// �	No ACK is sent for the previous received segment.
// �	Segment is received, and no other segment arrives within 200ms for that connection.
// In summary, normally an ACK is sent for every other TCP segment received
// on a connection, unless the delayed ACK timer (200ms) expires. There is no
// configuration parameter to disable delayed ACKs.
//
//
// THINGS TO CHANGE
// ----------------
//
// Do we need to impliment a checksum on UDP data?
//
// We need to check what type of command is being cancelled via the blocking
// hook.  If it's an acceptable command, there's no problem.  If not, then
// the socket becomes unusable.  It would be good if this could be done in some
// way as to be transparent to the app.  Then again, that might not make sense,
// because if certain operations are cancelled, it might make sense that the
// socket is unusable.  I'm no longer sure this is really necessary.  I think
// the general implication should be that any time a function fails, you can
// no longer use that socket, whether it failed because it was aborted or for
// some other reason.
//
// Check into how connections are currently being terminated.  Simply calling
// closesocket() sounds like it would work, but I'm not sure how the other end
// will react.  This needs to be worked out and implimented.
//
// Should we allow user to specify send and recieve buffer sizes, which some
// winsock implimentations support?
//
//////////////////////////////////////////////////////////////////////////////

#include "RSPiX.h"
#include "socket.h"


//////////////////////////////////////////////////////////////////////////////
// Macros
//////////////////////////////////////////////////////////////////////////////

// Small compat layer so the Winsock and BSD/POSIX paths can share one body.
// On Windows these expand to exactly what the original code did.
#ifdef WIN32
	// Set current (static) blocking callback from this socket's callback
	#define BSDIP_SET_CALLBACK()		(ms_callback = m_callback)
	// Record which socket function is about to be executed (for the blocking hook)
	#define BSDIP_FUNCNUM(f)			(ms_funcnum = (f))
	// Last socket error
	#define BSDIP_LAST_ERROR()			WSAGetLastError()
	// Whether a socket error means "would have blocked"
	#define BSDIP_IS_WOULDBLOCK(e)	((e) == WSAEWOULDBLOCK)
	// Flags passed to send()/sendto()
	#define BSDIP_SEND_FLAGS			0
#else
	// There's no blocking-hook machinery on BSD sockets: the callback is stored
	// (so Get/SetCallback() still work) but never called.
	#define BSDIP_SET_CALLBACK()		((void)0)
	#define BSDIP_FUNCNUM(f)			((void)0)
	#define BSDIP_LAST_ERROR()			(errno)
	// EINTR is treated like "would block" so callers simply retry, which is what
	// they already do for errWouldBlock.
	#define BSDIP_IS_WOULDBLOCK(e)	(((e) == EWOULDBLOCK) || ((e) == EAGAIN) || ((e) == EINTR))
	// Don't raise SIGPIPE when sending on a connection the peer has closed; we
	// want an EPIPE error instead.  (Apple platforms use SO_NOSIGPIPE, which is
	// set per-socket in Open()/Accept().)
	#ifdef MSG_NOSIGNAL
		#define BSDIP_SEND_FLAGS		MSG_NOSIGNAL
	#else
		#define BSDIP_SEND_FLAGS		0
	#endif

	// Poll a single socket with a zero timeout.  Returns the revents mask (0 if
	// nothing is ready) or -1 on error.  poll() is used rather than select()
	// because it has no FD_SETSIZE limit on the descriptor value and it reports
	// POLLERR, which is what IsError() and the connect() poll need.
	static int BSDIP_PollSocket(SOCKET sock, short events)
		{
		struct pollfd pfd;
		pfd.fd = sock;
		pfd.events = events;
		pfd.revents = 0;
		int iCount;
		do
			{
			iCount = poll(&pfd, 1, 0);
			} while ((iCount < 0) && (errno == EINTR));
		if (iCount < 0)
			return -1;
		return (iCount > 0) ? (int)pfd.revents : 0;
		}

	// Set O_NONBLOCK on a descriptor
	static int BSDIP_SetNonBlocking(SOCKET sock)
		{
		int iFlags = fcntl(sock, F_GETFL, 0);
		if (iFlags == -1)
			return -1;
		return fcntl(sock, F_SETFL, iFlags | O_NONBLOCK);
		}

	// Disable SIGPIPE on this socket where the platform supports doing so per
	// socket (Apple).  Elsewhere MSG_NOSIGNAL on send covers it.
	static void BSDIP_NoSigPipe(SOCKET sock)
		{
		#ifdef SO_NOSIGPIPE
		int optval = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
			TRACE("RProtocolBSDIP: Error setting SO_NOSIGPIPE: %s\n", strerror(errno));
		#else
		(void)sock;
		#endif
		}

	// Equivalent of Winsock's SO_DONTLINGER=1: close() returns immediately and
	// the stack still tries to deliver unsent data (graceful close in the
	// background).  This is l_onoff = 0, which is also the BSD default.
	static int BSDIP_DontLinger(SOCKET sock)
		{
		LINGER lin;
		lin.l_onoff = 0;
		lin.l_linger = 0;
		return setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&lin, sizeof(lin));
		}
#endif

//////////////////////////////////////////////////////////////////////////////
// Static data
//////////////////////////////////////////////////////////////////////////////

WSADATA							RProtocolBSDIP::ms_WSAData;
bool								RProtocolBSDIP::ms_bDidStartup;
bool								RProtocolBSDIP::ms_bWSAStartup;
#ifdef WIN32
bool								RProtocolBSDIP::ms_bWSASetBlockingHook;
RSocket::FuncNum				RProtocolBSDIP::ms_funcnum;
RSocket::BLOCK_CALLBACK		RProtocolBSDIP::ms_callback;
#endif

//////////////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// Constructor
//////////////////////////////////////////////////////////////////////////////
RProtocolBSDIP::RProtocolBSDIP(void)
	{
	Init();
	}


//////////////////////////////////////////////////////////////////////////////
// Destructor
//////////////////////////////////////////////////////////////////////////////
RProtocolBSDIP::~RProtocolBSDIP(void)
	{
	if (m_sock != INVALID_SOCKET)
		{
		TRACE("~RProtocolBSDIP(): Closing socket you forgot to, hoser!\n");

		Close();
		}
	}


//////////////////////////////////////////////////////////////////////////////
// Reset
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Reset(void)
	{
	Close();
	Init();
	RProtocol::Reset();
	}


//////////////////////////////////////////////////////////////////////////////
// Init
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Init(void)
	{
	m_sock = INVALID_SOCKET;
	RProtocol::Init();
	}


//////////////////////////////////////////////////////////////////////////////
//	Startup socket API.  This is normally called from the socket's Startup
// function.  Since we have multiple protocols using the same winsock
// code, the Startup and Shutdown will have to be called after creating
//	an RSocket and before calling Open()
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Startup(void)
	{
	int16_t sResult = 0;
	
	// Only do this once, but multiple calls are not considered an error
	if (!ms_bDidStartup)
		{
		// Startup winsock API.  If this fails, the typical call the WSGetLastError() is
		// not supported, so there's no way to get a specific error code.
		if (WSAStartup(MAKEWORD(1,1), &ms_WSAData) == 0)
			{
			ms_bWSAStartup = true;
			
			#ifdef WIN32
			// Confirm that this winsock implimentation supports 1.1.  If it supports
			// version greater than 1.1 in addition to 1.1, it will still return 1.1
			// since that's what we inquired about.
			if (LOBYTE(ms_WSAData.wVersion) == 1 && HIBYTE(ms_WSAData.wVersion) == 1)
				{
				// Set static hook to 0 as default
				ms_callback = 0;
				
				// Set our blocking hook function
				if (WSASetBlockingHook(RProtocolBSDIP::BlockingHook) != NULL)
					{
					ms_bWSASetBlockingHook = true;
					
					// success!
					ms_bDidStartup = true;
					
					// Set protocol
					ms_prototype = RSocket::TCPIP;
					}
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Hook(): Error returned by WSASetBlockingHook(): %ld\n", WSAGetLastError());
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Startup(): Incorrect version of WinSock DLL!\n");
				}
			#else
			// BSD sockets need no initialization and have no blocking hook.
			#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
			// No per-call or per-socket way to suppress SIGPIPE on this platform, so
			// ignore it process-wide; send() will then fail with EPIPE instead.
			signal(SIGPIPE, SIG_IGN);
			#endif

			// success!
			ms_bDidStartup = true;

			// Set protocol
			ms_prototype = RSocket::TCPIP;
			#endif
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Startup(): Incorrect version of WinSock DLL or can't find WinSock DLL!\n");
			}
		
		// If there's an error, clean up after ourselves
		if (sResult)
			Shutdown();
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
//	Shutdown socket API.  This is normally called from the socket's Startup
// function.  Since we have multiple protocols using the same winsock
// code, the Startup and Shutdown will have to be called after creating
//	an RSocket and before calling Open()
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::Shutdown(void)
	{
	#ifdef WIN32
	if (ms_bWSASetBlockingHook)
		{
		// Remove blocking hook
		if (WSAUnhookBlockingHook() == SOCKET_ERROR)
			TRACE("RProtocolBSDIP::Unhook(): Error returned by WSAUnhookBlockingHook(): %ld\n", WSAGetLastError());
		
		ms_bWSASetBlockingHook = false;
		}
	#endif

	if (ms_bWSAStartup)
		{
		// Cleanup winsock API
		if (WSACleanup() == SOCKET_ERROR)
			TRACE("RProtocolBSDIP::Shutdown(): Error returned by WSACleanup(): %ld\n", WSAGetLastError());
		
		ms_bWSAStartup = false;
		}
	
	// Set protocol
	ms_prototype = RSocket::NO_PROTOCOL;
	
	ms_bDidStartup = false;
	}


//////////////////////////////////////////////////////////////////////////////
// Open socket
// A return value of RSocket::errNotSupported means this protocol is
// not supported.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Open(							// Returns 0 if successful, non-zero otherwise
	uint16_t usPort,								// In:  Port number or 0 for any port
	int16_t sType,											// In:  Any one RSocket::typ* enum
	int16_t sOptionFlags,									// In:  Any combo of RSocket::opt* enums
	RSocket::BLOCK_CALLBACK callback /*NULL */)	// In:  Blocking callback (or NULL to keep current)
	{
	int16_t sResult = 0;
	
	// Make sure startup was called.  Only this function needs to do this
	// because all the others check for a valid socket, which can only be
	// created via this function.
	if (ms_bDidStartup)
		{
		// Make sure socket isn't already open
		if (m_sock == INVALID_SOCKET)
			{
			// If callback was specified, use it.
			// (On BSD sockets it is only stored, never called.)
			if (callback != NULL)
				m_callback = callback;
			
			// Set current callback
			BSDIP_SET_CALLBACK();

			// Save type
			m_sType = sType;
			
			// Select socket type
			int iType;
			if (sType == RSocket::typStream)
				iType = SOCK_STREAM;
			else if (sType == RSocket::typDatagram)
				iType = SOCK_DGRAM;
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Open(): Invalid type specified!\n");
				}
			if (sResult == 0)
				{

				// Create socket
				BSDIP_FUNCNUM(RSocket::OtherFunc);

				m_sock = socket(AF_INET, iType, 0);
				if (m_sock != INVALID_SOCKET)
					{
					// Set defaults
					m_bListening = false;
					m_bConnected = false;
					m_bConnecting = false;

					#if PLATFORM_UNIX
					// Get EPIPE instead of SIGPIPE when writing to a closed connection
					BSDIP_NoSigPipe(m_sock);
					#endif
					
					// Check if they want blocking turned off
					if (sOptionFlags & RSocket::optDontBlock)
						{
						#if PLATFORM_UNIX
						if (BSDIP_SetNonBlocking(m_sock) == SOCKET_ERROR)
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error setting non-blocking mode, error returned by fcntl(): %s\n", strerror(errno));
							}
						#else
						u_long ulEnableNonBlockingMode = 1;
						if (ioctlsocket(m_sock, FIONBIO, &ulEnableNonBlockingMode) == SOCKET_ERROR)
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error setting non-blocking mode, error returned by ioctlsocket(): %ld\n", WSAGetLastError());
							}
						#endif
						}

					// Check if they want coalescing turned off
					if ((sOptionFlags & RSocket::optDontCoalesce) && !sResult)
						{
						if (sType == RSocket::typStream)
							{
							int optval = 1;
							if (setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
								{
								sResult = -1;
								TRACE("RProtocolBSDIP::Open(): Error setting TCP_NODELAY, error returned by setsockopt(): %ld\n", (long)BSDIP_LAST_ERROR());
								}
							}
						else
							{
							sResult = -1;
							TRACE("RPRotocolBSDIP::Open(): optDontCoalesce is only valid for typStream!\n");
							}
						}

					// Check if they want lingering turned off
					if ((sOptionFlags & RSocket::optDontWaitOnClose) && !sResult)
						{
						if (sType == RSocket::typStream)
							{
							#if PLATFORM_UNIX
							if (BSDIP_DontLinger(m_sock) == SOCKET_ERROR)
							#else
							int optval = 1;
							if (setsockopt(m_sock, SOL_SOCKET, SO_DONTLINGER, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
							#endif
								{
								sResult = -1;
								TRACE("RProtocolBSDIP::Open(): Error setting SO_DONTLINGER, error returned by setsockopt(): %ld\n", (long)BSDIP_LAST_ERROR());
								}
							}
						else
							{
							sResult = -1;
							TRACE("RPRotocolBSDIP::Open(): optDontWaitOnClose is only valid for typStream!\n");
							}
						}

					#if PLATFORM_UNIX
					// Let a listening port be re-bound right away after a restart even
					// if old connections are still in TIME_WAIT (Winsock allows that
					// without SO_REUSEADDR; BSD stacks don't).  Only for streams: on
					// datagram sockets it would let two processes share a UDP port.
					if ((sType == RSocket::typStream) && !sResult)
						{
						int optval = 1;
						if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
							TRACE("RProtocolBSDIP::Open(): Error setting SO_REUSEADDR (ignored): %s\n", strerror(errno));
						}
					#endif
					
					if (sResult == 0)
						{
						// Setup local address (any address is fine)
						SOCKADDR_IN addr;
						#if PLATFORM_UNIX
						memset(&addr, 0, sizeof(addr));
						#endif
						addr.sin_family = AF_INET;
						addr.sin_port = htons(usPort);
						addr.sin_addr.s_addr = INADDR_ANY;

						// Bind socket to local address
						if (bind(m_sock, (SOCKADDR*) &addr, sizeof(addr)) != SOCKET_ERROR)
							{
							// success
							}
						else
							{
							sResult = -1;
							TRACE("RProtocolBSDIP::Open(): Error returned by bind(): %ld\n", (long)BSDIP_LAST_ERROR());
							}
						}

					// If anything went wrong, close the socket
					if (sResult)
						Close();
					}
				else
					{
					int iErr = BSDIP_LAST_ERROR();
					#if PLATFORM_UNIX
					if ((iErr == EAFNOSUPPORT) || (iErr == EPROTONOSUPPORT))
					#else
					if (iErr == WSAEAFNOSUPPORT)
					#endif
						sResult = RSocket::errNotSupported;
					else
						sResult = -1;
					TRACE("RProtocolBSDIP::Open(): Error returned by socket(): %ld\n", (long)iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Open(): Socket already open!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Open(): Didn't call WSAStartup()!\n");
		}

	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Close socket
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Close(							// Returns 0 if successfull, non-zero otherwise
	bool bForceNow /*= true */)						// In:  'true' means do it now, false follows normal rules
	{
	int16_t sResult = 0;

	// Only close it if it's open, but don't consider it an error if it isn't
	if (m_sock != INVALID_SOCKET)
		{
		// Set current callback
		BSDIP_SET_CALLBACK();
		
		// See if they want to force this to happen RIGHT NOW
		if (bForceNow && (m_sType == RSocket::typStream))
			{
			// Set the "don't linger" option, which will cause the closesocket()
			// to return immediately, but the underlying socket implimentation will
			// attempt to send all unsent data before it actually closes the connection.
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			#if PLATFORM_UNIX
			if (BSDIP_DontLinger(m_sock) == SOCKET_ERROR)
			#else
			int optval = 1;
			if (setsockopt(m_sock, SOL_SOCKET, SO_DONTLINGER, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
			#endif
				{
				#if PLATFORM_UNIX
				// On BSD sockets close() with l_onoff == 0 never blocks anyway, so a
				// failure here (e.g. EINVAL on a socket the peer already reset) is not
				// a reason to leak the descriptor.  Carry on and close it.
				TRACE("RProtocolBSDIP::Close(): Error setting SO_LINGER (ignored): %s\n", strerror(errno));
				#else
				sResult = -1;
				TRACE("RProtocolBSDIP::Close(): Error setting SO_DONTLINGER, error returned by setsockopt(): %ld\n", WSAGetLastError());
				#endif
				}
			}

		// If an error occurred, we can't do the close because it might block, which
		// is presumably the whole reason the caller asked for us to turn of lingering
		if (sResult == 0)
			{

			// Close socket
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			#if PLATFORM_UNIX
			// POSIX: the descriptor is released even if close() reports an error
			// (including EINTR on Linux), so never retry it -- the number may
			// already belong to someone else.
			if (closesocket(m_sock) == SOCKET_ERROR)
				{
				int iErr = errno;
				if (iErr != EINTR)
					TRACE("RProtocolBSDIP::Close(): Error returned by close() (descriptor released anyway): %s\n", strerror(iErr));
				}
			m_bListening = false;
			m_bConnected = false;
			m_bConnecting = false;
			m_sock = INVALID_SOCKET;
			#else
			if (closesocket(m_sock) != SOCKET_ERROR)
				{
				// Reset flags
				m_bListening = false;
				m_bConnected = false;
				m_bConnecting = false;
				
				// Mark it as invalid
				m_sock = INVALID_SOCKET;
				}
			else
				{
				int iErr = WSAGetLastError();
				if (iErr == WSAEWOULDBLOCK)
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Close(): Error returned by closesocket(): %ld\n", iErr);
					}
				}
			#endif
			}
		}
	
	return sResult;
	}


////////////////////////////////////////////////////////////////////////////////
// Set socket to broadcast mode
////////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Broadcast(void)				// Returns 0 if successfull, non-zero otherwise
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			int optval = 1;
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			if (setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Broadcast(): Error setting SO_BROADCAST, error returned by setsockopt(): %ld\n", (long)BSDIP_LAST_ERROR());
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Broadcast(): Socket is already listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Broadcast(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Set socket to listen for connection requests
//
// Current winsock implimentations support a maximum of 5 queued connectons.
// Requesting more than 5 will cause this function to return an error.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Listen(							// Returns 0 if successfull, non-zero otherwise
	int16_t sMaxQueued /* = 5*/)							// In:  Maximum number of queued connection requests
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if ((sMaxQueued >= 1) && (sMaxQueued <= 5))
				{
				// Set current callback
				BSDIP_SET_CALLBACK();
				
				// Current winsock implimentations are silently hardwired to support
				// no more than 5 queued connections.  They will NOT complain if you
				// set a higher number, but instead will silently set their internal
				// value to the max value (5).  Note, however, that it could be less
				// than 5, just not greater than 5.
				BSDIP_FUNCNUM(RSocket::OtherFunc);
				if (listen(m_sock, (int)sMaxQueued) != SOCKET_ERROR)
					{
					// Socket is listening
					m_bListening = true;
					}
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::Listen(): Error returned by listen(): %ld\n", (long)BSDIP_LAST_ERROR());
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Listen(): Max queued connections must be between 1 and 5!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Listen(): Socket is already listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Listen(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Accept request for connection
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Accept(						// Returns 0 on success, non-zero otherwise
	RSocket::RProtocol* pProtocolClient,			// I/O: Client protocol
	RSocket::Address* paddressClient)				// Out: Client address
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			// Make sure client socket isn't already in use
			if (((RProtocolBSDIP*) pProtocolClient)->m_sock == INVALID_SOCKET)
				{
				// Client address will be filled in here.  Length must be set to available bytes
				// before calling accept().
				AddressIP addressClient;
				addressClient.prototype = RSocket::TCPIP;
				addressClient.lAddressLen = sizeof(addressClient.address);
				
				// Accept a client connection.  If no client is pending, then this will block.
				// Upon return, the specified address is filled in, and the specified length
				// is set to the actual length of the address (in bytes).
				BSDIP_FUNCNUM(RSocket::AcceptFunc);
				#if PLATFORM_UNIX
				memset(&addressClient.address, 0, sizeof(addressClient.address));
				SOCKLEN addrlen = (SOCKLEN)sizeof(addressClient.address);
				((RProtocolBSDIP*)pProtocolClient)->m_sock = accept(m_sock, (SOCKADDR*)&(addressClient.address), &addrlen);
				addressClient.lAddressLen = (int32_t)addrlen;
				#else
				((RProtocolBSDIP*)pProtocolClient)->m_sock = accept(m_sock, (SOCKADDR*)&(addressClient.address), (int*)(&addressClient.lAddressLen));
				#endif
				if (((RProtocolBSDIP*)pProtocolClient)->m_sock != INVALID_SOCKET)
					{
					#if PLATFORM_UNIX
					SOCKET sockClient = ((RProtocolBSDIP*)pProtocolClient)->m_sock;

					// The accepted socket is always a stream (Winsock code leaves this
					// unset, which makes Close(true) read garbage).
					((RProtocolBSDIP*)pProtocolClient)->m_sType = RSocket::typStream;

					// Winsock hands back an accepted socket with the same properties
					// as the listening socket (non-blocking, TCP_NODELAY, linger).
					// Linux does not inherit O_NONBLOCK (BSD/Apple does), and option
					// inheritance in general varies, so copy them explicitly.
					int iListenFlags = fcntl(m_sock, F_GETFL, 0);
					if ((iListenFlags != -1) && (iListenFlags & O_NONBLOCK))
						{
						if (BSDIP_SetNonBlocking(sockClient) == SOCKET_ERROR)
							TRACE("RProtocolBSDIP::AcceptClient(): Error setting non-blocking mode on client: %s\n", strerror(errno));
						}
					int optval = 0;
					SOCKLEN optlen = (SOCKLEN)sizeof(optval);
					if ((getsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, &optlen) == 0) && optval)
						{
						optval = 1;
						if (setsockopt(sockClient, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
							TRACE("RProtocolBSDIP::AcceptClient(): Error setting TCP_NODELAY on client: %s\n", strerror(errno));
						}
					LINGER lin;
					SOCKLEN linlen = (SOCKLEN)sizeof(lin);
					if (getsockopt(m_sock, SOL_SOCKET, SO_LINGER, (char*)&lin, &linlen) == 0)
						setsockopt(sockClient, SOL_SOCKET, SO_LINGER, (char*)&lin, sizeof(lin));
					BSDIP_NoSigPipe(sockClient);
					#endif
					
					// Client socket is connected
					pProtocolClient->m_bConnected = true;
					pProtocolClient->m_bConnecting = false;

					// If caller requested client address, return it now
					if (paddressClient != NULL)
						{
						// A memcpy is perfectly safe since these structs are interchangable
						memcpy(paddressClient, &addressClient, sizeof(RSocket::Address));
						}
					}
				else
					{
					int iErr = BSDIP_LAST_ERROR();
					#if PLATFORM_UNIX
					// A connection that was reset while still queued is reported by
					// accept() on BSD sockets; Winsock just keeps waiting.  Report it
					// as "would block" so the caller simply polls again.
					if (BSDIP_IS_WOULDBLOCK(iErr) || (iErr == ECONNABORTED))
					#else
					if (iErr == WSAEWOULDBLOCK)
					#endif
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::AcceptClient(): Error returned by accept(): %ld\n", (long)iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::AcceptClient(): Specified client socket is already in use!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::AcceptClient(): Socket is not listening!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::AcceptClient(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Connect to address
//
// If the RSocket::optDontBlock option was set on this socket, then this
// function may return RSocket::errWouldBlock, which indicates that it is
// still trying to connect, but has not yet managed to do so.  In that case,
// this function should be called repeatedly (polled) until it returns either
// an actual error message other than RSocket::errWouldBlock, which would
// indicate that the connection attempt has failed, or 0, which indicates
// that it has actually connected successfully.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Connect(						// Returns 0 if successfull, non-zero otherwise
	RSocket::Address* paddress)						// In:  Remote address to connect to
	{
	int16_t sResult = 0;

	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
				
			// Make sure socket is not already trying to connect
			if (!m_bConnecting)
				{

				// Try to connect to remote socket
				BSDIP_FUNCNUM(RSocket::OtherFunc);
				if (connect(m_sock, (SOCKADDR*)&(paddress->address), sizeof(SOCKADDR_IN)) != SOCKET_ERROR)
					{
					// Connected!
					m_bConnecting = false;
					m_bConnected = true;
					}
				else
					{
					int iErr = BSDIP_LAST_ERROR();
					#if PLATFORM_UNIX
					// Non-blocking connect() in progress is EINPROGRESS on BSD sockets
					// (EWOULDBLOCK on Winsock).  EINTR on a blocking socket also leaves
					// the connect running asynchronously, and EALREADY means a previous
					// attempt is still running.  All of these are polled below.
					if ((iErr == EINPROGRESS) || (iErr == EALREADY) || BSDIP_IS_WOULDBLOCK(iErr))
						{
						sResult = RSocket::errWouldBlock;
						m_bConnected = false;
						m_bConnecting = true;
						}
					else if (iErr == EISCONN)
						{
						// Already connected
						m_bConnecting = false;
						m_bConnected = true;
						}
					#else
					if (iErr == WSAEWOULDBLOCK)
						{
						// WSAEWOULDBLOCK indicates that connect() would have blocked.  Unlike other
						// winsock functions, it will magically continue trying to connect!!!
						sResult = RSocket::errWouldBlock;
						m_bConnected = false;
						m_bConnecting = true;
						}
					#endif
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Connect(): Error returned by connect(): %ld\n", (long)iErr);
						}
					}
				}
			else
				{
				// The socket is trying to connect, so we check whether it succeeded or failed
				#if PLATFORM_UNIX
				// BSD sockets report completion of a non-blocking connect by making the
				// socket writable; success vs. failure is then read from SO_ERROR.
				// (Winsock instead flags failure via the "exception" set.)
				int iRevents = BSDIP_PollSocket(m_sock, POLLOUT);
				int iSoError = 0;
				if (iRevents > 0)
					{
					SOCKLEN optlen = (SOCKLEN)sizeof(iSoError);
					if (getsockopt(m_sock, SOL_SOCKET, SO_ERROR, (char*)&iSoError, &optlen) == SOCKET_ERROR)
						iSoError = errno;
					}
				if ((iRevents < 0) || (iSoError != 0) || ((iRevents > 0) && !(iRevents & POLLOUT)))
					{
					// Connection attempt has failed
					m_bConnecting = false;
					m_bConnected = false;
					sResult = -1;
					TRACE("RProtocolBSDIP::Connect(): Non-blocking connection attempt has failed: %s\n", strerror(iSoError ? iSoError : errno));
					}
				else if (iRevents > 0)
					{
					// Connected!
					m_bConnecting = false;
					m_bConnected = true;
					}
				else
					{
					// Return the "would block" error, which means we're still trying to connect
					sResult = RSocket::errWouldBlock;
					}
				#else

				// Create a set containing just this socket
				FD_SET set;
				FD_ZERO(&set);
				FD_SET(m_sock, &set);

				// Set time value to 0, which makes this into a polling operation
				TIMEVAL timeval = { 0, 0 };

				// Select exception/error sockets
				ms_funcnum = RSocket::SelectFunc;
				int32_t lCount = select(0, NULL, NULL, &set, &timeval);
				if (lCount == 0)
					{
					// Create a set containing just this socket
					FD_SET set;
					FD_ZERO(&set);
					FD_SET(m_sock, &set);

					// Set time value to 0, which makes this into a polling operation
					TIMEVAL timeval = { 0, 0 };

					// Select writable sockets
					ms_funcnum = RSocket::SelectFunc;
					int32_t lCount = select(0, NULL, &set, NULL, &timeval);
					if (lCount > 0)
						{
						// Connected!
						m_bConnecting = false;
						m_bConnected = true;
						}
					else
						{
						// Return the "would block" error, which means we're still trying to connect
						sResult = RSocket::errWouldBlock;
						}
					}
				else
					{
					// Connection attempt has failed
					m_bConnecting = false;
					m_bConnected = false;
					sResult = -1;
					TRACE("RProtocolBSDIP::Connect(): Non-blocking connection attempt has failed!\n");
					}
				#endif
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Connect(): Socket is listening -- can't connect!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Connect(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Send data to connected socket
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Send(							// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lNumBytes,										// In:  Number of bytes to send
	int32_t* plActualBytes)									// Out: Actual number of bytes sent
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if (m_bConnected)
				{
				// Set current callback
				BSDIP_SET_CALLBACK();
				
				// Send data
				BSDIP_FUNCNUM(RSocket::OtherFunc);
				*plActualBytes = (int32_t)send(m_sock, (char*)pBuf, lNumBytes, BSDIP_SEND_FLAGS);
				if (*plActualBytes == SOCKET_ERROR)
					{
					*plActualBytes = 0;
					int iErr = BSDIP_LAST_ERROR();
					if (BSDIP_IS_WOULDBLOCK(iErr))
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Send(): Error returned by send(): %ld\n", (long)iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Send(): Socket not connected!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::Send(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Send(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Send data to specified address
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::SendTo(							// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lNumBytes,										// In:  Number of bytes to send
	int32_t* plActualBytes,									// Out: Actual number of bytes sent
	RSocket::Address* paddress)						// In:  Address to send to
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			// Send data
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			*plActualBytes = (int32_t)sendto(m_sock, (char*)pBuf, lNumBytes, BSDIP_SEND_FLAGS, (SOCKADDR*)&(paddress->address), paddress->lAddressLen);
			if (*plActualBytes == SOCKET_ERROR)
				{
				*plActualBytes = 0;
				int iErr = BSDIP_LAST_ERROR();
				if (BSDIP_IS_WOULDBLOCK(iErr))
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::SendTo(): Error returned by sendto(): %ld\n", (long)iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::SendTo(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::SendTo(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Receive data - only valid with connected sockets
//
// For datagram (UDP) sockets, there is a one-to-one correspondence between
// sends and receives.  Each send implies a matching receive.  The specified
// buffer must at least as large as the amount of data that was sent, or the
// data will be truncated and an error will be returned.
//
// For stream (TCP) sockets, there is no direct correspondence between sends
// and receive.  One send can be broken up and require multiple receives, and
// and multiple sends can be coalesced into a single recieve.  There is no
// limitation on the amount of data being received.
//
// For stream (TCP) sockets, if the actual number of bytes received is 0, it
// means the other end disconnected gracefully.
//
// In all cases, if the connection was abortively disconnected, an error will
// be returned.
//
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::Receive(						// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lMaxBytes,										// In:  Maximum number of bytes that fit in the buffer
	int32_t* plActualBytes)									// Out: Actual number of bytes recieved into the buffer
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			if (m_bConnected)
				{
				// Set current callback
				BSDIP_SET_CALLBACK();
				
				// Receive data
				BSDIP_FUNCNUM(RSocket::OtherFunc);
				*plActualBytes = (int32_t)recv(m_sock, (char*)pBuf, lMaxBytes, 0);
				if (*plActualBytes == SOCKET_ERROR)
					{
					*plActualBytes = 0;
					int iErr = BSDIP_LAST_ERROR();
					if (BSDIP_IS_WOULDBLOCK(iErr))
						sResult = RSocket::errWouldBlock;
					else
						{
						sResult = -1;
						TRACE("RProtocolBSDIP::Receive(): Error returned by recv(): %ld\n", (long)iErr);
						}
					}
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::Receive(): Socket not connected!\n");
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtcolWinTcpip::Receive(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::Receive(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Receive data from the given address
// NOTE: If an error occurs, plActualBytes will return 0.
//////////////////////////////////////////////////////////////////////////////
int16_t RProtocolBSDIP::ReceiveFrom(					// Returns 0 on success, non-zero otherwise
	void* pBuf,												// In:  Pointer to data buffer
	int32_t lMaxBytes,										// In:  Maximum bytes that can fit in the buffer
	int32_t* plActualBytes,									// Out: Actual number of bytes recieved into buffer
	RSocket::Address* paddress)						// Out: Source address returned here
	{
	int16_t sResult = 0;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			// Source address
			AddressIP addr;
			addr.prototype = RSocket::TCPIP;
			addr.lAddressLen = sizeof(addr.address);
			
			// Receive data
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			#if PLATFORM_UNIX
			memset(&addr.address, 0, sizeof(addr.address));
			SOCKLEN addrlen = (SOCKLEN)sizeof(addr.address);
			*plActualBytes = (int32_t)recvfrom(m_sock, (char*)pBuf, lMaxBytes, 0, (SOCKADDR*)&(addr.address), &addrlen);
			addr.lAddressLen = (int32_t)addrlen;
			#else
			*plActualBytes = recvfrom(m_sock, (char*)pBuf, lMaxBytes, 0, (SOCKADDR*)&(addr.address), (int*)(&addr.lAddressLen));
			#endif
			if (*plActualBytes != SOCKET_ERROR)
				{
				// If caller requested source address, return it now
				if (paddress != NULL)
					{
					// A memcpy is perfectly safe since these structs are interchangable
					memcpy(paddress, &addr, sizeof(RSocket::Address));
					}
				}
			else
				{
				*plActualBytes = 0;
				int iErr = BSDIP_LAST_ERROR();
				if (BSDIP_IS_WOULDBLOCK(iErr))
					sResult = RSocket::errWouldBlock;
				else
					{
					sResult = -1;
					TRACE("RProtocolBSDIP::ReceiveFrom(): Error returned by recvfrom(): %ld\n", (long)iErr);
					}
				}
			}
		else
			{
			sResult = -1;
			TRACE("RProtocolBSDIP::ReceiveFrom(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::ReceiveFrom(): Socket is not open!\n");
		}
	
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if connection can be accepted without blocking
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanAcceptWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			#if PLATFORM_UNIX
			// A listening socket becomes readable when a connection is pending
			if (BSDIP_PollSocket(m_sock, POLLIN) > 0)
				bResult = true;
			#else
			// Create a set containing just this socket
			FD_SET set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			// Set time value to 0, which makes this into a polling operation
			TIMEVAL timeval = { 0, 0 };
			
			// Select listening sockets that have actual connections pending
			ms_funcnum = RSocket::SelectFunc;
			int32_t lCount = select(0, &set, NULL, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			#endif
			}
		else
			{
			//sResult = -1;
			TRACE("RProtocolBSDIP::CanAcceptWithoutBlocking(): Socket is not listening!\n");
			}
		}
	else
		{
		//sResult = -1;
		TRACE("RProtocolBSDIP::CanAcceptWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if socket can send without blocking.  There is no way of knowing
// how long this will be true, especially in a multi-threading environment
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanSendWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			#if PLATFORM_UNIX
			// Same as select()'s writable set: also true on error/hangup, in which
			// case the following Send() reports the error.
			if (BSDIP_PollSocket(m_sock, POLLOUT) > 0)
				bResult = true;
			#else
			// Create a set containing just this socket
			FD_SET set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			// Set time value to 0, which makes this into a polling operation
			TIMEVAL timeval = { 0, 0 };
			
			// Select writable sockets
			ms_funcnum = RSocket::SelectFunc;
			int32_t lCount = select(0, NULL, &set, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			#endif
			}
		else
			{
			//sResult = -1;
			TRACE("RProtocolBSDIP::CanSendWithoutBlocking(): Socket is listening -- can't send data!\n");
			}
		}
	else
		{
		//sResult = -1;
		TRACE("RProtocolBSDIP::CanSendWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check if socket can be read without blocking.  For datagrams 'true' means 
// queued data is available.  For streams 'true' means the connection has been
// closed.  If it was grecefully closed, the next read will return 0 bytes read.
// If it was aborted, the next read will return with an error.
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::CanReceiveWithoutBlocking(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			#if PLATFORM_UNIX
			// Same as select()'s readable set: data, EOF, or error/hangup
			if (BSDIP_PollSocket(m_sock, POLLIN) > 0)
				bResult = true;
			#else
			// Create a set containing just this socket
			FD_SET set;
			FD_ZERO(&set);
			FD_SET(m_sock, &set);
			
			// Set time value to 0, which makes this into a polling operation
			TIMEVAL timeval = { 0, 0 };
			
			// Select readable sockets
			ms_funcnum = RSocket::SelectFunc;
			int32_t lCount = select(0, &set, NULL, NULL, &timeval);
			if (lCount > 0)
				bResult = true;
			#endif
			}
		else
			{
			//sResult = -1;
			TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		//sResult = -1;
		TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is not open!\n");
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Check the number of bytes of data receivable without blocking.  For
// datagrams this returns the size of the next queued datagram.  For streams
// this returns the total amount of data that can be read with a single
// Receive() which is normally equal to the total amount of queued data.
//////////////////////////////////////////////////////////////////////////////
int32_t RProtocolBSDIP::CheckReceivableBytes(void)
	{
	#if PLATFORM_UNIX
	int lResult = 0;		// FIONREAD takes an int* on BSD sockets
	#else
	u_long lResult = 0;
	#endif
	
	if (m_sock != INVALID_SOCKET)
		{
		if (!m_bListening)
			{
			// Set current callback
			BSDIP_SET_CALLBACK();
			
			// Get amount of data available for reading
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			if (ioctlsocket(m_sock, FIONREAD, &lResult) == SOCKET_ERROR)
				{
				lResult = 0;
				TRACE("RProtocolBSDIP::GetAvailableForReceive(): Error returned by ioctrlsocket(): %ld\n", (long)BSDIP_LAST_ERROR());
				}
			}
		else
			{
			//sResult = -1;
			TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is listening -- can't receive data!\n");
			}
		}
	else
		{
		//sResult = -1;
		TRACE("RProtocolBSDIP::CanReceiveWithoutBlocking(): Socket is not open!\n");
		}
	
	return lResult;
	}


//////////////////////////////////////////////////////////////////////////////
// check status of protocol and this socket.
//////////////////////////////////////////////////////////////////////////////
bool RProtocolBSDIP::IsError(void)
	{
	bool bResult = false;
	
	if (m_sock != INVALID_SOCKET)
		{
		// Set current callback
		BSDIP_SET_CALLBACK();
		
		#if PLATFORM_UNIX
		// Winsock's "exception" set reports a failed non-blocking connect (and
		// OOB data, which we never use).  The BSD equivalent of a pending socket
		// error is POLLERR, which poll() always reports (events == 0) and which,
		// unlike reading SO_ERROR, doesn't clear the error.
		int iRevents = BSDIP_PollSocket(m_sock, 0);
		if ((iRevents < 0) || (iRevents & (POLLERR | POLLNVAL)))
			bResult = true;
		#else
		// Create a set containing just this socket
		FD_SET set;
		FD_ZERO(&set);
		FD_SET(m_sock, &set);
		
		// Set time value to 0, which makes this into a polling operation
		TIMEVAL timeval = { 0, 0 };
		
		// Select exception/error sockets
		ms_funcnum = RSocket::SelectFunc;
		int32_t lCount = select(0, NULL, NULL, &set, &timeval);
		if (lCount > 0)
			bResult = true;
		#endif
		}
	
	return bResult;
	}


//////////////////////////////////////////////////////////////////////////////
// SetCallback
//////////////////////////////////////////////////////////////////////////////
void RProtocolBSDIP::SetCallback(
	RSocket::BLOCK_CALLBACK callback)
	{
	m_callback = callback;
	}


//////////////////////////////////////////////////////////////////////////////
// GetCallback
//////////////////////////////////////////////////////////////////////////////
RSocket::BLOCK_CALLBACK RProtocolBSDIP::GetCallback(void)
	{
	return m_callback;
	}


//////////////////////////////////////////////////////////////////////////////
// GetMaxDatagramSize
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetMaxDatagramSize(			// Returns zero on success, non-zero otherwise
	int32_t* plSize)											// Out: Maximum datagram size in bytes
	{
	int16_t sResult = 0;
	
	if (ms_bDidStartup)
		{
		#ifdef WIN32
		*plSize = (int32_t)ms_WSAData.iMaxUdpDg;
		#else
		// No API for this on BSD sockets; use the IPv4 UDP payload limit
		// (65535 - 20 byte IP header - 8 byte UDP header).
		*plSize = 65507;
		#endif
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetMaxDatagramSize(): Never called Startup()!\n");
		}

	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Get maximum number of sockets.  This may be a system "global" value, which
// means that if other applications are using sockets, then the number
// available to this application may be lower than the returned value.
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetMaxSockets(				// Returns 0 if successfull, non-zero otherwise
	int32_t* plNum)											// Out: maximum number of sockets
	{
	int16_t sResult = 0;

	if (ms_bDidStartup)
		{
		#ifdef WIN32
		*plNum = (int32_t)ms_WSAData.iMaxSockets;
		#else
		// Sockets are file descriptors, so the descriptor limit is the limit.
		struct rlimit rl;
		if ((getrlimit(RLIMIT_NOFILE, &rl) == 0) && (rl.rlim_cur != RLIM_INFINITY) && (rl.rlim_cur < 0x7fffffff))
			*plNum = (int32_t)rl.rlim_cur;
		else
			*plNum = 0x7fffffff;
		#endif
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetMaxSockets(): Never called Startup()!\n");
		}
	return sResult;
	}


//////////////////////////////////////////////////////////////////////////////
// Get the address of the specified host from the string provided which may be
// a name or address
//////////////////////////////////////////////////////////////////////////////
/* static */
int16_t RProtocolBSDIP::GetAddress(					// Returns 0 if successfull, non-zero otherwise
	char* pszName,											// In:  Host's name or dotted address (x.x.x.x)
	uint16_t usPort,											// In:  Host's port number
	RSocket::Address* paddress)						// Out: Address
	{
	int16_t sResult = 0;
	
	if (ms_bDidStartup)
		{
		// Cast address to IP address to make it easier to work with
		AddressIP* pip = (AddressIP*)paddress;
		
		// If the string contains only digits and/or dots and there's at least one dot
		// then we assume it's a dotted address.
		if ((strspn(pszName, "0123456789.") >= strlen(pszName)) && (strchr(pszName, '.') != NULL))
			{
			// Convert dotted address into value (returned in network order!)
			uint32_t ulAddr = inet_addr(pszName);
			if (ulAddr != INADDR_NONE)
				{
				// Fill in the address
				#if PLATFORM_UNIX
				memset(&pip->address, 0, sizeof(pip->address));
				#endif
				pip->prototype = RSocket::TCPIP;
				pip->lAddressLen = sizeof(pip->address);
				pip->address.sin_family = AF_INET;
				#if PLATFORM_UNIX
				pip->address.sin_addr.s_addr = ulAddr;
				#else
				pip->address.sin_addr.S_un.S_addr = ulAddr;
				#endif
				pip->address.sin_port = htons(usPort);
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::GetAddress(): Invalid dotted address: %s\n", pszName);
				}
			}
		else
			{
			// Lookup address based on specified name
			BSDIP_FUNCNUM(RSocket::OtherFunc);
			#if PLATFORM_UNIX
			// getaddrinfo() is reentrant (gethostbyname() isn't, and is
			// deprecated on Apple platforms).  IPv4 only: our address struct is a
			// sockaddr_in.
			struct addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			struct addrinfo* pres = NULL;
			int iErr = getaddrinfo(pszName, NULL, &hints, &pres);
			if ((iErr == 0) && (pres != NULL) && (pres->ai_family == AF_INET) && (pres->ai_addrlen >= sizeof(SOCKADDR_IN)))
				{
				// Fill in the address
				memset(&pip->address, 0, sizeof(pip->address));
				pip->prototype = RSocket::TCPIP;
				pip->lAddressLen = sizeof(pip->address);
				pip->address.sin_family = AF_INET;
				pip->address.sin_addr = ((SOCKADDR_IN*)pres->ai_addr)->sin_addr;
				pip->address.sin_port = htons(usPort);
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::GetAddress(): Error returned by getaddrinfo(): %s\n", (iErr != 0) ? gai_strerror(iErr) : "no IPv4 address");
				}
			if (pres != NULL)
				freeaddrinfo(pres);
			#else
			HOSTENT* phost = gethostbyname(pszName);
			if (phost != NULL)
				{
				// Fill in the address
				pip->prototype = RSocket::TCPIP;
				pip->lAddressLen = sizeof(pip->address);
				pip->address.sin_family = phost->h_addrtype;
				memcpy((char*)&(pip->address.sin_addr), phost->h_addr_list[0], phost->h_length);
				pip->address.sin_port = htons(usPort);
				}
			else
				{
				sResult = -1;
				TRACE("RProtocolBSDIP::GetAddress(): Error returned by gethostbyname(): %ld\n", WSAGetLastError());
				}
			#endif
			}
		}
	else
		{
		sResult = -1;
		TRACE("RProtocolBSDIP::GetHostAddress(): Never called Startup()!\n");
		}
	
	return sResult;
	}


////////////////////////////////////////////////////////////////////////////////
// Create broadcast address using specified port
////////////////////////////////////////////////////////////////////////////////
// static
void RProtocolBSDIP::CreateBroadcastAddress(
	uint16_t usPort,								// In:  Port to broadcast to
	RSocket::Address* paddress)						// Out: Broadcast address returned here
	{
	// Cast address to IP address to make it easier to work with
	AddressIP* pip = (AddressIP*)paddress;
	
	// Create broadcast address using specified port
	pip->prototype = RSocket::TCPIP;
	pip->lAddressLen = sizeof(pip->address);
	pip->address.sin_family = AF_INET;
	pip->address.sin_port = htons(usPort);

    #ifdef WIN32
	pip->address.sin_addr.S_un.S_addr = htonl(INADDR_BROADCAST);
    #else
	pip->address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    #endif
	}


//////////////////////////////////////////////////////////////////////////////
// Get the specified address' current port number
//////////////////////////////////////////////////////////////////////////////
/* static */
uint16_t RProtocolBSDIP::GetAddressPort(	// Returns port number
	RSocket::Address* paddress)						// In:  Address from which to get port
	{
	PSOCKADDR_IN pin = (PSOCKADDR_IN) &(paddress->address);
	
	return ntohs(pin->sin_port);
	}


//////////////////////////////////////////////////////////////////////////////
// Set the specified address' port to the specified value
//////////////////////////////////////////////////////////////////////////////
/* static */
void RProtocolBSDIP::SetAddressPort(
	uint16_t usPort,											// In:  New port number
	RSocket::Address* paddress)						// I/O: Address who's port is to be set
	{
	PSOCKADDR_IN pin = (PSOCKADDR_IN) &(paddress->address);
	
	pin->sin_port = htons(usPort);
	}


////////////////////////////////////////////////////////////////////////////////
// This is a blocking-hook callback function.
// The winsock code will call this repeatedly while the return value is
// non-zero, but the documentation implies that it doesn't do anything else
// while calling this, so it seems like a better idea to return 0 all the time.
////////////////////////////////////////////////////////////////////////////////
/* static */
#ifdef WIN32
intptr_t CALLBACK RProtocolBSDIP::BlockingHook(void)
	{
	// !!!
	// The only Windows Socket API function you can call from this
	// function is WSACancelBlockingCall()
	// !!!
	
	// Call user-installed callback.  If return value is non-zero, cancel
	// the current blocking call.
	if (ms_callback != 0)
		{
		if ((*ms_callback)() != 0)
			{
			// Cancelling accept() or select() is fine --  only that particular
			// operation will fail, and the socket itself will be unaffected.
			// Cancelling any other operation will leave the socket in an unknown/
			// unstable state.  The only function you can call after cancelling
			// operations other than accept() or select() is closesocket().
			WSACancelBlockingCall();
			}
		}
	
	return 0;
	}
#endif


//////////////////////////////////////////////////////////////////////////////
// EOF
//////////////////////////////////////////////////////////////////////////////
