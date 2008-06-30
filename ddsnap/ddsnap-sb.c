/*
 * This program is used to upgrade the ondisk ddsnap superblock.
 * You should add an upgrade function here and change the SB_MAGIC in ddsnapd.c
 * everytime you change the ddsnap ondisk superblock structure.
 */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>
#include "diskio.h"
#include "buffer.h"
#include "trace.h"
#include "ddsnap.h"
#include "dm-ddsnap.h"

// FIXME: the following micros are copied from ddsnapd.c, move them to a header file
#define MAX_SNAPSHOTS 64
#define SB_SECTOR 8		/* Sector where superblock lives.     */
#define SB_SECTORS 8		/* Size of ddsnap super block in sectors */

#define SB_MAGIC_070604 { 't', 'e', 's', 't', 0xdd, 0x07, 0x06, 0x04 }	/* 0.4 - 0.7 release sb magic */
#define SB_MAGIC_080325 { 't', 'e', 's', 't', 0xdd, 0x08, 0x03, 0x25 }	/* 0.8 release sb magic */
#define SB_MAGIC { 't', 'e', 's', 't',  0xdd, 0x07, 0x06, 0x04 }	/* date of latest incompatible sb format */

struct allocspace_img {
	sector_t bitmap_base;	/* Allocation bitmap starting sector. */
	sector_t chunks;	/* if zero then snapdata is combined in metadata space */
	sector_t freechunks;
	chunk_t last_alloc;	/* Last chunk allocated.              */
	u64 bitmap_blocks;	/* Num blocks in allocation bitmap.   */
	u32 allocsize_bits;	/* Bits of number of bytes in chunk. */
};

struct disksuper_04 {		/* the subfix is the data of latest incompatible sb format: yymmdd */
	typeof((char[])SB_MAGIC) magic;
	u64 create_time;
	sector_t etree_root;	/* The b-tree root node sector.       */
	sector_t orgoffset, orgsectors;
	u64 flags;
	u64 deleting;
	struct snapshot_04 {
		u32 ctime;	// upper 32 bits are in super create_time
		u32 tag;	// external name (id) of snapshot
		u16 usecount;	// persistent use count on snapshot device
		u8 bit;		// internal snapshot number, not derived from tag
		s8 prio;	// 0=normal, 127=highest, -128=lowest
		char reserved[4];	/* adds up to 16 */
	} snaplist[MAX_SNAPSHOTS];	// entries are contiguous, in creation order
	u32 snapshots;
	u32 etree_levels;
	s32 journal_base;	/* Sector offset of start of journal. */
	s32 journal_next;	/* Next chunk of journal to write.    */
	s32 journal_size;	/* Size of the journal in chunks.     */
	u32 sequence;		/* Latest commit block sequence num.  */
	struct allocspace_img metadata, snapdata;
};

struct snapshot_06 {
	u32 ctime;		// upper 32 bits are in super create_time
	u32 tag;		// external name (id) of snapshot
	u16 usecount;		// persistent use count on snapshot device
	u8 bit;			// internal snapshot number, not derived from tag
	s8 prio;		// 0=normal, 127=highest, -128=lowest
	u64 sectors;		// sectors of the snapshot
};

struct disksuper_06 {
	typeof((char[])SB_MAGIC) magic;
	u64 create_time;
	sector_t etree_root;	/* The b-tree root node sector.       */
	sector_t orgoffset, orgsectors;
	u64 flags;
	u64 deleting;
	struct snapshot_06 snaplist[MAX_SNAPSHOTS];	// entries are contiguous, in creation order
	u32 snapshots;
	u32 etree_levels;
	s32 journal_base;	/* Sector offset of start of journal. */
	s32 journal_next;	/* Next chunk of journal to write.    */
	s32 journal_size;	/* Size of the journal in chunks.     */
	u32 sequence;		/* Latest commit block sequence num.  */
	struct allocspace_img metadata, snapdata;
};

