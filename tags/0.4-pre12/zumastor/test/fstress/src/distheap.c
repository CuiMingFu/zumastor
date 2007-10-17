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
#include <strings.h>
#include <assert.h>

#include "porting.h"
#include "report.h"
#include "distheap.h"

/* --------------------------------------------------------- */

typedef struct distheap_entry {
	u_int32_t valid:1;
	u_int32_t weight:31;  /* weight for this entry. */
	u_int32_t treeweight; /* weight for tree rooted at this entry. */
	union {
		int freelink; /* next entry in freelist, -OR- */
		char data;    /* first byte of data. */
	} u;
} *distheap_entry_t;

struct distheap {
	unsigned char *entries;
	int entrysize; /* size of an entry, in bytes. */
	int size; /* current occupancy, in entries. */
	int maxsize; /* current allocation, in entries. */
	int freestart; /* first free entry, chaining by freelink. */
};

/* --------------------------------------------------------- */

static __inline distheap_entry_t
distheap_data2entry(const distheap_t heap, const distheap_data_t hd)
{
	return (distheap_entry_t)
		((char *)hd - &((distheap_entry_t)0)->u.data);
}

static __inline distheap_data_t
distheap_entry2data(const distheap_t heap, const distheap_entry_t he)
{
	return (distheap_data_t)&he->u.data;
}

/* --------------------------------------------------------- */

static __inline int
distheap_entry2idx(const distheap_t heap, const distheap_entry_t he)
{
	return ((long)he - (long)heap->entries) / heap->entrysize;
}

static __inline distheap_entry_t
distheap_idx2entry(const distheap_t heap, const int idx)
{
	return (distheap_entry_t)(heap->entries + (idx * heap->entrysize));
}

/* --------------------------------------------------------- */

static __inline distheap_entry_t
distheap_parent(const distheap_t heap, const distheap_entry_t he)
{
	register int idx = distheap_entry2idx(heap, he), ridx = idx / 2;
	return (ridx > 0 ? distheap_idx2entry(heap, ridx) : 0);
}

static __inline distheap_entry_t
distheap_left(const distheap_t heap, const distheap_entry_t he)
{
	register int idx = distheap_entry2idx(heap, he), ridx = idx * 2;
	return (ridx <= heap->maxsize ? distheap_idx2entry(heap, ridx) : 0);
}

static __inline distheap_entry_t
distheap_right(const distheap_t heap, const distheap_entry_t he)
{
	register int idx = distheap_entry2idx(heap, he), ridx = (idx*2) + 1;
	return (ridx <= heap->maxsize ? distheap_idx2entry(heap, ridx) : 0);
}

/* --------------------------------------------------------- */

static int
distheap_sanity_walk(distheap_t heap, distheap_entry_t he)
{
	int l, r;

	/*
	 * terminate recursion.
	 */
	if (he == NULL) {
		return 0;
	}

	/*
	 * entry sanity checks.
	 */
	assert(he->weight >= 0);
	assert(he->treeweight >= 0);
	if (he->valid == 0) {
		assert(he->weight == 0);
	}

	/*
	 * postfix recurse.
	 */
	l = distheap_sanity_walk(heap, distheap_left(heap, he));
	r = distheap_sanity_walk(heap, distheap_right(heap, he));
	
	/*
	 * global sanity check.
	 */
	assert(he->treeweight == he->weight + l + r);

	return he->treeweight;
}

int
distheap_sanity(distheap_t heap)
{
	distheap_sanity_walk(heap, distheap_idx2entry(heap, 1)) /* root */;
	return 0;
}

/* --------------------------------------------------------- */

distheap_t
distheap_init(int maxsize, int elemdatasize)
{
	distheap_entry_t he;
	distheap_t heap;
	int i;

	assert(maxsize >= 0);
	elemdatasize = (elemdatasize + 3) & ~3; /* keep four byte aligned */

	if ((heap = malloc(sizeof(struct distheap))) == NULL) {
		report_perror(FATAL, "malloc");
		return NULL;
	}
	bzero(heap, sizeof(struct distheap));
	heap->entrysize = (sizeof(struct distheap_entry)-4) + elemdatasize;
	heap->maxsize = maxsize;

	if ((heap->entries = 
	     malloc(heap->entrysize * (maxsize + 1))) == NULL) {
		report_perror(FATAL, "malloc");
		free(heap);
		return NULL;
	}
	bzero(heap->entries, heap->entrysize * (maxsize + 1));

	heap->freestart = 1;
	for (i=1 ; i<=maxsize ; i++) {
		he = distheap_idx2entry(heap, i);
		he->weight = he->treeweight = 0; /* redundant with bzero */
		he->u.freelink = i < maxsize ? i + 1 : 0;
	}

	return heap;
}

