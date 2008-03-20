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

	int weights[10] = {1, 60, 5, 3, 3, 20, 1, 1, 5, 1}, counts[10];
	int i, sum = 0, victim = 0, fd;

	srandom(time(NULL));

	if (argc != 1) {
		fprintf(stderr, "usage: distheap-test\n");
		return -1;
	}

	heap = distheap_init(10, sizeof(int));

	for (i=0 ; i<10 ; i++) {
		if ((hd = distheap_alloc(heap, weights[i])) == NULL) {
			fprintf(stderr, "distheap_alloc error\n");
			return -1;
		}
		*(int *)hd = i;
		sum += weights[i];
	}

#if 1
	if ((fd = open("/usr/tmp/distheap-test-dummy", O_WRONLY|O_CREAT, 
		       S_IRUSR|S_IWUSR)) < 0) {
		perror("open");
		return -1;
	}
	if (distheap_save(heap, fd) < 0) {
		fprintf(stderr, "distheap_save error\n");
		unlink("/usr/tmp/distheap-test-dummy");
		return -1;
	}
	close(fd);
	distheap_uninit(heap);

	if ((fd = open("/usr/tmp/distheap-test-dummy", O_RDONLY,
		       S_IRUSR|S_IWUSR)) < 0) {
		perror("open");
		unlink("/usr/tmp/distheap-test-dummy");
		return -1;
	}
	if ((heap = distheap_load(fd)) == NULL) {
		fprintf(stderr, "distheap_load error\n");
		unlink("/usr/tmp/distheap-test-dummy");
		return -1;
	}
	close(fd);
	unlink("/usr/tmp/distheap-test-dummy");
#endif

	while (sum) {
		printf("treeweight = %d\n", sum);

		for (i=0 ; i<10 ; i++) {
			counts[i] = 0;
		}
		for (i=0 ; i<10000 ; i++) {
			if ((hd = distheap_select(heap)) == NULL) {
				fprintf(stderr, "distheap_select error\n");
				return -1;
			}
			counts[*(int *)hd]++;
		}
		for (i=0 ; i<10 ; i++) {
			printf("%d\t", weights[i]);
		}
		printf("\n");
		for (i=0 ; i<10 ; i++) {
			float want = (float)weights[i]/(float)sum*100.0;
			printf("%0.2f%%\t", want);
		}
		printf("\n");
		for (i=0 ; i<10 ; i++) {
			float saw = (float)counts[i]/10000.0*100.0;
			printf("%0.2f%%\t", saw);
		}
		printf("\n");
		
		if ((hd = distheap_select(heap)) != NULL) {
			victim = *(int *)hd;
			printf("removing entry %d (weight %d)\n", 
			       victim, weights[victim]);
			sum -= weights[victim];
			weights[victim] = 0;
			distheap_dele(heap, hd);
		}

		weights[victim] = 3;
		if ((hd = distheap_alloc(heap, weights[victim])) == NULL) {
			fprintf(stderr, "distheap_alloc error\n");
			return -1;
		}
		printf("adding entry %d (weight %d)\n", 
		       victim, weights[victim]);
		*(int *)hd = victim;
		sum += weights[victim];

		if ((hd = distheap_select(heap)) != NULL) {
			victim = *(int *)hd;
			printf("removing entry %d (weight %d)\n", 
			       victim, weights[victim]);
			sum -= weights[victim];
			weights[victim] = 0;
			distheap_dele(heap, hd);
		}
	}

	distheap_uninit(heap);
	return 0;
}