static int sb_print(int metadev)
{
	struct disksuper_06 sb;	/* latest disksuper structure */
	int err;
	if ((err = diskread(metadev, &sb, sizeof(sb), SB_SECTORS << SECTOR_BITS)) < 0) {
		warn("Unable to write superblock: %s", strerror(errno));
		return err;
	}
	printf("creation_time %llu, flags %llu\n", (unsigned long long)sb.create_time, (unsigned long long)sb.flags);
	for (int i = 0; i < sb.snapshots; i++) {
		printf("ctime %u, tag %u, usecount %u, bit %u, prio %d",
		       sb.snaplist[i].ctime, sb.snaplist[i].tag,
		       sb.snaplist[i].usecount, sb.snaplist[i].bit, sb.snaplist[i].prio);
		printf(", sectors %llu", sb.snaplist[i].sectors);
		printf("\n");
	}
	printf("snapshots %d\n", sb.snapshots);
	return 0;
}

/* upgrade from 0.4 release to 0.6 release */
static int upgrade_04(int metadev, char *magic)
{
	struct disksuper_04 oldsb;
	struct disksuper_06 sb;
	int err;

	warn("upgrade sb from 0.4 format to 0.6 format");
	if ((err = diskread(metadev, &oldsb, sizeof(struct disksuper_04), SB_SECTORS << SECTOR_BITS)) < 0) {
		warn("Unable to read old superblock: %s", strerror(errno));
		return err;
	}

	memcpy(&sb, &oldsb, ((char *)&oldsb.snaplist - (char *)&oldsb));
	for (int i = 0; i < oldsb.snapshots; i++) {
		memcpy(&sb.snaplist[i], &oldsb.snaplist[i], sizeof(struct snapshot_06));
		sb.snaplist[i].sectors = oldsb.orgsectors;
	}
	memcpy(&sb.snapshots, &oldsb.snapshots,
	       ((char *)&oldsb + sizeof(struct disksuper_04) - (char *)&oldsb.snapshots));

	if ((err = diskwrite(metadev, &sb, sizeof(struct disksuper_06), SB_SECTORS << SECTOR_BITS)) < 0) {
		warn("Unable to write superblock: %s", strerror(errno));
		return err;
	}
	return err;
}

/* upgrade from 0.6 release to 0.8 */
static int upgrade_06(int metadev, char *magic)
{
	int err;
	warn("just overwrite the sb magic");
	memcpy(magic, (char[])SB_MAGIC_080325, sizeof((char[])SB_MAGIC_080325));
	if ((err = diskwrite(metadev, magic, sizeof((char[])SB_MAGIC_080325), SB_SECTORS << SECTOR_BITS)) < 0)
		warn("Unable to write superblock: %s", strerror(errno));
	return err;
}

int main(int argc, char *argv[])
{
	typeof((char[])SB_MAGIC) magic;
	int metadev, err, release4 = 0;

	if (argc < 2)
		error("usage: %s <metadev> [0.4]", argv[0]);
	if ((metadev = open(argv[1], O_RDWR | O_SYNC)) < 0)
		error("unable to open the metadata device, err %s\n", strerror(errno));
	if ((err = diskread(metadev, magic, sizeof(magic), SB_SECTORS << SECTOR_BITS)) < 0) {
		warn("Unable to read superblock magic: %s", strerror(errno));
		goto out;
	}
	warn("sb magic %c%c%c%c %2x-%2x-%2x", magic[0], magic[1], magic[2],
	     magic[3], (unsigned)magic[5], (unsigned)magic[6], (unsigned)magic[7]);

	/* forgot to change sb magic number in 0.6 release, so need special handling between 0.4 to 0.6 */
	if (argc > 2 && !strcmp(argv[2], "0.4") && !memcmp(magic, (char[])SB_MAGIC_070604, sizeof(magic)))
		release4 = 1;

	while (!err && (memcmp(magic, (char[])SB_MAGIC, sizeof(magic)) || release4)) {
		if (!memcmp(magic, (char[])SB_MAGIC_070604, sizeof(magic))) {
			if (release4) {
				upgrade_04(metadev, magic);
				release4 = 0;
			} else
				upgrade_06(metadev, magic);
		} else if (!memcmp(magic, (char[])SB_MAGIC_080325, sizeof(magic))) {
			printf("magic number is up to date, do nothing\n");
			//upgrade_08(metadev, magic);
		} else {
			printf("superblock magic %s not supported\n", magic);
			err = -EINVAL;
		}
	}
	sb_print(metadev);
out:
	close(metadev);
	return err;
}