int
distheap_uninit(distheap_t heap)
{
	free(heap->entries);
	free(heap);
	return 0;
}

/* --------------------------------------------------------- */

distheap_data_t
distheap_alloc(distheap_t heap, int weight)
{
	distheap_entry_t he, i;

	assert(weight >= 0);

	if (heap->freestart == 0) {
		report_error(FATAL, "distheap_alloc: full");
		return NULL;
	}

	/*
	 * grab a free entry and fill it in.
	 */
	he = distheap_idx2entry(heap, heap->freestart);	
	heap->freestart = he->u.freelink;

	assert(he->weight == 0);
	he->weight = weight;
	/* he->treeweight includes lower entries, it may be non-zero! */

	/*
	 * propagate the weight gain up the tree, starting here.
	 */
	for (i = he ; i ; i = distheap_parent(heap, i)) {
		assert(i->treeweight + he->weight >= i->treeweight);/*ovfl*/
		i->treeweight += he->weight;
	}

	heap->size++;

	assert(he->valid == 0);
	he->valid = 1;

	return distheap_entry2data(heap, he);
}

int
distheap_dele(distheap_t heap, distheap_data_t hd)
{
	distheap_entry_t he = distheap_data2entry(heap, hd), i;

	assert(he && he->weight >= 0);
	assert(he->valid == 1);

	/* 
	 * propagate the weight loss up the tree, starting here.
	 */
	for (i = he ; i ; i = distheap_parent(heap, i)) {
		i->treeweight -= he->weight;
		assert(i->treeweight >= 0);
	}

	/*
	 * clear this record and add it to the freelist.
	 */
	he->weight = 0;

	he->valid = 0;
	he->u.freelink = heap->freestart;
	heap->freestart = distheap_entry2idx(heap, he);

	heap->size--;

	return 0;
}

distheap_data_t
distheap_select(distheap_t heap)
{
	distheap_entry_t he = distheap_idx2entry(heap, 1) /* root */, left;
	distheap_data_t r;
	int w, leftw;

	if (he->treeweight == 0) {
		report_error(NONFATAL, "distheap_select: treeweight==0");
		return NULL; /* empty tree */
	}
	w = random() % he->treeweight;

	while (1) {
		/*
		 * if the weight falls in the left subtree, recurse left.
		 */
		if ((left = distheap_left(heap, he)) != NULL) {
			leftw = left->treeweight;
		} else {
			leftw = 0;
		}
		if (w < leftw) {
			he = left;
			continue;
		}

		/*
		 * if remaining weight falls in the current node, use it.
		 */
		w -= leftw;
		if (w < he->weight) {
			r = distheap_entry2data(heap, he);
			assert(r != NULL);
			return r;
		}

		/*
		 * recurse right with remaining weight.
		 */
		w -= he->weight;
		he = distheap_right(heap, he);
	}
	return NULL; /* unreachable */
}

/* --------------------------------------------------------- */

int
distheap_toidx(distheap_t heap, distheap_data_t hd)
{
	return distheap_entry2idx(heap, distheap_data2entry(heap, hd));
}

distheap_data_t
distheap_fromidx(distheap_t heap, int idx)
{
	assert(idx > 0);
	if (idx > heap->maxsize) {
		return NULL; /* out of range */
	}
	return distheap_entry2data(heap, distheap_idx2entry(heap, idx));
}

/* --------------------------------------------------------- */

int
distheap_save(distheap_t heap, int fd)
{
	int bytes;

#ifdef SANITY_CHECKS
	distheap_sanity(heap);
#endif
	if (write(fd, &heap->entrysize, sizeof(int)) < 0) {
		report_perror(FATAL, "write");
		return -1;
	}
	if (write(fd, &heap->size, sizeof(int)) < 0) {
		report_perror(FATAL, "write");
		return -1;
	}
	if (write(fd, &heap->maxsize, sizeof(int)) < 0) {
		report_perror(FATAL, "write");
		return -1;
	}
	if (write(fd, &heap->freestart, sizeof(int)) < 0) {
		report_perror(FATAL, "write");
		return -1;
	}
	bytes = (heap->maxsize + 1) * heap->entrysize;
	if (write(fd, heap->entries, bytes) < 0) {
		report_perror(FATAL, "write");
		return -1;
	}
	return 0;
}

