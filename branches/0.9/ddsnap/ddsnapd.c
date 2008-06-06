/*
 * Distributed Data Snapshot Server
 *
 * By: Daniel Phillips, Nov 2003 to Mar 2007
 * (c) 2003, Sistina Software Inc.
 * (c) 2004, Red Hat Software Inc.
 * (c) 2005 Daniel Phillips
 * (c) 2006 - 2007, Google Inc
 *
 * Contributions by:
 * Robert Nelson <rlnelson@google.com>, 2006 - 2007
 * Shapor Ed  Shapor Ed Naghibzadeh <shapor@google.com>, 2007
 */

#define _GNU_SOURCE // posix_memalign
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h> // gethostbyname2_r
#include <popt.h>
#include <sys/prctl.h>
#include <sys/sysinfo.h>
#include "dm-ddsnap.h"
#include "buffer.h"
#include "daemonize.h"
#include "ddsnap.h"
#include "diskio.h"
#include "list.h"
#include "sock.h"
#include "trace.h"
#include "event.h"

#define SECTORS_PER_BLOCK 7
#define CHUNK_SIZE 4096
#define DEFAULT_JOURNAL_SIZE (100 * CHUNK_SIZE)
#define INPUT_ERROR 0

#define SB_SECTOR 8			/* Sector where superblock lives.     */
#define SB_SECTORS 8			/* Size of ddsnap super block in sectors */
#define SB_MAGIC { 't', 'e', 's', 't', 0xdd, 0x07, 0x06, 0x04 } /* date of latest incompatible sb format */
/*
 * Snapshot store format revision history
 * !!! always update this for every incompatible change !!!
 *
 * 2007-04-05: SB magic added and enforced
 * 2007-06-04: SB journal commit block used fields replaced by free
 */

#define DDSNAPD_CLIENT_ERROR -1
#define DDSNAPD_AGENT_ERROR -2
#define DDSNAPD_CAUGHT_SIGNAL -3

/* XXX - Hack, this number needs to be the same as the 
 * kernel headers. Make sure that these are same when
 * building with a new kernel. 
 */
#define PR_SET_LESS_THROTTLE 23
#define PR_SET_MEMALLOC	24
#define MAX_NEW_METACHUNKS 10
#define MAX_DEFERRED_ALLOCS 500

#ifndef trace
#define trace trace_off
#endif

#ifndef jtrace
#define jtrace trace_off
#endif

//#define BUSHY // makes btree nodes very small for debugging

#define DIVROUND(N, D) (((N)+(D)-1)/(D))

/*
Todo:

BTree
  * coalesce leafs/nodes for delete
  - B*Tree splitting

Allocation bitmaps
  + allocation statistics
  - Per-snapshot free space as full-tree pass
  - option to track specific snapshot(s) on the fly
  \ return stats to client (on demand? always?)
  * Bitmap block radix tree - resizing
  - allocation policy

Journal
  \ allocation
  \ write commit block
  \ write target blocks
  \ recovery
  \ stats, misc data in commit block?

File backing
  \ double linked list ops
  \ buffer lru
  \ buffer writeout policy
  \ buffer eviction policy
  - verify no busy buffers between operations

Snapshot vs origin locking
  - anti-starvation measures

Message handling
  - send reply on async write completion
  - build up immediate reply in separate buffer

Snapshot handling path
  - background copyout thread
  - try AIO
  \ coalesce leaves/nodes on delete
     - should wait for current queries on snap to complete
  - background deletion optimization
     - record current deletion list in superblock
  - separate thread for copyouts
  - separate thread for buffer flushing
  - separate thread for new connections (?)

Utilities
  - snapshot store integrity check (snapcheck)

Failover
  \ Mark superblock active/inactive
  + upload client locks on server restart
  + release snapshot read locks for dead client
  - Examine entire tree to initialize statistics after unsaved halt

General
  \ Prevent multiple server starts on same snapshot store
  + More configurable tracing
  + Add more internal consistency checks
  \ Magic number + version for superblock
  + Flesh out and audit error paths
  - Make it endian-neutral
  \ Verify wordsize neutral
  \ Add an on-the-fly verify path
  + strip out the unit testing gunk
  + More documentation
  + Audits and more audits
  \ Memory inversion prevention
  \ failed chunk alloc fails operation, no abort

Cluster integration
  + Restart/Error recovery/reporting
*/

/*
 * Snapshot btree
 */

struct enode
{
	u32 count;
	u32 unused;
	struct index_entry
	{
		u64 key; // note: entries[0].key never accessed
		sector_t sector; // node sector address goes here
	} entries[];
};

struct eleaf
{
	le_u16 magic;
	le_u16 version;
	le_u32 count;
	le_u64 base_chunk; // !!! FIXME the code doesn't use the base_chunk properly
	le_u64 using_mask;
	struct etree_map
	{
		le_u32 offset;
		le_u32 rchunk;
	}
	map[];
};

static inline struct enode *buffer2node(struct buffer *buffer)
{
	return (struct enode *)buffer->data;
}

static inline struct eleaf *buffer2leaf(struct buffer *buffer)
{
	return (struct eleaf *)buffer->data;
}

struct exception
{
	le_u64 share;
	le_u64 chunk;
};

static inline struct exception *emap(struct eleaf *leaf, unsigned i)
{
	return	(struct exception *)((char *) leaf + leaf->map[i].offset);
}

enum sbflags { SB_BUSY = 2 };

struct disksuper
{
	typeof((char[])SB_MAGIC) magic;
	u64 create_time;
	sector_t etree_root;		/* The b-tree root node sector.       */
	sector_t orgoffset, orgsectors;
	u64 flags;
	u64 deleting;
	struct snapshot
	{
		u32 ctime; // upper 32 bits are in super create_time
		u32 tag;   // external name (id) of snapshot
		u16 usecount; // persistent use count on snapshot device
		u8 bit;    // internal snapshot number, not derived from tag
		s8 prio;   // 0=normal, 127=highest, -128=lowest
		u64 sectors; // sectors of the snapshot
	} snaplist[MAX_SNAPSHOTS]; // entries are contiguous, in creation order
	u32 snapshots;
	u32 etree_levels;
	s32 journal_base;		/* Sector offset of start of journal. */
	s32 journal_next;		/* Next chunk of journal to write.    */
	s32 journal_size;		/* Size of the journal in chunks.     */
	u32 sequence;			/* Latest commit block sequence num.  */
	struct allocspace_img
	{
		sector_t bitmap_base;	/* Allocation bitmap starting sector. */
		sector_t chunks; /* if zero then snapdata is combined in metadata space */
		sector_t freechunks;
		chunk_t  last_alloc;	/* Last chunk allocated.              */
		u64      bitmap_blocks;	/* Num blocks in allocation bitmap.   */
		u32      allocsize_bits; /* Bits of number of bytes in chunk. */
	} metadata, snapdata;
};

struct allocspace { // everything bogus here!!!
	struct allocspace_img *asi;	/* Points at image.metadata/snapdata. */
	u32 allocsize;			/* Size of a chunk in bytes.          */
	u32 chunk_sectors_bits;	     /* Bits of number of sectors in a chunk. */
	u32 alloc_per_node;		/* Number of enode index entries per  */
					/* tree node. (metadata only).        */
};

/*
 * Deferred allocation
 *
 * Each time a block is allocated a bitmap must be updated then written both
 * to the journal and the physical bitmap location synchronously before a
 * write request can be acknowledged.
 *
 * The deferred allocation optimization avoids updating bitmaps at the time
 * a block is allocated, by remembering up to one contiguous range of block
 * allocations per journal commit block.  Eventually, before the journal
 * wraps (fuzzy definition alert!!!) all the deferred allocations are actually
 * written to the bitmaps and the bitmaps are updated in a single journal
 * transaction, called a "barrier".  This never requires more total writes and
 * should save a large number of bitmap writes by combining updates within a
 * given bitmap block.  It also reduces seeking between the journal and bitmap
 * blocks.
 *
 * At the lowest level of the block allocator, a successful allocation (how do
 * we avoid double allocations???) is remembered in the super block instead
 * of being updated in the bitmap. (!!! this won't work, we need to set the
 * bitmap to avoid re-using the allocation !!! does our cycle counter save us?
 * yes, but need to flush deferred allocs if cyclic allocator wraps, which will
 * not work well except for cyclic allocation, which is a bad idea in the long
 * run.  Just for now...)
 *
 * Note!!!  Does not work for separate data/metadata yet.  Need to think
 * about how to make that work.  Simply disable the optimization in the
 * separate case for now.
 */

struct alloc_range { u64 chunk; u32 barrier:1, count:31; u32 pad; };

struct superblock
{
	/* Persistent, saved to disk */
	struct disksuper image;
	struct allocspace metadata, snapdata; // kill me!!!
	char bogopad[4096 - sizeof(struct disksuper) - 2*sizeof(struct allocspace) ];

	/* Derived, not saved to disk */
	u64 snapmask; // bitmask of all valid snapshots
	unsigned runflags;
	unsigned snapdev, metadev, orgdev;
	unsigned snaplock_hash_bits;
	struct snaplock **snaplocks; // forward ref!!!
	unsigned copybuf_size;
	char *copybuf;
	chunk_t source_chunk, dest_exception;
	unsigned copy_chunks, deferred_allocs;
	unsigned max_commit_blocks; // physical addresses that fit in a commit block
	u16 usecount[MAX_SNAPSHOTS]; // transient usecount for connected devices
	struct alloc_range deferred_alloc[MAX_DEFERRED_ALLOCS], defer;
};

static int valid_sb(struct superblock *sb)
{
	return !memcmp(sb->image.magic, (char[])SB_MAGIC, sizeof(sb->image.magic));
}

static inline int combined(struct superblock *sb)
{
	return !sb->image.snapdata.chunks;
}

static inline chunk_t snapfree(struct superblock *sb)
{
	return sb->image.snapdata.freechunks;
}

static inline chunk_t metafree(struct superblock *sb)
{
	return sb->image.metadata.freechunks;
}

static inline unsigned chunk_sectors(struct allocspace *space)
{
	return 1 << space->chunk_sectors_bits;
}

static int deferring(struct superblock *sb)
{
	return !!(sb->runflags & RUN_DEFER);
}

/*
 * Journalling
 */

#define JMAGIC "MAGICNUM"

struct commit_block
{
	char magic[8];
	u32 checksum;
	s32 sequence;
	u32 entries;
	u64 snapfree;
	u64 metafree;
	struct alloc_range alloc;
	u64 sector[];
} PACKED;

/*
 * Given journal offset i, return the actual address of that block,
 * suitable for bread()ing.
 */
static sector_t journal_sector(struct superblock *sb, unsigned i)
{
	return sb->image.journal_base + (i << sb->metadata.chunk_sectors_bits);
}

static inline struct commit_block *buf2commit(struct buffer *buf)
{
	return (void *)buf->data;
}

static unsigned next_journal_block(struct superblock *sb)
{
	unsigned next = sb->image.journal_next;

	if (++sb->image.journal_next == sb->image.journal_size)
		sb->image.journal_next = 0;

	return next;
}

static int is_commit_block(struct commit_block *block)
{
	return !memcmp(&block->magic, JMAGIC, sizeof(block->magic));
}
/*
 * Note!!!  This relies on knowing that this magic number can never
 * occur in a journal data block.  This is true only of btree leaf
 * index blocks, not of bitmap blocks, which may carry arbitrary
 * data in the magic number position, which means that journal data
 * could mistakenly be interpreted as a commit block.  The chance
 * of this is very small, on the order of one in a billion replays.
 * To get rid of the possibility completely, data at the magic
 * position could be zeroed out if it happened to match the magic
 * number, and a bit shared with the physical block address in the
 * commit block could indicate this for each affected data block.
 * Then the data would be restored on journal replay.  This
 * is a lot of code to fix something that is highly unlikely ever
 * to rank highly as a possible failure mode.  One day, perhaps
 * this little chink in the armor should be filled.
 */

static u32 checksum_block(struct superblock *sb, u32 *data)
{
	int i, sum = 0;
	for (i = 0; i < sb->metadata.asi->allocsize_bits >> 2; i++)
		sum += data[i];
	return sum;
}

static struct buffer *jgetblk(struct superblock *sb, unsigned i)
{
	return getblk(sb->metadev, journal_sector(sb, i), sb->metadata.allocsize);
}

/*
 * Read the journal block at the given offset.
 */
static struct buffer *jread(struct superblock *sb, unsigned i)
{
	return bread(sb->metadev, journal_sector(sb, i), sb->metadata.allocsize);
}

/* counting free space for debugging purpose */
static struct buffer *snapread(struct superblock const *sb, sector_t sector)
{
	return bread(sb->metadev, sector, sb->metadata.allocsize);
}

static int bytebits(unsigned char c)
{
	unsigned count = 0;
	for (; c; c >>= 1)
		count += c & 1;
	return count;
}

static inline int get_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	return bitmap[bit >> 3] & (1 << (bit & 7));
}

static inline void set_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	bitmap[bit >> 3] |= 1 << (bit & 7);
}

static inline void clear_bitmap_bit(unsigned char *bitmap, unsigned bit)
{
	bitmap[bit >> 3] &= ~(1 << (bit & 7));
}

/*
 * flag = 0: check if bits for chunks from start_chunk to end_chunk are all zero
 * flag = 1: set bits for chunks from start_chunk to end_chunk
 * flag = 2: clear bits for chunks from start_chunk to end_chunk
 */
static int change_bits(struct superblock *sb, chunk_t start, chunk_t count, chunk_t base, int flag)
// !!! do sector shift inside here
{
	chunk_t chunk, limit = start + count;
	unsigned bitmap_shift = sb->metadata.asi->allocsize_bits + 3;
	u64 bitmap_mask = (1 << bitmap_shift ) - 1;
	sector_t sector = (base >> SECTOR_BITS) + ((start >> bitmap_shift) << sb->metadata.chunk_sectors_bits);
	trace(warn("start %Lu, count %Lu, base %Lu, flag %d, sector %Lu", start, count, base, flag, sector);)
	for (chunk = start; chunk < limit; sector += chunk_sectors(&sb->metadata)) {
		struct buffer *buffer = bread(sb->metadev, sector, sb->metadata.allocsize);
		do {
			if (!flag && get_bitmap_bit(buffer->data, chunk & bitmap_mask)) {
				warn("chunk %Lu is in use", chunk);
				return -1;
			}
			if (flag == 1)
				set_bitmap_bit(buffer->data, chunk & bitmap_mask);
			if (flag == 2)
				clear_bitmap_bit(buffer->data, chunk & bitmap_mask);
			chunk++;
		} while ((chunk & bitmap_mask) && (chunk < limit));
		if (flag)
			set_buffer_dirty(buffer);
		brelse(buffer);
	}
	return 0;
}

void set_allocated(struct superblock *sb, chunk_t chunk, unsigned count)
{
	change_bits(sb, chunk, count, sb->image.metadata.bitmap_base << SECTOR_BITS, 1);
}

/*
 * Walk the given allocation space bitmap and count the number of
 * free blocks.
 */
static chunk_t count_zeros(struct superblock *sb, struct allocspace *alloc)
{
	unsigned blocksize = sb->metadata.allocsize;
	unsigned char zeroes[256];

	for (int i = 0; i < 256; i++)
		zeroes[i] = bytebits(~i);
	chunk_t count = 0, block = 0, bytes = (alloc->asi->chunks + 7) >> 3;
	while (bytes) {
		struct buffer *buffer = snapread(sb, alloc->asi->bitmap_base + (block << sb->metadata.chunk_sectors_bits));
		if (!buffer)
			return 0; // can't happen!
		unsigned char *p = buffer->data;
		unsigned n = blocksize < bytes ? blocksize : bytes;
		trace_off(printf("count %u bytes of bitmap %Lx\n", n, block););
		bytes -= n;
		while (n--)
			count += zeroes[*p++];
		brelse(buffer);
		block++;
	}
	return count;
}

static chunk_t count_deferred(struct superblock *sb, struct allocspace *alloc)
{
	unsigned count = sb->defer.count;
	for (int i = 0; i < sb->deferred_allocs; i++)
		count += sb->deferred_alloc[i].count;
	return count;
}

static chunk_t count_free(struct superblock *sb, struct allocspace *alloc)
{
	return count_zeros(sb, alloc) - count_deferred(sb, alloc);
}

static void check_freespace(struct superblock *sb)
{
	chunk_t counted = count_free(sb, &sb->metadata);
	if (sb->metadata.asi->freechunks != counted) {
		warn("metadata free chunks count wrong: counted %Lu, free = %Li", (llu_t) counted, sb->metadata.asi->freechunks);
		sb->metadata.asi->freechunks = counted;
	}
	if (combined(sb)) // !!! should be able to lose this now
		return;
	counted = count_free(sb, &sb->snapdata);
	if (sb->snapdata.asi->freechunks != counted) {
		warn("snapdata free chunks count wrong: counted %Lu, free = %Li", (llu_t) counted, sb->snapdata.asi->freechunks);
		sb->snapdata.asi->freechunks = counted;
	}
}

static void selfcheck_freespace(struct superblock *sb)
{
	if ((sb->runflags & RUN_SELFCHECK))
		check_freespace(sb);
}

static void flush_journaled_buffers(void)
{
	while (!list_empty(&journaled_buffers)) {
		struct list_head *entry = journaled_buffers.next;
		struct buffer *buffer = list_entry(entry, struct buffer, dirty_list);
		jtrace(warn("write data sector = %Lx", buffer->sector););
		if (write_buffer_to(buffer, buffer->sector))
			warn("unable to write commit block to journal");
		set_buffer_uptodate(buffer);
	}
	assert(journaled_count == 0);
}

static void flush_deferred_allocs(struct superblock *sb)
{
	for (int i = 0; i < sb->deferred_allocs; i++)
		set_allocated(sb, sb->deferred_alloc[i].chunk, sb->deferred_alloc[i].count);
	sb->deferred_allocs = 0;
}

/*
 * Deferred allocations are written to the journal one (for now) per commit
 * block.  Do not commit so many deferred allocations that the barrier commit
 * required to flush them to bitmaps will overwrite the oldest deferred alloc
 * in the journal, otherwise deferred allocations will be forgotten and double
 * allocations will result, followed by severe corruption shortly after.
 *
 * The limit chosen below is arbitrary and pessimistic.  Repeat: must use a
 * pessimal flush critereon otherwise corruption is possible following journal
 * replay.  Do eventually develop a tighter limit by tracking the number of
 * bitmap blocks that will actually be dirtied by the flush.
 */

/*
 * For now there is only ever one open transaction in the journal, the newest
 * one, so we don't have to check for journal wrap, but just ensure that each
 * transaction stays small enough to fit in the journal.
 *
 * Since we don't have any asynchronous IO at the moment, journal commit is
 * straightforward: walk through the dirty blocks once, writing them to the
 * journal, then again, adding block addresses to the commit block.  We know
 * the dirty list didn't change between the two passes.  When ansynchronous
 * IO arrives here, this all has to be handled a lot more carefully.
 */
