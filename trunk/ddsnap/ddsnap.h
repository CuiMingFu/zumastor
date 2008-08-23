#include <unistd.h>
#include <errno.h>

#define u8 unsigned char
#define s8 char
#define u16 unsigned short
#define s16 short
#define s32 int
#define u32 unsigned
#define u64 unsigned long long

#define U64FMT "%llu"
//short hand for "whatever printf("%llu") needs
typedef unsigned long long llu_t;

#define le_u32 u32
#define le_u16 u16
#define le_u64 u64
#define EFULL ENOMEM
#define PACKED __attribute__ ((packed))

#ifndef __DDSNAP_H
#define __DDSNAP_H
static inline int readpipe(int fd, void *buffer, size_t count)
{
	int n;

	while (count) {
		if ((n = read(fd, buffer, count)) < 1)
			return n == 0 ? -EPIPE : -errno;
		buffer += n;
		count -= n;
	}

	return 0;
}

static inline int writepipe(int fd, void const *buffer, size_t count)
{
	int n;

	while (count) {
		if ((n = write(fd, buffer, count)) < 0)
			return -errno;
		buffer += n;
		count -= n;
	}

	return 0;
}

#define outbead(SOCK, CODE, STRUCT, VALUES...) ({ \
	struct { struct head head; STRUCT body; } PACKED message = \
		{ { CODE, sizeof(STRUCT) }, { VALUES } }; \
	writepipe(SOCK, &message, sizeof(message)); })

/* outhead calls event_hook, then writepipe, and returns writepipe's value. */
#define outhead(SOCK, CODE, SIZE) (writepipe(SOCK, &(struct head){ CODE, SIZE }, sizeof(struct head) ))

struct server { struct server_head { u8 type; u8 length; } header; char *address; } PACKED;

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#define MAX_SNAPSHOTS 64
#define SNAPSHOT_SQUASHED MAX_SNAPSHOTS

struct change_list
{
	u64 count;
	u64 length;
	u32 chunksize_bits;
	u32 src_snap;
	u32 tgt_snap;
	u64 *chunks;
};

#define MAX_ERRMSG_SIZE 128

extern struct change_list *init_change_list(u32 chunksize_bits, u32 src_snap, u32 tgt_snap);
extern int append_change_list(struct change_list *cl, u64 chunkaddr);
extern void free_change_list(struct change_list *cl);

enum runflags { RUN_SB_DIRTY = 1, RUN_SELFCHECK = 2, RUN_DEFER = 4 };

int sniff_snapstore(int metadev);

int init_snapstore(
	int orgdev, int snapdev, int metadev,
	unsigned bs_bits, unsigned cs_bits, unsigned js_bytes);

int start_server(
	int orgdev, int snapdev, int metadev, 
	char const *agent_sockname, char const *server_sockname, char const *logfile, char const *pidfile,
	int nobg, uint64_t cachesize_bytes, enum runflags flags);

/* start_server flags */

#endif // __DDSNAP_H
