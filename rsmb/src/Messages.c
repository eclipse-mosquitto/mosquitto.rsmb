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
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/

/**
 * @file
 * Message retrieval from storage and indexing
 */

#include "Messages.h"
#include "Log.h"
#include "StackTrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#if defined(WIN32)
#define snprintf _snprintf
#endif

#include "Heap.h"


#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static char* message_list[MAX_MESSAGE_INDEX+1];	/**< array of messages */

static char* protocol_message_list[] =
{
	"%d %s -> CONNECT cleansession: %d noLocal: %d (%d)", /* 0, was 131, 68 and 69 */
	"%d %s <- CONNACK rc: %d", /* 1, was 132 */
	"%d %s -> CONNACK rc: %d (%d)", /* 2, was 138 */
	"%d %s <- PINGREQ", /* 3, was 35 */
	"%d %s -> PINGRESP (%d)", /* 4 */
	"%d %s <- DISCONNECT", /* 5 */
	"%d %s <- SUBSCRIBE msgid: %d", /* 6, was 39 */
	"%d %s -> SUBACK msgid: %d (%d)", /* 7, was 40 */
	"%d %s <- UNSUBSCRIBE msgid: %d", /* 8, was 41 */
	"%d %s -> UNSUBACK msgid: %d (%d)", /* 9 */
	"%d %s -> PUBLISH msgid: %d qos: %d retained: %d (%d)", /* 10, was 42 */
	"%d %s <- PUBLISH msgid: %d qos: %d retained: %d", /* 11, was 46 */
	"%d %s -> PUBACK msgid: %d (%d)", /* 12, was 47 */
	"%d %s -> PUBREC msgid: %d (%d)", /* 13, was 48 */
	"%d %s <- PUBACK msgid: %d", /* 14, was 49 */
	"%d %s <- PUBREC msgid: %d", /* 15, was 53 */
	"%d %s -> PUBREL msgid: %d (%d)", /* 16, was 57 */
	"%d %s <- PUBREL msgid %d", /* 17, was 58 */
	"%d %s -> PUBCOMP msgid %d (%d)", /* 18, was 62 */
	"%d %s <- PUBCOMP msgid:%d", /* 19, was 63 */
	"%d %s -> PINGREQ (%d)", /* 20, was 137 */
	"%d %s <- PINGRESP", /* 21, was 70 */
	"%d %s -> SUBSCRIBE msgid: %d (%d)", /* 22, was 72 */
	"%d %s <- SUBACK msgid: %d", /* 23, was 73 */
	"%d %s <- UNSUBACK msgid: %d", /* 24, was 74 */
	"%d %s -> UNSUBSCRIBE msgid: %d (%d)", /* 25, was 106 */
	"%d %s <- CONNECT", /* 26 */
	"%d %s -> PUBLISH qos: 0 retained: %d (%d)", /* 27 */
	"%d %s -> DISCONNECT (%d)", /* 28 */
	"%d %s -- reserved", /* 29 */
#if defined(MQTTS)
	"%d %s %s -> MQTT-S ADVERTISE gateway_id: %d duration: %d (%d)", /* 30 */
	"%d %s %s <- MQTT-S ADVERTISE gateway_id: %d duration: %d", /* 31 */
    "%d %s %s -> MQTT-S SEARCHGW", /* 32 */
    "%d %s %s <- MQTT-S SEARCHGW", /* 33 */
    "%d %s %s -> MQTT-S GWINFO", /* 34 */
    "%d %s %s <- MQTT-S GWINFO", /* 35 */
    "reserved", /* 36 */
    "reserved", /* 37 */
    "%d %s %s -> MQTT-S CONNECT cleansession: %d (%d)", /* 38 */
    "%d %s %s <- MQTT-S CONNECT cleansession: %d", /* 39 */
    "%d %s %s -> MQTT-S CONNACK returncode %d (%d)", /* 40 */
    "%d %s %s <- MQTT-S CONNACK returncode %d", /* 41 */
    "%d %s %s -> MQTT-S WILLTOPICREQ (%d)", /* 42 */
    "%d %s %s <- MQTT-S WILLTOPICREQ", /* 43 */
    "%d %s %s -> MQTT-S WILLTOPIC qos: %d retained: %d: topicname %.10s (%d)", /* 44 */
    "%d %s %s <- MQTT-S WILLTOPIC qos: %d retained: %d: topicname %.10s", /* 45 */
    "%d %s %s -> MQTT-S WILLMSGREQ (%d)", /* 46 */
    "%d %s %s <- MQTT-S WILLMSGREQ", /* 47 */
    "%d %s %s -> MQTT-S WILLMSG msg: %.20s (%d)", /* 48 */
    "%d %s %s <- MQTT-S WILLMSG msg: %.20s", /* 49 */
    "%d %s %s -> MQTT-S REGISTER msgid: %d topicid: %d topicname: %.10s (%d)", /* 50 */
    "%d %s %s <- MQTT-S REGISTER msgid: %d topicid: %d topicname: %.10s", /* 51 */
    "%d %s %s -> MQTT-S REGACK msgid: %d topicid: %d returncode: %d (%d)", /* 52 */
    "%d %s %s <- MQTT-S REGACK msgid: %d topicid: %d returncode: %d", /* 53 */
	"%d %s %s -> MQTT-S PUBLISH msgid: %d qos: %d retained: %d (%d)", /* 54 */
	"%d %s %s <- MQTT-S PUBLISH msgid: %d qos: %d retained: %d", /* 55 */
	"%d %s %s -> MQTT-S PUBACK msgid: %d (%d)", /* 56 */
	"%d %s %s <- MQTT-S PUBACK msgid: %d", /* 57 */
	"%d %s %s -> MQTT-S PUBCOMP msgid: %d (%d)", /* 58 */
	"%d %s %s <- MQTT-S PUBCOMP msgid: %d", /* 59 */
	"%d %s %s -> MQTT-S PUBREC msgid: %d (%d)", /* 60 */
	"%d %s %s <- MQTT-S PUBREC msgid: %d", /* 61 */
	"%d %s %s -> MQTT-S PUBREL msgid: %d (%d)", /* 62 */
	"%d %s %s <- MQTT-S PUBREL msgid: %d", /* 63 */
    "reserved", /* 64 */
    "reserved", /* 65 */
	"%d %s %s -> MQTT-S SUBSCRIBE msgid: %d qos: %d topicIdType %d", /* 66 */
	"%d %s %s <- MQTT-S SUBSCRIBE msgid: %d qos: %d topicIdType %d", /* 67 */
	"%d %s %s -> MQTT-S SUBACK msgid: %d topicid: %d returncode: %d (%d)", /* 68 */
	"%d %s %s <- MQTT-S SUBACK msgid: %d topicid: %d returncode: %d", /* 69 */
	"%d %s %s -> MQTT-S UNSUBSCRIBE msgid: %d qos: %d topicIdType %d", /* 70 */
	"%d %s %s <- MQTT-S UNSUBSCRIBE msgid: %d qos: %d topicIdType %d", /* 71 */
	"%d %s %s -> MQTT-S UNSUBACK msgid: %d (%d)", /* 72 */
	"%d %s %s <- MQTT-S UNSUBACK msgid: %d", /* 73 */
	"%d %s %s -> MQTT-S PINGREQ (%d)", /* 74 */
	"%d %s %s <- MQTT-S PINGREQ", /* 75 */
	"%d %s %s -> MQTT-S PINGRESP (%d)", /* 76 */
	"%d %s %s <- MQTT-S PINGRESP", /* 77 */
	"%d %s %s -> MQTT-S DISCONNECT duration: %d (%d)", /* 78 */
	"%d %s %s <- MQTT-S DISCONNECT duration: %d", /* 79 */
    "reserved", /* 80 */
    "reserved", /* 81 */
	"%d %s %s -> MQTT-S WILLTOPICUPD msgid: %d (%d)", /* 82 */
	"%d %s %s <- MQTT-S WILLTOPICUPD msgid: %d", /* 83 */
	"%d %s %s -> MQTT-S WILLTOPICRESP returnCode: %d (%d)", /* 84 */
	"%d %s %s <- MQTT-S WILLTOPICRESP returnCode: %d", /* 85 */
	"%d %s %s -> MQTT-S WILLMSGUPD message: %.10s (%d)", /* 86 */
	"%d %s %s <- MQTT-S WILLMSGUPD message: %.10s", /* 87 */
	"%d %s %s -> MQTT-S WILLMSGRESP returnCode: %d (%d)", /* 88 */
	"%d %s %s <- MQTT-S WILLMSGRESP returnCode: %d", /* 89 */
#endif
};

