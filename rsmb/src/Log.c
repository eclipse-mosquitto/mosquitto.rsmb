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
 * Logging module
 */

#include "Log.h"
#include "MQTTPacket.h"
#include "MQTTProtocol.h"
#include "MQTTProtocolClient.h"
#include "Messages.h"
#include "LinkedList.h"
#include "StackTrace.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#if !defined(WIN32)
#ifdef ESP_PLATFORM
#include <esp_log.h>
#else
#include <syslog.h>
#endif
#define GETTIMEOFDAY 1
#else
#define snprintf _snprintf
#define vsnprintf _vsprintf_p
#endif

#if defined(GETTIMEOFDAY)
	#include <sys/time.h>
#else
	#include <sys/timeb.h>
#endif

#include "Heap.h"

#if !defined(min)
#define min(A,B) ( (A) < (B) ? (A):(B))
#endif

trace_settings_type trace_settings =
{
	LOG_INFORMATION,
	100,
	TRACE_MINIMUM,
	400,
	-1,
	0
};

static int Log_publishFlag = 0;			/**< Flag to control log publishing to system topic */
static int Log_recurse_flag = 0;		/**< To prevent infinite recursion */

static List* log_buffer = NULL;			/**< log buffer - list of log entries */

#define MAX_FUNCTION_NAME_LENGTH 100

typedef struct
{
#if defined(GETTIMEOFDAY)
	struct timeval ts;
#else
	struct timeb ts;
#endif
	int sametime_count;
	int number;
	int depth;
	char name[MAX_FUNCTION_NAME_LENGTH];
	int line;
	int has_rc;
	int rc;
	int level;
} traceEntry;

static int start_index = -1,
			next_index = 0;
static traceEntry* trace_queue = NULL;
static int trace_queue_size = 0;

static FILE* trace_destination = NULL;	/**< flag to indicate if trace is to be sent to a stream */
static int trace_output_level = -1;

static int sametime_count = 0;
#if defined(GETTIMEOFDAY)
struct timeval ts, last_ts;
#else
struct timeb ts, last_ts;
#endif


/**
 * Allow the control of publishing log messages to a system topic.
 * The setting lasts until it is next changed.
 * @param flag boolean flag to indicate whether log messages are published or not
 */
void Log_setPublish(int flag)
{
	Log_publishFlag = flag;
}


/**
 * Initialize the log module.
 * @return completion code, success == 0
 */
int Log_initialize()
{
	int rc = -1;

	trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries);
	trace_queue_size = trace_settings.max_trace_entries;

	if ((log_buffer = ListInitialize()) != NULL)
		rc = 0;
	return rc;
}


/**
 * Terminate the log module
 */
void Log_terminate()
{
	ListFree(log_buffer);
	log_buffer = NULL;
	free(trace_queue);
	trace_queue = NULL;
}


/**
 * Publish a message to the system log topic
 * @param topic topic to publish on
 * @param string the message to publish
 */
void Log_Publish(char* topic, char* string)
{
	Publish publish;
	publish.header.byte = 0; /* Qos 0, not retained */
	publish.payload = string;
	publish.payloadlen = strlen(string);
	publish.topic = topic;
	MQTTProtocol_handlePublishes(&publish, 0, NULL);
}


/**
 * Add a message to the trace buffer
 * @param msg the message to add
 */
static void addToBuffer(List* buffer, char* msg)
{
	if (buffer)
	{
		int size = strlen(msg) + 1;
		char *newmsg = malloc(size);
		int max_entries = (buffer == log_buffer) ? trace_settings.max_log_entries : trace_settings.max_trace_entries;

		strcpy(newmsg, msg);
		if (buffer->count < max_entries)
			ListAppend(buffer, newmsg, size);
		else
			ListRemoveHeadAddTail(buffer, newmsg, size);
		while (buffer->count > max_entries)
			ListRemoveHead(buffer);
	}
}


