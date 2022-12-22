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
 *******************************************************************************/

/**
 * @file
 * General client protocol handling module.
 * Protocol-specific functions (MQTT, MQTTs) are in other modules.
 */

#include "Socket.h"
#include "Log.h"
#include "Messages.h"
#include "Protocol.h"
#include "MQTTPacket.h"
#include "MQTTProtocol.h"
#include "Topics.h"
#include "Persistence.h"
#include "StackTrace.h"

#include "Heap.h"

#if defined(MQTTS)
#include "MQTTSProtocol.h"
#endif

#include <string.h>

/**
 * Broker state structure.
 */
extern BrokerStates* bstate;

/**
 * Initializes the protocol module
 * @param bs pointer to a broker state structure
 */
int Protocol_initialize(BrokerStates* bs)
{
	int rc = 0;

	FUNC_ENTRY;
	bstate = bs;
	rc = MQTTProtocol_initialize(bs);
#if defined(MQTTS)
	rc = MQTTSProtocol_initialize(bs);
#endif
	FUNC_EXIT;
	return rc;
}


/**
 * Cleans up the protocol module
 */
void Protocol_terminate()
{
	FUNC_ENTRY;
	MQTTProtocol_terminate();
#if defined(MQTTS)
	MQTTSProtocol_terminate();
#endif
	FUNC_EXIT;
}


/**
 * Indicates whether a client has any in-progress message exchanges.
 * Used to enable a clean shutdown of clients, and the broker.
 * @param client pointer to a client structure
 * @return boolean - are there any messages still
 */
int Protocol_inProcess(Clients* client)
{
	return client->inboundMsgs->count > 0 || client->outboundMsgs->count > 0;
}


/**
 * Close clients during a clean shutdown
 */
void Protocol_closing()
{
	int connected_count = 0;
	Node* current = NULL;

	FUNC_ENTRY;
	current = TreeNextElement(bstate->clients, current);
	while (current)
	{
		Clients* client =	(Clients*)(current->content);
		current = TreeNextElement(bstate->clients, current);

		if (client->connected)
		{
			/* we can close a client if there are no in-process messages */
			if (!Protocol_inProcess(client))
				MQTTProtocol_closeSession(client, 0);
			else
				++connected_count;
		}
	}

	if (connected_count == 0)
		bstate->state = BROKER_STOPPED;
	FUNC_EXIT;
}


/**
 * Timeslice function to run protocol exchanges
 */
void Protocol_timeslice()
{
	int sock;
	int bridge_connection = 0;
	static int more_work = 0;

	FUNC_ENTRY;
	if ((sock = Socket_getReadySocket(more_work, NULL)) == SOCKET_ERROR)
	{
#if defined(WIN32)
		int errno;
		errno = WSAGetLastError();
#endif
		if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS && errno != EWOULDBLOCK)
		{
			Log(LOG_SEVERE, 0, "Restarting MQTT protocol to resolve socket problems");
			MQTTProtocol_shutdown(0);
			SubscriptionEngines_save(bstate->se);
			MQTTProtocol_reinitialize();
			goto exit;
		}
	}

	MQTTProtocol_checkPendingWrites();
	if (sock > 0)
	{
		Clients* client = NULL;
		Node* curnode = TreeFind(bstate->clients, &sock);

		if (curnode)
		{
			client = (Clients*)(curnode->content);
#if !defined(NO_BRIDGE)
			if (client->outbound && client->connect_state == 1)
			{
				Bridge_handleConnection(client);
				bridge_connection = 1;
			}
#endif
		}
		
		if (bridge_connection == 0)
#if defined(MQTTS)
		{
			int protocol = PROTOCOL_MQTT;
			if (client == NULL)
				protocol = Socket_getParentListener(sock)->protocol;
			else
				protocol = client->protocol;

			if (protocol == PROTOCOL_MQTT)
#endif
				MQTTProtocol_timeslice(sock, client);
#if defined(MQTTS)
			else if (protocol == PROTOCOL_MQTTS)
			    MQTTSProtocol_timeslice(sock);
		}
#endif
	}
	if (bstate->state != BROKER_STOPPING)
#if !defined(NO_ADMIN_COMMANDS)
		Persistence_read_command(bstate);
#else
		;
#endif
	else
		Protocol_closing();
	more_work = MQTTProtocol_housekeeping(more_work);
