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
#include <strings.h>
#include <sys/types.h>
#include <assert.h>
#include <netinet/in.h>

#include <math.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "timer.h"
#include "nameset.h"
#include "operation.h"
#include "nfs.h"
#include "measure_op.h"

#define STRIPING_ZONE_OFFSET (64 * 1024)

static struct {
	int call;
	int rexmit;
	int reply_success;
	int reply_error;
	int reply_cancel;
} stats;

/* ------------------------------------------------------- */
/*
 * Given the formulas (x_i is an input element, cnt elements):
 *
 * avg = sigma(x_i) / cnt,
 * var = sigma((x_i - avg)^2) / (cnt - 1),
 * stddev = sqrt(var)
 */
#ifdef GMP
#include "gmp.h"
#else
#define mpf_t u_int64_t
#define mpf_init(a) a = 0
#define mpf_add_ui(a, b, c) a = b + c
#define mpf_set_ui(a, n) a = n
#define mpf_mul_ui(a, b, n) a = b * n
#define mpf_add(a, b, c) a = b + c
#define mpf_mul(a, b, c) a = b * c
#define mpf_div_ui(a, b, n) a = b / n
#define mpf_sub(a, b, c) a = b -c
#define mpf_sqrt(a, b) a = sqrt(b)
#define mpf_get_d(a) a
#warning "NOT USING GMP: LOSS OF PRECISION AND OVERFLOW LIKELY"
#endif

struct statrec {
	u_int32_t magic;
	mpf_t sum, ssq, tmp;

	int32_t cnt;

	int32_t good, error, rexmit, cancel; /* other counts */

	/* multiresolution: loose resolution as times get larger. */
	int32_t hist_decimsec[100]; /* [0-10) ms */
	int32_t hist_msec[100]; /* [10-100) ms */
	int32_t hist_decamsec[100]; /* [100-1000) ms */
	int32_t hist_hectomsec[100]; /* [1000-10000) ms */
	int32_t hist_other;
};

static void
statrec_reset(struct statrec *sr)
{
		bzero(sr, sizeof *sr);
		sr->magic = 0xcafebabe;

		mpf_init(sr->sum);
		mpf_init(sr->ssq);
		mpf_init(sr->tmp);
		sr->cnt = 0;
}

static void
statrec_add(struct statrec *sr, u_int32_t n, int histogram)
{
	if (sr->magic != 0xcafebabe) {
		statrec_reset(sr);
	}
	assert (n >= 0);

	mpf_add_ui(sr->sum, sr->sum, n);   /* sr->sum += n */

	mpf_set_ui(sr->tmp, n);
	mpf_mul_ui(sr->tmp, sr->tmp, n);
	mpf_add(sr->ssq, sr->ssq, sr->tmp); /* sr->ssq += n*n */

	sr->cnt += 1;

	if (histogram) {
		/* only record in highest resolution bucket */
		
		int decimsec = (n + 50) / 100;
		int msec = (decimsec + 5) / 10;
		int decamsec = (msec + 5) / 10;
		int hectomsec = (decamsec + 5) / 10;

		if (decimsec < 100) {
			sr->hist_decimsec[decimsec]++;
		} else if (msec < 100) {
			sr->hist_msec[msec]++;
		} else if (decamsec < 100) {
			sr->hist_decamsec[decamsec]++;
		} else if (hectomsec > 100) {
			sr->hist_hectomsec[hectomsec]++;
		} else {
			sr->hist_other++;
		}
	}
}

static double
statrec_avg(struct statrec *sr)
{
	if (sr->cnt == 0) {
		return 0;
	}
	mpf_div_ui(sr->tmp, sr->sum, sr->cnt);
	return mpf_get_d(sr->tmp); /* sr->sum / sr->cnt */
}

static double
statrec_stddev(struct statrec *sr)
{
	if (sr->cnt <= 1) {
		return 0;
	}
	mpf_mul(sr->tmp, sr->sum, sr->sum);       /* tmp = sum*sum */
	mpf_div_ui(sr->tmp, sr->tmp, sr->cnt);    /* tmp = sum*sum/cnt */
	mpf_sub(sr->tmp, sr->ssq, sr->tmp);       /* tmp = ssq - sum*sum/cnt */
	mpf_div_ui(sr->tmp, sr->tmp, sr->cnt - 1);/* tmp = [3] / (cnt-1) */
	mpf_sqrt(sr->tmp, sr->tmp);               /* tmp = sqrt([3]/(cnt-1)) */
	return mpf_get_d(sr->tmp); /* sqrt((ssq - sum*sum/cnt) / (cnt - 1)) */
}