traceEntry* Log_pretrace()
{
	traceEntry *cur_entry = NULL;

	/* calling ftime/gettimeofday seems to be comparatively expensive, so we need to limit its use */
	if (++sametime_count % 20 == 0)
	{
#if defined(GETTIMEOFDAY)
		gettimeofday(&ts, NULL);
		if (ts.tv_sec != last_ts.tv_sec || ts.tv_usec != last_ts.tv_usec)
#else
		ftime(&ts);
		if (ts.time != last_ts.time || ts.millitm != last_ts.millitm)
#endif
		{
			sametime_count = 0;
			last_ts = ts;
		}
	}

	if (trace_queue_size != trace_settings.max_trace_entries)
	{
		traceEntry* new_trace_queue = malloc(sizeof(traceEntry) * trace_settings.max_trace_entries);

		memcpy(new_trace_queue, trace_queue, min(trace_queue_size, trace_settings.max_trace_entries) * sizeof(traceEntry));
		free(trace_queue);
		trace_queue = new_trace_queue;
		trace_queue_size = trace_settings.max_trace_entries;

		if (start_index > trace_settings.max_trace_entries + 1 ||
				next_index > trace_settings.max_trace_entries + 1)
		{
			start_index = -1;
			next_index = 0;
		}
	}

	/* add to trace buffer */
	cur_entry = &trace_queue[next_index];
	if (next_index == start_index) /* means the buffer is full */
	{
		if (++start_index == trace_settings.max_trace_entries)
			start_index = 0;
	} else if (start_index == -1)
		start_index = 0;
	if (++next_index == trace_settings.max_trace_entries)
		next_index = 0;

	return cur_entry;
}


char* Log_formatTraceEntry(traceEntry* cur_entry)
{
	struct tm *timeinfo;
	int buf_pos = 31;
	static char msg_buf[512];

#if defined(GETTIMEOFDAY)
	timeinfo = localtime(&cur_entry->ts.tv_sec);
#else
	timeinfo = localtime(&cur_entry->ts.time);
#endif
	strftime(&msg_buf[7], 80, "%Y%m%d %H%M%S ", timeinfo);
#if defined(GETTIMEOFDAY)
	sprintf(&msg_buf[22], ".%.3lu ", cur_entry->ts.tv_usec / 1000L);
#else
	sprintf(&msg_buf[22], ".%.3hu ", cur_entry->ts.millitm);
#endif
	buf_pos = 27;

	sprintf(msg_buf, "(%.4d)", cur_entry->sametime_count);
	msg_buf[6] = ' ';

	if (cur_entry->has_rc == 2)
		strncpy(&msg_buf[buf_pos], cur_entry->name, sizeof(msg_buf)-buf_pos);
	else
	{
		char* format = Messages_get(cur_entry->number, cur_entry->level);
		if (cur_entry->has_rc == 1)
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line, cur_entry->rc);
		else
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format,
					cur_entry->depth, "", cur_entry->depth, cur_entry->name, cur_entry->line);
	}
	return msg_buf;
}


void Log_posttrace(int log_level, traceEntry* cur_entry)
{
	if (trace_destination &&
		((trace_output_level == -1) ? log_level >= trace_settings.trace_level : log_level >= trace_output_level))
	{
		fprintf(trace_destination, "%s\n", &Log_formatTraceEntry(cur_entry)[7]);
		fflush(trace_destination);
	}
}


void Log_trace(int log_level, char* buf)
{
	traceEntry *cur_entry = NULL;

	cur_entry = Log_pretrace();

	memcpy(&(cur_entry->ts), &ts, sizeof(ts));
	cur_entry->sametime_count = sametime_count;

	cur_entry->has_rc = 2;
	strncpy(cur_entry->name, buf, sizeof(cur_entry->name));

	Log_posttrace(log_level, cur_entry);
}


