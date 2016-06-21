/*******************************************************************************
 * Copyright (c) 2009, 2013 IBM Corp.
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
 *    Ian Craggs - fix for bug #424692
 *******************************************************************************/

#if defined(MQTTS)

#include "MQTTSProtocol.h"
#include "MQTTSPacket.h"
#include "MQTTProtocol.h"
#include "Socket.h"
#include "Topics.h"
#include "Log.h"
#include "Messages.h"
#include "Protocol.h"
#include "StackTrace.h"

#if !defined(NO_BRIDGE)
#include "MQTTSProtocolOut.h"
#endif


#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/timeb.h>

#include "Heap.h"

void MQTTProtocol_removePublication(Publications* p);


typedef int (*pf)(void*, int, char* clientAddr, Clients* client);

#if !defined(NO_BRIDGE)
static pf handle_packets[] =
{
		MQTTSProtocol_handleAdvertises,
		MQTTSProtocol_handleSearchGws,
		MQTTSProtocol_handleGwInfos,
		NULL,
		MQTTSProtocol_handleConnects,
		MQTTSProtocol_handleConnacks,
		MQTTSProtocol_handleWillTopicReqs,
		MQTTSProtocol_handleWillTopics,
		MQTTSProtocol_handleWillMsgReqs,
		MQTTSProtocol_handleWillMsgs,
		MQTTSProtocol_handleRegisters,
		MQTTSProtocol_handleRegacks,
		MQTTSProtocol_handlePublishes,
		MQTTSProtocol_handlePubacks,
		MQTTSProtocol_handlePubcomps,
		MQTTSProtocol_handlePubrecs,
		MQTTSProtocol_handlePubrels,
		NULL,
		MQTTSProtocol_handleSubscribes,
		MQTTSProtocol_handleSubacks,
		MQTTSProtocol_handleUnsubscribes,
		MQTTSProtocol_handleUnsubacks,
		MQTTSProtocol_handlePingreqs,
		MQTTSProtocol_handlePingresps,
		MQTTSProtocol_handleDisconnects,
		NULL,
		MQTTSProtocol_handleWillTopicUpds,
		MQTTSProtocol_handleWillTopicResps,
		MQTTSProtocol_handleWillMsgUpds,
		MQTTSProtocol_handleWillMsgResps
};
#else
static pf handle_packets[] =
{
		MQTTSProtocol_handleAdvertises,
		MQTTSProtocol_handleSearchGws,
		MQTTSProtocol_handleGwInfos,
		NULL,
		MQTTSProtocol_handleConnects,
		NULL, /* connack  */
		NULL, /* willtopicreq */
		MQTTSProtocol_handleWillTopics,
		NULL, /* willmsgreq */
		MQTTSProtocol_handleWillMsgs,
		MQTTSProtocol_handleRegisters,
		MQTTSProtocol_handleRegacks,
		MQTTSProtocol_handlePublishes,
		MQTTSProtocol_handlePubacks,
		MQTTSProtocol_handlePubcomps,
		MQTTSProtocol_handlePubrecs,
		MQTTSProtocol_handlePubrels,
		NULL,
		MQTTSProtocol_handleSubscribes,
		NULL, /* subacks */
		MQTTSProtocol_handleUnsubscribes,
		MQTTSProtocol_handleUnsubacks,
		MQTTSProtocol_handlePingreqs,
		MQTTSProtocol_handlePingresps,
		MQTTSProtocol_handleDisconnects,
		NULL,
		MQTTSProtocol_handleWillTopicUpds,
		MQTTSProtocol_handleWillTopicResps,
		MQTTSProtocol_handleWillMsgUpds,
		MQTTSProtocol_handleWillMsgResps
};
#endif

BrokerStates* bstate;


int registeredTopicNameCompare(void*a, void* b)
{
	Registration* reg = (Registration*)a;

	return strcmp(reg->topicName, (char*)b) == 0;
}


int registeredTopicIdCompare(void*a, void* b)
{
	Registration* reg = (Registration*)a;

	return reg->id == *(int*)b;
}


int MQTTSProtocol_initialize(BrokerStates* aBrokerState)
{
	bstate = aBrokerState;

	return MQTTSPacket_initialize(bstate);
}

/**
 * Shutdown the MQTT protocol modules.
 * @param terminate boolean flag to indicate whether the clients should all be closed
 */
