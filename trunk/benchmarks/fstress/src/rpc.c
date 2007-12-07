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
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <errno.h>

#include "porting.h"
#include "report.h"
#include "msg.h"
#include "rpc.h"

/* ------------------------------------------------------- */

static int
my_recvfrom(int s, int socktype, char *buf, size_t len, int flags, 
	    struct sockaddr *from, socklen_t *fromlen)
{
	int ret;

 again:
	if ((ret = recvfrom(s, buf, len, flags, from, fromlen)) < 0) {
		if (errno == EAGAIN) {
			report_perror(NONFATAL, "recvfrom");
			goto again;
		}
	} else if (socktype == SOCK_STREAM && ret < len) {
		report_error(NONFATAL, "recvfrom wanted %d bytes, got %d\n", 
			     len, ret);
	}
	return ret;
}

/* ------------------------------------------------------- */

/*
 * [rpc_insert_callhdr]
 *
 * add an rpc header to a message.
 *
 * PARAMETERS:
 * -> msg            message.
 * -> off            offset where rpc header will begin (probably 0).
 * -> xid            rpc "transaction" id.
 * -> prog           rpc service program number.
 * -> vers           rpc service version number.
 * -> proc           rpc service procedure number.
 * -> uid            uid for rpc authentication credential.
 * -> gid            gid for rpc authentication credential.
 *
 * RETURNS:
 *    int            offset where header ends, or -1 on error.
 */ 
static int
rpc_insert_callhdr(msg_t msg, int off, u_int32_t xid, u_int32_t prog,
		   u_int32_t vers, u_int32_t proc, u_int32_t uid, 
		   u_int32_t gid)
{
	/*
	 * all rpc messages start with xid and direction.
	 */
	off = msg_insert_htonl_int32(msg, off, xid);
	off = msg_insert_htonl_int32(msg, off, CALL);
	
	/*
	 * calls then have rpc version, and service prog, vers, and proc.
	 */
	off = msg_insert_htonl_int32(msg, off, RPC_MSG_VERSION);
	off = msg_insert_htonl_int32(msg, off, prog);
	off = msg_insert_htonl_int32(msg, off, vers);
	off = msg_insert_htonl_int32(msg, off, proc);
	
	/*
	 * next comes the call authentication credential.
	 */
	off = msg_insert_htonl_int32(msg, off, AUTH_UNIX);
	off = msg_insert_htonl_int32(msg, off, 5 * sizeof(u_int32_t));
	off = msg_insert_htonl_int32(msg, off, 0); /* ua_time? */
	off = msg_insert_htonl_int32(msg, off, 0); /* hostnamelen */
	off = msg_insert_htonl_int32(msg, off, uid);
	off = msg_insert_htonl_int32(msg, off, gid);
	off = msg_insert_htonl_int32(msg, off, 0); /* no gidlist */
		
	/*
	 * and finally the protocol specific credential (we don't use one).
	 */
	off = msg_insert_htonl_int32(msg, off, AUTH_NONE);
	off = msg_insert_htonl_int32(msg, off, 0);
	
	return off;
}

/*
 * [rpc_extract_callhdr]
 *
 * extract an rpc header from a message.
 *
 * PARAMETERS:
 * -> msg            message.
 * -> off            offset where rpc header will begin (probably 0).
 * <- xid            rpc "transaction" id.
 * <- prog           rpc service program number.
 * <- vers           rpc service version number.
 * <- proc           rpc service procedure number.
 * <- uid            uid for rpc authentication credential.
 * <- gid            gid for rpc authentication credential.
 *
 * RETURNS:
 *    int            offset where header ends, or -1 on error.
 */ 
