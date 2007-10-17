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
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>

#include "porting.h"
#include "report.h"
#include "timer.h"

#define noUSE_CYCLE_COUNTER
#ifdef USE_CYCLE_COUNTER

static __inline u_int32_t
cc_read(void)
{
	u_int64_t rv = 0;

#ifdef __i386__
	__asm__ __volatile__ (".byte 0x0f, 0x31" : "=A" (rv));
#endif
#ifdef __alpha__
	__asm__ __volatile__ ("rpcc %0" : "=r" (rv));
	rv &= 0xffffffff; /* 64 bit reg, but can only use low 32 bits */
#endif

	return (u_int32_t)rv;
}

static u_int32_t
cc_freq(void)
{
	struct timeval start, end, delta;
	u_int32_t cc_start, cc_end, usecs;
	
	gettimeofday(&start, NULL);
	cc_start = cc_read();
	usleep(250000); /* 250 ms, gettimeofday accounts for scheduling. */
	cc_end = cc_read();
	gettimeofday(&end, NULL);

	delta.tv_sec = end.tv_sec - start.tv_sec;
	delta.tv_usec = end.tv_usec - start.tv_usec;
	if (delta.tv_usec < 0) {
		delta.tv_sec -= 1;
		delta.tv_usec += 1000000;
	}
	usecs = ((u_int64_t)delta.tv_sec * (u_int64_t)1000000) + 
		(u_int64_t)delta.tv_usec;

	return (cc_end - cc_start) / usecs;
}

#endif

static u_int64_t
timer_read(struct timeval *timer)
{
	struct timeval now, delta;

	if (gettimeofday(&now, NULL) < 0) {
		report_perror(FATAL, "gettimeofday");
		return -1;
	}
	delta.tv_sec = now.tv_sec - timer->tv_sec;
	delta.tv_usec = now.tv_usec - timer->tv_usec;
	if (delta.tv_usec < 0) {
		delta.tv_sec -= 1;
		delta.tv_usec += 1000000;
	}
	return (((u_int64_t)delta.tv_sec * (u_int64_t)1000000) + 
		(u_int64_t)delta.tv_usec);
}

static int
timer_start(struct timeval *timer)
{
	if (gettimeofday(timer, NULL) < 0) {
		report_perror(FATAL, "gettimeofday");
		return -1;
	}
	return 0;
}

u_int64_t
global_timer(void)
{
	static int ready = 0;
	static struct timeval tv;
	u_int64_t ret;
	static u_int64_t last_timer;

#ifdef USE_CYCLE_COUNTER
	static u_int32_t last_cc, cycles_per_usec = 0;
	u_int32_t this_cc = cc_read();
	
	if (cycles_per_usec == 0) {
		cycles_per_usec = cc_freq();
	} else {
		if ((this_cc > last_cc) &&
		    (this_cc - last_cc) < (cycles_per_usec * 1000)) {
			return last_timer + 
				((this_cc - last_cc) / cycles_per_usec);
		}
	}
#endif

	if (ready == 0) {
		if (timer_start(&tv) < 0) {
			report_error(FATAL, "timer_start error");
			return 0;
		}
		ready = 1;
	}

	ret = timer_read(&tv);
	if (ret < last_timer) {
		ret = last_timer;
	}
	last_timer = ret;

#ifdef USE_CYCLE_COUNTER
	last_cc = cc_read();
#endif
	return ret;
}