static char* trace_message_list[] =
{
	"Processing queued messages for client %s", /* 0, was 25 */
	"Moving message from queued to inflight for client %s", /* 1, was 26 */
	"Removed client %s from bstate->clients, socket %d", /* 2, was 37 */
	"Queueing publish to client %s at qos %d", /* 3, was 44 */
	"PUBACK received from client %s for message id %d - removing publication", /* 4, was 52 */
	"PUBCOMP received from client %s for message id %d - removing publication", /* 5, was 67 */
	"FD_SETSIZE is %d", /* 6, was 76 */
	"We already have a socket %d in the list", /* 7, was 81 */
	"Return code %d from read select", /* 8, was 83 */
	"Return code %d from write select", /* 9, was 84 */
	"Accepted socket %d from %s:%d", /* 10, was 85 */
	"GetReadySocket returning %d", /* 11, was 86 */
	"%d bytes expected but %d bytes now received", /* 12, was 87 */
	"Removed socket %d", /* 13, was 90 */
	"New socket %d for %s, port %d", /* 14, was 93 */
	"Connect pending", /* 15, was 94 */
	"ContinueWrite wrote +%lu bytes on socket %d", /* 16, was 95 */
	"Packet_Factory: unhandled packet type %d", /* 17, was 107 */
	"will %s %s %d", /* 18, was 108 */
	"index is now %d, headerlen %d", /* 19, was 110 */
	"queueChar: index is now %d, headerlen %d", /* 20, was 114 */
	"Updating subscription %s, %s, %d", /* 21, was 115 */
	"Adding subscription %s, %s, %d", /* 22, was 116 */
	"Removing subscription %s, %s, %d", /* 23, was 117 */
	"Subscription %s %d %s", /* 24, was 119 */
	"Adding client %s to subscribers list", /* 25, was 120 */
	"Matching %s against %s", /* 26, was 121 */
	"Matched %s against %s", /* 27, was 122 */
	"%s connected %d, connect_state %d", /* 28, was 126 */
	"%*s(%d)> %s:%d", /* 29 */
	"%*s(%d)< %s:%d", /* 30 */
	"%*s(%d)< %s:%d (%d)", /* 31 */
	"No bytes written in publication, so no need to suspend write", /* 32 */
	"Partial write: %ld bytes of %d actually written on socket %d", /* 33 */
	"Failed to remove socket %d", /* 34 */
	"Failed to remove pending write from socket buffer list",  /* 35 */
	"Failed to remove pending write from list",  /* 36 */
	"Storing unsent QoS 0 message", /* 37 */
	"Unable to remove message from queued list", /* 38 */
	"Failed to remove client from bstate->clients", /* 39 */
};