#if defined(MQTTS)
	MQTTSProtocol_housekeeping();
#endif
exit:
	FUNC_EXIT;
}


/**
 * Originates a new publication - sends it to all clients subscribed.
 * @param publish pointer to a stucture which contains all the publication information
 * @param originator the originating client
 */
void Protocol_processPublication(Publish* publish, char* originator)
{
	Messages* stored = NULL; /* to avoid duplication of data where possible */
	List* clients;
	ListElement* current = NULL;
	int savedMsgId = publish->msgId;
	int clean_needed = 0;

	FUNC_ENTRY;
	
	if (Topics_hasWildcards(publish->topic))
	{
		Log(LOG_INFO, 12, NULL, publish->topic, originator);
		goto exit;
	}

	if ((strcmp(INTERNAL_CLIENTID, originator) != 0) && bstate->password_file && bstate->acl_file)
	{
		Clients* client = (Clients*)(TreeFindIndex(bstate->clients, originator, 1)->content);
		
		if (Users_authorise(client->user, publish->topic, ACL_WRITE) == false)
		{
			Log(LOG_AUDIT, 149, NULL, originator, publish->topic);
			goto exit;
		}
	}

	if (publish->header.bits.retain)
	{
		SubscriptionEngines_setRetained(bstate->se, publish->topic, publish->header.bits.qos, publish->payload, publish->payloadlen);
		if (bstate->persistence == 1 && bstate->autosave_on_changes == 1 && bstate->autosave_interval > 0
			&& bstate->se->retained_changes >= bstate->autosave_interval)
		{
			Log(LOG_INFO, 100, NULL, bstate->autosave_interval);
			SubscriptionEngines_save(bstate->se);
		}
	}

	clients = SubscriptionEngines_getSubscribers(bstate->se, publish->topic, originator);
	if (strncmp(publish->topic, "$SYS/client/", 12) == 0)
	{ /* default subscription for a client */
		Node* node = TreeFindIndex(bstate->clients, &publish->topic[12], 1);
		if (node == NULL)
			node = TreeFind(bstate->disconnected_clients, &publish->topic[12]);
		if (node && node->content)
		{
			Subscriptions* rcs = malloc(sizeof(Subscriptions));
			rcs->clientName = &publish->topic[12];
			rcs->qos = 2;
			rcs->priority = PRIORITY_NORMAL;
			rcs->topicName = publish->topic;
			ListAppend(clients, rcs, sizeof(Subscriptions));
		}
	}
	current = NULL;
	while (ListNextElement(clients, &current))
	{
		Node* curnode = NULL;
		unsigned int qos = ((Subscriptions*)(current->content))->qos;
		int priority = ((Subscriptions*)(current->content))->priority;
		char* clientName = ((Subscriptions*)(current->content))->clientName;
		
		if (publish->header.bits.qos < qos) /* reduce qos if > subscribed qos */
			qos = publish->header.bits.qos;
			
		if ((curnode = TreeFindIndex(bstate->clients, clientName, 1)) == NULL)
			curnode = TreeFind(bstate->disconnected_clients, clientName);
#if defined(MQTTS)
		if (curnode == NULL && ((curnode = TreeFindIndex(bstate->mqtts_clients, clientName, 1)) == NULL))
			curnode = TreeFind(bstate->disconnected_mqtts_clients, clientName);
#endif

		if (curnode)
		{
			Clients* pubclient = (Clients*)(curnode->content);
			int retained = 0;
			Messages* saved = NULL;
			char* original_topic = publish->topic;
			
#if !defined(NO_BRIDGE)
			if (pubclient->outbound || pubclient->noLocal)
			{
				retained = publish->header.bits.retain; /* outbound and noLocal mean outward/inward bridge client,
																							so keep retained flag */
				if (pubclient->outbound)
				{
					Bridge_handleOutbound(pubclient, publish);
					if (publish->topic != original_topic)
					{ /* handleOutbound has changed the topic, so we musn't used the stored pub which
					        contains the original topic */
						saved = stored;
						stored = NULL;
					}
				}
			}
#endif
			if (Protocol_startOrQueuePublish(pubclient, publish, qos, retained, priority, &stored) == SOCKET_ERROR)
			{
				pubclient->good = pubclient->connected = 0; /* flag this client as needing to be cleaned up */
				clean_needed = 1;
			}
			if (publish->topic != original_topic)
			{
				stored = saved;  /* restore the stored pointer for the next loop iteration */
				free(publish->topic);
				publish->topic = original_topic;
			}
		}
	}
	publish->msgId = savedMsgId;
	/* INTERNAL_CLIENTID means that we are publishing data to the log,
			and we don't want to interfere with other close processing */
	if (clean_needed && strcmp(originator, INTERNAL_CLIENTID) != 0)
		MQTTProtocol_clean_clients(bstate->clients);
	ListFree(clients);
exit:
	FUNC_EXIT;
}


