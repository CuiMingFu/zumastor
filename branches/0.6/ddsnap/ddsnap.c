#define _GNU_SOURCE // for O_DIRECT

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <linux/fs.h> // for BLKGETSIZE
#include <poll.h>
#include <sys/prctl.h>
#include "dm-ddsnap.h"
#include "buffer.h"
#include "daemonize.h"
#include "ddsnap.h"
#include "ddsnap.agent.h"
#include "delta.h"
#include "diskio.h"
#include "list.h"
#include "sock.h"
#include "trace.h"
#include "build.h"

/* Utilities */

int fd_size(int fd, u64 *bytes)
{
	struct stat stat;
	if (fstat(fd, &stat) == -1)
		return -errno;
	if (S_ISREG(stat.st_mode)) {
		*bytes = stat.st_size;
		return 0;
	}
	if (ioctl(fd, BLKGETSIZE64, bytes))
		return -errno;
	return 0;
}

int is_same_device(char const *dev1,char const *dev2) {
	struct stat stat1, stat2;

	if (stat(dev1, &stat1) < 0) {
		warn("could not stat %s", dev1);
		return -1;
	}

	if (stat(dev2, &stat2) < 0) {
		warn("could not stat %s", dev2);
		return -1;
	}

	if (!S_ISBLK(stat1.st_mode) && !S_ISREG(stat1.st_mode)) {
		fprintf(stderr, "device %s is not a block device\n", dev1);
		return -1;
	}

	if (!S_ISBLK(stat2.st_mode) && !S_ISREG(stat2.st_mode)) {
		fprintf(stderr, "device %s is not a block device\n", dev2);
		return -1;
	}

	if (S_ISBLK(stat1.st_mode) != S_ISBLK(stat2.st_mode))
		return 0;

	if (S_ISREG(stat1.st_mode) && stat1.st_ino != stat2.st_ino)
		return 0;

	if (stat1.st_rdev != stat2.st_rdev)
		return 0;

	warn("device %s is the same as %s\n", dev1, dev2);
	return 1;
}

// FIXME: Overloading *_MAX is bad
#define INPUT_ERROR UINT32_MAX
#define INPUT_ERROR_64 UINT64_MAX

u64 strtobytes64(char const *string)
{
	unsigned long long bytes = 0;
	char *letter = NULL;

	bytes = strtoull(string, &letter, 10);

	if (bytes < 0)
		return INPUT_ERROR_64;

	if (bytes == ULLONG_MAX && errno == ERANGE)
		return INPUT_ERROR_64;

	if (letter[0] == '\0')
		return bytes;

	if (letter[1] != '\0')
		return INPUT_ERROR_64;

	switch (letter[0]) {
	case 'k': case 'K':
		return bytes * (1 << 10);
	case 'm': case 'M':
		return bytes * (1 << 20);
	case 'g': case 'G':
		return bytes * (1 << 30);
	default:
		return INPUT_ERROR_64;
	}
}

u32 strtobytes(char const *string)
{
	unsigned long long bytes = 0;

	bytes = strtobytes64(string);
	if (bytes > ULONG_MAX || bytes == INPUT_ERROR_64)
		return INPUT_ERROR;
	return (u32)bytes;
}

u32 strtobits(char const *string)
{
	long amount = 0;
	u32 bits = 0;
	char *letter = NULL;

	amount = strtol(string, &letter, 10);

	if ((amount <= 0) || (amount & (amount - 1)))
		return INPUT_ERROR;

	while (amount > 1) {
		bits += 1;
		amount >>= 1;
	}

	switch (letter[0]) {
	case '\0':
		break;
	case 'k': case 'K':
		bits += 10;
		break;
	case 'm': case 'M':
		bits += 20;
		break;
	case 'g': case 'G':
		bits += 30;
		break;
	default:
		return INPUT_ERROR;
		break;
	}

	if (letter[1] != '\0')
		return INPUT_ERROR;

	return bits;
}

/* changelist and delta file header info */
#define MAGIC_SIZE 8
#define CHANGELIST_MAGIC_ID "rln"
#define DELTA_MAGIC_ID "jc"
#define MAGIC_NUM 0xbead0023

#define DEFAULT_REPLICATION_PORT 4321
#define TRUE 1
#define FALSE 0

#define XDELTA 1
#define RAW (1 << 1)
#define BEST_COMP (1 << 2)

#define DEF_GZIP_COMP 0
#define MAX_GZIP_COMP 9

#define MAX_MEM_BITS 20
#define MAX_MEM_SIZE (1 << MAX_MEM_BITS)
#define DEFAULT_CHUNK_SIZE_BITS 12
#define DEFAULT_JOURNAL_SIZE (1000 * SECTOR_SIZE)

struct cl_header
{
	char magic[MAGIC_SIZE];
	u32 chunksize_bits;
	u32 src_snap;
	u32 tgt_snap;
};

struct vol_header
{
	char magic[MAGIC_SIZE];
	u64 vol_size_bytes;
	u32 chunksize_bits;
};

struct delta_header
{
	char magic[MAGIC_SIZE];
	u64 chunk_num;
	u32 chunk_size;
	u32 src_snap;
	u32 tgt_snap;
};

struct delta_extent_header
{
	u32 magic_num;
	u32 mode;
	u32 gzip_on;
	u64 extent_addr;
	u64 num_of_chunks;
	u64 extents_delta_length;
	u64 ext1_chksum;
	u64 ext2_chksum;
};

static u64 checksum(const unsigned char *data, u32 data_length)
{
	u64 result = 0;
	u32 i;

	for (i = 0; i < data_length; i++)
		result = result + data[i];

	return result;
}

static char *get_message(int sock, unsigned int length)
{
	char *buffer;
	if (!(buffer = malloc(length)))
		return NULL;

	int err;
	if ((err = readpipe(sock, buffer, length)) < 0) {
		warn("unable to get reason why command failed: %s", strerror(err));
		free(buffer);
		return NULL;
	}
	buffer[length-1] = '\0';

	return buffer;
}

static void error_message_handler(int sock, const char *prefix, unsigned int length)
{
	char *buffer = get_message(sock, length);
	warn("%s : %s", prefix, (buffer? buffer : "no error message"));
	if (buffer)
		free(buffer);
}

static void unknown_message_handler(int sock, struct head *head)
{
	if (head->code != PROTOCOL_ERROR) {
		warn("received unexpected code=%x length=%u", head->code, head->length);
		return;
	}

	/* process protocol error message */
	int err;
	struct protocol_error pe;

	if (head->length < sizeof(struct protocol_error)) {
		warn("message too short");
		return;
	}

	if ((err = readpipe(sock, &pe, sizeof(struct protocol_error))) < 0) {
		warn("unable to receive protocol error message: %s", strerror(-err));
		return;
	}

	if (head->length - sizeof(struct protocol_error) <= 0)
		return;
	error_message_handler(sock, "protocol error message", 
			head->length - sizeof(struct protocol_error));
}

static int create_socket(char const *sockname)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int sock;

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		error("Can't get AF_UNIX socket: %s", strerror(errno));
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	if (connect(sock, (struct sockaddr *)&addr, addr_len) == -1)
		error("Can't connect to control socket %s: %s", sockname, strerror(errno));

	return sock;
}

static int parse_snaptag(char const *snapstr, u32 *snaptag)
{
	if (snapstr[0] == '\0' || snapstr[0] == '-')
		return -1;

	unsigned long num;
	char *endptr;

	num = strtoul(snapstr, &endptr, 10);

	if (*endptr != '\0')
		return -1;

	if (num >= ULONG_MAX)
		return -1;

	*snaptag = num;
	return 0;
}

#define CHUNK_ARRAY_INIT 1024

struct change_list *init_change_list(u32 chunksize_bits, u32 src_snap, u32 tgt_snap)
{
	struct change_list *cl;

	if ((cl = malloc(sizeof(struct change_list))) == NULL)
		return NULL;

	cl->count = 0;
	cl->length = CHUNK_ARRAY_INIT;
	cl->chunksize_bits = chunksize_bits;
	cl->src_snap = src_snap;
	cl->tgt_snap = tgt_snap;
	cl->chunks = malloc(cl->length * sizeof(u64));

	if (cl->chunks == NULL) {
		free(cl);
		return NULL;
	}

	return cl;
}

int append_change_list(struct change_list *cl, u64 chunkaddr)
{
	if ((cl->count + 1) >= cl->length) {
		u64 *newchunks;

		/* we could use a different strategy here like incrementing but this
		 * should work and will avoid having too many
		 * reallocations
		 */
		if ((newchunks = realloc(cl->chunks, cl->length * 2 * sizeof(u64))) == NULL)
		    return -1;

		cl->length *= 2;
		cl->chunks = newchunks;
	}

	cl->chunks[cl->count] = chunkaddr;
	cl->count++;

	return 0;
}

void free_change_list(struct change_list *cl)
{
	if (cl->chunks)
		free(cl->chunks);
	free(cl);
}

static struct change_list *read_changelist(int cl_fd)
{
	struct cl_header clh;

	if (fdread(cl_fd, &clh, sizeof(clh)) < 0) {
		warn("Not a proper changelist file (too short for header)");
		return NULL;
	}

	if (strncmp(clh.magic, CHANGELIST_MAGIC_ID, MAGIC_SIZE) != 0) {
		warn("Not a proper changelist file (wrong magic in header: %s)", clh.magic);
		return NULL;
	}

	struct change_list *cl;

	if ((cl = init_change_list(clh.chunksize_bits, clh.src_snap, clh.tgt_snap)) == NULL)
		return NULL;

	u64 chunkaddr = 0;
	ssize_t len;

	trace_on(printf("reading changelist for snapshots %u and %u from file\n", clh.src_snap, clh.tgt_snap););
	for (;;) {
		len = read(cl_fd, &chunkaddr, sizeof(u64)); /* FIXME: need to handle short read but fdread() doesn't distinguish short read vs. no read EOF */

		if (len == 0)
			break;

		if (len != sizeof(u64)) {
			error("Incomplete chunk address.");
			break;
		}

		if (chunkaddr == -1)
			break;

		if (append_change_list(cl, chunkaddr) < 0)
			error("unable to append chunk address.");
	}

	if (chunkaddr != -1)
		warn("changelist file may be incomplete");

	trace_on(printf("done reading "U64FMT" chunk addresses\n", cl->count););

	return cl;
}

static int write_changelist(int change_fd, struct change_list const *cl)
{
	struct cl_header clh;
	int err;

	strncpy(clh.magic, CHANGELIST_MAGIC_ID, sizeof(clh.magic));
	clh.chunksize_bits = cl->chunksize_bits;
	clh.src_snap = cl->src_snap;
	clh.tgt_snap = cl->tgt_snap;

	if ((err = fdwrite(change_fd, &clh, sizeof(clh))) < 0) {
		warn("unable to write magic information to changelist file: %s", strerror(-err));
		return -1;
	}

	if ((err = fdwrite(change_fd, cl->chunks, cl->count * sizeof(cl->chunks[0]))) < 0) {
		warn("unable to write changelist file: %s", strerror(-err));
		return -1;
	}

	u64 marker = -1;

	if ((err = fdwrite(change_fd, &marker, sizeof(marker))) < 0) {
		warn("unable to write changelist marker: %s", strerror(-err));
		return err;
	}

	return 0;
}

static u64 chunks_in_extent(struct change_list *cl, u64 pos, u32 chunk_size)
{
	u64 start_chunkaddr, cur_chunkaddr, num_of_chunks = 1;
	start_chunkaddr = cl->chunks[pos];
	cur_chunkaddr = cl->chunks[pos+1];

	while (cur_chunkaddr == start_chunkaddr + num_of_chunks && (num_of_chunks < (MAX_MEM_SIZE / chunk_size))) {
		num_of_chunks++;
		cur_chunkaddr = cl->chunks[pos + num_of_chunks];
	}
	return num_of_chunks;
}

static int create_raw_delta(struct delta_extent_header *deh_ptr, const unsigned char *input_buffer, unsigned char *output_buffer, u64 input_size, u64 *output_size) 
{
	memcpy(output_buffer, input_buffer, input_size);
	*output_size = input_size;
	deh_ptr->mode = RAW;
	deh_ptr->extents_delta_length = input_size;
	return 0;
}

static int create_xdelta_delta(struct delta_extent_header *deh_ptr, unsigned char *input_buffer1, unsigned char *input_buffer2, unsigned char *output_buffer, u64 input_size, u64 *output_size)
{
	trace_off(printf("create xdelta delta\n"););
	int err;
	int ret = create_delta_chunk(input_buffer1, input_buffer2, output_buffer, input_size, (int *)output_size);
	deh_ptr->mode = XDELTA;
	deh_ptr->extents_delta_length = *output_size;

	/* If xdelta result is larger than or equal to extent_size, we want to just copy over the raw extent */
	if (ret == BUFFER_SIZE_ERROR || *output_size == input_size) {
		trace_off(printf("extents_delta size is larger than or equal to extent_size\n"););
		create_raw_delta(deh_ptr, input_buffer2, output_buffer, input_size, output_size);
	} else if (ret < 0) {
		goto gen_create_error;
	} else if (ret >= 0) {
		/* sanity test for xdelta creation */
		unsigned char *delta_test = malloc(input_size);
		if (!delta_test)
			goto gen_alloc_error;

		ret = apply_delta_chunk(input_buffer1, delta_test, output_buffer, input_size, *output_size);

		if (ret != input_size) {
			free(delta_test);
			goto gen_applytest_error;
		}

		if (memcmp(delta_test, input_buffer2, input_size) != 0) {
			trace_off(printf("generated delta does not match extent on disk.\n"););
			create_raw_delta(deh_ptr, input_buffer2, output_buffer, input_size, output_size);
		}
		trace_off(printf("able to generate delta\n"););
		free(delta_test);
	}
	return 0;

gen_create_error:
	err = -ERANGE;
	warn("unable to create delta: %s", strerror(-err));
	return err;

gen_alloc_error:
	err = -ENOMEM;
	warn("memory allocation failed: %s", strerror(-err));
	return err;

gen_applytest_error:
	err = -ERANGE;
	warn("test application of delta failed: %s", strerror(-err));
	return err;
}

