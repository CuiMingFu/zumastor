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
#include <sys/types.h>
#include <rpc/rpc.h>
#include <assert.h>
#include <sys/poll.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "msg.h"
#include "rpc.h"
#include "nfs.h"
#include "mount.h"
#include "timer.h"
#include "metronome.h"
#include "nameset.h"
#include "measure_op.h"
#include "queue.h"
#include "linger.h"
#include "operation.h"

static int report_nfs_rexmits = 0; /* disable for better timings */
static int report_nfs_errors = 0; /* disable for better timings */

int rexmit_age = (2 * 1000); /* retransmit after N msecs. */
int rexmit_max = 2; /* cancel after N retransmissions. [EXPORTED] */

/*
 * this section deals with the low-level rpc call/response pairing,
 * retransmission after loss, and provides a metronome wrapper for a
 * steady request rate with retransmission support.
 */

#define NUM_OP_RECS 16384
#define HASHSIZE 1024

static struct operation_rec {
	u_int64_t lastsend;
	int retransmits;
     
	struct nfsmsg call;
	u_int32_t xid;
	
	op_callback_t callback;
	void *callback_arg;

	struct outstanding_op oop; /* private to measure_op.c */

	Q_ENTRY(operation_rec) link;
	Q_ENTRY(operation_rec) hash;
} *op_recs;
typedef struct operation_rec *op_t;

static Q_HEAD(ops_active, operation_rec) ops_active;
static Q_HEAD(ops_free, operation_rec) ops_free;
static Q_HEAD(ops_hash, operation_rec) ops_hash[HASHSIZE];

static int op_sock, op_socktype;
static int op_outstanding;
int limit_op_outstanding = 0; /* set by command line option */

int
op_init(struct in_addr addr, int socktype)
{
	int i, sock, sport;
	
	if ((op_recs = 
		malloc(sizeof(struct operation_rec) * NUM_OP_RECS)) == NULL) {
		report_perror(FATAL, "malloc");
		return -1;
	}
	bzero(op_recs, sizeof(struct operation_rec) * NUM_OP_RECS);

	sport = (getuid() == 0 ? 1 : 0); /* ask for privileged port if root */
	if ((sock = 
	     rpc_client(addr, NFS_PROG, NFS_VER3, socktype, sport)) < 0) {
		report_error(FATAL, "rpc_client error");
		return -1;
	}
	
	op_sock = sock;
	op_socktype = socktype;
	op_outstanding = 0;

	Q_INIT(&ops_active);
	Q_INIT(&ops_free);
	for (i=0 ; i<HASHSIZE ; i++) {
		Q_INIT(&ops_hash[i]);
	}
	for (i=0 ; i<NUM_OP_RECS ; i++) {
		Q_INSERT_HEAD(&ops_free, &op_recs[i], link);
	}
	return 0;
}

int
op_uninit(void)
{
	if (op_outstanding > 0) {
		report_error(NONFATAL, 
			     "warning: %d outstanding ops at op_uninit",
			     op_outstanding);
	}
	free(op_recs);
	op_recs = NULL;
	return 0;
}

/*
 * metronome with poll-for-replies and retransmissions.
 */

void (*op_metronome_callout)(void *);
void *op_metronome_callout_arg;

static int
op_metronome_func(char *arg)
{
	op_retransmit(); /* retransmit any stale sends */
	(*op_metronome_callout)(op_metronome_callout_arg);
	return 0;
}

