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
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <assert.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "msg.h"
#include "rpc.h"
#include "nfs.h"
#include "mount.h"
#include "nameset.h"

int
mount_getroot(struct in_addr addr, char *path, struct fh *fh)
{
	u_int32_t status;
	int sock = 0, off, rvalue = -1, zero = 0;
	int len = strlen(path), sport = (getuid() == 0 ? 1 : 0);
	msg_t m = 0;

	assert(len < MNTPATHLEN);

	if ((sock = rpc_client(addr, MOUNTPROG, MOUNTVERS3, SOCK_STREAM, 
			       sport)) < 0) {
		report_error(FATAL, "rpc_client error");
		goto out;
	}
	
	/*
	 * create the mount mnt message.
	 */
	if ((m = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc error");
		goto out;
	}
	off = msg_insert_htonl_int32(m, 0, len);
	off = msg_insert(m, off, len, path);
	if ((len & 3) != 0) {
		off = msg_insert(m, off,  4 - (len & 3), (char *)&zero);
	}
	assert(off == msg_mlen(m));

	/*
	 * send the message and wait for the reply.  use the same msg for
	 * the request and the reply.  this is tcp so don't worry about
	 * packet drops, etc.
	 */
	if (rpc_send(sock, SOCK_STREAM, m, MOUNTPROG, MOUNTVERS3,
		     MOUNTPROC_MNT, getuid(), getgid(), NULL) < 0) {
		report_error(FATAL, "rpc_send error");
		goto out;
	}
	if ((off = rpc_recv(sock, SOCK_STREAM, m, NULL)) < 0) {
		report_error(FATAL, "rpc_recv error"
			     "(may need to run as root for port auth)");
		goto out;
	}

	off = msg_extract_ntohl_int32(m, off, &status);
	if (status == 13) {
		/* special case this common error */
		report_error(FATAL, "mount_getroot: access error");
		goto out;
	}
	if (status != 0) {
		report_error(FATAL, "mount_getroot status error %d", status);
		goto out;
	}
	off = msg_extract_ntohl_int32(m, off, &fh->len);

	assert(fh->len <= FHSIZE3);
	assert(fh->data);
	bcopy(msg_mtod(m) + off, fh->data, fh->len);
	rvalue = 0;

 out:
	if (m) {
		msg_free(m);
	}
	if (sock > 0) {
		close(sock);
	}
	return rvalue;
}