static void commit_transaction(struct superblock *sb, int barrier)
{
	if (list_empty(&dirty_buffers) && !sb->defer.count)
		return;

	if (sb->deferred_allocs >= sb->image.journal_size / 2 || journaled_count >= sb->image.journal_size / 2) {
		flush_journaled_buffers();
		flush_deferred_allocs(sb);
		barrier = 1;
	}

//warn(">>> %i <<<", sb->image.sequence);
	struct list_head *list;
	int safety = dirty_buffer_count;

	/*
	 * Write each dirty buffer sequentially to the journal.
	 */
	list_for_each(list, &dirty_buffers) {
		struct buffer *buffer = list_entry(list, struct buffer, dirty_list);
		unsigned pos = next_journal_block(sb);
		jtrace(warn("journal data block @%Lx [%u]", buffer->sector, pos););
		if (!buffer_dirty(buffer)) {
			warn("non-dirty buffer %i of %i found on dirty list, state = %i",
				dirty_buffer_count - safety + 1, dirty_buffer_count, buffer->state);
			show_dirty_buffers();
			die(123);
		}
		if (write_buffer_to(buffer, journal_sector(sb, pos)))
			jtrace(warn("unable to write dirty blocks to journal"););
		assert(safety--);
	}
	assert(!safety);

	/*
	 * Prepare the commit block, add the block address for each of the
	 * blocks we just wrote to it, checksum it and write it to the
	 * journal.
	 */
	unsigned pos = next_journal_block(sb);
	struct buffer *commit_buffer = jgetblk(sb, pos);
	memset(commit_buffer->data, 0, sb->metadata.allocsize);
	struct commit_block *commit = buf2commit(commit_buffer);
	*commit = (struct commit_block){ .magic = JMAGIC, .sequence = sb->image.sequence++ };

	list_for_each(list, &dirty_buffers) {
		struct buffer *buffer = list_entry(list, struct buffer, dirty_list);
		assert(commit->entries < sb->max_commit_blocks);
		commit->sector[commit->entries++] = buffer->sector;
	}

	jtrace(warn("commit journal block [%u]", pos););
	commit->checksum = 0;
	commit->checksum = -checksum_block(sb, (void *)commit);
	commit->snapfree = snapfree(sb);
	commit->metafree = metafree(sb);

	if (sb->defer.count) {
		commit->alloc = (struct alloc_range){ .chunk = sb->defer.chunk, .count = sb->defer.count };
		sb->deferred_alloc[sb->deferred_allocs++] = commit->alloc;
		sb->defer.count = 0;
	}

	if (barrier)
		commit->alloc.barrier = 1;

	if (write_buffer_to(commit_buffer, journal_sector(sb, pos)))
		jtrace(warn("unable to write checksum from commit block");); // what does this mean?
	brelse(commit_buffer);

	/* Use deferred metadata writes if RUN_DEFER flag is set and this is not a barrier commit. */
	if (deferring(sb) && !barrier) {
		/* move dirty buffers to the journaled_buffer list after writing commit block to disk */
		while (!list_empty(&dirty_buffers)) {
			struct list_head *entry = dirty_buffers.next;
			struct buffer *buffer = list_entry(entry, struct buffer, dirty_list);
			assert(buffer_dirty(buffer));
			jtrace(warn("move buffer to journaled_list, sector = %Lx", buffer->sector););
			add_buffer_journaled(buffer);
		}
	} else {
		/*
	 	* Now write each dirty buffer to its proper location.
	 	* TODO: this can be merged with the above flush_journaled_buffers() code.
	 	*/
		while (!list_empty(&dirty_buffers)) {
			struct list_head *entry = dirty_buffers.next;
			struct buffer *buffer = list_entry(entry, struct buffer, dirty_list);
			jtrace(warn("write data sector = %Lx", buffer->sector););
			assert(buffer_dirty(buffer));
			if (write_buffer(buffer)) // deletes it from dirty (fixme: fragile)
				jtrace(warn("unable to write commit block to journal"););
		}
	}
	/* checking free chunks for debugging purpose only,, return before this to skip the checking */
	selfcheck_freespace(sb);
}

static void commit_deferred_allocs(struct superblock *sb)
{
	commit_transaction(sb, 0);
	flush_deferred_allocs(sb);
	flush_journaled_buffers();
	commit_transaction(sb, 1);
}

/* Journal Replay */

#ifdef SHOW_HELPERS
static void show_alloc_range(struct alloc_range *range, char *sep)
{
	printf("%Li@%Li%s", (long long)range->count, (long long)range->chunk, sep);
}

static void _show_journal(struct superblock *sb)
{
	for (int i = 0; i < sb->image.journal_size; i++) {
		struct buffer *buffer = jread(sb, i);
		struct commit_block *commit = buf2commit(buffer);

		if (!is_commit_block(commit)) {
			printf("[%i] <data>\n", i);
			continue;
		}

		printf("[%i] seq=%i (%i) ", i, commit->sequence, commit->entries);
		for (int j = 0; j < commit->entries; j++)
			printf("%Lx ", (long long)commit->sector[j]);
		struct alloc_range alloc = commit->alloc;
		if (commit->alloc.barrier)
			printf("<barrier> ");
		if (alloc.count)
			show_alloc_range(&commit->alloc, " deferred ");

		printf("\n");
		assert(buffer->count == 1);
		brelse(buffer);
		evict_buffer(buffer); /* avoid data alias on next show */
	}
	printf("defered: (");
	show_alloc_range(&sb->defer, ") ");
	for (int i = 0; i < sb->deferred_allocs; i++)
		show_alloc_range(sb->deferred_alloc + i, " ");
	printf("\n");
}

#define show_journal(sb) do { warn("Journal..."); _show_journal(sb); } while (0)
#endif

#define fieldtype(structname, fieldname) typeof(((struct structname *)NULL)->fieldname)

static int replay_journal(struct superblock *sb)
{
	struct buffer *buffer;
	int jblocks = sb->image.journal_size, commits = 0;;
	char const *why = "";

	warn("Replaying journal");
	fieldtype(commit_block, sequence) seq[jblocks];
	unsigned pos[jblocks], newest = -1;

	/* Scan the entire journal for commit blocks */
	for (int i = 0; i < jblocks; i++) {
		if (!(buffer = jread(sb, i))) {
			why = "block read failed";
			goto failed_buffer;
		}

		struct commit_block *commit = buf2commit(buffer);
		if (is_commit_block(commit)) {
			if (checksum_block(sb, (void *)commit)) {
				why = "corrupt journal block";
				goto failed_buffer;
			}
			seq[commits] = commit->sequence;
			pos[commits] = i;
			commits++;
		}
		brelse(buffer);
	}

	//for (int i = 0; i < commits; i++)
	//	printf("found commit [%i]\n", seq[i]);
	/* Find the one commit where sequence does not increase by one */
	for (int i = 0; i < commits; i++) {
		unsigned seq_plus_one = (seq[i] + 1) & (typeof(seq[0]))~0;
		unsigned next_recorded_seq = seq[(i + 1) % commits];

		if (next_recorded_seq != seq_plus_one) {
			if (newest != -1) {
				why = "unexpected gap in journal sequence";
				goto failed;
			}
			newest = i;
		}
	}
	jtrace(warn("found newest commit [%u]", pos[newest]););

	/* Pick up any deferred allocs */
	struct commit_block *commit;
	int i = newest + 1, barrier = -1;
	do {
		if (i == commits)
			i = 0;
		buffer = jread(sb, pos[i]);
		commit = buf2commit(buffer);
		struct alloc_range alloc = commit->alloc;
		if (alloc.barrier) {
			sb->deferred_allocs = 0;
			barrier = i;
		}
		if (alloc.count) {
			jtrace(show_alloc_range(&alloc, " deferred\n");)
			sb->deferred_alloc[sb->deferred_allocs++] = (struct alloc_range){
				.chunk = alloc.chunk, .count = alloc.count };
		}
		brelse(buffer);
	} while (i++ != newest);

	/* write the journal blocks between the barrier and the newest commit to disk */
	if (barrier == -1 || barrier == newest)
		i = newest;
	else
		i = barrier + 1;
	do {
		if (i == commits)
			i = 0;
		buffer = jread(sb, pos[i]);
		commit = buf2commit(buffer);
		unsigned entries = commit->entries;
		for (int j = 0; j < entries; j++) {
			unsigned replay = (pos[i] - entries + j + jblocks) % jblocks;
			struct buffer *databuf = jread(sb, replay);
			if (is_commit_block(buf2commit(databuf)))
				error("data block [%u] marked as commit block", replay);
			jtrace(warn("write journal [%u] data to %Lx", replay, commit->sector[i]););
			warn("write journal [%u] data to %Lx", replay, commit->sector[i]);
			write_buffer_to(databuf, commit->sector[i]);
			brelse(databuf);
		}
		if (i != newest)
			brelse(buffer);
	} while (i++ != newest);

	/* Recover durable state from newest commit */
	sb->image.journal_next = (pos[newest] + 1 + jblocks) % jblocks;
	sb->image.sequence = commit->sequence + 1;
	sb->image.snapdata.freechunks = commit->snapfree;
	sb->image.metadata.freechunks = commit->metafree;
	brelse(buffer);

	check_freespace(sb); // passes for a fsck (need more)
	return 0;

failed_buffer:
	brelse(buffer);
failed:
	error("Journal recovery failed, %s", why);
	return -1;
}

/*
 * btree leaf editing
 */

/*
 * We operate directly on the BTree leaf blocks to insert exceptions and
 * to enquire the sharing status of given chunks.  This means all the data
 * items in the block should be properly aligned for architecture
 * independence.  To save space and to permit binary search a directory
 * map at the beginning of the block points at the exceptions stored
 * at the top of the block.  The difference between two successive directory
 * pointers gives the number of distinct exceptions for a given chunk.
 * Each exception is paired with a bitmap that specifies which snapshots
 * the exception belongs to.  (Read the above carefully, it is too clever)
 *
 * The chunk addresses in the leaf block directory are relative to a base
 * chunk to save space.  These are currently 32 bit values but may become
 * 16 bits values.  Since each is paired with a pointer into the list of
 * exceptions, 16 bit emap entries would limit the blocksize to 64K.
 * (Relative address is nuts and does not work, instead store high order
 * 32 bits, enforce no straddle across 32 bit boundary in split and merge)
 *
 * A mask in the leaf block header specifies which snapshots are actually
 * encoded in the chunk.  This allows lazy deletion (almost, needs fixing)
 *
 * The leaf operations need to know the size of the block somehow.
 * Currently that is accomplished by inserting the block size as a sentinel
 * in the block directory map; this may change.
 *
 * When an exception is created by a write to the origin it is initially
 * shared by all snapshots that don't already have exceptions.  Snapshot
 * writes may later unshare some of these exceptions.  The bitmap code
 * that implements this is subtle but efficient and demonstrably stable.
 */

/*
 * btree leaf todo:
 *   - Check leaf, index structure
 *   - Mechanism for identifying which snapshots are in each leaf
 *   - binsearch for leaf, index lookup
 *   - enforce 32 bit address range within leaf
 */

static unsigned leaf_freespace(struct eleaf *leaf);
static unsigned leaf_payload(struct eleaf *leaf);

/*
 * origin_chunk_unique: an origin logical chunk is shared unless all snapshots
 * have exceptions.
 */
static int origin_chunk_unique(struct eleaf *leaf, u64 chunk, u64 snapmask)
{
	u64 using = 0;
	u64 i, target = chunk - leaf->base_chunk;
	struct exception const *p;

	for (i = 0; i < leaf->count; i++)
		if (leaf->map[i].rchunk == target)
			goto found;
	return !snapmask;
found:
	for (p = emap(leaf, i); p < emap(leaf, i+1); p++)
		using |= p->share;

	return !(~using & snapmask);
}

/*
 * snapshot_chunk_unique: a snapshot logical chunk is shared if it has no
 * exception or has the same exception as another snapshot.  In any case
 * if the chunk has an exception we need to know the exception address.
 */
static int snapshot_chunk_unique(struct eleaf *leaf, u64 chunk, int snapbit, chunk_t *exception)
{
	u64 mask = 1LL << snapbit;
	unsigned i, target = chunk - leaf->base_chunk;
	struct exception const *p;

	for (i = 0; i < leaf->count; i++)
		if (leaf->map[i].rchunk == target)
			goto found;
	return 0;
found:
	for (p = emap(leaf, i); p < emap(leaf, i+1); p++)
		/* shared if more than one bit set including this one */
		if ((p->share & mask)) {
			*exception = p->chunk;
			return !(p->share & ~mask);
		}
	return 0;
}

/*
 * add_exception_to_leaf:
 *  - cycle through map to find existing logical chunk or insertion point
 *  - if not found need to add new chunk address
 *      - move tail of map up
 *      - store new chunk address in map
 *  - otherwise
 *      - for origin:
 *          - or together all sharemaps, invert -> new map
 *      - for snapshot:
 *          - clear out bit for existing exception
 *              - if sharemap zero warn and reuse this location
 *  - insert new exception
 *      - move head of exceptions down
 *      - store new exception/sharemap
 *      - adjust map head offsets
 *
 * If the new exception won't fit in the leaf, return an error so that
 * higher level code may split the leaf and try again.  This keeps the
 * leaf-editing code complexity down to a dull roar.
 */
/*
 * Returns the number of bytes of free space in the given leaf by computing
 * the difference between the end of the map entry list and the beginning
 * of the first set of exceptions.
 */
static unsigned leaf_freespace(struct eleaf *leaf)
{
	char *maptop = (char *)(&leaf->map[leaf->count + 1]); // include sentinel
	return (char *)emap(leaf, 0) - maptop;
}

/*
 * Returns the number of bytes used in the given leaf by computing the number
 * of bytes used by the map entry list and all sets of exceptions.
 */
static unsigned leaf_payload(struct eleaf *leaf)
{
	int lower = (char *)(&leaf->map[leaf->count]) - (char *)leaf->map;
	int upper = (char *)emap(leaf, leaf->count) - (char *)emap(leaf, 0);
	return lower + upper;
}

/*
 * Add an "exception" to a b-tree leaf.
 *
 * Finds the chunk to which we're adding the exception.  If it doesn't exist
 * in the leaf, add it.  Compute the share mask and insert the exception at
 * the appropriate place.  Return an error if there isn't enough room for the
 * new entry.
 */
static int add_exception_to_leaf(struct eleaf *leaf, u64 chunk, u64 exception, int snapshot, u64 active)
{
	unsigned target = chunk - leaf->base_chunk;
	u64 mask = 1ULL << snapshot, sharemap;
	struct exception *ins, *exceptions = emap(leaf, 0);
	char *maptop = (char *)(&leaf->map[leaf->count + 1]); // include sentinel
	unsigned i, j, free = (char *)exceptions - maptop;

	trace(warn("chunk %Lx exception %Lx, snapshot = %i free space = %u", 
		chunk, exception, snapshot, free););

	/*
	 * Find the chunk for which we're adding an exception entry.
	 */
	for (i = 0; i < leaf->count; i++) // !!! binsearch goes here
		if (leaf->map[i].rchunk >= target)
			break;

	/*
	 * If we didn't find the chunk, insert a new one at map[i].
	 */
	if (i == leaf->count || leaf->map[i].rchunk > target) {
		if (free < sizeof(struct exception) + sizeof(struct etree_map))
			return -EFULL;
		ins = emap(leaf, i);
		memmove(&leaf->map[i+1], &leaf->map[i], maptop - (char *)&leaf->map[i]);
		leaf->map[i].offset = (char *)ins - (char *)leaf;
		leaf->map[i].rchunk = target;
		leaf->count++;
		sharemap = snapshot == -1? active: mask;
		goto insert;
	}

	if (free < sizeof(struct exception))
		return -EFULL;
	/*
	 * Compute the share map from that of each existing exception entry
	 * for this chunk.  If we're doing this for a chunk on the origin,
	 * the new exception is shared between those snapshots that weren't
	 * already sharing exceptions for this chunk.  (We combine the sharing
	 * that already exists, invert it, then mask off everything but the
	 * active snapshots.)
	 *
	 * If this is a chunk on a snapshot we go through the existing
	 * exception list to turn off sharing with this snapshot (with the
	 * side effect that if the chunk was only shared by this snapshot it
	 * becomes unshared).  We then set sharing for this snapshot in the
	 * new exception entry.
	 */
	if (snapshot == -1) {
		for (sharemap = 0, ins = emap(leaf, i); ins < emap(leaf, i+1); ins++)
			sharemap |= ins->share;
		sharemap = (~sharemap) & active;
	} else {
		for (ins = emap(leaf, i); ins < emap(leaf, i+1); ins++)
			if ((ins->share & mask)) {
				ins->share &= ~mask;
				break;
			}
		sharemap = mask;
	}
	ins = emap(leaf, i);
insert:
	/*
	 * Insert the new exception entry.  These grow from the end of the
	 * block toward the beginning.  First move any earlier exceptions up
	 * to make room for the new one, then insert the new entry in the
	 * space freed.  Adjust the offsets for all earlier chunks.
	 */
	memmove(exceptions - 1, exceptions, (char *)ins - (char *)exceptions);
	ins--;
	ins->share = sharemap;
	ins->chunk = exception;

	for (j = 0; j <= i; j++)
		leaf->map[j].offset -= sizeof(struct exception);

	return 0;
}

/*
 * Split a leaf.
 *
 * This routine splits a b-tree leaf at the middle chunk.  It copies that
 * and later map entries along with the associated lists of exceptions to
 * the new leaf.  It moves the remaining exception lists to the end of the
 * original block then adjusts the offsets for those map entries and the
 * counts for each leaf.  It returns the chunk at which the leaf was split
 * (which is now the first chunk in the new leaf).
 */
static u64 split_leaf(struct eleaf *leaf, struct eleaf *leaf2)
{
	unsigned i, nhead = (leaf->count + 1) / 2, ntail = leaf->count - nhead, tailsize;
	/* Should split at middle of data instead of median exception */
	u64 splitpoint = leaf->map[nhead].rchunk + leaf->base_chunk;
	char *phead, *ptail;

	phead = (char *)emap(leaf, 0);
	ptail = (char *)emap(leaf, nhead);
	tailsize = (char *)emap(leaf, leaf->count) - ptail;

	/* Copy upper half to new leaf */
	memcpy(leaf2, leaf, offsetof(struct eleaf, map)); // header
	memcpy(&leaf2->map[0], &leaf->map[nhead], (ntail + 1) * sizeof(struct etree_map)); // map
	memcpy(ptail - (char *)leaf + (char *)leaf2, ptail, tailsize); // data
	leaf2->count = ntail;

	/* Move lower half to top of block */
	memmove(phead + tailsize, phead, ptail - phead);
	leaf->count = nhead;
	for (i = 0; i <= nhead; i++) // also adjust sentinel
		leaf->map[i].offset += tailsize;
	leaf->map[nhead].rchunk = 0; // tidy up

	return splitpoint;
}

/*
 * Merge the contents of 'leaf2' into 'leaf.'  The leaves are contiguous and
 * 'leaf2' follows 'leaf.'  Move the exception lists in 'leaf' up to make room
 * for those of 'leaf2,' adjusting the offsets in the map entries, then copy
 * the map entries and exception lists straight from 'leaf2.'
 */
static void merge_leaves(struct eleaf *leaf, struct eleaf *leaf2)
{
	unsigned nhead = leaf->count, ntail = leaf2->count, i;
	unsigned tailsize = (char *)emap(leaf2, ntail) - (char *)emap(leaf2, 0);
	char *phead = (char *)emap(leaf, 0), *ptail = (char *)emap(leaf, nhead);

	// move data down
	memmove(phead - tailsize, phead, ptail - phead);

	// adjust pointers
	for (i = 0; i <= nhead; i++) // also adjust sentinel
		leaf->map[i].offset -= tailsize;

	// move data from leaf2 to top
	memcpy(ptail - tailsize, (char *)emap(leaf2, 0), tailsize); // data
	memcpy(&leaf->map[nhead], &leaf2->map[0], (ntail + 1) * sizeof(struct etree_map)); // map
	leaf->count += ntail;
}

/*
 * Copy the index entries in 'node2' into 'node.'
 */
static void merge_nodes(struct enode *node, struct enode *node2)
{
	memcpy(&node->entries[node->count], &node2->entries[0], node2->count * sizeof(struct index_entry));
	node->count += node2->count;
}

static void init_leaf(struct eleaf *leaf, int block_size)
{
	leaf->magic = 0x1eaf;
	leaf->version = 0;
	leaf->base_chunk = 0;
	leaf->count = 0;
	leaf->map[0].offset = block_size;
#ifdef BUSHY
	leaf->map[0].offset = 200;
#endif
}

static void set_sb_dirty(struct superblock *sb)
{
	sb->runflags |= RUN_SB_DIRTY;
}

static void save_sb(struct superblock *sb)
{
	if (sb->runflags & RUN_SB_DIRTY) {
		if (diskwrite(sb->metadev, &sb->image, 4096, SB_SECTOR << SECTOR_BITS) < 0)
			warn("Unable to write superblock to disk: %s", strerror(errno));
		sb->runflags &= ~RUN_SB_DIRTY;
	}
}

/*
 * Allocation bitmaps
 */

/*
 * Round "chunks" up to the next even block boundary based on the number of
 * bits in a block.  That is, since the bitmap must be big enough to
 * accommodate "chunks" number of chunks, there must be that number of bits
 * in the bitmap.  Since we're allocating the bitmap in terms of integral
 * blocks, we must round any fractional number of blocks up to the next
 * higher block boundary.
 *
 * This could also be written to round up based on the number of bytes in
 * a block, rather than the number of bits.
 */
static u64 calc_bitmap_blocks(struct superblock *sb, u64 chunks)
{
	unsigned chunkshift = sb->metadata.asi->allocsize_bits;
	return (chunks + (1 << (chunkshift + 3)) - 1) >> (chunkshift + 3);
}

/*
 * Initialize the allocation bitmap.  Mark all chunks free except
 * those we reserve for the superblock and the allocation bitmap
 * itself.  Note that the bitmap will likely take a fractional block
 * and indeed may also take a fractional byte.  In the latter case,
 * we cheat by using the whole byte but marking the last few chunks
 * (those past the end of the volume) as used.  They'll never be freed
 * so we don't have to worry about dealing with the partial byte.
 */
