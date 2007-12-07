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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/rpc.h>

#include "porting.h"
#include "nfs_constants.h"
#include "dns.h"
#include "nfs.h"
#include "mount.h"
#include "distheap.h"
#include "nameset.h"
#include "operation.h"
#include "distribution.h"
#include "createtree.h"
#include "gen_op.h"
#include "report.h"
#include "measure_op.h"

static char *ns_prefix = "_x";
static char *ns_file = "_nameset";
static int maxload = 0;
static int rusage = 0;

static void
usage(void) {
	fprintf(stderr, "usage: fstress_init\n");
	fprintf(stderr, "\t[-nsfile file (default \"%s\")]\n", ns_file);
	fprintf(stderr, "\t[-nsprefix prefix (default \"%s\")]\n", ns_prefix);
	fprintf(stderr, "\t[-maxload N (default %d)]\n", maxload);
	fprintf(stderr, "\t[-rusage]\n");
	exit(1);
}

static void
parse_args(int argc, char *argv[])
{
	int i;
	
	for (i=1 ; i<argc ; i++) {
		if (strcmp(argv[i], "-nsfile") == 0) {
			if (++i == argc) usage();
			ns_file = argv[i];
		}
		else if (strcmp(argv[i], "-nsprefix") == 0) {
			if (++i == argc) usage();
			ns_prefix = argv[i];
		}
		else if (strcmp(argv[i], "-maxload") == 0) {
			if (++i == argc) usage();
			maxload = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-rusage") == 0) {
			rusage = 1;
		}
		else {
			report_error(NONFATAL, 
				     "error parsing \"%s\"", argv[i]);
			usage();
		}
	}
	if (maxload <= 0) usage();
}

int
main(int argc, char *argv[])
{
	int fd, maxfiles, maxdirs, maxsymlinks;

	report_start(argc, argv);
	parse_args(argc, argv);

	maxfiles = 100 + ((40 * maxload) * 10);
	maxdirs = maxfiles / 5;
	maxsymlinks = maxfiles / 50;

	if (nameset_init(ns_prefix,  maxfiles, maxdirs, maxsymlinks) < 0) {
		report_error(FATAL, "nameset_init failed");
		return -1;
	}

	if ((fd = open(ns_file, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
		report_perror(FATAL, "open(\"%s\")", ns_file);
		return -1;
	}
	if (nameset_save(fd) < 0) {
		report_error(FATAL, "nameset_save failed");
		return -1;
	}
	close(fd);

	if (nameset_uninit() < 0) {
		report_error(FATAL, "nameset_uninit failed");
		return -1;
	}
	
	report_stop(rusage);

	return 0;
}