distheap_t
distheap_load(int fd)
{
	int size, maxsize, entrysize, freestart, c, elemdatasize, bytes;
	distheap_t heap;
	
	if ((c = read(fd, &entrysize, sizeof(int))) < sizeof(int)) {
		if (c < 0) {
			report_perror(FATAL, "read");
		} else {
			report_error(FATAL, "distheap_load: short read");
		}
		return NULL;
	}
	if ((c = read(fd, &size, sizeof(int))) < sizeof(int)) {
		if (c < 0) {
			report_perror(FATAL, "read");
		} else {
			report_error(FATAL, "distheap_load: short read");
		}
		return NULL;
	}
	if ((c = read(fd, &maxsize, sizeof(int))) < sizeof(int)) {
		if (c < 0) {
			report_perror(FATAL, "read");
		} else {
			report_error(FATAL, "distheap_load: short read");
		}
		return NULL;
	}
	if ((c = read(fd, &freestart, sizeof(int))) < sizeof(int)) {
		if (c < 0) {
			report_perror(FATAL, "read");
		} else {
			report_error(FATAL, "distheap_load: short read");
		}
		return NULL;
	}

	elemdatasize = entrysize - (sizeof(struct distheap_entry) - 4);

	if ((heap = distheap_init(maxsize, elemdatasize)) == NULL) {
		report_error(FATAL, "distheap_init error");
		return NULL;
	}

	assert(heap->entrysize == entrysize && heap->maxsize == maxsize);

	heap->size = size;
	heap->freestart = freestart;

	bytes = (maxsize + 1) * entrysize;
	if ((c = read(fd, heap->entries, bytes)) < bytes) {
		if (c < 0) {
			report_perror(FATAL, "read");
		} else {
			report_error(FATAL, "distheap_load: short read");
		}
		return NULL;
	}
#ifdef SANITY_CHECKS
	distheap_sanity(heap);
#endif
	return heap;
}

/* --------------------------------------------------------- */

/*
 * dump a heap to a dot-render-source file.
 */

#define DOT_PRINT(_txt) { \
	printf("DOT %p ", (void *)heap); \
	printf _txt ; \
	printf("\n"); \
}

static int
distheap_dot_walk_1(distheap_t heap, distheap_entry_t he)
{
	int visible = 0;

	if (he == NULL) {
		return 0;
	}

	visible += he->valid;
	visible += distheap_dot_walk_1(heap, distheap_left(heap, he));
	visible += distheap_dot_walk_1(heap, distheap_right(heap, he));

	if (visible) {
#if 0
		DOT_PRINT(("node%d[label = \"{{ <w> w=%d | <t> t=%d }|{ <l> l=%d | <r> r=%d}}\"%s];",
			   distheap_entry2idx(heap, he),
			   he->weight,
			   he->treeweight,
			   distheap_entry2idx(heap, distheap_left(heap, he)),
			   distheap_entry2idx(heap, distheap_right(heap, he)),
			   he->valid ? "" : " style=dotted"
			));
#endif
		DOT_PRINT(("node%d[label = \"{<t>t=%d|<w>w=%d|{<l>|<r>}}\"%s];",
			   distheap_entry2idx(heap, he),
			   he->treeweight,
			   he->weight,
			   he->valid ? "" : " style=dotted"
			));
	}

	return visible;
}

static int
distheap_dot_walk_2(distheap_t heap, distheap_entry_t he)
{
	int visible = 0, l, r;

	if (he == NULL) {
		return 0;
	}

	visible += he->valid;
	visible += l = distheap_dot_walk_2(heap, distheap_left(heap, he));
	visible += r = distheap_dot_walk_2(heap, distheap_right(heap, he));

	if (l) {
		DOT_PRINT(("\"node%d\":l -> \"node%d\";",
			   distheap_entry2idx(heap, he),
			   distheap_entry2idx(heap, distheap_left(heap, he))
			));
	}
	if (r) {
		DOT_PRINT(("\"node%d\":r -> \"node%d\";",
			   distheap_entry2idx(heap, he),
			   distheap_entry2idx(heap, distheap_right(heap, he))
			));
	}

	return visible;
}

void
distheap_dot(distheap_t heap)
{
	DOT_PRINT(("# dot -Tps < FILE.dot > FILE.ps"));
	DOT_PRINT(("digraph g {"));
	DOT_PRINT(("orientation = \"landscape\""));
	DOT_PRINT(("size = \"10.5,8\""));
	DOT_PRINT(("node [shape = record, height = 0.1];"));

	distheap_dot_walk_1(heap, distheap_idx2entry(heap, 1));
	distheap_dot_walk_2(heap, distheap_idx2entry(heap, 1));

	DOT_PRINT(("}"));
}
