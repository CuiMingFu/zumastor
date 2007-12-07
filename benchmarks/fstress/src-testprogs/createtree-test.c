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
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/rpc.h>

#include "porting.h"
#include "nfs_constants.h"
#include "dns.h"
#include "nfs.h"
#include "mount.h"
#include "distheap.h"
#include "nameset.h"
#include "operation.h"
#include "distribution.h"
#include "createtree.h"

/* so linger.o will link. XXX */
int (*rsize_dist_func)(void) = rsize_dist;
int (*wsize_dist_func)(void) = wsize_dist;

int
main(int argc, char *argv[])
{
	char *hostname = argv[1], *path = argv[2];
	struct in_addr addr;
	unsigned char fhdata[64];
	struct fh fh = {0, fhdata};

	srandom(time(NULL));

	if (argc != 3) {
		fprintf(stderr, "usage: createtree-test <hostname> <path>\n");
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

	if (nameset_init("ct-", 500, 500, 500) < 0) {
		fprintf(stderr, "nameset_init failed\n");
		return -1;
	}
	if (op_init(addr, SOCK_DGRAM) < 0) {
		fprintf(stderr, "op_init failed\n");
		return -1;
	}

	if (createtree(&fh, 20, 
		       -1/*dm*/, boring_dist/*dc*/, boring_dist/*dw*/, 
		       -1/*fm*/, boring_dist/*fc*/, boring_dist/*fw*/, 
		       -1/*lm*/, boring_dist/*lc*/, boring_dist/*lw*/, 
		       fsize_dist/*fs*/, 1) < 0) {
		fprintf(stderr, "createtree failed\n");
		return -1;
	}

	if (op_uninit() < 0) {
		fprintf(stderr, "op_uninit failed\n");
		return -1;
	}
	if (nameset_uninit() < 0) {
		fprintf(stderr, "nameset_uninit failed\n");
		return -1;
	}

	printf("createtree-test succeeded\n");	
	return 0;
}