/**
 * Log a message.  If possible, all messages should be indexed by message number, and
 * the use of the format string should be minimized or negated altogether.  If format is
 * provided, the message number is only used as a message label.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log(int log_level, int msgno, char* format, ...)
{
	int islogmsg = 1;
	char* temp = NULL;
	static char msg_buf[256];

	if (log_level < LOG_CONFIG)
		islogmsg = 0;

#if defined(HIGH_PERFORMANCE)
	if (!islogmsg)
#else
	if (!islogmsg && log_level < trace_settings.trace_level)
#endif
		return;

	if (islogmsg &&	log_level < trace_settings.log_level)
		return;

	if (format == NULL && (temp = Messages_get(msgno, log_level)) != NULL)
		format = temp;

	if (!islogmsg)
	{
		static char trace_msg_buf[256];
		va_list args;

		va_start(args, format);
		vsnprintf(trace_msg_buf, sizeof(trace_msg_buf), format, args);
		Log_trace(log_level, trace_msg_buf);
		va_end(args);
		return;
	}

	if (Log_recurse_flag == 0)
	{
		struct tm *timeinfo;
		char level_char = ' ';
		int buf_pos = 31;
		va_list args;

#if defined(GETTIMEOFDAY)
		gettimeofday(&ts, NULL);
		timeinfo = localtime(&ts.tv_sec);
#else
		ftime(&ts);
		timeinfo = localtime(&ts.time);
#endif
		strftime(&msg_buf[7], 80, "%Y%m%d %H%M%S ", timeinfo);
#if defined(GETTIMEOFDAY)
		sprintf(&msg_buf[22], ".%.3lu ", ts.tv_usec / 1000L);
#else
		sprintf(&msg_buf[22], ".%.3hu ", ts.millitm);
#endif
		buf_pos = 27;

#if defined(GETTIMEOFDAY)
		if (ts.tv_sec == last_ts.tv_sec && ts.tv_usec == last_ts.tv_usec)
#else
		if (ts.time == last_ts.time && ts.millitm == last_ts.millitm)
#endif
			++sametime_count; /* this message has the same timestamp as the last, so increase the sequence no */
		else
		{
			sametime_count = 0;
			last_ts = ts;
		}
		sprintf(msg_buf, "(%.4d)", sametime_count);
		msg_buf[6] = ' ';

		level_char = "     CDIAWESF"[log_level];
		sprintf(&msg_buf[buf_pos], "%s%.4d%c ", MSG_PREFIX, msgno, level_char);
		buf_pos += 11;

		va_start(args, format);
		vsnprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, format, args);
		va_end(args);

		if (log_level >= LOG_ERROR)
		{
			char* filename = NULL;
			Log_recurse_flag = 1;
			filename = Broker_recordFFDC(&msg_buf[7]);
			Log_recurse_flag = 0;
			snprintf(&msg_buf[buf_pos], sizeof(msg_buf)-buf_pos, Messages_get(13, LOG_WARN), filename);
		}

		addToBuffer(log_buffer, msg_buf);
#if !defined(WIN32)
		if (trace_settings.isdaemon)
		{
#ifdef ESP_PLATFORM
			static char priorities[] = { 5, 5, 5, 5, 4, 4, 3, 3, 2, 2, 1, 0};
			esp_log_write(priorities[log_level], "mqtt", "%s", &msg_buf[22]);
#else
			static char priorities[] = { 7, 7, 7, 7, 6, 6, 5, 5, 4, 3, 1, 0};
			syslog(priorities[log_level], "%s", &msg_buf[22]);
#endif
		}
		else
		{
#endif
			printf("%s\n", &msg_buf[7]);
			fflush(stdout);
#if !defined(WIN32)
		}
#endif
		if (Log_publishFlag)
		{
			#define MAX_LOG_TOPIC_NAME_LEN 25
			static char topic_buf[MAX_LOG_TOPIC_NAME_LEN];
			sprintf(topic_buf, "$SYS/broker/log/%c/%.4d", level_char, msgno);
			Log_recurse_flag = 1;
			Log_Publish(topic_buf, &msg_buf[7]);
			Log_recurse_flag = 0;
		}
	}

	if (log_level == LOG_FATAL)
		exit(-1);
}


/**
 * The reason for this function is to make trace logging as fast as possible so that the
 * function exit/entry history can be captured by default without unduly impacting
 * performance.  Therefore it must do as little as possible.
 * @param log_level the log level of the message
 * @param msgno the id of the message to use if the format string is NULL
 * @param aFormat the printf format string to be used if the message id does not exist
 * @param ... the printf inserts
 */
void Log_stackTrace(int log_level, int msgno, int current_depth, const char* name, int line, int* rc)
{
	traceEntry *cur_entry = NULL;

	if (log_level < trace_settings.trace_level)
		return;

	cur_entry = Log_pretrace();

	memcpy(&(cur_entry->ts), &ts, sizeof(ts));
	cur_entry->sametime_count = sametime_count;
	cur_entry->number = msgno;
	cur_entry->depth = current_depth;
	strcpy(cur_entry->name, name);
	cur_entry->level = log_level;
	cur_entry->line = line;
	if (rc == NULL)
		cur_entry->has_rc = 0;
	else
	{
		cur_entry->has_rc = 1;
		cur_entry->rc = *rc;
	}

	Log_posttrace(log_level, cur_entry);
}


