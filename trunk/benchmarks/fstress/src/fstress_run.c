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
#include "gen_op.h"
#include "report.h"
#include "measure_op.h"
#include "linger.h"

static char *ns_file = "_nameset";
static char *server = "vmhost";
static char *path = "/tmp";
static char *transp = "udp";
static char *quitfile = NULL;
static int rate = 0;
static int duration = 300;
static int warmup = 300;
static int cooldown = 10;
static int rusage = 0;
static int latency_cutoff = 100;
static int nclients = 0;
static int (*op_dist_func)(void) = op_dist;
int (*rsize_dist_func)(void) = rsize_dist;
int (*wsize_dist_func)(void) = wsize_dist;
extern int limit_op_outstanding;
static int loop = 0;

#ifdef IPPROTO_TUF
extern int tuf_client;
#endif

static void
gen_func(void *arg)
{
	if (gen_op((*op_dist_func)()) < 0) {
		fprintf(stderr, "gen_op failed\n");
		return;
	}
}

static void
usage(void) {
	fprintf(stderr, "usage: fstress_run\n");
	fprintf(stderr, "\t[-nsfile file (default \"%s\")]\n", ns_file);
	fprintf(stderr, "\t[-host server:path (default %s:%s)]\n", server, path);
	fprintf(stderr, "\t[-transp udp|tcp (default %s)]\n", transp);
	fprintf(stderr, "\t[-rate N (default %d)]\n", rate);
	fprintf(stderr, "\t[-warmup N] (default %d)]\n", warmup);
	fprintf(stderr, "\t[-duration N (default %d)]\n", duration);
	fprintf(stderr, "\t[-cooldown N] (default %d)]\n", cooldown);
	fprintf(stderr, "\t[-maxlat N (default %d)]\n", latency_cutoff);
	fprintf(stderr, "\t[-maxios N (default %d)]\n", linger_set_maxdepth(-1));
	fprintf(stderr, "\t[-maxops N (default %d)]\n", limit_op_outstanding);
	fprintf(stderr, "\t[-maxinuse N (default %d)]\n", linger_set_maxinuse(-1));
	fprintf(stderr, "\t[-rexmitmax N] (default %d)]\n", rexmit_max);
	fprintf(stderr, "\t[-rexmitage N] (default %d ms)]\n", rexmit_age);	
	fprintf(stderr, "\t[-workload X (default N/A)]\n");
	fprintf(stderr, "\t[-opdist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-rsizedist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-wsizedist \"v:w v:w ... v:w\" (default SPEC)]\n");
	fprintf(stderr, "\t[-quitfile fname] (default none)\n");
	fprintf(stderr, "\t[-rusage]\n");
	fprintf(stderr, "\t[-nclients N (set by fstress.csh)]\n");
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
		else if (strcmp(argv[i], "-rate") == 0) {
			if (++i == argc) usage();
			rate = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-warmup") == 0) {
			if (++i == argc) usage();
			warmup = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-duration") == 0) {
			if (++i == argc) usage();
			duration = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-cooldown") == 0) {
			if (++i == argc) usage();
			cooldown = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-maxlat") == 0) {
			if (++i == argc) usage();
			latency_cutoff = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-maxios") == 0) {
			if (++i == argc) usage();
			linger_set_maxdepth(atoi(argv[i]));
		}
		else if (strcmp(argv[i], "-maxinuse") == 0) {
			if (++i == argc) usage();
			linger_set_maxinuse(atoi(argv[i]));
		}
		else if (strcmp(argv[i], "-maxops") == 0) {
			if (++i == argc) usage();
			limit_op_outstanding = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-rexmitmax") == 0) {
			if (++i == argc) usage();
			rexmit_max = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-rexmitage") == 0) {
			if (++i == argc) usage();
			rexmit_age = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-workload") == 0) {
			char fn[128];
			if (++i == argc) usage();
			snprintf(fn, sizeof(fn), "%s/op", argv[i]);
			op_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/rsize", argv[i]);
			rsize_dist_func = dist_str(fn);
			snprintf(fn, sizeof(fn), "%s/wsize", argv[i]);
			wsize_dist_func = dist_str(fn);
		}
		else if (strcmp(argv[i], "-opdist") == 0) {
			if (++i == argc) usage();
			op_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-rsizedist") == 0) {
			if (++i == argc) usage();
			rsize_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-wsizedist") == 0) {
			if (++i == argc) usage();
			wsize_dist_func = dist_str(argv[i]);
		}
		else if (strcmp(argv[i], "-quitfile") == 0) {
			if (++i == argc) usage();
			quitfile = argv[i];
			report_set_quitfile(quitfile);
		}
		else if (strcmp(argv[i], "-rusage") == 0) {
			rusage = 1;
		}
		else if (strcmp(argv[i], "-nclients") == 0) {
			if (++i == argc) usage();
			nclients = atoi(argv[i]);
		}
		else if (strcmp(argv[i], "-loop") == 0) {
			if (++i == argc) usage();
			loop = atoi(argv[i]);
		}
		else {
			fprintf(stderr, "error parsing \"%s\"\n", argv[i]);
			usage();
		}
	}
	if (rate <= 0 || duration <= 0) usage();
}

