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

#include "porting.h"
#include "distheap.h"

int
main(int argc, char *argv[])
{
	distheap_t heap;
	distheap_data_t hd;
	int max, idx = 0, weight;
	char line[80];

	srandom(time(NULL));

	if (argc != 2) {
		fprintf(stderr, "usage: distheap-gen max\n");
		return -1;
	}

	max = atoi(argv[1]);
	if (max <= 0) {
		fprintf(stderr, "invalid max %d\n", max);
		return -1;
	}

	heap = distheap_init(max, sizeof(int));

	for (idx = 0 ; fgets(line, sizeof(line), stdin) ; idx++) {
		weight = atoi(line);
		if ((hd = distheap_alloc(heap, weight)) == NULL) {
			fprintf(stderr, "distheap_alloc error\n");
			return -1;
		}
		*(int *)hd = idx;
	}

	while (1) {
		if ((hd = distheap_select(heap)) == NULL) {
			fprintf(stderr, "distheap_select error\n");
			return -1;
		}
		idx = *((int *)hd);
		printf("%d\n", idx);
	}

	distheap_uninit(heap);
	return 0;
}
