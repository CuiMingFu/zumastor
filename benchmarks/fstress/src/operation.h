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

#define MAX_PAYLOAD_SIZE 8192

#if 0
#warning "ACP SANITY CHECK -- REMOVE FOR PRODUCTION"
#undef MAX_PAYLOAD_SIZE
#define MAX_PAYLOAD_SIZE 1024
#endif

extern int rexmit_age;
extern int rexmit_max;
struct nfsmsg;

typedef void (*op_callback_t)(void *arg, struct nfsmsg *reply, u_int32_t xid);

int op_init(struct in_addr addr, int socktype);
int op_uninit(void);
int op_metronome(int rate, int duration, void (*func)(void *), void *arg);

/* ------------------------------------------------------- */
/*
 * low-level rpc operation interface.
 */

struct nfsmsg *op_alloc(int proc);
int op_send(struct nfsmsg *nm, op_callback_t callback, void *callback_arg,
	    u_int32_t *xid);
void op_cancel(u_int32_t xid);
void op_cancel_all(void);
int op_retransmit(void);

int op_recv(void);
int op_poll(int waitusecs);
int op_barrier(int maxoutstanding);

/* ------------------------------------------------------- */
/*
 * high-level NFS interface.
 */

int do_null(void);
int do_lookup(nameset_entry_t nse);
int do_read(nameset_entry_t nse, u_int64_t offset, int count);
int do_write(nameset_entry_t nse, u_int64_t offset, int count, int stable);
int do_getattr(nameset_entry_t nse);
int do_readlink(nameset_entry_t nse);
int do_readdir(nameset_entry_t nse);
int do_create(nameset_entry_t nse);
int do_mkdir(nameset_entry_t nse);
int do_remove(nameset_entry_t nse);
int do_rmdir(nameset_entry_t nse);
int do_fsstat(nameset_entry_t nse);
int do_setattr(nameset_entry_t nse);
int do_readdirplus(nameset_entry_t nse);
int do_access(nameset_entry_t nse);
int do_commit(nameset_entry_t nse);

