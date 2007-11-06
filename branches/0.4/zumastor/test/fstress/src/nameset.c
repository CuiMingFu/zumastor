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

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "distheap.h"
#include "nameset.h"

int nameset_entry_fsize = 0;

distheap_t dh_reg, dh_dir, dh_lnk;
char name_prefix[32];

/* --------------------------------------------------------- */

static __inline distheap_t
type2dh(int type)
{
	switch(type) {
	case NFREG:
		return dh_reg;
	case NFDIR:
		return dh_dir;
	case NFLNK:
		return dh_lnk;
	}
	report_error(FATAL, "type2dh: bad type %d", type);
	return NULL;
}

/* --------------------------------------------------------- */

int
nameset_init(char *prefix, int regmax, int dirmax, int lnkmax)
{
	int elemdatasize = sizeof(struct nameset_entry);

	if (strlen(prefix) > sizeof(name_prefix) - 1) {
		report_error(FATAL, "nameset_init prefix too long (%d > %d)",
			     strlen(prefix), sizeof(name_prefix) - 1);
		return -1;
	}
	strncpy(name_prefix, prefix, sizeof(name_prefix));

	if ((dh_reg = distheap_init(regmax, elemdatasize)) == NULL) {
		report_error(FATAL, "distheap_init error");
		return -1;
	}
	if ((dh_dir = distheap_init(dirmax, elemdatasize)) == NULL) {
		report_error(FATAL, "distheap_init error");
		return -1;
	}
	if ((dh_lnk = distheap_init(lnkmax, elemdatasize)) == NULL) {
		report_error(FATAL, "distheap_init error");
		return -1;
	}
	return 0;
}

int
nameset_uninit(void)
{
	if (distheap_uninit(dh_reg) < 0) {
		report_error(FATAL, "distheap_uninit error");
		return -1;
	}
	if (distheap_uninit(dh_dir) < 0) {
		report_error(FATAL, "distheap_uninit error");
		return -1;
	}
	if (distheap_uninit(dh_lnk) < 0) {
		report_error(FATAL, "distheap_uninit error");
		return -1;
	}
	return 0;
}

/* --------------------------------------------------------- */

int
nameset_getfname(nameset_entry_t nse, char *fname, int maxlen)
{
        int idx = distheap_toidx(type2dh(nse->type), nse);
        char type;

        switch (nse->type) {
        case NFREG:
                type = 'f';
                break;
        case NFDIR:
                type = 'd';
                break;
        case NFLNK:
                type = 'l';
                break;
        default:
                report_error(FATAL, "nameset_getfname: bad type %d", 
			nse->type);
                return -1;
        }
        if (snprintf(fname, maxlen, "%s%c-%d", name_prefix, type, idx) >= maxlen) {
                report_error(FATAL, "nameset_getfname: buffer too small (%d)",
			maxlen);
                return -1; /* string truncated -- wanted to overrun */
        }
        return 0;	
}

void
nameset_setfh(nameset_entry_t nse, char *fhdata, int fhlen)
{
	assert(fhlen <= NAMESET_ENTRY_MAX_FHSIZE);
	assert(*((u_int64_t *)fhdata) != 0); /* data in first 8 bytes */
        bcopy(fhdata, nse->fhdata, fhlen);
	nse->fhlen = fhlen;
}

/* --------------------------------------------------------- */

nameset_entry_t
nameset_parent(nameset_entry_t nse)
{
	if (nse->parentdir_idx == -1) {
		return NULL;
	}
	return (nameset_entry_t)distheap_fromidx(type2dh(NFDIR), 
						 nse->parentdir_idx);
}

nameset_entry_t
nameset_select(int type)
{
	distheap_t dh = type2dh(type);
	nameset_entry_t nse;

	nse = distheap_select(dh);
	assert(nse == NULL || nse->type == type);

	return nse;
}

nameset_entry_t
nameset_alloc(nameset_entry_t parent, int type, int weight)
{
	distheap_t dh = type2dh(type);
	nameset_entry_t nse;

	assert(parent == NULL || parent->type == NFDIR);
	assert(weight >= 0);

	if ((nse = distheap_alloc(dh, weight)) == NULL) {
		report_error(FATAL, "distheap_alloc error");
		return NULL;
	}

	bzero(nse, sizeof(struct nameset_entry));
	nse->parentdir_idx = parent ? distheap_toidx(dh_dir, parent) : -1;
	nse->type = type;

	return nse;
}