void MQTTSProtocol_terminate()
{
	FUNC_ENTRY;
	Log(LOG_INFO, 301, NULL);
	MQTTProtocol_shutdownclients(bstate->mqtts_clients, 1);
	MQTTProtocol_shutdownclients(bstate->disconnected_mqtts_clients, 1);
	MQTTSPacket_terminate();
	FUNC_EXIT;
}


/**
 * MQTTs protocol advertise processing.
 */
void MQTTSProtocol_housekeeping()
{
	ListElement* current = NULL;
	time_t now = 0;

	FUNC_ENTRY;
	/* for each listener, if the advertise parameter is set and the interval has expired,
	 * call send_advertise
	 */
	while (ListNextElement(bstate->listeners, &current))
	{
		Listener *listener = (Listener*)current->content;
		if (listener->advertise)
		{
			if (now == 0)
				time(&(now));
			if (difftime(now, listener->advertise->last) > listener->advertise->interval)
			{
				MQTTSPacket_send_advertise(listener->socket,
						listener->advertise->address, listener->advertise->gateway_id,
						listener->advertise->interval);
				listener->advertise->last = now;
			}
		}
	}
	FUNC_EXIT;
}


void MQTTSProtocol_timeslice(int sock)
{
	int error;
	MQTTS_Header* pack = NULL;
	char* clientAddr = NULL;
	Clients* client = NULL;
	struct sockaddr_in6 from;

	FUNC_ENTRY;
	pack = MQTTSPacket_Factory(sock, &clientAddr, (struct sockaddr *)&from, &error);

	if (clientAddr)
		client = Protocol_getclientbyaddr(clientAddr);

#if !defined(NO_BRIDGE)
	if (client == NULL)
		client = Protocol_getoutboundclient(sock);
#endif

	if (pack == NULL)
	{
		if (error == SOCKET_ERROR || error == UDPSOCKET_INCOMPLETE)
		{
			if (client != NULL)
			{
				client->good = 0; /* make sure we don't try and send messages to ourselves */
				//client->connected = 0;
				if (error == SOCKET_ERROR)
					Log(LOG_WARNING, 18, NULL, client->clientID, client->socket,
							Socket_getpeer(client->socket));
				else
					Log(LOG_WARNING, 19, NULL, client->clientID, client->socket,
							Socket_getpeer(client->socket));
				MQTTProtocol_closeSession(client, 0);
			}
			else
			{
				if (error == SOCKET_ERROR)
					Log(LOG_WARNING, 20, NULL, sock, Socket_getpeer(sock));
				else
					Log(LOG_WARNING, 21, NULL, sock, Socket_getpeer(sock));
				/*Socket_close(sock);*/
			}
		}
	}
	else if (handle_packets[(int)pack->header.type] == NULL)
	{
			Log(LOG_WARNING, 22, NULL, pack->header.type, handle_packets[(int)pack->header.type]);
			MQTTSPacket_free_packet(pack);
	}
	else if (client == NULL &&  pack->header.type != MQTTS_CONNECT &&
			pack->header.type != MQTTS_ADVERTISE && pack->header.type != MQTTS_SEARCHGW &&
			(pack->header.type != MQTTS_PUBLISH || ((MQTTS_Publish*)pack)->flags.QoS != 3))
	{
			Log(LOG_WARNING, 23, NULL, sock, Socket_getpeer(sock), MQTTSPacket_name(pack->header.type));
			MQTTSPacket_free_packet(pack);
	}
	else
	{
		(*handle_packets[(int)pack->header.type])(pack, sock, clientAddr, client);
		/* TODO:
		 *  - error handling
		 *  - centralise calls to time( &(c->lastContact) ); (currently in each _handle* function
		 */
	}
	FUNC_EXIT;
}


int MQTTSProtocol_handleAdvertises(void* pack, int sock, char* clientAddr, Clients* client)
{
	MQTTS_Advertise* advertisePack = (MQTTS_Advertise*)pack;
	Listener* listener = NULL;
	char* topic = NULL;
	char* data = NULL;
	int offset = 0;
	struct timeb ts;
	struct tm *timeinfo;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 31, NULL, sock, "", clientAddr, advertisePack->gwId, advertisePack->duration);

	ftime(&ts);
	timeinfo = localtime(&ts.time);

	listener = Socket_getParentListener(sock);

	topic = malloc(40);
	sprintf(topic, "$SYS/broker/mqtts/listener/%d", listener->port);

	data = malloc(80);
	offset = strftime(data, 60, "%Y%m%d %H%M%S ", timeinfo);
	sprintf(&data[offset], "advertise from %s gateway id: %d duration: %d",
			clientAddr, advertisePack->gwId, advertisePack->duration);

	MQTTSPacket_free_packet(pack);
	MQTTProtocol_sys_publish(topic, data);

	free(topic);
	free(data);
	FUNC_EXIT;
	return 0;
}