static int gzip_on_delta(struct delta_extent_header *deh_ptr, unsigned char *input_buffer, unsigned char *output_buffer, u64 input_size, u64 *output_size, int level)
{
	int err;
	*output_size = input_size + 12 + (input_size >> 9);
	int comp_ret = compress2(output_buffer, (unsigned long *) output_size, input_buffer, input_size, level);

	if (comp_ret == Z_MEM_ERROR)
		goto gen_compmem_error;
	if (comp_ret == Z_BUF_ERROR)
		goto gen_compbuf_error;
	if (comp_ret == Z_STREAM_ERROR)
		goto gen_compstream_error;

	if (*output_size < input_size) {
		deh_ptr->gzip_on = TRUE;
		deh_ptr->extents_delta_length = *output_size;
	} else {
		deh_ptr->gzip_on = FALSE;
		deh_ptr->extents_delta_length = input_size;
		memcpy(output_buffer, input_buffer, input_size);
		*output_size = input_size;
	}
	return 0;

gen_compmem_error:
	err = -ENOMEM;
	warn("not enough buffer memory for compression of delta: %s", strerror(-err));
	return err;

gen_compbuf_error:
	err = -ERANGE;
	warn("not enough room in the output buffer for compression of delta: %s", strerror(-err));
	return err;

gen_compstream_error:
	err = -ERANGE;
	warn("invalid compression parameter level=%d delta_size=%Lu in delta: %s", level, input_size, strerror(-err));
	return err;
}

static struct status_reply *generate_status(int serv_fd, u32 snaptag)
{
	int err;
	struct status_reply *reply;

	if ((err = outbead(serv_fd, STATUS, struct status_request, snaptag))) {
		warn("unable to send status request: %s", strerror(-err));
		return NULL;
	}

	struct head head;

	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("could not read message header: %s", strerror(-err));
		return NULL;
	}

	if (head.code != STATUS_OK) {
		if (head.code != STATUS_ERROR) {
			unknown_message_handler(serv_fd, &head);
			return NULL;
		}
		error_message_handler(serv_fd, "server reason why status failed", head.length);
		return NULL;
	}

	if (head.length < sizeof(struct status_reply)) {
		warn("status length mismatch: expected >=%zu, actual %u", sizeof(struct status_reply), head.length);
		return NULL;
	}

	if (!(reply = malloc(head.length))) {
		warn("unable to allocate %Lu bytes for reply buffer", (llu_t) head.length);
		return NULL;
	}

	/* We won't bother to check that the lengths match because it would
	 * be ugly to read in the structure in pieces.
	 */
	if ((err = readpipe(serv_fd, reply, head.length)) < 0) {
		warn("received incomplete status message: %s", strerror(-err));
		free(reply);
		return NULL;
	}

	return reply;
}

/* returns the current time in seconds since the epoch */
int now(void) {
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec;
}

static char *malloc_snapshot_name(const char *devstem, u32 id)
{
	char *snapshot_name = NULL;
	int length = strlen(devstem) + 32; // !!! why 32?

	if (!(snapshot_name = malloc(length))) {
		warn("unable to allocate memory snapshot name");
		return NULL;
	}

	snprintf(snapshot_name, length, "%s(%Lu)", devstem, (llu_t) id);

	return snapshot_name;
}

static int generate_progress_file(const char *progress_file, char **tmpfilename)
{
	char *progress_tmpfile;
	int progress_len = strlen(progress_file);
	if (!(progress_tmpfile = malloc(progress_len + 5))) {
		warn("unable to allocate progress tempfile name");
		return -ENOMEM;
	}
	strncpy(progress_tmpfile, progress_file, progress_len);
	strncpy(progress_tmpfile + progress_len, ".tmp", 5);
	*tmpfilename = progress_tmpfile;
	return 0;
}

static int write_progress(const char *progress_file, const char *progress_tmpfile, u64 chunk_num, u64 chunk_count, u64 extent_addr, u32 tgt_snap)
{
	FILE *progress_fs;
	if ((progress_fs = fopen(progress_tmpfile, "w")) == NULL) {
		warn("unable to open progress temp file %s: %s", progress_tmpfile, strerror(errno));
		return -1;
	}
	if (fprintf(progress_fs, "%u %Lu/%Lu %Lu\n", tgt_snap, chunk_num, chunk_count, extent_addr) < 0) {
		warn("unable write to progress temp file %s: %s", progress_tmpfile, strerror(errno));
		fclose(progress_fs);
		return -1;
	}
	fclose(progress_fs);
	if (rename(progress_tmpfile, progress_file) < 0) {
		warn("unable to rename progress file: %s", strerror(errno));
		return -1;
	}
	return 0;
}

static int generate_delta_extents(u32 mode, int level, struct change_list *cl, int deltafile, char const *devstem, u32 src_snap, u32 tgt_snap, char const *progress_file, u64 start_chunk)
{
	int fullvolume = (src_snap == -1);
	char *dev1name = NULL, *dev2name = NULL, *progress_tmpfile = NULL;
	int snapdev1 = -1, snapdev2 = -1;
	int err = -ENOMEM;
	unsigned char *dev1_extent   = malloc(MAX_MEM_SIZE);
	unsigned char *dev2_extent   = malloc(MAX_MEM_SIZE);
	unsigned char *extents_delta = malloc(MAX_MEM_SIZE);
	unsigned char *gzip_delta    = malloc(MAX_MEM_SIZE + 12 + (MAX_MEM_SIZE >> 9));
	unsigned char *dev2_gzip_extent = malloc(MAX_MEM_SIZE + 12 + (MAX_MEM_SIZE >> 9));

	if (!fullvolume && (!(dev1name = malloc_snapshot_name(devstem, src_snap)) || ((snapdev1 = open(dev1name, O_RDONLY)) < 0))) {
		warn("unable to open source snapshot: %s", strerror(errno));
		goto out;
	}
	if (!(dev2name = malloc_snapshot_name(devstem, tgt_snap)) || ((snapdev2 = open(dev2name, O_RDONLY)) < 0)) {
		warn("unable to open target snapshot: %s", strerror(errno));
		goto out;
	}

	if (!dev1_extent || !dev2_extent || !extents_delta || !gzip_delta || !dev2_gzip_extent) {
		warn("variable memory allocation failed: %s", strerror(-err));
		goto out;
	}

	u64 dev2_gzip_size;
	struct delta_extent_header deh = { .magic_num = MAGIC_NUM };
	u64 extent_addr, chunk_num, num_of_chunks, volume_size;
	u64 extent_size, delta_size, gzip_size = MAX_MEM_SIZE, bytes_total = 0, bytes_sent = 0;
	u32 chunk_size = 1 << cl->chunksize_bits;
	struct delta_extent_header deh2 = { .magic_num = MAGIC_NUM, .mode = RAW };

	trace_off(printf("dev1name: %s, dev2name: %s\n", dev1name, dev2name););
	trace_off(printf("level: %d, chunksize bits: %Lu, chunk_count: %Lu\n", level, (llu_t) cl->chunksize_bits, (llu_t) cl->count););
	trace_off(printf("starting delta generation, mode %u, chunksize %u\n", mode, chunk_size););

	if (fd_size(snapdev2, &volume_size) < 0)
		goto failed_fdsize;
	if (progress_file && (err = generate_progress_file(progress_file, &progress_tmpfile)))
		goto out;

	int current_time, last_update = 0, start_time = now();

	for (chunk_num = start_chunk; chunk_num < cl->count;) {
		if (fullvolume) {
			extent_size = chunk_size;
			extent_addr = chunk_num * extent_size;
			num_of_chunks = 1;
		} else {
			extent_addr = cl->chunks[chunk_num] << cl->chunksize_bits;
			if (chunk_num == (cl->count - 1) )
				num_of_chunks = 1;
			else
				num_of_chunks = chunks_in_extent(cl, chunk_num, chunk_size);
			extent_size = chunk_size * num_of_chunks;
		}
		if (extent_addr > volume_size - extent_size)
			extent_size = volume_size - extent_addr;
		bytes_total += extent_size;

		/* read in extents from dev1 & dev2 */
		if (!fullvolume && (err = diskread(snapdev1, dev1_extent, extent_size, extent_addr)) < 0) {
			warn("read from snapshot device \"%s\" failed ", dev1name);
			goto error_source;
		}
		if ((err = diskread(snapdev2, dev2_extent, extent_size, extent_addr)) < 0) {
			warn("read from snapshot device \"%s\" failed ", dev2name);
			goto error_source;
		}

		/* delta extent header set-up*/
		deh.gzip_on = FALSE;
		deh.extent_addr = extent_addr;
		deh.num_of_chunks = num_of_chunks;
		deh.ext1_chksum = checksum((const unsigned char *) dev1_extent, extent_size);
		deh.ext2_chksum = checksum((const unsigned char *) dev2_extent, extent_size);

		if (fullvolume) {
			deh.extents_delta_length = extent_size;
			deh.mode = RAW;
			//memcpy(gzip_delta, dev2_extent, extent_size);
			if ((err = gzip_on_delta(&deh, dev2_extent, gzip_delta, extent_size, &gzip_size, level)) < 0)
				goto error_source;
		} else {
			/* Three different modes, raw, xdelta, best (either gzipped raw or gzipped xdelta)
			 * Always use raw when senind the first chunk we are resuming on */
			if (mode == RAW)
				err = create_raw_delta (&deh, dev2_extent, extents_delta, extent_size, &delta_size);
			else // compute xdelta for XDELTA or BEST_COMP mode
				err = create_xdelta_delta (&deh, dev1_extent, dev2_extent, extents_delta, extent_size, &delta_size);
			if ((err < 0) || ((err = gzip_on_delta(&deh, extents_delta, gzip_delta, delta_size, &gzip_size, level)) < 0))
				goto error_source;

			if (mode == BEST_COMP) {
				/* delta extent header set-up for dev2_extent */
				deh2.gzip_on = FALSE;
				deh2.extents_delta_length = extent_size;
				if ((err = gzip_on_delta(&deh2, dev2_extent, dev2_gzip_extent, extent_size, &dev2_gzip_size, level)) < 0)
					goto error_source;
				if (dev2_gzip_size <= gzip_size) {
					deh.mode = deh2.mode;
					deh.gzip_on = deh2.gzip_on;
					deh.extents_delta_length = deh2.extents_delta_length;
					memcpy(gzip_delta, dev2_gzip_extent, dev2_gzip_size);
				}
			}
		}

		/* write the delta extent header and extents_delta to the delta file*/
		if ((err = fdwrite(deltafile, &deh, sizeof(deh))) < 0) {
			warn("unable to write delta header ");
			goto error_source;
		}
		if ((err = fdwrite(deltafile, gzip_delta, deh.extents_delta_length)) < 0) {
			warn("unable to write delta data ");
			goto error_source;
		}
		bytes_sent += deh.extents_delta_length + sizeof(deh);

		if (progress_file && (((current_time = now()) - last_update) > 0)) {
			if (write_progress(progress_file, progress_tmpfile, chunk_num, cl->count, extent_addr, tgt_snap) < 0)
				goto out;
			last_update = current_time;
		}

		chunk_num = chunk_num + num_of_chunks;
	}

	/* Make sure everything in changelist was properly transmitted */
	if (chunk_num != cl->count) {
		warn("changelist was not fully transmitted");
		err = -ERANGE;
	} else {
		current_time = now();
		warn("Total chunks %Lu (%Lu bytes), wrote %Lu bytes in %i seconds", chunk_num, bytes_total, bytes_sent, current_time - start_time);
		err = progress_file ? write_progress(progress_file, progress_tmpfile, chunk_num, cl->count, extent_addr, tgt_snap) : 0;
	}
	goto out;
failed_fdsize:
	warn("unable to determine volume size for %s", dev2name);
	goto out;
error_source:
	warn("for "U64FMT" chunk extent starting at offset "U64FMT": %s", num_of_chunks, extent_addr, strerror(-err));
out:
	if (snapdev1 >= 0)
		close(snapdev1);
	if (snapdev2 >= 0)
		close(snapdev2);
	if (dev1_extent)
		free(dev1_extent);
	if (dev2_extent)
		free(dev2_extent);
	if (extents_delta)
		free(extents_delta);
	if (gzip_delta)
		free(gzip_delta);
	if (dev2_gzip_extent)
		free(dev2_gzip_extent);
	if (progress_tmpfile)
		free(progress_tmpfile);
	return err;
}

