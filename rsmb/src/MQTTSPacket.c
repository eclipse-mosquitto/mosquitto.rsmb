/*******************************************************************************
 * Copyright (c) 2008, 2013 IBM Corp.
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

#if defined(MQTTS)

#include "MQTTSPacket.h"
#include "Log.h"
#include "Clients.h"
#include "Messages.h"
#include "Protocol.h"
#include "Socket.h"
#include "StackTrace.h"

#include <stdlib.h>
#include <string.h>

#include "Heap.h"


static char* packet_names[] =
{
		"ADVERTISE", "SEARCHGW", "GWINFO", "RESERVED", "CONNECT", "CONNACK",
		"WILLTOPICREQ", "WILLTOPIC", "WILLMSGREQ", "WILLMSG", "REGISTER", "REGACK",
		"PUBLISH", "PUBACK", "PUBCOMP", "PUBREC", "PUBREL", "RESERVED",
		"SUBSCRIBE", "SUBACK", "UNSUBSCRIBE", "UNSUBACK", "PINGREQ", "PINGRESP",
		"DISCONNECT", "RESERVED", "WILLTOPICUPD", "WILLTOPICRESP", "WILLMSGUPD",
		"WILLMSGRESP"
};

typedef void* (*mqtts_pf)(MQTTSHeader, char*);

typedef void (*mqtts_fpf)(void *);

static mqtts_pf new_mqtts_packets[] =
{
	MQTTSPacket_advertise, /* ADVERTISE */
	MQTTSPacket_searchGw,  /* SEARCHGW */
	MQTTSPacket_gwInfo,    /* GWINFO */
	NULL,                  /* RESERVED */
	MQTTSPacket_connect,   /* CONNECT */
	MQTTSPacket_connack,   /* CONNACK */
	MQTTSPacket_header_only, /* WILLTOPICREQ */
	MQTTSPacket_willTopic, /* WILLTOPIC */
	MQTTSPacket_header_only, /* WILLMSGREQ */
	MQTTSPacket_willMsg, /* WILLMSG */
	MQTTSPacket_register, /* REGISTER */
	MQTTSPacket_ack_with_reasoncode, /* REGACK */
	MQTTSPacket_publish, /* PUBLISH */
	MQTTSPacket_ack_with_reasoncode, /* PUBACK */
	MQTTSPacket_header_with_msgId, /* PUBCOMP */
	MQTTSPacket_header_with_msgId, /* PUBREC */
	MQTTSPacket_header_with_msgId, /* PUBREL */
	NULL,         /* RESERVED */
	MQTTSPacket_subscribe, /* SUBSCRIBE */
	MQTTSPacket_suback, /* SUBACK */
	MQTTSPacket_subscribe, /* UNSUBSCRIBE */
	MQTTSPacket_header_with_msgId, /* UNSUBACK */
	MQTTSPacket_pingreq, /* PINGREQ */
	MQTTSPacket_header_only, /* PINGRESP */
	MQTTSPacket_disconnect, /* DISCONNECT */
	NULL,         /* RESERVED */
	MQTTSPacket_willTopicUpd, /* WILLTOPICUPD */
	MQTTSPacket_header_only, /* WILLTOPICRESP */
	MQTTSPacket_willMsgUpd, /* WILLMSGUPD */
	MQTTSPacket_header_only  /* WILLMSGRESP */
};

static mqtts_fpf free_mqtts_packets[] =
{
	NULL, /* ADVERTISE */
	NULL,  /* SEARCHGW */
	MQTTSPacket_free_gwInfo,    /* GWINFO *** */
	NULL,                  /* RESERVED */
	MQTTSPacket_free_connect,   /* CONNECT *** */
	NULL,   /* CONNACK */
	NULL, /* WILLTOPICREQ */
	MQTTSPacket_free_willTopic, /* WILLTOPIC *** */
	NULL, /* WILLMSGREQ */
	MQTTSPacket_free_willMsg, /* WILLMSG *** */
	MQTTSPacket_free_register, /* REGISTER *** */
	NULL, /* REGACK */
	MQTTSPacket_free_publish, /* PUBLISH *** */
	NULL, /* PUBACK */
	NULL, /* PUBCOMP */
	NULL, /* PUBREC */
	NULL, /* PUBREL */
	NULL,         /* RESERVED */
	MQTTSPacket_free_subscribe, /* SUBSCRIBE *** */
	NULL, /* SUBACK */
	MQTTSPacket_free_subscribe, /* UNSUBSCRIBE *** */
	NULL, /* UNSUBACK */
	MQTTSPacket_free_pingreq, /* PINGREQ *** */
	NULL, /* PINGRESP */
	NULL, /* DISCONNECT */
	NULL,         /* RESERVED */
	MQTTSPacket_free_willTopicUpd, /* WILLTOPICUPD *** */
	NULL, /* WILLTOPICRESP */
	MQTTSPacket_free_willMsgUpd, /* WILLMSGUPD *** */
	NULL  /* WILLMSGRESP */
};


