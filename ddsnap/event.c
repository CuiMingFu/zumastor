#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "event.h"
#include "trace.h"

static const char *event_names[] = {
       "tx_identify",
       "tx_server_ready",
};

static int event_shoulddelay[EVENT_END] = { 0 };
static int event_want_delay_ms = 0;

static void event_sleep_ms(unsigned long sleep_msecs)
{
	struct timespec sleep_time, left_time;
	sleep_time.tv_sec = sleep_msecs / 1000;
	sleep_time.tv_nsec = 0;
	while (nanosleep(&sleep_time, &left_time) && (errno == EINTR)) {
		sleep_time.tv_sec = left_time.tv_sec;
		sleep_time.tv_nsec = left_time.tv_nsec;
	}
}

int event_parse_options(void)
{
	int i;
	char *debug_string = NULL;
	if ((debug_string = getenv("DDSNAP_DELAY_MSEC"))) {
		if ((event_want_delay_ms = atol(debug_string)) <= 0) {
			warn("invalid delay time");	
			return -1;
		}
		for (i=0; i < EVENT_END; i++) {
			if (strstr(debug_string, event_names[i]))
				event_shoulddelay[i] = 1;
		}
	}
	return 0;
}

void event_hook(enum event_ids event_id) {
	if (event_shoulddelay[event_id])
		event_sleep_ms(event_want_delay_ms);
}