/**
 * Kick off a new publication, queue it or send it as appropriate.
 * @param pubclient the client to send the publication to
 * @param publish the publication data
 * @param qos the MQTT QoS to use
 * @param retained boolean - whether to set the MQTT retained flag
 * @param mm - pointer to the message to send
 * @return the completion code
 */
int Protocol_startOrQueuePublish(Clients* pubclient, Publish* publish, int qos, int retained, int priority, Messages** mm)
{
	int rc = TCPSOCKET_COMPLETE;

	FUNC_ENTRY;
	if (pubclient->connected && pubclient->good &&           /* client is connected and has no errors */
		Socket_noPendingWrites(pubclient->socket) &&         /* there aren't any previous packets still stacked up on the socket */
		queuedMsgsCount(pubclient) == 0 &&                   /* there are no messages ahead in the queue */
		pubclient->outboundMsgs->count < bstate->max_inflight_messages) /* the queue is not full */
	{
#if defined(MQTTS)
		if (pubclient->protocol == PROTOCOL_MQTTS_MULTICAST)
		{
			MQTTSProtocol_startPublishCommon(pubclient, publish, 0, 3, retained);
		}
		else if (pubclient->protocol == PROTOCOL_MQTTS && strlen(publish->topic) > 2 &&
				MQTTSProtocol_getRegisteredTopicId(pubclient, publish->topic) == 0)
		{
			if (pubclient->pendingRegistration == NULL)
				rc = MQTTSProtocol_startRegistration(pubclient, publish->topic);
			rc = MQTTProtocol_queuePublish(pubclient, publish, qos, retained, priority, mm);
		}
		else if (pubclient->protocol == PROTOCOL_MQTTS && qos > 0 && pubclient->outboundMsgs->count > 0)
		{
			/* can only have 1 qos 1/2 message in flight with MQTT-S */
			rc = MQTTProtocol_queuePublish(pubclient, publish, qos, retained, priority, mm);
		}
		else
		{
#endif
			rc = MQTTProtocol_startPublish(pubclient, publish, qos, retained, mm);
			/* We don't normally queue QoS 0 messages just send them straight through.  But in the case when none of the packet
			 * is written we need to queue it up.  If only part of the packet is written, then it is buffered by the socket module.
			 */
			if (qos == 0 && rc == TCPSOCKET_NOWORK)
				rc = MQTTProtocol_queuePublish(pubclient, publish, qos, retained, priority, mm);
#if defined(MQTTS)
		}
#endif
	}
	else if (qos != 0 || (pubclient->connected && pubclient->good))    /* only queue qos 0 if the client is connected in a good way */
		rc = MQTTProtocol_queuePublish(pubclient, publish, qos, retained, priority, mm);
	FUNC_EXIT_RC(rc);
	return rc;
}


#if defined(MQTTS)
Clients* Protocol_getclientbyaddr(char* addr)
{
	Node* node = NULL;
	Clients* client = NULL;

	FUNC_ENTRY;
	if ((node = TreeFind(bstate->mqtts_clients, addr)) != NULL)
		client = (Clients*)(node->content);
	FUNC_EXIT;
	return client;
}

#if !defined(NO_BRIDGE)
Clients* Protocol_getoutboundclient(int sock)
{
	Node* node = NULL;
	Clients* client = NULL;

	FUNC_ENTRY;
	if ((node = TreeFind(bstate->clients, &sock)) != NULL)
		client = (Clients*)(node->content);
	FUNC_EXIT;
	return client;
}
#endif
#endif


/**
 * Compare clients by client id prefix - used for simple authentication
 * @param prefix the client id prefix match
 * @param clientid the client id to match against
 * @return boolean - a flag to indicate whether the prefix matches the client id
 */
