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
 * generate <object, density> pairs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <math.h>

/*
 * zipf: x^(-a)
 */
static double
zipf(int rank, double alpha)
{
	return pow(rank, -alpha);
}

/*
 * {heavy|fat|long}-tail (pareto): a*k*x^(-(a+1))
 */
static double
pareto(int rank, double alpha, double k)
{
	return alpha * k * pow(rank, -1 - alpha);
}

/*
 * uniform: 0 <= x <= 1
 */
static double
uniform(int rank, int maxrank)
{
	return (double)(rank+1) / (double)maxrank;
}

static void
usage(void)
{
	fprintf(stderr, "distributions:\n");
	fprintf(stderr, "   {-zipf alpha}\n");
	fprintf(stderr, "   {-pareto alpha k}\n");
	fprintf(stderr, "other options\n");
	fprintf(stderr, "   {-cnt n} : number of values generated\n");
	fprintf(stderr, "   {-scale f} : multiply values by f\n");
	fprintf(stderr, "   {-trunc} : truncate values to integers\n");
	fprintf(stderr, "   {-limit f} : truncate values exceeding f\n");
	fprintf(stderr, "   {-weight n} : result weight values (default 1)\n");
	fprintf(stderr, "   {-v} : verbose [list first for most info!]\n");
	exit(1);
}

#define DIST_ZIPF 1
#define DIST_PARETO 2
#define DIST_UNIFORM 3

int
main(int argc, char *argv[])
{
	int i, which = 0, cnt = 100, trunc = 0, verbose = 0, weight = 1;
	float alpha = 0, k = 0, value = 0, scale = 1.0, limit = 0;
	float total = 0, median = -1, min = 0, max = 0;

	for (i=1 ; i<argc ; i++) {
		if (strcmp(argv[i], "-zipf") == 0) {
			which = DIST_ZIPF;
			if (++i >= argc || *argv[i] == '-') usage();
			alpha = atof(argv[i]);
			if (verbose) {
				fprintf(stderr, "zipf(a=%0.2f)\n",
					alpha);
			}
		}
		else if (strcmp(argv[i], "-pareto") == 0) {
			which = DIST_PARETO;
			if (++i >= argc || *argv[i] == '-') usage();
			alpha = atof(argv[i]);
			if (++i >= argc || *argv[i] == '-') usage();
			k = atof(argv[i]);
			if (verbose) {
				fprintf(stderr, "pareto(a=%0.2f, k=%0.2f)\n",
					alpha, k);
			}
		}
		else if (strcmp(argv[i], "-uniform") == 0) {
			which = DIST_UNIFORM;
			if (verbose) {
				fprintf(stderr, "uniform()\n");
			}
		}
		else if (strcmp(argv[i], "-cnt") == 0) {
			if (++i >= argc || *argv[i] == '-') usage();
			cnt = atoi(argv[i]);
			if (verbose) {
				fprintf(stderr, "cnt=%d\n", cnt);
			}
		}
		else if (strcmp(argv[i], "-scale") == 0) {
			if (++i >= argc || *argv[i] == '-') usage();
			scale = atof(argv[i]);
			if (verbose) {
				fprintf(stderr, "scale=%0.2f\n", scale);
			}			
		}
		else if (strcmp(argv[i], "-limit") == 0) {
			if (++i >= argc || *argv[i] == '-') usage();
			limit = atof(argv[i]);
			if (verbose) {
				fprintf(stderr, "limit=%0.2f\n", limit);
			}			
		}
		else if (strcmp(argv[i], "-trunc") == 0) {
			trunc = 1;
			if (verbose) {
				fprintf(stderr, "truncating values\n");
			}			
		}
		else if (strcmp(argv[i], "-weight") == 0) {
			if (++i >= argc || *argv[i] == '-') usage();
			weight = atoi(argv[i]);
			if (verbose) {
				fprintf(stderr, "weight=%d\n", weight);
			}			
		}
		else if (strcmp(argv[i], "-v") == 0) {
			verbose = 1;
			if (verbose) {
				fprintf(stderr, "verbose output\n");
			}			
		}
		else {
			fprintf(stderr, "unknown option \"%s\"\n", argv[i]);
			usage();
		}
	}
	
	for (i=1 ; i<=cnt ; i++) {
		switch (which) {
		case DIST_ZIPF:
			value = zipf(i, alpha);
			break;
		case DIST_PARETO:
			value = pareto(i, alpha, k);
			break;
		case DIST_UNIFORM:
			value = uniform(i, cnt);
			break;
		default:
			usage();
			break;
		}

		/*
		 * scale value.
		 */
		value = value * scale;

		/*
		 * optionally truncate values exceeding limit.
		 */
		if (limit && value > limit) {
			value = limit;
		}

		/*
		 * optionally truncate values to integers.
		 */
		if (trunc) {
			value = (int)value;
		}

		/*
		 * print value with proper decimal format.
		 */
		if (trunc) {
			printf("%d\t%d\n", (int)value, weight);
		} else {
			printf("%0.2f\t%d\n", value, weight);
		}

		/*
		 * statistics: max, median, min, and total (for avg).
		 */
		if (i == 1) {
			max = value;
		}
		if (i == cnt / 2) {
			median = value;
		}
		if (i == cnt) {
			min = value;
		}
		total += value;
	}

	if (verbose) {
		fprintf(stderr, "total = %0.2f\n", total);
		fprintf(stderr, "min = %0.2f (%0.2f%% of total)\n", 
			min, min / total * 100.0);
		fprintf(stderr, "max = %0.2f (%0.2f%% of total)\n", 
			max, max / total * 100.0);
		fprintf(stderr, "average = %0.2f (%0.2f%% of total)\n", 
			total/(float)cnt, (total/(float)cnt) / total * 100.0);
		fprintf(stderr, "median = %0.2f (%0.2f%% of total)\n", 
			median, median / total * 100.0);
	}

	return 0;
}