static int generate_delta(u32 mode, int level, struct change_list *cl, int deltafile, char const *devstem)
{
	/* Delta header set-up */
	struct delta_header dh;

	strncpy(dh.magic, DELTA_MAGIC_ID, sizeof(dh.magic));
	dh.chunk_num = cl->count;
	dh.chunk_size = 1 << cl->chunksize_bits;
	dh.src_snap = cl->src_snap;
	dh.tgt_snap = cl->tgt_snap;

	trace_off(fprintf(stderr, "writing delta file with chunk_num=%Lu chunk_size=%Lu mode=%Lu\n", (llu_t) dh.chunk_num, (llu_t) dh.chunk_size, (llu_t) mode););

	int err;
	if ((err = fdwrite(deltafile, &dh, sizeof(dh))) < 0)
		return err;

	return generate_delta_extents(mode, level, cl, deltafile, devstem, dh.src_snap, dh.tgt_snap, NULL, 0);
}

static int ddsnap_generate_delta(u32 mode, int level, char const *changelistname, char const *deltaname, char const *devstem)
{
	int clfile = open(changelistname, O_RDONLY);
	if (clfile < 0) {
		warn("could not open changelist file \"%s\" for reading: %s", changelistname, strerror(errno));
		return 1;
	}

	struct change_list *cl;

	if ((cl = read_changelist(clfile)) == NULL) {
		warn("unable to parse changelist file \"%s\"", changelistname);
		close(clfile);
		return 1;
	}

	close(clfile);

	int deltafile = open(deltaname, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
	if (deltafile < 0) {
		warn("could not create delta file \"%s\": %s", deltaname, strerror(errno));
		free_change_list(cl);
		return 1;
	}

	if (generate_delta(mode, level, cl, deltafile, devstem) < 0) {
		warn("could not write delta file \"%s\"", deltaname);
		close(deltafile);
		free_change_list(cl);
		return 1;
	}

	close(deltafile);
	free_change_list(cl);

	return 0;
}

static struct change_list *stream_changelist(int serv_fd, u32 src_snap, u32 tgt_snap)
{
	int err;

	if ((err = outbead(serv_fd, STREAM_CHANGELIST, struct stream_changelist, src_snap, tgt_snap))) {
		warn("could not request change list: %s", strerror(-err));
		return NULL;
	}

	struct head head;

	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("could not read change list message head: %s", strerror(-err));
		return NULL;
	}

	if (head.code != STREAM_CHANGELIST_OK) {
		warn("unable to obtain changelist between snapshot %Lu and %Lu", (llu_t) src_snap, (llu_t) tgt_snap);
		if (head.code != STREAM_CHANGELIST_ERROR) {
			unknown_message_handler(serv_fd, &head);
			return NULL;
		}
		error_message_handler(serv_fd, "downstream server reason why send delta failed",
				head.length);
		return NULL;
	}

	struct changelist_stream cl_head;

	if (head.length != sizeof(cl_head)) {
		warn("change list length mismatch: expected %zu, actual %Lu", sizeof(cl_head), (llu_t) head.length);
		return NULL;
	}

	if ((err = readpipe(serv_fd, &cl_head, sizeof(cl_head))) < 0) {
		warn("could not read change list head: %s", strerror(-err));
		return NULL;
	}

	struct change_list *cl;

	if ((cl = malloc(sizeof(struct change_list))) == NULL) {
		/* FIXME: need to read the data anyway to clear the socket */
		warn("unable to allocate change list");
		return NULL;
	}

	cl->count = cl_head.chunk_count;
	cl->chunksize_bits = cl_head.chunksize_bits;
	cl->src_snap = src_snap;
	cl->tgt_snap = tgt_snap;

	if (cl->chunksize_bits == 0) {
		/* FIXME: need to read the data anyway to clear the socket */
		warn("invalid chunk size %Lu in REPLY_STREAM_CHANGE_LIST", (llu_t) cl->chunksize_bits);
		free(cl);
		return NULL;
	}

	if (cl->count == 0) {
		cl->chunks = NULL;
		return cl;
	}

	if ((cl->chunks = malloc(cl->count * sizeof(cl->chunks[0]))) == NULL) {
		/* FIXME: need to read the data anyway to clear the socket */
		warn("unable to allocate change list chunks");
		free(cl);
		return NULL;
	}

	trace_off(printf("reading %Lu chunk addresses (%Lu bits) from ddsnapd\n", (llu_t) cl->count, (llu_t) cl->chunksize_bits););
	if ((err = readpipe(serv_fd, cl->chunks, cl->count * sizeof(cl->chunks[0]))) < 0) {
		warn("unable to read change list chunks: %s", strerror(-err));
		free(cl->chunks);
		free(cl);
		return NULL;
	}

	return cl;
}

static u64 get_snapshot_sectors(int serv_fd, u32 snaptag)
{
	int err;

	if ((err = outbead(serv_fd, REQUEST_SNAPSHOT_SECTORS, struct status_request, snaptag))) {
		warn("unable to send request for snapshot sector count: %s", strerror(-err));
		return 0;
	}

	struct head head;

	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("received incomplete packet header: %s", strerror(-err));
		return 0;
	}

	if (head.code != SNAPSHOT_SECTORS) {
		if (head.code != STATUS_ERROR)
			unknown_message_handler(serv_fd, &head);
		else
			error_message_handler(serv_fd, "server reason why snapshot_sectors failed", head.length);
		return 0;
	}

	if (head.length != sizeof(struct snapshot_sectors)) {
		warn("snapshot_sectors length mismatch: expected >=%zu, actual %u", sizeof(struct snapshot_sectors), head.length);
		return 0;
	}

	struct snapshot_sectors reply;

	if ((err = readpipe(serv_fd, &reply, head.length)) < 0) {
		warn("received incomplete snapshot_sectors message: %s", strerror(-err));
		return 0;
	}

	return reply.count;
}

static int ddsnap_replication_send(int serv_fd, u32 src_snap, u32 tgt_snap, char const *devstem, u32 mode, int level, int ds_fd, char const *progress_file, u64 start_addr)
{
	int fullvolume = (src_snap == -1), err = -ENOMEM;
	u64 skip_chunks = 0;
	struct change_list *cl = NULL;

	/* setup changelist */
	if (fullvolume) {
		if ((cl = malloc(sizeof(struct change_list))) == NULL) {
			warn("unable to allocate change list");
			goto out;
		}

		err = -EINVAL;
		struct status_reply *reply;
		if (!(reply = generate_status(serv_fd, ~((u32)0U)))) {
			warn("cannot generate status");
			goto out;
		}
		cl->chunksize_bits = reply->meta.chunksize_bits;
		free(reply);

		u64 vol_size_bytes = get_snapshot_sectors(serv_fd, (u32)~0UL) * 512, chunkmask = ~(-1 << cl->chunksize_bits);
		cl->count = (vol_size_bytes >> cl->chunksize_bits) + !!(vol_size_bytes & chunkmask);
		cl->length = CHUNK_ARRAY_INIT;
		cl->src_snap = src_snap;
		cl->tgt_snap = tgt_snap;
		cl->chunks = NULL;
		skip_chunks = cl->count - ((vol_size_bytes - start_addr) >> cl->chunksize_bits);
	} else {
		trace_off(printf("requesting changelist from snapshot %Lu to %Lu\n", (llu_t) src_snap, (llu_t) tgt_snap););
		if ((cl = stream_changelist(serv_fd, src_snap, tgt_snap)) == NULL) {
			warn("could not receive change list for snapshots %Lu and %Lu", (llu_t) src_snap, (llu_t) tgt_snap);
			err = -EINVAL;
			goto out;
		}
		/* If resuming, find the first chunk that starts after the start_addr, TODO binary search */
		while (skip_chunks < cl->count && cl->chunks[skip_chunks] << cl->chunksize_bits < start_addr)
			skip_chunks++;
	}

	/* request approval for delta send */
	if ((err = outbead(ds_fd, SEND_DELTA, struct delta_header, DELTA_MAGIC_ID, cl->count - skip_chunks, 1 << cl->chunksize_bits, src_snap, tgt_snap))) {
		warn("unable to send delta: %s", strerror(-err));
		goto out;
	}

	struct head head;
	if ((err = readpipe(ds_fd, &head, sizeof(head))) < 0) {
		warn("unable to read response from downstream: %s", strerror(-err));
		goto out;
	}

	if (head.code != SEND_DELTA_PROCEED) {
		if (head.code != SEND_DELTA_ERROR)
			unknown_message_handler(ds_fd, &head);
		else
			error_message_handler(ds_fd, "downstream server reason why send delta failed",
				head.length);
		err = -EPIPE;
		goto out;
	}

	warn("sending delta from %i to %i", src_snap, tgt_snap);

	/* stream delta */
	if ((err = generate_delta_extents(mode, level, cl, ds_fd, devstem, src_snap, tgt_snap, progress_file, skip_chunks)) < 0) {
		warn("could not send delta downstream for snapshots %i and %i", src_snap, tgt_snap);
		goto out;
	}
	if ((err = readpipe(ds_fd, &head, sizeof(head))) < 0) {
		warn("unable to read response from downstream: %s", strerror(-err));
		goto out;
	}

	if (head.code != SEND_DELTA_DONE) {
		if (head.code != SEND_DELTA_ERROR)
			unknown_message_handler(ds_fd, &head);
		else
			error_message_handler(ds_fd, "downstream server reason why send delta failed",
				head.length);
		err = -EPIPE;
		goto out;
	}
	err = 0;
out:
	if (cl)
		free_change_list(cl);
	return err;
}

static int apply_delta_extents(int deltafile, u32 chunk_size, u64 chunk_count, char const *dev1name, char const *dev2name, char const *progress_file, u32 tgt_snap)
{
	int fullvolume = !dev1name;
	int snapdev1, snapdev2;
	int err;

	/* if an extent is being applied */
	if (!fullvolume && ((snapdev1 = open(dev1name, O_RDONLY)) < 0)) {
		warn("could not open snapdev file \"%s\" for reading: %s.", dev1name, strerror(-err));
		return -errno;
	}

	if ((snapdev2 = open(dev2name, O_WRONLY)) < 0) {
		warn("could not open snapdev file \"%s\" for writing: %s.", dev2name, strerror(-err));
		if (!fullvolume)
			close(snapdev1);
		return -errno;
	}

	char *progress_tmpfile = NULL;
	if (progress_file && (err = generate_progress_file(progress_file, &progress_tmpfile)))
		goto out;

	unsigned char *updated=NULL, *extent_data=NULL, *delta_data=NULL, *comp_delta=NULL;
	char *up_extent1=NULL, *up_extent2=NULL;

	if (!(updated = malloc(MAX_MEM_SIZE)) || !(extent_data = malloc(MAX_MEM_SIZE)) \
		|| !(delta_data = malloc(MAX_MEM_SIZE)) || !(comp_delta = malloc(MAX_MEM_SIZE)) \
		|| !(up_extent1 = malloc(MAX_MEM_SIZE)) || !(up_extent2 = malloc(MAX_MEM_SIZE))) {
		warn("memory allocation failed: %s", strerror(errno));
		err = -ENOMEM;
		goto out;
	}

	struct delta_extent_header deh;
	u64 uncomp_size, extent_size, volume_size;
	u64 extent_addr = 0, chunk_num;
	int current_time, last_update = 0;

	if (fd_size(snapdev2, &volume_size) < 0)
		goto failed_fdsize;

	for (chunk_num = 0; chunk_num < chunk_count;) {
		trace_off(printf("reading chunk "U64FMT" header\n", chunk_num););
		if ((err = fdread(deltafile, &deh, sizeof(deh))) < 0)
			goto apply_headerread_error;
		if (deh.magic_num != MAGIC_NUM)
			goto apply_magic_error;

		extent_addr = deh.extent_addr;
		extent_size = deh.num_of_chunks * chunk_size;
		if (extent_addr > volume_size - extent_size) /* end chunk and volume not a multiple of chunksize */
			extent_size = volume_size - extent_addr;
		uncomp_size = extent_size;

		if (!fullvolume && ((err = diskread(snapdev1, extent_data, extent_size, extent_addr)) < 0))
			goto apply_devread_error;
		/* check to see if the checksum of snap0 is the same on upstream and downstream */
		if (!fullvolume && deh.ext1_chksum != checksum((const unsigned char *)extent_data, extent_size)) {
			warn("delta header checksum '%lld', actual checksum '%lld'", deh.ext1_chksum, checksum((const unsigned char *)extent_data, extent_size));
			goto apply_checksum_error_snap0;
		}

		/* gzip compression was used on extent */
		if (deh.gzip_on == TRUE) {
			if ((err = fdread(deltafile, comp_delta, deh.extents_delta_length)) < 0)
				goto apply_deltaread_error;
			trace_off(printf("data was compressed\n"););
			/* zlib decompression */
			int comp_ret = uncompress(delta_data, (unsigned long *) &uncomp_size, comp_delta, deh.extents_delta_length);
			if (comp_ret == Z_MEM_ERROR)
				goto apply_compmem_error;
			if (comp_ret == Z_BUF_ERROR)
				goto apply_compbuf_error;
			if (comp_ret == Z_DATA_ERROR)
				goto apply_compdata_error;
		} else {
			if ((err = fdread(deltafile, delta_data, deh.extents_delta_length)) < 0)
				goto apply_deltaread_error;
			uncomp_size = deh.extents_delta_length;
		}

		//if the mode is RAW or XDELTA RAW then copy the delta file directly to updated (no uncompression)
		if (deh.mode == RAW)
			memcpy(updated, delta_data, extent_size); // !!! FIXME TODO - this has to go, bogus data copy
		if (!fullvolume && deh.mode == XDELTA) {
			trace_off(warn("read %llx chunk delta extent data starting at chunk "U64FMT"/offset "U64FMT" from \"%s\"", deh.num_of_chunks, chunk_num, extent_addr, dev1name););
			int apply_ret = apply_delta_chunk(extent_data, updated, delta_data, extent_size, uncomp_size);
			trace_off(warn("apply_ret %d\n", apply_ret););
			if (apply_ret < 0)
				goto apply_chunk_error;
		}

		if (deh.ext2_chksum != checksum((const unsigned char *)updated, extent_size))  {
			warn("deh chksum %lld, checksum %lld", deh.ext2_chksum, checksum((const unsigned char *)updated, extent_size));
			goto apply_checksum_error;
		}
		trace_off(warn("dev2name %s, extent_size %lld, extent_addr %lld", dev2name, extent_size, extent_addr););
		if ((err = diskwrite(snapdev2, updated, extent_size, extent_addr)) < 0)
			goto apply_write_error;

		if (progress_file && (((current_time = now()) - last_update) > 0)) {
			if (fsync(snapdev2))
				goto out;
			if (write_progress(progress_file, progress_tmpfile, chunk_num, chunk_count, extent_addr, tgt_snap) < 0)
				goto out;
			last_update = current_time;
		}

		chunk_num = chunk_num + deh.num_of_chunks;
	}
	trace_on(warn("All extents applied to %s\n", dev2name););
	if (fsync(snapdev2))
		goto out;
	err = progress_file ? write_progress(progress_file, progress_tmpfile, chunk_num, chunk_count, extent_addr, tgt_snap) : 0;
	goto out;

	/* error messages */
failed_fdsize:
	warn("unable to determine volume size for %s", dev2name);
	goto out;
apply_headerread_error:
	warn("could not read header for extent starting at chunk "U64FMT" of "U64FMT" total chunks: %s", chunk_num, chunk_count, strerror(-err));
	goto out;

apply_magic_error:
	err = -ERANGE;
	warn("wrong magic in header for extent starting at chunk "U64FMT" of "U64FMT" total chunks", chunk_num, chunk_count);
	goto out;

apply_deltaread_error:
	warn("could not properly read delta data for extent at offset "U64FMT": %s", extent_addr, strerror(-err));
	goto out;

apply_devread_error:
	warn("could not read "U64FMT" chunk extent at offset "U64FMT" from downstream snapshot device \"%s\": %s", deh.num_of_chunks, extent_addr, dev1name, strerror(-err));
	goto out;

apply_compmem_error:
	warn("not enough buffer memory for decompression of delta for "U64FMT" chunk extent starting at offset "U64FMT, deh.num_of_chunks, extent_addr);
	goto out;

apply_compbuf_error:
	warn("not enough room in the output buffer for decompression of delta for "U64FMT" chunk extent starting at offset "U64FMT, deh.num_of_chunks, extent_addr);
	goto out;

apply_compdata_error:
	warn("compressed data corrupted in delta for "U64FMT" chunk extent starting at offset "U64FMT, deh.num_of_chunks, extent_addr);
	goto out;

apply_chunk_error:
	err = -ERANGE; /* FIXME: find better error */
	warn("delta could not be applied for "U64FMT" chunk extent with start address of "U64FMT, deh.num_of_chunks, extent_addr);
	goto out;

apply_checksum_error_snap0:
	err = -ERANGE;
	warn("checksum failed for "U64FMT" chunk extent with start address of "U64FMT" snapshot0 is not the same on the upstream and the downstream", deh.num_of_chunks, extent_addr);
	goto out;

apply_checksum_error:
	err = -ERANGE;
	warn("checksum failed for "U64FMT" chunk extent with start address of "U64FMT, deh.num_of_chunks, extent_addr);
	goto out;

apply_write_error:
	warn("updated extent could not be written at start address "U64FMT" in snapshot device \"%s\": %s", extent_addr, dev2name, strerror(-err));

out:
	if (up_extent2)
		free(up_extent2);
	if (up_extent1)
		free(up_extent1);
	if (comp_delta)
		free(comp_delta);
	if (extent_data)
		free(extent_data);
	if (delta_data)
		free(delta_data);
	if (updated)
		free(updated);
	close(snapdev2);
	if (!fullvolume)
		close(snapdev1);
	return err;
}

