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
 * this heap differs slightly from the standard, in that once a heap entry
 * is allocated, it does not move until deletion, allowing external
 * references into the heap.  instead of removal via swapping entries, this
 * heap allows "free" entries inside the heap proper.  these free entries
 * have a zero weight, and thus are skipped in the selection process.  free
 * entries are recycled by later allocs.
 *
 * secondly, the heap nature is used to implement distribution select.
 * each "tree" element has a weight, and also records the weight of the
 * subtree rooted at that element.  selection chooses a weight between zero
 * and the root's tree weight, then iterates down the tree to locate the
 * element responsible for the chosen value.  by seeding entries according
 * to a distribution, this selection maps uniform (random) into the seeded
 * distribution.
 *
 * Darrell Anderson, 10/2000
 * Thanks to Syam Gadde for helpful discussion.
 */

typedef struct distheap * distheap_t;
typedef void * distheap_data_t;

/*
 * create/destroy a distheap instance.
 */
distheap_t distheap_init(int maxsize, int elemdatasize);
int distheap_uninit(distheap_t heap);

int distheap_sanity(distheap_t heap);

/*
 * create/destroy/select distheap entries.
 */
distheap_data_t distheap_alloc(distheap_t heap, int weight);
int distheap_dele(distheap_t heap, distheap_data_t hd);
distheap_data_t distheap_select(distheap_t heap);

int distheap_toidx(distheap_t heap, distheap_data_t hd);
distheap_data_t distheap_fromidx(distheap_t heap, int idx);

int distheap_save(distheap_t heap, int fd);
distheap_t distheap_load(int fd);

void distheap_dot(distheap_t heap);
