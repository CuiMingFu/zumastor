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
#include <sys/types.h>
#include <netinet/in.h>

#include "porting.h"
#include "dns.h"
#include "msg.h"
#include "nfs.h"
#include "mount.h"

int
main(int argc, char *argv[])
{
	char *hostname = argv[1], *path = argv[2];
	struct in_addr addr;
	unsigned char fhdata[64];
	struct fh fh = {0, fhdata};
	int i;

	if (argc != 3) {
		fprintf(stderr, "usage: mount-test <hostname> <path>\n");
		return -1;
	}

	if (dns_name2addr(hostname, &addr) != 0) {
		fprintf(stderr, "dns_name2addr(\"%s\", ...) failed\n",
			hostname);
		return -1;
	}

	if (mount_getroot(addr, path, &fh) != 0) {
		fprintf(stderr, "mount_getroot failed\n");
		return -1;
	}
	printf("%s:%s (fhlen=%d) 0x", hostname, path, fh.len);
	for (i=0 ; i<fh.len ; i++) {
		printf("%02x", (unsigned char)fh.data[i]);
	}
	printf("\n");

	printf("mount-test succeeded\n");
	return 0;
}
