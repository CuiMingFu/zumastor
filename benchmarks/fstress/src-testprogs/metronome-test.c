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
#include "msg.h"
#include "rpc.h"
#include "nfs.h"
#include "metronome.h"

static int sock;

static int
metronome_func(char *arg)
{
	struct nfsmsg nm;
	u_int32_t xid = 0;

	nfsmsg_prep(&nm, CALL);
	nm.proc = NFSPROC_NULL;
	if (nfs_send(sock, SOCK_DGRAM, &nm, &xid) < 0) {
		fprintf(stderr, "nfs_send error\n");
		return -1;
	}
	nfsmsg_rele(&nm);
	return 0;
}

int
main(int argc, char *argv[])
{
	char *hostname = argv[1];
	struct metronome mn;
	int rate, duration, count, calls = 0, replies = 0, ticks = 0;
	struct in_addr addr;
	struct nfsmsg nm;
	fd_set rset, eset;
	struct timeval timeout;
	u_int32_t xid;

	if (argc != 4) {
		fprintf(stderr, "usage: metronome-test <hostname> <rate> <duration>\n");
		return -1;
	}
	if (dns_name2addr(hostname, &addr) != 0) {
		fprintf(stderr, "dns_name2addr(\"%s\", ...) failed\n",
			hostname);
		return -1;
	}
	rate = atoi(argv[2]);
	duration = atoi(argv[3]);

	if ((sock = rpc_client(addr, NFS_PROG, NFS_VER3, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "rpc_client failed\n");
		return -1;
	}

	metronome_start(&mn, rate, duration, metronome_func, NULL);

	while (metronome_active(&mn)) {
		FD_ZERO(&rset);
		FD_ZERO(&eset);
		FD_SET(sock, &rset);
		FD_SET(sock, &eset);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		if (select(sock+1, &rset, NULL, &eset, &timeout) < 0) {
			perror("select");
			return -1;
		}
		if (FD_ISSET(sock, &eset)) {
			fprintf(stderr, "select exception\n");
			return -1;
		}
		if (FD_ISSET(sock, &rset)) {
			nfsmsg_prep(&nm, REPLY);
			if (nfs_recv(sock, SOCK_DGRAM, &nm, &xid) < 0) {
				fprintf(stderr, "nfs_recv error\n");
				return -1;
			}
			nfsmsg_rele(&nm);
			replies++;
		}
		if ((count = metronome_tick(&mn)) < 0) {
			fprintf(stderr, "metronome_tick error\n");
			return -1;
		}
		calls += count;
		ticks++;
	}

	printf("%d calls, %d replies\n", calls, replies);
	printf("%0.2f calls/tick\n", (float)calls / (float)ticks);
	printf("%ld ops in %0.02f seconds = %0.2f ops/sec\n",
	       mn.ops, (float)mn.msecs / 1000.0,
	       (float)mn.ops / ((float)mn.msecs / 1000.0));
	printf("metronome-test succeeded\n");	
	return 0;
}
