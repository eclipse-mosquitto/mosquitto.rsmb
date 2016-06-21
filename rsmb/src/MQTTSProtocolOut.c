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

#if !defined(NO_BRIDGE) && defined(MQTTS)

#include "MQTTSProtocolOut.h"
#include "Clients.h"
#include "Bridge.h"
#include "Protocol.h"
#include "MQTTProtocolClient.h"
#include "StackTrace.h"

#include <stdlib.h>
#if defined(WIN32)
#include <Iphlpapi.h>
#else
#include <sys/ioctl.h>
#include <net/if.h>
#endif

#include "Heap.h"

extern BrokerStates* bstate;

char* MQTTProtocol_addressPort(char* ip_address, int* port);
void Bridge_subscribe(BridgeConnections* bc, Clients* client);


Clients* MQTTSProtocol_create_multicast(char* ip_address, char* clientID, int loopback)
{ /* outgoing connection */
	int i, port;
	Clients* newc = NULL;
	char* intface = NULL;
	int ipv6 = 0;

	FUNC_ENTRY;
	newc = malloc(sizeof(Clients));
	memset(newc, '\0', sizeof(Clients));
	newc->clientID = clientID;
	newc->outbound = 1;
	newc->good = 1;
	newc->connected = 1;
	newc->outboundMsgs = ListInitialize();
	newc->inboundMsgs = ListInitialize();
	for (i = 0; i < PRIORITY_MAX; ++i)
		newc->queuedMsgs[i] = ListInitialize();
	newc->registrations = ListInitialize();

	newc->protocol = PROTOCOL_MQTTS_MULTICAST;

	/* if there is a space in the ip_address string, it means we have address plus interface specified */
	if ((intface = strchr(ip_address, ' ')) != NULL)
	{
		*intface = '\0';
		++intface;
	}

	MQTTProtocol_addressPort(ip_address, &port);
	newc->addr = malloc(strlen(ip_address) + 1);
	strcpy(newc->addr, ip_address);

	ipv6 = (newc->addr[0] == '[');

	Socket_new_udp(&(newc->socket), ipv6);

	if (setsockopt(newc->socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback)) == SOCKET_ERROR)
		Socket_error("set bridge IP_MULTICAST_LOOP", newc->socket);

	if (intface)
	{
		if (ipv6)
		{
			int index = 0;

			if ((index = if_nametoindex(intface)) == 0)
				Socket_error("get interface index", newc->socket);
			else if (setsockopt(newc->socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&index, sizeof(index)) == SOCKET_ERROR)
				Socket_error("set bridge IP_MULTICAST_IF", newc->socket);
		}
		else
		{
			struct in_addr interface_addr;
#if defined(WIN32)
			if ((rc = win_inet_pton(AF_INET, intface, &interface_addr)) == SOCKET_ERROR)
				Socket_error("WSAStringToAddress interface", newc->socket);
			else
			{
#else
			/* get address of the interface */
			struct ifreq ifreq;

			strncpy(ifreq.ifr_name, intface, IFNAMSIZ);
			if (ioctl(newc->socket, SIOCGIFADDR, &ifreq) == SOCKET_ERROR)
				Socket_error("get interface address", newc->socket);
			else
			{
				memcpy(&interface_addr, &((struct sockaddr_in *)&ifreq.ifr_addr)->sin_addr, sizeof(struct in_addr));
#endif
				if (setsockopt(newc->socket, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) == SOCKET_ERROR)
					Socket_error("set bridge IP_MULTICAST_IF", newc->socket);
			}
		}
	}

	TreeAdd(bstate->disconnected_mqtts_clients, newc, sizeof(Clients) + strlen(newc->clientID)+1 + 3*sizeof(List));

	FUNC_EXIT;
	return newc;
}


