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
#include <ctype.h>
#include <assert.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "distheap.h"
#include "distribution.h"

#if 0
#define DEBUG_PRINTF(_x_) printf _x_ ;
#endif
#ifndef DEBUG_PRINTF
#define DEBUG_PRINTF(_x_)
#endif

struct distitem {
	int value, weight;
};

static distheap_t
dist_init(struct distitem *mix)
{
	int i, *data, setsize = 0;
	distheap_t dh;

	while (mix[setsize].weight != -1) {
		setsize++;
	}
	if ((dh = distheap_init(setsize, sizeof(int))) == NULL) {
		report_error(FATAL, "distheap_init failed");
		return NULL;
	}
	for (i=0 ; i<setsize ; i++) {
		if ((data = distheap_alloc(dh, mix[i].weight)) == NULL) {
			report_error(FATAL, "distheap_alloc failed");
			return NULL;
		}
		*data = mix[i].value;
	}
	return dh;
}

/*
 * nasty macro.. set up a private distheap and distitem array, 
 * define a selection function for that array, and assign whatever
 * follows the macro definition to the distitem array contents.
 */
#define CREATE_DIST( _name ) \
static distheap_t _name ## dh; \
static struct distitem _name ## mix[]; \
int _name (void) { \
	if ( _name ## dh == NULL) { \
		_name ## dh = dist_init(_name ## mix); \
	} \
	return *(int *)distheap_select( _name ## dh); \
} \
static struct distitem _name ## mix[] =

/* ------------------------------------------------------- */

CREATE_DIST(boring_dist) {
	{ 1, 1 },
	{ -1, -1}
};

CREATE_DIST(boringbig_dist) {
	{ 10000, 1 },
	{ -1, -1}
};

CREATE_DIST(null_dist) {
	{ 0, 0 },
	{ -1, -1}
};

CREATE_DIST(op_dist) {
	{ NFSPROC_LOOKUP, 27 },
	{ NFSPROC_READ, 18 },
	{ NFSPROC_WRITE, 9 },
	{ NFSPROC_GETATTR, 11 },
	{ NFSPROC_READLINK, 7 },
	{ NFSPROC_READDIR, 2 },
	{ NFSPROC_CREATE, 1 },
	{ NFSPROC_REMOVE, 1 },
	{ NFSPROC_FSSTAT, 1 }, 
	{ NFSPROC_SETATTR, 1 },
	{ NFSPROC_READDIRPLUS, 9 },
	{ NFSPROC_ACCESS, 7 },
	{ NFSPROC_COMMIT, 5 },
	{ -1, -1}
};

CREATE_DIST(fsize_dist) {
	{ 1024 * 1, 33 },
	{ 1024 * 2, 21 },
	{ 1024 * 4, 13 },
	{ 1024 * 8, 10 },
	{ 1024 * 16, 8 },
	{ 1024 * 32, 5 },
	{ 1024 * 64, 4 },
	{ 1024 * 128, 3 },
	{ 1024 * 256, 2 },
	{ 1024 * 1024, 1 },
	{ -1, -1}
};

CREATE_DIST(rsize_dist) {
	{ 8192 * 1, 85 },
	{ 8192 * 2, 8 },
	{ 8192 * 4, 4 },
	{ 8192 * 8, 2 },
	{ 8192 * 16, 1 },
	{ -1, -1 }
};
CREATE_DIST(wsize_dist) {
	{ 8192 * 0, 49 },
	{ 8192 * 1, 36 },
	{ 8192 * 2, 8 },
	{ 8192 * 4, 4 },
	{ 8192 * 8, 2 },
	{ 8192 * 16, 1 },
	{ -1, -1 }
};
CREATE_DIST(wpip_dist) {
	{ 0, 49 },
	{ 16, 36 },
	{ 64, 8 },
	{ 256, 4 },
	{ 1024, 2 },
	{ 4096, 1 },
	{ -1, -1 }
};

/* ------------------------------------------------------- */

/*
 * syntax is "item1:weight1 item2:weight2 ... itemN:weightN"
 * THIS IS ONE STRING, QUOTE IT ON THE COMMAND LINE.
 */

static distheap_t
dist_init_str(char *str)
{
	int *data, setsize = 0, weight, item;
	distheap_t dh;
	char *c, *c1, *c2;

	for (c = str ; *c ; c++) {
		if (*c == ':') {
			setsize++;
		}
	}
	DEBUG_PRINTF(("create_new_dist> setsize %d\n", setsize));
	if ((dh = distheap_init(setsize, sizeof(int))) == NULL) {
		report_error(FATAL, "distheap_init failed");
		return NULL;
	}
	
	for (c = str ; *c ; ) {
		/*
		 * c1: starts at item str.
		 * c2: advance to weight str.
		 * c: advance to next item:weight pair.
		 */
		c1 = c;
		for (c2 = c ; *c2 && *c2 != ':' ; c2++) ; 
		if (*c2) { c2++; }
		for ( ; *c && *c != ' ' && *c != ',' ; c++) ; 
		if (*c) { c++; }

		item = atoi(c1);
		weight = atoi(c2);
		DEBUG_PRINTF(("create_new_dist> item=%d weight=%d\n", item, weight));

		if ((data = distheap_alloc(dh, weight)) == NULL) {
			report_error(FATAL, "distheap_alloc failed");
			return NULL;
		}
		*data = item;
	}
	return dh;
}

static distheap_t
dist_init_file(char *fname)
{
	int *data, setsize = 0, weight, item;
	distheap_t dh;
	FILE *fp;
	char *format = "%d %d";

	if ((fp = fopen(fname, "r")) == NULL) {
		report_perror(FATAL, "fopen %s failed", fname);
		return NULL;
	}
	while (!feof(fp)) {
		fscanf(fp, format, &item, &weight);
		setsize++;
	}
	rewind(fp);
	
	DEBUG_PRINTF(("create_new_dist> setsize %d\n", setsize));
	if ((dh = distheap_init(setsize, sizeof(int))) == NULL) {
		report_error(FATAL, "distheap_init failed");
		return NULL;
	}
	while (!feof(fp)) {
		item = weight = 0;
		fscanf(fp, format, &item, &weight);
		DEBUG_PRINTF(("create_new_dist> item=%d weight=%d\n", item, weight));
		
		if ((data = distheap_alloc(dh, weight)) == NULL) {
			report_error(FATAL, "distheap_alloc failed");
			return NULL;
		}
		*data = item;
	}
	fclose(fp);

	return dh;
}

/*
 * ugly.  provide a few dist functions for command line overrides.
 */

static distheap_t dist[16];
static int dist0(void) { return *(int *)distheap_select(dist[0]); }
static int dist1(void) { return *(int *)distheap_select(dist[1]); }
static int dist2(void) { return *(int *)distheap_select(dist[2]); }
static int dist3(void) { return *(int *)distheap_select(dist[3]); }
static int dist4(void) { return *(int *)distheap_select(dist[4]); }
static int dist5(void) { return *(int *)distheap_select(dist[5]); }
static int dist6(void) { return *(int *)distheap_select(dist[6]); }
static int dist7(void) { return *(int *)distheap_select(dist[7]); }
static int dist8(void) { return *(int *)distheap_select(dist[0]); }
static int dist9(void) { return *(int *)distheap_select(dist[1]); }
static int distA(void) { return *(int *)distheap_select(dist[2]); }
static int distB(void) { return *(int *)distheap_select(dist[3]); }
static int distC(void) { return *(int *)distheap_select(dist[4]); }
static int distD(void) { return *(int *)distheap_select(dist[5]); }
static int distE(void) { return *(int *)distheap_select(dist[6]); }
static int distF(void) { return *(int *)distheap_select(dist[7]); }
static int (*distN[])() = {
	dist0,dist1,dist2,dist3,dist4,dist5,dist6,dist7,
	dist8,dist9,distA,distB,distC,distD,distE,distF
};
static int dist_next_alloc = 0;

int 
(*dist_str(char *str))(void)
{
	assert(0 <= dist_next_alloc && dist_next_alloc < 16);
	if (isdigit((int)str[0])) {
		dist[dist_next_alloc] = dist_init_str(str);
	} else {
		dist[dist_next_alloc] = dist_init_file(str);
	}
	return distN[dist_next_alloc++];
}

