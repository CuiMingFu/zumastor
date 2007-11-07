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
 * nameset maintains a directory tree, supporting weighted selection of
 * files, directories, and symlinks.  nameset can save and restore its
 * state.  the usage model is, given a new or loaded nameset, new entries
 * are allocated, then the corresponding file/dir/symlink is created, and
 * finally the nameset entry is completed by setting its file handle.
 * nameset entry selection chooses an appropriately typed entry.  there is
 * no interface for directory tree traversal.
 */

#define NAMESET_ENTRY_MAX_FHSIZE 64
/*
 * #if (NAMESET_ENTRY_MAX_FHSIZE > 32)
 * #warning "reduce NAMESET_ENTRY_MAX_FHSIZE for better memory use"
 * #endif
 */

typedef struct nameset_entry {

	int parentdir_idx; /* index for parent directory (-1 for root) */
	unsigned char fhdata[NAMESET_ENTRY_MAX_FHSIZE];

	unsigned int fhlen:7; /* fhdata length, max 64 */
	unsigned int type:3; /* NFNON(0), NFREG(1), NFDIR(2), or NFLNK(5) */
	unsigned int needs_commit:1; /* dirty file */
	unsigned int go_away:1; /* create/remove in progress */
	unsigned int dele_when_free:1; /* release on last unref */
	unsigned int refcnt:16;
	unsigned int RESERVED:3;

	unsigned int size; /* dir capacity or file size (26=64 MB max) */

} *nameset_entry_t;

int nameset_init(char *prefix, int regmax, int dirmax, int lnkmax);
int nameset_uninit(void);
int nameset_save(int fd);
int nameset_load(int fd);

int nameset_getfname(nameset_entry_t nse, char *fname, int maxlen);
void nameset_setfh(nameset_entry_t nse, char *fhdata, int fhlen);

nameset_entry_t nameset_parent(nameset_entry_t nse);
nameset_entry_t nameset_select(int type);
nameset_entry_t nameset_alloc(nameset_entry_t parent, int type, int weight);
int nameset_dele(nameset_entry_t nse);

void nameset_ref(nameset_entry_t nse);
void nameset_deref(nameset_entry_t nse);

nameset_entry_t nameset_select_safe(int type);