static void init_bitmap_blocks(struct superblock *sb, unsigned bitmap_base, unsigned bitmaps, u64 chunks, u64 reserved_chunks)
{
	int i;
	unsigned sector = bitmap_base;
	u64 reserved = reserved_chunks;
	unsigned chunks_per_bitmap_block = sb->metadata.allocsize << 3;

	warn("Initializing %u bitmap block(s)", bitmaps);
	for (i = 0; i < bitmaps; i++, sector += chunk_sectors(&sb->metadata)) {
		struct buffer *buffer = getblk(sb->metadev, sector, sb->metadata.allocsize);
		if (reserved > chunks_per_bitmap_block) {
			memset(buffer->data, 0xff, sb->metadata.allocsize);
			reserved -= chunks_per_bitmap_block;
		} else {
			memset(buffer->data, 0, sb->metadata.allocsize);
			/* Suppress overrun allocation in partial last byte */
			if (reserved > 0) {
				for (int j = 0; j < reserved; j++)
					set_bitmap_bit(buffer->data, j);
				reserved = 0;
			}
		}
		if (i == bitmaps - 1 && (chunks & 7))
			buffer->data[(chunks >> 3) & (sb->metadata.allocsize - 1)] |= 0xff << (chunks & 7);
		write_buffer(buffer);
		brelse(buffer);
	}
}

/* 
 * Disk layout for combined snapshot store:
 *    4k unused
 *    4k ddsnap superblock
 *    bitmap blocks
 *    journal blocks
 *    btree and copied out blocks
 */

static int init_allocation(struct superblock *sb)
{
	unsigned meta_bitmap_base_chunk = (SB_SECTOR + SB_SECTORS + chunk_sectors(&sb->metadata) - 1) >> sb->metadata.chunk_sectors_bits;
	
	sb->metadata.asi->bitmap_blocks = calc_bitmap_blocks(sb, sb->metadata.asi->chunks); 
	sb->metadata.asi->bitmap_base = meta_bitmap_base_chunk << sb->metadata.chunk_sectors_bits;
	sb->metadata.asi->last_alloc = 0;

	unsigned reserved = meta_bitmap_base_chunk + sb->metadata.asi->bitmap_blocks + sb->image.journal_size;

	/*
	 * If we're using combined snapshot and metadata, we don't need to
	 * compute everything again.
	 */
	if (!combined(sb)) {
		u64 snap_bitmap_base_chunk = (sb->metadata.asi->bitmap_base >> sb->metadata.chunk_sectors_bits) + sb->metadata.asi->bitmap_blocks; // !!! expressing bitmap_base in sectors instead of chunks is brain damage

		sb->snapdata.asi->bitmap_blocks = calc_bitmap_blocks(sb, sb->snapdata.asi->chunks);
		sb->snapdata.asi->bitmap_base = snap_bitmap_base_chunk << sb->metadata.chunk_sectors_bits;
		sb->snapdata.asi->freechunks = sb->snapdata.asi->chunks;
		reserved += sb->snapdata.asi->bitmap_blocks;
	}

	sb->metadata.asi->freechunks = sb->metadata.asi->chunks - reserved;

	// !!! express journal_base in blocks, not sectors
	sb->image.journal_base = sb->metadata.asi->bitmap_base 
		+ ((sb->metadata.asi->bitmap_blocks +
		    (!combined(sb) ? sb->snapdata.asi->bitmap_blocks : 0)) 
		   << sb->metadata.chunk_sectors_bits);
	
	if (!combined(sb))
		warn("metadata store size: %Li chunks (%Li sectors)", 
		     sb->metadata.asi->chunks, sb->metadata.asi->chunks << sb->metadata.chunk_sectors_bits);
	u64 chunks  = !combined(sb) ? sb->snapdata.asi->chunks  : sb->metadata.asi->chunks;
	warn("snapshot store size: %Li chunks (%Li sectors)", 
	     chunks, chunks << sb->snapdata.chunk_sectors_bits);

	init_bitmap_blocks(sb, sb->metadata.asi->bitmap_base, sb->metadata.asi->bitmap_blocks, sb->metadata.asi->chunks, reserved);
	if (!combined(sb))
		init_bitmap_blocks(sb, sb->snapdata.asi->bitmap_base, sb->snapdata.asi->bitmap_blocks, sb->snapdata.asi->chunks, 0);
	selfcheck_freespace(sb);
	return 0;
}

// !!! unusual return codes
static int free_chunk(struct superblock *sb, struct allocspace *as, chunk_t chunk)
{
	unsigned bitmap_shift = sb->metadata.asi->allocsize_bits + 3, bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 bitmap_block = chunk >> bitmap_shift;

	trace(printf("free chunk %Lx\n", chunk););
	struct buffer *buffer = snapread(sb, as->asi->bitmap_base + 
			(bitmap_block << sb->metadata.chunk_sectors_bits));
	
	if (!buffer) {
		warn("unable to free chunk %Lu", (llu_t) chunk);
		return 0;
	}
	if (!get_bitmap_bit(buffer->data, chunk & bitmap_mask)) {
		warn("chunk %Lx already free!", (long long)chunk);
		brelse(buffer);
		return 0;
	}
	clear_bitmap_bit(buffer->data, chunk & bitmap_mask);
	brelse_dirty(buffer);
	as->asi->freechunks++;
	set_sb_dirty(sb); // !!! optimize this away
	return 1;
}

static inline void free_block(struct superblock *sb, sector_t address)
{
	free_chunk(sb, &sb->metadata, address >> sb->metadata.chunk_sectors_bits);
}

static inline void free_exception(struct superblock *sb, chunk_t chunk)
{
	free_chunk(sb, &sb->snapdata, chunk); // !!! why even have this?
}

#ifdef INITDEBUG2
static void grab_chunk(struct superblock *sb, struct allocspace *as, chunk_t chunk) // just for testing
{
	unsigned bitmap_shift = sb->metadata.asi->allocsize_bits + 3, bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 bitmap_block = chunk >> bitmap_shift;

	struct buffer *buffer = snapread(sb, as->asi->bitmap_base + (bitmap_block << sb->metadata.chunk_sectors_bits));
	assert(!get_bitmap_bit(buffer->data, chunk & bitmap_mask));
	set_bitmap_bit(buffer->data, chunk & bitmap_mask);
	brelse_dirty(buffer);
}
#endif

static int chunk_within(chunk_t chunk, chunk_t start, chunk_t count)
{
	return chunk - start < count; // chunk_t must be unsigned!
}

static int is_deferred_alloc(struct superblock *sb, chunk_t chunk)
{
	if (chunk_within(chunk, sb->defer.chunk, sb->defer.count))
		return 1;
	for (int i = 0; i < sb->deferred_allocs; i++)
		if (chunk_within(chunk, sb->deferred_alloc[i].chunk, sb->deferred_alloc[i].count))
			return 1;
	return 0;
}

static void show_deferred_alloc(struct superblock *sb)
{
	unsigned count = sb->defer.count;
	printf("deferred chunk %Li count %u\n", sb->defer.chunk, count);
	for (int i = 0; i < sb->deferred_allocs; i++) {
		count = sb->deferred_alloc[i].count;
		printf("deferred chunk %Li count %u\n", sb->deferred_alloc[i].chunk, count);
	}
}

/*
 * Allocate a single chunk out of a given range of chunks from the given
 * allocation space.
 *
 * This computes the block in the allocation bitmap that contains the bit
 * corresponding to the passed chunk as well as the offset to the byte that
 * contains that bit and the offset within that byte of the bit itself.  It
 * then scans the bitmap from that point to find a free chunk.  If it reaches
 * the end of the bitmap without finding a free chunk it starts over from the
 * beginning.  If it exhausts the range it's searching without finding a free
 * chunk, return failure.
 */
static chunk_t alloc_chunk_from_range(struct superblock *sb, struct allocspace *as, chunk_t startchunk, chunk_t nchunks)
{
	/*
	 * Set up a useful few bits of information here:
	 *	bitmap_shift - Conversion factor between bit and block offsets.
	 *	bitmap_mask  - "Modulo" mask to get a bit offset within a
	 *	               block.
	 *	blocknum     - The bitmap block at which we're starting.
	 *	offset       - The offset of the byte in that block with the
	 *	               bit for the starting chunk.
	 *	bit          - The bit offset of that bit within that byte.
	 *	length       - How far, in bytes, that we're going to scan
	 *	               within the allocation space.
	 */
	const unsigned bitmap_shift = sb->metadata.asi->allocsize_bits + 3;
	const unsigned bitmap_mask = (1 << bitmap_shift ) - 1;
	u64 blocknum = startchunk >> bitmap_shift;
	unsigned offset = (startchunk & bitmap_mask) >> 3;
	unsigned bit = startchunk & 7;
	u64 length = (nchunks + bit + 7) >> 3;

	while (1) {
		struct buffer *buffer = snapread(sb, as->asi->bitmap_base + (blocknum << sb->metadata.chunk_sectors_bits));
		if (!buffer)
			return -1;
		unsigned char c, *p = buffer->data + offset;
		unsigned tail = sb->metadata.allocsize - offset, n = tail > length? length: tail;
		trace(printf("search %u bytes of bitmap %Lx from offset %u\n", n, blocknum, offset););
		for (length -= n; n--; p++)
			if ((c = *p) != 0xff) {
				trace_off(printf("found byte at offset %u of bitmap %Lx = %hhx\n", p - buffer->data, blocknum, c););
				/* Scan the byte for the free chunk that must be there */
				for (int i = 0, bit = 1; i <= 7; i++, bit <<= 1)
					if (!(c & bit) ) {
						chunk_t chunk = i + ((p - buffer->data) << 3) + (blocknum << bitmap_shift);

						if (is_deferred_alloc(sb, chunk))
							continue;

						if (get_bitmap_bit(buffer->data, chunk & bitmap_mask)) {
							warn("chunk %Li already in use", chunk);
							show_deferred_alloc(sb);
						}
						assert(!get_bitmap_bit(buffer->data, chunk & bitmap_mask));
						if (deferring(sb) && sb->deferred_allocs < MAX_DEFERRED_ALLOCS) {
							if (!sb->defer.count) {
								sb->defer.chunk = chunk;
								sb->defer.count = 1;
								goto success;
							}
							if (chunk == sb->defer.chunk + 1) {
								sb->defer.count++;
								goto success;
							}
						}
						set_bitmap_bit(buffer->data, chunk & bitmap_mask);
						set_buffer_dirty(buffer);
success:
						brelse(buffer);
						as->asi->freechunks--;
						set_sb_dirty(sb); // !!! optimize this away
						return chunk;
					}
			}
		/* No free chunk in that block, did we run out? */
		brelse(buffer);
		if (!length)
			return -1;
		/* Go to the next block with wrap */
		if (++blocknum == as->asi->bitmap_blocks)
			 blocknum = 0;
		offset = 0;
		trace_off(printf("go to bitmap %Lx\n", blocknum););
	}

}

/*
 * btree entry lookup
 */

struct etree_path { struct buffer *buffer; struct index_entry *pnext; };

static inline struct enode *path_node(struct etree_path path[], int level)
{
	return buffer2node(path[level].buffer);
}

/*
 * Release each buffer in the given path array.
 */
static void brelse_path(struct etree_path *path, unsigned levels)
{
	unsigned i;
	for (i = 0; i < levels; i++)
		brelse(path[i].buffer);
}

/*
 * Find the b-tree leaf for the passed chunk.  Record the chain of enodes
 * leading from the root to that leaf in the passed etree_path.  Each element
 * of that path gets a buffer containing the enode at that level of the path
 * and a pointer to the next index entry in that enode.
 */
static struct buffer *probe(struct superblock *sb, u64 chunk, struct etree_path *path)
{
	unsigned i, levels = sb->image.etree_levels;
	struct buffer *nodebuf = snapread(sb, sb->image.etree_root);
	if (!nodebuf)
		return NULL;
	struct enode *node = buffer2node(nodebuf);

	for (i = 0; i < levels; i++) {
		struct index_entry *pnext = node->entries, *top = pnext + node->count;

		while (++pnext < top)
			if (pnext->key > chunk)
				break;

		path[i].buffer = nodebuf;
		path[i].pnext = pnext;
		nodebuf = snapread(sb, (pnext - 1)->sector);
		if (!nodebuf) {
			brelse_path(path, i);
			return NULL;
		}
		node = (struct enode *)nodebuf->data;
	}
	assert(((struct eleaf *)nodebuf->data)->magic == 0x1eaf);
	return nodebuf;
}

/*
 * Stack-based inorder B-tree traversal.
 */
static int traverse_tree_range(
	struct superblock *sb, chunk_t start, chunk_t finish,
	void (*visit_leaf)(struct superblock *sb, struct eleaf *leaf, void *data),
	void *data)
{
	int levels = sb->image.etree_levels, level = -1;
	struct etree_path path[levels];
	struct buffer *nodebuf;
	struct buffer *leafbuf;
	struct enode *node;

	if (start) { /* FIXME TODO start at random chunk address (not tested!!!!) */
		if (!(leafbuf = probe(sb, start, path)))
			return -ENOMEM;
		level = levels - 1;
		nodebuf = path[level].buffer;
		node = buffer2node(nodebuf);
		goto start;
	}

	while (1) {
		/*
		 * Build the stack down to the leaf level of the tree.  If
		 * this is the first time through we start at the root and
		 * work down.  Otherwise we're sitting at the beginning or
		 * middle of a non-leaf-level node and start from there.
		 */
		do {
			level++;
			nodebuf = snapread(sb, level? path[level - 1].pnext++->sector: sb->image.etree_root);
			if (!nodebuf) {
				warn("unable to read node at sector 0x%Lx at level %d of tree traversal",
					level? path[level - 1].pnext++->sector: sb->image.etree_root, level);
				return -EIO;
			}
			node = buffer2node(nodebuf);
			path[level].buffer = nodebuf;
			path[level].pnext = node->entries;
			trace(printf("push to level %i, %i nodes\n", level, node->count););
		} while (level < levels - 1);

		trace(printf("do %i leaf nodes, level = %i\n", node->count, level););
		/*
		 * Process the leaves in this node.  Call the passed function
		 * for each.
		 */
		while (path[level].pnext < node->entries + node->count) {
			leafbuf = snapread(sb, path[level].pnext++->sector);
			if (!leafbuf) {
				warn("unable to read leaf at sector 0x%Lx of tree traversal",
					level? path[level - 1].pnext++->sector: sb->image.etree_root);
				return -EIO;
			}
start:
			trace(printf("process leaf %Lx\n", leafbuf->sector););
			visit_leaf(sb, buffer2leaf(leafbuf), data);

			brelse(leafbuf);
		}
		/*
		 * We're finished with this node, pop the stack to the next
		 * one.  Keep popping as long as we're at the end of a node.
		 * If we get all the way to the root, we're done.
		 */
		do {
			brelse(nodebuf);
			if (!level)
				return 0;
			nodebuf = path[--level].buffer;
			node = buffer2node(nodebuf);
			trace(printf("pop to level %i, %i of %i nodes\n", level, path[level].pnext - node->entries, node->count););
		} while (path[level].pnext == node->entries + node->count);
	}
}

/*
 * btree debug dump
 */

static void show_leaf_range(struct eleaf *leaf, chunk_t start, chunk_t finish)
{
	for (int i = 0; i < leaf->count; i++) {
		chunk_t addr = leaf->map[i].rchunk;
		if (addr >= start && addr <= finish) {
			printf("Addr %Lu: ", (unsigned long long) addr);
			for (struct exception *p = emap(leaf, i); p < emap(leaf, i+1); p++)
				printf("%Lx/%08llx, ", p->chunk, p->share);
			printf("\n");
		}
	}
}

#if 0
static void show_leaf(struct eleaf *leaf)
{
	show_leaf_range(leaf, 0, -1);
}
#endif

static void show_subtree_range(struct superblock *sb, struct enode *node, chunk_t start, chunk_t finish, int levels, int indent)
{
	int i;

	for (i = 0; i < node->count; i++) {
		struct buffer *buffer = snapread(sb, node->entries[i].sector);
		if (levels)
			show_subtree_range(sb, buffer2node(buffer), start, finish, levels - 1, indent + 3);
		else {
			show_leaf_range(buffer2leaf(buffer), start, finish);
		}
		brelse(buffer);
	}
}

static void show_tree_range(struct superblock *sb, chunk_t start, chunk_t finish)
{
	struct buffer *buffer = snapread(sb, sb->image.etree_root);
	if (!buffer)
		return;
	show_subtree_range(sb, buffer2node(buffer), start, finish, sb->image.etree_levels - 1, 0);
	brelse(buffer);
}

#ifdef SHOW_HELPERS
static void show_tree(struct superblock *sb)
{
	show_tree_range(sb, 0, -1);
}
#endif

static void check_leaf(struct eleaf *leaf, u64 snapmask)
{
	struct exception *p;
	int i;

	for (i = 0; i < leaf->count; i++) {
		trace(printf("%x=", leaf->map[i].rchunk););
		// printf("@%i ", leaf->map[i].offset);
		for (p = emap(leaf, i); p < emap(leaf, i+1); p++) {
			// !!! should also check for any zero sharemaps here
			trace(printf("%Lx/%08llx%s", p->chunk, p->share, p+1 < emap(leaf, i+1)? ",": " "););
			if (p->share & snapmask)
				printf("nonzero bits %016Lx outside snapmask %016Lx\n", p->share, snapmask);
		}
	}
	// printf("top@%i", leaf->map[i].offset);
}

#define MAX_BTREE_DIRTY	10 // max dirty buffers generated in a btree operation, depends on how bushy the btree is

/*
 * Check if the number of dirty buffers meets or exceeds the size of the 
 * journal itself less the maximum number of dirty B-Tree buffers (i.e. 
 * reserving enough space for B-Tree operations), commit the current 
 * transaction, thereby flushing the buffer cache.
 */
static void dirty_buffer_count_check(struct superblock *sb)
{
	if ((int)dirty_buffer_count >= (int)(sb->image.journal_size - MAX_BTREE_DIRTY)) {
		if (dirty_buffer_count > sb->image.journal_size) {
			warn("number of dirty buffers %d is too large for journal %u", dirty_buffer_count, sb->image.journal_size);
			abort();
		}
		commit_transaction(sb, 0);
	}
}

/* FIXME: the structure is only used to pass info between two functions */
struct delete_info
{
	u64 snapmask;
	u64 any;
};

/*
 * Remove all exceptions belonging to a given snapshot from the passed leaf.
 *
 * This clears the "share" bits on each chunk for the snapshot mask passed
 * in the delete_info structure.  In the process, it compresses out any
 * exception entries that are entirely unshared and/or unused.  In a second
 * pass, it compresses out any map entries for which there are no exception
 * entries remaining.
 */
static void _delete_snapshots_from_leaf(struct superblock *sb, struct eleaf *leaf, void *data)
{
	struct delete_info *dinfo = data;
	/* p points just past the last map[] entry in the leaf. */
	struct exception *p = emap(leaf, leaf->count), *dest = p;
	struct etree_map *pmap, *dmap;
	unsigned i;

	dinfo->any = 0;

	/* Scan top to bottom clearing snapshot bit and moving
	 * non-zero entries to top of block */
	/*
	 * p points at each original exception; dest points at the location
	 * to receive an exception that is being moved down in the leaf.
	 * Exceptions that are unshared after clearing the share bit for
	 * the passed snapshot mask are skipped and the associated "exception"
	 * chunk is freed.  This operates on the exceptions for one map entry
	 * at a time; when the beginning of a list of exceptions is reached,
	 * the associated map entry offset is adjusted.
	 */
	for (i = leaf->count; i--;) {
		/*
		 * False the first time through, since i is leaf->count and p
		 * was set to emap(leaf, leaf->count) above.
		 */
		while (p != emap(leaf, i)) {
			u64 share = (--p)->share;
			dinfo->any |= share & dinfo->snapmask;
					/* Unshare with given snapshot(s).    */
			p->share &= ~dinfo->snapmask;
			if (p->share)	/* If still used, keep chunk.         */
				*--dest = *p;
			else
				free_exception(sb, p->chunk);
			dirty_buffer_count_check(sb);
		}
		leaf->map[i].offset = (char *)dest - (char *)leaf;
	}
	/* Remove empties from map */
	/*
	 * This runs through the map entries themselves, skipping entries
	 * with matching offsets.  If all the exceptions for a given map
	 * entry are skipped, its offset will be set to that of the following
	 * map entry (since the dest pointer will not have moved).
	 */
	dmap = pmap = &leaf->map[0];
	for (i = 0; i < leaf->count; i++, pmap++)
		if (pmap->offset != (pmap + 1)->offset)
			*dmap++ = *pmap;
	/*
	 * There is always a phantom map entry after the last, that has the
	 * offset of the end of the leaf and, of course, no chunk number.
	 */
	dmap->offset = pmap->offset;
	dmap->rchunk = 0; // tidy up
	leaf->count = dmap - &leaf->map[0];
	check_leaf(leaf, dinfo->snapmask);
}