/* ------------------------------------------------------- */

#define noINCLUDE_SLICE_THRESHOLD_MEASURES
#ifdef INCLUDE_SLICE_THRESHOLD_MEASURES
#define NUM_NFS_SRS (NFS_NPROCS + 4)
#define NFSPROC_READ_small (NFS_NPROCS + 0)
#define NFSPROC_READ_large (NFS_NPROCS + 1)
#define NFSPROC_WRITE_small (NFS_NPROCS + 2)
#define NFSPROC_WRITE_large (NFS_NPROCS + 3)
#else
#define NUM_NFS_SRS (NFS_NPROCS + 1)
#endif

static struct statrec nfs_srs[NUM_NFS_SRS];

double
measure_op_global_avg(void)
{
	double sum = 0;
	int i, cnt = 0;

	for (i=0 ; i<NFS_NPROCS ; i++) {
		sum += statrec_avg(&nfs_srs[i]) * nfs_srs[i].cnt;
		cnt += nfs_srs[i].cnt;
	}

	return sum/cnt;
}

int
measure_op_called(void)
{
	return stats.call;
}

int
measure_op_achieved(void)
{
	return stats.reply_success + stats.reply_error;
}

void
measure_op_resetstats(void)
{
	int i;

	bzero(&stats, sizeof(stats));
	for (i=0 ; i<NFS_NPROCS ; i++) {
		statrec_reset(&nfs_srs[i]);
	}
}

static double
measure_op_contrib(int proc)
{
	double sum = 0;
	int i;

	for (i=0 ; i<NFS_NPROCS ; i++) {
		sum += statrec_avg(&nfs_srs[i]) * nfs_srs[i].cnt;
	}	
	return (double)
		(statrec_avg(&nfs_srs[proc]) * nfs_srs[proc].cnt) * 100.0 / 
		sum;
}

void
measure_op_printstats(void)
{
	int i, j;

	printf("good\t");
	printf("error\t");
	printf("rexmit\t");
	printf("cancel\t");
	printf("avg\t");
	printf("stddev\t");
	printf("contrib\t");
	printf("name\n");

	for (i=0 ; i<NFS_NPROCS ; i++) {
		printf("%6d\t", nfs_srs[i].good);
		printf("%6d\t", nfs_srs[i].error);
		printf("%6d\t", nfs_srs[i].rexmit);
		printf("%6d\t", nfs_srs[i].cancel);
		printf("%6.2f\t", (float)statrec_avg(&nfs_srs[i])/1000.0);
		printf("%6.2f\t", (float)statrec_stddev(&nfs_srs[i])/1000.0);
		printf("%6.2f\t", (float)measure_op_contrib(i));
		printf("%s\n", nfs_procstr(i));
	}

	printf("\t");
	printf("\t");
	printf("\t");
	printf("\t");
	printf("%6.2f\t", (float)measure_op_global_avg()/1000.0);
	printf("\t");
	printf("\t");
	printf("global average\n");

#ifdef INCLUDE_SLICE_THRESHOLD_MEASURES
	for (i=NFS_NPROCS ; i<NUM_NFS_SRS ; i++) {
		printf("%6d\t", nfs_srs[i].good);
		printf("%6d\t", nfs_srs[i].error);
		printf("%6d\t", nfs_srs[i].rexmit);
		printf("%6d\t", nfs_srs[i].cancel);
		printf("%6.2f\t", (float)statrec_avg(&nfs_srs[i])/1000.0);
		printf("%6.2f\t", (float)statrec_stddev(&nfs_srs[i])/1000.0);
		printf("%6.2f\t", (float)measure_op_contrib(i));
		printf("%s %dK\n", 
		       i == NFSPROC_READ_small ? "read <" :
		       i == NFSPROC_READ_large ? "read >" :
		       i == NFSPROC_WRITE_small ? "write <" :
		       i == NFSPROC_WRITE_large ? "write >" : 
		       "(error)", STRIPING_ZONE_OFFSET/1024);
	}
#endif

#if 1
	printf("histograms (msecs:count ... msecs+:count)\n");
	for (i=0 ; i<NFS_NPROCS ; i++) {
		printf("HIST %s ", nfs_procstr(i));
		/* [0 to 10) ms */
		for (j=0 ; j<100 ; j++) {
			if (nfs_srs[i].hist_decimsec[j]) {
				printf(" %0.1f:%d", (float)j / 10.0, 
				       nfs_srs[i].hist_decimsec[j]);
			}
		}
		/* [10 to 100) ms */
		for (j=10 ; j<100 ; j++) {
			if (nfs_srs[i].hist_msec[j]) {
				printf(" %d:%d", j, 
				       nfs_srs[i].hist_msec[j]);
			}
		}
		/* [100 to 1000) ms */
		for (j=10 ; j<100 ; j++) {
			if (nfs_srs[i].hist_decamsec[j]) {
				printf(" %d:%d", j * 10, 
				       nfs_srs[i].hist_decamsec[j]);
			}
		}
		/* [1000 to 10000) ms */
		for (j=10 ; j<100 ; j++) {
			if (nfs_srs[i].hist_hectomsec[j]) {
				printf(" %d:%d", j * 100, 
				       nfs_srs[i].hist_hectomsec[j]);
			}
		}
		/* [10000+) ms */
		if (nfs_srs[i].hist_other) {
			printf(" %d:%d", 10000, 
			       nfs_srs[i].hist_other);
		}
		printf("\n");
	}
#endif
}