static int apply_delta(int deltafile, char const *devstem)
{
	struct delta_header dh;

	if (fdread(deltafile, &dh, sizeof(dh)) < 0) {
		warn("not a proper delta file (too short)");
		return -1; /* FIXME: use named error */
	}

	/* Make sure it's a proper delta file */
	if (strncmp(dh.magic, DELTA_MAGIC_ID, MAGIC_SIZE) != 0) {
		warn("not a proper delta file (wrong magic in header)");
		return -1; /* FIXME: use named error */
	}

	if (dh.chunk_size == 0) {
		warn("not a proper delta file (zero chunk size)");
		return -1; /* FIXME: use named error */
	}

	char *dev1name = NULL;
	int err, fullvolume = (dh.src_snap == ~0U);

	/* check to see if the replication is full volume replication (dh.src_snap == -1)
	 * or replication via snapshot. In case of full volume replication, pass NULL dev1name
	 * to apply_delta_extents */
	if (!fullvolume && !(dev1name = malloc_snapshot_name(devstem, dh.src_snap))) {
		warn("unable to allocate memory for dev1name");
		return -ENOMEM;
	}

	if ((err = apply_delta_extents(deltafile, dh.chunk_size, dh.chunk_num, dev1name, devstem, NULL, dh.tgt_snap)) < 0) {
		if (dev1name)
			free(dev1name);
		return err;
	}

	if (!fullvolume)
		free(dev1name);

	return 0;
}

static int ddsnap_apply_delta(char const *deltaname, char const *devstem)
{
	int deltafile;

	deltafile = open(deltaname, O_RDONLY);
	if (deltafile < 0) {
		warn("could not open delta file \"%s\" for reading", deltaname);
		return 1;
	}

	if (apply_delta(deltafile, devstem) < 0) {
		warn("could not apply delta file \"%s\" to origin device \"%s\"", deltaname, devstem);
		close(deltafile);
		return 1;
	}

	char test;

	if (read(deltafile, &test, 1) == 1)
		warn("extra data at end of delta file \"%s\"", deltaname);

	close(deltafile);

	return 0;
}

static int list_snapshots(int serv_fd, int verbose, int onlylast)
{
	int err;

	if ((err = outbead(serv_fd, LIST_SNAPSHOTS, struct create_snapshot, 0))) {
		warn("unable to request snaphot list: %s", strerror(-err));
		return 1;
	}

	struct head head;

	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("unable to read snaphot list head: %s", strerror(-err));
		return 1;
	}

	if (head.code != SNAPSHOT_LIST) {
		unknown_message_handler(serv_fd, &head);
		return 1;
	}

	if (head.length < sizeof(int32_t)) {
		warn("snapshot list reply length mismatch: expected >=%zu, actual %Lu", sizeof(int32_t), (llu_t) head.length);
		return 1;
	}

	int count;

	if ((err = readpipe(serv_fd, &count, sizeof(int))) < 0) {
		warn("unable to read snaphot list count: %s", strerror(-err));
		return 1;
	}

	if (head.length != sizeof(int32_t) + count * sizeof(struct snapinfo)) {
		warn("snapshot list reply length mismatch: expected %zu, actual %Lu", sizeof(int32_t) + count * sizeof(struct snapinfo), (llu_t) head.length);
		return 1;
	}

	struct snapinfo *buffer = malloc(count * sizeof(struct snapinfo));

	if (!buffer) {
		warn("unable to allocate snapshot list buffer");
		return 1;
	}

	if ((err = readpipe(serv_fd, buffer, count * sizeof(struct snapinfo))) < 0) {
		warn("unable to read snaphot list: %s", strerror(-err));
		free(buffer);
		return 1;
	}

	if (verbose)
		printf("Snapshot list:\n");

	int i;

	for (i = ((onlylast && count > 0) ? count - 1 : 0); i < count; i++) {
		if (verbose) {
			printf("Snapshot %d:\n", i);
			printf("\ttag= %u \t", buffer[i].snap);
			printf("priority= %d \t", (int)buffer[i].prio);
			printf("use count= %u \t", (unsigned int)buffer[i].usecnt);

			time_t snap_time = (time_t)buffer[i].ctime;
			char *ctime_str = ctime(&snap_time);
			if (ctime_str[strlen(ctime_str)-1] == '\n')
				ctime_str[strlen(ctime_str)-1] = '\0';

			printf("creation time= %s\n", ctime_str);
		} else {
			printf("%u ", buffer[i].snap);
		}
	}

	if (!verbose)
		printf("\n");

	free(buffer);

	return 0;
}

static int ddsnap_generate_changelist(int serv_fd, char const *changelist_filename, u32 src_snap, u32 tgt_snap)
{
	int change_fd = open(changelist_filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);

	if (change_fd < 0) {
		warn("unable to open changelist file %s for writing: %s", changelist_filename, strerror(errno));
		return 1;
	}

	struct change_list *cl;

	if ((cl = stream_changelist(serv_fd, src_snap, tgt_snap)) == NULL) {
		warn("could not generate change list between snapshots %u and %u", src_snap, tgt_snap);
		return 1;
	}

	int err = write_changelist(change_fd, cl);
	free_change_list(cl);

	close(change_fd);

	if (err < 0)
		return 1;

	return 0;
}

static int delete_snapshot(int sock, u32 snaptag)
{
	int err;

	if ((err = outbead(sock, DELETE_SNAPSHOT, struct create_snapshot, snaptag)) < 0) { /* FIXME: why create_snapshot? */
		warn("unable to send delete snapshot message: %s", strerror(-err));
		return 1;
	}

	struct head head;
	if ((err = readpipe(sock, &head, sizeof(head))) < 0) {
		warn("unable to read delete snapshot message head: %s", strerror(-err));
		return 1;
	}

	if (head.code != DELETE_SNAPSHOT_OK) {
		if (head.code == DELETE_SNAPSHOT_ERROR)
			warn("snapshot server is unable to delete snapshot %u", snaptag);
		else
			unknown_message_handler(sock, &head);
		return 1;
	}
	return 0;
}

static int create_snapshot(int sock, u32 snaptag)
{
	int err;

	trace_on(printf("sending snapshot create request %u\n", snaptag););

	if ((err = outbead(sock, CREATE_SNAPSHOT, struct create_snapshot, snaptag)) < 0) {
		warn("unable to send create snapshot message: %s", strerror(-err));
		return 1;
	}

	struct head head;
	if ((err = readpipe(sock, &head, sizeof(head))) < 0) {
		warn("unable to read create snapshot message head: %s", strerror(-err));
		return 1;
	}

	if (head.code != CREATE_SNAPSHOT_OK) {
		if (head.code == CREATE_SNAPSHOT_ERROR)
			warn("snapshot server is unable to create snapshot %u", snaptag);
		else
			unknown_message_handler(sock, &head);
		return 1;
	}
	return 0;
}

static int set_priority(int sock, u32 snaptag, int8_t prio_val)
{
	int err;

	if ((err = outbead(sock, PRIORITY, struct priority_info, snaptag, prio_val)) < 0) {
		warn("unable to send set priority message: %s", strerror(-err));
		return 1;
	}

	struct head head;
	if ((err = readpipe(sock, &head, sizeof(head))) < 0) {
		warn("unable to read priority message head: %s", strerror(-err));
		return 1;
	}

	if (head.code != PRIORITY_OK) {
		if (head.code != PRIORITY_ERROR) {
			unknown_message_handler(sock, &head);
			return 1;
		}
		struct priority_error *prio_err =
			(struct priority_error *)get_message(sock, head.length);
		warn("snapshot server is unable to set priority for snapshot %u", snaptag);
		if (!prio_err || head.length == sizeof(struct priority_error))
			return 1;
		warn("server reason for priority failure: %s", prio_err->msg);
		free(prio_err);
		return 1;
	}

	return 0;
}

static int usecount(int sock, u32 snaptag, int32_t usecnt_dev)
{
	int err;

	if ((err = outbead(sock, USECOUNT, struct usecount_info, snaptag, usecnt_dev)) < 0) {
		warn("unable to send usecount message: %s", strerror(-err));
		return 1;
	}

	struct head head;
	unsigned maxbuf = 500;
	char buf[maxbuf];

	/* read message header */
	if ((err = readpipe(sock, &head, sizeof(head))) < 0) {
		warn("unable to read usecount message head: %s", strerror(-err));
		return 1;
	}

	/* check for error before reading message body */
	if (head.code != USECOUNT_OK) {
		if (head.code != USECOUNT_ERROR) {
			unknown_message_handler(sock, &head);
			return 1;
		}
		struct generic_error *usecnt_err = (void *)get_message(sock, head.length);
		warn("snapshot server is unable to set usecount for snapshot %u", snaptag);
		if (!usecnt_err || head.length == sizeof(struct generic_error))
			return 1;
		warn("server reason for usecount failure: %s", usecnt_err->msg);
		free(usecnt_err);
		return 1;
	}

	assert(head.length < maxbuf); // !!! don't die

	/* read message body */
	if ((err = readpipe(sock, buf, head.length)) < 0) {
		warn("unable to read usecount message body: %s", strerror(-err));
		return 1;
	}
	printf("%u\n", (unsigned int)((struct usecount_ok *)buf)->usecount);

	return 0; // should we return usecount here too?
}

