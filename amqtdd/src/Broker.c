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
 * Main broker module.
 */

/**
 * @mainpage
 * @section design_principles Design Principles
 *
 * 	- single-threaded, to
 * 		-# allow control of time-slicing
 * 		-# avoid threading synchronization problems
 * 	- memory tracking of heap allocation and freeing to:
 * 		-# eliminate memory leaks
 * 		-# keep track of memory allocation amount, to be able to limit it
 * 	- small size is favoured over speed
 *
 * @section module_relationships Module Relationships
 *
 * The diagram following shows roughly how the modules fit approximately into layers.  In general,
 * function calls are made by modules in upper layers to those in the same or lower layers, but not
 * to layers above.  The "utility" modules are used by all layers.
 *
 * <img src="../RSMB.jpeg" />
 *
 *
 *
 */


#include <signal.h>
#include <stdlib.h>
#if !defined(WIN32)
#define GETTIMEOFDAY 1
#endif

#if defined(GETTIMEOFDAY)
	#include <sys/time.h>
#else
	#include <sys/timeb.h>
#endif

#include "Broker.h"
#include "Log.h"
#include "StackTrace.h"
#include "Protocol.h"
#include "Socket.h"
#include "SubsEngine.h"
#include "Persistence.h"
#include "Users.h"
#include "Heap.h"
#include "Messages.h"

void Users_initialize(BrokerStates* aBrokerState);

int Broker_startup();
void Broker_shutdown(int);

static BrokerStates BrokerState =
{
	NULL, 		/**< version */
	NULL,		/**< build timestamp */
	10, 		/**< max_inflight_messages */
	1000, 		/**< max_queued_messages */
	20, 		/**< retry_interval */
	NULL, 		/**< clients */
	1, 			/**< connection_messages */
	NULL,  		/**< se */
	BROKER_RUNNING, /**< state */
	0, 			/**< hup_signal */
	NULL,       /**< ffdc_location */
	NULL, 		/**< persistence_location */
	0, 			/**< persistence */
	0, 			/**< autosave_on_changes */
	1800, 		/**< autosave_interval */
	0L, 		/**< last_autosave */
	NULL, 		/**< clientid_prefixes */
	{ NULL }, 	/**< bridge */
#if defined(SINGLE_LISTENER)
	1883, 		/**< port */
	NULL, 		/**< bind_address */
	-1, 		/**< max_connections */
	0,			/**< ipv6 */
#else
	NULL, 		/**< listeners */
#endif
	NULL,       /**< password file location */
	NULL,       /**< known users & passwords */
	NULL,       /**< acl file location */
	0,          /**< whether anonymous users are allowed to connect */
	NULL,       /**< default ACL */
	0, 			/**< unsigned int msgs_received; */
	0, 			/**< unsigned int msgs_sent; */
	0L,			/**< unsigned long int bytes_received; */
	0L,			/**< unsigned long int bytes_sent; */
	0L,			/**< start time of the broker for uptime calculation */
};	/**< the global broker state structure */


char* config = "amqtdd.cfg";	/**< default broker configuration file name */
int config_set = 0;				/**< has the broker config file name been set at startup? */
int segv_flag = 0;				/**< has a segfault occurred? */

#include "StackTrace.h"


/**
 * Signal handling function to stop the broker.
 */
void finish(int sig)
{
	if (BrokerState.state == BROKER_RUNNING)
		BrokerState.state = BROKER_STOPPING;
}

int Broker_stop(char* s)
{
	finish(0);
	return 0;
}

#if !defined(SIGHUP)
#define SIGHUP 1
#endif

/**
 * HUP signal handling function
 */
void HUPHandler(int sig)
{
	BrokerState.hup_signal = 1;
}