static int
rpc_extract_callhdr(msg_t msg, int off, u_int32_t *xid,
		    u_int32_t *prog, u_int32_t *vers, u_int32_t *proc, 
		    u_int32_t *uid, u_int32_t *gid)
{
	u_int32_t word;

	/*
	 * all rpc messages start with xid and direction.
	 */
	off = msg_extract_ntohl_int32(msg, off, xid);
	off = msg_extract_ntohl_int32(msg, off, &word);
	assert(off == -1 || word == CALL);

	/*
	 * calls then have rpc version, and service prog, vers, and proc.
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	off = msg_extract_ntohl_int32(msg, off, prog);
	off = msg_extract_ntohl_int32(msg, off, vers);
	off = msg_extract_ntohl_int32(msg, off, proc);
	
	/*
	 * next comes the call authentication credential.
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	off = msg_extract_ntohl_int32(msg, off, &word);
	off += word;
		
	/*
	 * and finally the protocol specific credential (we don't use one).
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	off = msg_extract_ntohl_int32(msg, off, &word);
	off += word;

	return off;
}

/*
 * [rpc_insert_replyhdr]
 *
 * insert an rpc header into a message.
 *
 * PARAMETERS:
 * -> msg            message.
 * -> off            offset where rpc header will begin (probably 0).
 * -> xid            rpc "transaction" id.
 *
 * RETURNS:
 *    int            offset where header ends, or -1 on error.
 */
static int
rpc_insert_replyhdr(msg_t msg, int off, u_int32_t xid)
{
	/*
	 * all rpc messages start with xid and direction.
	 */
	off = msg_insert_htonl_int32(msg, off, xid);
	off = msg_insert_htonl_int32(msg, off, REPLY);
	
	/*
	 * replies then have an authentication error status word.
	 */
	off = msg_insert_htonl_int32(msg, off, 0);
	
	/*
	 * next comes the server authentication credential. (NULL)
	 */
	off = msg_insert_htonl_int32(msg, off, 0);
	off = msg_insert_htonl_int32(msg, off, 0);

	/*
	 * finally we get the rpc status word.
	 */
	off = msg_insert_htonl_int32(msg, off, 0);

	return off;
}

/*
 * [rpc_extract_replyhdr]
 *
 * parse an rpc header from a message.
 *
 * PARAMETERS:
 * -> msg            message.
 * -> off            offset where rpc header will begin (probably 0).
 * <- xid            rpc "transaction" id.
 *
 * RETURNS:
 *    int            offset where header ends, or -1 on error.
 */
static int
rpc_extract_replyhdr(msg_t msg, int off, u_int32_t *xid)
{
	u_int32_t word;

	/*
	 * all rpc messages start with xid and direction.
	 */
	off = msg_extract_ntohl_int32(msg, off, xid);
	off = msg_extract_ntohl_int32(msg, off, &word);
	assert(off == -1 || word == REPLY);

	/*
	 * replies then have an authentication error status word.
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	if (off != -1 && word != 0) {
		assert (word == 1/*MSG_DENIED*/);
		off = msg_extract_ntohl_int32(msg, off, &word);
		switch (word) {
		case 0:
			report_error(FATAL, 
				     "rpchdr_parse_reply rpc mismatch");
			break;
		case 1:
			off = msg_extract_ntohl_int32(msg, off, &word);
			report_error(FATAL, "rpchdr_parse_reply auth error %d", word);
			break;
		default:
			assert(0); /* can't happen */
		}
		return -1;
	}
	
	/*
	 * next comes the server authentication credential, skip it.
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	off = msg_extract_ntohl_int32(msg, off, &word);
	off += word; /* auth len, if one is supplied */

	/*
	 * finally we get the rpc status word.
	 */
	off = msg_extract_ntohl_int32(msg, off, &word);
	if (off != -1 && word != 0) {
		report_error(FATAL, "rpchdr_parse_reply status error %d", word);
		return -1;
	}

	return off;
}

/* ------------------------------------------------------- */

/*
 * [rpc_send]
 *
 * send an rpc message (without a header!)
 *
 * PARAMETERS:
 * -> sock           socket file descriptor.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 * -> m              message.
 * -> prog           rpc service program number.
 * -> vers           rpc service version number.
 * -> proc           rpc service procedure number.
 * -> uid            uid for rpc authentication credential.
 * -> gid            gid for rpc authentication credential.
 * <- xidp           xid pointer, filled in with request xid.
 *
 * RETURNS:
 *    int            0 on success, or -1 on error.
 */ 