static int delete_snapshots_from_leaf(struct superblock *sb, struct eleaf *leaf, u64 snapmask)
{
	struct delete_info dinfo;

	dinfo.snapmask = snapmask;

	_delete_snapshots_from_leaf(sb, leaf, &dinfo);

	return !!dinfo.any;
}

/*
 * Delete algorithm (flesh this out)
 *
 * reached the end of an index block:
 *    try to merge with an index block in hold[]
 *    if can't merge then maybe can rebalance
 *    if can't merge then release the block in hold[] and move this block to hold[]
 *    can't merge if there's no block in hold[] or can't fit two together
 *    if can merge
 *       release and free this index block and
 *       delete from parent:
 *         if parent count zero, the grantparent key is going to be deleted, updating the pivot
 *         otherwise parent's deleted key becomes new pivot 
 *
 * Good luck understanding this.  It's complicated because it's complicated.
 */

/*
 * Return true if path[level].pnext points at the end of the list of index
 * entries.
 */
static inline int finished_level(struct etree_path path[], int level)
{
	struct enode *node = path_node(path, level);
	return path[level].pnext == node->entries + node->count;
}

/*
 * Remove the index entry at path[level].pnext-1 by moving entries below it up
 * into its place.  If it wasn't the last entry in the node but it _was_ the
 * first entry (and we're not at the root), preserve the key by inserting it
 * into the index entry of the parent node that refers to this node.
 */
static void remove_index(struct etree_path path[], int level)
{
	struct enode *node = path_node(path, level);
	chunk_t pivot = (path[level].pnext)->key; // !!! out of bounds for delete of last from full index
	int count = node->count, i;

	// stomps the node count (if 0th key holds count)
	memmove(path[level].pnext - 1, path[level].pnext,
		(char *)&node->entries[count] - (char *)path[level].pnext);
	node->count = count - 1;
	--(path[level].pnext);
	set_buffer_dirty(path[level].buffer);

	// no pivot for last entry
	if (path[level].pnext == node->entries + node->count)
		return;

	// climb up to common parent and set pivot to deleted key
	// what if index is now empty? (no deleted key)
	// then some key above is going to be deleted and used to set pivot
	if (path[level].pnext == node->entries && level) {
		/* Keep going up the path if we're at the first index entry. */
		for (i = level - 1; path[i].pnext - 1 == path_node(path, i)->entries; i--)
			if (!i)		/* If we hit the root, we're done.    */
				return;
		/*
		 * Found a node where we're not at the first entry.  Set the
		 * key here to that of the deleted index entry.
		 */
		(path[i].pnext - 1)->key = pivot;
		set_buffer_dirty(path[i].buffer);
	}
}

/*
 * Release the passed buffer and free the block it contains.
 */
static void brelse_free(struct superblock *sb, struct buffer *buffer)
{
	brelse(buffer);
	if (buffer->count) {
		warn("free block %Lx still in use!", (long long)buffer->sector);
		return;
	}
	free_block(sb, buffer->sector);
	set_buffer_empty(buffer);
}

/*
 * Delete all chunks in the B-tree for the snapshot(s) indicated by the
 * passed snapshot mask, beginning at the passed chunk.
 *
 * Walk the tree (a stack-based inorder traversal) starting with the passed
 * chunk, calling delete_snapshots_from_leaf() on each leaf to remove chunks
 * associated with the snapshot(s) we're removing.  As leaves and nodes become
 * sparsely filled, merge them with their neighbors.  When we reach the root
 * we've finished the traversal; if there are empty levels (that is, level(s)
 * directly below the root that only contain a single node), remove those
 * empty levels until either the second level is no longer empty or we only
 * have one level remaining.
 */
static int delete_tree_range(struct superblock *sb, u64 snapmask, chunk_t resume)
{
	int levels = sb->image.etree_levels, level = levels - 1;
	struct etree_path path[levels], hold[levels];
	struct buffer *leafbuf, *prevleaf = NULL;
	unsigned i;

	for (i = 0; i < levels; i++) // can be initializer if not dynamic array (change it?)
		hold[i] = (struct etree_path){ };
	/*
	 * Find the B-tree leaf with the chunk we were passed.  Often this
	 * will be chunk 0.
	 */
	if (!(leafbuf = probe(sb, resume, path)))
		return -ENOMEM;

	commit_transaction(sb, 0);
	while (1) { /* in-order leaf walk */
		trace_off(show_leaf(buffer2leaf(leafbuf)););
		if (delete_snapshots_from_leaf(sb, buffer2leaf(leafbuf), snapmask))
			set_buffer_dirty(leafbuf);
		/*
		 * If we have a previous leaf (i.e. we're past the first),
		 * try to merge the current leaf with it.
		 */
		if (prevleaf) { /* try to merge this leaf with prev */
			struct eleaf *this = buffer2leaf(leafbuf);
			struct eleaf *prev = buffer2leaf(prevleaf);
			trace_off(warn("check leaf %p against %p", leafbuf, prevleaf););
			trace_off(warn("need = %i, free = %i", leaf_payload(this), leaf_freespace(prev)););
			/*
			 * If there's room in the previous leaf for this leaf,
			 * merge this leaf into the previous leaf and remove
			 * the index entry that points to this leaf.
			 */
			if (leaf_payload(this) <= leaf_freespace(prev)) {
				trace_off(warn(">>> can merge leaf %p into leaf %p", leafbuf, prevleaf););
				merge_leaves(prev, this);
				remove_index(path, level);
				set_buffer_dirty(prevleaf);
				brelse_free(sb, leafbuf);
				dirty_buffer_count_check(sb);
				goto keep_prev_leaf;
			}
			brelse(prevleaf);
		}
		prevleaf = leafbuf;	/* Save leaf for next time through.   */
keep_prev_leaf:
		/*
		 * If we've reached the end of the index entries in the B-tree
		 * node at the current level, try to merge the node referred
		 * to at this level of the path with a prior node.  Repeat
		 * this process at successively higher levels up the path; if
		 * we reach the root, clean up and exit.  If we don't reach
		 * the root, we've reached a node with multiple entries;
		 * rebuild the path from the next index entry to the next
		 * leaf.
		 */
		if (finished_level(path, level)) {
			do { /* pop and try to merge finished nodes */
				/*
				 * If we have a previous node at this level
				 * (again, we're past the first node), try to
				 * merge the current node with it.
				 */
				if (hold[level].buffer) {
					assert(level); /* root node can't have any prev */
					struct enode *this = path_node(path, level);
					struct enode *prev = path_node(hold, level);
					trace_off(warn("check node %p against %p", this, prev););
					trace_off(warn("this count = %i prev count = %i", this->count, prev->count););
					/*
					 * If there's room in the previous node
					 * for the entries in this node, merge
					 * this node into the previous node and
					 * remove the index entry that points
					 * to this node.
					 */
					if (this->count <= sb->metadata.alloc_per_node - prev->count) {
						trace(warn(">>> can merge node %p into node %p", this, prev););
						merge_nodes(prev, this);
						remove_index(path, level - 1);
						set_buffer_dirty(hold[level].buffer);
						brelse_free(sb, path[level].buffer);
						dirty_buffer_count_check(sb);
						goto keep_prev_node;
					}
					brelse(hold[level].buffer);
				}
				/* Save the node for the next time through.   */
				hold[level].buffer = path[level].buffer;
keep_prev_node:
				/*
				 * If we're at the root, we need to clean up
				 * and return.  First, though, try to reduce
				 * the number of levels.  If the tree at the
				 * root has been reduced to only the nodes in
				 * our path, eliminate nodes with only one
				 * entry until we either have a new root node
				 * with multiple entries or we have only one
				 * level remaining in the B-tree.
				 */
				if (!level) { /* remove levels if possible */
					/*
					 * While above the first level and the
					 * root only has one entry, point the
					 * root at the (only) first-level node,
					 * reduce the number of levels and
					 * shift the path up one level.
					 */
					while (levels > 1 && path_node(hold, 0)->count == 1) {
						trace_off(warn("drop btree level"););
						/* Point root at the first level. */
						sb->image.etree_root = hold[1].buffer->sector;
						brelse_free(sb, hold[0].buffer);
						dirty_buffer_count_check(sb);
						levels = --sb->image.etree_levels;
						memcpy(hold, hold + 1, levels * sizeof(hold[0]));
						set_sb_dirty(sb);
					}
					brelse(prevleaf);
					brelse_path(hold, levels);
					if (dirty_buffer_count)
						commit_transaction(sb, 0);
					sb->snapmask &= ~snapmask;
					set_sb_dirty(sb);
					save_sb(sb); /* we don't call save_state after a squash */
					return 0;
				}

				level--;
				trace_off(printf("pop to level %i, %i of %i nodes\n", level, path[level].pnext - path_node(path, level)->entries, path_node(path, level)->count););
			} while (finished_level(path, level));
			/*
			 * Now rebuild the path from where we are (one entry
			 * past the last leaf we processed, which may have
			 * been adjusted in operations above) down to the node
			 * above the next leaf.
			 */
			do { /* push back down to leaf level */
				struct buffer *nodebuf = snapread(sb, path[level++].pnext++->sector);
				if (!nodebuf) {
					brelse_path(path, level - 1); /* anything else needs to be freed? */
					return -ENOMEM;
				}
				path[level].buffer = nodebuf;
				path[level].pnext = buffer2node(nodebuf)->entries;
				trace_off(printf("push to level %i, %i nodes\n", level, path_node(path, level)->count););
			} while (level < levels - 1);
		}

		dirty_buffer_count_check(sb);
		/*
		 * Get the leaf indicated in the next index entry in the node
		 * at this level.
		 */
		if (!(leafbuf = snapread(sb, path[level].pnext++->sector))) {
			brelse_path(path, level);
			return -ENOMEM;		
		}
	}
}

/*
 * Find the given snapshot tag in the list of snapshots in the superblock.
 * Return a pointer to that snapshot entry.
 */
static struct snapshot *find_snap(struct superblock *sb, u32 tag)
{
	struct snapshot *snapshot = sb->image.snaplist;
	struct snapshot *end = snapshot + sb->image.snapshots;

	for (; snapshot < end; snapshot++)
		if (snapshot->tag == tag)
			return snapshot;
	return NULL;
}

static inline int is_squashed(const struct snapshot *snapshot)
{
	return snapshot->bit == SNAPSHOT_SQUASHED;
}

/* usecount() calculates and returns the usecount for a given snapshot.
 * The persistent (on disk) usecount is added to the transient usecount
 * (for devices using the snapshot). */
static inline u16 usecount(struct superblock *sb, struct snapshot *snap)
{
	return (is_squashed(snap) ? 0 : sb->usecount[snap->bit]) + snap->usecount;
}

/* find the oldest snapshot with 0 usecnt and lowest priority.
 * if no such snapshot exists, find the snapshot with lowest priority
 */
static struct snapshot *find_victim(struct superblock *sb)
{
	struct snapshot *snaplist = sb->image.snaplist;
	u32 snapshots = sb->image.snapshots;

	assert(snapshots);
	struct snapshot *snap, *best = snaplist;

	for (snap = snaplist + 1; snap < snaplist + snapshots; snap++) {
		if (is_squashed(snap))
			continue;
		if (!is_squashed(best) && (usecount(sb, snap) && !usecount(sb, best)))
			continue;
		if (!is_squashed(best) && (!usecount(sb, snap) == !usecount(sb, best)) && (snap->prio >= best->prio))
			continue;
		best = snap;
	}
	return best;
}

/*
 * Delete the passed snapshot.
 */
static int delete_snap(struct superblock *sb, struct snapshot *snap)
{
	trace_on(warn("Delete snaptag %u (snapnum %i)", snap->tag, snap->bit););
	u64 mask;
	if (is_squashed(snap))
		mask = 0;
	else {
		mask = 1ULL << snap->bit;
		sb->usecount[snap->bit] = 0; // reset transient usecount for this bit during auto-deletion
	}
	/* Compress the snapshot entry out of the list. */
	memmove(snap, snap + 1, (char *)(sb->image.snaplist + --sb->image.snapshots) - (char *)snap);
	set_sb_dirty(sb);
	if (!mask) {
		trace_on(warn("snapshot squashed, skipping tree delete"););
		return 0;
	}
	commit_deferred_allocs(sb);
	return delete_tree_range(sb, mask, 0);
}

/*
 * Snapshot Store Allocation
 */

/*
 * Allocate a chunk from the passed allocation space.
 *
 * The search for a free chunk is optimized by starting from the position of the
 * last allocation and searching forward to the end of the allocation space.  If
 * no free chunk is found, then search from the beginning of the space to the
 * last allocation position.  Fail if both searches turn up nothing.
 */
static chunk_t alloc_chunk(struct superblock *sb, struct allocspace *as)
{
	chunk_t last = as->asi->last_alloc, total = as->asi->chunks, found;

	if ((found = alloc_chunk_from_range(sb, as, last, total - last)) != -1 ||
	    (found = alloc_chunk_from_range(sb, as, 0, last)) != -1) {
		as->asi->last_alloc = found;
		set_sb_dirty(sb);
		return found;
	}
	warn("failed to allocate chunk");
	return -1;
}

/*
 * Both alloc_metablock() and alloc_snapblock() call alloc_chunk() to allocate
 * a chunk.  The new_block() function calls alloc_metablock() to allocate a
 * block from the metadata allocation, then calls getblk() to allocate a
 * buffer for that block.  Both new_leaf() and new_node() call new_block()
 * for their buffer.  The chunk allocation functions here return either the
 * newly-allocated chunk number or -1 if the allocation failed.
 * new_block() returns NULL for failure.
 *
 * Note that alloc_metablock() is only called here.  alloc_snapblock() is
 * called elsewhere (make_unique()) to allocate snapshot blocks.
 */
static chunk_t alloc_metablock(struct superblock *sb)
{
	return alloc_chunk(sb, &sb->metadata);
}

static u64 alloc_snapblock(struct superblock *sb)
{
	return alloc_chunk(sb, &sb->snapdata);
}

static struct buffer *new_block(struct superblock *sb)
{
	chunk_t newchunk;

	if ((newchunk = alloc_metablock(sb)) == -1)
		return NULL;
	return getblk(sb->metadev, newchunk << sb->metadata.chunk_sectors_bits, sb->metadata.allocsize);
}

/*
 * Get a new buffer for an eleaf, zero it and initialize it appropriately.
 */
static struct buffer *new_leaf(struct superblock *sb)
{
	trace(printf("New leaf\n"););
	struct buffer *buffer = new_block(sb); 
	if (!buffer)
		return NULL;
	memset(buffer->data, 0, sb->metadata.allocsize);
	init_leaf(buffer2leaf(buffer), sb->metadata.allocsize);
	set_buffer_dirty(buffer);
	return buffer;
}

/*
 * Get a new buffer for an enode, zero it and set the node count to zero.
 */
static struct buffer *new_node(struct superblock *sb)
{
	trace(printf("New node\n"););
	struct buffer *buffer = new_block(sb); 
	if (!buffer)
		return buffer;
	memset(buffer->data, 0, sb->metadata.allocsize);
	struct enode *node = buffer2node(buffer);
	node->count = 0;
	set_buffer_dirty(buffer);
	return buffer;
}

/*
 * Generate list of chunks not shared between two snapshots
 */

/* Danger Must Fix!!! ddsnapd can block waiting for ddsnap.c, which may be blocked on IO => deadlock */

struct gen_changelist
{
	u64 mask1;
	u64 mask2;
	struct change_list *cl;
};

static void gen_changelist_leaf(struct superblock *sb, struct eleaf *leaf, void *data)
{
	u64 mask1 = ((struct gen_changelist *)data)->mask1;
	u64 mask2 = ((struct gen_changelist *)data)->mask2;
	struct change_list *cl = ((struct gen_changelist *)data)->cl;
	struct exception const *p;
	u64 newchunk;
	int i;
	u32 snap = cl->tgt_snap;
	struct snapshot const *snaplist = sb->image.snaplist;
	u64 snap_sectors = 0;

	for (i = 0; i < sb->image.snapshots; i++)
		if (snaplist[i].tag == snap)
			snap_sectors = snaplist[i].sectors;
	if (snap_sectors == 0) {
		warn("unable to get snapshot sectors");
		return;
	}
	for (i = 0; i < leaf->count; i++)
		for (p = emap(leaf, i); p < emap(leaf, i+1); p++) {
			if ( ((p->share & mask2) == mask2) != ((p->share & mask1) == mask1) ) {
				newchunk = leaf->base_chunk + leaf->map[i].rchunk;
				/* check if the chunk is within the size of the target snapshot
				 * to deal with origin device shrinking */
				if ((newchunk << sb->snapdata.chunk_sectors_bits) >= snap_sectors)
					continue;
				if (append_change_list(cl, newchunk) < 0)
					warn("unable to write chunk %Li to changelist", newchunk);
				break;
			}
		}
}

/*
 * High Level BTree Editing
 */

/*
 * BTree insertion is a little hairy, as expected.  We keep track of the
 * access path in a vector of etree_path elements, each of which holds
 * a node buffer and a pointer into the buffer data giving the address at
 * which the next buffer in the path was found, which is also where a new
 * node will be inserted if necessary.  If a leaf is split we may need to
 * work all the way up from the bottom to the top of the path, splitting
 * index nodes as well.  If we split the top index node we need to add
 * a new tree level.  We have to keep track of which nodes were modified
 * and keep track of refcounts of all buffers involved, which can be quite
 * a few.
 *
 * Note that the first key of an index block is never accessed.  This is
 * because for a btree, there is always one more key than nodes in each
 * index node.  In other words, keys lie between node pointers.  We will
 * micro-optimize by placing the node count in the first key, which allows
 * a node to contain an esthetically pleasing binary number of pointers.
 * (Not done yet.)
 */
/*
 * Insert a child into an enode.
 *
 * Move all entries from here to the end of the index entries down one entry,
 * thereby freeing the current entry.  Fill that with the information for our
 * new child.
 */
static void insert_child(struct enode *node, struct index_entry *p, sector_t child, u64 childkey)
{
	memmove(p + 1, p, (char *)(&node->entries[0] + node->count) - (char *)p);
	p->sector = child;
	p->key = childkey;
	node->count++;
}

/*
 * Add an exception to the B-tree.
 *
 * This routine calls add_exception_to_leaf() to add the passed exception to
 * the leaf.  If that fails, we split the leaf and add the exception to the
 * appropriate leaf of the pair.  We then add the new leaf to the enode,
 * splitting it (and any parents) if necessary.  In the degenerate case, we
 * split enodes all the way up the etree path until we create a new root at
 * the top.
 *
 * Returns 0 on success and -errno on failure.
 */
