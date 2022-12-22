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

#if !defined(MQTTSPACKET_H)
#define MQTTSPACKET_H

#include "Clients.h"
#include "Broker.h"
#include "MQTTSPacketSerialize.h"

#define MQTTS_PROTOCOL_VERSION 0x01

#define MQTTS_RC_ACCEPTED 0x00
#define MQTTS_RC_REJECTED_CONGESTED 0x01
#define MQTTS_RC_REJECTED_INVALID_TOPIC_ID 0x02

#define MQTTS_TOPIC_TYPE_NORMAL 0x00 /* topic id in publish, topic name in subscribe */
#define MQTTS_TOPIC_TYPE_PREDEFINED 0x01
#define MQTTS_TOPIC_TYPE_SHORT 0x02

#define BAD_MQTTS_PACKET -2

enum MQTTS_msgTypes
{
	MQTTS_ADVERTISE, MQTTS_SEARCHGW, MQTTS_GWINFO, MQTTS_RESERVED1, MQTTS_CONNECT,MQTTS_CONNACK,
	MQTTS_WILLTOPICREQ, MQTTS_WILLTOPIC, MQTTS_WILLMSGREQ, MQTTS_WILLMSG, MQTTS_REGISTER, MQTTS_REGACK,
	MQTTS_PUBLISH, MQTTS_PUBACK, MQTTS_PUBCOMP, MQTTS_PUBREC, MQTTS_PUBREL, MQTTS_RESERVED2,
	MQTTS_SUBSCRIBE, MQTTS_SUBACK, MQTTS_UNSUBSCRIBE, MQTTS_UNSUBACK, MQTTS_PINGREQ, MQTTS_PINGRESP,
	MQTTS_DISCONNECT, MQTTS_RESERVED3, MQTTS_WILLTOPICUPD, MQTTS_WILLTOPICRESP, MQTTS_WILLMSGUPD,
	MQTTS_WILLMSGRESP
};


typedef union
{
	unsigned char all;
#if defined(REVERSED)
	struct
	{
		int dup: 1;
		unsigned int QoS : 2;
		unsigned int retain : 1;
		unsigned int will : 1;
		unsigned int cleanSession : 1;
		unsigned int topicIdType : 2;
	};
#else
	struct
	{
		unsigned int topicIdType : 2;
		unsigned int cleanSession : 1;
		unsigned int will : 1;
		unsigned int retain : 1;
		unsigned int QoS : 2;
		int dup: 1;
	};
#endif
} MQTTSFlags;


typedef struct
{
	unsigned int len;
	char type;
} MQTTSHeader;

typedef struct
{
	MQTTSHeader header;
} MQTTS_Header;

typedef struct
{
	MQTTSHeader header;
	int msgId;
} MQTTS_Header_MsgId;

typedef struct
{
	MQTTSHeader header;
	short topicId;
	int msgId;
	char returnCode;
} MQTTS_Ack;

typedef struct
{
	MQTTSHeader header;
	char gwId;
	short duration;
} MQTTS_Advertise;

typedef struct
{
	MQTTSHeader header;
	char radius;
} MQTTS_SearchGW;

typedef struct
{
	MQTTSHeader header;
	char gwId;
	char* gwAdd; // TODO: is this right?
} MQTTS_GWInfo;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	char protocolID; // should always be MQTTS_PROTOCOL_VERSION
	short keepAlive;
	char* clientID;

} MQTTS_Connect;

typedef struct
{
	MQTTSHeader header;
	char returnCode;
} MQTTS_Connack;

typedef MQTTS_Header MQTTS_WillTopicReq;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	char* willTopic;
} MQTTS_WillTopic;

typedef MQTTS_Header MQTTS_WillMsgReq;

typedef struct
{
	MQTTSHeader header;
	char* willMsg;

} MQTTS_WillMsg;

typedef struct
{
	MQTTSHeader header;
	short topicId;
	int msgId;
	char* topicName;
	time_t lastTouch;
} MQTTS_Register;

typedef MQTTS_Ack MQTTS_RegAck;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	short topicId;
	char* shortTopic;
	int msgId;
	char* data;
	short dataLen;
} MQTTS_Publish;

typedef MQTTS_RegAck MQTTS_PubAck;

typedef MQTTS_Header_MsgId MQTTS_PubRec;

typedef MQTTS_Header_MsgId MQTTS_PubRel;

typedef MQTTS_Header_MsgId MQTTS_PubComp;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	int msgId;
	short topicId;
	char* topicName;
} MQTTS_Subscribe;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	short topicId;
	int msgId;
	char returnCode;
} MQTTS_SubAck;

typedef MQTTS_Subscribe MQTTS_Unsubscribe;

typedef MQTTS_Header_MsgId MQTTS_UnsubAck;

typedef struct
{
	MQTTSHeader header;
	char* clientId; //TODO: this is optional
} MQTTS_PingReq;