int
rpc_send(int sock, int socktype, msg_t m, int prog, int vers, int proc,
	 int uid, int gid, u_int32_t *xidp)
{
	static u_int32_t xid_memory = (u_int32_t)-1;
	u_int32_t len, record_mark, xid;
	int iovcnt = 0, rvalue = -1;
	struct iovec iov[3];
	msg_t mhdr = 0;

	/*
	 * use the supplied xid, or pick the next from a sequence.
	 */
	if (xidp != NULL && *xidp != 0) {
		xid = *xidp;
	} else {
		while (++xid_memory == 0) {
			xid_memory = random();
		}
		xid = xid_memory;
		if (xidp) {
			*xidp = xid;
		}
	}

	if ((mhdr = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc error");
		goto out;
	}
	if (rpc_insert_callhdr(mhdr, 0, xid, prog, vers, proc, uid, gid) < 0) {
		report_error(FATAL, "rpc_insert_callhdr error");
		goto out;
	}

	if (socktype == SOCK_STREAM) {
		/*
		 * rpc over tcp uses a four-byte record mark between
		 * messages encoding message length.
		 */
		len = msg_mlen(mhdr) + msg_mlen(m);
		record_mark = htonl(len | 0x80000000);
		iov[iovcnt].iov_base = (char *)&record_mark;
		iov[iovcnt].iov_len = sizeof(u_int32_t);
		iovcnt++;
	}
	
	iov[iovcnt].iov_base = msg_mtod(mhdr);
	iov[iovcnt].iov_len = msg_mlen(mhdr);
	iovcnt++;

	iov[iovcnt].iov_base = msg_mtod(m);
	iov[iovcnt].iov_len = msg_mlen(m);
	iovcnt++;

	while (writev(sock, iov, iovcnt) < 0) {
		/*
		 * tolerate ENOBUFS errors, just retry.
		 */
		if (errno == ENOBUFS) {
			report_perror(NONFATAL, "writev");
			continue;
		}
		if (errno == EAGAIN) {
			/*
			 * non-blocking I/O and can't finish the write.
			 * drop the request.
			 */
			report_error(NONFATAL, "non-blocking socket would block, dropping request");
			rvalue = 0;
			goto out;
		}
#if 1
		{
			int total_len = 0, i;
			
			for (i=0 ; i<iovcnt ; i++) {
				total_len += iov[i].iov_len;
			}
			printf("writev errno %d len %d\n", errno, total_len);
		}
#endif
		report_perror(FATAL, "writev");
		goto out;
	}
	rvalue = 0;
 out:
	if (mhdr) {
		msg_free(mhdr);
	}
	return rvalue;
}

/*
 * [rpc_sendreply]
 *
 * send an rpc reply message (without a header!)
 *
 * PARAMETERS:
 * -> sock           socket file descriptor.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 * -> m              message.
 * -> xid            xid pointer, filled in with request xid.
 *
 * RETURNS:
 *    int            0 on success, or -1 on error.
 */ 
int
rpc_sendreply(int sock, int socktype, msg_t m, u_int32_t xid)
{
	u_int32_t len, record_mark;
	int iovcnt = 0, rvalue = -1;
	struct iovec iov[3];
	msg_t mhdr = 0;

	if ((mhdr = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc error");
		goto out;
	}
	if (rpc_insert_replyhdr(mhdr, 0, xid) < 0) {
		report_error(FATAL, "rpc_insert_replyhdr error");
		goto out;
	}

	if (socktype == SOCK_STREAM) {
		/*
		 * rpc over tcp uses a four-byte record mark between
		 * messages encoding message length.
		 */
		len = msg_mlen(mhdr) + msg_mlen(m);
		record_mark = htonl(len | 0x80000000);
		iov[iovcnt].iov_base = (char *)&record_mark;
		iov[iovcnt].iov_len = sizeof(u_int32_t);
		iovcnt++;
	}
	
	iov[iovcnt].iov_base = msg_mtod(mhdr);
	iov[iovcnt].iov_len = msg_mlen(mhdr);
	iovcnt++;

	iov[iovcnt].iov_base = msg_mtod(m);
	iov[iovcnt].iov_len = msg_mlen(m);
	iovcnt++;

	while (writev(sock, iov, iovcnt) < 0) {
		/*
		 * tolerate ENOBUFS errors, just retry.
		 */
		if (errno == ENOBUFS) {
			report_perror(NONFATAL, "writev");
			continue;
		}
		report_perror(FATAL, "writev");
		goto out;
	}
	rvalue = 0;
 out:
	if (mhdr) {
		msg_free(mhdr);
	}
	return rvalue;
}

/*
 * [rpc_recv]
 *
 * receive an rpc message, and parse off the rpc header.
 *
 * PARAMETERS:
 * -> sock           socket file descriptor.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 * <- m              message, filled in with rpc message (with rpc header).
 * <- xidp           xid pointer, filled in with request xid.
 *
 * RETURNS:
 *    int            offset where rpc header ends, or -1 on error.
 */
int
rpc_recv(int sock, int socktype, msg_t m, u_int32_t *xidp)
{
	int off, len = IP_MAXPACKET; /* max udp datagram size */
	u_int32_t record_mark, xid;
	int recvflag = (socktype == SOCK_STREAM) ? MSG_WAITALL : 0;

	if (socktype == SOCK_STREAM) {
		/*
		 * rpc over tcp uses a four-byte record mark between
		 * messages encoding message length.
		 */
		if (my_recvfrom(sock, socktype, (caddr_t)&record_mark, 
				sizeof(u_int32_t), recvflag, NULL, NULL) < 0) {
			report_perror(FATAL, "recvfrom");
			return -1;
		}
		record_mark = ntohl(record_mark);
		assert(record_mark & 0x80000000);
		len = record_mark & ~0x80000000;
	}
#ifdef SHORT_RECV_ENABLE
	else {
		/* don't bother reading the whole message */
		len = 256;
	}
#endif
	if (msg_resize(m, len) < 0) { /* exact for stream, extra for dgram */
		report_error(FATAL, "msg_resize error");
		return -1;
	}
	if ((len = my_recvfrom(sock, socktype, msg_mtod(m), len, recvflag, 
			       NULL, NULL)) < 0) {
		report_perror(FATAL, "recvfrom");
		return -1;
	}
	if (socktype != SOCK_STREAM) {
		if (len < 1024) {
			/*
			 * if we overestimated (due to lack of record mark
			 * size) and the packet is small, exchange our
			 * large message for a smaller one.  this will copy
			 * the data from one message buffer to another.
			 */
			if (msg_resize(m, len) < 0) {
				report_error(FATAL, "msg_resize error");
				return -1;
			}
		} else {
			/*
			 * just correct the length, leaving a large
			 * buffer attached.
			 */
			if (msg_resize_nocopy(m, len) < 0) {
				report_error(FATAL, "msg_resize error");
				return -1;
			}
		}			
	}
	assert(msg_mlen(m) == len);

	if ((off = rpc_extract_replyhdr(m, 0, &xid)) < 0) {
		report_error(FATAL, "rpc_extract_replyhdr error");
		return -1;
	}
	if (xidp) {
		*xidp = xid;
	}

	return off;
}

/*
 * [rpc_recvcall]
 *
 * receive an rpc message, and parse off the rpc header.
 *
 * PARAMETERS:
 * -> sock           socket file descriptor.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 * <- m              message, filled in with rpc message (with rpc header).
 * <- xidp           xid pointer, filled in with request xid.
 * <- procp          proc number pointer, filled in with request xid.
 *
 * RETURNS:
 *    int            offset where rpc header ends, or -1 on error.
 */
int
rpc_recvcall(int sock, int socktype, msg_t m, u_int32_t *xidp,
	     u_int32_t *procp)
{
	int off, len = IP_MAXPACKET; /* max udp datagram size */
	u_int32_t record_mark, xid, proc, ignore;
	int recvflag = (socktype == SOCK_STREAM) ? MSG_WAITALL : 0;

	if (socktype == SOCK_STREAM) {
		/*
		 * rpc over tcp uses a four-byte record mark between
		 * messages encoding message length.
		 */
		if (my_recvfrom(sock, socktype, (caddr_t)&record_mark, 
				sizeof(u_int32_t), recvflag, NULL, NULL) < 0) {
			report_perror(FATAL, "recvfrom");
			return -1;
		}
		record_mark = ntohl(record_mark);
		assert(record_mark & 0x80000000);
		len = record_mark & ~0x80000000;
	}
#ifdef SHORT_RECV_ENABLE
	else {
		/* don't bother reading the whole message */
		len = 256;
	}
#endif
	if (msg_resize(m, len) < 0) { /* exact for stream, extra for dgram */
		report_error(FATAL, "msg_resize error");
		return -1;
	}
	if ((len = my_recvfrom(sock, socktype, msg_mtod(m), len, recvflag, 
			       NULL, NULL)) < 0) {
		report_perror(FATAL, "recvfrom");
		return -1;
	}
	if (socktype != SOCK_STREAM) {
		if (len < 1024) {
			/*
			 * if we overestimated (due to lack of record mark
			 * size) and the packet is small, exchange our
			 * large message for a smaller one.  this will copy
			 * the data from one message buffer to another.
			 */
			if (msg_resize(m, len) < 0) {
				report_error(FATAL, "msg_resize error");
				return -1;
			}
		} else {
			/*
			 * just correct the length, leaving a large
			 * buffer attached.
			 */
			if (msg_resize_nocopy(m, len) < 0) {
				report_error(FATAL, "msg_resize error");
				return -1;
			}
		}			
	}
	assert(msg_mlen(m) == len);

	if ((off = rpc_extract_callhdr(m, 0, &xid, &ignore, &ignore, &proc, 
				       &ignore, &ignore)) < 0) {
		report_error(FATAL, "rpc_extract_callhdr error");
		return -1;
	}
	if (xidp) {
		*xidp = xid;
	}
	if (procp) {
		*procp = proc;
	}

	return off;
}

/* ------------------------------------------------------- */

#ifdef IPPROTO_TUF
int tuf_client = 0;
#endif

/*
 * [socket_client]
 *
 * establish a socket connection.
 *
 * PARAMETERS:
 * -> addr           remote ip address, in network byte order.
 * -> sport          local port, in host byte order (or 0 for any, 1 for root).
 * -> dport          remote port, in host byte order.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 *
 * RETURNS:
 *    int            socket file descriptor, or -1 on error.
 */
int
socket_client(struct in_addr addr, short sport, short dport, int socktype)
{
	struct sockaddr_in src, dst;
	int sock = 0, sockoptarg, sockoptlen, sockproto = 0;
	struct timeval sndtimeo;

#ifdef IPPROTO_TUF
	if (tuf_client == 1 && dport == 2049) {
		printf("socket client using IPPROTO_TUF for dport %d\n", dport);
		sockproto = IPPROTO_TUF;
	}
#endif

	if ((sock = socket(AF_INET, socktype, sockproto)) < 0) {
		report_perror(FATAL, "socket");
		goto err;
	}

#if 1
#ifdef __FreeBSD__
        sockoptlen = sizeof(sockoptarg);
        if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &sockoptarg, 
                       &sockoptlen) < 0) {
                report_perror(FATAL, "getsockopt");
        }
        while (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &sockoptarg, 
                          sizeof(sockoptarg)) == 0) {
                sockoptarg *= 1.25;
        }
        sockoptlen = sizeof(sockoptarg);
        if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sockoptarg, 
                       &sockoptlen) < 0) {
                report_perror(FATAL, "getsockopt");
        }
        while (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sockoptarg, 
                          sizeof(sockoptarg)) == 0) {
                sockoptarg *= 1.25;
        }
