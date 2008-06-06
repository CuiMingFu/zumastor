#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dm-ddsnap.h"   /* for csnap_codes */
#include "event.h"
#include "trace.h"

/* FIXME: this array must exactly match enum csnap_codes in dm-ddsnap.h */
static const char *csnap_names[] = {
	"PROTOCOL_ERROR",
	"IDENTIFY",
	"IDENTIFY_OK",
	"IDENTIFY_ERROR",
	"QUERY_WRITE",
	"ORIGIN_WRITE_OK",
	"ORIGIN_WRITE_ERROR",
	"SNAPSHOT_WRITE_OK",
	"SNAPSHOT_WRITE_ERROR",
	"QUERY_SNAPSHOT_READ",
	"SNAPSHOT_READ_OK",
	"SNAPSHOT_READ_ERROR",
	"SNAPSHOT_READ_ORIGIN_OK",
	"SNAPSHOT_READ_ORIGIN_ERROR",
	"FINISH_SNAPSHOT_READ",
	"GENERIC_ERROR",
	"CREATE_SNAPSHOT", 
	"CREATE_SNAPSHOT_OK", 
	"DELETE_SNAPSHOT", 
	"DELETE_SNAPSHOT_OK",
	"DUMP_TREE",
	"INITIALIZE_SNAPSTORE",
	"NEED_SERVER",
	"CONNECT_SERVER",
	"CONNECT_SERVER_OK",
	"CONNECT_SERVER_ERROR",
	"CONTROL_SOCKET",
	"SERVER_READY",
	"START_SERVER",
	"SHUTDOWN_SERVER",
	"SET_IDENTITY",
	"UPLOAD_LOCK",
	"FINISH_UPLOAD_LOCK",
	"NEED_CLIENTS",
	"UPLOAD_CLIENT_ID",
	"FINISH_UPLOAD_CLIENT_ID",
	"REMOVE_CLIENT_IDS",
	"LIST_SNAPSHOTS",
	"SNAPSHOT_LIST",
	"PRIORITY",
	"PRIORITY_OK", 
	"PRIORITY_ERROR", 
	"USECOUNT",
	"USECOUNT_ERROR",
	"USECOUNT_OK",
	"STREAM_CHANGELIST",
	"STREAM_CHANGELIST_OK", 
	"STREAM_CHANGELIST_ERROR", 
	"SEND_DELTA",
	"SEND_DELTA_PROCEED",
	"SEND_DELTA_DONE", 
	"SEND_DELTA_ERROR",
	"STATUS",
	"STATUS_OK",
	"STATUS_ERROR",
	"REQUEST_SNAPSHOT_STATE",
	"SNAPSHOT_STATE",
	"REQUEST_SNAPSHOT_SECTORS",
	"SNAPSHOT_SECTORS",
};
#define EVENT_FIRST PROTOCOL_ERROR	/* FIXME: must be first packet name */
#define EVENT_NUMNAMES SNAPSHOT_SECTORS+1	/* FIXME: must be last packet name */
#define EVENT_BAD ((enum csnap_codes) 0)
#define EVENT_ANY ((enum csnap_codes) -1)

enum event_action_e {
	EVENT_ACTION_NONE,
	EVENT_ACTION_DELAY,
	EVENT_ACTION_ABORT,
	EVENT_ACTION_CLOSE
};

/* Fault injection options */
/* What should go wrong */
static enum event_action_e event_action = EVENT_ACTION_NONE;
static int event_want_delay_ms = 0;

/* What kind of packet we watch for */
static enum csnap_codes event_want_trigger = EVENT_BAD;

/* How many times we've seen it, and how many times we want to 
 * see it before firing an action 
 */
static int event_count = 0;
static int event_want_count = 1;

/* Convert a packet name into a packet enum */
static enum csnap_codes csnap_atoi(const char *codename)
{
	int i;
	for (i=0; i < EVENT_NUMNAMES; i++)
		if (!strcasecmp(codename, csnap_names[i]))
			return i + EVENT_FIRST;
	return EVENT_BAD;
}

/* See event.h for interface contract of this function */
int event_parse_options(void)
{
	enum event_action_e new_action = EVENT_ACTION_NONE;
	enum csnap_codes new_trigger = EVENT_BAD;
	int new_want_count = 0;
	int new_want_delay = 0;
	const char *action_opt, *trigger_opt, *count_opt;

	action_opt = getenv("DDSNAP_ACTION");
	if (action_opt) {
		char actionname[100];
		actionname[0] = 0;
		sscanf(action_opt, "%s %d", actionname, &new_want_delay);
		if (!strcmp(actionname, "delay_ms")) {
			if (new_want_delay <= 0) {
				warn("invalid delay time in DDSNAP_ACTION");	
				return -1;
			}
			new_action = EVENT_ACTION_DELAY;
		} else if (!strcmp(actionname, "abort")) {
			new_action = EVENT_ACTION_ABORT;
		} else if (!strcmp(actionname, "close")) {
			new_action = EVENT_ACTION_CLOSE;
		} else {
			warn("invalid action name in DDSNAP_ACTION");	
			return -1;
		}
	} else
		action_opt = "";

	trigger_opt = getenv("DDSNAP_TRIGGER");
	if (trigger_opt) {
		if (!strcmp(trigger_opt, "any"))
			new_trigger = EVENT_ANY;
		else
			new_trigger = csnap_atoi(trigger_opt);
		if (new_trigger == EVENT_BAD) {
			warn("invalid action name in DDSNAP_TRIGGER");	
			return -1;
		}
	} else
		trigger_opt = "";

	count_opt = getenv("DDSNAP_COUNT");
	if (count_opt)
		new_want_count = atoi(count_opt);
	if (new_want_count <= 0) {
		warn("invalid count in DDSNAP_COUNT");	
		return -1;
	} else
		count_opt = "";

	event_action = new_action;
	event_want_count = new_want_count;
	event_want_trigger = new_trigger;
	warn("fault injection enabled: action %s, count %s, trigger %s\n", action_opt, count_opt, trigger_opt);

	return 0;
}

static void event_sleep_ms(unsigned long sleep_msecs)
{
	struct timespec sleep_time, left_time;
	sleep_time.tv_sec = sleep_msecs / 1000;
	sleep_time.tv_nsec = (sleep_msecs % 1000) * 1000000;
	while (nanosleep(&sleep_time, &left_time) && (errno == EINTR)) {
		sleep_time.tv_sec = left_time.tv_sec;
		sleep_time.tv_nsec = left_time.tv_nsec;
	}
}

/* See event.h for interface contract of this function */
void event_hook(int fd, enum csnap_codes id)
{
	if (EVENT_ANY != event_want_trigger && id != event_want_trigger)
		return;
	if (++event_count < event_want_count)
		return;
	event_count = 0;

	switch (event_action) {
	case EVENT_ACTION_ABORT:
		abort();
		break;
	case EVENT_ACTION_CLOSE:
		shutdown(fd, SHUT_RDWR);
		break;
	case EVENT_ACTION_DELAY:
		event_sleep_ms(event_want_delay_ms);
		break;
	case EVENT_ACTION_NONE:
		;
	}
}
