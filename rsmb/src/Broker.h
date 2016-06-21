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
 *    Ian Craggs - updates for the async client
 *******************************************************************************/


#if !defined(BROKER_H)
#define BROKER_H

#include "SubsEngine.h"
#include "Bridge.h"
#include "Tree.h"

#define true 1
#define false 0

#define assert(x) if (!x) printf("Assertion error %s\n", # x);

#ifdef __GNUC__
  #define ATTR_UNUSED __attribute__((unused))
#else
  #define ATTR_UNUSED
#endif

/*BE
def INT { n32 "value" }
def TMP { n32 "tmp" }
def DATA { 100 n8 hex "data" }
def STRING { buf 100 zterm asc "data" }
map bool
{
   "false" .
   "true"  .
}
BE*/

/*BE
include "Log" // include this even though it isn't included in the .h
include "SubsEngine"
include "Bridge"
include "Users"
include "Tree"
include "MQTTProtocol"
BE*/

/*BE
map BROKER_RUN_STATE {
  "BROKER_STOPPED" .
  "BROKER_RUNNING" .
  "BROKER_STOP_REQUESTED" .
  "BROKER_STOPPING" .
}
BE*/
/**
 * broker run states
 */
enum broker_run_state { BROKER_STOPPED, BROKER_RUNNING, BROKER_STOPPING };

/*BE

defTree(CLIENTS)

def BROKERSTATES
{
   n32 ptr STRING open "version"
   n32 ptr STRING open "build_timestamp"
   n32 dec "max_inflight_messages"
   n32 dec "max_queued_messages"
   n32 dec "retry_interval"
   n32 ptr CLIENTSTree open "clients"
   n32 ptr CLIENTSTree open "disconnected_clients"
$ifdef MQTTS
   n32 ptr CLIENTSTree open "mqtts_clients"
   n32 ptr CLIENTSTree open "disconnected_mqtts_clients"
$endif
$ifndef MQTTCLIENT
   n32 dec "connection_messages"
   n32 ptr SUBSCRIPTIONENGINES "SubscriptionEngine"
   n32 map BROKER_RUN_STATE "state"
   n32 dec "hup_signal"
   n32 ptr STRING open "ffdc_location"
   n32 dec "ffdc_count"
   n32 ptr STRING open "persistence_location"
   n32 dec "persistence"
   n32 map bool "autosave_on_changes"
   n32 dec "autosave_interval"
$ifdef WIN32
   n64 time "last_autosave"
$else
   n32 time "last_autosave"
$endif
   n32 ptr STRINGList open "clientid_prefixes"
   BRIDGES "bridge"
$ifdef SINGLE_LISTENER
   n32 dec "port"
   n32 ptr STRING open "bind_address"
   n32 dec "max_connections"
   n32 map bool "ipv6"
$else
   n32 ptr LISTENERList open "listeners"
$endif
   n32 ptr STRING open "password_file"
   n32 ptr USERList open "users"
   n32 ptr STRING open "acl_file"
   n32 map bool "allow_anonymous"
   n32 ptr RULEList open "defaultACL"
   n32 dec "msgs_received"
   n32 dec "msgs_sent"
   n32 unsigned dec "bytes_received"
   n32 unsigned dec "bytes_sent"
$ifdef WIN32
   n64 time "start_time"
$else
   n32 time "start_time"
$endif
$endif
$ifdef MQTTS
	n32 dec "max_mqtts_packet_size"
$endif
}
BE*/
/**
 * Global broker state.
 */
typedef struct
{
	char* version;				/**< broker version */
	char* timestamp;			/**< build timestamp */
	int max_inflight_messages; 	/**< per client, outbound */
	int max_queued_messages;	/**< per client, outbound */
	int retry_interval;			/**< MQTT retry interval */
	Tree* clients;				/**< list of clients, main index is by socket */
	Tree* disconnected_clients; 	/**< list of disconnected clients */
#if defined(MQTTS)
	Tree* mqtts_clients; 		/**< list of clients connected via MQTTS */
	Tree* disconnected_mqtts_clients; 	/**< list of disconnected clients for MQTTS */
#endif
#if !defined(MQTTCLIENT)
	int connection_messages;	/**< connection messages for bridge clients */
	SubscriptionEngines* se;	/**< subscription engine state */
	volatile int state;			/**< signal handling indicator */
	volatile int hup_signal;	/**< hup signal handling indicator */
	char* ffdc_location;		/**< ffdc output (if different from persistence) */
	int ffdc_count;         /**< number of ffdcs issued since start */
	char* persistence_location;	/**< persistence directory location */
	int persistence; 			/**< retained message and subscription persistence */
	int autosave_on_changes;	/**< autosave on number of state changes? */
	int autosave_interval;		/**< autosave on time interval? */
	time_t last_autosave;		/**< time of last autosave */
	List* clientid_prefixes;	/**< list of authorized client prefixes */
	Bridges bridge;				/**< bridge state */
#if defined(SINGLE_LISTENER)
	int port;					/**< single listener server port */
	char* bind_address;			/**< single listener server bind address */
	int max_connections;		/**< single listener max no of connections */
	int ipv6;					/**< use ipv6? */
#else
	List* listeners;			/**< list of listeners */
#endif
	char* password_file;        /**< password file location */
	List* users;                /**< known users & passwords */
	char* acl_file;             /**< acl file location */
	int allow_anonymous;        /**< whether anonymous users are allowed to connect */
	List* defaultACL;           /**< acl that applied to all users */
	unsigned int msgs_received;	/**< statistics: number of messages received */
	unsigned int msgs_sent;		/**< statistics: number of messages sent */
	unsigned long int bytes_received;	/**< statistics: number of bytes received */
	unsigned long int bytes_sent;		/**< statistics: number of bytes sent */
	time_t start_time;			/**< start time of the broker for uptime calculation */
#endif
#if defined(MQTTS)
	int max_mqtts_packet_size;  /**< max size of MQTT-S packets we can receive.  We have to allocate a memory
	                               buffer of this size, so we may want to reduce it.  The current max is 65535.  */
#endif
} BrokerStates;	/**< Global broker state */

/*BE
def main
{
   BROKERSTATES "Broker States"
   n32 ptr STRING open "Config file"
   SOCKETS "sockets"
   MQTTPROTOCOL "MQTTProtocol"
}
BE*/

int Broker_stop(char* s);
int Broker_dumpHeap(char* dest);
char* Broker_recordFFDC(char* symptoms);

#endif