#endif
#else
#warning "disabling large socket window sizes"
#endif

#if 0
	/*
	 * watch out for deadlock: cancel a send that blocks for too long.
	 */
	sndtimeo.tv_usec = 20000; /* 20 ms? 0? */
	sndtimeo.tv_sec = 0;
        if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sndtimeo, 
		       sizeof(sndtimeo)) < 0) {
		report_perror(FATAL, "setsockopt SO_SNDTIMEO");
		goto err;
        }
#endif

	if (sport == 1) {
		/*
		 * pick any root port.
		 */
#ifdef __FreeBSD__
		/*
		 * this setsockopt seems to be FreeBSD specific
		 */
		sockoptarg = IP_PORTRANGE_LOW;
		if (setsockopt(sock, IPPROTO_IP, IP_PORTRANGE, 
			       &sockoptarg, sizeof(sockoptarg)) < 0) {
			report_perror(FATAL, "setsockopt IP_PORTRANGE");
			goto err;
		}
#else
		/*
		 * is there a portable way to ask for a privileged port?
		 * if the effective UID is root, then we should get one
		 * anyhow.  don't do anything.
		 */
		/*
		 * apparently this isn't true.  look for a free
		 * low-numbered port.
		 */
		for (sport = 700 ; sport < 1024 ; sport++) {
			bzero(&src, sizeof(src));
			src.sin_family = AF_INET;
			src.sin_addr.s_addr = INADDR_ANY;
			src.sin_port = htons(sport);
			if (bind(sock, (struct sockaddr *)&src, 
				 sizeof(src)) == 0) {
				break;
			}
		}
		if (sport == 1024) {
			report_error(FATAL, "cannot find free privileged port");
			goto err;
		}
#endif
	} else {
		bzero(&src, sizeof(src));
		src.sin_family = AF_INET;
		src.sin_addr.s_addr = INADDR_ANY;
		src.sin_port = htons(sport);
		if (bind(sock, (struct sockaddr *)&src, sizeof(src)) < 0) {
			report_perror(FATAL, "bind");
			goto err;
		}
	}

	bzero(&dst, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = addr.s_addr; /* already in network order! */
	dst.sin_port = htons(dport);	
	if (connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
		report_perror(FATAL, "connect");
		goto err;
	}

	if (socktype == SOCK_STREAM) {
		/*
		 * disable nagle send coalescing.
		 */
		sockoptarg = IPTOS_LOWDELAY;
		if (setsockopt(sock, IPPROTO_IP, IP_TOS,
			       &sockoptarg, sizeof(sockoptarg)) < 0) {
			report_perror(FATAL, "setsockopt IP_TOS");
			goto err;
		}
		sockoptarg = 1;
		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
			       &sockoptarg, sizeof(sockoptarg)) < 0) {
			report_perror(FATAL, "setsockopt TCP_NODELAY");
			goto err;
		}
	}

	return sock;
 err:
	if (sock > 0) {
		close(sock);
	}
	return -1;
}