int
op_metronome(int rate, int duration, void (*func)(void *), void *arg)
{
	struct metronome mn;
	int waitusecs = 1000;
	u_int64_t start = global_timer();

#define noSPIN_INSTEAD_OF_BLOCK
#ifdef SPIN_INSTEAD_OF_BLOCK
#warning "SPIN INSTEAD OF BLOCK"
	waitusecs = 0;
#endif

	op_metronome_callout = func;
	op_metronome_callout_arg = arg;

	metronome_start(&mn, rate, duration, op_metronome_func, NULL);

	while (metronome_active(&mn)) {
		/*
		 * read and process replies
		 */
		if (op_poll(waitusecs) < 0) {
			report_error(FATAL, "op_poll error");
			return -1;
		}
		/*
		 * send new messages
		 */
		if (metronome_tick(&mn) < 0) {
			report_error(FATAL, "metronome_tick error");
			return -1;
		}
		/*
		 * retransmit stale messages
		 */
		if (op_retransmit() < 0) {
			report_error(FATAL, "op_retransmit error");
			return -1;
		}
		/* Jiaying: continously print response time
		if ((global_timer() - start) >= (60*1000000)) {
			u_int64_t duration;
			duration = global_timer() - start;
			printf("called %d nfsops, ", measure_op_called());
			printf("got %d nfsops, ", measure_op_achieved());
			printf("avg %0.2f ms ", (float)measure_op_global_avg()/1000.0);
			printf("\n");
			measure_op_resetstats();
			start = global_timer();
		}
		*/
	}
	return 0;
}

/* ------------------------------------------------------- */
/*
 * low-level rpc operation interface.
 */

struct nfsmsg *
op_alloc(int proc)
{
	int retries = 0;
	op_t op;

	if (limit_op_outstanding) {
		op_barrier(limit_op_outstanding);
	}

	/*
	 * are we out of free records?  various ways of dealing with this.
	 * note that the following while loop will recycle an active op.
	 */
	if (Q_FIRST(&ops_free) == NULL) {
#if 0
		/*
		 * rely on timeout reclaimation.
		 */
		return NULL;
#endif
#if 0
		/*
		 * don't be *too* aggressive about stealing.  if the free
		 * list is empty, first wait briefly before continuing.
		 */
		u_int64_t start = global_timer();
		report_error(NONFATAL, "out of operation records, waiting");
		while (Q_FIRST(&ops_free) == NULL &&
		       global_timer() - start < 100/*usecs*/) {
			if (op_poll(0/*noblock*/) < 0) {
				report_error(FATAL, "op_poll error");
				return NULL;
			}
		}
#endif
#if 1
		/*
		 * drop a random active message (random early drop).
		 * preserves timing information better than lru.
		 * only pick evens so some victims must time out.
		 */
	retry:
		op = &op_recs[(unsigned int)(random() << 1) % NUM_OP_RECS];

		/* 
		 * make a small effort not to cancel create/delete
		 * operations, otherwise our idea of the file set may
		 * stray from what's really out there.
		 */
		if (retries++ < 8) switch (op->call.proc) {
		case NFSPROC_CREATE:
		case NFSPROC_MKDIR:
		case NFSPROC_REMOVE:
		case NFSPROC_RMDIR:
			goto retry;
		default:
			break;
		}
		op_cancel(op->xid); /* puts it on the freelist */
#endif
	}
     
	while ((op = Q_FIRST(&ops_free)) == NULL) {
		report_error(NONFATAL, "out of operation records, stealing");
		if ((op = Q_LAST(&ops_active, ops_active)) == NULL) {
			report_error(FATAL, "stealing failed");
			return NULL;
		}
		op_cancel(op->xid); /* puts it on the freelist */
	}
	Q_REMOVE(&ops_free, op, link);

	bzero(op, sizeof(struct operation_rec));
	nfsmsg_prep(&op->call, CALL);
	op->call.proc = proc;
	return &op->call;
}

int
op_send(struct nfsmsg *nm, op_callback_t callback, void *callback_arg,
	u_int32_t *xid)
{
	op_t op = (op_t)(((char *)nm) - (long)&((op_t)0)->call);

	op->retransmits = 0;
	op->callback = callback;
	op->callback_arg = callback_arg;
	op->xid = 0; /* tell nfs_send to pick an xid */
	op->lastsend = global_timer();
	if (nfs_send(op_sock, op_socktype, &op->call, &op->xid) < 0) {
		report_error(FATAL, "nfsmsg_send error");
		return -1;
	}
	if (xid) {
		*xid = op->xid;
	}
	measure_op_call(&op->oop, &op->call);

	op_outstanding++;
	assert(0 <= op_outstanding && op_outstanding <= NUM_OP_RECS);
	Q_INSERT_HEAD(&ops_active, op, link);
	Q_INSERT_HEAD(&ops_hash[op->xid & (HASHSIZE-1)], op, hash);
	return 0;
}