char* MQTTSPacket_name(int ptype)
{
	return (ptype >= 0 && ptype <= MQTTS_WILLMSGRESP) ? packet_names[ptype] : "UNKNOWN";
}

static int max_packet_size = 0;
static char* msg = NULL;
static BrokerStates* bstate;

int MQTTSPacket_initialize(BrokerStates* aBrokerState)
{
	bstate = aBrokerState;
	max_packet_size = bstate->max_mqtts_packet_size;

	msg = malloc(max_packet_size);

	return 0;
}


void MQTTSPacket_terminate()
{
	free(msg);
}


void* MQTTSPacket_Factory(int sock, char** clientAddr, struct sockaddr* from, int* error)
{
	static MQTTSHeader header;
	int ptype;
	void* pack = NULL;
	/*struct sockaddr_in cliAddr;*/
	int n;
	char* data = &msg[0];
	socklen_t len = sizeof(struct sockaddr_in6);

	FUNC_ENTRY;
/* #if !defined(NO_BRIDGE)
	client = Protocol_getoutboundclient(sock);
	FUNC_ENTRY;
	if (client!=NULL)
		n = recv(sock,msg,512,0);
	else
 #endif */

	/* max message size from global parameters, as we lose the packet if we don't receive it.  Default is
	 * 65535, so the parameter can be used to decrease the memory usage.
	 * The message memory area must be allocated on the heap so that this memory can be not allocated
	 * on reduced-memory systems.
	 */
	n = recvfrom(sock, msg, max_packet_size, 0, from, &len);
	if (n == SOCKET_ERROR)
	{
		int en = Socket_error("UDP read error", sock);
		if (en == EINVAL)
			Log(LOG_WARNING, 0, "EINVAL");

		*error = SOCKET_ERROR;
		goto exit;
	}

	*clientAddr = Socket_getaddrname(from, sock);
/*
	printf("%d bytes of data on socket %d from %s\n",n,sock,*clientAddr);
	if (n>0) {
		for (i=0;i<n;i++) {
			printf("%d ",msg[i]);
		}
		printf("\n");
	}
*/
	*error = SOCKET_ERROR;  // indicate whether an error occurred, or not
	if (n < 2)
		goto exit;

	if (msg[0] == 1)
	{
		++data;
		header.len = readInt(&data);
	}
	else
		header.len = *(unsigned char*)data++;
	header.type = *data++;
	//printf("header.type is %d, header.len is %d, n is %d\n", header.type, header.len, n);
    if (header.len != n)
    {
		*error = UDPSOCKET_INCOMPLETE;
		goto exit;
    }
	else
	{
		ptype = header.type;
		if (ptype < MQTTS_ADVERTISE || ptype > MQTTS_WILLMSGRESP || new_mqtts_packets[ptype] == NULL)
			Log(TRACE_MAX, 17, NULL, ptype);
		else if ((pack = (*new_mqtts_packets[ptype])(header, data)) == NULL)
			*error = BAD_MQTTS_PACKET;
	}
exit:
   	FUNC_EXIT_RC(*error);
   	return pack;
}