/*
 * [socket_server]
 *
 * establish a server socket connection.
 *
 * PARAMETERS:
 * -> sport          local port, in host byte order.
 * -> socktype       socket protocol (SOCK_DGRAM or SOCK_STREAM).
 * -> connect        connection oriented (accept before returning).
 *
 * RETURNS:
 *    int            socket file descriptor, or -1 on error.
 */
int
socket_server(short sport, int socktype, int connect)
{
	struct sockaddr_in src, dst;
	int listensock = 0, sock = 0, sockoptarg, socklen;

	if ((listensock = socket(AF_INET, socktype, 0)) < 0) {
		report_perror(FATAL, "socket");
		goto err;
	}
	bzero(&src, sizeof(src));
	src.sin_family = AF_INET;
	src.sin_addr.s_addr = INADDR_ANY;
	src.sin_port = htons(sport);
	if (bind(listensock, (struct sockaddr *)&src, sizeof(src)) < 0) {
		report_perror(FATAL, "bind");
		goto err;
	}

	if (connect) {
		if (listen(listensock, 5) < 0) {
			report_perror(FATAL, "listen");
			goto err;
		}
		bzero(&dst, sizeof(dst));
		socklen = sizeof(dst);
		if ((sock = 
		     accept(listensock, (struct sockaddr *)&dst, &socklen)) < 0) {
			report_perror(FATAL, "accept");
			goto err;
		}
		close(listensock);
	} else {
		sock = listensock;
	}

	if (socktype == SOCK_STREAM) {
		/*
		 * disable nagle send coalescing.
		 */
		sockoptarg = IPTOS_LOWDELAY;
		if (setsockopt(sock, IPPROTO_IP, IP_TOS,
			       &sockoptarg, sizeof(sockoptarg)) < 0) {
			report_perror(FATAL, "setsockopt IP_TOS");
			goto err;
		}
		sockoptarg = 1;
		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
			       &sockoptarg, sizeof(sockoptarg)) < 0) {
			report_perror(FATAL, "setsockopt TCP_NODELAY");
			goto err;
		}
	}

	return sock;
 err:
	if (listensock > 0) {
		close(listensock);
	}
	if (sock > 0) {
		close(sock);
	}
	return -1;
}

