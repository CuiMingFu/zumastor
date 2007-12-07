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

/*
 * recycling malloc
 * Darrell Anderson
 *
 * !!! amortize sbrk !!!
 *
 * Buffer calls to sbrk().  my_sbrk() maintains a small buffer
 * (SBRK_BUFSIZE), invokations fall into one of three cases:
 *
 * 1. The request is larger than the maximum buffer size: honor these
 * directly.  If the user is requesting more than the buffer size, they are
 * probably asking for much more.
 *
 * 2. The request is less than the maximum buffer size, but the available
 * buffer space is too small: allocate a larger buffer and proceed to the
 * next step.  Note that if the newly allocated buffer is adjacent to the
 * previous one, they are combined (should always happen unless sbrk() is
 * being called elsewhere).
 *
 * 3. The request fits within the available buffer space: touch the buffer
 * accordingly, hand back the allocated segment.
 *
 * !!! recycle !!!
 *
 * In my_malloc, the block size allocated is the requested size rounded up
 * the the nearest power of two.  When freed, it is placed on a free list
 * specifically for blocks of that size (only 33 such lists need be
 * maintained as 2^33 is the upper bound on requests).
 *
 * The next time a similarly sized (match after rounding up to the same
 * power of two) is requested, the freed block can be issued without
 * resorting to sbrk().
 *
 * !!! penalties !!!
 *
 * Rounding size requests up to the next power of two can be wasteful in
 * both time and space, especially when such requests are very large.
 *
 * This isn't a large concern becase memory is somewhat cheap and *large*
 * non-power-of-two requests are usually few if at all.  Also, these wasted
 * segments consume a process' virtual address space, but neither physical
 * nor virtual memory so long as they remain untouched.
 *
 * !!! my_malloc() vs system malloc() !!!
 *
 * Omitting the details of the tests, my_malloc() performs from 3 to 100
 * times faster than system malloc, doing especially well when memory is
 * freed and reallocated.  
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "porting.h"
#include "my_malloc.h"

#define SANITY 0xdca3a110
#define SBRK_BUFSIZE (64 * 1024)

struct mem_hdr {
	u_int32_t sanity;
	u_int32_t log2size;
	struct mem_hdr *next;
	char padding[4];
};

/* 
 * free_list[n] points to the first element of a linked list of mem_cells
 * (mem_header followed by associated memory) with associated memory size
 * of 2^n.  This is a FILO stack.
 */
void *free_list[34];

/*
 * allocate memory.  note that because free list contains initially null
 * elements nothing special need be done "at startup" 
 */
void *
my_malloc(u_int32_t size)
{
	struct mem_hdr *hdr;
	int log2size = 0, t;
  
	/* 
	 * round size to next pow2, and compute that log2.
	 */
	t = size;
	while (t > 1) {
		log2size++;
		t >>= 1;
	}
	if (size != (1 << log2size)) {
		log2size++;
		size = 1 << log2size;
	}

	if(free_list[log2size] == NULL) {
		/* 
		 * the free list is empty, create a record.
		 */
		if ((hdr = my_sbrk(size + sizeof(*hdr))) == NULL) {
			return NULL;
		}
		hdr->sanity = SANITY;
		hdr->log2size = log2size;
	} else {
		/* 
		 * fetch the first item in the free list.
		 */
		hdr = free_list[log2size];
		free_list[log2size] = hdr->next;
	}

	/* 
	 * the memory segment is after the header.
	 */
	return (char *)hdr + sizeof(*hdr);
}

/* 
 * return memory allocated with my_malloc to the "free pool" 
 */
void
my_free(void *ptr)
{
	struct mem_hdr *hdr = 
		(struct mem_hdr *)((char *)ptr - sizeof(*hdr));
	
	assert(ptr);
	assert(hdr->sanity == SANITY);
	
	hdr->next = free_list[hdr->log2size];
	free_list[hdr->log2size] = hdr;
}

static void *
real_sbrk(u_int32_t size)
{
#if 0
	return sbrk(size);
#else
	return malloc(size);
#endif

}

/*
 * a 'buffering' wrapper to sbrk with three cases: (1.) request is larger
 * than max buffer size: honor these directly.  (2.) not enough space in
 * buffer: expand buffer, then (3.).  (3.) request available in buffer: use
 * it, shrink buffer accordingly.  Note that the initial call falls in case
 * 2 because start=end=0.  Also note that if sbrk returns null, that null
 * is passed on (sbrk set errno for us).
*/
void *
my_sbrk(u_int32_t size)
{
	static char *start = NULL, *end = NULL;
	char *ptr;

	/*
	 * case (1.), request larger than max buffer size. honor directly.
	 */
	if (size > SBRK_BUFSIZE) {
		return (void *)real_sbrk(size);
	}

	/* 
	 * case (2.), buffer needs epansion (or cold start start=end=0).
	 */
	if (end - start < size) {
		if ((ptr = (char *)real_sbrk(SBRK_BUFSIZE)) == NULL) {
			return NULL;
		}
		/* 
		 * if buffers are not adjacent, reset start 
		 */
		if (ptr != end) {
			start = ptr;
		}
		end = ptr + SBRK_BUFSIZE;
	}

	/* 
	 * case (3.), use buffer for request, align start for next call.
	 */
	ptr = start;
	start += (size + 7) & ~7;

	return ptr;
}



