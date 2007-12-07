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
#include <sys/types.h>
#include <netinet/in.h>

#include "porting.h"
#include "dns.h"

int
main(int argc, char *argv[])
{
	char *hostname = argv[1], buf[64];
	struct in_addr addr;

	if (argc != 2) {
		fprintf(stderr, "usage: dns-test <hostname>\n");
		return -1;
	}

	if (dns_name2addr(hostname, &addr) != 0) {
		fprintf(stderr, "dns_name2addr(\"%s\", ...) failed\n",
			hostname);
		return -1;
	}
	printf("dns_name2addr(\"%s\", ...) returned addr 0x%04x\n",
	       hostname, addr.s_addr);

	if (dns_addr2name(addr, buf, sizeof(buf)) != 0) {
		fprintf(stderr, "dns_addr2name(0x%04x, ...) failed\n",
			addr.s_addr);
		return -1;
	}
	printf("dns_addr2name(0x%04x, ...) returned hostname \"%s\"\n",
			addr.s_addr, buf);
	
	printf("dns-test succeeded\n");
	return 0;
}
