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
#include "mount.h"

int
main(int argc, char *argv[])
{
	char *hostname = argv[1], *path = argv[2];
	struct in_addr addr;
	struct nfsmsg nm;
	int sock, socktype = SOCK_STREAM;
	u_int32_t xid = 0;
	unsigned char fhdata[64];

	if (argc != 3) {
		fprintf(stderr, "usage: readdir-test <hostname> <path>\n");
		return -1;
	}

	if (dns_name2addr(hostname, &addr) != 0) {
		fprintf(stderr, "dns_name2addr(\"%s\", ...) failed\n",
			hostname);
		return -1;
	}
	if ((sock = rpc_client(addr, NFS_PROG, NFS_VER3, socktype, 0)) < 0) {
		fprintf(stderr, "rpc_client failed\n");
		return -1;
	}

	/*
	 * construct a readdir for the root directory.
	 */
	nfsmsg_prep(&nm, CALL);
	nm.proc = NFSPROC_READDIR;
	nm.u.readdir_arg.dir.data = fhdata;
	if (mount_getroot(addr, path, &nm.u.readdir_arg.dir) != 0) {
		fprintf(stderr, "mount_getroot failed\n");
		return -1;
	}
	nm.u.readdir_arg.cookie = 0;
	nm.u.readdir_arg.cookieverf = 0;
	nm.u.readdir_arg.count = 8192;
	if (nfs_send(sock, socktype, &nm, &xid) < 0) {
		fprintf(stderr, "nfs_send error\n");
		return -1;
	}
	nfsmsg_rele(&nm);

	/*
	 * get the response.
	 */
	nfsmsg_prep(&nm, REPLY);
	if (nfs_recv(sock, socktype, &nm, &xid) < 0) {
		fprintf(stderr, "nfs_recv error\n");
		return -1;
	} else {
		struct entry *e = nm.u.readdir_res.dirlist.entries;
		while (e) {
			printf("0x%08x-%08x \"%s\"\n", (int)e->fileid, 
			       (int)(e->fileid>>32), e->name.data);
			e = e->nextentry;
		}
	}
	nfsmsg_rele(&nm);

	close(sock);
	printf("readdir-test succeeded\n");
	return 0;
}