void
op_cancel(u_int32_t xid)
{
	op_callback_t callback;
	void *callback_arg;
	op_t op;

	Q_FOREACH(op, &ops_hash[xid & (HASHSIZE-1)], hash) {
		if (op->xid == xid) {
			callback = op->callback;
			callback_arg = op->callback_arg;

			measure_op_reply(&op->oop, &op->call, -1);
			nfsmsg_rele(&op->call);

			op_outstanding--;
			assert(0 <= op_outstanding && 
			       op_outstanding <= NUM_OP_RECS);
			Q_REMOVE(&ops_active, op, link);
			Q_REMOVE(&ops_hash[xid & (HASHSIZE-1)], op, hash);
			Q_INSERT_HEAD(&ops_free, op, link);

			if (callback) {
				(*callback)(callback_arg, NULL, xid);
			}
			break;
		}
	}
}

void
op_cancel_all(void)
{
	op_t op;

	while ((op = Q_FIRST(&ops_active)) != NULL) {
		op_cancel(op->xid);
	}
}

int
op_retransmit(void)
{
	static u_int64_t last_trigger = 0;
	u_int64_t now = global_timer();
	int rexmit_age_usecs = rexmit_age * 1000;
	op_t op;

	/*
	 * limit the active list scan frequency to something useful.
	 */
	if (now - last_trigger < 1000000) {
		return 0;
	}
	last_trigger = now;
	
 restart:
	Q_FOREACH(op, &ops_active, link) {
		if (now - op->lastsend > rexmit_age_usecs) {
			if (++op->retransmits > rexmit_max) {
				if (report_nfs_rexmits) {
					report_error(NONFATAL, 
						     "cancelling %s xid 0x%x",
						     nfs_procstr(op->call.proc), 
						     op->xid);
				}
				op_cancel(op->xid);
				goto restart; /* list fields modified */
			}
			if (report_nfs_rexmits) {
				report_error(NONFATAL, 					     
					     "retransmitting %s xid 0x%x (%d)",
					     nfs_procstr(op->call.proc), 
					     op->xid, op->retransmits);
			}
			op->lastsend = now;
#if 1
			if (op_socktype == SOCK_STREAM) {
				measure_op_rexmit(&op->oop, &op->call);
				return 0; /* let tcp take care of it. */
			}
#endif
			if (nfs_send(op_sock, op_socktype, &op->call, &op->xid) < 0) {
				report_error(FATAL, "nfsmsg_resend error");
				return -1;
			}
			measure_op_rexmit(&op->oop, &op->call);
		}
	}
	return 0;
}

/*
 * op_recv waits (forever) for a reply.  NO REXMIT.
 *
 * op_poll waits at most the specified number of usecs.  NO REXMIT.
 *
 * op_barrier is a weak form of flow control, waiting for the number of
 * outstanding requests to drop below a specified threshold, retransmitting
 * aging requests as necessary.
 */

int
op_recv(void)
{
	struct nfsmsg reply;
	u_int32_t xid;
	int ret = 0;
	op_t op;
	op_callback_t callback;
	void *callback_arg;

	nfsmsg_prep(&reply, REPLY);
     
	if (nfs_recv(op_sock, op_socktype, &reply, &xid) < 0) {
#if 0
		report_error(NONFATAL, "nfsmsg_recv error");
		ret = -1;
#else
		/* tolerate nfs_recv errors: nfs_recv reported the error. */
#endif
		goto out;
	}
	Q_FOREACH(op, &ops_hash[xid & (HASHSIZE-1)], hash) {
		if (op->xid == xid) {
			callback = op->callback;
			callback_arg = op->callback_arg;

			measure_op_reply(&op->oop, &op->call, 
					 reply.u.result_status);
			nfsmsg_rele(&op->call);

			op_outstanding--;
			assert(0 <= op_outstanding && op_outstanding <= NUM_OP_RECS);
			Q_REMOVE(&ops_active, op, link);
			Q_REMOVE(&ops_hash[xid & (HASHSIZE-1)], op, hash);
			Q_INSERT_HEAD(&ops_free, op, link);

			if (callback) {
				(*callback)(callback_arg, &reply, xid);
			}
			goto out;
		}
	}
 out:
	nfsmsg_rele(&reply);
	return ret;
}

