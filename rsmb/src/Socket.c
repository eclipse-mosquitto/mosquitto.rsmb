/*******************************************************************************
 * Copyright (c) 2007, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs, Nicholas O'Leary - initial API and implementation and/or initial documentation
 *******************************************************************************/

/**
 * @file
 * Socket related functions.
 * Some other related functions are in the SocketBuffer module
 */

#include "Socket.h"
#include "Log.h"
#include "SocketBuffer.h"
#include "Messages.h"
#include "StackTrace.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#if defined(WIN32)
#include <Iphlpapi.h>
#define iov_len len
#define iov_base buf
#else
#include <sys/uio.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif

#if defined(USE_POLL)
#include <sys/epoll.h>
#endif

#include "Heap.h"

int Socket_close_only(int socket);
#if defined(USE_POLL)
int Socket_continueWrite(int socket);
#else
int Socket_continueWrites(fd_set* pwset);
#endif

/**
 * Structure to hold all socket data for the module
 */
static Sockets s;

/**
 * Set a socket non-blocking, OS independently
 * @param sock the socket to set non-blocking
 * @return TCP call error code
 */
int Socket_setnonblocking(int sock)
{
	int rc;
#if defined(WIN32)
	u_long flag = 1L;

	FUNC_ENTRY;
	rc = ioctl(sock, FIONBIO, &flag);
#else
	int flags;

	FUNC_ENTRY;
	if ((flags = fcntl(sock, F_GETFL, 0)))
		flags = 0;
	rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Gets the specific error corresponding to SOCKET_ERROR
 * @param aString the function that was being used when the error occurred
 * @param sock the socket on which the error occurred
 * @return the specific TCP error code
 */
int Socket_error(char* aString, int sock)
{
#if defined(WIN32)
	int errno;
#endif

	FUNC_ENTRY;
#if defined(WIN32)
	errno = WSAGetLastError();
#endif
	if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)
	{
		if (strcmp(aString, "shutdown") != 0 || (errno != ENOTCONN && errno != ECONNRESET))
		{
			int orig_errno = errno;
			char* errmsg = strerror(errno);

			Log(LOG_WARNING, 75, NULL, orig_errno, errmsg, aString, sock);
		}
	}
	FUNC_EXIT_RC(errno);
	return errno;
}


#if defined(USE_POLL)
int TreeSockCompare(void* a, void* b, int value)
{
	int socka = ((struct socket_info*)a)->fd;
	int sockb = (value) ? ((struct socket_info*)b)->fd : *(int*)b;

	//printf("socka %d sockb %d value %d\n", socka, sockb, value);
	return (socka > sockb) ? -1 : (socka == sockb) ? 0 : 1;
}
#endif


/**
 * Initialize the socket module for outbound communications
 */
void Socket_outInitialize()
{
#if defined(WIN32)
	WORD    winsockVer = 0x0202;
	WSADATA wsd;

	FUNC_ENTRY;
	WSAStartup(winsockVer, &wsd);
#else
	FUNC_ENTRY;
	signal(SIGPIPE, SIG_IGN);
#endif
	SocketBuffer_initialize();
	
#if !defined(USE_POLL)
	s.clientsds = ListInitialize();
	s.connect_pending = ListInitialize();
	s.write_pending = ListInitialize();
	s.cur_clientsds = NULL;
	FD_ZERO(&(s.rset));														/* Initialize the descriptor set */
	FD_ZERO(&(s.pending_wset));
	s.maxfdp1 = 0;
	memcpy((void*)&(s.rset_saved), (void*)&(s.rset), sizeof(s.rset_saved));
	FD_ZERO(&(s.pending_wset));
#else
	s.fds_tree = TreeInitialize(TreeSockCompare);
	s.epoll_fds = epoll_create(1024);
	s.cur_sds = 0;
	s.no_ready = 0;
#endif
	s.newSockets = ListInitialize();
	FUNC_EXIT;
}


/**
 * Internal function to remove the trailing ] from IPv6 address strings.
 * @param str the IPv6 address string
 * @return the position of the string character changed, or 0 (] is never the first char in such a string)
 */
int ipv6_format(char* str)
{
	int pos = strlen(str)-1;
	int rc = 0;

	if (str[pos] == ']')
	{
		str[pos] = '\0';
		rc = pos;
	}
	return rc;
}


#if !defined(SINGLE_LISTENER)
/**
 * Fully initialize the socket module for in and outbound communications.
 * In multi-listener mode.
 * @param myListeners list of listeners
 * @return completion code
 */
int Socket_initialize(List* myListeners)
{
	ListElement* current = NULL;
	int rc = 0;

	FUNC_ENTRY;
	s.listeners = myListeners;
	Socket_outInitialize();
	while (ListNextElement(s.listeners, &current))
	{
		Listener* list = (Listener*)(current->content);
		if ((rc = Socket_addServerSocket(list)) != 0)
		{
			Log(LOG_WARNING, 15, NULL, list->port);
			break;
		}
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


#if defined(WIN32)
int win_inet_pton(int family, const char *src, void *dst)
{
	int rc;

	if (family == AF_INET6)
	{
		wchar_t buf[(INET6_ADDRSTRLEN+1)*2];
		int buflen = sizeof(buf);
		struct sockaddr_in6 sockaddr;

		mbstowcs(buf, src, sizeof(buf));
		rc = WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr*)&sockaddr, &buflen);
		memcpy(dst, &sockaddr.sin6_addr.s6_addr, sizeof(sockaddr.sin6_addr.s6_addr));
	}
	else
	{
		wchar_t buf[(INET_ADDRSTRLEN+1)*2];
		int buflen = sizeof(buf);
		struct sockaddr_in sockaddr;

		mbstowcs(buf, src, sizeof(buf));
		rc = WSAStringToAddress(buf, AF_INET, NULL, (struct sockaddr*)&sockaddr, &buflen);
		memcpy(dst, &sockaddr.sin_addr.s_addr, sizeof(sockaddr.sin_addr.s_addr));
	}
	return rc;
}
#endif