FILE* Log_destToFile(char* dest)
{
	FILE* file = NULL;

	if (strcmp(dest, "stdout") == 0)
		file = stdout;
	else if (strcmp(dest, "stderr") == 0)
		file = stderr;
	else
	{
		if (strstr(dest, "FFDC"))
			file = fopen(dest, "ab");
		else
			file = fopen(dest, "wb");
	}
	return file;
}


int Log_compareEntries(char* entry1, char* entry2)
{
	int comp = strncmp(&entry1[7], &entry2[7], 19);

	/* if timestamps are equal, use the sequence numbers */
	if (comp == 0)
		comp = strncmp(&entry1[1], &entry2[1], 4);

	return comp;
}

/**
 * Write the contents of the stored trace to a stream
 * @param dest string which contains a file name or the special strings stdout or stderr
 */
int Log_dumpTrace(char* dest)
{
	FILE* file = NULL;
	ListElement* cur_log_entry = NULL;
	const int msgstart = 7;
	int rc = -1;
	int trace_queue_index = 0;
	char* msg_buf = NULL;

	if ((file = Log_destToFile(dest)) == NULL)
	{
		Log(LOG_ERROR, 9, NULL, "trace", dest, "trace entries");
		goto exit;
	}

	fprintf(file, "=========== Start of trace dump ==========\n");
	/* Interleave the log and trace entries together appropriately */
	ListNextElement(log_buffer, &cur_log_entry);
	trace_queue_index = start_index;
	if (trace_queue_index == -1)
		trace_queue_index = next_index;
	else
	{
		msg_buf = Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
		if (trace_queue_index == trace_settings.max_trace_entries)
			trace_queue_index = 0;
	}
	
	while (cur_log_entry || trace_queue_index != next_index)
	{
		ListElement* cur_entry = cur_log_entry;
		
		/*printf("cur_trace_entry %p, cur_log_entry %p, trace_queue_index %d, next_index %d\n",
			cur_trace_entry, cur_log_entry, trace_queue_index, next_index);*/
	
		if (cur_entry && trace_queue_index != -1)
		{	/* compare these timestamps */
			if (Log_compareEntries((char*)cur_entry->content, msg_buf) > 0)
				cur_entry = NULL;
		}

		if (cur_entry)
		{
			fprintf(file, "%s\n", &((char*)(cur_entry->content))[msgstart]);
			ListNextElement(log_buffer, &cur_log_entry);
		}
		else
		{
			fprintf(file, "%s\n", &msg_buf[7]);
			if (trace_queue_index != next_index)
			{
				msg_buf = Log_formatTraceEntry(&trace_queue[trace_queue_index++]);
				if (trace_queue_index == trace_settings.max_trace_entries)
					trace_queue_index = 0;
			}
			else
				msg_buf = NULL;
		}
	}
	if (msg_buf)		/* a preprocessed trace message buffer may be left over */
		fprintf(file, "%s\n", &msg_buf[7]);
	fprintf(file, "========== End of trace dump ==========\n\n");
	if (file != stdout && file != stderr && file != NULL)
		fclose(file);
	rc = 0;
exit:
	return rc;
}


/**
 * Start or stop streaming trace output to a stdout, stderr or a file
 * @param dest string which contains a file name or the special strings stdout, stderr, protocol or off
 */
int Log_traceOutput(char* dest)
{
	int rc = 0;

	FUNC_ENTRY;
	trace_output_level = -1;
	if (trace_destination != stdout && trace_destination != stderr && trace_destination != NULL)
		fclose(trace_destination);

	if (dest == NULL || strcmp(dest, "off") == 0)
		trace_destination = NULL;
	else if (strcmp(dest, "stdout") == 0)
		trace_destination = stdout;
	else if (strcmp(dest, "stderr") == 0)
		trace_destination = stderr;
	else if (strcmp(dest, "protocol") == 0)
	{
		trace_destination = stdout;
		trace_output_level = TRACE_PROTOCOL;
	}
	else
	{
		trace_destination = fopen(dest, "w");
		if (trace_destination == NULL)
		{
			Log(LOG_ERROR, 9, NULL, "trace", dest, "trace entries");
			rc = -1;
		}
	}
	FUNC_EXIT_RC(rc);
	return rc;
}