/* ------------------------------------------------------- */

void
measure_op_call(struct outstanding_op *oop, struct nfsmsg *call)
{
	stats.call++;

	oop->measure = 1;
	oop->start = global_timer();
}

void
measure_op_rexmit(struct outstanding_op *oop, struct nfsmsg *call)
{
	stats.rexmit++;
	nfs_srs[call->proc].rexmit++;

	oop->measure = 0; /* do not measure rexmit latency */
}

void
measure_op_reply(struct outstanding_op *oop, struct nfsmsg *call, 
		 int status)
{
	u_int64_t now = global_timer();

	switch (status) {
	case NFS_OK:
		stats.reply_success++;
		break;
	case -1:
		stats.reply_cancel++;
		break;
	default:
		stats.reply_error++;
		break;
	}

	if (oop->measure == 0) {
		return; /* do not measure */
	}

	switch (status) {
	case NFS_OK:
		statrec_add(&nfs_srs[call->proc], now - oop->start, 1);
		nfs_srs[call->proc].good++;
		break;
	case -1:
		nfs_srs[call->proc].cancel++;
		break;
	default:
		statrec_add(&nfs_srs[call->proc], now - oop->start, 0);
		nfs_srs[call->proc].error++;
		break;
	}

#ifdef INCLUDE_SLICE_THRESHOLD_MEASURES
	if (call->proc == NFSPROC_READ || call->proc == NFSPROC_WRITE) {
		int proc2 = -1;
		if (call->proc == NFSPROC_READ) {
			if (call->u.read_arg.offset + 
			    call->u.read_arg.count <
			    STRIPING_ZONE_OFFSET) {
				proc2 = NFSPROC_READ_small;
			} else {
				proc2 = NFSPROC_READ_large;
			}
		}
		if (call->proc == NFSPROC_WRITE) {
			if (call->u.write_arg.offset + 
			    call->u.write_arg.count <
			    STRIPING_ZONE_OFFSET) {
				proc2 = NFSPROC_WRITE_small;
			} else {
				proc2 = NFSPROC_WRITE_large;
			}
		}
		assert(proc2 != -1);
		switch (status) {
		case NFS_OK:
			statrec_add(&nfs_srs[proc2], now - oop->start, 1);
			nfs_srs[proc2].good++;
			break;
		case -1:
			nfs_srs[proc2].cancel++;
			break;
		default:
			statrec_add(&nfs_srs[proc2], now - oop->start, 0);
			nfs_srs[proc2].error++;
			break;
		}
	}	
#endif
}