int Socket_joinMulticastGroup(int sock, int ipv6, char* mcast)
{
	int rc = 0;
	char* mcast_addr = mcast;
	char* mcast_interface = NULL;

	/* if there is a space in the mcast string, it means we have address plus interface specified */
	if ((mcast_interface = strchr(mcast, ' ')) != NULL)
	{
		*mcast_interface = '\0';
		++mcast_interface;
	}
	else
		mcast_interface = "INADDR_ANY";

	if (ipv6)
	{
		struct ipv6_mreq mreq6;

#if defined(WIN32)
		if ((rc = win_inet_pton(AF_INET6, mcast_addr, &mreq6.ipv6mr_multiaddr.s6_addr)) == SOCKET_ERROR)
			Socket_error("WSAStringToAddress", sock);
#else
		if ((rc = inet_pton(AF_INET6, mcast_addr, &mreq6.ipv6mr_multiaddr.s6_addr)) != 1)
		{
			if (rc == 0)
				Log(LOG_WARNING, 67, NULL, mcast_addr);
			else if (rc == SOCKET_ERROR)
				Socket_error("inet_pton", sock);
		}
#endif
		if (strcmp(mcast_interface, "INADDR_ANY") == 0)
			mreq6.ipv6mr_interface = 0;
		else
		{
#if defined(WIN32)
			/* on Windows, the if_nametoindex function requires Vista, i.e. will not work on XP */
			/* Alternative functions are not short running.  So for now, we can take an interface number */
			mreq6.ipv6mr_interface = atoi(mcast_interface);
#else
			mreq6.ipv6mr_interface = if_nametoindex(mcast_interface);
#endif
			if (mreq6.ipv6mr_interface == 0)
				Log(LOG_WARNING, 302, NULL, mcast_interface);
		}
		rc = setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (const char*)&mreq6, sizeof(mreq6));
	}
	else
	{
		struct ip_mreq mreq;
#if defined(WIN32)
		if ((rc = win_inet_pton(AF_INET, mcast_addr, &mreq.imr_multiaddr.s_addr)) == SOCKET_ERROR)
			Socket_error("WSAStringToAddress", sock);
#else
		if ((rc = inet_pton(AF_INET, mcast_addr, &mreq.imr_multiaddr.s_addr)) != 1)
		{
			if (rc == 0)
				Log(LOG_WARNING, 67, NULL, mcast_addr);
			else if (rc == -1)
				Socket_error("inet_pton", sock);
		}
#endif
		if (strcmp(mcast_interface, "INADDR_ANY") == 0)
			mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		else
		{
			/* find an address corresponding to the named interface */
#if defined(WIN32)
			/* on Windows, the GetAdaptersAddresses is suggested as a method for getting an address for an adapter.
			   However that function is not short running.  So for now, we can just take an address */

			if ((rc = win_inet_pton(AF_INET, mcast_interface, &mreq.imr_interface.s_addr)) == SOCKET_ERROR)
				Socket_error("WSAStringToAddress interface", sock);

			//mreq.imr_interface.s_addr = inet_addr(mcast_interface); /* can use inet_addr as this code is IPv4 specific */
#else
			struct ifreq ifreq;

			strncpy(ifreq.ifr_name, mcast_interface, IFNAMSIZ);
			if (ioctl(sock, SIOCGIFADDR, &ifreq) >= 0)
				memcpy(&mreq.imr_interface,
						&((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr,
						sizeof(struct in_addr));
			else
				Socket_error("ioctl SIOCGIFADDR", sock);
#endif
		}
		rc = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
	}
	Log(LOG_INFO, 303, NULL, mcast_addr, mcast_interface);
	if (rc != 0)
		Socket_error("add to multicast group", sock);
	return rc;
}


/**
 * Create the server socket in multi-listener mode.
 * @param list pointer to a listener structure
 * @return completion code
 */
int Socket_addServerSocket(Listener* list)
{
	int flag = 1;
	int rc = SOCKET_ERROR;
	int ipv4 = 1; /* yes we can drop down to ipv4 */

	FUNC_ENTRY;
	if (!list->address || strcmp(list->address, "INADDR_ANY") == 0)
	{
		struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
		list->addr.sin_addr.s_addr = htonl(INADDR_ANY);
		list->addr6.sin6_addr = in6addr_any;
	}
	else
	{
		if (list->address[0] == '[')
		{
			int changed = ipv6_format(list->address);
#if defined(WIN32)
			/*wchar_t buf[(INET6_ADDRSTRLEN+1)*2];
			int buflen = sizeof(list->addr6);
			mbstowcs(buf, &list->address[1], sizeof(buf));
			rc = WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr*)&list->addr6, &buflen);*/
			rc = win_inet_pton(AF_INET6, &list->address[1], &(list->addr6.sin6_addr));
#else
			rc = inet_pton(AF_INET6, &list->address[1], &(list->addr6.sin6_addr));
#endif
			ipv4 = 0;
			if (changed)
				(list->address)[changed] = ']';
		}
		else
		{
#if defined(WIN32)
			/*wchar_t buf[(INET6_ADDRSTRLEN+1)*2];
			int buflen = sizeof(list->addr);
			mbstowcs(buf, list->address, sizeof(buf));
			rc = WSAStringToAddress(buf, AF_INET, NULL, (struct sockaddr*)&list->addr, &buflen);*/
			rc = win_inet_pton(AF_INET, list->address, &(list->addr.sin_addr.s_addr));
#else
			rc = inet_pton(AF_INET, list->address, &(list->addr.sin_addr.s_addr));
#endif
			list->ipv6 = 0;
		}
#if defined(WIN32)
		if (rc != 0)
#else
		if (rc != 1)
#endif
		{
			Log(LOG_WARNING, 67, NULL, list->address);
			rc = -1;
			goto exit;
		}
	}
	list->socket = -1;
	if (list->protocol == PROTOCOL_MQTT)
	{
		if (list->ipv6)
			list->socket = socket(AF_INET6, SOCK_STREAM, 0);
		if (list->socket < 0 && ipv4)
		{
			list->socket = socket(AF_INET, SOCK_STREAM, 0);
			list->ipv6 = 0;
		}
	}
#if defined(MQTTS)
	else if (list->protocol == PROTOCOL_MQTTS)
	{
		if (list->ipv6)
			list->socket = socket(AF_INET6, SOCK_DGRAM, 0);
		if (list->socket < 0 && ipv4)
		{
			list->socket = socket(AF_INET, SOCK_DGRAM, 0);
			list->ipv6 = 0;
		}
	}
#endif
	Log(TRACE_MAX, 6, NULL, FD_SETSIZE);
	if (list->socket < 0)
	{
		Socket_error("socket", list->socket);
		Log(LOG_WARNING, 77, NULL);
		rc = list->socket;
		goto exit;
	}

#if !defined(WIN32)
	if (setsockopt(list->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(int)) != 0)
		Log(LOG_WARNING, 109, NULL, list->port);
#endif
	if (list->ipv6)
	{
		list->addr6.sin6_family = AF_INET6;
		list->addr6.sin6_port = htons(list->port);
		rc = bind(list->socket, (struct sockaddr *)&(list->addr6), sizeof(list->addr6));
	}
	else
	{
		list->addr.sin_family = AF_INET;
		list->addr.sin_port = htons(list->port);
		memset(list->addr.sin_zero, 0, sizeof(list->addr.sin_zero));
		rc = bind(list->socket, (struct sockaddr *)&(list->addr), sizeof(list->addr));
	}
	if (rc == SOCKET_ERROR)
	{
		Socket_error("bind", list->socket);
		Log(LOG_WARNING, 78, NULL, list->port);
		goto exit;
	}

	/* Only listen if this is mqtt/tcp */
	if (list->protocol == PROTOCOL_MQTT &&
			listen(list->socket, SOMAXCONN) == SOCKET_ERROR) /* second parm is max no of connections */
	{
		Socket_error("listen", list->socket);
		Log(LOG_WARNING, 79, NULL, list->port);
		goto exit;
	}

	if (Socket_setnonblocking(list->socket) == SOCKET_ERROR)
	{
		Socket_error("setnonblocking", list->socket);
		goto exit;
	}

#if defined(MQTTS)
	/* If mqtts/udp, add to the list of clientsds to test for reading */
	if (list->protocol == PROTOCOL_MQTTS)
	{
		ListElement* current = NULL;
		int loopback = list->loopback;

#if !defined(USE_POLL)
		int* pnewSd = (int*)malloc(sizeof(list->socket));

		*pnewSd = list->socket;
		ListAppend(s.clientsds, pnewSd, sizeof(list->socket));
#endif
		Log(LOG_INFO, 300, NULL, list->port);

		if (setsockopt(list->socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback)) == SOCKET_ERROR)
			Socket_error("set listener IP_MULTICAST_LOOP", list->socket);

		/* join any multicast groups */
		while (ListNextElement(list->multicast_groups, &current))
			Socket_joinMulticastGroup(list->socket, list->ipv6, (char*)(current->content));
	}
	else
#endif
		Log(LOG_INFO, 14, NULL, list->port);

#if defined(USE_POLL)
	/* add new socket to the epoll descriptor with epoll_ctl */
	struct socket_info* si = malloc(sizeof(struct socket_info));
	si->listener = list;
	si->fd = list->socket;
	si->connect_pending = 0;
	si->event.events = EPOLLIN;
	si->event.data.ptr = si;
	TreeAdd(s.fds_tree, si, sizeof(struct socket_info));
	if (epoll_ctl(s.epoll_fds, EPOLL_CTL_ADD, list->socket, &si->event) != 0)
		Socket_error("epoll_ctl add", list->socket);
#else
	FD_SET((u_int)list->socket, &(s.rset));         /* Add the current socket descriptor */
	s.maxfdp1 = max(s.maxfdp1+1, list->socket+1);

	memcpy((void*)&(s.rset_saved), (void*)&(s.rset), sizeof(s.rset_saved));
#endif

	rc = 0;
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}
#else
/**
 * Fully initialize the socket module for in and outbound communications.
 * In single-listener mode.
 * @param anAddress IP address string
 * @param port number
 * @param ipv6 flag to indicate ipv6 should be used
 * @return completion code
 */