static int add_exception_to_tree(struct superblock *sb, struct buffer *leafbuf, u64 target, u64 exception, int snapbit, struct etree_path path[], unsigned levels)
{
	/*
	 * Try to add the exception to the leaf we already have in hand.  If
	 * that works, we're done.
	 */
	if (!add_exception_to_leaf(buffer2leaf(leafbuf), target, exception, snapbit, sb->snapmask)) {
		brelse_dirty(leafbuf);
		return 0;
	}
	/*
	 * There wasn't room to add a new exception to the leaf.  Split it.
	 */
	trace(warn("adding a new leaf to the tree"););
	struct buffer *childbuf = new_leaf(sb);
	if (!childbuf) 
		return -ENOMEM; /* this is the right thing to do? */
	
	u64 childkey = split_leaf(buffer2leaf(leafbuf), buffer2leaf(childbuf));
	sector_t childsector = childbuf->sector;
	/*
	 * Now add the exception to the appropriate leaf.  Childkey has the
	 * first chunk in the new leaf we just created.
	 */
	if (add_exception_to_leaf(target < childkey ? buffer2leaf(leafbuf): buffer2leaf(childbuf), target, exception, snapbit, sb->snapmask)) {
		warn("new leaf has no space");
		return -ENOMEM;
	}
	brelse_dirty(leafbuf);
	brelse_dirty(childbuf);

	while (levels--) {
		struct index_entry *pnext = path[levels].pnext;
		struct buffer *parentbuf = path[levels].buffer;
		struct enode *parent = buffer2node(parentbuf);

		/*
		 * If there's room in this enode, insert the child and we're
		 * done.
		 */
		if (parent->count < sb->metadata.alloc_per_node) {
			insert_child(parent, pnext, childsector, childkey);
			set_buffer_dirty(parentbuf);
			return 0;
		}
		/*
		 * Split the node.
		 */
		unsigned half = parent->count / 2;
		u64 newkey = parent->entries[half].key;
		struct buffer *newbuf = new_node(sb); 
		if (!newbuf) 
			return -ENOMEM;
		struct enode *newnode = buffer2node(newbuf);

		newnode->count = parent->count - half;
		memcpy(&newnode->entries[0], &parent->entries[half], newnode->count * sizeof(struct index_entry));
		parent->count = half;
		/*
		 * If the path entry is in the new node, use that as the
		 * parent.
		 */
		if (pnext > &parent->entries[half]) {
			pnext = pnext - &parent->entries[half] + newnode->entries;
			set_buffer_dirty(parentbuf);
			parentbuf = newbuf;
			parent = newnode;
		} else set_buffer_dirty(newbuf);
		/*
		 * Insert the child now that we have room in the parent, then
		 * climb the path and insert the new child there.
		 */
		insert_child(parent, pnext, childsector, childkey);
		set_buffer_dirty(parentbuf);
		childkey = newkey;
		childsector = newbuf->sector;
		brelse(newbuf);
	}
	/*
	 * If we get here, we've added a node at every level up the path to
	 * the root.  This means that we have to add a new level and make a
	 * new root.  Do so and point it at the pair of nodes that are now
	 * at the second level (i.e. the old root and the new node).  Point
	 * the superblock at the new root.
	 */
	trace(printf("add tree level\n"););
	struct buffer *newrootbuf = new_node(sb);
	if (!newrootbuf)
		return -ENOMEM;
	struct enode *newroot = buffer2node(newrootbuf);

	newroot->count = 2;
	newroot->entries[0].sector = sb->image.etree_root;
	newroot->entries[1].key = childkey;
	newroot->entries[1].sector = childsector;
	sb->image.etree_root = newrootbuf->sector;
	sb->image.etree_levels++;
	set_sb_dirty(sb);
	brelse_dirty(newrootbuf);
	return 0;
}

#define chunk_highbit ((sizeof(chunk_t) * 8) - 1)

/*
 * Actually perform a "copyout" operation.
 *
 * If a copyout operation is pending (as set up by copyout(), below), this
 * routine actually does the copy via calls to diskread() and diskwrite().
 */
static int finish_copyout(struct superblock *sb)
{
	if (sb->copy_chunks) {
		int is_snap = sb->source_chunk >> chunk_highbit;
		chunk_t source = sb->source_chunk & ~(1ULL << chunk_highbit);
		unsigned size = sb->copy_chunks << sb->snapdata.asi->allocsize_bits;
		trace(printf("copy %u %schunks from %Lx to %Lx\n", sb->copy_chunks,
			is_snap? "snapshot ": "origin ", source, sb->dest_exception););
		assert(size <= sb->copybuf_size);
		if (diskread(is_snap? sb->snapdev: sb->orgdev, sb->copybuf, size,
			source << sb->snapdata.asi->allocsize_bits) < 0)
			trace(printf("copyout death on read\n"););
		if (diskwrite(sb->snapdev, sb->copybuf, size,
			sb->dest_exception << sb->snapdata.asi->allocsize_bits) < 0)
			trace_on(printf("copyout death on write\n"););
		sb->copy_chunks = 0;
	}
	return 0;
}

/*
 * Handle the "copyout" operation.
 *
 * When a shared chunk is changed, the original contents must be copied from
 * the origin or from the snapshot in which it changed to (a new location in)
 * the snapshot store before the change takes place.  This routine stores the
 * latest such operation in the superblock.  As an optimization, it also
 * accumulates consecutive chunks so that they may be copied together.
 */
static int copyout(struct superblock *sb, chunk_t chunk, chunk_t exception)
{
#if 1
	/*
	 * If this is the next consecutive chunk, it's mapped to the next
	 * consecutive snapshot chunk _and_ it will fit in the copy buffer,
	 * just bump the count; we'll copy the whole set together.
	 */
	if (sb->source_chunk + sb->copy_chunks == chunk &&
		sb->dest_exception + sb->copy_chunks == exception &&
		sb->copy_chunks < sb->copybuf_size >> sb->snapdata.asi->allocsize_bits) {
		sb->copy_chunks++;
		return 0;
	}
	/*
	 * If there's a pending copyout and this one doesn't meet the above
	 * criteria, perform the previous copyout and start a new one.
	 */
	finish_copyout(sb);
	sb->copy_chunks = 1;
	sb->source_chunk = chunk;
	sb->dest_exception = exception;
#else
	int is_snap = sb->source_chunk >> chunk_highbit;
	chunk_t source = chunk & ~((1ULL << chunk_highbit) - 1);
	diskread(is_snap? sb->snapdev: sb->orgdev, sb->copybuf, sb->snapdata.allocsize, 
		 source << sb->snapdata.asi->allocsize_bits);  
	diskwrite(sb->snapdev, sb->copybuf, sb->snapdata.allocsize, exception << sb->snapdata.asi->allocsize_bits);  
#endif
	return 0;
}

/*
 * Select a victim snapshot and delete/squash it. Called when the snapshot 
 * store is full or when we reach the maximum number of snapshots.
 */
static int auto_delete_snapshot(struct superblock *sb)
{
	struct snapshot *victim = find_victim(sb);
	int err;
	if (is_squashed(victim) || victim->prio == 127) {
		/* All snapshots deleted, check for lost chunks */
		if (sb->image.snapdata.freechunks < sb->image.snapdata.chunks ) {
			warn("%Li free data chunks after all snapshots deleted", sb->image.snapdata.freechunks);
			sb->image.snapdata.freechunks = count_free(sb, &sb->snapdata);
		}
		if (!combined(sb)) {
			/* reserved meta data + bitmap_blocks + super_block */
			// !!! this is also used in initialization, make this a common function
			unsigned meta_bitmap_base_chunk = (SB_SECTOR + 2*chunk_sectors(&sb->metadata) - 1) >> sb->metadata.chunk_sectors_bits;
			unsigned reserved = meta_bitmap_base_chunk + sb->metadata.asi->bitmap_blocks + sb->image.journal_size
				+ sb->metadata.asi->bitmap_blocks + 1
				+ sb->snapdata.asi->bitmap_blocks;
			if (sb->image.metadata.freechunks + reserved < sb->image.metadata.chunks) {
				warn("%Li free metadata chunks after all snapshots deleted", sb->image.metadata.freechunks);
				sb->image.metadata.freechunks = count_free(sb, &sb->metadata);
			}
		}
		return -1;
	}
	warn("releasing snapshot %u", victim->tag);
	if (usecount(sb, victim)) {
		commit_deferred_allocs(sb);
		err = delete_tree_range(sb, 1ULL << victim->bit, 0);
		sb->usecount[victim->bit] = 0;
		victim->bit = SNAPSHOT_SQUASHED;
	} else
		err = delete_snap(sb, victim);
	return err;
}

static int ensure_free_chunks(struct superblock *sb, struct allocspace *as, int chunks)
{
	do {
		if (as->asi->freechunks >= chunks)
			return 0;
		if (auto_delete_snapshot(sb))
			goto fail_delete;
	} while (sb->image.snapshots);

fail_delete:
	warn("snapshot delete failed");
	return -1;
}

/*
 * This is the bit that does all the work.  It's rather arbitrarily
 * factored into a probe and test part, then an exception add part,
 * called only if an exception for a given chunk isn't already present
 * in the Btree.  This factoring will change a few more times yet as
 * the code gets more asynchronous and multi-threaded.  This is a hairball
 * and needs a rewrite.
 */
/*
 * This creates "exceptions" to the rule that all chunks go to the origin.
 * If any snapshot has not had the chunk copied to it, this routine performs
 * that task.  It returns zero if no copies took place (that is, "exceptions"
 * for this chunk already existed for all snapshots), nonzero otherwise.  A
 * return of -1 indicates an error that caused make_unique() to fail.
 */
static chunk_t make_unique(struct superblock *sb, chunk_t chunk, int snapbit)
{
	chunk_t exception = 0;
	int error;
	trace(warn("chunk %Lx, snapbit %i", chunk, snapbit););

	/* first we check if we will have enough freespace */
	if (combined(sb)) {
		if (ensure_free_chunks(sb, &sb->metadata, MAX_NEW_METACHUNKS + 1))
			return -1;
	} else { /* separate */
		if (ensure_free_chunks(sb, &sb->metadata, MAX_NEW_METACHUNKS))
			return -1;
		if (ensure_free_chunks(sb, &sb->snapdata, 1))
			return -1;
	}

	unsigned levels = sb->image.etree_levels;
	struct etree_path path[levels + 1];
	/*
	 * Find the proper leaf for this chunk.  The "path" gets the list of
	 * B-tree nodes that lead to the returned leaf.
	 */
	struct buffer *leafbuf = probe(sb, chunk, path);
	if (!leafbuf) 
		return -1;

	/*
	 * If snapbit is -1, we're doing I/O to the origin.  The snapmask
	 * field, here, is a bitmask of all valid snapshots for the volume.
	 */
	if (snapbit == -1?
		origin_chunk_unique(buffer2leaf(leafbuf), chunk, sb->snapmask):
		snapshot_chunk_unique(buffer2leaf(leafbuf), chunk, snapbit, &exception))
	{
		trace_off(warn("chunk %Lx already unique in snapnum %i", chunk, snapbit););
		brelse(leafbuf);
		goto out;
	}
	u64 newex = alloc_snapblock(sb);
	if (newex == -1) {
		// count_free
		error("we should count free bits here and try to get the accounting right");
	}; /* if this broke, then our ensure above is broken */

	copyout(sb, exception? (exception | (1ULL << chunk_highbit)): chunk, newex);
	if ((error = add_exception_to_tree(sb, leafbuf, chunk, newex, snapbit, path, levels)) < 0) {
		free_exception(sb, newex);
		brelse(leafbuf); /* !!! redundant? */
		warn("unable to add exception to tree: %s", strerror(-error));
		newex = -1;
	}
	exception = newex;
out:
	brelse_path(path, levels);
	trace(warn("returning exception: %Lx", exception););
	return exception;
}

/*
 * Find the chunk in the b-tree corresponding to the passed chunk and return
 * an indication of whether or not it is shared.
 */
static int test_unique(struct superblock *sb, chunk_t chunk, int snapbit, chunk_t *exception)
{
	unsigned levels = sb->image.etree_levels;
	struct etree_path path[levels + 1];
	struct buffer *leafbuf = probe(sb, chunk, path);
	
	if (!leafbuf)
		return -1; /* not sure what to do here */
	
	trace(warn("chunk %Lx, snapbit %i", chunk, snapbit););
	int result = snapbit == -1?
		origin_chunk_unique(buffer2leaf(leafbuf), chunk, sb->snapmask):
		snapshot_chunk_unique(buffer2leaf(leafbuf), chunk, snapbit, exception);
	brelse(leafbuf);
	brelse_path(path, levels);
	return result;
}

/* Snapshot Store Superblock handling */

/*
 * Calculate a bitmask from the list of existing snapshots.
 */
static u64 calc_snapmask(struct superblock *sb)
{
	u64 mask = 0;
	unsigned int i;

	for (i = 0; i < sb->image.snapshots; i++)
		if (!is_squashed(&sb->image.snaplist[i]))
			mask |= 1ULL << sb->image.snaplist[i].bit;

	return mask;
}

#if 0
static int tag_snapbit(struct superblock *sb, unsigned tag)
{
	struct snapshot *snapshot = find_snap(sb, tag);
	return snapshot ? snapshot->bit : -1;
}

static unsigned int snapbit_tag(struct superblock *sb, unsigned bit)
{
	unsigned int i, n = sb->image.snapshots;
	struct snapshot const *snap = sb->image.snaplist;

	for (i = 0; i < n; i++)
		if (snap[i].bit == bit)
			return snap[i].tag;

	return (u32)~0UL;
}
#endif

/* find the oldest snapshot with 0 usecnt and lowest priority. */
static struct snapshot *find_unused(struct superblock *sb)
{
	struct snapshot *snaplist = sb->image.snaplist;
	u32 snapshots = sb->image.snapshots;

	assert(snapshots);
	struct snapshot *snap, *best = snaplist;

	for (snap = snaplist + 1; snap < snaplist + snapshots; snap++) {
		if (usecount(sb, snap) && !usecount(sb, best))
			continue;
		if ((!usecount(sb, snap) == !usecount(sb, best)) && (snap->prio >= best->prio))
			continue;
		best = snap;
	}
	return best;
}

static int create_snapshot(struct superblock *sb, unsigned snaptag)
{
	int i, snapshots = sb->image.snapshots;
	struct snapshot *snapshot;

	/* check if we are out of snapshots */
	if (snapshots >= MAX_SNAPSHOTS) {
		struct snapshot *victim = find_unused(sb);
		if (!usecount(sb, victim))
			delete_snap(sb, victim);
		if ((snapshots = sb->image.snapshots) >= MAX_SNAPSHOTS) {
			warn("the number of snapshots is beyond the %d limit", MAX_SNAPSHOTS);
			return -EFULL;
		}
	}

	/* tag already used? */
	if (find_snap(sb, snaptag))
		return -EEXIST;

	/* Find available snapshot bit */
	for (i = 0; i < MAX_SNAPSHOTS; i++)
		if (!(sb->snapmask & (1ULL << i)))
			goto create;
	return -EFULL;

create:
	trace_on(warn("Create snapshot tag = %u, bit = %i)", snaptag, i););
	snapshot = sb->image.snaplist + sb->image.snapshots++;
	*snapshot = (struct snapshot){ .tag = snaptag, .bit = i, .ctime = time(NULL), .sectors = sb->image.orgsectors };
	sb->snapmask |= (1ULL << i);
	set_sb_dirty(sb);
	return i;
}

#if 0
static void show_snapshots(struct superblock *sb)
{
	unsigned int i, snapshots = sb->image.snapshots;

	printf("%u snapshots\n", snapshots);
	for (i = 0; i < snapshots; i++) {
		struct snapshot *snapshot = sb->image.snaplist + i;
		printf("snapshot %u tag %u prio %i created %x\n", 
			snapshot->bit, 
			snapshot->tag, 
			snapshot->prio, 
			snapshot->ctime);
	}
}
#endif

/* Lock snapshot reads against origin writes */

static void reply(int sock, struct messagebuf *message) // !!! bad name choice, make this send_reply
{
	trace(warn("%x/%u", message->head.code, message->head.length););
	writepipe(sock, &message->head, message->head.length + sizeof(message->head));
}

#define SNAPCLIENT_BIT 1

struct client
{
	u64 id;
	int sock;
	int snaptag;
	u32 flags; 
};

/*
 * Cross-client Locking strategy
 *
 * Counterintuitively, no locking is required even between multiple clients
 * on multiple different nodes, when the accesses lie in the same virtual
 * snapshot or origin image.  This is because the requirement serialization
 * must necessarily be supplied by the filesystem accessing the block device,
 * whether that is a local filesystem running only on one node (at a time)
 * or a cluster filesystem.
 *
 * This "inherited" locking does not exist between different volumes,
 * because the mounted filesystem knows nothing about parallel accesses
 * performed on other volume images which may share some of physical data.
 * The ddsnap server must therefore supply locking between these accesses,
 * which turns out to be somewhat subtle, even taking advantage of the
 * simplification of having one central server that handles all inter-client
 * synchronization, as we have here.
 *
 * Here is a description of the locking events that occur.
 *
 * Lock an origin region for reading:
 *
 * If part of a read from a snapshot lies on the origin volume (very
 * common) then the region needs to be locked against origin writers
 * so that the data does not change while being read.  So a snapshot
 * write request loops across each chunk of the requested region and
 * either finds an existing lock on the chunk or creates a new one if
 * there are none.  Then a hold record is created and added to a list
 * on the lock record, to remember which snapshot client has locked
 * that chunk.
 *
 * A snapshot reader never has to wait to obtain its lock, because any
 * write requests are serialized against the read request by the server's
 * incoming message queue.  Any write request that has already been
 * granted and is currently in progress on an origin client will necessarily
 * have already copied out all its chunks to the snapshot store, and they
 * therefore do not have to be locked.
 *
 * Write a region of the origin:
 *
 * We walk across each chunk of the region and, if no snapshot client
 * has locked any of these origin chunks for reading, the write request
 * can be granted immediately.  Otherwise the write request has to be
 * granted later, after some read locks have been released.
 *
 * For each chunk that some client has locked for reading, a "wait"
 * record is created and entered onto a list.  (The first time a wait record
 * is created for a given write request, a "pending" structure is created
 * so that all the read locks involved have something to point at.  We
 * only want one of these pending structures per write request, because
 * we only want to reply to a pending write request once.)
 *
 * The wait record is pushed onto a list in the read lock record so that
 * when the reader releases the lock on that chunk, if that was the last
 * lock, the pending record will be used to submit the reply for the
 * pending write request.  Note that multiple write requests may be
 * waiting on a given chunk, and that multiple chunks of any given write
 * request may be locked by multiple different snapshot clients.  So this
 * little problem and the given solution is not simple at all.  Really not
 * simple.
 *
 * While the write request is being processed chunk by chunk, the head
 * of the pending list is held in a variable on the stack, if any read locks
 * were found.  This list head will end up being referenced by one or
 * more read locks, not through a global list.  This is why the pending
 * variable in incoming is allowed to go out of scope, seemingly without
 * being used.  The referenced pending structure will eventually be
 * freed when some read lock is released.
 *
 * Release an origin region:
 *
 * When a snapshot client sends a release message to the server, the
 * server loops across each chunk (which must be on the origin) to find
 * a corresponding lock hold record (which must be in the hash) and
 * releases the hold.  If the hold count is now zero, then if there are any
 * write requests waiting on the read lock, their pending counts may be
 * reduced.  Any pending counts that hit zero cause a pending write
 * request to be granted.
 */

struct pending
{
	unsigned holdcount;
	struct client *client;
	struct messagebuf message;
};

struct snaplock_wait
{
	struct pending *pending;
	struct snaplock_wait *next;
};

struct snaplock_hold
{
	struct client *client;
	struct snaplock_hold *next;
};

struct snaplock
{
	struct snaplock_wait *waitlist;
	struct snaplock_hold *holdlist;
	struct snaplock *next;
	chunk_t chunk;
};

/* !!! use structure assignment instead of calloc */
static struct snaplock *new_snaplock(struct superblock *sb)
{
	return calloc(1, sizeof(struct snaplock));
}

static struct snaplock_wait *new_snaplock_wait(struct superblock *sb)
{
	return calloc(1, sizeof(struct snaplock_wait));
}

static struct snaplock_hold *new_snaplock_hold(struct superblock *sb)
{
	return calloc(1, sizeof(struct snaplock_hold));
}

static void free_snaplock(struct superblock *sb, struct snaplock *p)
{
	free(p);
}

static void free_snaplock_hold(struct superblock *sb, struct snaplock_hold *p)
{
	free(p);
}

static void free_snaplock_wait(struct superblock *sb, struct snaplock_wait *p)
{
	free(p);
}

static unsigned snaplock_hash(struct superblock *sb, chunk_t chunk)
{
	unsigned bin = ((u32)(chunk * 3498734713U)) >> (32 - sb->snaplock_hash_bits); // !!! wordsize braindamage !!!
	assert(bin >= 0 && bin < (1 << sb->snaplock_hash_bits));
	return bin;
}

static struct snaplock *find_snaplock(struct snaplock *list, chunk_t chunk)
{
	for (; list; list = list->next)
		if (list->chunk == chunk)
			return list;
	return NULL;
}

# ifdef DEBUG_LOCKS
static void show_locks(struct superblock *sb)
{
	unsigned n = 0, i;
	for (i = 0; i < (1 << sb->snaplock_hash_bits); i++) {
		struct snaplock *lock = sb->snaplocks[i];
		if (!lock)
			continue;
		if (!n) printf("Locks:\n");
		printf("[%03u] ", i);
		do {
			printf("chunk %Lx ", lock->chunk);
			struct snaplock_hold *hold = lock->holdlist;
			for (; hold; hold = hold->next)
				printf("held by client %Lx ", hold->client->id);
			struct snaplock_wait *wait = lock->waitlist;
			for (; wait; wait = wait->next)
				printf("wait [%02hx/%u] ", snaplock_hash(sb, (u32)wait->pending), wait->pending->holdcount);
		} while ((lock = lock->next));
		printf("\n");
		n++;
	}
	if (!n) printf("-- no locks --\n");
}
# endif