static int ddsnap_delta_server(int lsock, char const *devstem, const char *progress_file, char const *logfile, int getsigfd)
{
	char const *origindev = devstem;
        struct pollfd pollvec[1];
	struct sigaction sigact = { .sa_handler = sighandler, .sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESETHAND) };
	/* FIXME: keep list of child pids if we want to support multiple children */
	pid_t pid = -1;  /* delta server has a single child only now */

        pollvec[0] = (struct pollfd){ .fd = getsigfd, .events = POLLIN };
	if (sigprocmask(0, NULL, &sigact.sa_mask))  /* get the current signal mask */
		error("fail to set signal mask");

	for (;;) {
		int csock;

		if ((poll(pollvec, 1, 0) < 0) && (errno != EINTR))
			error("poll failed, %s", strerror(errno));
                if (pollvec[0].revents) {
                        u8 sig = 0;
                        /* it's stupid but this read also gets interrupted, so... */
                        do { } while (read(getsigfd, &sig, 1) == -1 && errno == EINTR);
                        trace_on(warn("Caught signal %i", sig););
                        switch (sig) {
                                case SIGHUP:
                                        fflush(stderr);
                                        fflush(stdout);
                                        re_open_logfile(logfile);
                                        break;
				case SIGTERM:
				case SIGINT:
					close(csock);
					if (pid > 0)
						kill(pid, SIGKILL);
					exit(0);
				case SIGCHLD:
					pid = -1; /* delta server has a single child only now */
					break;
				default:
					break;
                        }
                }

		if ((csock = accept_socket(lsock)) < 0) {
			warn("unable to accept connection: %s", strerror(-csock));
			continue;
		}

		trace_on(fprintf(stderr, "got client connection\n"););

		assert(pid == -1); /* delta server has a single child only now */
		sigaction(SIGCHLD, &sigact, NULL); /* monitor child exits */
		if ((pid = fork()) < 0) {
			warn("unable to fork to service connection: %s", strerror(errno));
			goto cleanup_client;
		}

		if (pid != 0) {
			/* parent -- wait for another connection */
			goto cleanup_client;
		}

		trace_on(fprintf(stderr, "processing\n"););

		/* child */

		struct messagebuf message;
		int err;
		char err_msg[MAX_ERRMSG_SIZE];
		err_msg[0] = '\0';

		if ((err = readpipe(csock, &message.head, sizeof(message.head))) < 0) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "error reading upstream message header: %s", strerror(-err));
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			goto end_connection;
		}
		if (message.head.length > maxbody) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "message body too long %u", message.head.length);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			goto end_connection;
		}
		if ((err = readpipe(csock, &message.body, message.head.length)) < 0) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "error reading upstream message body: %s", strerror(-err));
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			goto end_connection;
		}

		struct delta_header body;

		switch (message.head.code) {
		case SEND_DELTA:
			if (message.head.length < sizeof(body)) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "incomplete SEND_DELTA request sent by client: length %u, size %zu", message.head.length, sizeof(body));
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				goto end_connection;
			}

			memcpy(&body, message.body, sizeof(body));

			if (body.chunk_size == 0) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "invalid chunk size %u in SEND_DELTA", body.chunk_size);
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				goto end_connection;
			}

			char *src_snapdev = NULL;
			if ((body.src_snap != (u32)~0UL) && !(src_snapdev = malloc_snapshot_name(devstem, body.src_snap))) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "unable to allocate device name");
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				goto end_connection;
			}

			/* FIXME: verify snapshot exists */

			/* FIXME: In the future we should also lookup the client's address in a
			 * device permission table and check for replicatiosn already in progress.
			 */

			if (outbead(csock, SEND_DELTA_PROCEED, struct {}) < 0) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "unable to send delta proceed message to server");
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				if (src_snapdev)
					free(src_snapdev);
				goto end_connection;
			}

			/* retrieve it */

			if (apply_delta_extents(csock, body.chunk_size,
						body.chunk_num, src_snapdev, origindev, progress_file, body.tgt_snap) < 0) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "unable to apply upstream delta to device \"%s\"", origindev);
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				if (src_snapdev)
					free(src_snapdev);
				goto end_connection;
			}

			if (src_snapdev)
				free(src_snapdev);

			/* success */

			if (outbead(csock, SEND_DELTA_DONE, struct {}) < 0) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "unable to send delta complete message to server");
				err_msg[MAX_ERRMSG_SIZE-1] = '\0';
				goto end_connection;
			}
			trace_on(fprintf(stderr, "applied streamed delta to \"%s\", closing connection\n", origindev););
			close(csock);
			exit(0);

		default:
			snprintf(err_msg, MAX_ERRMSG_SIZE,
					"unexpected message type sent to snapshot replication server %x", message.head.code);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			goto end_connection;
		}

	end_connection:
		warn("closing connection on error: %s", err_msg);
		if (outhead(csock, SEND_DELTA_ERROR, strlen(err_msg)+1) < 0 ||
				writepipe(csock, err_msg, strlen(err_msg)+1) < 0)
			warn("unable to send delta error message to upstream server");
		close(csock);
		exit(1);

	cleanup_client:
		close(csock);
	}

	return 0;
}

static u32 get_state(int serv_fd, u32 snaptag)
{
	int err;

	if ((err = outbead(serv_fd, REQUEST_SNAPSHOT_STATE, struct status_request, snaptag))) {
		warn("unable to send state request: %s", strerror(-err));
		return 9;
	}

	struct head head;

	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("received incomplete packet header: %s", strerror(-err));
		return 9;
	}

	if (head.code != SNAPSHOT_STATE) {
		unknown_message_handler(serv_fd, &head);
		return 9;
	}

	if (head.length != sizeof(struct state_message)) {
		warn("state length mismatch: expected >=%zu, actual %u", sizeof(struct state_message), head.length);
		return 9;
	}

	struct state_message reply;

	if ((err = readpipe(serv_fd, &reply, head.length)) < 0) {
		warn("received incomplete state message: %s", strerror(-err));
		return 9;
	}

	return reply.state;
}

int commas(char *buf, int len, long long n)
{
	int newlen = snprintf(buf, len, "%Lu", n);
	if (!newlen)
		return 0;
	int need = (newlen - 1) / 3;
	if ((newlen += need) >= len)
		return 0;
	char *to = buf + newlen, *from = to - need;
	*to = 0;
	while (need) {
		*--to = *--from;
		*--to = *--from;
		*--to = *--from;
		*--to = ',';
		need--;
	}
	return newlen;
}

static int ddsnap_get_status(int serv_fd, u32 snaptag, int verbose)
{
	struct status_reply *reply = generate_status(serv_fd, snaptag);
	if (!reply)
		return 1;
	int separate = !!reply->store.total;
	char number1[27], number2[27];
	unsigned snapshots = reply->snapshots;

	commas(number1, sizeof(number1), reply->meta.total);
	commas(number2, sizeof(number2), reply->meta.free);
	printf(separate? "Snapshot metadata " : "Snapshot store ");
	printf("block size = %lu; ", 1UL << reply->meta.chunksize_bits);
	printf("%s of ", number2);
	printf("%s chunks free\n", number1);

	if (separate) {
		commas(number1, sizeof(number1), reply->store.total);
		commas(number2, sizeof(number2), reply->store.free);
		printf("Snapshot data store ");
		printf("chunk size = %lu; ", 1UL << reply->store.chunksize_bits);
		printf("%s of ", number2);
		printf("%s chunks free\n", number1);
	}

	commas(number1, sizeof(number1), get_snapshot_sectors(serv_fd, (u32)~0UL) << SECTOR_BITS); // include this in status reply!!
	printf("Origin size: %s bytes\n", number1);
	printf("Write density: %g\n", (double)reply->write_density/(double)0xffffffff);

	time_t snaptime = (time_t)reply->ctime;
	char *ctime_str = ctime(&snaptime);
	if (ctime_str[strlen(ctime_str)-1] == '\n')
		ctime_str[strlen(ctime_str)-1] = '\0';
	printf("Creation time: %s\n", ctime_str);

	unsigned int row, col;
	u64 total_chunks;

	printf("%6s %24s %6s %4s %8s %8s", "Snap", "Creation time", "Usecnt", "Prio", "Chunks", "Unshared");

	if (verbose) {
		for (col = 1; col < snapshots; col++)
			printf(" %7dX", col);
	} else {
		printf(" %8s", "Shared");
	}

	printf("\n");

	for (row = 0; row < snapshots; row++) {
		struct snapshot_details *details = snapshot_details(reply, row, snapshots);

		if (snaptag != (u32)~0UL && details->snapinfo.snap != snaptag)
			continue;

		printf("%6u", details->snapinfo.snap);

		snaptime = (time_t)details->snapinfo.ctime;
		ctime_str = ctime(&snaptime);
		if (ctime_str[strlen(ctime_str)-1] == '\n')
			ctime_str[strlen(ctime_str)-1] = '\0';
		printf(" %24s %6u %4i", ctime_str, details->snapinfo.usecnt, details->snapinfo.prio);

		if (details->sharing[0] == -1) {
			printf(" %8s %8s %8s\n", "!", "!", "!");
			continue;
		}

		total_chunks = 0;
		for (col = 0; col < snapshots; col++)
			total_chunks += details->sharing[col];

		printf(" %8llu", (llu_t) total_chunks);
		printf(" %8llu", (llu_t) details->sharing[0]);

		if (verbose)
			for (col = 1; col < snapshots; col++)
				printf(" %8llu", (llu_t) details->sharing[col]);
		else
			printf(" %8llu", (llu_t)(total_chunks - details->sharing[0]));

		printf("\n");
	}

	/* if we are printing all the snapshot stats, also print totals for each
	 * degree of sharing
	 */

	if (snaptag == (u32)~0UL) {
		u64 *column_totals;

		if (!(column_totals = malloc(sizeof(u64) * snapshots))) {
			warn("unable to allocate array for column totals");
			free(reply);
			return 1;
		}

		/* sum the columns and divide by their share counts */

		total_chunks = 0;
		for (col = 0; col < snapshots; col++) {
			column_totals[col] = 0;
			for (row = 0; row < snapshots; row++) {
				struct snapshot_details *details = snapshot_details(reply, row, snapshots);
				if (details->sharing[0] == -1)
					continue;
				column_totals[col] += details->sharing[col];
			}
			column_totals[col] /= col+1;

			total_chunks += column_totals[col];
		}

		printf("%6s %45llu", "totals", total_chunks);
		if (snapshots > 0)
			printf(" %8llu", column_totals[0]);
		else
			printf(" %8d", 0);

		if (verbose)
			for (col = 1; col < snapshots; col++)
				printf(" %8llu", column_totals[col]);
		else if (snapshots > 0)
			printf(" %8llu", total_chunks - column_totals[0]);
		else
			printf(" %8d", 0);

		printf("\n");

		free(column_totals);
	}

	free(reply);

	return 0;
}

static int ddsnap_resize_devices(int serv_fd, u64 orgsize, u64 snapsize, u64 metasize)
{
	int err;
	if ((err = outbead(serv_fd, RESIZE, struct resize_request, orgsize, snapsize, metasize))) {
		warn("unable to send resize request: %s", strerror(-err));
		return -1;
	}
	struct head head;
	if ((err = readpipe(serv_fd, &head, sizeof(head))) < 0) {
		warn("received incomplete packet header: %s", strerror(-err));
		return -1;
	}
	if (head.code != RESIZE) {
		if (head.code == STATUS_ERROR)
			error_message_handler(serv_fd, "server reason why resizing failed", head.length);
		else
			unknown_message_handler(serv_fd, &head);
		return -1;
	}
	if (head.length != sizeof(struct resize_request)) {
		warn("state length mismatch: expected >=%zu, actual %u", sizeof(struct resize_request), head.length);
		return -1;
	}
	struct resize_request reply;
	if ((err = readpipe(serv_fd, &reply, head.length)) < 0) {
		warn("received incomplete resize message: %s", strerror(-err));
		return -1;
	}
	printf("Device sizes after resizing:\n\t");
	printf("origin device %Lu\n\t", reply.orgsize);
	printf("snapshot device %Lu\n\t", reply.snapsize);
	printf("metadata device %Lu\n", reply.metasize);

	if ((orgsize && reply.orgsize != orgsize) || (snapsize && reply.snapsize != snapsize) || (metasize && reply.metasize != metasize)) {
		warn("Can not resize devices to the specified value: orgsize %Lu snapsize %Lu metasize %Lu!!", orgsize, snapsize, metasize);
		return -1;
	}

	return 0;
}

static void mainUsage(void)
{
	printf("usage: ddsnap [-?|--help|--usage|--version] <subcommand>\n"
	       "\n"
	       "Available subcommands:\n"
	       "        initialize        Initialize snapshot storage device\n"
	       "        agent             Start the snapshot agent\n"
	       "        server            Start the snapshot server\n"
	       "	create            Create a snapshot\n"
	       "	delete            Delete a snapshot\n"
	       "	list              Return list of snapshots currently held\n"
	       "	priority          Set the priority of a snapshot\n"
	       "	usecount          Change the use count of a snapshot\n"
	       "        status            Report snapshot usage statistics\n"
	       "	vol               \n"
               "        usage: ddsnap vol [-?|--help|--usage] <subcommand>\n"
               "\n"
               "        Available vol subcommands:\n"
	       "	create            Create a volume delta file\n"
	       "	apply             Apply a volume file to a downstream volume\n"
	       "	send              Send a volume file to a downstream server\n"
	       "        listen            Listen for a volume file arriving from upstream\n"
	       "	delta             \n"
               "        usage: ddsnap delta [-?|--help|--usage] <subcommand>\n"
               "\n"
               "        Available delta subcommands:\n"
               "                changelist        Create a changelist given 2 snapshots\n"
	       "	        create            Create a delta file given a changelist and 2 snapshots\n"
	       "	        apply             Apply a delta file to a volume\n"
	       "	        send              Send a delta file to a downstream server\n"
	       "	        listen            Listen for a delta arriving from upstream\n");
}