int MQTTSProtocol_handleSearchGws(void* pack, int sock, char* clientAddr, Clients* client)
{
	return 0;
}


int MQTTSProtocol_handleGwInfos(void* pack, int sock, char* clientAddr, Clients* client)
{
	return 0;
}


int MQTTSProtocol_handleConnects(void* pack, int sock, char* clientAddr, Clients* client)
{
	MQTTS_Connect* connect = (MQTTS_Connect*)pack;
	Listener* list = NULL;
	int terminate = 0;
	Node* elem = NULL;
	int rc = 0;
	int existingClient = 0;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 39, NULL, sock, clientAddr, client ? client->clientID : "", connect->flags.cleanSession);

	if (bstate->clientid_prefixes->count > 0 &&
		!ListFindItem(bstate->clientid_prefixes, connect->clientID, clientPrefixCompare))
	{
		Log(LOG_WARNING, 31, NULL, connect->clientID);
		terminate = 1;
	}
	else
	{
		list = Socket_getParentListener(sock);
		if (list->max_connections > -1 &&
				list->connections->count > list->max_connections)
		{
			/* TODO: why is this commented out? delete if not needed
			//MQTTPacket_send_connack(3, sock);
			*/
			Log(LOG_WARNING, 141, NULL, connect->clientID, list->max_connections, list->port);
			terminate = 1;

		}
		else if (connect->protocolID != 1)
		{
			Log(LOG_WARNING, 32, NULL, "MQTT-S", connect->protocolID);
			/* TODO: why is this commented out? delete if not needed
			//MQTTPacket_send_connack(1, sock);
			 */
			terminate = 1;
		}
	}

	if (terminate)
	{
		/*TODO: process the terminate*/
		MQTTSPacket_free_packet(pack);
		goto exit;
	}

	if (client != NULL && !strcmp(client->clientID, connect->clientID))
	{
		/* Connect for a new client id on a used addr
		 * TODO: clean out 'old' Client (that may be 'connected')
		 */
	}

	elem = TreeFindIndex(bstate->mqtts_clients, connect->clientID, 1);
	if (elem == NULL)
	{
		client = TreeRemoveKey(bstate->disconnected_mqtts_clients, connect->clientID);
		if (client == NULL) /* this is a totally new connection */
		{
			int i;
		
			client = malloc(sizeof(Clients));
			memset(client, '\0', sizeof(Clients));
			client->protocol = PROTOCOL_MQTTS;
			client->outboundMsgs = ListInitialize();
			client->inboundMsgs = ListInitialize();
			for (i = 0; i < PRIORITY_MAX; ++i)
				client->queuedMsgs[i] = ListInitialize();
			client->registrations = ListInitialize();
			client->noLocal = 0; /* (connect->version == PRIVATE_PROTOCOL_VERSION) ? 1 : 0; */
			client->clientID = connect->clientID;
			connect->clientID = NULL; /* don't want to free this space as it is being used in the clients tree below */
		}
		else /* there is an existing disconnected client */
		{
			free(client->addr);
			client->connect_state = 0;
			client->connected = 0; /* Do not connect until we know the connack has been sent */
		}
		client->good = 1; /* good is set to 0 in disconnect, so we need to reset it here */
		client->keepAliveInterval = connect->keepAlive;
		client->cleansession = connect->flags.cleanSession;
		client->socket = sock;
		client->addr = malloc(strlen(clientAddr)+1);
		strcpy(client->addr, clientAddr);
		TreeAdd(bstate->mqtts_clients, client, sizeof(Clients) + strlen(client->clientID)+1 + 3*sizeof(List));

		if (client->cleansession)
			MQTTProtocol_removeAllSubscriptions(client->clientID); /* clear any persistent subscriptions */

		if (connect->flags.will)
		{
			client->connect_state = 1;
			rc = MQTTSPacket_send_willTopicReq(client);
		}
		else
		{
			client->connected = 1;
			rc = MQTTSPacket_send_connack(client, 0); /* send response */
		}
	}
	else
	{
		client = (Clients*)(elem->content);
		if (client->connected)
		{
			Log(LOG_INFO, 34, NULL, connect->clientID, clientAddr);
			if (client->socket != sock)
				Socket_close(client->socket);
		}
		client->socket = sock;
		client->connected = 0; /* Do not connect until we know the connack has been sent */
		client->connect_state = 0;
		client->good = 1;
		if (client->addr != NULL)
			free(client->addr);
		client->addr = malloc(strlen(clientAddr)+1);
		strcpy(client->addr, clientAddr);

		client->cleansession = connect->flags.cleanSession;
		if (client->cleansession)
		{
			int i;
			MQTTProtocol_removeAllSubscriptions(client->clientID);
			/* empty pending message lists */
			MQTTProtocol_emptyMessageList(client->outboundMsgs);
			MQTTProtocol_emptyMessageList(client->inboundMsgs);
			for (i = 0; i < PRIORITY_MAX; ++i)
				MQTTProtocol_emptyMessageList(client->queuedMsgs[i]);
			MQTTProtocol_clearWill(client);
		}
		/* registrations are always cleared */
		MQTTSProtocol_emptyRegistrationList(client->registrations);
		
		/* have to remove and re-add client so it is in the right order for new socket */
		if (client->socket != sock)
		{
			TreeRemoveNodeIndex(bstate->mqtts_clients, elem, 1);
			TreeRemoveKeyIndex(bstate->mqtts_clients, &client->socket, 0);
			client->socket = sock;
			TreeAdd(bstate->mqtts_clients, client, sizeof(Clients) + strlen(client->clientID)+1 + 3*sizeof(List));
		}

		client->keepAliveInterval = connect->keepAlive;
		client->pendingRegistration = NULL;
#if !defined(NO_BRIDGE)
		client->pendingSubscription = NULL;
#endif

		if (connect->flags.will)
		{
			client->connect_state = 1;
			rc = MQTTSPacket_send_willTopicReq(client);
		}
		else
		{
			client->connected = 1;
			rc = MQTTSPacket_send_connack(client,0); /* send response */
		}
	}
	
	if (existingClient)
		MQTTProtocol_processQueued(client);

	Log(LOG_INFO, 0, "Client connected to udp port %d from %s (%s)", list->port, client->clientID, clientAddr);

	MQTTSPacket_free_packet(pack);
	time( &(client->lastContact) );
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleWillTopics(void* pack, int sock, char* clientAddr, Clients* client)
{
	MQTTS_WillTopic* willTopic = (MQTTS_WillTopic*)pack;
	int rc = 0;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 45, NULL, sock, clientAddr, client->clientID,
				willTopic->flags.QoS, willTopic->flags.retain, willTopic->willTopic);
	if (client->connect_state == 1)
	{
		MQTTProtocol_setWillTopic(client, willTopic->willTopic, willTopic->flags.retain, willTopic->flags.QoS);
		willTopic->willTopic = NULL;
		client->connect_state = 2;
		rc = MQTTSPacket_send_willMsgReq(client);
	}
	MQTTSPacket_free_packet(pack);
	time( &(client->lastContact) );
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleWillMsgs(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_WillMsg* willMsg = (MQTTS_WillMsg*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 49, NULL, sock, clientAddr, client->clientID, willMsg->willMsg);
	if (client->connect_state == 2)
	{
		MQTTProtocol_setWillMsg(client, willMsg->willMsg);
		willMsg->willMsg = NULL;
		client->connect_state = 0;
		client->connected = 1;
		rc = MQTTSPacket_send_connack(client, 0); /* send response */
	}
	MQTTSPacket_free_packet(pack);
	time( &(client->lastContact) );
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleRegisters(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_Register* registerPack = (MQTTS_Register*)pack;
	ListElement* elem = NULL;
	int topicId = 0;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 51, NULL, sock, clientAddr, client ? client->clientID : "",
			registerPack->msgId, registerPack->topicId, registerPack->topicName);
	if ((elem = ListFindItem(client->registrations, registerPack->topicName, registeredTopicNameCompare)) == NULL)
	{
		topicId = (MQTTSProtocol_registerTopic(client, registerPack->topicName))->id;
		registerPack->topicName = NULL;
	}
	else
		topicId = ((Registration*)(elem->content))->id;

	rc = MQTTSPacket_send_regAck(client, registerPack->msgId, topicId, MQTTS_RC_ACCEPTED);
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleRegacks(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_RegAck* regack = (MQTTS_RegAck*)pack;

	FUNC_ENTRY;
	if (client->pendingRegistration == NULL ||
			regack->msgId != client->pendingRegistration->msgId)
	{
		/* unexpected regack*/
	}
	else if (!client->outbound)
	{
		if (regack->topicId != client->pendingRegistration->registration->id)
		{
			/* unexpected regack*/
		}
		else if (regack->returnCode != MQTTS_RC_ACCEPTED)
		{
			/* rejected ack... what do we do now? */
		}
		else
		{
			free(client->pendingRegistration);
			client->pendingRegistration = NULL;
			rc = MQTTProtocol_processQueued(client);
		}
	}
	else /* outbound client */
	{
		if (regack->returnCode != MQTTS_RC_ACCEPTED)
		{
			/* rejected ack... what do we do now? */
		}
		else
		{
			Registration* reg = client->pendingRegistration->registration;
			free(client->pendingRegistration);
			client->pendingRegistration = NULL;
			reg->id = regack->topicId;
			ListAppend(client->registrations, reg, sizeof(reg) + strlen(reg->topicName)+1);
			rc = MQTTProtocol_processQueued(client);
		}

	}

	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePublishes(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	char* topicName = NULL;
	MQTTS_Publish* pub = NULL;

	FUNC_ENTRY;
	pub = (MQTTS_Publish*)pack;
	Log(LOG_PROTOCOL, 55, NULL, sock, clientAddr, client ? client->clientID : "",
			(pub->flags.QoS == 1 || pub->flags.QoS == 2) ? pub->msgId : 0,
			(pub->flags.QoS == 3) ? -1: pub->flags.QoS,
			pub->flags.retain);

	if (client != NULL && pub->topicId != 0) /*TODO: pre registered */
	{
		/* copy the topic name as it will be freed later */
		char* name = MQTTSProtocol_getRegisteredTopicName(client, pub->topicId);
		if (name)
		{
			topicName = malloc(strlen(name) + 1);
			strcpy(topicName, name);
		}
	}
	else if (pub->shortTopic != NULL)
	{
		topicName = pub->shortTopic;
		pub->shortTopic = NULL; /* will be freed in Protocol_handlePublishes */
	}

	if (topicName == NULL)
	{
		/* TODO: unrecognized topic */
		/* send back nack */
	}
	else
	{
		Publish* publish = malloc(sizeof(Publish));
		publish->header.bits.type = PUBLISH;
		publish->header.bits.qos = pub->flags.QoS;
		publish->header.bits.retain = pub->flags.retain;
		publish->header.bits.dup = pub->flags.dup;
		publish->msgId = pub->msgId;
		publish->payload = pub->data;
		publish->payloadlen = pub->dataLen;
		publish->topic = topicName;
		rc = Protocol_handlePublishes(publish, sock, client, client ? client->clientID : clientAddr);
	}

	if (client != NULL)
		time( &(client->lastContact) );

	MQTTSPacket_free_packet(pack);

	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePubacks(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_PubAck* puback = (MQTTS_PubAck*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 57, NULL, sock, clientAddr, client ? client->clientID : "", puback->msgId);

	/* look for the message by message id in the records of outbound messages for this client */
	if (ListFindItem(client->outboundMsgs, &(puback->msgId), messageIDCompare) == NULL)
		Log(LOG_WARNING, 50, NULL, "PUBACK", client->clientID, puback->msgId);
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 1)
			Log(LOG_WARNING, 51, NULL, "PUBACK", client->clientID, puback->msgId, m->qos);
		else
		{
			Log(TRACE_MAX, 4, NULL, client->clientID, puback->msgId);
			MQTTProtocol_removePublication(m->publish);
			ListRemove(client->outboundMsgs, m);
			/* TODO: msgs counts */
			/* (++state.msgs_sent);*/
		}
	}
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePubcomps(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_PubComp* pubcomp = (MQTTS_PubComp*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 59, NULL, sock, clientAddr, client ? client->clientID : "", pubcomp->msgId);

	/* look for the message by message id in the records of outbound messages for this client */
	if (ListFindItem(client->outboundMsgs, &(pubcomp->msgId), messageIDCompare) == NULL)
	{
		/* No Dupe flag in MQTTs
		if (pubcomp->header.dup == 0)
		*/
		Log(LOG_WARNING, 50, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
	}
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 2)
			Log(LOG_WARNING, 51, NULL, "PUBCOMP", client->clientID, pubcomp->msgId, m->qos);
		else
		{
			if (m->nextMessageType != PUBCOMP)
				Log(LOG_WARNING, 52, NULL, "PUBCOMP", client->clientID, pubcomp->msgId);
			else
			{
				Log(TRACE_MAX, 5, NULL, client->clientID, pubcomp->msgId);
				MQTTProtocol_removePublication(m->publish);
				ListRemove(client->outboundMsgs, m);
				/* TODO: msgs counts */
				/*(++state.msgs_sent); */
			}
		}
	}
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePubrecs(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_PubRec* pubrec = (MQTTS_PubRec*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 15, NULL, pubrec->msgId, client->clientID);

	/* look for the message by message id in the records of outbound messages for this client */
	client->outboundMsgs->current = NULL;
	if (ListFindItem(client->outboundMsgs, &(pubrec->msgId), messageIDCompare) == NULL)
	{
		/* No Dupe flag in MQTTs
		if (pubrec->header.dup == 0)
		*/
		Log(LOG_WARNING, 50, NULL, "PUBREC", client->clientID, pubrec->msgId);
	}
	else
	{
		Messages* m = (Messages*)(client->outboundMsgs->current->content);
		if (m->qos != 2)
		{
			/* No Dupe flag in MQTTs
			if (pubrec->header.dup == 0)
			*/
			Log(LOG_WARNING, 51, NULL, "PUBREC", client->clientID, pubrec->msgId, m->qos);
		}
		else if (m->nextMessageType != PUBREC)
		{
			/* No Dupe flag in MQTTs
			if (pubrec->header.dup == 0)
			*/
			Log(LOG_WARNING, 52, NULL, "PUBREC", client->clientID, pubrec->msgId);
		}
		else
		{
			rc = MQTTSPacket_send_pubrel(client, pubrec->msgId);
			m->nextMessageType = PUBCOMP;
			time(&(m->lastTouch));
		}
	}
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePubrels(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_PubRel* pubrel = (MQTTS_PubRel*)pack;

	FUNC_ENTRY;
	/* look for the message by message id in the records of inbound messages for this client */
	if (ListFindItem(client->inboundMsgs, &(pubrel->msgId), messageIDCompare) == NULL)
	{
		/* TODO: no dup flag in mqtts... not sure this is right
		if (pubrel->header.dup == 0)
			Log(LOG_WARNING, 50, "PUBREL", client->clientID, pubrel->msgId);
		else
		*/
			/* Apparently this is "normal" behaviour, so we don't need to issue a warning */
			rc = MQTTSPacket_send_pubcomp(client,pubrel->msgId);
	}
	else
	{
		Messages* m = (Messages*)(client->inboundMsgs->current->content);
		if (m->qos != 2)
			Log(LOG_WARNING, 51, NULL, "PUBREL", client->clientID, pubrel->msgId, m->qos);
		else if (m->nextMessageType != PUBREL)
			Log(LOG_WARNING, 52, NULL, "PUBREL", client->clientID, pubrel->msgId);
		else
		{
			Publish publish;

			/* send pubcomp before processing the publications because a lot of return publications could fill up the socket buffer */
			rc = MQTTSPacket_send_pubcomp(client, pubrel->msgId);
			publish.header.bits.qos = m->qos;
			publish.header.bits.retain = m->retain;
			publish.msgId = m->msgid;
			publish.topic = m->publish->topic;
			publish.payload = m->publish->payload;
			publish.payloadlen = m->publish->payloadlen;
			Protocol_processPublication(&publish, client->clientID);
			MQTTProtocol_removePublication(m->publish);
			ListRemove(client->inboundMsgs, m);
			/* TODO: msgs counts */
			/* ++(state.msgs_received); */
		}
	}
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleSubscribes(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_Subscribe* sub = (MQTTS_Subscribe*)pack;
	int isnew;
	int topicId = 0;
	char* topicName = NULL;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 67, NULL, sock, clientAddr, client ? client->clientID : "",
		sub->msgId,
		(sub->flags.QoS == 3) ? -1: sub->flags.QoS,
		sub->flags.topicIdType);
	if (sub->flags.topicIdType == MQTTS_TOPIC_TYPE_PREDEFINED)
	{
		topicName = MQTTSProtocol_getRegisteredTopicName(client, sub->topicId);
		topicId = sub->topicId;
	}
	else
	{
		topicName = sub->topicName;
		sub->topicName = NULL;
	}

	if (topicName == NULL)
		rc = MQTTSPacket_send_subAck(client, sub, 0, sub->flags.QoS, MQTTS_RC_REJECTED_INVALID_TOPIC_ID);
	else
	{
		if (sub->flags.topicIdType == MQTTS_TOPIC_TYPE_NORMAL && !Topics_hasWildcards(topicName))
		{
			char* regTopicName = malloc(strlen(topicName)+1);
			strcpy(regTopicName, topicName);
			topicId = (MQTTSProtocol_registerTopic(client, regTopicName))->id;
		}

		isnew = SubscriptionEngines_subscribe(bstate->se, client->clientID,
				topicName, sub->flags.QoS, client->noLocal, (client->cleansession == 0), PRIORITY_NORMAL);

		if ( (rc = MQTTSPacket_send_subAck(client, sub, topicId, sub->flags.QoS, MQTTS_RC_ACCEPTED)) == 0)
			if ((client->noLocal == 0) || isnew)
				MQTTProtocol_processRetaineds(client, topicName,sub->flags.QoS, PRIORITY_NORMAL);
	}
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleUnsubscribes(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_Unsubscribe* unsub = (MQTTS_Unsubscribe*)pack;
	char* topicName = NULL;

	FUNC_ENTRY;
	if (unsub->flags.topicIdType == 0x01)
		topicName=MQTTSProtocol_getRegisteredTopicName(client, unsub->topicId);
	else
		topicName = unsub->topicName;

	if (topicName != NULL)
		SubscriptionEngines_unsubscribe(bstate->se, client->clientID, topicName);

	rc = MQTTSPacket_send_unsubAck(client, unsub->msgId);
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleUnsubacks(void* pack, int sock, char* clientAddr, Clients* client)
{
	return 0;
}


int MQTTSProtocol_handlePingreqs(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;

	FUNC_ENTRY;
	rc = MQTTSPacket_send_pingResp(client);
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handlePingresps(void* pack, int sock, char* clientAddr, Clients* client)
{
	FUNC_ENTRY;
	client->ping_outstanding = 0;
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT;
	return 0;
}


int MQTTSProtocol_handleDisconnects(void* pack, int sock, char* clientAddr, Clients* client)
{
	MQTTS_Disconnect* disc = (MQTTS_Disconnect*)pack;

	FUNC_ENTRY;
	Log(LOG_PROTOCOL, 79, NULL, socket, client->addr, client->clientID, disc->duration);
	client->good = 0; /* don't try and send log message to this client if it is subscribed to $SYS/broker/log */
	Log((bstate->connection_messages) ? LOG_INFO : LOG_PROTOCOL, 38, NULL, client->clientID);
	MQTTProtocol_closeSession(client, 0);
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT;
	return 0;
}


int MQTTSProtocol_handleWillTopicUpds(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_WillTopicUpd* willTopic = (MQTTS_WillTopicUpd*)pack;

	FUNC_ENTRY;
	if (willTopic->willTopic != NULL)
	{
		MQTTProtocol_setWillTopic(client, willTopic->willTopic, willTopic->flags.retain, willTopic->flags.QoS);
		willTopic->willTopic = NULL;
	}
	else
		MQTTProtocol_clearWill(client);
	rc = MQTTSPacket_send_willTopicResp(client);
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleWillTopicResps(void* pack, int sock, char* clientAddr, Clients* client)
{
	return 0;
}


int MQTTSProtocol_handleWillMsgUpds(void* pack, int sock, char* clientAddr, Clients* client)
{
	int rc = 0;
	MQTTS_WillMsgUpd* willMsg = (MQTTS_WillMsgUpd*)pack;

	FUNC_ENTRY;
	MQTTProtocol_setWillMsg(client, willMsg->willMsg);
	willMsg->willMsg = NULL;
	rc = MQTTSPacket_send_willMsgResp(client);
	time( &(client->lastContact) );
	MQTTSPacket_free_packet(pack);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_handleWillMsgResps(void* pack, int sock, char* clientAddr, Clients* client)
{
	return 0;
}


char* MQTTSProtocol_getRegisteredTopicName(Clients* client, int topicId)
{
	ListElement* elem;
	char* rc = NULL;

	FUNC_ENTRY;
	if ((elem = ListFindItem(client->registrations, &topicId, registeredTopicIdCompare)) == NULL)
		goto exit;
	if (client->pendingRegistration != NULL && elem->content == client->pendingRegistration->registration)
		goto exit;
	rc = ((Registration*)(elem->content))->topicName;
exit:
	FUNC_EXIT;
	return rc;
}


int MQTTSProtocol_getRegisteredTopicId(Clients* client, char* topicName)
{
	ListElement* elem;
	int rc = 0;

	FUNC_ENTRY;
	if ((elem = ListFindItem(client->registrations, topicName, registeredTopicNameCompare)) == NULL)
		goto exit;
	if ( client->pendingRegistration!= NULL && elem->content == client->pendingRegistration->registration )
		goto exit;
	rc = ((Registration*)(elem->content))->id;
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


void MQTTSProtocol_emptyRegistrationList(List* regList)
{
	ListElement* current = NULL;

	FUNC_ENTRY;
	while (ListNextElement(regList, &current))
	{
		Registration* m = (Registration*)(current->content);
		free(m->topicName);
	}
	ListEmpty(regList);
	FUNC_EXIT;
}


void MQTTSProtocol_freeRegistrationList(List* regList)
{
	FUNC_ENTRY;
	MQTTSProtocol_emptyRegistrationList(regList);
	free(regList);
	FUNC_EXIT;
}


Registration* MQTTSProtocol_registerTopic(Clients* client, char* topicName)
{
	Registration* reg = malloc(sizeof(Registration));

	FUNC_ENTRY;
	reg->topicName = topicName;
	reg->id = client->registrations->count+1;
	ListAppend(client->registrations, reg, sizeof(reg) + strlen(reg->topicName)+1);
	FUNC_EXIT;
	return reg;
}


int MQTTSProtocol_startRegistration(Clients* client, char* topic)
{
	int rc = 0;

	FUNC_ENTRY;
	if (client->outbound)
		rc = MQTTSProtocol_startClientRegistration(client,topic);
	else
	{
		PendingRegistration* pendingReg = malloc(sizeof(PendingRegistration));
		Registration* reg;
		int msgId = MQTTProtocol_assignMsgId(client);
		char* regTopicName = malloc(strlen(topic)+1);
		strcpy(regTopicName,topic);
		reg = MQTTSProtocol_registerTopic(client, regTopicName);
		pendingReg->msgId = msgId;
		pendingReg->registration = reg;
		time(&(pendingReg->sent));
		client->pendingRegistration = pendingReg;
		rc = MQTTSPacket_send_register(client, reg->id, regTopicName, msgId);
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSProtocol_startPublishCommon(Clients* client, Publish* mqttPublish, int dup, int qos, int retained)
{
	int rc = 0;
	int topicId = 0;
	MQTTS_Publish* pub = NULL;

	FUNC_ENTRY;
	pub = malloc(sizeof(MQTTS_Publish));
	memset(pub, '\0', sizeof(MQTTS_Publish));
	if (strlen(mqttPublish->topic) > 2 &&
			(topicId = MQTTSProtocol_getRegisteredTopicId(client, mqttPublish->topic)) == 0 && (qos != 3))
	{
		/* TODO: Logic elsewhere _should_ mean this case never happens... */
		/*printf("I want to send a msg to %s on topic %s but it isn't registered\n",client->clientID,mqttPublish->topic); */
	}
	else
	{
		pub->header.type = MQTTS_PUBLISH;
		pub->flags.QoS = qos;
		pub->flags.retain = retained;
		pub->flags.dup = dup;
		pub->msgId = mqttPublish->msgId;
		pub->data = mqttPublish->payload;
		pub->shortTopic = NULL;

		if (strlen(mqttPublish->topic) > 2 && qos == 3)
		{
			pub->topicId = strlen(mqttPublish->topic);
			pub->dataLen = mqttPublish->payloadlen + strlen(mqttPublish->topic);

			pub->data = malloc(pub->dataLen);
			memcpy(pub->data, mqttPublish->topic, pub->topicId);
			memcpy(&pub->data[pub->topicId], mqttPublish->payload, mqttPublish->payloadlen);

			pub->flags.topicIdType = MQTTS_TOPIC_TYPE_NORMAL;
		}
		else if (mqttPublish->payloadlen > 65535)
		{
			/* TODO: add a message for truncated payload */
			/* printf("Truncating a %d byte message sent to %s on topic %s\n",mqttPublish->payloadlen, client->clientID, mqttPublish->topic);*/
			pub->dataLen = 65535;
		}
		else
			pub->dataLen = mqttPublish->payloadlen;

		pub->header.len = 7 + pub->dataLen;
		if (strlen(mqttPublish->topic) < 3)
		{
			pub->flags.topicIdType = MQTTS_TOPIC_TYPE_SHORT;
			pub->shortTopic = mqttPublish->topic;
		}
		else if (qos != 3)
			pub->topicId = topicId;

		rc = MQTTSPacket_send_publish(client, pub);

		if (pub->data == mqttPublish->payload)
			pub->data = NULL;
		pub->shortTopic = NULL;
	}
	MQTTSPacket_free_packet((MQTTS_Header*)pub);
	FUNC_EXIT_RC(rc);
	return rc;
}


#endif /* #if defined(MQTTS */
