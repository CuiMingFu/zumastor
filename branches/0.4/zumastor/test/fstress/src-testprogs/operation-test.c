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
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/rpc.h>

#include "porting.h"
#include "nfs_constants.h"
#include "dns.h"
#include "nfs.h"
#include "nameset.h"
#include "operation.h"
#include "distribution.h"

static int calls = 0, replies = 0;
int (*rsize_dist_func)(void) = rsize_dist;
int (*wsize_dist_func)(void) = wsize_dist;

static void
reply_func(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	if (reply) {
		replies++;
	}
}

static void
call_func(void *arg)
{
	struct nfsmsg *nm;

	if ((nm = op_alloc(NFSPROC_NULL)) == NULL) {
		fprintf(stderr, "op_alloc failed\n");
		return;
	}
	if (op_send(nm, reply_func, NULL, NULL) < 0) {
		fprintf(stderr, "nfs_send error\n");
		return;
	}
	calls++;
}

int
main(int argc, char *argv[])
{
	char *hostname = argv[1];
	struct in_addr addr;
	int rate, duration;
	
	if (argc != 4) {
		fprintf(stderr, "usage: operation-test <hostname> <rate> <duration>\n");
		return -1;
	}
	if (dns_name2addr(hostname, &addr) != 0) {
		fprintf(stderr, "dns_name2addr(\"%s\", ...) failed\n",
			hostname);
		return -1;
	}
	rate = atoi(argv[2]);
	duration = atoi(argv[3]);

	if (op_init(addr, SOCK_STREAM) < 0) {
		fprintf(stderr, "op_init failed\n");
		return -1;
	}
	if (op_metronome(rate, duration, call_func, NULL) < 0) {
		fprintf(stderr, "op_metronome failed\n");
		return -1;
	}
#if 0
	if (op_barrier(0) < 0) {
		fprintf(stderr, "op_barrier failed\n");
		return -1;
	}
#else
	usleep(1000000);
	op_poll(0);
#endif
	if (op_uninit() < 0) {
		fprintf(stderr, "op_uninit failed\n");
		return -1;
	}

	printf("calls=%d replies=%d (expect=%d)\n", 
	       calls, replies, rate * duration);
	printf("operation-test succeeded\n");	
	return 0;
}