int
nameset_dele(nameset_entry_t nse)
{
	if (nse->type == NFNON) {
		/*
		 * if an nse is marked for deletion ("dele_when_free") and
		 * would be deleted anyway (remove, rmdir), then both
		 * nameset_deref and an explicit nameset_dele will attempt
		 * to delete it.  this is not a race.  a sort-of-cleaner
		 * fix would be to delay the unref until after the dele in
		 * remove and rmdir.
		 */
		return 0; /* already deleted */
	}
	nse->dele_when_free = 1;
	if (nse->refcnt == 0) {
		distheap_t dh = type2dh(nse->type);
		nse->type = NFNON; /* further type2dh calls will fail */
		return distheap_dele(dh, nse);
	}
	return 0;
}

void
nameset_ref(nameset_entry_t nse)
{
	nse->refcnt++;
	if (nse->refcnt <= 0) {
		report_error(NONFATAL, "nameset entry refcnt overflow");
	}
}

void
nameset_deref(nameset_entry_t nse)
{
	if (nse->refcnt > 0) {
		nse->refcnt--;
	} else {
		report_error(NONFATAL, "nameset entry refcnt underflow");
	}
	if (nse->refcnt == 0 && nse->dele_when_free) {
		nameset_dele(nse);
	}
}

/* --------------------------------------------------------- */

#define SANITY_CHECKS
#ifdef SANITY_CHECKS
static int
nameset_sanity(void)
{
	nameset_entry_t nse;
	int type[3] = {NFREG, NFDIR, NFLNK};
	int i, j, ret = 0, total, valid;
	distheap_t dh;

	for (i=0 ; i<3 ; i++) {
		dh = type2dh(type[i]);
		total = valid = 0;
	
		for (j=1 ; (nse = (nameset_entry_t)
			    distheap_fromidx(dh, j)) != NULL ; j++) {
			total++;
			if (nse->type == NFNON) {
				continue;
			}
			assert(nse->type == type[i]);
			assert(nse->go_away == 0);
			assert(nse->refcnt == 0);
			assert(nse->dele_when_free == 0);
			valid++;
		}

		printf("nameset_sanity type=%d max=%d inuse=%d\n",
		       type[i], total, valid);
	}
	
	return ret;
}
#endif

/* --------------------------------------------------------- */

int
nameset_save(int fd)
{
#ifdef SANITY_CHECKS
	if (nameset_sanity()) {
		report_error(FATAL, "nameset integrity error");
		return -1;
	}
#endif
	if (write(fd, name_prefix, sizeof(name_prefix)) < 0) {
		report_perror(FATAL, "write error");
		return -1;
	}
	if (distheap_save(dh_reg, fd) < 0) {
		report_error(FATAL, "distheap_save error");
		return -1;
	}
	if (distheap_save(dh_dir, fd) < 0) {
		report_error(FATAL, "distheap_save error");
		return -1;
	}
	if (distheap_save(dh_lnk, fd) < 0) {
		report_error(FATAL, "distheap_save error");
		return -1;
	}
	if (fsync(fd) < 0) {
		report_error(FATAL, "distheap_save error");
		return -1;
	}
	return 0;
}

int
nameset_load(int fd)
{
	if (read(fd, name_prefix, sizeof(name_prefix)) < 0) {
		report_perror(FATAL, "read error");
		return -1;
	}
	if ((dh_reg = distheap_load(fd)) == NULL) {
		report_error(FATAL, "distheap_load error");
		return -1;
	}
	if ((dh_dir = distheap_load(fd)) == NULL) {
		report_error(FATAL, "distheap_load error");
		return -1;
	}
	if ((dh_lnk = distheap_load(fd)) == NULL) {
		report_error(FATAL, "distheap_load error");
		return -1;
	}
#ifdef SANITY_CHECKS
	if (nameset_sanity()) {
		report_error(FATAL, "nameset integrity error");
		return -1;
	}
#endif
	return 0;
}

/* --------------------------------------------------------- */

nameset_entry_t
nameset_select_safe(int type)
{
	nameset_entry_t nse;
	int retries = 0;
	
	/*
	 * pray there aren't too many busy entries! XXX
	 */
	do {

		nse = nameset_select(type);
		if (nse == NULL) {
			report_error(FATAL, 
				     "unable to select a free %s, all are busy or deleted",
				     type == NFREG ? "file" :
				     type == NFDIR ? "dir" :
				     type == NFLNK ? "symlink" :
				     "<unknown type>");
			return NULL;
		}
		if (retries++ > 100) {
			report_error(FATAL,
				     "nameset_select_safe(%s) could not find a non-busy entry",
				     type == NFREG ? "file" :
				     type == NFDIR ? "dir" :
				     type == NFLNK ? "symlink" :
				     "<unknown type>");
			return NULL;
		}

	} while (nse->go_away);

	assert(nse->type == type);
	return nse;
}