int
op_poll(int waitusecs)
{
	struct pollfd pfd;
	int count = 0;

#if 0
#warning "using PEEK instead of POLL"
	/*
	 * alternative to polling: try a non-destructive read.  if there's
	 * something there, then it's ready for service.  (this exists for
	 * experimental socket layers that don't handle poll properly).
	 * (hmm, this doesn't work very well when there's nothing to find.)
	 */
	int len, buf;
	while (1) {
		if ((len = recv(op_sock, &buf, 4, MSG_PEEK)) < 0) {
			report_perror(FATAL, "recv");
			return -1;
		} else if (len == 4) {
			if (op_recv() < 0) {
				report_error(FATAL, "op_recv error");
				return -1;
			}
			count++;
			continue;
		}
		break;
	}
	return count;
#endif

	while (1) {
		pfd.fd = op_sock;
		pfd.events = POLLIN | POLLERR;
		pfd.revents = 0;

		if (poll(&pfd, 1, (waitusecs+999)/1000) < 0) {
			report_perror(FATAL, "poll");
			return -1;
		}
		if (pfd.revents) {
			if (op_recv() < 0) {
				report_error(FATAL, "op_recv error");
				return -1;
			}
			count++;
			continue;
		}
		break;
	}
	return count;
}

int
op_barrier(int maxoutstanding)
{
	int count;

	while (op_outstanding > maxoutstanding) {
		if ((count = op_poll(1000)) < 0) {
			report_error(FATAL, "op_poll error");
			return -1;
		}
		if (op_retransmit() < 0) {
			report_error(FATAL, "op_retransmit error");
			return -1;
		}
	}
	return 0;
}

/* ------------------------------------------------------- */
/*
 * high-level NFS interface.
 */

static void
do_null_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	;
}

int
do_null(void)
{
	struct nfsmsg *nm = NULL;
	u_int32_t xid = 0;

	if ((nm = op_alloc(NFSPROC_NULL)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		return -1;
	}
	if (op_send(nm, do_null_callback, NULL, &xid) < 0) {
		report_error(FATAL, "op_send error");
		return -1;
	}
	return 0;
}

static void
do_lookup_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct lookup_res *res = &reply->u.lookup_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "lookup cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "lookup reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->status == NFS_OK) {
		assert(res->fh.len == nse->fhlen);
		assert(*((u_int64_t *)res->fh.data) != 0);
		if (*((u_int64_t *)nse->fhdata) == 0) {
			/* a create operation didn't fill in the fh? */
			/* fill it in now. XXX */
			nameset_setfh(nse, res->fh.data, res->fh.len);
		}
#if 0 /* check if they match? XXX */
		if (bcmp(res->fh.data, nse->fhdata, res->fh.len) != 0) {

			char fh1[128], fh2[128], tmp[16], i;
			snprintf(fh1, sizeof(fh1), "%d:", nse->fhlen);
			for (i=0 ; i<nse->fhlen ; i++) {
				snprintf(tmp, sizeof(tmp), "%02x",
					 (unsigned char)nse->fhdata[i]);
				strcat(fh1, tmp);
			}
			snprintf(fh2, sizeof(fh2), "%d:", res->fh.len);
			for (i=0 ; i<res->fh.len ; i++) {
				snprintf(tmp, sizeof(tmp), "%02x",
					 (unsigned char)res->fh.data[i]);
				strcat(fh2, tmp);
			}
			
			report_error(NONFATAL, "lookup: server returned unexpected filehandle");

			report_error(NONFATAL, "expect %s", fh1);
			report_error(NONFATAL, "got    %s", fh2);
			report_flush();

			linger_dele_notify(nse);
			nameset_dele(nse);
		}
#endif
	}
}