typedef struct
{
	MQTTSHeader header;
} MQTTS_PingResp;

typedef struct
{
	MQTTSHeader header;
	short duration; //TODO: this is optional
} MQTTS_Disconnect;

typedef struct
{
	MQTTSHeader header;
	MQTTSFlags flags;
	char* willTopic;
} MQTTS_WillTopicUpd;

typedef struct
{
	MQTTSHeader header;
	char* willMsg;
} MQTTS_WillMsgUpd;

typedef MQTTS_Header MQTTS_WillTopicResp;

typedef MQTTS_Header MQTTS_WillMsgResp;


int MQTTSPacket_initialize(BrokerStates* aBrokerState);
void MQTTSPacket_terminate();
char* MQTTSPacket_name(int ptype);
void* MQTTSPacket_Factory(int sock, char** clientAddr, struct sockaddr* from, int* error);

void* MQTTSPacket_header_only(MQTTSHeader header, char* data);
void* MQTTSPacket_header_with_msgId(MQTTSHeader header, char* data);
void* MQTTSPacket_ack_with_reasoncode(MQTTSHeader header, char* data);
void* MQTTSPacket_advertise(MQTTSHeader header, char* data);
void* MQTTSPacket_searchGw(MQTTSHeader header, char* data);
void* MQTTSPacket_gwInfo(MQTTSHeader header, char* data);
void* MQTTSPacket_connect(MQTTSHeader header, char* data);
void* MQTTSPacket_connack(MQTTSHeader header, char* data);
void* MQTTSPacket_willTopic(MQTTSHeader header, char* data);
void* MQTTSPacket_willMsg(MQTTSHeader header, char* data);
void* MQTTSPacket_register(MQTTSHeader header, char* data);
void* MQTTSPacket_publish(MQTTSHeader header, char* data);
void* MQTTSPacket_subscribe(MQTTSHeader header, char* data);
void* MQTTSPacket_suback(MQTTSHeader header, char* data);
void* MQTTSPacket_unsubscribe(MQTTSHeader header, char* data);
void* MQTTSPacket_pingreq(MQTTSHeader header, char* data);
void* MQTTSPacket_disconnect(MQTTSHeader header, char* data);
void* MQTTSPacket_willTopicUpd(MQTTSHeader header, char* data);
void* MQTTSPacket_willMsgUpd(MQTTSHeader header, char* data);


void MQTTSPacket_free_packet(MQTTS_Header* pack);

void MQTTSPacket_free_gwInfo(void* pack);
void MQTTSPacket_free_connect(void* pack);
void MQTTSPacket_free_willTopic(void* pack);
void MQTTSPacket_free_willMsg(void* pack);
void MQTTSPacket_free_register(void* pack);
void MQTTSPacket_free_publish(void* pack);
void MQTTSPacket_free_subscribe(void* pack);
void MQTTSPacket_free_pingreq(void* pack);
void MQTTSPacket_free_willTopicUpd(void* pack);
void MQTTSPacket_free_willMsgUpd(void* pack);



int MQTTSPacket_send(int socket, char* addr, MQTTSHeader header, char* buffer, int buflen);
int MQTTSPacket_send_publish(Clients* client, MQTTS_Publish* pub);
int MQTTSPacket_send_connack(Clients* client, int returnCode);
int MQTTSPacket_send_willTopicReq(Clients* client);
int MQTTSPacket_send_willMsgReq(Clients* client);
int MQTTSPacket_send_pingResp(Clients* client);
int MQTTSPacket_send_willTopicResp(Clients* client);
int MQTTSPacket_send_willMsgResp(Clients* client);
int MQTTSPacket_send_regAck(Clients* client, int msgId, int topicId, char rc);
int MQTTSPacket_send_puback(Clients* client, int topicId, int msgId, char returnCode);
int MQTTSPacket_send_pubrec(Clients* client, int msgId);
int MQTTSPacket_send_pubrel(Clients* client, int msgId);
int MQTTSPacket_send_pubcomp(Clients* client, int msgId);
int MQTTSPacket_send_subAck(Clients* client, MQTTS_Subscribe* sub, int topicId, int qos, char returnCode);
int MQTTSPacket_send_unsubAck(Clients* client, int msgId);
int MQTTSPacket_send_register(Clients* client, int topicId, char* topicName, int msgId);
int MQTTSPacket_send_advertise(int sock, char* address, unsigned char gateway_id, short duration);


int MQTTSPacket_send_connect(Clients* client);
int MQTTSPacket_send_willTopic(Clients* client);
int MQTTSPacket_send_willMsg(Clients* client);
int MQTTSPacket_send_pingReq(Clients* client);
int MQTTSPacket_send_disconnect(Clients* client, int duration);
int MQTTSPacket_send_subscribe(Clients* client, char* topicName, int qos, int msgId);

#endif /* MQTTSPACKET_H */