#if !defined(WIN32)
void linux_segv(int signo, siginfo_t* info, void* context)
{
	const char *signal_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};
	static char symptoms[1024];

	sprintf(symptoms, "SEGV information\n"
			"info.si_signo = %d\n"
			"info.si_errno = %d\n"
			"info.si_code  = %d (%s)\n"
			"info.si_addr  = %p\n",
			info->si_signo, info->si_errno, info->si_code, signal_codes[info->si_code], info->si_addr);
#else
/**
 * Normal segv handling function
 * @param sig signal that invoked this handler
 */
void segv(int sig)
{
	static char symptoms[128];
	sprintf(symptoms, "SEGV error");
#endif
	printf("\n##### SEGV signal received - recording FFDC and stopping #####\n");
	signal(SIGSEGV, NULL);
	segv_flag = 1;
	Broker_recordFFDC(symptoms);
	exit(-1);
}


int set_sigsegv()
{
#if defined(WIN32)
	int rc = signal(SIGSEGV, segv) != SIG_ERR;
	return rc;
#else
	struct sigaction action;
	void linux_segv(int signo, siginfo_t* info, void* context);

	action.sa_flags = SA_SIGINFO; /* indicates the sa_sigaction field is to be used */
	action.sa_sigaction = linux_segv;
	sigemptyset(&action.sa_mask);

	return sigaction(SIGSEGV, &action, NULL);
#endif
}



void getopts(int argc, char** argv)
{
	int count = 1;

	while (count < argc)
	{
		if (strcmp(argv[count], "--daemon") == 0)
			trace_settings.isdaemon = 1;
		else
		{
			config = argv[count];
			Log(LOG_INFO, 49, "Configuration file name is %s", config);
			config_set = 1;
		}
		count++;
	}
}


/**
 * Main broker startup function
 * @param argc number of elements of argv
 * @param argv command line strings
 * @return system return code
 */
int main(int argc, char* argv[])
{
	int rc = 0;
#define BUILD_TIMESTAMP __DATE__ " " __TIME__ /* __TIMESTAMP__ */
#define BROKER_VERSION "1.2.0.12" /* __VERSION__ */
#define PRODUCT_NAME "MQTT Daemon for Devices"

	static char* broker_version_eye = NULL;
	static char* broker_timestamp_eye = NULL;

	FUNC_ENTRY_NOLOG;
	broker_timestamp_eye = "AMQTDD_Timestamp " BUILD_TIMESTAMP;
	broker_version_eye = "AMQTDD_Version " BROKER_VERSION;
	BrokerState.version = BROKER_VERSION;
	BrokerState.timestamp = BUILD_TIMESTAMP;
	
	srand(time(NULL));

	Heap_initialize();
	Log_initialize();

	Log(LOG_INFO, 9999, PRODUCT_NAME);
	Log(LOG_INFO, 9998, "Part of Project Mosquitto in Eclipse\n("
                      "http://projects.eclipse.org/projects/technology.mosquitto)");
	getopts(argc, argv);

	if (Messages_initialize(&BrokerState) != 0)
		goto no_messages;

	Log(LOG_INFO, 53, NULL, BrokerState.version, BrokerState.timestamp);
	Log(LOG_INFO, 54, "%s %s", Messages_get(54, LOG_INFO), "" 
#if !defined(NO_BRIDGE)
	" bridge"
#endif
#if defined(MQTTMP)
	" MQTTMP"
#endif
#if defined(MQTTS)
	" MQTTS"
#endif
	);

	if ((rc = Broker_startup()) == 0)
	{
		SubscriptionEngines_setRetained(BrokerState.se, "$SYS/broker/version", 0,
			BrokerState.version, strlen(BrokerState.version));
		SubscriptionEngines_setRetained(BrokerState.se, "$SYS/broker/timestamp", 0,
					BrokerState.timestamp, strlen(BrokerState.timestamp));
		while (BrokerState.state == BROKER_RUNNING)
		{
			Protocol_timeslice();
			#if !defined(NO_BRIDGE)
				Bridge_timeslice(&BrokerState.bridge);
			#endif
		}
		Log(LOG_INFO, 46, NULL);
		#if !defined(NO_BRIDGE)
			Bridge_stop(&BrokerState.bridge);
		#endif
		while (BrokerState.state == BROKER_STOPPING)
		{
			Protocol_timeslice();
			#if !defined(NO_BRIDGE)
				Bridge_timeslice(&BrokerState.bridge);
			#endif
		}
	}
	Broker_shutdown(rc);

	Log(LOG_INFO, 47, NULL);

no_messages:
	Messages_terminate();
	Log_terminate();
	Heap_terminate();

	/*FUNC_EXIT_NOLOG(rc); would anyone ever see this? */
	return rc;
}