int Socket_initialize(char* anAddress, int aPort, int ipv6)
{
	int flag = 1;
	int rc = SOCKET_ERROR;
	int ipv4 = 1; /* yes we can drop down to ipv4 */

	FUNC_ENTRY;
	s.port = aPort;
	if (anAddress == NULL || strcmp(anAddress, "INADDR_ANY") == 0)
	{
		s.addr.sin_addr.s_addr = htonl(INADDR_ANY);
		s.addr6.sin6_addr = in6addr_any;
	}
	else
	{
		if (anAddress[0] == '[')
		{
			int changed = ipv6_format(anAddress);
			inet_pton(AF_INET6, &anAddress[1], &(s.addr6.sin6_addr));
			ipv4 = 0;
			if (changed)
				(anAddress)[changed] = ']';
		}
		else
		{
			inet_pton(AF_INET, anAddress, &(s.addr.sin_addr.s_addr));
			ipv6 = 0;
		}
	}
	Socket_outInitialize();
	s.mySocket = -1;
	if (ipv6)
		s.mySocket = socket(AF_INET6, SOCK_STREAM, 0);
	if (s.mySocket < 0 && ipv4)
	{
		s.mySocket = socket(AF_INET, SOCK_STREAM, 0);
		ipv6 = 0;
	}
	s.maxfdp1 = 0;
	Log(TRACE_MAX, 6, NULL, FD_SETSIZE);
	if (s.mySocket < 0)
	{
		Socket_error("socket", s.mySocket);
		Log(LOG_WARNING, 77, NULL);
		rc = s.mySocket;
		goto exit;
	}

#if !defined(WIN32)
	if (setsockopt(s.mySocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(int)) != 0)
		Log(LOG_WARNING, 109, NULL, aPort);
#endif
	if (ipv6)
	{
		s.addr6.sin6_family = AF_INET6;
		s.addr6.sin6_port = htons(s.port);
		rc = bind(s.mySocket, (struct sockaddr *)&(s.addr6), sizeof(s.addr6));
	}
	else
	{
		s.addr.sin_family = AF_INET;
		s.addr.sin_port = htons(s.port);
		memset(s.addr.sin_zero, 0, sizeof(s.addr.sin_zero));
		rc = bind(s.mySocket, (struct sockaddr *)&(s.addr), sizeof(s.addr));
	}
	if (rc == SOCKET_ERROR)
	{
		Socket_error("bind", s.mySocket);
		Log(LOG_WARNING, 78, NULL, aPort);
		goto exit;
	}
	if (listen(s.mySocket, SOMAXCONN) == SOCKET_ERROR) /* second parm is max no of connections */
	{
		Socket_error("listen", s.mySocket);
		Log(LOG_WARNING, 79, NULL, aPort);
		goto exit;
	}
	if (Socket_setnonblocking(s.mySocket) == SOCKET_ERROR)
	{
		Socket_error("setnonblocking", s.mySocket);
		goto exit;
	}
	FD_SET((u_int)s.mySocket, &(s.rset));         /* Add the current socket descriptor */
	s.maxfdp1 = s.mySocket + 1;
	memcpy((void*)&(s.rset_saved), (void*)&(s.rset), sizeof(s.rset_saved));
	rc = 0;
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}
#endif


/**
 * Terminate the socket module for outbound communications only
 */
void Socket_outTerminate()
{
	FUNC_ENTRY;
#if defined(USE_POLL)
	TreeFree(s.fds_tree);
#else
	ListFree(s.connect_pending);
	ListFree(s.write_pending);
	ListFree(s.clientsds);
#endif
	ListFree(s.newSockets);
	SocketBuffer_terminate();
#if defined(WIN32)
	WSACleanup();
#endif
	FUNC_EXIT;
}

/**
 * Terminate the socket module for both in an outbound communications
 */
void Socket_terminate()
{
#if !defined(SINGLE_LISTENER)
	ListElement* current = NULL;
#if defined(USE_POLL)
	struct socket_info* si = NULL;
#endif

	FUNC_ENTRY;
	while (ListNextElement(s.listeners, &current))
	{
		Listener* listener = (Listener*)(current->content);
		Socket_close_only(listener->socket);
#if defined(USE_POLL)
		if ((si = TreeRemoveKey(s.fds_tree, &listener->socket)) == NULL)
			Log(LOG_WARNING, 13, "Failed to remove socket %d", listener->socket);
		else
			free(si);
#endif
	}
#else
	FUNC_ENTRY;
	Socket_close_only(s.mySocket);
#endif
	Socket_outTerminate();
	FUNC_EXIT;
}


int newSocketCompare(void* a, void* b)
{
	NewSockets* new = (NewSockets*)a;
	/*printf("comparing %d with %d\n", (char*)a, (char*)b);*/
	return new->socket ==  *(int*)b;
}


NewSockets* Socket_getNew(int socket)
{
	NewSockets* rc = NULL;
	ListElement* found = ListFindItem(s.newSockets, &socket, newSocketCompare);
	
	if (found)
		rc = (NewSockets*)(found->content); 
	return rc;
}


/**
 * Add a new socket to the client sockets list so that select() will operate on it
 * @param newSd the new socket to add
 * @return completion code - not used
 */