int
do_lookup(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct lookup_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);

	if ((nm = op_alloc(NFSPROC_LOOKUP)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.lookup_arg;

	arg->what.dir.data = nameset_parent(nse)->fhdata;
	arg->what.dir.len = nameset_parent(nse)->fhlen;
	arg->what.name.data = nfsmsg_alloc_data(nm, NFS_MAXNAMLEN);
	nameset_getfname(nse, arg->what.name.data, NFS_MAXNAMLEN);
	arg->what.name.len = strlen(arg->what.name.data);

	if (op_send(nm, do_lookup_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_read_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct read_res *res = &reply->u.read_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	linger_iodone(nse);
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "read cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "read reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_read(nameset_entry_t nse, u_int64_t offset, int count)
{
	struct nfsmsg *nm = NULL;
	struct read_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	assert(nse->type == NFREG);

	if ((nm = op_alloc(NFSPROC_READ)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.read_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;
	arg->offset = offset;
	arg->count = count;

	if (op_send(nm, do_read_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_write_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct write_res *res = &reply->u.write_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	linger_iodone(nse);
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "write cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "write reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->wcc_data.after.present) {
		if (res->wcc_data.after.fattr.fa_size != nse->size) {
			nse->size = res->wcc_data.after.fattr.fa_size;
		}
	}
}

int
do_write(nameset_entry_t nse, u_int64_t offset, int count, int stable)
{
	struct nfsmsg *nm = NULL;
	struct write_arg *arg;
	u_int32_t xid = 0;
	static void *leaky_write_data = NULL;

	nameset_ref(nse);
	assert(nse->type == NFREG);

	assert(0 < count && count <= MAX_PAYLOAD_SIZE);
	if (leaky_write_data == NULL) {
		if ((leaky_write_data = malloc(8192)) == NULL) {
			report_perror(FATAL, "malloc");
			nameset_deref(nse);
			return -1;
		}
		memset(leaky_write_data, 'a', 8192);
	}

	if (stable == NFSV3WRITE_UNSTABLE) {
		nse->needs_commit = 1;
	}

	if ((nm = op_alloc(NFSPROC_WRITE)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.write_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;
	arg->offset = offset;
	arg->count = count;
	arg->stable = stable;
	arg->data.len = count;
	arg->data.data = leaky_write_data;

	if (op_send(nm, do_write_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_getattr_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct getattr_res *res = &reply->u.getattr_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "getattr cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "getattr reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_getattr(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct getattr_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);

	if ((nm = op_alloc(NFSPROC_GETATTR)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.getattr_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;

	if (op_send(nm, do_getattr_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_readlink_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct readlink_res *res = &reply->u.readlink_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "readlink cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "readlink reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_readlink(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct readlink_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	assert(nse->type == NFLNK);

	if ((nm = op_alloc(NFSPROC_READLINK)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.readlink_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;

	if (op_send(nm, do_readlink_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_readdir_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct readdir_res *res = &reply->u.readdir_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "readdir cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "readdir reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}

	/* keep reading until no more cookies? XXX */
}

int
do_readdir(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct readdir_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	assert(nse->type == NFDIR);

	if ((nm = op_alloc(NFSPROC_READDIR)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.readdir_arg;

	arg->dir.len = nse->fhlen;
	arg->dir.data = nse->fhdata;
	arg->cookie = 0;
	arg->cookieverf = 0;
	arg->count = MAX_PAYLOAD_SIZE;

	if (op_send(nm, do_readdir_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_create_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct create_res *res = &reply->u.create_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nse->go_away = 0;
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "create cancelled");
		}
		linger_dele_notify(nse);
		nameset_dele(nse); /* can't keep.. need fhandle! */
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "create reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->status == NFS_OK) {
		assert(res->optfh.present);
		nameset_setfh(nse, res->optfh.fh.data, res->optfh.fh.len);
	} else {
		linger_dele_notify(nse);
		nameset_dele(nse);
	}
}

int
do_create(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct create_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	nse->go_away = 1;
	assert(nse->type == NFREG);

	if ((nm = op_alloc(NFSPROC_CREATE)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.create_arg;

	arg->where.dir.len = nameset_parent(nse)->fhlen;
	arg->where.dir.data = nameset_parent(nse)->fhdata;
	arg->where.name.data = nfsmsg_alloc_data(nm, NFS_MAXNAMLEN);
	nameset_getfname(nse, arg->where.name.data, NFS_MAXNAMLEN);
	arg->where.name.len = strlen(arg->where.name.data);
	arg->createmode = NFSV3CREATE_UNCHECKED;
	arg->sattr.sa_mode.present = 1;
	arg->sattr.sa_mode.sa_mode = 00644; /*rw-r--r--*/
	arg->sattr.sa_uid.present = 0;
	arg->sattr.sa_gid.present = 0;
	arg->sattr.sa_size.present = 0;
	arg->sattr.sa_atime.present = 0;
	arg->sattr.sa_mtime.present = 0;

	if (op_send(nm, do_create_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}

	return 0;
}

static void
do_mkdir_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct mkdir_res *res = &reply->u.mkdir_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nse->go_away = 0;
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "mkdir cancelled");
		}
		nameset_dele(nse); /* can't keep.. need fhandle! */
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "mkdir reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->status == NFS_OK) {
		assert(res->optfh.present);
		nameset_setfh(nse, res->optfh.fh.data, res->optfh.fh.len);
	} else {
		nameset_dele(nse);
	}
}

int
do_mkdir(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct mkdir_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	nse->go_away = 1;
	assert(nse->type == NFDIR);

	if ((nm = op_alloc(NFSPROC_MKDIR)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.mkdir_arg;

	arg->where.dir.len = nameset_parent(nse)->fhlen;
	arg->where.dir.data = nameset_parent(nse)->fhdata;
	arg->where.name.data = nfsmsg_alloc_data(nm, NFS_MAXNAMLEN);
	nameset_getfname(nse, arg->where.name.data, NFS_MAXNAMLEN);
	arg->where.name.len = strlen(arg->where.name.data);
	arg->sattr.sa_mode.present = 1;
	arg->sattr.sa_mode.sa_mode = 00755; /*rwxr-xr-x*/
	arg->sattr.sa_uid.present = 0;
	arg->sattr.sa_gid.present = 0;
	arg->sattr.sa_size.present = 0;
	arg->sattr.sa_atime.present = 0;
	arg->sattr.sa_mtime.present = 0;

	if (op_send(nm, do_mkdir_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}

	return 0;
}

static void
do_remove_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	nameset_entry_t nse = (nameset_entry_t)arg;
	struct remove_res *res = &reply->u.remove_res;

	nse->go_away = 0;
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "remove cancelled");
		}
		linger_dele_notify(nse);
		nameset_dele(nse);
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "remove reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->status == NFS_OK) {
		linger_dele_notify(nse);
		nameset_dele(nse);
	}
}

int
do_remove(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct remove_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	nse->go_away = 1;
	assert(nse->type == NFREG);

	if ((nm = op_alloc(NFSPROC_REMOVE)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.remove_arg;

	arg->what.dir.data = nameset_parent(nse)->fhdata;
	arg->what.dir.len = nameset_parent(nse)->fhlen;
	arg->what.name.data = nfsmsg_alloc_data(nm, NFS_MAXNAMLEN);
	nameset_getfname(nse, arg->what.name.data, NFS_MAXNAMLEN);
	arg->what.name.len = strlen(arg->what.name.data);

	if (op_send(nm, do_remove_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}

	return 0;
}

static void
do_rmdir_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	nameset_entry_t nse = (nameset_entry_t)arg;
	struct rmdir_res *res = &reply->u.rmdir_res;

	nse->go_away = 0;
	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "rmdir cancelled");
		}
		/* nameset_dele(nse); */ /*XXX*/
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "rmdir reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->status == NFS_OK) {
		nameset_dele(nse);
	}
}

int
do_rmdir(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct rmdir_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	nse->go_away = 1;
	assert(nse->type == NFDIR);

	if ((nm = op_alloc(NFSPROC_RMDIR)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.rmdir_arg;

	arg->what.dir.data = nameset_parent(nse)->fhdata;
	arg->what.dir.len = nameset_parent(nse)->fhlen;
	arg->what.name.data = nfsmsg_alloc_data(nm, NFS_MAXNAMLEN);
	nameset_getfname(nse, arg->what.name.data, NFS_MAXNAMLEN);
	arg->what.name.len = strlen(arg->what.name.data);

	if (op_send(nm, do_rmdir_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nse->go_away = 0;
		nameset_deref(nse);
		return -1;
	}
	
	return 0;
}

static void
do_fsstat_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct fsstat_res *res = &reply->u.fsstat_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "fsstat cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "fsstat reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_fsstat(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct fsstat_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);

	if ((nm = op_alloc(NFSPROC_FSSTAT)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.fsstat_arg;

	arg->fsroot.len = nse->fhlen;
	arg->fsroot.data = nse->fhdata;

	if (op_send(nm, do_fsstat_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_setattr_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	nameset_entry_t nse = (nameset_entry_t)arg;
	struct setattr_res *res = &reply->u.setattr_res;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "setattr cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "setattr reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
	if (res->wcc_data.after.present) {
		if (res->wcc_data.after.fattr.fa_size != nse->size) {
			nse->size = res->wcc_data.after.fattr.fa_size;
		}
	}	
}

int
do_setattr(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct setattr_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);

	if ((nm = op_alloc(NFSPROC_SETATTR)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.setattr_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;
	arg->sattr.sa_mode.present = 0;
	arg->sattr.sa_uid.present = 0;
	arg->sattr.sa_gid.present = 0;
	arg->sattr.sa_size.present = 0;
	arg->sattr.sa_atime.present = 0;
	arg->sattr.sa_mtime.present = 0;
	arg->guard_ctime.present = 0;

	if (op_send(nm, do_setattr_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_readdirplus_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct readdirplus_res *res = &reply->u.readdirplus_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "readdirplus cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "readdirplus reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}

	/* keep reading until no more cookies? XXX */
}

int
do_readdirplus(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct readdirplus_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	assert(nse->type == NFDIR);

	if ((nm = op_alloc(NFSPROC_READDIRPLUS)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.readdirplus_arg;

	arg->dir.len = nse->fhlen;
	arg->dir.data = nse->fhdata;
	arg->cookie = 0;
	arg->cookieverf = 0;
	arg->dircount = MAX_PAYLOAD_SIZE;
	arg->maxcount = MAX_PAYLOAD_SIZE;

	if (op_send(nm, do_readdirplus_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_access_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct access_res *res = &reply->u.access_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "access cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "access reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_access(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct access_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);

	if ((nm = op_alloc(NFSPROC_ACCESS)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.access_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;
	arg->access = NFSV3ACCESS_READ;

	if (op_send(nm, do_access_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

static void
do_commit_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct commit_res *res = &reply->u.commit_res;
	nameset_entry_t nse = (nameset_entry_t)arg;

	nameset_deref(nse);

	if (reply == NULL) {
		if (report_nfs_errors) {
			report_error(NONFATAL, "commit cancelled");
		}
		return;
	}
	if (res->status != NFS_OK && report_nfs_errors) {
		report_error(NONFATAL, "commit reply error %d: %s", 
			     res->status, nfs_errstr(res->status));
	}
}

int
do_commit(nameset_entry_t nse)
{
	struct nfsmsg *nm = NULL;
	struct commit_arg *arg;
	u_int32_t xid = 0;

	nameset_ref(nse);
	assert(nse->type == NFREG);

	if ((nm = op_alloc(NFSPROC_COMMIT)) == NULL) {
		report_error(NONFATAL, "op_alloc error");
		nameset_deref(nse);
		return -1;
	}
	arg = &nm->u.commit_arg;

	arg->fh.len = nse->fhlen;
	arg->fh.data = nse->fhdata;
	arg->offset = 0;
	arg->count = 0; /* commit whole file */

	if (op_send(nm, do_commit_callback, nse, &xid) < 0) {
		report_error(FATAL, "op_send error");
		nameset_deref(nse);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------- */