/*
 * If the passed chunk is locked, append a new node to the lock's wait list,
 * allocating a new "pending" structure if necessary.  There is one "pending"
 * structure per client I/O, to which all affected waitlist nodes point.
 * The holdcount indicates the number of lock waitlist nodes that point to
 * it, since more than one waitlist node can point to the same "pending"
 * structure if the chunk has been locked by more than one client or more
 * than once by a client.
 */
static void waitfor_chunk(struct superblock *sb, chunk_t chunk, struct pending **pending)
{
	struct snaplock *lock;

	trace(printf("enter waitfor_chunk\n"););
	if ((lock = find_snaplock(sb->snaplocks[snaplock_hash(sb, chunk)], chunk))) {
		if (!*pending) {
			// arguably we should know the client and fill it in here
			*pending = calloc(1, sizeof(struct pending));
			(*pending)->holdcount = 1;
		}
		trace(printf("new_snaplock_wait call\n"););
		struct snaplock_wait *wait = new_snaplock_wait(sb);
		wait->pending = *pending;
		wait->next = lock->waitlist;
		lock->waitlist = wait;
		(*pending)->holdcount++;
	}
	trace(printf("leaving waitfor_chunk\n"););
#ifdef DEBUG_LOCKS
	show_locks(sb);
#endif
}

/*
 * Lock a chunk.  If the lock structure doesn't exist for the chunk, create
 * it.  In any event, create a new hold structure and add it to the lock
 * hold list.
 */
static void readlock_chunk(struct superblock *sb, chunk_t chunk, struct client *client)
{
	struct snaplock **bucket = &sb->snaplocks[snaplock_hash(sb, chunk)];
	struct snaplock *lock;

	trace(printf("enter readlock_chunk\n"););
	if (!(lock = find_snaplock(*bucket, chunk))) {
		trace(printf("creating a new lock\n"););
		lock = new_snaplock(sb);
		*lock = (struct snaplock){ .chunk = chunk, .next = *bucket };
		*bucket = lock;
	}
	trace(printf("holding snaplock\n"););
	struct snaplock_hold *hold = new_snaplock_hold(sb);
	trace(printf("got the snaplock?\n"););
	hold->client = client;
	hold->next = lock->holdlist;
	lock->holdlist = hold;
	trace(printf("leaving readlock_chunk\n"););
}

static struct snaplock *release_lock(struct superblock *sb, struct snaplock *lock, struct client *client)
{
	struct snaplock *ret = lock;
	struct snaplock_hold **holdp = &lock->holdlist;

	trace(printf("entered release_lock\n"););
	while (*holdp && (*holdp)->client != client)
		holdp = &(*holdp)->next;

	if (!*holdp) {
		trace_on(printf("chunk %Lx holder %Lx not found\n", (llu_t) lock->chunk, client->id););
		return NULL;
	}

	/* Delete and free holder record */
	struct snaplock_hold *next = (*holdp)->next;
	free_snaplock_hold(sb, *holdp);
	*holdp = next;

	if (lock->holdlist)
		return ret;

	/* Release and delete waiters, delete lock */
	struct snaplock_wait *list = lock->waitlist;
	while (list) {
		struct snaplock_wait *next = list->next;
		assert(list->pending->holdcount);
		if (list->pending != NULL && !--(list->pending->holdcount)) {
			struct pending *pending = list->pending;
			reply(pending->client->sock, &pending->message);
			free(pending);
		}
		free_snaplock_wait(sb, list);
		list = next;
	}
	ret = lock->next;
	free_snaplock(sb, lock);

	trace(printf("leaving release_lock\n"););
	return ret;
}

static int release_chunk(struct superblock *sb, chunk_t chunk, struct client *client)
{
	trace(printf("enter release_chunk\n"););
	trace(printf("release %Lx\n", chunk););
	struct snaplock **lockp = &sb->snaplocks[snaplock_hash(sb, chunk)];

	/* Find pointer to lock record */
	while (*lockp && (*lockp)->chunk != chunk) {
		assert(lockp != &(*lockp)->next);
		lockp = &(*lockp)->next;
	}
	struct snaplock *next, *lock = *lockp;

	if (!lock) {
		trace_on(printf("chunk %Lx not locked\n", (llu_t) chunk););
		return -1;
	}

	next = release_lock(sb, lock, client);
	*lockp = next;
	if (!next)
		return -2;

	trace(printf("release_chunk returning 0\n next lock %p\n",next););
	return 0;
}

/* Build up a response as a list of chunk ranges */
// !!! needlessly complex because of misguided attempt to pack multiple transactions into single message

struct addto
{
	unsigned count;
	chunk_t firstchunk;
	chunk_t nextchunk;
	struct rwmessage *reply;
	shortcount *countp;
	chunk_t *top;
	char *lim;
};

static void check_response_full(struct addto *r, unsigned bytes)
{
	if ((char *)r->top < r->lim - bytes)
		return;
	error("Need realloc");
}

static void addto_response(struct addto *r, chunk_t chunk)
{
	trace(printf("inside addto_response\n"););
	if (chunk != r->nextchunk) {
		if (r->top) {
			trace(warn("finish old range"););
			*(r->countp) = (r->nextchunk -  r->firstchunk);
		} else {
			trace(warn("alloc new reply"););
			r->reply = (void *) malloc(sizeof(struct messagebuf)); // FIXME TODO - malloc/free in the snapshot read path, bad for performance
			r->top = (chunk_t *)(((char *)r->reply) + sizeof(struct head) + offsetof(struct rw_request, ranges));
			r->lim = ((char *)r->reply) + maxbody;
			r->count++;
		}
		trace(warn("start new range"););
		check_response_full(r, 2*sizeof(chunk_t));
		r->firstchunk = *(r->top)++ = chunk;
		r->countp = (shortcount *)r->top;
		r->top = (chunk_t *)(((shortcount *)r->top) + 1);
	}
	r->nextchunk = chunk + 1;
	trace(printf("leaving addto_response\n"););
}

static int finish_reply_(struct addto *r, unsigned code, unsigned id)
{
	if (!r->countp)
		return 0;

	*(r->countp) = (r->nextchunk -  r->firstchunk);
	r->reply->head.code = code;
	r->reply->head.length = (char *)r->top - (char *)r->reply - sizeof(struct head);
	r->reply->body.id = id;
	r->reply->body.count = r->count;
	return 1;
}

static void finish_reply(int sock, struct addto *r, unsigned code, unsigned id)
{
	if (finish_reply_(r, code, id)) {
		trace(printf("sending reply... "););
		reply(sock, (struct messagebuf *)r->reply);
		trace(printf("done sending reply\n"););
	}
	free(r->reply); // FIXME TODO - malloc/free in the snapshot read path, bad for performance
}

/*
 * Initialization, State load/save
 */

static void setup_sb(struct superblock *sb)
{
	int err;

	int bs_bits = sb->image.metadata.allocsize_bits;
	int cs_bits = sb->image.snapdata.allocsize_bits;
	assert(!combined(sb) || bs_bits == cs_bits);
	sb->metadata.allocsize = 1 << bs_bits;
	sb->snapdata.allocsize = 1 << cs_bits;
	sb->metadata.chunk_sectors_bits = bs_bits - SECTOR_BITS;
	sb->snapdata.chunk_sectors_bits = cs_bits - SECTOR_BITS;
	sb->metadata.alloc_per_node = (sb->metadata.allocsize - offsetof(struct enode, entries)) / sizeof(struct index_entry);
#ifdef BUSHY
	sb->metadata.alloc_per_node = 10;
#endif

	sb->copybuf_size = 32 * sb->snapdata.allocsize;
	if ((err = posix_memalign((void **)&(sb->copybuf), SECTOR_SIZE, sb->copybuf_size)))
	    error("unable to allocate buffer for copyout data: %s", strerror(err));
	sb->max_commit_blocks = (sb->metadata.allocsize - sizeof(struct commit_block)) / sizeof(sector_t);

	unsigned snaplock_hash_bits = 8;
	sb->snaplock_hash_bits = snaplock_hash_bits;
	sb->snaplocks = (struct snaplock **)calloc(1 << snaplock_hash_bits, sizeof(struct snaplock *));

	sb->metadata.asi = &sb->image.metadata; // !!! so why even have sb->metadata??
	sb->snapdata.asi = combined(sb) ? &(sb)->image.metadata : &(sb)->image.snapdata;
}

/*
 * Shrink snapshot store: clear bits for shrinked space and free unused bitmap chunks
 * Expand snapshot store:
 *   If we don't need more bitmap chunks, just update last bytes.
 *   Otherwise:
 *     Copy old bitmap blocks to the beginning of the added space
 *     Zero additional new bitmap blocks
 *     Set new bitmap base and bitmap blocks
 *     Clear bits for old bitmap chunks in new bitmap
 *     Reserve new bitmap chunks in new bitmap
 */

static int adjust_bitmap(struct superblock *sb, struct allocspace *as, u64 newchunks, u64 new_basechunk, u64 new_metachunks)
{
	chunk_t oldchunks = as->asi->chunks;
	unsigned oldbitmaps = as->asi->bitmap_blocks;
	unsigned newbitmaps = calc_bitmap_blocks(sb, newchunks);
	chunk_t oldbase = as->asi->bitmap_base << SECTOR_BITS;
	chunk_t newbase = new_basechunk << sb->metadata.asi->allocsize_bits;
	unsigned blockshift = sb->metadata.asi->allocsize_bits;
	unsigned blocksize = sb->metadata.allocsize;
	chunk_t oldchunks_meta = oldbase >> sb->metadata.asi->allocsize_bits;
	sector_t sector;
	int i;

	warn("oldchunks %Lu newchunks %Lu, oldbitmaps %u, newbitmaps %u", oldchunks, newchunks, oldbitmaps, newbitmaps);
	/* snapshot shrinking */
	if (newchunks <= oldchunks) {
		/* check if any chunk to be freed is currently in use */
		if (change_bits(sb, newchunks, oldchunks - newchunks, oldbase, 0)) {
			warn("some chunks to be freed are still in use!!!");
			return -1;
		}
		/* clear bits for unused bitmap chunks */
		if (newbitmaps < oldbitmaps) {
			change_bits(sb, oldchunks_meta + newbitmaps, oldbitmaps - newbitmaps, sb->image.metadata.bitmap_base << SECTOR_BITS, 2);
			as->asi->bitmap_blocks = newbitmaps;
		}
		return 0;
	}

	/* no need to relocate bitmap if we don't need more bitmap chunks */
	if (oldbitmaps == newbitmaps) {
		if ((oldchunks & 7) || (newchunks & 7)) {
			/* clear the last byte of the old/new bitmap block */
			sector_t sector = (oldbase >> SECTOR_BITS) + ((oldbitmaps - 1) << sb->metadata.chunk_sectors_bits);
			struct buffer *buffer = bread(sb->metadev, sector, blocksize);
			if ((oldchunks & 7))
				buffer->data[(oldchunks >> 3) & (blocksize - 1)] &= ~(0xff << (oldchunks & 7));
			if ((newchunks & 7))
				buffer->data[(newchunks >> 3) & (blocksize - 1)] |= 0xff << (newchunks & 7);
			brelse_dirty(buffer);
		}
		return 0;
	}

	warn("expand bitmap: oldbase %Lu, newbase %Lu, new_metachunks %Lu", oldbase, newbase, new_metachunks);
	/* need to add enough metadata space when expanding metadata/snapshot store */
	if (new_metachunks < newbitmaps) {
		warn("Not enough space for bitmap relocation, please add more metadata space!!!");
		return -1;
	}

	/* copy old bitmap chunks to the newbase */
	for (i = 0; i < oldbitmaps; i++) {
		trace_off(warn("copy oldbitmap %d to sector %Lu", i, (newbase + (i << blockshift)) >> SECTOR_BITS););
		diskread(sb->metadev, sb->copybuf, blocksize, oldbase + (i << blockshift));
		diskwrite(sb->metadev, sb->copybuf, blocksize, newbase + (i << blockshift));
	}

	/* clear the partial bits for the last old bitmap byte */
	if ((oldchunks & 7)) {
		sector_t sector = (newbase >> SECTOR_BITS) + ((oldbitmaps - 1) << sb->metadata.chunk_sectors_bits);
		struct buffer *buffer = bread(sb->metadev, sector, blocksize);
		buffer->data[(oldchunks >> 3) & (blocksize - 1)] &= ~(0xff << (oldchunks & 7));
		brelse_dirty(buffer);
	}

	/* clear new bitmap chunks */
	sector = (newbase >> SECTOR_BITS) + (oldbitmaps << sb->metadata.chunk_sectors_bits);
	for (i = oldbitmaps; i < newbitmaps; i++, sector += chunk_sectors(&sb->metadata)) {
		struct buffer *buffer = getblk(sb->metadev, sector, blocksize);
		memset(buffer->data, 0, blocksize);
		trace_off(warn("clear newbitmap %d, sector %Lu", i, sector););
		/* Suppress overrun allocation in partial last byte */
		if (i == newbitmaps - 1 && (newchunks & 7))
			buffer->data[(newchunks >> 3) & (blocksize - 1)] |= 0xff << (newchunks & 7);
		brelse_dirty(buffer);
	}

	as->asi->bitmap_base = newbase >> SECTOR_BITS;
	as->asi->bitmap_blocks = newbitmaps;

	/* clear bits for old bitmap chunks and reserve new bitmap chunks at new bitmap base */
	change_bits(sb, oldchunks_meta, oldbitmaps, sb->image.metadata.bitmap_base << SECTOR_BITS, 2);
	change_bits(sb, new_basechunk, newbitmaps, sb->image.metadata.bitmap_base << SECTOR_BITS, 1);
	return 0;
}

static int change_device_sizes(struct superblock *sb, u64 orgsize, u64 snapsize, u64 metasize)
{
	u64 metachunks, snapchunks, orgchunks;
	u64 new_bitmap_basechunk = 0;
	u64 new_metachunks = 0;

	metachunks = metasize >> sb->image.metadata.allocsize_bits;
	if (metachunks && sb->image.metadata.chunks != metachunks) {
		u64 oldbitmaps = sb->image.metadata.bitmap_blocks;
		warn("metadev size changes from %Lu to %Lu", sb->image.metadata.chunks, metachunks);
		/* no need to do bitmap update during initialization (i.e., metadata.chunks ==0) */
		if (sb->image.metadata.chunks) {
			new_bitmap_basechunk = sb->image.metadata.chunks;
			new_metachunks = metachunks - sb->image.metadata.chunks;
			if (adjust_bitmap(sb, &sb->metadata, metachunks, new_bitmap_basechunk, new_metachunks) < 0)
				return -1;
			new_bitmap_basechunk += sb->metadata.asi->bitmap_blocks;
			new_metachunks -= sb->metadata.asi->bitmap_blocks;
		}
		sb->image.metadata.freechunks += metachunks - sb->image.metadata.chunks + oldbitmaps - sb->image.metadata.bitmap_blocks;
		sb->image.metadata.chunks = metachunks;
	}

	if (sb->metadev != sb->snapdev) {
		snapchunks = snapsize >> sb->image.snapdata.allocsize_bits;
	       	if (snapchunks && sb->image.snapdata.chunks != snapchunks) {
			warn("snapdev size changes from %Lu to %Lu", sb->image.snapdata.chunks, snapchunks);
			/* 
			 * For bitmap relocation purpose, we allow snapdev expanding only if metadev 
			 * is also expanded. In that case, new_bitmap_basechunk and new_metachunks
			 * have been properly updated above.
			 */
			if (sb->image.snapdata.chunks) {
				u64 oldbitmaps = sb->image.snapdata.bitmap_blocks;
				if (adjust_bitmap(sb, &sb->snapdata, snapchunks, new_bitmap_basechunk, new_metachunks) < 0)
					return -1;
				sb->image.metadata.freechunks += oldbitmaps - sb->image.snapdata.bitmap_blocks;
			}
			sb->image.snapdata.freechunks += snapchunks - sb->image.snapdata.chunks;
			sb->image.snapdata.chunks = snapchunks;
		}
	}

	orgchunks = orgsize >> SECTOR_BITS;
	if (orgchunks && sb->image.orgsectors != orgchunks) {
		warn("orgdev size changes from %Lu to %Lu", sb->image.orgsectors, orgchunks);
		sb->image.orgsectors = orgchunks;
	}
	return 0;
}

static int sb_get_device_sizes(struct superblock *sb)
{
	u64 metasize, snapsize, orgsize;

	if ((metasize = fdsize64(sb->metadev)) == -1) {
		warn("Error %i: %s determining metadata store size", errno, strerror(errno));
		return -errno;
	}

	if (sb->metadev != sb->snapdev) {
		if ((snapsize = fdsize64(sb->snapdev)) == -1) {
			warn("Error %i: %s determining snapshot store size", errno, strerror(errno));
			return -errno;
		}
	} else
		snapsize = metasize;

	if ((orgsize = fdsize64(sb->orgdev)) == -1) {
		warn("Error %i: %s determining origin volume size", errno, strerror(errno));
		return -errno;
	}

	change_device_sizes(sb, orgsize, snapsize, metasize);
	return 0;
}

static void save_sb_check(struct superblock *sb)
{
	commit_deferred_allocs(sb);
	if (dirty_buffer_count)
		warn("%i dirty buffers when all should be clean!", dirty_buffer_count);
	save_sb(sb);
}

static int init_journal(struct superblock *sb)
 {
	chunk_t metafree = sb->image.metadata.freechunks;
	chunk_t snapfree = sb->image.snapdata.freechunks;
	for (int i = 0; i < sb->image.journal_size; i++) {
		struct buffer *buffer = jgetblk(sb, i);
		memset(buffer->data, 0, sb->metadata.allocsize);
		struct commit_block *commit = (struct commit_block *)buffer->data;
		*commit = (struct commit_block){ .magic = JMAGIC, .sequence = next_journal_block(sb), .metafree = metafree, .snapfree = snapfree };
		commit->checksum = -checksum_block(sb, (void *)commit);
		assert(buffer->count == 1);
		write_buffer(buffer);
		brelse(buffer);
		evict_buffer(buffer);
	}
	return 0;
}

static int init_super(struct superblock *sb, u32 js_bytes, u32 bs_bits, u32 cs_bits)
{
	int error;

	sb->image = (struct disksuper){ .magic = SB_MAGIC };
	sb->image.metadata.allocsize_bits = bs_bits;
	sb->image.snapdata.allocsize_bits = cs_bits;

	if ((error = sb_get_device_sizes(sb)) != 0) {
		warn("Error %i: %s get device sizes", error, strerror(-error));
		return error;
	}

	setup_sb(sb);
	sb->image.etree_levels = 1;
	sb->image.create_time = time(NULL);
	sb->image.orgoffset  = 0; //!!! FIXME: shouldn't always assume offset starts at 0

	trace_off(warn("cs_bits = %u", sb->snapdata.asi->allocsize_bits););
	u32 chunk_size = 1 << sb->snapdata.asi->allocsize_bits, js_chunks = DIVROUND(js_bytes, chunk_size);
	trace_off(warn("chunk_size = %u, js_chunks = %u", chunk_size, js_chunks););

	sb->image.journal_size = js_chunks;
	sb->image.journal_next = 0;
	sb->image.sequence = sb->image.journal_size;
	if ((error = init_allocation(sb)) < 0) {
		warn("Error: Unable to initialize allocation information");
		return error;
	}
	set_sb_dirty(sb);

	/* Get an enode and an eleaf for the root of the b-tree. */
	struct buffer *leafbuf = new_leaf(sb);
	struct buffer *rootbuf = new_node(sb);
	assert(leafbuf != NULL && rootbuf != NULL);
	buffer2node(rootbuf)->count = 1;
	buffer2node(rootbuf)->entries[0].sector = leafbuf->sector;
	sb->image.etree_root = rootbuf->sector;
	brelse_dirty(rootbuf);
	brelse_dirty(leafbuf);
	return 0;
}

static int client_locks(struct superblock *sb, struct client *client, int check)
{
	int i;

	for (i = 0; i < (1 << sb->snaplock_hash_bits); i++) {
		struct snaplock **lockp = &sb->snaplocks[i];

		while (*lockp) {
			struct snaplock_hold *hold;

			for (hold = (*lockp)->holdlist; hold; hold = hold->next)
				if (hold->client == client) {
					if (check)
						return 1;
					*lockp = release_lock(sb, *lockp, client);
					goto next;
				}
			lockp = &(*lockp)->next;
next:
			continue;
		}
	}
	return 0;
}

#define check_client_locks(x, y) client_locks(x, y, 1)
#define free_client_locks(x, y) client_locks(x, y, 0)