int clientPrefixCompare(void* prefix, void* clientid)
{
	/* printf("comparing prefix %s with clientid %s\n", (char*)prefix, (char*)clientid); */
	return strncmp((char*)prefix, (char*)clientid, strlen(prefix)) == 0;
}


/**
 * Check to see if a client should accept new work
 * @param client the client to check
 * @return boolean value to indicate whether the client should accept new work
 */
int Protocol_isClientQuiescing(Clients* client)
{
	return (bstate->state != BROKER_RUNNING ||
		(client && (client->outbound && ((BridgeConnections*)client->bridge_context)->state != CONNECTION_RUNNING)));
}



int Protocol_handlePublishes(Publish* publish, int sock, Clients* client, char* clientid)
{
	int rc = TCPSOCKET_COMPLETE;
	#if defined(MQTTS)
	int topicId = 0;
	#endif
#if !defined(SINGLE_LISTENER)
	Listener* listener = NULL;
#endif

	FUNC_ENTRY;
	if (Protocol_isClientQuiescing(client))
		goto exit; /* don't accept new work */
#if !defined(SINGLE_LISTENER)
	listener = Socket_getParentListener(sock);
	if (listener && listener->mount_point)
	{
		char* temp = malloc(strlen(publish->topic) + strlen(listener->mount_point) + 1);
		strcpy(temp, listener->mount_point);
		strcat(temp, publish->topic);
		free(publish->topic);
		publish->topic = temp;
	}
#endif

#if !defined(NO_BRIDGE)
	if (client && client->outbound)
		Bridge_handleInbound(client, publish);
#endif

	if (publish->header.bits.qos == 0)
	{
		if (strlen(publish->topic) < 5 || strncmp(publish->topic, sysprefix, strlen(sysprefix)) != 0)
		{
			++(bstate->msgs_received);
			bstate->bytes_received += publish->payloadlen;
		}
		Protocol_processPublication(publish, clientid);
	}
	else if (publish->header.bits.qos == 1)
	{
		/* send puback before processing the publications because a lot of return publications could fill up the socket buffer */
#if defined(MQTTS)
		if (client->protocol == PROTOCOL_MQTTS){
			topicId = MQTTSProtocol_getRegisteredTopicId(client, publish->topic);
			rc = MQTTSPacket_send_puback(client, topicId, publish->msgId, MQTTS_RC_ACCEPTED);
		}
		else
#endif
			rc = MQTTPacket_send_puback(publish->msgId, sock, clientid);
		/* if we get a socket error from sending the puback, should we ignore the publication? */
		Protocol_processPublication(publish, clientid);
		++(bstate->msgs_received);
		bstate->bytes_received += publish->payloadlen;
	}
	else if (publish->header.bits.qos == 2 && client->inboundMsgs->count < bstate->max_inflight_messages)
	{
		/* store publication in inbound list - if list is full, ignore and rely on client retry */
		int len;
		ListElement* listElem = NULL;
		Messages* m = NULL;
		Publications* p = MQTTProtocol_storePublication(publish, &len);

		if ((listElem = ListFindItem(client->inboundMsgs, &publish->msgId, messageIDCompare)) != NULL)
		{
			m = (Messages*)(listElem->content);
			MQTTProtocol_removePublication(m->publish); /* remove old publication data - could be different */
		}
		else
			m = malloc(sizeof(Messages));

		m->publish = p;
		m->msgid = publish->msgId;
		m->qos = publish->header.bits.qos;
		m->retain = publish->header.bits.retain;
		m->nextMessageType = PUBREL;

		if (listElem == NULL)
			ListAppend(client->inboundMsgs, m, sizeof(Messages) + len);
#if defined(MQTTS)
		if (client->protocol == PROTOCOL_MQTTS)
			rc = MQTTSPacket_send_pubrec(client, publish->msgId);
		else
#endif
			rc = MQTTPacket_send_pubrec(publish->msgId, sock, clientid);
	}
	else if (publish->header.bits.qos == 3) /* only applies to MQTT-S */
	{
		publish->header.bits.qos = 0;
		Protocol_processPublication(publish, clientid);
	}
exit:
	if (sock > 0)
		MQTTPacket_freePublish(publish);
	FUNC_EXIT_RC(rc);
	return rc;
}
