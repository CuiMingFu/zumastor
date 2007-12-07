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
#include "nfs.h"
#include "nameset.h"
#include "queue.h"
#include "my_malloc.h"
#include "gen_op.h"
#include "report.h"
#include "operation.h"
#include "linger.h"

extern int (*rsize_dist_func)(void);
extern int (*wsize_dist_func)(void);

static int linger_maxdepth = 16;
static int linger_maxinuse = 8192;
static int linger_inuse = 0;

int
linger_set_maxdepth(int depth)
{
	if (depth >= 0) {
		linger_maxdepth = depth;
	}
	return linger_maxdepth;
}

int
linger_set_maxinuse(int inuse)
{
	if (inuse >= 0) {
		linger_maxinuse = inuse;
	}
	return linger_maxinuse;
}

struct linger_state {
	int iocnt;

	nameset_entry_t nse;
	u_int64_t off;
	u_int32_t cnt;

	Q_ENTRY(linger_state) link;
};
Q_HEAD(NULL, linger_state) linger_list[NUM_LINGER_TYPES];

nameset_entry_t
linger_select(int type, u_int64_t *off, u_int32_t *cnt)
{
	struct linger_state *ls;
	nameset_entry_t nse;

	/*
	 * search for an "idle-enough" active entry.
	 */
 retry:
	Q_FOREACH(ls, &linger_list[type], link) {
		if (ls->iocnt < linger_maxdepth) {
			break;
		}
	}
	if (ls != NULL && ls->nse->type != NFREG) {
		report_error(NONFATAL, "linger_select found a stale sequential I/O entry, retrying");
		Q_REMOVE(&linger_list[type], ls, link);
		my_free(ls);
		linger_inuse--;
		goto retry;
	}

	/*
	 * if we didn't find one, allocate a new one.
	 */
	if (ls == NULL) {

		if (linger_inuse >= linger_maxinuse) {
			report_error(NONFATAL, 
				     "linger_select: maxinuse (%d) exceeded", 
				     linger_maxinuse);
			*off = 0;
			*cnt = 0;
			return NULL;
		}
		linger_inuse++;

		nse = nameset_select_safe(NFREG);
		ls = my_malloc(sizeof(struct linger_state));
		bzero(ls, sizeof(struct linger_state));
		ls->iocnt = 0;
		ls->nse = nse;
		switch(type) {
		case LINGER_READ:
			ls->off = nse->size ? (random() % nse->size) & ~8191 : 0;
			ls->cnt = (*rsize_dist_func)();
			ls->cnt = MIN(ls->cnt, nse->size - ls->off);
			break;
		case LINGER_SEQREAD:
			ls->off = 0;
			ls->cnt = nse->size;
			break;
		case LINGER_WRITE:
			ls->off = nse->size ? (random() % nse->size) & ~8191 : 0;
			ls->cnt = (*wsize_dist_func)();
			break;
		case LINGER_SEQWRITE:
			ls->off = 0;
			ls->cnt = (*wsize_dist_func)();
			ls->cnt = MAX(ls->cnt, nse->size);
			break;
		case LINGER_APPENDWRITE:
			ls->off = nse->size;
			ls->cnt = (*wsize_dist_func)();
			break;
		}
		Q_INSERT_TAIL(&linger_list[type], ls, link);
	}

	/*
	 * "ls" is an active entry with a low-enough I/O count.
	 */
	nse = ls->nse;
	*off = ls->off;
	*cnt = MIN(MAX_PAYLOAD_SIZE, ls->cnt);
	ls->off += *cnt;
	ls->cnt -= *cnt;
	
	ls->iocnt += 1;

	if (ls->cnt == 0) {
		Q_REMOVE(&linger_list[type], ls, link);
		my_free(ls);
		linger_inuse--;
	}
	
	assert(nse->type == NFREG);
	return nse;
}

void
linger_dele_notify(nameset_entry_t nse)
{
	struct linger_state *ls;
	int type, loopcount = 0;
	
	/* linear scan.  big enough to make a real hash table? */
	for (type=0 ; type < NUM_LINGER_TYPES ; type++) {
	again:
		Q_FOREACH(ls, &linger_list[type], link) {
			if (ls->nse == nse) {
				Q_REMOVE(&linger_list[type], ls, link);
				my_free(ls);
				linger_inuse--;
				/*
				 * Q_FOREACH can't tolerate removal, and
				 * it's possible that the name nameset
				 * entry would appear in two or more linger
				 * state entries of the same type.  so, if
				 * we found one, rescan the list to see if
				 * there is another.
				 */
				assert(loopcount++ < linger_maxinuse);
				goto again;
			}
		}
	}
}

void
linger_iodone(nameset_entry_t nse)
{
	struct linger_state *ls;
	int type;

	/* linear scan.  big enough to make a real hash table? */
	for (type=0 ; type < NUM_LINGER_TYPES ; type++) {
		Q_FOREACH(ls, &linger_list[type], link) {
			if (ls->nse == nse) {
				/* weak record of outstanding I/O count */
				if (ls->iocnt > 0) {
					ls->iocnt--;
				}
			}
		}
	}
}