/* A very simple-minded implementation.  You can do it in very
 * few operations with whole-register bit twiddling but I assume
 * that we can juse find a macro somewhere which works.
 *  AKA hamming weight, sideways add
 */
static unsigned int bit_count(u64 num)
{
	unsigned count = 0;

	for (; num; num >>= 1)
		if (num & 1)
			count++;

	return count;
}

/*
 * Walk a B-tree leaf, counting shared chunks per snapshot.
 */
static void calc_sharing(struct superblock *sb, struct eleaf *leaf, void *data)
{
	uint64_t *share_table = data;
	struct exception const *p;
	unsigned bit;
	unsigned int share_count;
	int i;

	for (i = 0; i < leaf->count; i++)
		for (p = emap(leaf, i); p < emap(leaf, i+1); p++) {
			assert(p->share); // belongs in check leaf function
			share_count = bit_count(p->share) - 1;

			for (bit = 0; bit < MAX_SNAPSHOTS; bit++)
				if (p->share & (1ULL << (u64)bit))
					share_table[MAX_SNAPSHOTS * bit + share_count]++;
		}
}

/* It is more expensive than we'd like to find the struct snapshot, FIXME */
static struct snapshot *client_snap(struct superblock *sb, struct client *client)
{
	struct snapshot *snapshot = find_snap(sb, client->snaptag);
	assert(snapshot);
	return snapshot;
}

void outerror(int sock, int err, char *text)
{
	unsigned len = sizeof(struct generic_error) + strlen(text) + 1;
	if (outhead(sock, GENERIC_ERROR, len) < 0 ||
		writepipe(sock, &err, sizeof(struct generic_error)) < 0 ||
		writepipe(sock, text, len - sizeof(struct generic_error)) < 0)
		warn("unable to send error %u", GENERIC_ERROR);
}

void get_status(struct superblock *sb, unsigned sock)
{
	struct snapshot const *snaplist = sb->image.snaplist;

	unsigned snapshots = sb->image.snapshots;
	size_t reply_len = snapshot_details_calc_size(snapshots, snapshots);
	struct status_reply *reply = calloc(reply_len, 1); // !!! error check?

	uint64_t share_array[MAX_SNAPSHOTS * MAX_SNAPSHOTS];
	memset(share_array, 0, sizeof(share_array));

	traverse_tree_range(sb, 0, -1, calc_sharing, share_array);
	reply->ctime = sb->image.create_time;
	reply->meta.chunksize_bits = sb->image.metadata.allocsize_bits;
	reply->meta.total = sb->image.metadata.chunks;
	reply->meta.free = sb->image.metadata.freechunks;
	reply->store.chunksize_bits = sb->image.snapdata.allocsize_bits;
	reply->store.total = sb->image.snapdata.chunks;
	reply->store.free = sb->image.snapdata.freechunks;
	reply->write_density = 0; // !!! think about how to compute this
	reply->snapshots = snapshots;

	for (int row = 0; row < sb->image.snapshots; ++row) {
		struct snapshot_details *details = snapshot_details(reply, row, snapshots);

		details->snapinfo.ctime = snaplist[row].ctime;
		details->snapinfo.snap = snaplist[row].tag;
		details->snapinfo.prio = snaplist[row].prio;
		details->snapinfo.usecnt = usecount(sb, (struct snapshot*)&snaplist[row]);

		if (is_squashed(&snaplist[row])) {
			details->sharing[0] = -1;
			continue;
		}
		for (int col = 0; col < snapshots; ++col)
			details->sharing[col] = share_array[MAX_SNAPSHOTS * snaplist[row].bit + col];
	}

	if (outhead(sock, STATUS_OK, reply_len) < 0 || writepipe(sock, reply, reply_len) < 0)
		warn("unable to send status message");

	free(reply);
	selfcheck_freespace(sb);
}

/*
 * Responses to IO requests take two quite different paths through the
 * machinery:
 *
 *   - Origin write requests are just sent back with their message
 *     code changed, unless they have to wait for a snapshot read
 *     lock in which case the incoming buffer is copied and the
 *     response takes a kafkaesque journey through the read locking
 *     beaurocracy.
 *
 *   - Responses to snapshot read or write requests have to be built
 *     up painstakingly in allocated buffers, keeping a lot of state
 *     around so that they end up with a minimum number of contiguous
 *     chunk ranges.  Once complete they can always be sent
 *     immediately.
 *
 * To mess things up further, snapshot read requests can return both
 * a list of origin ranges and a list of snapshot store ranges.  In
 * the latter case the specific snapshot store chunks in each logical
 * range are also returned, because they can (normally will) be
 * discontiguous.  This goes back to the client in two separate
 * messages, on the theory that the client will find it nice to be
 * able to process the origin read ranges and snapshot read chunks
 * separately.  We'll see how good an idea that is.
 *
 * The implementation ends up looking nice and tidy, but appearances
 * can be deceiving.
 */
static int incoming(struct superblock *sb, struct client *client)
{
	struct messagebuf message;
	unsigned sock = client->sock;
	int i, j, err;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	trace(warn("%x/%u", message.head.code, message.head.length););
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case QUERY_WRITE:
		/* Write to origin if snaptag is -1 */
		if (client->snaptag == -1) {
			struct pending *pending = NULL;
			struct rw_request *body = (struct rw_request *)message.body;
			struct chunk_range *p = body->ranges;
			chunk_t chunk;
			if (message.head.length < sizeof(*body))
				goto message_too_short;

			trace(warn("origin write query, %u ranges", body->count););
			message.head.code = ORIGIN_WRITE_OK;
			for (i = 0; i < body->count; i++, p++)
				for (j = 0, chunk = p->chunk; j < p->chunks; j++, chunk++) {
					chunk_t exception = make_unique(sb, chunk, -1);
					if (exception == -1) {
						warn("ERROR: unable to perform copyout during origin write.");
						message.head.code = ORIGIN_WRITE_ERROR;
					} else {
						/* 
						 * check if there is any pending readlock on the chunk.
						 * Note: we may want to defer the copyout and/or the
						 * update of btree if the chunk has any pending readlock.
						 * We can then skip the lock checking if a chunk is unique.
						 */
						waitfor_chunk(sb, chunk, &pending);
					}
				}
			finish_copyout(sb);
			commit_transaction(sb, 0);
			/*
			 * If waitfor_chunk() detected a pending readlock and
			 * queued this write for it, just update the "pending"
			 * structure with our client ID and fill in the
			 * message to be sent when the lock is released.
			 */
			if (pending) {
				pending->client = client;
				memcpy(&pending->message, &message, message.head.length + sizeof(struct head));
				pending->holdcount--;
				break;
			}
			reply(sock, &message);
			break;
		}
		/* Write to snapshot */
		struct rw_request *body = (struct rw_request *)message.body;
		if (message.head.length < sizeof(*body))
			goto message_too_short;
		trace(printf("snapshot write request, %u ranges\n", body->count););
		struct addto snap = { .nextchunk = -1 };
		u32 ret_msgcode = SNAPSHOT_WRITE_OK;
		struct snapshot *snapshot = client_snap(sb, client);
		for (i = 0; i < body->count; i++)
			for (j = 0; j < body->ranges[i].chunks; j++) {
				chunk_t chunk = body->ranges[i].chunk + j;
				chunk_t exception;

				if (is_squashed(snapshot)) {
					warn("trying to write squashed snapshot, id = %u", body->id);
					exception =  -1;
				} else
					exception = make_unique(sb, chunk, snapshot->bit);
				if (exception == -1) {
					warn("ERROR: unable to perform copyout during snapshot write.");
					ret_msgcode = SNAPSHOT_WRITE_ERROR;
				}
				trace(printf("exception = %Lx\n", exception););
					addto_response(&snap, chunk);
				check_response_full(&snap, sizeof(chunk_t));
				*(snap.top)++ = exception;
			}
		finish_copyout(sb);
		commit_transaction(sb, 0);
		finish_reply(client->sock, &snap, ret_msgcode, body->id);
		break;
	case QUERY_SNAPSHOT_READ:
	{
		struct rw_request *body = (struct rw_request *)message. body;
		if (message.head.length < sizeof(*body))
			goto message_too_short;
		trace(printf("snapshot read request, %u ranges\n", body->count););
		struct addto snap = { .nextchunk = -1 }, org = { .nextchunk = -1 };
		struct snapshot *snapshot = client_snap(sb, client);

		if (is_squashed(snapshot)) {
			warn("trying to read squashed snapshot %u", client->snaptag);
			for (i = 0; i < body->count; i++)
				for (j = 0; j < body->ranges[i].chunks; j++) {
					chunk_t chunk = body->ranges[i].chunk + j;
					addto_response(&snap, chunk);
					check_response_full(&snap, sizeof(chunk_t));
					*(snap.top)++ = 0;
				}
			finish_reply(client->sock, &snap, SNAPSHOT_READ_ERROR, body->id);
			break;
		}

		for (i = 0; i < body->count; i++)
			for (j = 0; j < body->ranges[i].chunks; j++) {
				chunk_t chunk = body->ranges[i].chunk + j, exception = 0;
				trace(warn("read %Lx", chunk););
				test_unique(sb, chunk, snapshot->bit, &exception);
				/*
				 * If this chunk is only in a snapshot, we
				 * want to read only from the snapshot; if it's
				 * shared with the origin, we want to read
				 * that instead.  Add the chunk to the
				 * appropriate message.
				 */
				if (exception) { /* It's only in a snapshot.  */
					trace(warn("read exception %Lx", exception););
					addto_response(&snap, chunk);
					check_response_full(&snap, sizeof(chunk_t));
					*(snap.top)++ = exception;
				} else {	 /* Shared with the origin.   */
					trace(warn("read origin %Lx", chunk););
					addto_response(&org, chunk);
					trace(printf("locking chunk %Lx\n", chunk););
					/*
					 * Lock the chunk so that later origin
					 * writes can't change it out from
					 * under us.
					 */
					readlock_chunk(sb, chunk, client);
				}
			}
		/*
		 * Above, we built both a SNAPSHOT_READ_ORIGIN_OK message and a
		 * SNAPSHOT_READ_OK message.  We placed chunks that were only
		 * in a snapshot into the latter and chunks that were shared
		 * with the origin in the former.  We now need to finish and
		 * transmit both messages.
		 *
		 * If there happened to be no chunks that satisfied one or the
		 * other criterion, the corresponding message will be empty.
		 * The finish_reply() function conveniently frees any empty
		 * message it receives.
		 */
		finish_reply(client->sock, &org, SNAPSHOT_READ_ORIGIN_OK, body->id);
		finish_reply(client->sock, &snap, SNAPSHOT_READ_OK, body->id);
		break;
	}
	case FINISH_SNAPSHOT_READ:
	{
		struct rw_request *body = (struct rw_request *)message.body;
		if (message.head.length < sizeof(*body))
			goto message_too_short;
		trace(printf("finish snapshot read, %u ranges\n", body->count););

		for (i = 0; i < body->count; i++)
			for (j = 0; j < body->ranges[i].chunks; j++)
				release_chunk(sb, body->ranges[i].chunk + j, client);

		break;
	}
	case IDENTIFY:
	{
		u32 tag      = ((struct identify *)message.body)->snap;
		sector_t off = ((struct identify *)message.body)->off;
		sector_t len = ((struct identify *)message.body)->len;
		u64 sectors;
		u32 err = 0; /* success */
		unsigned int error_len;
		char err_msg[MAX_ERRMSG_SIZE];

#ifdef DEBUG_ERROR
		snprintf(err_msg, MAX_ERRMSG_SIZE, "Debug mode to test error messages");
		err_msg[MAX_ERRMSG_SIZE-1] = '\0';
		goto identify_error;
#endif

		client->id = ((struct identify *)message.body)->id;
		client->snaptag = tag;

		trace(warn("got identify request, setting id="U64FMT" snap=%i (tag=%u), sending chunksize_bits=%u\n",
			client->id, client->snap, tag, sb->snapdata.asi->allocsize_bits););
		warn("client id %Lx, snaptag %u", client->id, tag);

		if (tag != (u32)~0UL) {
			struct snapshot *snapshot = find_snap(sb, tag);

			if (!snapshot || is_squashed(snapshot)) {
				warn("Snapshot tag %u is not valid", tag);
				snprintf(err_msg, MAX_ERRMSG_SIZE, "Snapshot tag %u is not valid", tag);
				err_msg[MAX_ERRMSG_SIZE-1] = '\0'; // make sure it's null terminated
				err = ERROR_INVALID_SNAPSHOT;
				goto identify_error;
			}
			if ((((usecount(sb, snapshot) + 1) >> 16) != 0)) {
				snprintf(err_msg, MAX_ERRMSG_SIZE, "Usecount overflow.");
				err_msg[MAX_ERRMSG_SIZE-1] = '\0'; // make sure it's null terminated
				err = ERROR_USECOUNT;
				goto identify_error;
			}
			client->flags |= SNAPCLIENT_BIT;
			sb->usecount[snapshot->bit]++; // update the transient usecount only
			sectors = snapshot->sectors;
		} else
			sectors = sb->image.orgsectors;
		if (len != sectors) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "volume size mismatch for snapshot %u", tag);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0'; // make sure it's null terminated
			err = ERROR_SIZE_MISMATCH;
			goto identify_error;
		}
		if (off != sb->image.orgoffset) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "volume offset mismatch for snapshot %u", tag);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0'; // make sure it's null terminated
			err = ERROR_OFFSET_MISMATCH;
			goto identify_error;
		}

		if (outbead(sock, IDENTIFY_OK, struct identify_ok,
				 .chunksize_bits = sb->snapdata.asi->allocsize_bits) < 0)
			warn("unable to reply to IDENTIFY message");
		break;

	identify_error:
		error_len = sizeof(struct identify_error) + strlen(err_msg) + 1;
		if (outhead(sock, IDENTIFY_ERROR, error_len) < 0 ||
			writepipe(sock, &err, sizeof(struct identify_error)) < 0 ||
			writepipe(sock, err_msg, error_len - sizeof(struct identify_error)) < 0)
			warn("unable to reply to IDENTIFY message with error");
		break;

	}
	case UPLOAD_LOCK:
		break;

	case FINISH_UPLOAD_LOCK:
		break;

	case CREATE_SNAPSHOT:
	{
		int err = create_snapshot(sb, ((struct create_snapshot *)message.body)->snap);
		if (err < 0) {
			char *error =
				-err == EFULL ? "too many snapshots" :
				-err == EEXIST ? "snapshot already exists" :
				"unknown snapshot create error";
			outerror(sock, -err, error);
			break;
		}
		save_sb_check(sb);
		if (outbead(sock, CREATE_SNAPSHOT_OK, struct { }) < 0)
			warn("unable to reply to create snapshot message");
		break;
	}
	case DELETE_SNAPSHOT:
	{
		struct snapshot *snapshot = find_snap(sb, ((struct create_snapshot *)message.body)->snap);
		if (!snapshot)
			outerror(sock, EINVAL, "snapshot doesn't exist");
		else if (usecount(sb, snapshot))
			outerror(sock, EINVAL, "snapshot has non-zero usecount");
		else if (delete_snap(sb, snapshot))
			outerror(sock, EIO, "fail to delete snapshot");
		else {
			save_sb_check(sb);
			if (outbead(sock, DELETE_SNAPSHOT_OK, struct { }) < 0)
				warn("unable to reply to delete snapshot message"); // !!! return message
		}
		break;
	}
	case INITIALIZE_SNAPSTORE: // this is a stupid feature
	{
		break;
	}
	case DUMP_TREE_RANGE:
	{
		struct dump_tree_range *dump = (void *)message.body;
		show_tree_range(sb, dump->start, dump->finish);
		break;
	}
	case START_SERVER:
	{
		warn("Activating server");
		if (diskread(sb->metadev, &sb->image, 4096, SB_SECTOR << SECTOR_BITS) < 0)
			error("Unable to read superblock: %s", strerror(errno));
		assert(valid_sb(sb));
		setup_sb(sb);
		sb->snapmask = calc_snapmask(sb);
		trace(printf("Active snapshot mask: %016llx\n", sb->snapmask););
		if (sb_get_device_sizes(sb))
			error("FIXME!!! don't exit from load_sb, return -1 instead");
#if 0
		// make this a startup option !!!
		sb->metadata.chunks_used = sb->metadata.asi->chunks - count_free(sb, &sb->metadata);
		if (combined(sb))
			return;
		sb->snapdata.chunks_used = sb->snapdata.asi->chunks - count_free(sb, &sb->snapdata);
#endif
		if (sb->image.flags & SB_BUSY) {
			warn("Server was not shut down properly");
			//jtrace(show_journal(sb););
			replay_journal(sb); // !!! handle error
		} else {
			sb->image.flags |= SB_BUSY;
			set_sb_dirty(sb);
			save_sb(sb);
		}
		break;
	}
	case LIST_SNAPSHOTS:
	{
		struct snapshot *snapshot = sb->image.snaplist;
		unsigned int n = sb->image.snapshots;

		outhead(sock, SNAPSHOT_LIST, sizeof(int) + n * sizeof(struct snapinfo));
		fdwrite(sock, &n, sizeof(int));
		for (; n--; snapshot++) {
			fdwrite(sock, &(struct snapinfo){ 
					.snap   = snapshot->tag,
					.prio   = snapshot->prio,
					.ctime  = snapshot->ctime,
					.usecnt = usecount(sb, snapshot)},
				sizeof(struct snapinfo));
		}
		break;
	}
	case PRIORITY:
	{
		u32 tag = ((struct priority_info *)message.body)->snap;
		s8 prio  = ((struct priority_info *)message.body)->prio;
		struct snapshot *snapshot;
		uint32_t err = 0;
		char err_msg[MAX_ERRMSG_SIZE];

		if (tag == (u32)~0UL) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Can not set priority for origin");
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_INVALID_SNAPSHOT;
			goto prio_error;
		}
		if (!(snapshot = find_snap(sb, tag))) {
			warn("Snapshot tag %u is not valid", tag);
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Snapshot tag %u is not valid", tag);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_INVALID_SNAPSHOT;
			goto prio_error;
		}
		snapshot->prio = prio;
		if (outbead(sock, PRIORITY_OK, struct priority_ok, snapshot->prio) < 0)
			warn("unable to reply to set priority message");
		break;
	prio_error:
		outerror(sock, err, err_msg);
		break;
	}
	case USECOUNT:
	{
		u32 tag = ((struct usecount_info *)message.body)->snap;
		int usecnt_dev = ((struct usecount_info *)message.body)->usecnt_dev;
		int err = 0;
		char err_msg[MAX_ERRMSG_SIZE];

#ifdef DEBUG_ERROR
		snprintf(err_msg, MAX_ERRMSG_SIZE, "Debug mode to test error messages");
		err_msg[MAX_ERRMSG_SIZE-1] = '\0';
		goto usecnt_error;
#endif

		struct snapshot *snapshot;
		if (tag == (u32)~0UL) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Setting the usecount of the origin.");
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_INVALID_SNAPSHOT;
			goto usecnt_error; /* not really an error though */
		}
		if (!(snapshot = find_snap(sb, tag))) {
			warn("Snapshot tag %u is not valid", tag);
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Snapshot tag %u is not valid", tag);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_INVALID_SNAPSHOT;
			goto usecnt_error;
		}
		/* check for overflow against persistent + transient usecount */
		if ((usecnt_dev >= 0) && (((usecount(sb, snapshot) + usecnt_dev) >> 16) != 0)) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Usecount overflow.");
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_USECOUNT;
			goto usecnt_error;
		}
		/* check for usecount underflow against persistent usecount only */
		if ((usecnt_dev < 0) && (((snapshot->usecount + usecnt_dev) >> 16) != 0)) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Usecount underflow.");
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			err = ERROR_USECOUNT;
			goto usecnt_error;
		}

		snapshot->usecount += usecnt_dev;
		if (outbead(sock, USECOUNT_OK, struct usecount_ok, .usecount = usecount(sb, snapshot)) < 0)
			warn("unable to reply to USECOUNT message");
		break;

	usecnt_error:
		outerror(sock, err, err_msg);
		break;
	}
	case STREAM_CHANGELIST:
	{
		u32 tag1 = ((struct stream_changelist *)message.body)->snap1;
		u32 tag2 = ((struct stream_changelist *)message.body)->snap2;
		int against_origin = (tag1 == (u32)~0UL);
		char *error = "unable to generate changelist";
		err = EINVAL;

		struct snapshot *snapshot1 = NULL;
		struct snapshot *snapshot2 = find_snap(sb, tag2);

		if (!against_origin) {
			snapshot1 = find_snap(sb, tag1);
			error = "source snapshot does not exist";
			if (!snapshot1)
				goto eek;
		}
		if (!snapshot2) {
			error = "destination snapshot does not exist";
			goto eek;
		}
		/* Danger, Danger!!!  This is going to turn into a bug when tree walks go
		   incremental because pointers to the snapshot list are short lived.  Need
		   to pass the tags instead.  Next round of cleanups. */

		trace_on(printf("generating changelist from snapshot tags %u and %u\n", tag1, tag2););
		error = "unable to generate changelist";
		struct gen_changelist gcl = {
			.cl = init_change_list(sb->snapdata.asi->allocsize_bits, tag1, tag2),
			.mask1 = against_origin ? ~0ULL : 1ULL << snapshot1->bit,
			.mask2 = 1ULL << snapshot2->bit };

		if (!gcl.cl)
			goto eek;
		if ((err = traverse_tree_range(sb, 0, -1, gen_changelist_leaf, &gcl)))
			goto eek_free;
		trace_on(printf("sending list of "U64FMT" changed chunks\n", gcl.cl->count););
		error = "unable to send reply to stream change list message";
		if (outbead(sock, STREAM_CHANGELIST_OK, struct changelist_stream, gcl.cl->count, sb->snapdata.asi->allocsize_bits) < 0)
			goto eek_free;
		if (writepipe(sock, gcl.cl->chunks, gcl.cl->count * sizeof(gcl.cl->chunks[0])) < 0)
			goto eek_free;
		free_change_list(gcl.cl);
		break;
	eek_free:
		free_change_list(gcl.cl);
	eek:
		warn("%s", error);
		outerror(sock, err, error);
		break;
	}
	case STATUS:
	{
		if (message.head.length > sizeof(struct status_request)) { //maybe we should allow messages to be long and assume zero filled for upward compatibility?
			goto message_too_long;
		} else if (message.head.length < sizeof(struct status_request)) {
			goto message_too_short;
		}

		get_status(sb, sock);
		break;
	}
	case REQUEST_SNAPSHOT_STATE:
	{
		struct status_request request;

		if (message.head.length != sizeof(request)) {
			outerror(sock, EINVAL, "state_request has wrong length");
			break;
		}
		memcpy(&request, message.body, sizeof(request));

		struct snapshot const *snaplist = sb->image.snaplist;
		struct state_message reply;
		unsigned int i;

		reply.snap = request.snap;
		reply.state = 1;
		for (i = 0; i < sb->image.snapshots; i++)
			if (snaplist[i].tag == request.snap) {
				reply.state = is_squashed(&snaplist[i]) ? 2 : 0;
				break;
			}

		if (outbead(sock, SNAPSHOT_STATE, struct state_message, reply.snap, reply.state) < 0)
			warn("unable to send state message");
		break;
	}
	case REQUEST_SNAPSHOT_SECTORS:
	{
		u32 snap = ((struct status_request *)message.body)->snap;
		struct snapshot const *snaplist = sb->image.snaplist;
		struct snapshot_sectors reply;
		char err_msg[MAX_ERRMSG_SIZE];
		int i;

		reply.snap = snap;
		reply.count = 0;
		if (snap == (u32)~0UL) {
			reply.count = sb->image.orgsectors;
		} else {
			for (i = 0; i < sb->image.snapshots; i++)
				if (snaplist[i].tag == snap) {
					reply.count = is_squashed(&snaplist[i]) ? 0 : snaplist[i].sectors;
					break;
				}
		}
		if (reply.count == 0) {
			snprintf(err_msg, MAX_ERRMSG_SIZE, "Snapshot %u is not valid", snap);
			err_msg[MAX_ERRMSG_SIZE-1] = '\0';
			outerror(sock, EINVAL, err_msg);
		} else if (outbead(sock, SNAPSHOT_SECTORS, struct snapshot_sectors, reply.snap, reply.count) < 0)
			warn("unable to send snapshot sectors message");
		break;
	}
	case RESIZE:
	{
		int err;
		u64 orgsize = ((struct resize_request *)message.body)->orgsize;
		u64 snapsize = ((struct resize_request *)message.body)->snapsize;
		u64 metasize = ((struct resize_request *)message.body)->metasize;

		if (sb->metadev == sb->snapdev) {
			if (snapsize && metasize && snapsize != metasize) {
				outerror(sock, EINVAL, "snapshot device and metadata device are the same, can't resize them to two values");
				break;
			}
			if (!metasize)
				metasize = snapsize;
		}
		err = change_device_sizes(sb, orgsize, snapsize, metasize);
		check_freespace(sb);
		/* return the device sizes after change_device_sizes */
		orgsize = sb->image.orgsectors << SECTOR_BITS;
		metasize = sb->image.metadata.chunks << sb->image.metadata.allocsize_bits;
		snapsize = (sb->metadev == sb->snapdev) ? (sb->image.metadata.chunks << sb->image.metadata.allocsize_bits) : (sb->image.snapdata.chunks << sb->image.snapdata.allocsize_bits);
		if (outbead(sock, RESIZE, struct resize_request, orgsize, snapsize, metasize) < 0)
			warn("unable to send resize reply");
		break;
	}
	case SHUTDOWN_SERVER:
		return -2;
		
	case PROTOCOL_ERROR:
	{	struct protocol_error *pe = malloc(message.head.length);
		
		if (!pe) {
			warn("received protocol error message; unable to retrieve information");
			break;
		}
		
		memcpy(pe, message.body, message.head.length);
		
		char * err_msg = "No message sent";
		if (message.head.length - sizeof(struct protocol_error) > 0) {
			pe->msg[message.head.length - sizeof(struct protocol_error) - 1] = '\0';
			err_msg = pe->msg;
		}
		warn("protocol error message - error code: %x unknown code: %x message: %s", 
				pe->err, pe->culprit, err_msg); 
		free(pe);
		break;
	}
	default: 
	{
		uint32_t proto_err  = ERROR_UNKNOWN_MESSAGE;
		char *err_msg = "Server received unknown message";
		warn("snapshot server received unknown message code=%x, length=%u", message.head.code, message.head.length);
		if (outhead(sock, PROTOCOL_ERROR, sizeof(struct protocol_error) + strlen(err_msg) +1) < 0 ||
			writepipe(sock, &proto_err, sizeof(uint32_t)) < 0 ||
			writepipe(sock, &message.head.code, sizeof(uint32_t)) < 0 ||
			writepipe(sock, err_msg, strlen(err_msg) + 1) < 0)
				warn("unable to send unknown message error");
	}

	} /* end switch statement */
	