/**
 * Start up the broker.
 * @return completion code, success == 0
 */
int Broker_startup()
{
	int rc;

	FUNC_ENTRY;
	signal(SIGINT, finish);
	signal(SIGTERM, finish);
	signal(SIGHUP, HUPHandler);
	#if !defined(_DEBUG)
		set_sigsegv();
	#endif

	BrokerState.clients = ListInitialize();
#if !defined(SINGLE_LISTENER)
	BrokerState.listeners = ListInitialize();
#endif
	Users_initialize(&BrokerState);
	if ((rc = Persistence_read_config(config, &BrokerState, config_set)) == 0)
	{
		BrokerState.se = SubscriptionEngines_initialize();
		rc = Protocol_initialize(&BrokerState);
#if !defined(SINGLE_LISTENER)
		rc = Socket_initialize(BrokerState.listeners);
#else
		rc = Socket_initialize(BrokerState.bind_address, BrokerState.port, BrokerState.ipv6);
#endif
#if !defined(NO_BRIDGE)
		Bridge_initialize(&(BrokerState.bridge), BrokerState.se);
#endif
		Log_setPublish(true);
	}
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Shutdown the broker.
 * @param rc - startup success code
 */
void Broker_shutdown(int rc)
{
	FUNC_ENTRY;
	if (rc != -99)
	{
		time_t now = 0;

		Log_setPublish(false);
#if !defined(NO_BRIDGE)
		Bridge_terminate(&(BrokerState.bridge));
#endif
		if (rc != -98)
		{
			if (rc != -97)
			{
				if (BrokerState.persistence)
					SubscriptionEngines_save(BrokerState.se);
				Protocol_terminate();
				Socket_terminate();
				SubscriptionEngines_terminate(BrokerState.se);

				Log(LOG_INFO, 44, NULL, BrokerState.msgs_sent);
				Log(LOG_INFO, 43, NULL, BrokerState.msgs_received);
				time(&(now));
				Log(LOG_INFO, 42, NULL, (int)difftime(now, BrokerState.start_time));
				Log(LOG_INFO, 55, NULL, Heap_get_info()->max_size);
			}
		}
		ListFree(BrokerState.clients);
		Persistence_free_config(&BrokerState);
	}
	FUNC_EXIT;
}


/**
 * Dump the broker heap so it can be viewed for debugging.
 * @param dest - stream destination to write the information to
 * @return completion code, success == 0
 */
int Broker_dumpHeap(char* dest)
{
	FILE* file = NULL;
	int rc = 0;

	if ((file = Log_destToFile(dest)) == NULL)
		rc = -1;
	else
	{
		List* log = Log_getLogBuffer();
		List* trace = Log_getTraceBuffer();
		Sockets* sockets = Socket_getSockets();
		int ssize = sizeof(Sockets);
		int bssize = sizeof(BrokerStates);

		fprintf(file, "=========== Start of heap dump ==========\n");
		if (fwrite(&bssize, sizeof(bssize), 1, file) != 1)
			rc = -1;
		else if (fwrite(&BrokerState, bssize, 1, file) != 1)
			rc = -1;
		else if (fwrite(&log, sizeof(log), 1, file) != 1)
			rc = -1;
		else if (fwrite(&trace, sizeof(trace), 1, file) != 1)
			rc = -1;
		else if (fwrite(&config, sizeof(config), 1, file) != 1)
			rc = -1;
		else if (fwrite(&ssize, sizeof(ssize), 1, file) != 1)
			rc = -1;
		else if (fwrite(sockets, ssize, 1, file) != 1)
			rc = -1;
		else if (HeapDumpString(file, config) != 0)
			rc = -1;
		else if (HeapDumpString(file, BrokerState.version) != 0)
			rc = -1;
		else if (HeapDumpString(file, BrokerState.persistence_location) != 0)
			rc = -1;
		else
			rc = HeapDump(file);
		fprintf(file, "\n=========== End of heap dump ==========\n\n");
		if (file != stdout && file != stderr)
			fclose(file);
	}
	return rc;
}


/**
 * Record First Failure Data Capture information for problem determination.
 * @param symptoms - a symptom string to write along with the rest of the info
 * @return FFDC filename
 */
char* Broker_recordFFDC(char* symptoms)
{
	static char filename[160];
	struct tm *timeinfo;
#if defined(GETTIMEOFDAY)
	struct timeval ts;
#else
	struct timeb ts;
#endif
	FILE* file = NULL;
	char* ptr = filename;
	int mem_leak = 0;

	filename[0] = '\0';
	if (BrokerState.ffdc_location && strcmp(BrokerState.ffdc_location, "off") == 0)
	{
		if (segv_flag)
			printf("%s\n", Messages_get(48, LOG_WARNING));
		else
			Log(LOG_WARNING, 48, NULL);
		goto exit;
	}

	if (BrokerState.ffdc_location)
		strcpy(filename, BrokerState.ffdc_location);
	else if (BrokerState.persistence_location)
		strcpy(filename, BrokerState.persistence_location);
    ptr += strlen(filename);

#if defined(GETTIMEOFDAY)
	gettimeofday(&ts, NULL);
	timeinfo = localtime(&ts.tv_sec);
#else
	ftime(&ts);
	timeinfo = localtime(&ts.time);
#endif

	strftime(ptr, 80, "FFDC.CWNAN.%Y%m%d.%H%M%S.", timeinfo);
#if defined(GETTIMEOFDAY)
	sprintf(&ptr[26], ".%.3ld.dmp", ts.tv_usec / 1000L);
#else
	sprintf(&ptr[26], ".%.3u.dmp", ts.millitm);
#endif

	if ((file = fopen(filename, "wb")) != NULL)
	{
		fprintf(file, "========== Start FFDC ==========\n");
		fprintf(file, "Filename  :- %s\n", filename);
		fprintf(file, "Product   :- %s\n", PRODUCT_NAME);
		fprintf(file, "Version   :- %s\n", 	BrokerState.version);
		fprintf(file, "Build     :- %s\n", 	BrokerState.timestamp);	
		fprintf(file, "Date/Time :- %s", asctime(timeinfo));
		fprintf(file, "Reason    :- %s\n\n", symptoms);
		/* potential additions:
		 * os info (uname, getWindowsVersion)
		 * environment variables
		 */
		fclose(file);
	}

	StackTrace_dumpStack(filename);
	if (strstr(symptoms, "memory leak"))
	{
		if ((file = fopen(filename, "ab")) != NULL)
		{
			Heap_scan(file);
			fclose(file);
		}
		mem_leak = 1;
	}
	else
	{
		Log_dumpTrace(filename);
		Broker_dumpHeap(filename);
	}
	if ((file = fopen(filename, "ab")) != NULL)
	{
		fprintf(file, "========== End FFDC ==========\n");
		fclose(file);
	}
	if (segv_flag || mem_leak)
		printf("First Failure Data Capture (FFDC) information written to file %s\n", filename);

exit:
	return filename;
}