static void deltaUsage(void)
{
	printf("usage: ddsnap delta [-?|--help|--usage] <subcommand>\n"
	       "\n"
               "Available delta subcommands:\n"
               "        changelist        Create a changelist given 2 snapshots\n"
	       "	create            Create a delta file given a changelist and 2 snapshots\n"
	       "	apply             Apply a delta file to a volume\n"
	       "	send              Send a delta file to a downstream server\n"
	       "        listen            Listen for a delta arriving from upstream\n");
}

static void cdUsage(poptContext optCon, int exitcode, char const *error, char const *addl)
{
	poptPrintUsage(optCon, stderr, 0);
	if (error) fprintf(stderr, "%s: %s", error, addl);
	exit(exitcode);
}

#ifdef DDSNAP_MEM_MONITOR
int mmon_interval = DDSNAP_MEM_MONITOR;
#endif

#define DEVMAP_PATH "/dev/mapper"
int main(int argc, char *argv[])
{
	char const *command;

	struct poptOption noOptions[] = {
		POPT_TABLEEND
	};

	char *js_str = NULL, *bs_str = NULL, *cs_str = NULL;
	int yes = FALSE;
	struct poptOption initOptions[] = {
		{ "yes", 'y', POPT_ARG_NONE, &yes, 0, "Answer yes to all prompts", NULL},
		{ "journalsize", 'j', POPT_ARG_STRING, &js_str, 0, "User specified journal size, i.e. 400k (default: 100 * chunk_size)", "size" },
		{ "blocksize", 'b', POPT_ARG_STRING, &bs_str, 0, "Snapshot metadata block size (power of two, default = 4K)", "size" },
		{ "chunksize", 'c', POPT_ARG_STRING, &cs_str, 0, "Snapshot store chunk size (power of two, default = 16K)", "size" },
		POPT_TABLEEND
	};

	int nobg = 0;
	char const *logfile = NULL;
	char const *pidfile = NULL;
	char const *progress_file = NULL;
	char const *resume = NULL;
	char const *cachesize_str = NULL;
	struct poptOption serverOptions[] = {
		{ "foreground", 'f', POPT_ARG_NONE, &nobg, 0, "run in foreground. daemonized by default.", NULL }, // !!! unusual semantics, we should be foreground by default, and optionally daemonize
		{ "logfile", 'l', POPT_ARG_STRING, &logfile, 0, "use specified log file", NULL },
		{ "cachesize", 'k', POPT_ARG_STRING, &cachesize_str, 0, "Buffer cache size (default = max(128M,1/4 sys RAM)", "size" },
#ifdef DDSNAP_MEM_MONITOR
		{ "mmonitor", 'm', POPT_ARG_INT, &mmon_interval, 0, "Memory monitor delay, seconds, zero to disable.", NULL },
#endif
		{ "pidfile", 'p', POPT_ARG_STRING, &pidfile, 0, "use specified process id file", NULL },
		{ "progress", 'o', POPT_ARG_STRING, &progress_file, 0, "Output progress to specified file", NULL },
		POPT_TABLEEND
	};

	int xd = FALSE, raw = FALSE, best_comp = FALSE, gzip_level = DEF_GZIP_COMP;
	struct poptOption cdOptions[] = {
		{ "xdelta", 'x', POPT_ARG_NONE, &xd, 0, "Delta file format: xdelta chunk", NULL },
		{ "raw", 'r', POPT_ARG_NONE, &raw, 0, "Delta file format: raw chunk from later snapshot", NULL },
		{ "best", 'b', POPT_ARG_NONE, &best_comp, 0, "Delta file format: best compression (slowest)", NULL},
		{ "gzip", 'g', POPT_ARG_INT, &gzip_level, 0, "Compression via gzip", "compression_level"},
		{ "progress", 'p', POPT_ARG_STRING, &progress_file, 0, "Output progress to specified file", NULL },
		{ "resume", 's', POPT_ARG_STRING, &resume, 0, "Resume from specified address", NULL },
		POPT_TABLEEND
	};

	int last = FALSE;
	int list = FALSE;
	int size = FALSE;
	int state = FALSE;
	int verb = FALSE;
	struct poptOption stOptions[] = {
		{ "last", '\0', POPT_ARG_NONE, &last, 0, "List the newest snapshot", NULL},
		{ "list", 'l', POPT_ARG_NONE, &list, 0, "List all active snapshots", NULL},
		{ "size", 's', POPT_ARG_NONE, &size, 0, "Print the size of the origin device", NULL},
		{ "state", 'S', POPT_ARG_NONE, &state, 0,
			"Return the state of snapshot after exit. Output: 0: normal; 1: not exist; 2: squashed; 9: other error", NULL},
		{ "verbose", 'v', POPT_ARG_NONE, &verb, 0, "Verbose sharing information", NULL},
		POPT_TABLEEND
	};

	struct poptOption deltaOptions[] = {
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Create changelist\n\t Function: Create a changelist given 2 snapshots\n\t Usage: delta changelist <sockname> <changelist> <snapshot1> <snapshot2>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &cdOptions, 0,
		  "Create delta\n\t Function: Create a delta file given a changelist and 2 snapshots\n\t Usage: delta create [OPTION...] <changelist> <deltafile> <devstem>\n", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Apply delta\n\t Function: Apply a delta file to a volume\n\t Usage: delta apply <deltafile> <devstem>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &cdOptions, 0,
		  "Send delta\n\t Function: Send a delta file to a downstream server\n\t Usage: delta send [OPTION...] <sockname> <snapshot1> <snapshot2> <devstem> <remsnapshot> <host>[:<port>]\n", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0,
		  "Listen\n\t Function: Listen for a delta arriving from upstream\n\t Usage: delta listen [OPTION...] <devstem> [<host>[:<port>]]", NULL },
		POPT_TABLEEND
	};

	char *orgsize_str = NULL, *snapsize_str = NULL, *metasize_str = NULL;
	struct poptOption resizeOptions[] = {
		{ "origin", 'o', POPT_ARG_STRING, &orgsize_str, 0, "New origin device size", "size" },
		{ "snapshot", 's', POPT_ARG_STRING, &snapsize_str, 0, "New snapshot device size", "size" },
		{ "metadata", 'm', POPT_ARG_STRING, &metasize_str, 0, "New metadata device size", "size" },
		POPT_TABLEEND
	};

	poptContext mainCon;
	struct poptOption mainOptions[] = {
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &initOptions, 0,
		  "Initialize\n\t Function: Initialize a snapshot storage volume\n\t Usage: initialize [OPTION...] <dev/snapshot> <dev/origin> [dev/meta]", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0,
		  "Agent Server\n\t Function: Start the snapshot agent\n\t Usage: agent [OPTION...] <agent_socket>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0,
		  "Snapshot Server\n\t Function: Start the snapshot server\n\t Usage: server [OPTION...] <dev/snapshot> <dev/origin> [dev/meta] <agent_socket> <server_socket>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Create snapshot\n\t Function: Create a snapshot\n\t Usage: create <sockname> <snapshot>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Delete snapshot\n\t Function: Delete a snapshot\n\t Usage: delete <sockname> <snapshot>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "List snapshots\n\t Function: Return list of snapshots currently held\n\t Usage: list <sockname>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Priority\n\t Function: Set the priority of a snapshot\n\t Usage: priority <sockname> <snap_tag> <new_priority_value>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &noOptions, 0,
		  "Usecount\n\t Function: Change the use count of a snapshot\n\t Usage: usecount <sockname> <snap_tag> <diff_amount>", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &stOptions, 0,
		  "Get statistics\n\t Function: Report snapshot usage statistics\n\t Usage: status [OPTION...] <sockname> [<snapshot>]", NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &deltaOptions, 0,
		  "Delta\n\t Usage: delta [OPTION...] <subcommand> ", NULL},
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &initOptions, 0,
		  "Resize\n\t Function: Change origin/snapshot/metadata device size\n\t Usage: resize [OPTION...] <sockname>", NULL },
		{ "version", 'V', POPT_ARG_NONE, NULL, 0, "Show version", NULL },
		POPT_AUTOHELP
		POPT_TABLEEND
	};
	mainCon = poptGetContext(NULL, argc, (const char **)argv, mainOptions, 0);

	if (argc < 2) {
		poptPrintHelp(mainCon, stdout, 0);
		poptFreeContext(mainCon);
		exit(1);
	}

	command = argv[1];

	if (strcmp(command, "--help") == 0 || strcmp(command, "-?") == 0) {
		poptPrintHelp(mainCon, stdout, 0);
		poptFreeContext(mainCon);
		exit(0);
	}

	poptFreeContext(mainCon);

	if (strcmp(command, "--version") == 0 || strcmp(command, "-V") == 0) {
		printf("ddsnap revision \"%s\"\n", SVN_VERSION);
		printf(" built on %s by %s@%s\n", BUILD_DATE, BUILD_USER, BUILD_HOST);
		exit(0);
	}

	if (strcmp(command, "--usage") == 0) {
		mainUsage();
		exit(0);
	}

	char process_name[16];
	snprintf(process_name, 16, "ddsnap_%s", command);
	prctl(PR_SET_NAME, process_name);
	if (!strcmp(command, "dump")) {
		int err = 1;
		u64 start = 0, finish = -1; /* if not specified, the defaults dump the entire tree */
		if (argc == 5) {
			char *endptr;
			start = strtoull(argv[3], &endptr, 10);
			if (*endptr == '\0')
				finish = strtoull(argv[4], &endptr, 10);
			if (*endptr != '\0')
				fprintf(stderr, "%s %s: invalid chunk specified\n", argv[0], argv[1]);
			else
				err = 0;
		}
		if ((argc != 3) && err) {
			printf("Usage: %s dump <socket> [<start> <end>]\n", argv[0]);
			exit(1);
		}
		int sock = create_socket(argv[2]);
		if ((err = outbead(sock, DUMP_TREE_RANGE, struct dump_tree_range, start, finish))) {
			warn("unable to send dump tree request: %s", strerror(-err));
			exit(1);
		}
		exit(0);
	}

	if (strcmp(command, "initialize") == 0) {
		char const *snapdev, *origdev, *metadev;
		u32 bs_bits = DEFAULT_CHUNK_SIZE_BITS;
		u32 cs_bits = DEFAULT_CHUNK_SIZE_BITS;
		u32 js_bytes = DEFAULT_JOURNAL_SIZE;

		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &initOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		poptContext initCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(initCon, "<dev/snapshot> <dev/origin> [dev/meta]");

		char initOpt = poptGetNextOpt(initCon);

		if (argc < 3) {
			poptPrintUsage(initCon, stderr, 0);
			exit(1);
		}

		if (initOpt < -1) {
			fprintf(stderr, "%s: %s: %s\n", command, poptBadOption(initCon, POPT_BADOPTION_NOALIAS), poptStrerror(initOpt));
			poptFreeContext(initCon);
			return 1;
		}

		snapdev = poptGetArg(initCon);
		if (!snapdev) {
			fprintf(stderr, "%s: snapshot device must be specified\n", command);
			poptPrintUsage(initCon, stderr, 0);
			poptFreeContext(initCon);
			return 1;
		}

		origdev = poptGetArg(initCon);
		if (!origdev) {
			fprintf(stderr, "%s: origin device must be specified\n", command);
			poptPrintUsage(initCon, stderr, 0);
			poptFreeContext(initCon);
			return 1;
		}

		metadev = poptGetArg(initCon); /* ok if NULL */

		trace_off(printf("snapdev is %s, origdev is %s, metadev is %s.\n", snapdev, origdev, metadev););

		if (is_same_device(snapdev, origdev)) {
			poptPrintUsage(initCon, stderr, 0);
			poptFreeContext(initCon);
			return 1;
		}

		if (bs_str && cs_str && (strcmp(bs_str, cs_str) != 0)) {

			if (!metadev) {
				fprintf(stderr, "%s: metadata device must be specified\n", command);
				poptPrintUsage(initCon, stderr, 0);
				poptFreeContext(initCon);
				return 1;
			}

			if (is_same_device(snapdev, metadev)) {
				poptPrintUsage(initCon, stderr, 0);
				poptFreeContext(initCon);
				return 1;
			}

			if (is_same_device(origdev, metadev)) {
				poptPrintUsage(initCon, stderr, 0);
				poptFreeContext(initCon);
				return 1;
			}
		}

		if (poptPeekArg(initCon) != NULL) {
			fprintf(stderr, "%s: too many arguments\n", command);
			poptPrintUsage(initCon, stderr, 0);
			poptFreeContext(initCon);
			return 1;
		}

		int orgdev_, snapdev_, metadev_;
		if ((snapdev_ = open(snapdev, O_RDWR | O_DIRECT)) == -1)
			error("Could not open snapshot store %s: %s", snapdev, strerror(errno));

		if ((orgdev_ = open(origdev, O_RDONLY | O_DIRECT)) == -1)
			error("Could not open origin volume %s: %s", origdev, strerror(errno));

		metadev_ = snapdev_;

		if (metadev && (metadev_ = open(metadev, O_RDWR | O_DIRECT)) == -1)
			error("Could not open meta volume %s: %s", metadev, strerror(errno));

		if (!yes && sniff_snapstore(metadev_) > 0) {
			printf("There exists a valid magic number in sb, are you sure you want to overwrite? (y/N) ");
			if (toupper(getchar()) != 'Y')
				return 1;
		}

		poptFreeContext(initCon);

		trace_off(warn("js_bytes was %u, bs_bits was %u and cs_bits was %u", js_bytes, bs_bits, cs_bits););

		if (bs_str != NULL) {
			bs_bits = strtobits(bs_str);
			if (bs_bits == INPUT_ERROR) {
				poptPrintUsage(initCon, stderr, 0);
				fprintf(stderr, "Invalid block size input. Try 64k\n");
				exit(1);
			}
			if (!cs_str)
				cs_bits = bs_bits;
		}

		if (cs_str != NULL) {
			cs_bits = strtobits(cs_str);
			if (cs_bits == INPUT_ERROR) {
				poptPrintUsage(initCon, stderr, 0);
				fprintf(stderr, "Invalid chunk size input. Try 64k\n");
				exit(1);
			}
			if (!bs_str)
				bs_bits = cs_bits;
		}

		if (js_str != NULL) {
			js_bytes = strtobytes(js_str);
			if (js_bytes == INPUT_ERROR) {
				poptPrintUsage(initCon, stderr, 0);
				fprintf(stderr, "Invalid journal size input. Try 400k\n");
				exit(1);
			}
		}
		trace_off(printf("js_bytes is %u, bs_bits is %u, and cs_bits is %u\n", js_bytes, bs_bits, cs_bits););
		return really_init_snapstore(orgdev_, snapdev_, metadev_, bs_bits, cs_bits, js_bytes);
	}
	if (strcmp(command, "agent") == 0) {
		char const *sockname;
		int listenfd;

		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		poptContext agentCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(agentCon, "<agent_socket>");

		char agentOpt = poptGetNextOpt(agentCon);

		if (agentOpt < -1) {
			fprintf(stderr, "%s: %s: %s\n", command, poptBadOption(agentCon, POPT_BADOPTION_NOALIAS), poptStrerror(agentOpt));
			poptFreeContext(agentCon);
			return 1;
		}

		sockname = poptGetArg(agentCon);
		if (sockname == NULL) {
			fprintf(stderr, "%s: socket name for ddsnap agent must be specified\n", command);
			poptPrintUsage(agentCon, stderr, 0);
			poptFreeContext(agentCon);
			return 1;
		}
		if (poptPeekArg(agentCon) != NULL) {
			fprintf(stderr, "%s: only one socket name may be specified\n", command);
			poptPrintUsage(agentCon, stderr, 0);
			poptFreeContext(agentCon);
			return 1;
		}

		poptFreeContext(agentCon);

		if (monitor_setup(sockname, &listenfd) < 0)
			error("Could not setup ddsnap agent server\n");

		int getsigfd;
		if (!nobg) {
			pid_t pid;

			if (!logfile)
				logfile = "/var/log/ddsnap.agent.log";

			pid = daemonize(logfile, pidfile, &getsigfd);
			if (pid == -1)
				error("Could not daemonize\n");
			if (pid != 0) {
				trace_off(printf("pid = %lu\n", (unsigned long)pid););
				return 0;
			}
		}

		if (monitor(listenfd, &(struct context){ .polldelay = -1 },
		    logfile, getsigfd) < 0)
			error("Could not start ddsnap agent server\n");

		return 0; /* not reached */

	}
	if (strcmp(command, "server") == 0) {
		char const *snapdev, *origdev, *metadev;
		char const *agent_sockname, *server_sockname;
		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		poptContext serverCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(serverCon, "<dev/snapshot> <dev/origin> [dev/meta] <agent_socket> <server_socket>");

		char serverOpt = poptGetNextOpt(serverCon);

		if (argc < 3) {
			poptPrintUsage(serverCon, stderr, 0);
			exit(1);
		}

		if (serverOpt < -1) {
			fprintf(stderr, "%s: %s: %s\n", command, poptBadOption(serverCon, POPT_BADOPTION_NOALIAS), poptStrerror(serverOpt));
			poptFreeContext(serverCon);
			return 1;
		}

		unsigned long long cachesize_bytes = 0;
		if (cachesize_str != NULL) {
			cachesize_bytes = strtobytes64(cachesize_str);
			if (cachesize_bytes == INPUT_ERROR_64) {
				fprintf(stderr, "Invalid cache size input. Omit option, or use 0 for the default\n");
				poptPrintUsage(serverCon, stderr, 0);
				return 1;
			}
		}

		snapdev = poptGetArg(serverCon);
		if (!snapdev) {
			fprintf(stderr, "%s: snapshot device must be specified\n", command);
			poptPrintUsage(serverCon, stderr, 0);
			poptFreeContext(serverCon);
			return 1;
		}

		origdev = poptGetArg(serverCon);
		if (!origdev) {
			fprintf(stderr, "%s: origin device must be specified\n", command);
			poptPrintUsage(serverCon, stderr, 0);
			poptFreeContext(serverCon);
			return 1;
		}

		char const *extra_arg[3];

		extra_arg[0] = poptGetArg(serverCon);
		extra_arg[1] = poptGetArg(serverCon);
		extra_arg[2] = poptGetArg(serverCon);

		if (!extra_arg[0] || !extra_arg[1]) {
			fprintf(stderr, "%s: agent and server socket names must both be specified\n", command);
			poptPrintUsage(serverCon, stderr, 0);
			poptFreeContext(serverCon);
			return 1;
		}

		if (extra_arg[2]) {
			metadev = extra_arg[0];
			agent_sockname = extra_arg[1];
			server_sockname = extra_arg[2];
		} else {
			metadev = NULL;
			agent_sockname = extra_arg[0];
			server_sockname = extra_arg[1];
		}

		if (poptPeekArg(serverCon) != NULL) {
			fprintf(stderr, "%s: too many arguments\n", command);
			poptPrintUsage(serverCon, stderr, 0);
			poptFreeContext(serverCon);
			return 1;
		}

		int orgdev_, snapdev_, metadev_;
		if ((snapdev_ = open(snapdev, O_RDWR | O_DIRECT)) == -1)
			error("Could not open snapshot store %s: %s", snapdev, strerror(errno));

		if ((orgdev_ = open(origdev, O_RDONLY | O_DIRECT)) == -1)
			error("Could not open origin volume %s: %s", origdev, strerror(errno));

		metadev_ = snapdev_;
		if (metadev && (metadev_ = open(metadev, O_RDWR | O_DIRECT)) == -1)
			error("Could not open meta volume %s: %s", metadev, strerror(errno));

		poptFreeContext(serverCon);

		return start_server(orgdev_, snapdev_, metadev_, agent_sockname, server_sockname, logfile, pidfile, nobg, cachesize_bytes);
	}
	if (strcmp(command, "create") == 0) {
		if (argc != 4) {
			printf("Usage: %s create <sockname> <snapshot>\n", argv[0]);
			return 1;
		}

		u32 snaptag;

		if (parse_snaptag(argv[3], &snaptag) < 0) {
			fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], argv[3]);
			return 1;
		}

		int sock = create_socket(argv[2]);

		int ret = create_snapshot(sock, snaptag);
		close(sock);
		return ret;
	}
	if (strcmp(command, "delete") == 0) {
		if (argc != 4) {
			printf("Usage: %s delete <sockname> <snapshot>\n", argv[0]);
			return 1;
		}

		u32 snaptag;

		if (parse_snaptag(argv[3], &snaptag) < 0) {
			fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], argv[3]);
			return 1;
		}

		int sock = create_socket(argv[2]);

		int ret = delete_snapshot(sock, snaptag);
		close(sock);
		return ret;
	}
	if (strcmp(command, "list") == 0) {
		if (argc != 3) {
			printf("Usage: %s list <sockname>\n", argv[0]);
			return 1;
		}

		int sock = create_socket(argv[2]);

		int ret = list_snapshots(sock, TRUE, FALSE);
		close(sock);
		return ret;
	}
	if (strcmp(command, "priority") == 0) {
		if (argc != 5) {
			printf("usage: %s priority <sockname> <snap_tag> <new_priority_value>\n", argv[0]);
			return 1;
		}

		u32 snaptag;

		if (parse_snaptag(argv[3], &snaptag) < 0) {
			fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], argv[3]);
			return 1;
		}

		int sock = create_socket(argv[2]);

		int prio = atoi(argv[4]);
		if (prio < -128 || prio > 127) {
			fprintf(stderr, "%s: priority %d out of range\n", argv[0], prio);
			return 1;
		}
		int ret = set_priority(sock, snaptag, prio);
		close(sock);
		return ret;
	}
	if (strcmp(command, "usecount") == 0) {
		int diff_amount = 0;

		if (argc != 4 && argc != 5) {
			printf("usage: %s usecount <sockname> <snap_tag> [diff_amount]\n", argv[0]);
			return 1;
		}
		if (argc == 5)
			diff_amount = atoi(argv[4]);

		u32 snaptag;

		if (parse_snaptag(argv[3], &snaptag) < 0) {
			fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], argv[3]);
			return 1;
		}

		int sock = create_socket(argv[2]);

		int ret = usecount(sock, snaptag, diff_amount);
		close(sock);
		return ret;
	}
	if (strcmp(command, "status") == 0) {
		poptContext cdCon;

		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &stOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		cdCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(cdCon, "<sockname> [<snapshot>]");

		char c;

		while ((c = poptGetNextOpt(cdCon)) >= 0);
		if (c < -1) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], poptBadOption(cdCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
			poptFreeContext(cdCon);
			return 1;
		}

		if (last+list+size+state+verb > 1) {
			fprintf(stderr, "%s %s: Incompatible status options specified\n", argv[0], argv[1]);
			poptPrintUsage(cdCon, stderr, 0);
			poptFreeContext(cdCon);
			return 1;
		}

		if (last) {
			char const *sockname;

			sockname = poptGetArg(cdCon);

			if (sockname == NULL)
				cdUsage(cdCon, 1, argv[0], "Must specify socket name to status\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 1, argv[0], "Too many arguments to status\n");

			poptFreeContext(cdCon);

			int sock = create_socket(sockname);

			int ret = list_snapshots(sock, FALSE, TRUE);
			close(sock);

			return ret;
		} else if (list) {
			char const *sockname;

			sockname = poptGetArg(cdCon);

			if (sockname == NULL)
				cdUsage(cdCon, 1, argv[0], "Must specify socket name to status\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 1, argv[0], "Too many arguments to status\n");

			poptFreeContext(cdCon);

			int sock = create_socket(sockname);

			int ret = list_snapshots(sock, FALSE, FALSE);
			close(sock);

			return ret;
		} else if (size) {
			char const *sockname, *snaptagstr;

			sockname = poptGetArg(cdCon);
			snaptagstr = poptGetArg(cdCon);

			if (sockname == NULL)
				cdUsage(cdCon, 1, argv[0], "Must specify socket name to status\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 1, argv[0], "Too many arguments to status\n");

			poptFreeContext(cdCon);

			int sock = create_socket(sockname);

			u32 snaptag;
			if (snaptagstr == NULL)
				snaptag = ~((u32)0U); /* meaning "origin snapshot" */
			else if (parse_snaptag(snaptagstr, &snaptag) < 0) {
				fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], snaptagstr);
				return 1;
			}
			printf("%Lu\n", get_snapshot_sectors(sock, snaptag));
			close(sock);
			return 0;
		} else if (state) {
			char const *sockname, *snaptagstr;

			sockname = poptGetArg(cdCon);
			snaptagstr = poptGetArg(cdCon);

			if (sockname == NULL)
				cdUsage(cdCon, 9, argv[0], "Must specify socket name to state\n");
			if (snaptagstr == NULL)
				cdUsage(cdCon, 9, argv[0], "Must specify snapshot id to state\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 9, argv[0], "Too many arguments to state\n");

			poptFreeContext(cdCon);

			u32 snaptag;

			if (parse_snaptag(snaptagstr, &snaptag) < 0) {
				fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], snaptagstr);
				return 1;
			}

			int sock = create_socket(sockname);

			int ret = get_state(sock, snaptag);
			close(sock);

			return ret;
		} else{
			char const *sockname, *snaptagstr;

			sockname   = poptGetArg(cdCon);
			snaptagstr = poptGetArg(cdCon);

			if (sockname == NULL)
				cdUsage(cdCon, 1, argv[0], "Must specify socket name to status\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 1, argv[0], "Too many arguments to status\n");

			poptFreeContext(cdCon);

			u32 snaptag;

			if (snaptagstr) {
				if (parse_snaptag(snaptagstr, &snaptag) < 0) {
					fprintf(stderr, "%s: invalid snapshot %s\n", argv[0], snaptagstr);
					return 1;
				}
			} else {
				snaptag = ~((u32)0U); /* meaning "all snapshots" */
			}

			int sock = create_socket(sockname);

			int ret = ddsnap_get_status(sock, snaptag, verb);
			close(sock);

			return ret;
		}
	}
	/* syntax for delta/volume replication: ddsnap transmit <socket> <host[:port]> [fromsnap] <tosnap> */
	if (strcmp(command, "transmit") == 0) {
		poptContext cdCon;

		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &cdOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		cdCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(cdCon, "<sockname> <host[:port]> [fromsnap] <tosnap>");

		char c;

		while ((c = poptGetNextOpt(cdCon)) >= 0);
		if (c < -1) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], poptBadOption(cdCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
			poptFreeContext(cdCon);
			return 1;
		}

		/* Make sure the options are mutually exclusive */
		if (xd+raw+best_comp > 1) {
			fprintf(stderr, "%s %s: Too many chunk options were selected.\nPlease select only one: -x, -r or -b\n", argv[0], argv[1]);
			poptPrintUsage(cdCon, stderr, 0);
			poptFreeContext(cdCon);
			return 1;
		}

		u32 mode = (raw ? RAW : (xd ? XDELTA : BEST_COMP));
		if (best_comp)
			gzip_level = MAX_GZIP_COMP;

		u64 start_addr = 0;
		if (resume && (!sscanf(resume, "%Lu", &start_addr))) {
			fprintf(stderr, "%s %s: Invalid resume position specified", argv[0], argv[1]);
			poptPrintUsage(cdCon, stderr, 0);
			poptFreeContext(cdCon);
			return 1;
		}

		trace_off(fprintf(stderr, "xd=%d raw=%d best_comp=%d mode=%u gzip_level=%d\n", xd, raw, best_comp, mode, gzip_level););

		char const *sockname, *snaptag1str, *snaptag2str, *hoststr;

		sockname      = poptGetArg(cdCon);
		hoststr       = poptGetArg(cdCon);
		snaptag1str   = poptGetArg(cdCon);
		snaptag2str   = poptGetArg(cdCon);

		if ((sockname == NULL) || (hoststr == NULL) || (snaptag1str == NULL))
			cdUsage(cdCon, 1, argv[0], "Not enough arguments to send-delta\n");
		if (poptPeekArg(cdCon) != NULL)
			cdUsage(cdCon, 1, argv[0], "Too many arguments to send-delta\n");

		poptFreeContext(cdCon);

		char *hostname;
		unsigned port;
		hostname = strdup(hoststr);
		if (strchr(hostname, ':')) {
			unsigned int len = strlen(hostname);
			port = parse_port(hostname, &len);
			hostname[len] = '\0';
		} else {
			port = DEFAULT_REPLICATION_PORT;
		}
		int sock = create_socket(sockname);
		int ds_fd = open_socket(hostname, port);
		if (ds_fd < 0) {
			warn("%s %s: unable to connect to downstream server %s port %u: %s", argv[0], argv[1], hostname, port, strerror(errno));
			free(hostname);
			return 1;
		}
		free(hostname);

		u32 snaptag1, snaptag2;
		/* the fromsnap is optional. in case a single snap is specified, set src_snap to -1
		 * when calling ddsnap_replication_send to indicate full volume replication */
		if (parse_snaptag(snaptag1str, &snaptag1) < 0) {
			fprintf(stderr, "%s %s: invalid snapshot %s\n", argv[0], argv[1], snaptag1str);
			return 1;
		}
		if (snaptag2str == NULL) {
			snaptag2 = snaptag1;
			snaptag1 = -1;
		} else if (parse_snaptag(snaptag2str, &snaptag2) < 0) {
			fprintf(stderr, "%s %s: invalid snapshot %s\n", argv[0], argv[1], snaptag2str);
			return 1;
		}

		struct sigaction ign_sa;
		ign_sa.sa_handler = SIG_IGN;
		sigemptyset(&ign_sa.sa_mask);
		ign_sa.sa_flags = 0;
		if (sigaction(SIGPIPE, &ign_sa, NULL) == -1)
			warn("could not disable SIGPIPE: %s", strerror(errno));

		int ret;
		char *volume = strrchr(sockname, '/');
		if (!volume) {
			warn ("cannot get volume name from server sockname");
			ret = 1;
		} else {
			/* FIXME: should get device name from ddsnap server instead of assuming the naming style by zumastor */
			char *devstem = malloc(strlen(DEVMAP_PATH) + strlen(volume) + 1);
			if (!devstem) {
				warn ("cannot get volume name from server sockname");
				ret = 1;
			} else {
				sprintf(devstem, "%s%s", DEVMAP_PATH, volume);
				ret = ddsnap_replication_send(sock, snaptag1, snaptag2, devstem, mode, gzip_level, ds_fd, progress_file, start_addr);
				free(devstem);
			}
		}
		close(ds_fd);
		close(sock);

		return ret;
	}
	if (strcmp(command, "delta") == 0) {
		poptContext deltaCon;
		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &deltaOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};
		deltaCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);

		if (argc < 3) {
			poptPrintHelp(deltaCon, stdout, 0);
			poptFreeContext(deltaCon);
			exit(1);
		}

		char const *subcommand = argv[2];

		if (strcmp(subcommand, "--help") == 0 || strcmp(subcommand, "-?") == 0) {
			poptPrintHelp(deltaCon, stdout, 0);
			poptFreeContext(deltaCon);
			exit(0);
		}

		poptFreeContext(deltaCon);

		if (strcmp(subcommand, "--usage") == 0) {
			deltaUsage();
			exit(0);
		}

		if (strcmp(subcommand, "changelist") == 0) {
			if (argc != 7) {
				printf("usage: %s %s changelist <sockname> <changelist> <snapshot1> <snapshot2>\n", argv[0], argv[1]);
				return 1;
			}

			u32 snaptag1, snaptag2;

			if (parse_snaptag(argv[5], &snaptag1) < 0) {
				fprintf(stderr, "%s %s: invalid snapshot %s\n", argv[0], argv[1], argv[5]);
				return 1;
			}

			if (parse_snaptag(argv[6], &snaptag2) < 0) {
				fprintf(stderr, "%s %s: invalid snapshot %s\n", argv[0], argv[1], argv[6]);
				return 1;
			}

			int sock = create_socket(argv[3]);

			int ret = ddsnap_generate_changelist(sock, argv[4], snaptag1, snaptag2);
			close(sock);
			return ret;
		}
		if (strcmp(subcommand, "create") == 0) {
			char cdOpt;
			poptContext cdCon;

			struct poptOption options[] = {
				{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &cdOptions, 0, NULL, NULL },
				POPT_AUTOHELP
				POPT_TABLEEND
			};

			cdCon = poptGetContext(NULL, argc-2, (const char **)&(argv[2]), options, 0);
			poptSetOtherOptionHelp(cdCon, "<changelist> <deltafile> <devstem>");

			cdOpt = poptGetNextOpt(cdCon);

			if (argc < 3) {
				poptPrintUsage(cdCon, stderr, 0);
				exit(1);
			}

			if (cdOpt < -1) {
				/* an error occurred during option processing */
				fprintf(stderr, "%s: %s: %s\n",
					command,
					poptBadOption(cdCon, POPT_BADOPTION_NOALIAS),
					poptStrerror(cdOpt));
				poptFreeContext(cdCon);
				return 1;
			}

			/* Make sure the options are mutually exclusive */
			if (xd+raw+best_comp > 1) {
				fprintf(stderr, "%s %s: Too many chunk options were selected.\nPlease select only one: -x, -r or -b\n", argv[0], argv[1]);
				poptPrintUsage(cdCon, stderr, 0);
				poptFreeContext(cdCon);
				return 1;
			}

			u32 mode = (raw ? RAW : (xd? XDELTA : BEST_COMP));
			if (best_comp)
				gzip_level = MAX_GZIP_COMP;

			trace_off(fprintf(stderr, "xd=%d raw=%d best_comp=%d mode=%u gzip_level=%d\n", xd, raw, best_comp, mode, gzip_level););

			char const *changelist, *deltafile, *devstem;

			changelist = poptGetArg(cdCon);
			deltafile  = poptGetArg(cdCon);
			devstem    = poptGetArg(cdCon);

			if (changelist == NULL)
				cdUsage(cdCon, 1, "Specify a changelist", ".e.g., cl01\n");
			if (deltafile == NULL)
				cdUsage(cdCon, 1, "Specify a deltafile", ".e.g., df01\n");
			if (devstem == NULL)
				cdUsage(cdCon, 1, "Specify a devstem", ".e.g., /dev/mapper/snap\n");
			if (poptPeekArg(cdCon) != NULL)
				cdUsage(cdCon, 1, "Too many arguments inputted", "\n");

			int ret = ddsnap_generate_delta(mode, gzip_level, changelist, deltafile, devstem);

			poptFreeContext(cdCon);
			return ret;
		}
		if (strcmp(subcommand, "apply") == 0) {
			if (argc != 5) {
				printf("usage: %s %s apply <deltafile> <devstem>\n", argv[0], argv[1]);
				return 1;
			}
			return ddsnap_apply_delta(argv[3], argv[4]);
		}
		if (strcmp(subcommand, "listen") == 0) {
			char const *devstem;
			char const *hostspec;

			struct poptOption options[] = {
				{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &serverOptions, 0, NULL, NULL },
				POPT_AUTOHELP
				POPT_TABLEEND
			};

			poptContext dsCon = poptGetContext(NULL, argc-2, (const char **)&(argv[2]), options, 0);
			poptSetOtherOptionHelp(dsCon, "<devstem> <host>[:<port>]");

			char dsOpt = poptGetNextOpt(dsCon);

			if (dsOpt < -1) {
				fprintf(stderr, "%s %s: %s: %s\n", command, subcommand, poptBadOption(dsCon, POPT_BADOPTION_NOALIAS), poptStrerror(dsOpt));
				poptFreeContext(dsCon);
				return 1;
			}

			devstem = poptGetArg(dsCon);
			if (devstem == NULL) {
				fprintf(stderr, "%s %s: device stem must be specified\n", command, subcommand);
				poptPrintUsage(dsCon, stderr, 0);
				poptFreeContext(dsCon);
				return 1;
			}

			hostspec = poptGetArg(dsCon);

			if (poptPeekArg(dsCon) != NULL) {
				fprintf(stderr, "%s %s: only one host may be specified\n", command, subcommand);
				poptPrintUsage(dsCon, stderr, 0);
				poptFreeContext(dsCon);
				return 1;
			}

			poptFreeContext(dsCon);

			char *hostname;
			unsigned port;

			if (!hostspec) {
				hostname = strdup("0.0.0.0");
				port = DEFAULT_REPLICATION_PORT;
			} else {
				hostname = strdup(hostspec);

				if (strchr(hostname, ':')) {
					unsigned int len = strlen(hostname);
					port = parse_port(hostname, &len);
					hostname[len] = '\0';
				} else {
					port = DEFAULT_REPLICATION_PORT;
				}
			}

			/* make sure origin device exists (catch typos early) */
			int origin = open(devstem, O_RDONLY);
			if (origin < 0) {
				fprintf(stderr, "%s %s: unable to open origin device \"%s\" for reading: %s\n", command, subcommand, devstem, strerror(errno));
				free(hostname);
				return 1;
			}

			close(origin);

			int sock = bind_socket(hostname, port);
			if (sock < 0) {
				fprintf(stderr, "%s %s: unable to bind to %s port %u\n", command, subcommand, hostname, port);
				free(hostname);
				return 1;
			}
			free(hostname);

			int getsigfd;
			if (!nobg) {
				pid_t pid;

				if (!logfile)
					logfile = "/var/log/ddsnap.delta.log";

				pid = daemonize(logfile, pidfile, &getsigfd);
				if (pid == -1)
					error("Error: could not daemonize\n");
				if (pid != 0) {
					trace_off(printf("pid = %lu\n", (unsigned long)pid););
					return 0;
				}
			}

			return ddsnap_delta_server(sock, devstem, progress_file,
				logfile, getsigfd);
		}

		fprintf(stderr, "%s %s: unrecognized delta subcommand: %s.\n", argv[0], command, subcommand);
		return 1;
	}
	if (strcmp(command, "resize") == 0) {
		struct poptOption options[] = {
			{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &resizeOptions, 0, NULL, NULL },
			POPT_AUTOHELP
			POPT_TABLEEND
		};

		poptContext cdCon = poptGetContext(NULL, argc-1, (const char **)&(argv[1]), options, 0);
		poptSetOtherOptionHelp(cdCon, "<server_socket>");

		char cdOpt = poptGetNextOpt(cdCon);
		if (cdOpt < -1) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], poptBadOption(cdCon, POPT_BADOPTION_NOALIAS), poptStrerror(cdOpt));
			poptFreeContext(cdCon);
			return 1;
		}

		u64 orgsize=0, snapsize=0, metasize=0;
		if (orgsize_str != NULL)
			orgsize = strtobytes64(orgsize_str);
		if (snapsize_str != NULL)
			snapsize = strtobytes64(snapsize_str);
		if (metasize_str != NULL)
			metasize = strtobytes64(metasize_str);
		if (orgsize == INPUT_ERROR_64 || snapsize == INPUT_ERROR_64 || metasize == INPUT_ERROR_64) {
			fprintf(stderr, "ddsnap: Invalid size input. Omit option, or use 0 for keep the old size\n");
			return 1;
		}
		
		const char *sockname = poptGetArg(cdCon);
		if (sockname == NULL)
			cdUsage(cdCon, 1, argv[0], "Must specify socket name to resize\n");
		if (poptPeekArg(cdCon) != NULL)
			cdUsage(cdCon, 1, argv[0], "Too many arguments to resize\n");

		poptFreeContext(cdCon);

		int sock = create_socket(sockname);

		int ret = ddsnap_resize_devices(sock, orgsize, snapsize, metasize);

		return ret;
	}

	fprintf(stderr, "%s: unrecognized subcommand: %s.\n", argv[0], command);

	return 1;
}
