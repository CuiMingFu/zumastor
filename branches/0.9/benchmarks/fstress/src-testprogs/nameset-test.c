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
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#include "porting.h"
#include "nfs_constants.h"
#include "nameset.h"

int
main(int argc, char *argv[])
{
	unsigned char fhdata[NAMESET_ENTRY_MAX_FHSIZE];
	nameset_entry_t root, level1, entry;
	int i, j, adds = 0, removes = 0, fd;

	srandom(time(NULL));
	memset(fhdata, 0xEE, sizeof(fhdata)); /* recognizable in hexdump */

	if (argc != 1) {
		fprintf(stderr, "usage: nameset-test\n");
		return -1;
	}

	if (nameset_init("nameset-test-", 100, 100, 0) < 0) {
		fprintf(stderr, "nameset_init error\n");
		return -1;
	}

	if ((root = nameset_alloc(NULL, NFDIR, 1)) == NULL) {
		fprintf(stderr, "nameset_alloc error\n");
		return -1;
	}
	nameset_setfh(root, fhdata, sizeof(fhdata));

	for (i=0 ; i<8 ; i++) {
		if ((level1 = nameset_alloc(root, NFDIR, i+1)) == NULL) {
			fprintf(stderr, "nameset_alloc error\n");
			return -1;
		}
		nameset_setfh(level1, fhdata, sizeof(fhdata));
		for (j=0 ; j<8 ; j++) {
			if ((entry = 
			     nameset_alloc(level1, NFREG, j+1)) == NULL){
				fprintf(stderr, "nameset_alloc error\n");
				return -1;
			}
			nameset_setfh(entry, fhdata, sizeof(fhdata));
			adds++;
		}
	}

#if 1
	if ((fd = open("nameset-test-dummy", O_WRONLY|O_CREAT, 
		       S_IRUSR|S_IWUSR)) < 0) {
		perror("open");
		return -1;
	}
	if (nameset_save(fd) < 0) {
		fprintf(stderr, "nameset_save error\n");
		return -1;
	}
	close(fd);
	nameset_uninit();

	if ((fd = open("nameset-test-dummy", O_RDONLY,
		       S_IRUSR|S_IWUSR)) < 0) {
		perror("open");
		return -1;
	}
	if (nameset_load(fd) < 0) {
		fprintf(stderr, "nameset_load error\n");
		return -1;
	}
	close(fd);
	unlink("nameset-test-dummy");
#endif	

	while ((entry = nameset_select(NFREG)) != NULL) {
		assert(entry->type == NFREG);
		if (nameset_dele(entry)) {
			fprintf(stderr, "nameset_dele error\n");
			return -1;
		}
		removes++;
	}
	if (adds != removes) {
		fprintf(stderr, "nfregs mismatch (%d adds, %d removes)\n",
			adds, removes);
		return -1;
	}	
		
	if (nameset_uninit() < 0) {
		fprintf(stderr, "nameset_uninit error\n");
		return -1;
	}

	printf("nameset-test succeeded\n");
	return 0;
}