#if defined(USE_POLL)
int Socket_addSocket(int newSd, struct socket_info** ssi, int outbound)
#else
int Socket_addSocket(int newSd, int outbound)
#endif
{
	int rc = 0;

	FUNC_ENTRY;
#if !defined(USE_POLL)
	if (ListFindItem(s.clientsds, &newSd, intcompare) == NULL) /* make sure we don't add the same socket twice */
	{
		NewSockets* new = (NewSockets*)malloc(sizeof(NewSockets));
		int* pnewSd = (int*)malloc(sizeof(newSd));
		*pnewSd = newSd;
		ListAppend(s.clientsds, pnewSd, sizeof(newSd));
		FD_SET((u_int)newSd, &(s.rset_saved));
		s.maxfdp1 = max(s.maxfdp1, newSd + 1);
		rc = Socket_setnonblocking(newSd);
		new->socket = newSd;
		new->outbound = outbound;
		time(&new->opened);
		ListAppend(s.newSockets, new, sizeof(NewSockets));
	}
	else
		Log(TRACE_MAX, 7, NULL, newSd);
#else
	if (TreeFind(s.fds_tree, &newSd) == NULL)
	{
		NewSockets* new = (NewSockets*)malloc(sizeof(NewSockets));
		struct socket_info* si = malloc(sizeof(struct socket_info));

		si->listener = NULL;
		si->fd = newSd;
		si->connect_pending = 0;
		si->event.events = EPOLLIN;
		si->event.data.ptr = *ssi = si;
		TreeAdd(s.fds_tree, si, sizeof(struct socket_info));
		if (epoll_ctl(s.epoll_fds, EPOLL_CTL_ADD, newSd, &si->event) != 0)
			Socket_error("epoll_ctl add", newSd);
		rc = Socket_setnonblocking(newSd);
		new->socket = newSd;
		new->outbound = outbound;
		time(&new->opened);
		ListAppend(s.newSockets, new, sizeof(NewSockets));
	}
	else
		Log(TRACE_MAX, 7, NULL, newSd);
#endif

	FUNC_EXIT_RC(rc);
	return rc;
}

int Socket_removeNew(int socket)
{
	return ListRemoveItem(s.newSockets, &socket, newSocketCompare);
}


void Socket_cleanNew(time_t now)
{
	ListElement* elem = s.newSockets->first;

	while (elem)
	{
		ListElement* current = elem;
		
		ListNextElement(s.newSockets, &elem);
		if (difftime(now, ((NewSockets*)(current->content))->opened) > 60)
		{
			Log(TRACE_MIN, 0, "Connect packet not received on socket %d within 60s. - closing socket", 
				((NewSockets*)(current->content))->socket);
			ListRemove(s.newSockets, current->content);
		}			
	}
}


#if !defined(USE_POLL)
/**
 * Checks whether a socket is ready for work.
 * Don't accept work from a client unless it is accepting work back, i.e. its socket is writeable
 * this seems like a reasonable form of flow control, and practically, seems to work.
 * @param socket the socket to check
 * @param read_set the socket read set (see select doc)
 * @param write_set the socket write set (see select doc)
 * @return boolean - is the socket ready to go?
 */
int isReady(int socket, fd_set* read_set, fd_set* write_set)
{
	int rc = 1;
	int is_writeable = -1;

	if  (ListFindItem(s.connect_pending, &socket, intcompare) && (is_writeable = FD_ISSET(socket, write_set)))
		ListRemoveItem(s.connect_pending, &socket, intcompare);
	else
	{
		if (is_writeable == -1)
			is_writeable = FD_ISSET(socket, write_set);
		rc = FD_ISSET(socket, read_set) && is_writeable && Socket_noPendingWrites(socket);
	}
	return rc;
}
#else
int isReady(struct socket_info* info)
{
	int rc = 0;
	struct pollfd pollfd;
	
	/* the fact that we are here means the we are ready for reading */
	pollfd.fd = info->fd;
	pollfd.events = POLLOUT;
	if (poll(&pollfd, 1, 0) &&                   	/* if the socket is writeable, */
		(info->event.events & POLLOUT) == 0)        /* and it has no pending writes, */
		rc = 1;                                     /* return true. */

	return rc;
}
#endif


/**
 * Create and initialize the socket statistics.
 */
static socket_stats ss =
{
	0, 0, 0, 0
};


/**
 * Allows another module to get access to the socket statistics
 * @return pointer to the socket statistics structure
 */
socket_stats* Socket_getStats()
{
	static socket_stats reported;
	reported = ss;
	memset(&ss, 0, sizeof(ss));
	return &reported;
}


void newConnection(Listener* list)
{
	int newSd;
	struct sockaddr_in addr;
	unsigned int cliLen = sizeof(struct sockaddr_in6);
	struct sockaddr_in6 addr6;
	struct sockaddr *cliAddr = (struct sockaddr *)&addr6;
					
	if (list->ipv6 == 0)
	{
		cliLen = sizeof(struct sockaddr_in);
		cliAddr = (struct sockaddr *)&addr;
	}
	
	if ((newSd = accept(list->socket, (struct sockaddr *)cliAddr, &cliLen)) == SOCKET_ERROR)
		Socket_error("accept", list->socket);
	else
	{
		int* sockmem = (int*)malloc(sizeof(int));
		char buf[INET6_ADDRSTRLEN];
#if defined(USE_POLL)
		struct socket_info* si;
#endif
		
		if (list->ipv6)
		{
			Socket_getaddrname((struct sockaddr*)&addr6, addr6.sin6_port);
			Log(TRACE_MAX, 10, NULL, newSd, buf, addr6.sin6_port);
		}
		else
		{
			Socket_getaddrname((struct sockaddr*)&addr, addr.sin_port);
			Log(TRACE_MAX, 10, NULL, newSd, buf, addr.sin_port);
		}
		*sockmem = newSd;
		ListAppend(list->connections, sockmem, sizeof(sockmem));
#if defined(USE_POLL)
		Socket_addSocket(newSd, &si, 0);
#else
		Socket_addSocket(newSd, 0);
#endif
	}
}


#if defined(USE_POLL)
void Socket_epollprocess()
{
	FUNC_ENTRY;
	/* cycle through the list of returned socket structures, processing each */
	while (s.cur_sds < s.no_ready)
	{
		struct socket_info* cur_info = s.events[s.cur_sds].data.ptr;
			
		if (cur_info->event.events & EPOLLIN) /* if this socket is readable */
		{
			if (cur_info->listener && cur_info->listener->protocol == 0) /* if it is a listener, and not MQTTs */
				newConnection(cur_info->listener);
			else if (isReady(cur_info))
				break;
		}	
		else if ((cur_info->event.events & EPOLLOUT)) /* has a pending write or connect */
		{
			if (cur_info->connect_pending)
			{
				cur_info->connect_pending = 0;
				cur_info->event.events = EPOLLIN;
				if (epoll_ctl(s.epoll_fds, EPOLL_CTL_MOD, cur_info->fd, &cur_info->event) != 0)
					Socket_error("epoll ctl MOD", cur_info->fd);
				break;
			}
			if (Socket_continueWrite(cur_info->fd))
			{
				if (!SocketBuffer_writeComplete(cur_info->fd))
					Log(LOG_SEVERE, 35, NULL);
				else
				{
					cur_info->event.events = EPOLLIN;
					if (epoll_ctl(s.epoll_fds, EPOLL_CTL_MOD, cur_info->fd, &cur_info->event) != 0)
						Socket_error("epoll ctl MOD", cur_info->fd);
				}
			}
		}

		++s.cur_sds;
	}
	FUNC_EXIT;
}
#endif