#if defined(WIN32)
	char sep = '\\';
#else
	char sep = '/';
#endif


/**
 * Find the location of this program 
 * @param buf a character buffer to hold the directory name
 * @param bufsize the size of buf
 * @return the success return code
 */
int Messages_findMyLocation(char* buf, int bufsize)
{
	int rc = -1;
#if defined(WIN32)
	wchar_t wbuf[256];
#endif
 	
	FUNC_ENTRY;
#if defined(WIN32) 	
	rc = GetModuleFileName(NULL, wbuf, bufsize);
	wcstombs(buf, wbuf, bufsize);
#else /* Linux */
#ifdef ESP_PLATFORM
    rc = sprintf(buf, "/sdcard/dummy.exe");
#else
	rc = (int)readlink("/proc/self/exe", buf, bufsize);
#endif
#endif
	if (rc > 0 && rc < bufsize)
	{
		char* pos;
		if ((pos = strrchr(buf, sep)) != NULL)
			*pos = '\0'; /* remove trailing program name, leaving just the directory */
		rc = 0; /* success */
	}
 	else
		rc = -1; /* failure */
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * maximum length of a message format string read from the file
 */
#define max_msg_len 120

/**
 * Initialize the message module
 * @param bstate pointer to the broker state structure
 * @return completion code, success =0
 */
int Messages_initialize(BrokerStates* bstate)
{
	FILE* rfile = NULL;
	char buf[max_msg_len];
	int count = 0;
	int rc = -99;
	char fn[20] = "Messages.";

	FUNC_ENTRY;
	strcat(fn, bstate->version);
	if ((rfile = fopen(fn, "r")) == NULL)
	{
		char fullfn[256];
		sprintf(fullfn, "..%cmessages%c%s", sep, sep, fn);

		if ((rfile = fopen(fullfn, "r")) == NULL)
		{
			if (Messages_findMyLocation(fullfn, sizeof(fullfn)) == 0)
			{
				int dirlength = strlen(fullfn);

				snprintf(&fullfn[dirlength], sizeof(fullfn) - dirlength, "%c%s", sep, fn);

				rfile = fopen(fullfn, "r");
				if (rfile == NULL)
				{
					snprintf(&fullfn[dirlength + 1], sizeof(fullfn) - dirlength, "..%cmessages%c%s", sep, sep, fn);
					rfile = fopen(fullfn, "r");
				}
			}
		}
	}

	if (rfile == NULL)
		Log(LOG_WARNING, 9989, "Could not find or open message file %s", fn);
	else
	{
		char* msg;
		memset(message_list, '\0', sizeof(message_list));
		while (fgets(buf, max_msg_len, rfile) != NULL && count < MESSAGE_COUNT)
		{
			int msgindex = 0;

			if (buf[0] == '#')
				continue; /* it's a comment */
			msgindex = atoi(buf);
			if (msgindex < ARRAY_SIZE(message_list))
			{
				char* start = strchr(buf, '=');
				int msglen = strlen(buf);

				if (start == NULL)
					continue;
				if (buf[msglen - 1] == '\n')
					buf[--msglen] = '\0';
				if (buf[msglen - 1] == '\r') /* this can happen if we read a messages file in with gcc with windows */
					buf[--msglen] = '\0';				/* end of line markers */
				msglen -= ++start - buf;
				msg = (char*)malloc(msglen + 1);
				strcpy(msg, start);
				message_list[msgindex] = msg;
				count++;
			}
		}
		fclose(rfile);
		if (count != MESSAGE_COUNT)
			Log(LOG_WARNING, 9988, "Found %d instead of %d messages in file %s", count, MESSAGE_COUNT, fn);
		else
			rc = 0;
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Get a log message by its index
 * @param index the integer index
 * @param log_level the log level, used to determine which message list to use
 * @return the message format string
 */
char* Messages_get(int index, int log_level)
{
	char* msg = NULL;

	if (log_level < TRACE_PROTOCOL || log_level > LOG_WARNING)
		msg = (index >= 0 && index < ARRAY_SIZE(trace_message_list)) ? trace_message_list[index] : NULL;
	else if (log_level == TRACE_PROTOCOL)
		msg = (index >= 0 && index < ARRAY_SIZE(protocol_message_list)) ? protocol_message_list[index] : NULL;
	else
		msg = (index >= 0 && index < ARRAY_SIZE(message_list)) ? message_list[index] : NULL;
	return msg;
}


/**
 * Free up allocated message storage
 */
void Messages_terminate()
{
	int i;

	FUNC_ENTRY;
	for (i = 0; i < ARRAY_SIZE(message_list); ++i)
	{
		if (message_list[i])
			free(message_list[i]);
	}
	FUNC_EXIT;
}
