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
#include <time.h>
#include <sys/time.h>
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
#include "report.h"

/* so linger.o will link. */
int (*rsize_dist_func)(void) = rsize_dist;
int (*wsize_dist_func)(void) = wsize_dist;

#ifdef IPPROTO_TUF
extern int tuf_client;
#endif

static int num_dirs_int = 20; /* 20 in SPECsfs97 XXX */

/*
 * 1 directory in the root, then 20 subdirectories.
 */
static int
num_dirs(void)
{
#if 0
	static int num_dirs_callnumber = 0;
	switch (num_dirs_callnumber++) {
	case 0:
		return 1;
	case 1:
		return num_dirs_int;
	default:
		return 0;
	}
#endif
	return num_dirs_int; /* createtree now makes our root dir for us. */
}

/*
 * 0 files in the top dir, spread remaining files.
 */
static int filesperdir = 0;
static int
num_files(void)
{
	static int num_files_callnumber = 0;
	switch (num_files_callnumber++) {
	case 0:
		return 0;
	default:
		return filesperdir;
	}
}

/*
 * 0 symlinks in the top dir, spread remaining symlinks.
 */
static int
num_symlinks(void)
{
	static int num_symlinks_callnumber = 0;
	switch (num_symlinks_callnumber++) {
	case 0:
		return 0;
	default:
		return 1; /* one symlink in each dir */
	}
}

static int
tenpercent_dist(void)
{
	if (random() % 10 == 0) {
		return 1000;
	}
	return 1;
}

static char *ns_file = "_nameset";
static int addload = 0;
static char *server = "vmhost";
static char *path = "/tmp";
static char *transp = "udp";
static int maxdepth = 2;
static int (*fcnt_dist_func)(void) = num_files;
static int (*fpop_dist_func)(void) = tenpercent_dist;
static int (*fsize_dist_func)(void) = fsize_dist;
static int (*dcnt_dist_func)(void) = num_dirs;
static int (*dpop_dist_func)(void) = boringbig_dist;
static int (*lcnt_dist_func)(void) = num_symlinks;
static int (*lpop_dist_func)(void) = boringbig_dist;
static int rusage = 0;

static void
usage(void) {
	fprintf(stderr, "usage: fstress_fill\n");
	fprintf(stderr, "\t[-nsfile file (default \"%s\")]\n", ns_file);
	fprintf(stderr, "\t[-host server:path (default %s:%s)]\n", server, path);
	fprintf(stderr, "\t[-transp udp|tcp (default %s)]\n", transp);
	fprintf(stderr, "\t[-addload N (default %d)]\n", addload);
	fprintf(stderr, "\t[-workload X (default N/A)]\n");
	fprintf(stderr, "\t[-fcntdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-fpopdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-fsizedist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-dcntdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-dpopdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-lcntdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-lpopdist \"v:w v:w ... v:w\" (default SPEC)]\n");
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
		else if (strcmp(argv[i], "-host") == 0) {
			if (++i == argc) usage();
			server = argv[i];
			for (path=server ; *path!=':' ; path++) {
				if (path == '\0') usage();
			}
			*path = '\0'; /* terminate server */
			path++;
		}
		else if (strcmp(argv[i], "-transp") == 0) {
			if (++i == argc) usage();
			transp = argv[i];
		}
		else if (strcmp(argv[i], "-addload") == 0) {
			if (++i == argc) usage();
			addload = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-workload") == 0) {
			char fn[128], smallbuf[32];
			int fd;
			if (++i == argc) usage();
			snprintf(fn, sizeof(fn), "%s/maxdepth", argv[i]);
			if ((fd = open(fn, O_RDONLY)) < 0) {
				report_error(FATAL, "open(%s)", fn);
				return;
			}
			if (read(fd, smallbuf, sizeof(smallbuf)) < 0) {
				report_error(FATAL, "read");
				return;
			}
			maxdepth = atoi(smallbuf);
			close(fd);
			snprintf(fn, sizeof(fn), "%s/fcnt", argv[i]);
			fcnt_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/fpop", argv[i]);
			fpop_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/fsize", argv[i]);
			fsize_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/dcnt", argv[i]);
			dcnt_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/dpop", argv[i]);
			dpop_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/lcnt", argv[i]);
			lcnt_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/lpop", argv[i]);
			lpop_dist_func = dist_str(fn);
		}
		else if (strcmp(argv[i], "-maxdepth") == 0) {
			if (++i == argc) usage();
			maxdepth = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-fcntdist") == 0) {
			if (++i == argc) usage();
			fcnt_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-fpopdist") == 0) {
			if (++i == argc) usage();
			fpop_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-fsizedist") == 0) {
			if (++i == argc) usage();
			fsize_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-dcntdist") == 0) {
			if (++i == argc) usage();
			dcnt_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-dpopdist") == 0) {
			if (++i == argc) usage();
			dpop_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-lcntdist") == 0) {
			if (++i == argc) usage();
			lcnt_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-lpopdist") == 0) {
			if (++i == argc) usage();
			lpop_dist_func = dist_str(argv[i]);
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
	if (addload <= 0) {
		report_error(NONFATAL, "specify a valid addload");
		usage();
	}
}

int
main(int argc, char *argv[])
{
	struct in_addr addr;
	unsigned char fhdata[64];
	struct fh fh = {0, fhdata};
	int fd, files = 0;

	srandom(time(NULL));

	report_start(argc, argv);
	parse_args(argc, argv);
	
#ifdef IPPROTO_TUF
	if (strcmp(transp, "tuf") == 0) {
		tuf_client = 1;
		transp = "tcp";
	}
#endif

	if (dns_name2addr(server, &addr) != 0) {
		report_error(FATAL, "dns_name2addr(\"%s\", ...) failed", 
			     server);
		return -1;
	}
	if (mount_getroot(addr, path, &fh) != 0) {
		report_error(FATAL, "mount_getroot failed");
		return -1;
	}

	if ((fd = open(ns_file, O_RDONLY)) < 0) {
		report_perror(FATAL, "open(\"%s\")", ns_file);
		return -1;
	}
	if (nameset_load(fd) < 0) {
		report_error(FATAL, "nameset_load failed");
		return -1;
	}
	close(fd);

	if (op_init(addr, strcmp(transp, "udp") == 0 ? 
		    SOCK_DGRAM : SOCK_STREAM) < 0) {
		report_error(FATAL, "op_init failed");
		return -1;
	}

	/* these only apply if the distributions haven't been overridden */
	files = 40 * addload; /* plus 100 for initial set... XXX */
	filesperdir = files / (num_dirs_int + 1);
	
	printf("creating...\n");
	if (createtree(&fh, maxdepth,
		       -1, dcnt_dist_func, dpop_dist_func, 
		       -1, fcnt_dist_func, fpop_dist_func,
		       -1, lcnt_dist_func, lpop_dist_func, 
		       fsize_dist_func, addload) < 0) {
		report_error(FATAL, "createtree failed");
		return -1;
	}
	report_flush();
	printf("done creating files.\n");
		
	if (op_uninit() < 0) {
		report_error(FATAL, "op_uninit failed");
		return -1;
	}
	
	if ((fd = open(ns_file, O_RDWR, 0644)) < 0) {
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