static void
print_statline(char *desc, int seconds)
{
	printf("%s: ", desc);
	printf("wanted %d nfsops, ", rate);
	printf("called %d nfsops, ", measure_op_called() / seconds);
	printf("got %d nfsops, ", measure_op_achieved() / seconds);
	printf("avg %0.2f ms ", (float)measure_op_global_avg()/1000.0);
	printf("nclients %d", nclients);
	printf("\n");
}

int
main(int argc, char *argv[])
{
	struct in_addr addr;
	unsigned char fhdata[64];
	struct fh fh = {0, fhdata};
	int fd, retval = 0;

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

	printf("-----------------------------------------------\n");
	if (warmup) {
		measure_op_resetstats();
		printf("warmup...\n");
		if (op_metronome(rate, warmup, gen_func, NULL) < 0) {
			report_error(FATAL, "op_metronome failed");
			return -1;
		}
		if (op_barrier(0) < 0) {
			report_error(FATAL, "op_barrier failed");
			return -1;
		}
		report_flush();
		printf("done warmup.\n");
		print_statline("WARMUP", warmup);
		printf("-----------------------------------------------\n");
	}

	if (loop) {
		int count = 0;
		while (count < loop) {
			char time[64];
			measure_op_resetstats();
			if (op_metronome(rate, duration, gen_func, NULL) < 0) {
				report_error(FATAL, "op_metronome failed");
				return -1;
			}
			if (op_barrier(0) < 0) {
				report_error(FATAL, "op_barrier failed");
				return -1;
			}
			report_flush();
			sprintf(time, "%d", (loop*duration));
			print_statline(time, duration);
			count++;
		}
	} else {
		measure_op_resetstats();
		printf("generating load...\n");
		if (op_metronome(rate, duration, gen_func, NULL) < 0) {
			report_error(FATAL, "op_metronome failed");
			return -1;
		}
		if (op_barrier(0) < 0) {
			report_error(FATAL, "op_barrier failed");
			return -1;
		}
		report_flush();
		printf("done generating load.\n");

		measure_op_printstats();
		print_statline("SUMMARY", duration);
	}

	if (measure_op_global_avg()/1000 >= latency_cutoff) {
		printf("latency (%0.1f msecs/op) exceeded maxlat (%d), dying.\n",
		       (float)measure_op_global_avg()/1000.0, latency_cutoff);
		retval = 1;
	}

	printf("-----------------------------------------------\n");
	if (cooldown) {
		measure_op_resetstats();
		printf("cooldown...\n");
		if (op_metronome(rate, cooldown, gen_func, NULL) < 0) {
			report_error(FATAL, "op_metronome failed");
			return -1;
		}
		if (op_barrier(0) < 0) {
			report_error(FATAL, "op_barrier failed");
			return -1;
		}
		report_flush();
		printf("done cooldown.\n");
		print_statline("COOLDOWN", cooldown);
		printf("-----------------------------------------------\n");
	}

	if (op_uninit() < 0) {
		report_error(FATAL, "op_uninit failed");
		return -1;
	}

	if ((fd = open(ns_file, O_RDWR)) < 0) {
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

	if (retval != 0) {
		report_error(FATAL, "main returning with error status %d",
			     retval);
		/* don't return here */
	}
	return retval;
}