void* MQTTSPacket_header_only(MQTTSHeader header, char* data)
{
	MQTTS_Header* pack = NULL;

	FUNC_ENTRY;
	if (header.len != 2)
		goto exit;
	pack = malloc(sizeof(MQTTS_Header));
	pack->header = header;
exit:
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_header_with_msgId(MQTTSHeader header, char* data)
{
	MQTTS_Header_MsgId* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	if (header.len != 4)
		goto exit;
	pack = malloc(sizeof(MQTTS_Header_MsgId));
	pack->header = header;
	pack->msgId = readInt(&curdata);
exit:
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_ack_with_reasoncode(MQTTSHeader header, char* data)
{
	MQTTS_Ack* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	if (header.len != 7)
		goto exit;
	pack = malloc(sizeof(MQTTS_Ack));
	pack->header = header;
	pack->topicId = readInt(&curdata);
	pack->msgId = readInt(&curdata);
	pack->returnCode = readChar(&curdata);
exit:
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_advertise(MQTTSHeader header, char* data)
{
	MQTTS_Advertise* pack = malloc(sizeof(MQTTS_Advertise));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->gwId = readChar(&curdata);
	pack->duration = readInt(&curdata);
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_searchGw(MQTTSHeader header, char* data)
{
	MQTTS_SearchGW* pack = malloc(sizeof(MQTTS_SearchGW));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->radius = readChar(&curdata);
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_gwInfo(MQTTSHeader header, char* data)
{
	MQTTS_GWInfo* pack = malloc(sizeof(MQTTS_GWInfo));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->gwId = readChar(&curdata);
	if (header.len > 3)
	{
		pack->gwAdd = malloc(header.len-2);
		memcpy(pack->gwAdd, curdata, header.len-3);
		pack->gwAdd[header.len-3] = '\0';
	}
	else
	{
		pack->gwAdd = 0;
	}
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_connect(MQTTSHeader header, char* data)
{
	MQTTS_Connect* pack = malloc(sizeof(MQTTS_Connect));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->flags.all = readChar(&curdata);
	pack->protocolID = readChar(&curdata);
	pack->keepAlive = readInt(&curdata);
	pack->clientID = malloc(header.len - 5);
	memcpy(pack->clientID, curdata, header.len - 6);
	pack->clientID[header.len - 6] = '\0';
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_connack(MQTTSHeader header, char* data)
{
	MQTTS_Connack* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	pack = malloc(sizeof(MQTTS_Connack));
	pack->header = header;
	pack->returnCode = readChar(&curdata);
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_willTopic(MQTTSHeader header, char* data)
{
	MQTTS_WillTopic* pack = malloc(sizeof(MQTTS_WillTopic));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->flags.all = readChar(&curdata);
	pack->willTopic = malloc(header.len-2);
	memcpy(pack->willTopic, curdata, header.len-3);
	pack->willTopic[header.len-3] = '\0';
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_willMsg(MQTTSHeader header, char* data)
{
	MQTTS_WillMsg* pack = malloc(sizeof(MQTTS_WillMsg));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->willMsg = malloc(header.len-1);
	memcpy(pack->willMsg, curdata, header.len-2);
	pack->willMsg[header.len-2] = '\0';
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_register(MQTTSHeader header, char* data)
{
	MQTTS_Register* pack = malloc(sizeof(MQTTS_Register));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->topicId = readInt(&curdata);
	pack->msgId = readInt(&curdata);
	pack->topicName = malloc(header.len-5);
	memcpy(pack->topicName, curdata, header.len-6);
	pack->topicName[header.len-6] = '\0';
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_publish(MQTTSHeader header, char* data)
{
	MQTTS_Publish* pack = NULL;
	char* curdata = data;
	int topicLen = 0;
	int datalen = 0;

	FUNC_ENTRY;
	//printf("publish header.len %d\n", header.len);
	pack = malloc(sizeof(MQTTS_Publish));
	pack->header = header;
	pack->flags.all = readChar(&curdata);
	if (pack->flags.topicIdType == MQTTS_TOPIC_TYPE_NORMAL && pack->flags.QoS == 3)
	{
		topicLen = readInt(&curdata); /* topic id is length if qos == -1 && topic type is "normal" */
		pack->topicId = 0;
		pack->shortTopic = malloc(topicLen + 1);
	}
	else if (pack->flags.topicIdType == MQTTS_TOPIC_TYPE_SHORT)
	{
		char* st = malloc(3);
		st[0] = readChar(&curdata);
		st[1] = readChar(&curdata);
		st[2] = '\0';
		pack->topicId = 0;
		pack->shortTopic = st;
	}
	else /* NORMAL or PREDEFINED */
	{
		pack->topicId = readInt(&curdata);
		pack->shortTopic = NULL;
	}
	pack->msgId = readInt(&curdata);
	if (pack->flags.topicIdType == MQTTS_TOPIC_TYPE_NORMAL && pack->flags.QoS == 3)
	{
		datalen = header.len - 7 - topicLen;
		memcpy(pack->shortTopic, curdata, topicLen);
		pack->shortTopic[topicLen] = '\0';
		curdata += topicLen;
	}
	else
		datalen = header.len - 7;
	pack->data = malloc(datalen);
	memcpy(pack->data, curdata, datalen);
	pack->dataLen = datalen;
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_subscribe(MQTTSHeader header, char* data)
{
	MQTTS_Subscribe* pack = malloc(sizeof(MQTTS_Subscribe));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->flags.all = readChar(&curdata);
	pack->msgId = readInt(&curdata);
	if (pack->flags.topicIdType == MQTTS_TOPIC_TYPE_PREDEFINED)
	{
		pack->topicId = readInt(&curdata);
		pack->topicName = NULL;
	}
	else
	{
		pack->topicId = 0;
		pack->topicName = malloc(header.len - 4);
		memcpy(pack->topicName, curdata, header.len - 5);
		pack->topicName[header.len - 5] = '\0';
	}
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_suback(MQTTSHeader header, char* data)
{
	MQTTS_SubAck* pack = malloc(sizeof(MQTTS_SubAck));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	pack->flags.all = readChar(&curdata);
	pack->topicId = readInt(&curdata);
	pack->msgId = readInt(&curdata);
	pack->returnCode = readChar(&curdata);
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_pingreq(MQTTSHeader header, char* data)
{
	MQTTS_PingReq* pack = malloc(sizeof(MQTTS_PingReq));
	char* curdata = data;

	FUNC_ENTRY;
	pack->header = header;
	if (header.len > 2)
	{
		pack->clientId = malloc(header.len - 1);
		memcpy(pack->clientId, curdata, header.len - 2);
		pack->clientId[header.len-2] = '\0';
	}
	else
		pack->clientId = NULL;
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_disconnect(MQTTSHeader header, char* data)
{
	MQTTS_Disconnect* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	pack = malloc(sizeof(MQTTS_Disconnect));
	pack->header = header;
	if (header.len > 2)
		pack->duration = readInt(&curdata);
	else
		pack->duration = 0;
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_willTopicUpd(MQTTSHeader header, char* data)
{
	MQTTS_WillTopicUpd* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	pack = malloc(sizeof(MQTTS_WillTopicUpd));
	pack->header = header;
	if (header.len > 2)
	{
		pack->flags.all = readChar(&curdata);
		pack->willTopic = malloc(header.len - 2);
		memcpy(pack->willTopic, curdata, header.len - 3);
		pack->willTopic[header.len-3] = '\0';
	}
	else
	{
		pack->flags.all = 0;
		pack->willTopic = NULL;
	}
	FUNC_EXIT;
	return pack;
}


void* MQTTSPacket_willMsgUpd(MQTTSHeader header, char* data)
{
	MQTTS_WillMsgUpd* pack = NULL;
	char* curdata = data;

	FUNC_ENTRY;
	pack = malloc(sizeof(MQTTS_WillMsgUpd));
	pack->header = header;
	pack->willMsg = malloc(header.len - 1);
	memcpy(pack->willMsg, curdata, header.len - 2);
	pack->willMsg[header.len-2] = '\0';
	FUNC_EXIT;
	return pack;
}


void MQTTSPacket_free_packet(MQTTS_Header* pack)
{
	FUNC_ENTRY;
	if (free_mqtts_packets[(int)pack->header.type] != NULL)
		(*free_mqtts_packets[(int)pack->header.type])(pack);
	free(pack);
	FUNC_EXIT;
}


void MQTTSPacket_free_gwInfo(void* pack)
{
	MQTTS_GWInfo* gwi = (MQTTS_GWInfo*)pack;

	FUNC_ENTRY;
	if (gwi->gwAdd != NULL)
		free(gwi->gwAdd);
	FUNC_EXIT;
}


void MQTTSPacket_free_connect(void* pack)
{
	MQTTS_Connect* con = (MQTTS_Connect*)pack;

	FUNC_ENTRY;
	if (con->clientID != NULL)
		free(con->clientID);
	FUNC_EXIT;
}


void MQTTSPacket_free_willTopic(void* pack)
{
	MQTTS_WillTopic* wt = (MQTTS_WillTopic*)pack;

	FUNC_ENTRY;
	if (wt->willTopic != NULL)
		free(wt->willTopic);
	FUNC_EXIT;
}


void MQTTSPacket_free_willMsg(void* pack)
{
	MQTTS_WillMsg* wm = (MQTTS_WillMsg*)pack;

	FUNC_ENTRY;
	if (wm->willMsg != NULL)
		free(wm->willMsg);
	FUNC_EXIT;
}


void MQTTSPacket_free_register(void* pack)
{
	MQTTS_Register* reg = (MQTTS_Register*)pack;

	FUNC_ENTRY;
	if (reg->topicName != NULL)
		free(reg->topicName);
	FUNC_EXIT;
}


void MQTTSPacket_free_publish(void* pack)
{
	MQTTS_Publish* pub = (MQTTS_Publish*)pack;

	FUNC_ENTRY;
	if (pub->data != NULL)
		free(pub->data);
	if (pub->shortTopic != NULL)
		free(pub->shortTopic);
	FUNC_EXIT;
}


void MQTTSPacket_free_subscribe(void* pack)
{
	MQTTS_Subscribe* sub = (MQTTS_Subscribe*)pack;

	FUNC_ENTRY;
	if (sub->topicName != NULL) {
		free(sub->topicName);
	}
	FUNC_EXIT;
}


void MQTTSPacket_free_pingreq(void* pack)
{
	MQTTS_PingReq* ping = (MQTTS_PingReq*)pack;

	FUNC_ENTRY;
	if (ping->clientId != NULL)
		free(ping->clientId);
	FUNC_EXIT;
}


void MQTTSPacket_free_willTopicUpd(void* pack)
{
	MQTTS_WillTopicUpd* wtu = (MQTTS_WillTopicUpd*)pack;

	FUNC_ENTRY;
	if (wtu->willTopic != NULL)
		free(wtu->willTopic);
	FUNC_EXIT;
}


void MQTTSPacket_free_willMsgUpd(void* pack)
{
	MQTTS_WillMsgUpd* wmu = (MQTTS_WillMsgUpd*)pack;

	FUNC_ENTRY;
	if (wmu->willMsg != NULL)
		free(wmu->willMsg);
	FUNC_EXIT;
}


int MQTTSPacket_sendPacketBuffer(int socket, char* addr, PacketBuffer buf)
{
	char *port;
	int rc = 0;

	FUNC_ENTRY;
	port = strrchr(addr, ':') + 1;
	*(port - 1) = '\0';
	if (strchr(addr, ':'))
	{
		struct sockaddr_in6 cliaddr6;
		memset(&cliaddr6, '\0', sizeof(cliaddr6));
		cliaddr6.sin6_family = AF_INET6;
		if (inet_pton(cliaddr6.sin6_family, addr, &cliaddr6.sin6_addr) == 0)
			Socket_error("inet_pton", socket);
		cliaddr6.sin6_port = htons(atoi(port));
		if ((rc = sendto(socket, buf.data, buf.len, 0, (const struct sockaddr*)&cliaddr6, sizeof(cliaddr6))) == SOCKET_ERROR)
			Socket_error("sendto", socket);
		else
			rc = 0;
	}
	else
	{
		struct sockaddr_in cliaddr;
		cliaddr.sin_family = AF_INET;
		if (inet_pton(cliaddr.sin_family, addr, &cliaddr.sin_addr.s_addr) == 0)
			Socket_error("inet_pton", socket);
		cliaddr.sin_port = htons(atoi(port));
		if ((rc = sendto(socket, buf.data, buf.len, 0, (const struct sockaddr*)&cliaddr, sizeof(cliaddr))) == SOCKET_ERROR)
			Socket_error("sendto", socket);
		else
			rc = 0;
	}
	*(port - 1) = ':';

	FUNC_EXIT_RC(rc);
	return rc;
}



int MQTTSPacket_send(int socket, char* addr, MQTTSHeader header, char* buffer, int buflen)
{
	int rc = 0;
	char *data = NULL;
	char *ptr = NULL;
	PacketBuffer buf;

	FUNC_ENTRY;
	if (header.len > 256)
	{
		header.len += 2;
		buflen += 2;
		data = malloc(header.len);
		ptr = data;
		*ptr++ = 0x01;
		writeInt(&ptr, header.len);
	}
	else
	{
		data = malloc(header.len);
		ptr = data;
		*ptr++ = header.len;
	}
	*ptr++ = header.type;

	memcpy(ptr, buffer, buflen);

	buf.data = data;
	buf.len = buflen + 2;
	rc = MQTTSPacket_sendPacketBuffer(socket, addr, buf);

	if (rc == SOCKET_ERROR)
	{
		Socket_error("sendto", socket);
/*		if (err == EWOULDBLOCK || err == EAGAIN)
			rc = TCPSOCKET_INTERRUPTED;
*/
	}
	else
		rc = 0;

	free(data);

	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_ack(Clients* client, char type)
{
	PacketBuffer buf;
	int rc = 0;

	FUNC_ENTRY;
	buf = MQTTSPacketSerialize_ack(type, -1);
	rc = MQTTSPacket_sendPacketBuffer(client->socket, client->addr, buf);
	free(buf.data);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_ack_with_msgId(Clients* client, char type, int msgId)
{
	PacketBuffer buf;
	int rc = 0;

	FUNC_ENTRY;
	buf = MQTTSPacketSerialize_ack(type, msgId);
	rc = MQTTSPacket_sendPacketBuffer(client->socket, client->addr, buf);
	free(buf.data);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_connack(Clients* client, int returnCode)
{
	PacketBuffer buf;
	int rc = 0;

	FUNC_ENTRY;
	buf = MQTTSSerialize_connack(returnCode);
	rc = MQTTSPacket_sendPacketBuffer(client->socket, client->addr, buf);
	free(buf.data);
	Log(LOG_PROTOCOL, 40, NULL, socket, client->addr, client->clientID, returnCode, rc);	
	FUNC_EXIT;
	return rc;
}


int MQTTSPacket_send_willTopicReq(Clients* client)
{
	int rc = MQTTSPacket_send_ack(client, MQTTS_WILLTOPICREQ);
	Log(LOG_PROTOCOL, 42, NULL, client->socket, client->addr, client->clientID, rc);
	return rc;
}


int MQTTSPacket_send_willMsgReq(Clients* client)
{
	int rc = MQTTSPacket_send_ack(client, MQTTS_WILLMSGREQ);
	Log(LOG_PROTOCOL, 46, NULL, client->socket, client->addr, client->clientID, rc);
	return rc;
}


int MQTTSPacket_send_pingResp(Clients* client)
{
	int rc = MQTTSPacket_send_ack(client, MQTTS_PINGRESP);
	Log(LOG_PROTOCOL, 76, NULL, client->socket, client->addr, client->clientID, rc);
	return rc;
}


int MQTTSPacket_send_willTopicResp(Clients* client)
{
	int returnCode = 0;
	int rc = MQTTSPacket_send_ack_with_msgId(client, MQTTS_WILLTOPICRESP, returnCode);
	Log(LOG_PROTOCOL, 84, NULL, client->socket, client->addr, client->clientID, returnCode, rc);
	return rc;
}


int MQTTSPacket_send_willMsgResp(Clients* client)
{
	int returnCode = 0;
	int rc = MQTTSPacket_send_ack_with_msgId(client, MQTTS_WILLMSGRESP, returnCode);
	Log(LOG_PROTOCOL, 88, NULL, client->socket, client->addr, client->clientID, returnCode, rc);
	return rc;
}


int MQTTSPacket_send_regAck(Clients* client, int msgId, int topicId, char returnCode)
{
	MQTTS_RegAck packet;
	int rc = 0;
	char *buf, *ptr;
	int datalen = 5;

	FUNC_ENTRY;
	packet.header.len = 7;
	packet.header.type = MQTTS_REGACK;

	ptr = buf = malloc(datalen);
	writeInt(&ptr, topicId);
	writeInt(&ptr, msgId);
	writeChar(&ptr, returnCode);

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, datalen);
	free(buf);

	Log(LOG_PROTOCOL, 52, NULL, client->socket, client->addr, client->clientID, msgId, topicId, returnCode, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_subAck(Clients* client, MQTTS_Subscribe* sub, int topicId, int qos, char returnCode)
{
	MQTTS_SubAck packet;
	int rc = 0;
	char *buf, *ptr;
	int datalen = 6;

	FUNC_ENTRY;
	packet.header.len = 8;
	packet.header.type = MQTTS_SUBACK;

	ptr = buf = malloc(datalen);
	packet.flags.QoS = qos;
	writeChar(&ptr, packet.flags.all);
	writeInt(&ptr, topicId);
	writeInt(&ptr, sub->msgId);
	writeChar(&ptr, returnCode);
	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, datalen);
	free(buf);

	Log(LOG_PROTOCOL, 68, NULL, client->socket, client->addr, client->clientID, sub->msgId, topicId, returnCode, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_unsubAck(Clients* client, int msgId)
{
	MQTTS_UnsubAck packet;
	int rc = 0;
	char *buf, *ptr;

	FUNC_ENTRY;
	packet.header.len = 4;
	packet.header.type = MQTTS_UNSUBACK;
	ptr = buf = malloc(2);
	writeInt(&ptr, msgId);
	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, 2);
	free(buf);

	Log(LOG_PROTOCOL, 72, NULL, client->socket, client->addr, client->clientID, msgId, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_publish(Clients* client, MQTTS_Publish* pub)
{

	int rc = 0;
	char *buf, *ptr;
	int datalen = 5 + pub->dataLen;

	FUNC_ENTRY;
	ptr = buf = malloc(datalen);

	writeChar(&ptr, pub->flags.all);
	//if ((pub->flags.topicIdType == MQTTS_TOPIC_TYPE_NORMAL && pub->flags.QoS == 3) ||
	if (pub->flags.topicIdType == MQTTS_TOPIC_TYPE_SHORT)
	{
		writeChar(&ptr, pub->shortTopic[0]);
		writeChar(&ptr, pub->shortTopic[1]);
	}
	else
		writeInt(&ptr, pub->topicId);
	writeInt(&ptr, pub->msgId);
	memcpy(ptr, pub->data, pub->dataLen);
	rc = MQTTSPacket_send(client->socket, client->addr, pub->header, buf, datalen);
	free(buf);
	Log(LOG_PROTOCOL, 54, NULL, client->socket, client->addr, client->clientID,
			(pub->flags.QoS == 1 || pub->flags.QoS == 2) ? pub->msgId : 0,
			(pub->flags.QoS == 3) ? -1: pub->flags.QoS,
			pub->flags.retain, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_puback(Clients* client, /*char* shortTopic, int topicId, */ int msgId, char returnCode)
{
	MQTTS_PubAck packet;
	char *buf, *ptr;
	int rc = 0;
	int datalen = 5;

	FUNC_ENTRY;
	packet.header.len = 7;
	packet.header.type = MQTTS_PUBACK;

	ptr = buf = malloc(datalen);
	/*if (shortTopic)
	{
		writeChar(&ptr, shortTopic[0]);
		writeChar(&ptr, shortTopic[1]);
	}
	else */
		writeInt(&ptr, 0); /* writeInt(&ptr, topicId); */
	writeInt(&ptr, msgId);
	writeChar(&ptr, returnCode);

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, datalen);
	free(buf);

	Log(LOG_PROTOCOL, 56, NULL, socket, client->addr, client->clientID, msgId, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_pubrec(Clients* client, int msgId)
{
	int rc = MQTTSPacket_send_ack_with_msgId(client, MQTTS_PUBREC, msgId);
	Log(LOG_PROTOCOL, 60, NULL, client->socket, client->addr, client->clientID, msgId, rc);
	return rc;
}


int MQTTSPacket_send_pubrel(Clients* client, int msgId)
{
	int rc = MQTTSPacket_send_ack_with_msgId(client, MQTTS_PUBREL, msgId);
	Log(LOG_PROTOCOL, 62, NULL, client->socket, client->addr, client->clientID, msgId, rc);
	return rc;
}


int MQTTSPacket_send_pubcomp(Clients* client, int msgId)
{
	int rc = 0;

	rc = MQTTSPacket_send_ack_with_msgId(client, MQTTS_PUBCOMP, msgId);
	Log(LOG_PROTOCOL, 58, NULL, client->socket, client->addr, client->clientID, msgId, rc);
	return rc;
}


int MQTTSPacket_send_register(Clients* client, int topicId, char* topicName, int msgId)
{
	MQTTS_Register packet;
	int rc = 0;
	char *buf, *ptr;
	int datalen = 4 + strlen(topicName);

	FUNC_ENTRY;
	packet.header.len = datalen+2;
	packet.header.type = MQTTS_REGISTER;

	ptr = buf = malloc(datalen);

	writeInt(&ptr, topicId);
	writeInt(&ptr, msgId);
	memcpy(ptr, topicName, strlen(topicName));

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, datalen);
	free(buf);

	Log(LOG_PROTOCOL, 50, NULL, client->socket, client->addr, client->clientID, msgId, topicId, topicName, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_advertise(int sock, char* address, unsigned char gateway_id, short duration)
{
	PacketBuffer buf;
	int rc = 0;

	FUNC_ENTRY;
	buf = MQTTSPacketSerialize_advertise(gateway_id, duration);
	
	rc = MQTTSPacket_sendPacketBuffer(sock, address, buf);
	free(buf.data);

	Log(LOG_PROTOCOL, 30, NULL, sock, "", address, gateway_id, duration, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


/****************************************************************/
/* client/bridge specific sends                                 */
/****************************************************************/
int MQTTSPacket_send_connect(Clients* client)
{
	PacketBuffer buf;
	int rc;

	FUNC_ENTRY;
	buf = MQTTSPacketSerialize_connect(client->cleansession, (client->will != NULL), 1, client->keepAliveInterval, client->clientID);
	
	rc = MQTTSPacket_sendPacketBuffer(client->socket, client->addr, buf);
	free(buf.data);

	Log(LOG_PROTOCOL, 38, NULL, client->socket, client->addr, client->clientID, client->cleansession, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_willTopic(Clients* client)
{
	char *buf, *ptr;
	MQTTS_WillTopic packet;
	int rc, len;

	FUNC_ENTRY;
	len = 1 + strlen(client->will->topic);

	ptr = buf = malloc(len);

	packet.header.type = MQTTS_WILLTOPIC;
	packet.header.len = len+2;
	packet.flags.all = 0;
	packet.flags.QoS = client->will->qos;
	packet.flags.retain = client->will->retained;

	writeChar(&ptr, packet.flags.all);
	memcpy(ptr, client->will->topic, len-1);

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, len);
	free(buf);

	Log(LOG_PROTOCOL, 44, NULL, client->socket, client->addr, client->clientID,
			client->will->qos, client->will->retained, client->will->topic, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_willMsg(Clients* client)
{
	char *buf, *ptr;
	MQTTS_WillMsg packet;
	int rc, len;

	FUNC_ENTRY;
	len = strlen(client->will->msg);

	ptr = buf = malloc(len);

	packet.header.type = MQTTS_WILLMSG;
	packet.header.len = len+2;

	memcpy(ptr, client->will->msg, len);

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, len);
	free(buf);

	Log(LOG_PROTOCOL, 48, NULL, client->socket, client->addr, client->clientID, client->will->msg, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_pingReq(Clients* client)
{
	int rc = MQTTSPacket_send_ack(client, MQTTS_PINGREQ);
	Log(LOG_PROTOCOL, 74, NULL, client->socket, client->addr, client->clientID, rc);
	return rc;
}


int MQTTSPacket_send_disconnect(Clients* client, int duration)
{
	int rc = 0;

	FUNC_ENTRY;
	if (duration == 0)
		rc = MQTTSPacket_send_ack(client, MQTTS_DISCONNECT);
	/* TODO: when support for sleeping clients is added, need to handle duration > 0 */

	Log(LOG_PROTOCOL, 78, NULL, client->socket, client->addr, client->clientID, duration, rc);
	FUNC_EXIT_RC(rc);
	return rc;
}


int MQTTSPacket_send_subscribe(Clients* client, char* topicName, int qos, int msgId)
{
	MQTTS_Subscribe packet;
	int rc = 0;
	char *buf, *ptr;
	int datalen = 3 + strlen(topicName);

	FUNC_ENTRY;
	packet.header.len = datalen+2;
	packet.header.type = MQTTS_SUBSCRIBE;

	/* TODO: support TOPIC_TYPE_PREDEFINED/TOPIC_TYPE_SHORT */
	packet.flags.all = 0;
	packet.flags.topicIdType = MQTTS_TOPIC_TYPE_NORMAL;
	packet.flags.QoS = qos;

	ptr = buf = malloc(datalen);

	writeChar(&ptr, packet.flags.all);
	writeInt(&ptr, msgId);
	memcpy(ptr, topicName, strlen(topicName));

	rc = MQTTSPacket_send(client->socket, client->addr, packet.header, buf, datalen);
	free(buf);

	FUNC_EXIT_RC(rc);
	return rc;
}


#if defined(MQTTSPACKET_TEST)

int main(int argc, char *argv[]) {
	HeapScan();

	MQTTS_Publish* pack = malloc(sizeof(MQTTS_Publish));
	pack->header.type = MQTTS_PUBLISH;
	pack->data = malloc(10);
	MQTTSPacket_free_packet((MQTTS_Header*)pack);

	Clients* client = malloc(sizeof(Clients));
	client->cleansession = 1;
	client->clientID = "a";
	client->keepAliveInterval = 15;
	/* MQTTSPacket_send_connect(client); */
	/*  7:4:4:1:0:15:97 */
	free(client);



	Heap_terminate();
	return 0;
}

#endif

#endif