/*
 * [portmap_getport]
 *
 * use the portmap service to locate an rpc server.
 *
 * PARAMETERS:
 * -> addr           remote ip address, in network byte order.
 * -> prog           rpc service program number.
 * -> vers           rpc service version number.
 * -> prot           rpc service protocol (IPPROTO_UDP or IPPROTO_TCP).
 *
 * RETURNS:
 *    int            port number, or -1 on error.
 */
static int
portmap_getport(struct in_addr addr, int prog, int vers, int prot)
{
	int rvalue = -1, sock = 0, off;
	u_int32_t port;
	msg_t m = 0;

	if ((sock = socket_client(addr, 0, PMAPPORT, SOCK_STREAM)) < 0) {
		report_error(FATAL, "socket_client error");
		goto out;
	}

	/*
	 * create the portmap getport message.
	 */
	if ((m = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc error");
		goto out;
	}
	off = msg_insert_htonl_int32(m, 0, prog);
	off = msg_insert_htonl_int32(m, off, vers);
	off = msg_insert_htonl_int32(m, off, prot);
	off = msg_insert_htonl_int32(m, off, 0); /* ignored */

	/*
	 * send the message and wait for the reply.  use the same msg for
	 * the request and the reply.  this is tcp so don't worry about
	 * packet drops, etc.
	 */
	if (rpc_send(sock, SOCK_STREAM, m, PMAPPROG, PMAPVERS, 
		     PMAPPROC_GETPORT, getuid(), getgid(), NULL) < 0) {
		report_error(FATAL, "rpc_send error");
		goto out;
	}
	if ((off = rpc_recv(sock, SOCK_STREAM, m, NULL)) < 0) {
		report_error(FATAL, "rpc_recv error");
		goto out;
	}
	
	/*
	 * extract the port number from the reply.
	 */
	off = msg_extract_ntohl_int32(m, off, &port);
	rvalue = port;
	
 out:
	if (sock > 0) {
		close(sock);
	}
	if (m) {
		msg_free(m);
	}
	return rvalue;	
}

/*
 * [rpc_client]
 *
 * establish a socket connection.
 *
 * PARAMETERS:
 * -> addr           remote ip address, in network byte order.
 * -> prog           rpc service program number.
 * -> vers           rpc service version number.
 * -> socktype       socket protocol (SOCK_STREAM or SOCK_DGRAM).
 * -> sport          local port, in host byte order (or 0=any, 1=root).
 *
 * RETURNS:
 *    int            socket file descriptor, or -1 on error.
 */
int
rpc_client(struct in_addr addr, int prog, int vers, int socktype, int sport)
{
	int sock, dport, prot;

	prot = (socktype == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
	if ((dport = portmap_getport(addr, prog, vers, prot)) < 0) {
		report_error(FATAL, 
			     "portmap_getport(prog=%d, ver=%d) error %d",
			     prog, vers, dport);
		return -1;
	}
	if (dport == 0) {
		report_error(FATAL,
			     "portmap_getport(prog=%d, ver=%d): service not found",
			     prog, vers);
	}
	if ((sock = socket_client(addr, sport, dport, socktype)) < 0) {
		report_error(FATAL, "socket_client error");
		return -1;
	}
	return sock;
}

/*
 * [rpc_server]
 *
 * establish a server socket connection.
 *
 * PARAMETERS:
 * -> prog           rpc service program number.
 * -> vers           rpc service version number.
 * -> socktype       socket protocol (SOCK_STREAM or SOCK_DGRAM).
 * -> sport          local port, in host byte order.
 * -> connect        connection oriented (accept before returning).
 *
 * RETURNS:
 *    int            socket file descriptor, or -1 on error.
 */
int
rpc_server(int prog, int vers, int socktype, int sport, int connect)
{
	int sock, prot = (socktype == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
	struct sockaddr_in saddr;

	/*
	 * if present, unregister with portmap.  then, register with portmap.
	 */
	if (pmap_getport(&saddr, prog, vers, prot) != 0) {
		if (pmap_unset(prog, vers) != 1) {
			report_error(FATAL, "pmap_unset error");
			return -1;
		}
	}
	if (pmap_set(prog, vers, prot, sport) != 1) {
		report_error(FATAL, "pmap_set error");
		return -1;
	}

	if ((sock = socket_server(sport, socktype, connect)) < 0) {
		report_error(FATAL, "socket_server error");
		return -1;
	}
	return sock;
}

