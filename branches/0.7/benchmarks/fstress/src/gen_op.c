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
#include <rpc/rpc.h>
#include <sys/param.h>
#include <assert.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "nfs.h"
#include "nameset.h"
#include "operation.h"
#include "distribution.h"
#include "queue.h"
#include "my_malloc.h"
#include "linger.h"
#include "gen_op.h"

extern int (*rsize_dist_func)(void);
extern int (*wsize_dist_func)(void);

int
gen_op(int proc)
{
	nameset_entry_t nse, pnse;
	u_int64_t offset;
	u_int32_t count;

	switch (proc) {
	case NFSPROC_NULL:
		do_null();
		break;
	case NFSPROC_LOOKUP:
		nse = nameset_select_safe(NFREG); /*XXX*/
		do_lookup(nse);
		break;
	case NFSPROC_READ:
		nse = linger_select(LINGER_READ, &offset, &count);
		if (nse && count) {
			do_read(nse, offset, count);
		}
		break;
	case PROC_SEQREAD:
		nse = linger_select(LINGER_SEQREAD, &offset, &count);
		if (nse && count) {
			do_read(nse, offset, count);
		}
		break;		
	case NFSPROC_WRITE:
		nse = linger_select(LINGER_WRITE, &offset, &count);
		if (nse && count) {
			do_write(nse, offset, count, NFSV3WRITE_UNSTABLE); /*XXX*/
		}
		break;
	case PROC_SEQWRITE:
		nse = linger_select(LINGER_SEQWRITE, &offset, &count);
		if (nse && count) {
			do_write(nse, offset, count, NFSV3WRITE_UNSTABLE); /*XXX*/
		}
		break;
	case PROC_APPENDWRITE:
		nse = linger_select(LINGER_APPENDWRITE, &offset, &count);
		if (nse && count) {
			do_write(nse, offset, count, NFSV3WRITE_UNSTABLE); /*XXX*/
		}
		break;		
	case NFSPROC_GETATTR:
		nse = nameset_select_safe(NFREG); /*XXX*/
		do_getattr(nse);
		break;
	case NFSPROC_READLINK:
		nse = nameset_select_safe(NFLNK);
		do_readlink(nse);
		break;
	case NFSPROC_READDIR:
		nse = nameset_select_safe(NFDIR);
		do_readdir(nse);
		break;
	case NFSPROC_CREATE:
		pnse = nameset_select_safe(NFDIR);
		nse = nameset_alloc(pnse, NFREG, 1); /*XXX*/
		do_create(nse);
		break;
	case NFSPROC_MKDIR:
		pnse = nameset_select_safe(NFDIR);
		nse = nameset_alloc(pnse, NFDIR, 1); /*XXX*/
		do_mkdir(nse);
		break;
	case NFSPROC_REMOVE:
		nse = nameset_select_safe(NFREG);
		linger_dele_notify(nse);
		do_remove(nse);
		break;
	case NFSPROC_RMDIR:
		nse = nameset_select_safe(NFDIR);
		do_rmdir(nse);
		break;
	case NFSPROC_FSSTAT:
		nse = nameset_select_safe(NFREG); /*root? XXX*/
		do_fsstat(nse);
		break;
	case NFSPROC_SETATTR:
		nse = nameset_select_safe(NFREG); /*XXX*/
		do_setattr(nse);
		break;
	case NFSPROC_READDIRPLUS:
		nse = nameset_select_safe(NFDIR);
		do_readdirplus(nse);
		break;
	case NFSPROC_ACCESS:
		nse = nameset_select_safe(NFREG); /*XXX*/
		do_access(nse);
		break;
	case NFSPROC_COMMIT:
		nse = nameset_select_safe(NFREG); /*dirty? XXX*/
		do_commit(nse);
		break;
	default:
		report_error(FATAL, "gen_op bad proc %d", proc);
		return -1;
	}
	return 0;
}