/**
 *  Returns the next socket ready for communications as indicated by select.
 *  We have two types of socket, the main broker one on which connections are accepted,
 *  and client sockets which are generated as a result of accepting connections.  So there
 *  are two different actions to be taken depending on the type of the connection.  We can use the
 *  select() call to check for actions to be taken on both types of socket when especially when
 *  we are in single-threaded mode.
 *  @param more_work flag to indicate more work is waiting, and thus a timeout value of 0 should
 *  be used for the select
 *  @param tp the timeout to be used for the select, unless overridden
 *  @return the socket next ready, or 0 if none is ready
 */
int Socket_getReadySocket(int more_work, struct timeval *tp)
{
	int retval = 0;
#if defined(USE_POLL)
	int timeout = 1000; /* default one second */
#else
	static struct timeval zero = {0L, 0L}; /* 0 seconds */
	static fd_set wset;
	static struct timeval one = {1L, 0L}; /* 1 second */
	struct timeval timeout = one;
	static int done = 1, rc1 = 0;
	#if !defined(SINGLE_LISTENER)
		ListElement* current = NULL;
	#endif
#endif
	static int rc = 0;

	FUNC_ENTRY;
#if !defined(USE_POLL)
	if (more_work)
		timeout = zero;
	else if (tp)
		timeout = *tp;
#else
	if (more_work)
		timeout = 0;
	else if (tp)
		timeout = (tp->tv_sec * 1000) + (tp->tv_usec / 1000);
#endif

	if (more_work)
		(ss.more_work_count)++;
	else
		(ss.not_more_work_count)++;
		
#if defined(USE_POLL)
	if (timeout == 0)
		(ss.timeout_zero_count)++;
	else
		(ss.timeout_non_zero_count)++;
#else
	if (timeout.tv_sec == 0L && timeout.tv_usec == 0L)
		(ss.timeout_zero_count)++;
	else
		(ss.timeout_non_zero_count)++;
#endif

#if !defined(USE_POLL)
	while (s.cur_clientsds != NULL)
	{
		if (done >= rc)
		{
			s.cur_clientsds = NULL;
			break;
		}
		if (isReady(*((int*)(s.cur_clientsds->content)), &(s.rset), &wset))
			break;
		ListNextElement(s.clientsds, &s.cur_clientsds);
	}
#else
	Socket_epollprocess();
#endif

#if !defined(USE_POLL)
	if (s.cur_clientsds == NULL)
	{
		fd_set pwset;
		fd_set errorset;

		memcpy((void*)&(s.rset), (void*)&(s.rset_saved), sizeof(s.rset));
		memcpy((void*)&(pwset), (void*)&(s.pending_wset), sizeof(pwset));
		memcpy((void*)&(errorset), (void*)&(s.rset_saved), sizeof(errorset));
		if ((rc = select(s.maxfdp1, &(s.rset), &pwset, &errorset, &timeout)) == SOCKET_ERROR)
		{
			Socket_error("read select", 0);
			goto exit;
		}
		Log(TRACE_MAX, 8, NULL, rc);

		if (Socket_continueWrites(&pwset) == SOCKET_ERROR)
		{
			rc = 0;
			goto exit;
		}

		memcpy((void*)&wset, (void*)&(s.rset_saved), sizeof(wset));
		memcpy((void*)&(errorset), (void*)&(s.rset_saved), sizeof(errorset));
		if ((rc1 = select(s.maxfdp1, NULL, &(wset), &(errorset), &zero)) == SOCKET_ERROR)
		{
			Socket_error("write select", 0);
			rc = rc1;
			goto exit;
		}
		Log(TRACE_MAX, 9, NULL, rc1);
		//printf("select rc %d rc1 %d\n", rc, rc1);
		
		if (rc == 0 && rc1 == 0)
			goto exit; /* no work to do */
#else
	if (s.cur_sds >= s.no_ready)
	{	
		/* check for any readable sockets, or writeable for pending writes */
		/* the events field has already been modified for pending writes in Socket_putdatas */
		rc = epoll_wait(s.epoll_fds, s.events, MAX_EVENTS, timeout);
		if (rc == SOCKET_ERROR)
		{
			Socket_error("read epoll_wait", 0);
			goto exit;
		}
		Log(TRACE_MAX, 8, NULL, rc);
		
		if (rc == 0)
			goto exit; /* no work to do */
#endif

#if !defined(SINGLE_LISTENER)
#if !defined(USE_POLL)
		while (ListNextElement(s.listeners, &current))
		{
			Listener* list = (Listener*)(current->content);
			/* New connections only exist for mqtt/tcp listeners
			 */
			if (list->protocol == 0 && FD_ISSET(list->socket, &(s.rset)))  /* if this is a new connection attempt */
				newConnection(list);
		}
#endif
#else
		if (FD_ISSET(s.mySocket, &(s.rset)))  /* if this is a new connection attempt */
		{
			int newSd;
			struct sockaddr_in addr;
			unsigned int cliLen = sizeof(struct sockaddr_in6);
			struct sockaddr_in6 addr6;
			struct sockaddr *cliAddr = (struct sockaddr *)&addr6;

			if (s.ipv6 == 0)
			{
				cliLen = sizeof(struct sockaddr_in);
				cliAddr = (struct sockaddr *)&addr;
			}

			if ((newSd = accept(s.mySocket, (struct sockaddr *)cliAddr, &cliLen)) == SOCKET_ERROR)
				Socket_error("accept", s.mySocket);
			else
			{
				char buf[INET6_ADDRSTRLEN];

				if (s.ipv6)
				{
					if (inet_ntop(AF_INET6, (const void*)&addr6.sin6_addr, buf, sizeof(buf)) == NULL)
						Socket_error("inet_ntop", s.mySocket);
					else
						Log(TRACE_MAX, 10, NULL, newSd, buf, addr6.sin6_port);
				}
				else
				{
					if (inet_ntop(AF_INET, (const void*)&addr.sin_addr, buf, sizeof(buf)) == NULL)
						Socket_error("inet_ntop", s.mySocket);
					else
						Log(TRACE_MAX, 10, NULL, newSd, buf, addr.sin_port);
				}
				Socket_addSocket(newSd, 0);
			}
		}
#endif

#if !defined(USE_POLL)
		/* Any mqtts/udp listeners that exist will be in the clientsds list.
		 * As the one socket could be serving many clients, this could
		 * cause mqtt/tcp to starve mqtts/udp.
		 */
		done = 0;
		s.cur_clientsds = s.clientsds->first;
		while (s.cur_clientsds != NULL)
		{
			int cursock = *((int*)(s.cur_clientsds->content));
			if (isReady(cursock, &(s.rset), &wset))
				break;
			/* if there is a socket which is readable but not writeable, and it's the only work going, return it so it can be cleared up.
			 * If it's not cleaned up it can cause getReadySocket to never wait on select, meaning we use 100% CPU spinning.
			 */
			if (rc == 1 && rc1 == 0 && FD_ISSET(cursock, &s.rset) && !FD_ISSET(cursock, &wset))
				break;
			ListNextElement(s.clientsds, &s.cur_clientsds);
		}
#else
		s.no_ready = rc;
		s.cur_sds = 0;
		Socket_epollprocess();
#endif
	}

#if !defined(USE_POLL)
	if (s.cur_clientsds == NULL)
		retval = 0;
	else
	{
		retval = *((int*)(s.cur_clientsds->content));
		ListNextElement(s.clientsds, &s.cur_clientsds);
	}
#else
	if (s.cur_sds >= s.no_ready)
		retval = 0;
	else
	{
		retval = ((struct socket_info*)(s.events[s.cur_sds].data.ptr))->fd;
		++s.cur_sds;
	}
#endif
exit:
	FUNC_EXIT_RC(retval);
	return retval;
} /* end getReadySocket */


/**
 *  Reads one byte from a socket
 *  @param socket the socket to read from
 *  @param c the character read, returned
 *  @return completion code
 */
int Socket_getch(int socket, char* c)
{
	int rc = SOCKET_ERROR;

	FUNC_ENTRY;
	if ((rc = SocketBuffer_getQueuedChar(socket, c)) != SOCKETBUFFER_INTERRUPTED)
		goto exit;

	if ((rc = recv(socket, c, (size_t)1, 0)) == SOCKET_ERROR)
	{
		int err = Socket_error("recv - getch", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
		{
			rc = TCPSOCKET_INTERRUPTED;
			SocketBuffer_interrupted(socket, 0);
		}
	}
	else if (rc == 0)
		rc = SOCKET_ERROR; 	/* The return value from recv is 0 when the peer has performed an orderly shutdown. */
	else if (rc == 1)
	{
		SocketBuffer_queueChar(socket, *c);
		rc = TCPSOCKET_COMPLETE;
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to read a number of bytes from a socket, non-blocking. If a previous read did not
 *  finish, then retrieve that data.
 *  @param socket the socket to read from
 *  @param bytes the number of bytes to read
 *  @param actual_len the actual number of bytes read
 *  @return completion code
 */
char *Socket_getdata(int socket, int bytes, int* actual_len)
{
	int rc;
	char* buf = NULL;

	FUNC_ENTRY;
	if (bytes == 0)
	{
		buf = SocketBuffer_complete(socket);
		goto exit;
	}

	buf = SocketBuffer_getQueuedData(socket, bytes, actual_len);

	if ((rc = recv(socket, buf + (*actual_len), (size_t)(bytes - (*actual_len)), 0)) == SOCKET_ERROR)
	{
		rc = Socket_error("recv - getdata", socket);
		if (rc != EAGAIN && rc != EWOULDBLOCK)
		{
			buf = NULL;
			goto exit;
		}
	}
	else if (rc == 0) /* rc 0 means the other end closed the socket, albeit "gracefully" */
	{
		buf = NULL;
		goto exit;
	}
	else
		*actual_len += rc;

	if (*actual_len == bytes)
		SocketBuffer_complete(socket);
	else /* we didn't read the whole packet */
	{
		SocketBuffer_interrupted(socket, *actual_len);
		Log(TRACE_MAX, 12, NULL, bytes, *actual_len);
	}
exit:
	FUNC_EXIT;
	return buf;
}

#if defined(USE_POLL)
int Socket_noPendingWrites(int socket)
{	
	int rc = 1;
	struct socket_info* si = NULL;
	Node* elem = NULL;
	
	FUNC_ENTRY;
	elem = TreeFind(s.fds_tree, &socket);
	if (elem && ((si = (struct socket_info*)(elem->content)) != NULL))
		rc = ((si->event.events & POLLOUT) == 0);
	FUNC_EXIT_RC(rc);
	return rc;
}
#else
/**
 *  Indicate whether any data is pending outbound for a socket.
 *  @return boolean - true == data pending.
 */
int Socket_noPendingWrites(int socket)
{
	int cursock = socket;
	/*if (s.write_pending->count > 0)
		printf("size of s.write_pending is %d\n", s.write_pending->count);*/
	return ListFindItem(s.write_pending, &cursock, intcompare) == NULL;
}
#endif


/**
 *  Attempts to write a series of iovec buffers to a socket in *one* system call so that
 *  they are sent as one packet.
 *  @param socket the socket to write to
 *  @param iovecs an array of buffers to write
 *  @param count number of buffers in iovecs
 *  @param bytes number of bytes actually written returned
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_writev(int socket, iobuf* iovecs, int count, unsigned long* bytes)
{
	int rc;

	FUNC_ENTRY;
#if defined(WIN32)
	rc = WSASend(socket, iovecs, count, (LPDWORD)bytes, 0, NULL, NULL);
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("WSASend - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
	}
#else
	*bytes = 0L;
	rc = writev(socket, iovecs, count);
	if (rc == SOCKET_ERROR)
	{
		int err = Socket_error("writev - putdatas", socket);
		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
	}
	else
		*bytes = rc;
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Attempts to write a series of buffers to a socket in *one* system call so that they are
 *  sent as one packet.
 *  @param socket the socket to write to
 *  @param buf0 the first buffer
 *  @param buf0len the length of data in the first buffer
 *  @param count number of buffers
 *  @param buffers an array of buffers to write
 *  @param buflens an array of corresponding buffer lengths
 *  @return completion code, especially TCPSOCKET_INTERRUPTED
 */
int Socket_putdatas(int socket, char* buf0, int buf0len, int count, char** buffers, int* buflens)
{
	unsigned long bytes = 0L;
	iobuf iovecs[5];
	int rc = TCPSOCKET_NOWORK, i, total = buf0len;

	FUNC_ENTRY;
	if (!Socket_noPendingWrites(socket))
	{
		Log(LOG_SEVERE, 0, "Trying to write to socket %d for which there is already pending output", socket);
		rc = TCPSOCKET_NOWORK;
		goto exit;
	}

	for (i = 0; i < count; i++)
		total += buflens[i];

	iovecs[0].iov_base = buf0;
	iovecs[0].iov_len = buf0len;
	for (i = 0; i < count; i++)
	{
		iovecs[i+1].iov_base = buffers[i];
		iovecs[i+1].iov_len = buflens[i];
	}

	if ((rc = Socket_writev(socket, iovecs, count+1, &bytes)) != SOCKET_ERROR)
	{
		if (bytes == total)
			rc = TCPSOCKET_COMPLETE;
		else if (bytes == 0)
		{
		  Log(TRACE_MIN, 32, NULL);
			rc = TCPSOCKET_NOWORK;
		}
		else /* the packet was partially written, so we have to buffer for the write to be finished later */
		{
#if !defined(USE_POLL)
			int* sockmem = NULL;
#endif
			
			Log(TRACE_MIN, 33, NULL, bytes, total, socket);
			SocketBuffer_pendingWrite(socket, count+1, iovecs, total, bytes);
#if defined(USE_POLL)
			struct socket_info* si = (struct socket_info*)(TreeFind(s.fds_tree, &socket)->content); /* find socket_info stucture by socket */
			si->event.events = EPOLLOUT;
			rc = epoll_ctl(s.epoll_fds, EPOLL_CTL_MOD, socket, &si->event);
#else
			sockmem = (int*)malloc(sizeof(int));
			*sockmem = socket;
			ListAppend(s.write_pending, sockmem, sizeof(int));
			FD_SET(socket, &(s.pending_wset));
#endif
			rc = TCPSOCKET_INTERRUPTED;
		}
	}
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Close a socket without removing it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
int Socket_close_only(int socket)
{
	int rc;

	FUNC_ENTRY;
#if defined(WIN32)
	if (shutdown(socket, SD_BOTH) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = closesocket(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#else
	if (shutdown(socket, SHUT_RDWR) == SOCKET_ERROR)
		Socket_error("shutdown", socket);
	if ((rc = close(socket)) == SOCKET_ERROR)
		Socket_error("close", socket);
#endif
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Close a socket and remove it from the select list.
 *  @param socket the socket to close
 *  @return completion code
 */
void Socket_close(int socket)
{
#if !defined(SINGLE_LISTENER)
	ListElement* current = NULL;
#endif
#if defined(USE_POLL)
	struct socket_info* si;
#endif

	FUNC_ENTRY;
#if defined(USE_POLL)
	/* have to call epoll_ctl DEL before closing the socket */
	if (epoll_ctl(s.epoll_fds, EPOLL_CTL_DEL, socket, NULL) != 0)
		Socket_error("epoll_ctl del", socket);
#endif

	Socket_close_only(socket);
	
#if !defined(USE_POLL)
	FD_CLR((u_int)socket, &(s.rset_saved));
	if (FD_ISSET((u_int)socket, &(s.pending_wset)))
		FD_CLR((u_int)socket, &(s.pending_wset));
	if (s.cur_clientsds != NULL && *(int*)(s.cur_clientsds->content) == socket)
		s.cur_clientsds = s.cur_clientsds->next;
	ListRemoveItem(s.connect_pending, &socket, intcompare);
	ListRemoveItem(s.write_pending, &socket, intcompare);
#else
	if ((si = TreeRemoveKey(s.fds_tree, &socket)) == NULL)
		Log(LOG_ERROR, 13, "Failed to remove socket %d", socket);
	else
		free(si);
	if (s.cur_sds < s.no_ready && ((struct socket_info*)s.events[s.cur_sds].data.ptr)->fd == socket)
		++s.cur_sds;
#endif
	SocketBuffer_cleanup(socket);
	Socket_removeNew(socket);

#if !defined(SINGLE_LISTENER)
	while (ListNextElement(s.listeners, &current))
	{
		if (ListRemoveItem( ((Listener*)current->content)->connections, &socket, intcompare))
		{
			Log(TRACE_MIN, 0, "Removed socket %d from listener %d", socket,((Listener*)current->content)->port);
			break;
		}
	}
#endif

#if !defined(USE_POLL)
	if (ListRemoveItem(s.clientsds, &socket, intcompare))
		Log(TRACE_MIN, 13, NULL, socket);
	else
		Log(TRACE_MIN, 34, NULL, socket);
	if (socket + 1 >= s.maxfdp1)
	{
		/* now we have to reset s.maxfdp1 */
		ListElement* cur_clientsds = NULL;

#if !defined(SINGLE_LISTENER)
		current = NULL;
		while (ListNextElement(s.listeners, &current))
		{
			Listener* list = (Listener*)(current->content);
			s.maxfdp1 = max(s.maxfdp1,list->socket);
		}
#else
		s.maxfdp1 = s.mySocket;
#endif
		while (ListNextElement(s.clientsds, &cur_clientsds))
			s.maxfdp1 = max(*((int*)(cur_clientsds->content)), s.maxfdp1);
		++(s.maxfdp1);
		/* Log(TRACE_MAX, 0, "Reset max fdp1 to %d\n", s.maxfdp1); */
	}
#endif
	FUNC_EXIT;
}


int Socket_new_udp(int* sock, int ipv6)
{
	short family = AF_INET;
	int type = SOCK_DGRAM;
	int rc = 0;

	FUNC_ENTRY;
	if (ipv6)
		family = AF_INET6;
	*sock =	socket(family, type, 0);
	if (*sock == INVALID_SOCKET)
		rc = Socket_error("socket", *sock);
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Create a new socket and TCP connect to an address/port
 *  @param addr the address string
 *  @param port the TCP port
 *  @param sock returns the new socket
 *  @return completion code
 */
int Socket_new(char* addr, int port, int* sock)
{
#if defined(MQTTS)
	return Socket_new_type(addr, port, sock, SOCK_STREAM);
}
int Socket_new_type(char* addr, int port, int* sock, int type)
{
#else
	int type = SOCK_STREAM;
#endif
	struct sockaddr_in address;
	struct sockaddr_in6 address6;
	int rc = 0;
#if defined(WIN32)
	int buflen;
	wchar_t buf[(INET6_ADDRSTRLEN+1)*2];
	short family;
#else
	sa_family_t family;
#endif

	FUNC_ENTRY;
	*sock = -1;

	if (addr[0] == '[')
	{
		#if defined(WIN32)
			buflen = sizeof(address6);
			mbstowcs(buf, &addr[1], sizeof(buf));
			rc = WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr*)&address6, &buflen);
		#else
			rc = inet_pton(AF_INET6, &addr[1], &address6.sin6_addr);
		#endif
		address6.sin6_port = htons(port);
		address6.sin6_family = family = AF_INET6;
	}
	else
	{
		#if defined(WIN32)
			buflen = sizeof(address);
			mbstowcs(buf, addr, sizeof(buf));
			rc = WSAStringToAddress(buf, AF_INET, NULL, (struct sockaddr*)&address, &buflen);
		#else
			rc = inet_pton(AF_INET, addr, &address.sin_addr);
		#endif
		address.sin_port = htons(port);
		address.sin_family = family = AF_INET;
	}

#if defined(WIN32)
	if (rc == SOCKET_ERROR)
		Socket_error("WSAStringToAddress", -1);
#else
	if (rc == SOCKET_ERROR)
		Socket_error("inet_pton", -1);
#endif

	if (rc == SOCKET_ERROR)
		Log(LOG_WARNING, 92, NULL, addr);
	else
	{
		*sock =	socket(family, type, 0);
		if (*sock == INVALID_SOCKET)
			rc = Socket_error("socket", *sock);
		else
		{
#if defined(USE_POLL)
			struct socket_info* si = NULL;
#endif
			
			Log(TRACE_MIN, 14, NULL, *sock, (family == AF_INET) ? addr : &addr[1], port);
#if defined(USE_POLL)
			if (Socket_addSocket(*sock, &si, 1) == SOCKET_ERROR)
#else
			if (Socket_addSocket(*sock, 1) == SOCKET_ERROR)
#endif
				rc = Socket_error("setnonblocking", *sock);
			else
			{
				/* this could complete immediately, even though we are non-blocking */
				if (family == AF_INET)
					rc = connect(*sock, (struct sockaddr*)&address, sizeof(address));
				else
					rc = connect(*sock, (struct sockaddr*)&address6, sizeof(address6));
				if (rc == SOCKET_ERROR)
					rc = Socket_error("connect", *sock);
				if (rc == EINPROGRESS || rc == EWOULDBLOCK)
				{
#if !defined(USE_POLL)
					int* pnewSd = (int*)malloc(sizeof(int));
					*pnewSd = *sock;
					ListAppend(s.connect_pending, pnewSd, sizeof(int));
#else
					si->connect_pending = 1;
					si->event.events = EPOLLOUT;
					if (epoll_ctl(s.epoll_fds, EPOLL_CTL_MOD, si->fd, &si->event) != 0)
						Socket_error("epoll ctl MOD", si->fd);
#endif
					Log(TRACE_MIN, 15, NULL);
				}
			}
		}
	}

	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 *  Continue any outstanding writes for a single socket
 *  @param socket socket with outstanding writes
 *  @return completion code
 */
int Socket_continueWrite(int socket)
{
	int rc = 0;
	pending_writes* pw;
	unsigned long curbuflen = 0L, /* cumulative total of buffer lengths */
		bytes;
	int curbuf = -1, i;
	iobuf iovecs1[5];

	FUNC_ENTRY;
	pw = SocketBuffer_getWrite(socket);

	for (i = 0; i < pw->count; ++i)
	{
		if (pw->bytes <= curbuflen)
		{ /* if previously written length is less than the buffer we are currently looking at,
				add the whole buffer */
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len;
			iovecs1[curbuf].iov_base = pw->iovecs[i].iov_base;
		}
		else if (pw->bytes < curbuflen + pw->iovecs[i].iov_len)
		{ /* if previously written length is in the middle of the buffer we are currently looking at,
				add some of the buffer */
			int offset = pw->bytes - curbuflen;
			iovecs1[++curbuf].iov_len = pw->iovecs[i].iov_len - offset;
			iovecs1[curbuf].iov_base = pw->iovecs[i].iov_base + offset;
			break;
		}
		curbuflen += pw->iovecs[i].iov_len;
	}

	if ((rc = Socket_writev(socket, iovecs1, curbuf+1, &bytes)) != SOCKET_ERROR)
	{
		pw->bytes += bytes;
		if ((rc = (pw->bytes == pw->total)))
		{  /* topic and payload buffers are freed elsewhere, when all references to them have been removed */
			free(pw->iovecs[0].iov_base);
			free(pw->iovecs[1].iov_base);
			if (pw->count == 5)
				free(pw->iovecs[3].iov_base);
			Log(TRACE_MIN, 0, "ContinueWrite: partial write now complete for socket %d", socket);
		}
		else
			Log(TRACE_MIN, 16, NULL, bytes, socket);
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


#if !defined(USE_POLL)
/**
 *  Continue any outstanding writes for a socket set
 *  @param pwset the set of sockets
 *  @return completion code
 */
int Socket_continueWrites(fd_set* pwset)
{
	int rc1 = 0;
	ListElement* curpending = s.write_pending->first;

	FUNC_ENTRY;
	while (curpending)
	{
		int socket = *(int*)(curpending->content);
		//printf("continueWrites socket %d\n", socket);
		if (FD_ISSET(socket, pwset) && Socket_continueWrite(socket))
		{
			if (!SocketBuffer_writeComplete(socket))
				Log(LOG_SEVERE, 35, NULL);
			FD_CLR(socket, &(s.pending_wset));
			if (!ListRemove(s.write_pending, curpending->content))
			{
				Log(LOG_SEVERE, 36, NULL);
				ListNextElement(s.write_pending, &curpending);
			}
			curpending = s.write_pending->current;
		}
		else
			ListNextElement(s.write_pending, &curpending);
	}
	FUNC_EXIT_RC(rc1);
	return rc1;
}
#endif

/**
 *  Get the hostname of the computer we are running on
 *  @return the hostname
 */
char* Socket_gethostname()
{
#if !defined(HOST_NAME_MAX)
	#define HOST_NAME_MAX 256 /* 255 is a POSIX limit/256 a Windows max for this call */
#endif
	static char buf[HOST_NAME_MAX+1] = "";
	gethostname(buf, HOST_NAME_MAX+1);
	return buf;
}


/**
 *  Get information about the other end connected to a socket
 *  @param sock the socket to inquire on
 *  @return the peer information
 */
char* Socket_getpeer(int sock)
{
	struct sockaddr_in6 sa;
	socklen_t sal = sizeof(sa);
	int rc;

	if ((rc = getpeername(sock, (struct sockaddr*)&sa, &sal)) == SOCKET_ERROR)
	{
		Socket_error("getpeername", sock);
		return "unknown";
	}

	return Socket_getaddrname((struct sockaddr*)&sa, sock);
}


/**
 *  Convert a numeric address to character string
 *  @param sa	socket numerical address
 *  @param sock socket
 *  @return the peer information
 */
char* Socket_getaddrname(struct sockaddr* sa, int sock)
{
/**
 * maximum length of the address string
 */
#define ADDRLEN INET6_ADDRSTRLEN+1
/**
 * maximum length of the port string
 */
#define PORTLEN 10
	static char addr_string[ADDRLEN + PORTLEN];

#if defined(WIN32)
	int buflen = ADDRLEN*2;
	wchar_t buf[ADDRLEN*2];
	if (WSAAddressToString(sa, sizeof(struct sockaddr_in6), NULL, buf, (LPDWORD)&buflen) == SOCKET_ERROR)
		Socket_error("WSAAddressToString", sock);
	else
		wcstombs(addr_string, buf, sizeof(addr_string));
	/* TODO: append the port information - format: [00:00:00::]:port */
	/* strcpy(&addr_string[strlen(addr_string)], "what?"); */
#else
	if (sa->sa_family == AF_INET)
	{
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		if (inet_ntop(AF_INET, &sin->sin_addr, addr_string, ADDRLEN) == NULL)
			Socket_error("inet_ntop", sock);
		sprintf(&addr_string[strlen(addr_string)], ":%d", ntohs(sin->sin_port));
	}
	else
	{
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)sa;
		if (inet_ntop(AF_INET6, &sin->sin6_addr, addr_string, ADDRLEN) == NULL)
			Socket_error("inet_ntop", sock);
		sprintf(&addr_string[strlen(addr_string)], ":%d", ntohs(sin->sin6_port));
	}
#endif
	return addr_string;
}


#if !defined(SINGLE_LISTENER)
/**
 * Returns the listener that the specified socket originally connected to.
 * In the case of UDP, sock will be the socket of the listener itself.
 * @param sock the socket to use in the search
 * @return the "parent" listener of the socket
 */
Listener* Socket_getParentListener(int sock)
{
	ListElement* current = NULL;

	FUNC_ENTRY;
	if (s.listeners->count == 1)
		current = s.listeners->first;
	else if (s.listeners)
		while (ListNextElement(s.listeners, &current))
		{
			Listener *listener = (Listener*)current->content;
			if (listener->socket == sock || ListFindItem(listener->connections, &sock, intcompare))
				break;
		}

	FUNC_EXIT;
	return (current == NULL) ? NULL : (Listener*)current->content;
}


/**
 * Create and initialize a new listener structure
 * @return pointer to the new structure
 */
Listener* Socket_new_listener()
{
	Listener* l = malloc(sizeof(Listener));

	FUNC_ENTRY;
	memset(l, '\0', sizeof(Listener));
	l->port = 1883;
	l->max_connections = -1;
	l->protocol = PROTOCOL_MQTT;
	l->connections = ListInitialize();
#if defined(MQTTS)
	l->multicast_groups = ListInitialize();
#endif

	FUNC_EXIT;
	return l;
}
#endif

/**
 * Get access to the socket module control struture for other modules
 * @return pointer to the socket strcucture
 */
Sockets* Socket_getSockets()
{
	return &(s);
}


#if defined(TCPSOCKET_TEST)

int main(int argc, char *argv[])
{
	Socket_connect("127.0.0.1", 1883);
	Socket_connect("localhost", 1883);
	Socket_connect("loadsadsacalhost", 1883);
}

#endif
