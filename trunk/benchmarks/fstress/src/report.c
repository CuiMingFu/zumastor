/*
 * Copyright (c) 2001 Duke University -- Darrell Anderson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Duke University
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */ 

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>

#include "porting.h"
#include "report.h"

#define noFATAL_ASSERT

/* ------------------------------------------------------- */
static char *quitfile = NULL;

void
report_set_quitfile(char *fname)
{
	quitfile = fname;
}

void
report_creat_quitfile(void)
{
	if (quitfile) {
		if (open(quitfile, O_CREAT | O_TRUNC, 0644) < 0) {
			report_perror(NONFATAL, "open(\"%s\")", quitfile);
		}
	}
}

/* ------------------------------------------------------- */

static char *
timestr(void)
{
	static char timebuf[64];
	time_t t;

	if (time(&t) != (time_t)-1) {
		strftime(timebuf, sizeof(timebuf), "%T", localtime(&t));
	} else {
		timebuf[0] = '\0';
	}
	return timebuf;
}

static void
report_stderr(const int fatal, const char *str)
{
	char e[300];
	e[0] = '\0';

#define REVERSE_VIDEO_ERRORS
#ifdef REVERSE_VIDEO_ERRORS
	snprintf(e+strlen(e), sizeof(e)-strlen(e), "\x1b[7m"); /* reverse video */
#endif
	snprintf(e+strlen(e), sizeof(e)-strlen(e), "[%s] ", timestr());
	snprintf(e+strlen(e), sizeof(e)-strlen(e), "%s", str);
#ifdef REVERSE_VIDEO_ERRORS
	snprintf(e+strlen(e), sizeof(e)-strlen(e), "\x1b[0m"); /* reset */
#endif

	fflush(stdout);
	fflush(stderr);
	fprintf(stderr, "%s\n", e);
	fflush(stderr);

	if (fatal) {
		fprintf(stderr, "<terminating after fatal error>\n");
		report_creat_quitfile();
#ifdef FATAL_ASSERT
		fflush(stderr);
		assert(0);
#endif
		exit(1);
	}
}

static void
report_stderr_limit(const int fatal, const char *str)
{
	static char laststr[256];
	static int lastcnt = 0;
	char tmpstr[80];

	if (str == NULL) {
		/* flush */
		if (lastcnt) {
			snprintf(tmpstr, sizeof(tmpstr), 
				 "<last messages repeated %d times>",
				 lastcnt);
			report_stderr(NONFATAL, tmpstr);
		}
		lastcnt = 0;
		laststr[0] = '\0';
		return;
	}

	if (strncmp(laststr, str, sizeof(laststr)) == 0) {
		lastcnt++;
		if (!fatal) {
			return;
		}
	} else {
		if (lastcnt) {
			snprintf(tmpstr, sizeof(tmpstr), 
				 "<last messages repeated %d times>",
				 lastcnt);
			report_stderr(NONFATAL, tmpstr);
			lastcnt = 0;
		}
		strncpy(laststr, str, sizeof(laststr));
	}

	report_stderr(fatal, str);
}

/* ------------------------------------------------------- */

void
report_flush(void)
{
	report_stderr_limit(0, NULL);
}

void
report_error(const int fatal, const char *format, ...)
{
	char buf[256];
	va_list ap;
	int len;

	va_start(ap, format);
	len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	report_stderr_limit(fatal, buf);
}

void
report_perror(const int fatal, const char *format, ...)
{
	char buf[256], buf2[256];
	va_list ap;
	int len, cur_errno = errno;

	va_start(ap, format);
	len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	snprintf(buf2, sizeof(buf2), "%s errno %d: %s", buf, cur_errno, 
		 strerror(cur_errno));

	report_stderr_limit(fatal, buf2);
}

/* ------------------------------------------------------- */

#include <sys/resource.h>
#include <unistd.h>

static struct timeval start_tv;

void
report_start(int argc, char *argv[])
{
	char cline[1024], hostname[64];
	int i;

	if (gettimeofday(&start_tv, NULL) < 0) {
		report_perror(FATAL, "gettimeofday");
		return;
	}
	if (gethostname(hostname, sizeof(hostname)) < 0) {
		report_perror(FATAL, "gethostname");
		return;
	}
	cline[0] = '\0';
	for (i=0 ; i<argc ; i++) {
		strncat(cline, argv[i], sizeof(cline) - strlen(cline));
		if (i < argc - 1) {
			strncat(cline, " ", sizeof(cline) - strlen(cline));
		}
	}

	fprintf(stdout, "[%s] START host \"%s\" command line \"%s\"\n",
		timestr(), hostname, cline);
	fflush(stdout);
}

void
report_stop(int print_rusage)
{
	float usertime, systemtime, totaltime;
	struct timeval end_tv;
	struct rusage rusage;

	if (gettimeofday(&end_tv, NULL) < 0) {
		report_perror(FATAL, "gettimeofday");
		return;
	}
	if (getrusage(RUSAGE_SELF, &rusage) == -1) {
		report_perror(FATAL, "getrusage");
		return;
	}

	fprintf(stdout, "[%s] STOP\n", timestr());
	fflush(stdout);

	if (!print_rusage) {
		return;
	}

	totaltime = (float)(end_tv.tv_sec - start_tv.tv_sec) + 
		(float)(end_tv.tv_usec - start_tv.tv_usec)/1000000.0;
	usertime = (float)rusage.ru_utime.tv_sec + 
		((float)rusage.ru_utime.tv_usec)/1000000.0;
	systemtime = (float)rusage.ru_stime.tv_sec + 
		((float)rusage.ru_stime.tv_usec)/1000000.0;

	printf(">>> real(s):                     %9.2f\n", totaltime);
	printf(">>> user(s):                     %9.2f (%0.2f%%)\n", 
	       usertime, 100.0*usertime/totaltime);
	printf(">>> sys(s):                      %9.2f (%0.2f%%)\n", 
	       systemtime, 100.0*systemtime/totaltime);
	printf(">>> \"idle\"(s):                   %9.2f (%0.2f%%)\n", 
	       totaltime-(usertime+systemtime),
	       100.0*(totaltime-(usertime+systemtime))/totaltime);
	printf(">>> maximal resident set size(KB): %7ld\n", rusage.ru_maxrss);
	printf(">>> integral resident set size:    %7ld\n", rusage.ru_idrss);
	printf(">>> page faults not requiring I/O: %7ld\n", rusage.ru_minflt);
	printf(">>> page faults requiring I/O:     %7ld\n", rusage.ru_majflt);
	printf(">>> swaps:                         %7ld\n", rusage.ru_nswap);
	printf(">>> input operations:              %7ld\n", rusage.ru_inblock);
	printf(">>> output operations:             %7ld\n", rusage.ru_oublock);
	printf(">>> msgsnd operations:             %7ld\n", rusage.ru_msgsnd);
	printf(">>> msgrcv operations:             %7ld\n", rusage.ru_msgrcv);
	printf(">>> signals handled:               %7ld\n", rusage.ru_nsignals);
	printf(">>> voluntary context switches:    %7ld\n", rusage.ru_nvcsw);
	printf(">>> involuntary context switches:  %7ld\n", rusage.ru_nivcsw);
}