#ifdef SIMULATE_CRASH
	static int messages = 0;
	if (++messages == 5) {
		warn(">>>>Simulate server crash<<<<");
		exit(1);
	}
#endif
	return 0;

 message_too_long:
	warn("message %x too long (%u bytes) (disconnecting client)", message.head.code, message.head.length);
#ifdef DEBUG_BAD_MESSAGES
	for (;;) {
		unsigned int byte;

		if ((err = readpipe(sock, &byte, 1)))
			return -1;
		warn("%02x", byte);
	}
#endif
	return -1;
 message_too_short:
	warn("message %x too short (%u bytes) (disconnecting client)", message.head.code, message.head.length);
	return -1;
 pipe_error:
	return -1; /* we quietly drop the client if the connect breaks */
}

static int cleanup(struct superblock *sb)
{
	event_hook(0, SHUTDOWN_SERVER); /* event hook for abort action */
	commit_deferred_allocs(sb);
	sb->image.flags &= ~SB_BUSY;
	set_sb_dirty(sb);
	save_sb(sb);
	return 0;
}

int snap_server_setup(const char *agent_sockname, const char *server_sockname, int *listenfd, int *agentfd)
{
	struct sockaddr_un server_addr = { .sun_family = AF_UNIX };
	int server_addr_len = sizeof(server_addr) - sizeof(server_addr.sun_path) + strlen(server_sockname);

	if (strlen(server_sockname) > sizeof(server_addr.sun_path) - 1)
		error("server socket name too long, %s", server_sockname);
	strncpy(server_addr.sun_path, server_sockname, sizeof(server_addr.sun_path));
	unlink(server_sockname);

	if ((*listenfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		error("Can't get AF_UNIX socket: %s", strerror(errno));
	if (bind(*listenfd, (struct sockaddr *)&server_addr, server_addr_len) == -1)
		error("Can't bind to socket %s: %s", server_sockname, strerror(errno));
	if (listen(*listenfd, 5) == -1)
		error("Can't listen on socket: %s", strerror(errno));

	warn("ddsnapd server bound to socket %s", server_sockname);

	/* Get agent connection */
	struct sockaddr_un agent_addr = { .sun_family = AF_UNIX };
	int agent_addr_len = sizeof(agent_addr) - sizeof(agent_addr.sun_path) + strlen(agent_sockname);

	trace(warn("Connect to control socket %s", agent_sockname););
	if ((*agentfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		error("Can't get AF_UNIX socket: %s", strerror(errno));
	strncpy(agent_addr.sun_path, agent_sockname, sizeof(agent_addr.sun_path));
	if (agent_sockname[0] == '@')
		agent_addr.sun_path[0] = '\0';
	if (connect(*agentfd, (struct sockaddr *)&agent_addr, agent_addr_len) == -1)
		error("Can't connect to control socket %s: %s", agent_sockname, strerror(errno));
	trace(warn("Established agent control connection"););

	struct server_head server_head = { .type = AF_UNIX, .length = (strlen(server_sockname) + 1) };
	trace(warn("server socket name is %s and length is %d", server_sockname, server_head.length););
	event_hook(*agentfd, SERVER_READY);
	if (writepipe(*agentfd, &(struct head){ SERVER_READY, sizeof(struct server_head) }, sizeof(struct head)) < 0 ||
	    writepipe(*agentfd, &server_head, sizeof(server_head)) < 0 ||
	    writepipe(*agentfd, server_sockname, server_head.length) < 0)
		error("Unable to send SEVER_READY msg to agent: %s", strerror(errno));

	return 0;
}

#ifdef DDSNAP_MEM_MONITOR

#include <sys/time.h>
#include <sys/resource.h>

/*
 * itoa() and reverse() lifted straight from K&R C, 2nd edition.
 */
void reverse(char s[])
{
	int c, i, j;

	for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}

void itoa(int n, char s[])
{
	int i, sign;
	sign = n;
	
	i = 0;
	do {
		s[i++] = abs(n % 10) + '0';
	} while (n /= 10);
	if (sign < 0)
		s[i++] = '-';
	
	s[i] = '\0';
	reverse(s);
}

void
ddsnap_mem_monitor(void)
{
	extern int mmon_interval;
	static int old_maxrss = -1;
	struct rusage ru;
	char buf[128];

	if (mmon_interval <= 0)		/* No interval?  Don't monitor.       */
		return;
	if (getrusage(RUSAGE_SELF, &ru) < 0) {
		perror("getrusage() failed");
		mmon_interval = 0;
		return;
	}
	if (ru.ru_maxrss > old_maxrss) {
		/*
		 * Go to great lengths to avoid an allocate as a result of
		 * this log message.
		 */
		strcpy(buf, "WARNING:  RSS increased from ");
		itoa(old_maxrss, &buf[strlen(buf)]);
		strcat(buf, " to ");
		itoa(ru.ru_maxrss, &buf[strlen(buf)]);
		strcat(buf, " @ ");
		itoa(time(NULL), &buf[strlen(buf)]);
		strcat(buf, "\n");
		write(2, buf, strlen(buf));
		old_maxrss = ru.ru_maxrss;
	}
}
#endif /* DDSNAP_MEM_MONITOR */

int snap_server(struct superblock *sb, int listenfd, int getsigfd, int agentfd, const char *logfile)
{
	unsigned maxclients = 100, clients = 0, others = 3;
	struct client *clientvec[maxclients];
	struct pollfd pollvec[others+maxclients];
	int err = 0;
#ifdef DDSNAP_MEM_MONITOR
	int poll_timeout = -1;
	extern int mmon_interval;
#endif

	pollvec[0] = (struct pollfd){ .fd = listenfd, .events = POLLIN };
	pollvec[1] = (struct pollfd){ .fd = getsigfd, .events = POLLIN };
	pollvec[2] = (struct pollfd){ .fd = agentfd, .events = POLLIN };

	if ((err = prctl(PR_SET_LESS_THROTTLE, 0, 0, 0, 0)))
		warn("can not set process to throttle less (error %i, %s)", errno, strerror(errno));
	if ((err = prctl(PR_SET_MEMALLOC, 0, 0, 0, 0)))
		warn("failed to enter memalloc mode (may deadlock) (error %i, %s)", errno, strerror(errno));
	event_parse_options();
#ifdef DDSNAP_MEM_MONITOR
	if (mmon_interval > 0)
		poll_timeout = mmon_interval;
	ddsnap_mem_monitor();
#endif	
	while (1) {
		trace(warn("Waiting for activity"););

#ifdef DDSNAP_MEM_MONITOR
		int activity = poll(pollvec, others+clients, poll_timeout);
#else
		int activity = poll(pollvec, others+clients, -1);
#endif
		if (activity < 0) {
			if (errno != EINTR)
				error("poll failed: %s", strerror(errno));
			continue;
		}

		if (!activity) {
#ifdef DDSNAP_MEM_MONITOR
			ddsnap_mem_monitor();
			if (mmon_interval == 0) /* If we stopped monitoring, */
				poll_timeout = -1; /* kill timeout.          */
#else
			printf("waiting...\n");
#endif
			continue;
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			unsigned int addr_len = sizeof(addr);
			int clientfd;

			if ((clientfd = accept(listenfd, (struct sockaddr *)&addr, &addr_len))<0)
				error("Cannot accept connection: %s", strerror(errno));

			trace_on(warn("Received connection"););
			assert(clients < maxclients); // !!! send error and disconnect

			struct client *client = malloc(sizeof(struct client));
			*client = (struct client){ .sock = clientfd };
			clientvec[clients] = client;
			pollvec[others+clients] = 
				(struct pollfd){ .fd = clientfd, .events = POLLIN };
			clients++;
		}

		/* Signal? */
		if (pollvec[1].revents) {
			u8 sig = 0;
			/* it's stupid but this read also gets interrupted, so... */
			do { } while (read(getsigfd, &sig, 1) == -1 && errno == EINTR);
			trace_on(warn("Caught signal %i", sig););
			switch (sig) {
				case SIGINT:
				case SIGTERM:
					cleanup(sb); // !!! don't do it on segfault
					(void)flush_buffers();
					evict_buffers();
					signal(sig, SIG_DFL); /* this should happen automatically */
					raise(sig); /* commit harikiri */
					err = DDSNAPD_CAUGHT_SIGNAL; /* FIXME we never get here */
					goto done;
					break;
				case SIGHUP:
					fflush(stderr);
					fflush(stdout);
					re_open_logfile(logfile);
					break;
				default:
					warn("I don't handle signal %i", sig);
					break;
			}
		}

		/* Agent message? */
		if (pollvec[2].revents) {
			if (pollvec[2].revents & (POLLHUP|POLLERR)) { /* agent went away */
				cleanup(sb);
				err = DDSNAPD_AGENT_ERROR;
				goto done;
			}
			incoming(sb, &(struct client){ .sock = agentfd, .id = -2, .snaptag = -2 });
		}

		/* Client message? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				struct client *client = clientvec[i];
				int result;

				trace_off(printf("event on socket %i = %x\n", client->sock, pollvec[others+i].revents););
				if ((result = incoming(sb, client)) == -1) {
					warn("Client %Lx disconnected", client->id);

					if ((client->flags & SNAPCLIENT_BIT)) {
						struct snapshot *snapshot = client_snap(sb, client);
						if (!is_squashed(snapshot)) {
							assert(sb->usecount[snapshot->bit] > 0);
							sb->usecount[snapshot->bit]--;
						}
						free_client_locks(sb, client);
					}
					close(client->sock);
					free(client);
					--clients;
					clientvec[i] = clientvec[clients];
					pollvec[others + i] = pollvec[others + clients];
					continue;
				}

				if (result == -2) { // !!! wrong !!!
					cleanup(sb);
					err = DDSNAPD_CLIENT_ERROR;
					goto done;
				}
			}
			i++;
		}
	}
done:
	// in a perfect world we'd close all the connections
	close(listenfd);
	return err;
}

struct superblock *new_sb(int metadev, int orgdev, int snapdev)
{
	int error;
	struct superblock *sb;
	if ((error = posix_memalign((void **)&sb, SECTOR_SIZE, sizeof(*sb))))
		error("no memory for superblock: %s", strerror(error));
	*sb = (struct superblock){ .orgdev = orgdev, .snapdev = snapdev, .metadev = metadev };
	return sb;
}

int sniff_snapstore(int metadev)
{
	struct superblock *sb = new_sb(metadev, -1, -1);
	if (diskread(metadev, &sb->image, 4096, SB_SECTOR << SECTOR_BITS) < 0) {
		warn("Unable to read superblock: %s", strerror(errno));
		return -1;
	}
	int sniff = valid_sb(sb);
	free(sb);
	return sniff;
}

int init_snapstore(int orgdev, int snapdev, int metadev, unsigned bs_bits, unsigned cs_bits, unsigned js_bytes)
{
	struct superblock *sb = new_sb(metadev, orgdev, snapdev);

	unsigned bufsize = 1 << bs_bits;
	init_buffers(bufsize, 0); /* do not preallocate buffers */
	if (init_super(sb, js_bytes, bs_bits, cs_bits) < 0)
		goto fail;
	if (init_journal(sb) < 0)
		goto fail;
	save_sb_check(sb);
	free(sb->copybuf);
	free(sb->snaplocks);
	free(sb);
	return 0;
fail:
	warn("Snapshot storage initiailization failed");
	return -1;
}

int start_server(
	int orgdev, int snapdev, int metadev, 
	char const *agent_sockname, char const *server_sockname, char const *logfile, char const *pidfile,
	int nobg, uint64_t cachesize_bytes, enum runflags flags)
{
	struct superblock *sb = new_sb(metadev, orgdev, snapdev);

	if (!sb)
		error("unable to allocate superblock\n");

	if (diskread(sb->metadev, &sb->image, 4096, SB_SECTOR << SECTOR_BITS) < 0)
		warn("Unable to read superblock: %s", strerror(errno));

	int listenfd, getsigfd, agentfd, ret;

	if (!valid_sb(sb))
		error("Invalid superblock: please run 'ddsnap-sb' first to upgrade the superblock.\n");
	sb->runflags = flags;

	unsigned bufsize = 1 << sb->image.metadata.allocsize_bits;
	if (cachesize_bytes == 0) {
		/* default mem_pool_size is min(1/4 of physical RAM,  128MB)
		 * because "128MB is enough for now" and "want to run on tiny UML instances"
		 * It's probably wrong for anything but our smoke test, 
		 * which is why we allow caller to override default.
		 */
		cachesize_bytes = 128 << 20;
		struct sysinfo info;
		if (!sysinfo(&info)) {
			const u64 QUARTER_TOTAL_RAM = (u64)(info.totalram / 4) * (u64)info.mem_unit;
			if (cachesize_bytes > QUARTER_TOTAL_RAM)
				cachesize_bytes = QUARTER_TOTAL_RAM;
		}
	}
	/* The buffer system will grow if you don't preallocate buffers,
	 * but if you preallocate, it won't allow growth.
	 * Growth could cause deadlock, so pass a cachesize
	 * to tell init_buffers to set the initial and max cache size the same.
	 */
	trace_on(warn("Setting cache size to %llu bytes.\n", cachesize_bytes););
	init_buffers(bufsize, cachesize_bytes);
	
	if (snap_server_setup(agent_sockname, server_sockname, &listenfd, &agentfd) < 0)
		error("Could not setup snapshot server\n");

	if (!nobg) {
		pid_t pid;
		
		if (!logfile)
			logfile = "/var/log/ddsnap.server.log";
		pid = daemonize(logfile, pidfile, &getsigfd);
		if (pid == -1)
			error("Could not daemonize\n");
		if (pid != 0) {
			trace_on(printf("pid = %lu\n", (unsigned long)pid););
			return 0;
		}
	}

	/* To avoid writeout deadlock, we can't let ourselves be
	 * swapped out, so lock everything into RAM.
	 */
	if (mlockall(MCL_CURRENT|MCL_FUTURE))
		warn("Unable to lock self into RAM: %s", strerror(errno));

#ifdef DDSNAP_MEM_MONITOR
	ddsnap_mem_monitor();
#endif

	/* should only return on an error */
	if ((ret = snap_server(sb, listenfd, getsigfd, agentfd, logfile)) < 0)
		warn("server exited with error %i", ret);

	return 0;
}
