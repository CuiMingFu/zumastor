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
#include "metronome.h"

int
metronome_start(struct metronome *mn, int target_ops_per_sec, int lifetime,
		int (*func)(char *), char *func_arg)
{
	if (target_ops_per_sec <= 0) {
		report_error(FATAL, "invalid ops/sec %d", target_ops_per_sec);
		return -1;
	}
	mn->start = global_timer();
	mn->target_ops_per_sec = target_ops_per_sec;
	mn->lifetime = lifetime;
	mn->func = func;
	mn->func_arg = func_arg;
	mn->ops = 0;
	mn->msecs = 0;
	return 0;
}

int
metronome_active(struct metronome *mn)
{
	return (mn->lifetime == 0 || 
		(global_timer() - mn->start) / 1000000 < mn->lifetime);
}

int
metronome_tick(struct metronome *mn)
{
	u_int64_t usecs = global_timer() - mn->start;
	long target_ops;
	int local_ops = 0, r;

	mn->msecs = usecs / (u_int64_t)1000;
	target_ops = ((u_int64_t)mn->target_ops_per_sec * usecs) / 
		(u_int64_t)1000000;

	while (mn->ops < target_ops) {
		if ((r = (*mn->func)(mn->func_arg)) < 0) {
			report_error(FATAL, "metronome func error %d", r);
			return r;
		}
		mn->ops++;
		local_ops++;
	}

	return local_ops;
}