Clients* MQTTSProtocol_connect(char* ip_address, char* clientID, int cleansession, int try_private, int keepalive, willMessages* willMessage)
{ /* outgoing connection */
	int rc, port;
	char* addr;
	Clients* newc = NULL;

	FUNC_ENTRY;
	newc = TreeRemoveKeyIndex(bstate->disconnected_clients, clientID, 1); /* must be a dummy client */
	if (newc == NULL)
		newc = TreeRemoveKeyIndex(bstate->clients, clientID, 1);
	if (newc)
	{
		free(clientID);
		newc->connected = newc->ping_outstanding = newc->connect_state = newc->msgID = newc->discardedMsgs = 0;
	}
	else
	{
		int i;
		newc = malloc(sizeof(Clients));
		memset(newc, '\0', sizeof(Clients));
		newc->outboundMsgs = ListInitialize();
		newc->inboundMsgs = ListInitialize();
		for (i = 0; i < PRIORITY_MAX; ++i)
			newc->queuedMsgs[i] = ListInitialize();
		newc->clientID = clientID;
	}
	newc->cleansession = cleansession;
	newc->outbound = newc->good = 1;
	newc->keepAliveInterval = keepalive;
	newc->registrations = ListInitialize();
	newc->will = willMessage;
	newc->noLocal = try_private; /* try private connection first */
	time(&(newc->lastContact));
	newc->pendingRegistration = NULL;
	newc->protocol = PROTOCOL_MQTTS;

	addr = MQTTProtocol_addressPort(ip_address, &port);

	newc->addr = malloc(strlen(ip_address));
	strcpy(newc->addr, ip_address);

	rc = Socket_new_type(addr, port, &(newc->socket), SOCK_DGRAM);

	if (rc == EINPROGRESS || rc == EWOULDBLOCK)
		newc->connect_state = 1; /* UDP connect called - improbable, but possible on obscure platforms */
	else if (rc == 0)
	{
		newc->connect_state = 2;
		rc = MQTTSPacket_send_connect(newc);
	}
	TreeAdd(bstate->clients, newc, sizeof(Clients) + strlen(newc->clientID)+1 + 3*sizeof(List));

	FUNC_EXIT;
	return newc;
}


void MQTTSProtocol_reconnect(char* ip_address, Clients* client)
{
	int port, rc;
	char *address = NULL;
	Clients* oldc = NULL;

	FUNC_ENTRY;
	oldc = TreeRemoveKeyIndex(bstate->disconnected_clients, client->clientID, 1);
	if (oldc == NULL)
		oldc = TreeRemoveKeyIndex(bstate->clients, client->clientID, 1);

	client->good = 1;
	client->ping_outstanding = 0;
	client->connect_state = 0;
	client->connected = 0;
	if (client->cleansession)
		client->msgID = 0;

	if (client->addr)
		free(client->addr);

	client->addr = malloc(strlen(ip_address));
	strcpy(client->addr, ip_address);

	address = MQTTProtocol_addressPort(ip_address, &port);
	rc = Socket_new_type(address, port, &(client->socket), SOCK_DGRAM);
	if (rc == EINPROGRESS || rc == EWOULDBLOCK)
		client->connect_state = 1; /* UDP connect called - improbable, but possible on obscure platforms */
	else if (rc == 0)
	{
		client->connect_state = 2; // TCP connect completed, in which case send the MQTT connect packet
		MQTTSPacket_send_connect(client);
		time(&(client->lastContact));
	}
	TreeAdd(bstate->clients, client, sizeof(Clients) + strlen(client->clientID)+1 + 3*sizeof(List));
	FUNC_EXIT;
}



int MQTTSProtocol_handleConnacks(void* pack, int sock, char* clientAddr, Clients* client)
{
	MQTTS_Connack* connack = (MQTTS_Connack*)pack;
	int rc = 0;
	BridgeConnections* bc = NULL;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 41, NULL, sock, clientAddr, client ? client->clientID : "",
			connack->returnCode);

	if ((bc = Bridge_getBridgeConnection(sock)) == NULL)
	{
		rc = SOCKET_ERROR;
		goto exit;
	}

	if (bc->primary && bc->primary->socket == sock)
		client = bc->primary;
	else
		client = bc->backup;

	if (connack->returnCode != MQTTS_RC_ACCEPTED)
	{
		if (client)
		{
			if (client->noLocal == 1)
				client->noLocal = 0;
			else
				Log(LOG_WARNING, 132, NULL, connack->returnCode, client->clientID);
			MQTTProtocol_closeSession(client, 0);
		}
		else
			Socket_close(sock);
	}
	else if (client)
	{
		ListElement* addr = bc->cur_address;

		if (client == bc->primary && bc->round_robin == 0)
			addr = bc->addresses->first;
		Log(LOG_INFO, 133, NULL, bc->name, (char*)(addr->content));

		client->connect_state = 3;  //should have been 2 before
		bc->last_connect_result = connack->returnCode;
		(bc->no_successful_connections)++;
		client->connected = 1;
		client->good = 1;
		client->ping_outstanding = 0;
		time(&(client->lastContact));

		if (client->will)
		{
			MQTTS_Publish pub;

			MQTTProtocol_sys_publish(client->will->topic, "1");
			memset(&pub, '\0', sizeof(MQTTS_Publish));
			pub.header.type = MQTTS_PUBLISH;
			pub.data = "1";
			pub.dataLen = 1;
			pub.shortTopic = client->will->topic;
			pub.header.len = 7 + pub.dataLen;
			//MQTTProtocol_startOrQueuePublish(client, &pub, 0, client->will->retained, &m);
			MQTTSPacket_send_publish(client, &pub);
		}


		if (client == bc->primary &&
			(bc->backup && (bc->backup->connected == 1 || bc->backup->connect_state != 0)))
		{
			Log(LOG_INFO, 134, NULL, (char*)(bc->cur_address->content));
			MQTTProtocol_closeSession(bc->backup, 0);
		}

		//if (bc->addresses->count > 1 || bc->no_successful_connections == 1)
		Bridge_subscribe(bc, client);
	}

	//Clients* client = Protocol_getclientbyaddr(clientAddr);

	MQTTSPacket_free_packet(pack);

exit:
	FUNC_EXIT_RC(rc);
	return rc;
}



int MQTTSProtocol_handleWillTopicReqs(void* pack, int sock, char* clientAddr, Clients* client)
{
	FUNC_ENTRY;
	//client = Bridge_getClient(sock);
	Log(LOG_PROTOCOL, 43, NULL, client->socket, client->addr, client->clientID);
	if (client == NULL)
	{
		//TODO: what now?
	}
	else
		MQTTSPacket_send_willTopic(client);

	MQTTSPacket_free_packet(pack);
  	FUNC_EXIT;
  	return 0;
}


int MQTTSProtocol_handleWillMsgReqs(void* pack, int sock, char* clientAddr, Clients* client)
{
	FUNC_ENTRY;
	//client = Bridge_getClient(sock);
	Log(LOG_PROTOCOL, 47, NULL, client->socket, client->addr, client->clientID);
	if (client == NULL)
	{
		//TODO: what now?
	}
	else
		MQTTSPacket_send_willMsg(client);

	MQTTSPacket_free_packet(pack);
  	FUNC_EXIT;
  	return 0;
}


int MQTTSProtocol_handleSubacks(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_SubAck* suback = (MQTTS_SubAck*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 69, NULL, client->socket, client->addr, client->clientID, suback->msgId, suback->topicId,
			suback->returnCode);
	if (client->pendingSubscription == NULL ||
			suback->msgId != client->pendingSubscription->msgId)
	{
		/* TODO: unexpected suback*/
	}
	else if (suback->returnCode != MQTTS_RC_ACCEPTED)
	{
		/* TODO: rejected ack... what do we do now? */
	}
	else
	{
		if (suback->topicId > 0)
		{
			Registration* reg = malloc(sizeof(Registration));
			reg->topicName = client->pendingSubscription->topicName;
			reg->id = suback->topicId;
			ListAppend(client->registrations, reg, sizeof(reg) + strlen(reg->topicName)+1);
		}
		else
			free(client->pendingSubscription->topicName);
		free(client->pendingSubscription);
		client->pendingSubscription = NULL;
		/* TODO: could proactively call Bridge_subscribe() */
	}

	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_startSubscription(Clients* client, char* topic, int qos)
{
	PendingSubscription* pendingSub = malloc(sizeof(PendingSubscription));
	int msgId = MQTTProtocol_assignMsgId(client);
	int rc = 0;

	FUNC_ENTRY;
	pendingSub->msgId = msgId;
	pendingSub->topicName = topic;
	pendingSub->qos = qos;
	time(&(pendingSub->sent));
	client->pendingSubscription = pendingSub;
	rc = MQTTSPacket_send_subscribe(client, topic, qos, msgId);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_startClientRegistration(Clients* client, char* topic)
{
	PendingRegistration* pendingReg = malloc(sizeof(PendingRegistration));
	Registration* reg;
	int msgId = MQTTProtocol_assignMsgId(client);
	char* regTopicName = malloc(strlen(topic)+1);
	int rc = 0;

	FUNC_ENTRY;
	strcpy(regTopicName,topic);

	reg = malloc(sizeof(Registration));
	reg->topicName = regTopicName;
	reg->id = 0;

	pendingReg->msgId = msgId;
	pendingReg->registration = reg;
	time(&(pendingReg->sent));
	client->pendingRegistration = pendingReg;
	rc = MQTTSPacket_send_register(client, reg->id, regTopicName, msgId);
	FUNC_EXIT_RC(rc);
	return rc;
}

#endif
