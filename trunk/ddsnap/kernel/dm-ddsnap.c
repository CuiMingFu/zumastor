#include <linux/version.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/syscalls.h> // recvmsg
#include <linux/socket.h>
#include <linux/un.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/bug.h>
#include <linux/bio.h>
#include <linux/proc_fs.h>
#include <linux/sysrq.h>
#include <linux/jiffies.h>
#include "dm.h"
#include "dm-ddsnap.h"

#define DM_MSG_PREFIX "ddsnap"

#define warn(string, args...) do { printk("%s: " string "\n", __func__, ##args); } while (0)
#define error(string, args...) do { warn(string, ##args); BUG(); } while (0)
#define assert(expr) do { if (!(expr)) error("Assertion " #expr " failed!\n"); } while (0)
#define trace_on(args) args
#define trace_off(args)

#define trace trace_off

/*
 * To do:
 *
 * - variable length bio handling
 * - unique cache
 * - receive chunk size
 * - make pending and hook a union
 * - get rid of multiple ranges per message misfeature
 * - rationalize sector vs chunk usage in messages
 * - detect message id wrap
 * - detect message timeout
 */

/* Useful gizmos */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
static int rwpipe(struct file *file, const void *buffer, unsigned int count,
        ssize_t (*op)(struct kiocb *, const struct iovec *, unsigned long, loff_t), int mode)
#else
static int rwpipe(struct file *file, const void *buffer, unsigned int count,
                        ssize_t (*op)(struct kiocb *, const char *, size_t, loff_t), int mode)
#endif
{
        struct kiocb iocb;
        mm_segment_t oldseg;
        int err = 0;
        trace_off(warn("%s %i bytes", mode == FMODE_READ? "read": "write", count);)
        if (!(file->f_mode & mode))
                return -EBADF;
        if (!op)
                return -EINVAL;
        init_sync_kiocb(&iocb, file); // new in 2.5 (hmm)
        iocb.ki_pos = file->f_pos;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
        iocb.ki_left = count;
#endif

        oldseg = get_fs();
        set_fs(get_ds());
        while (count) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
                int chunk = (*op)(&iocb, &(struct iovec){ .iov_base = (void *)buffer, .iov_len = count }, 1, iocb.ki_pos);
#else
                int chunk = (*op)(&iocb, buffer, count, iocb.ki_pos);
#endif
                if (chunk <= 0) {
                        err = chunk? chunk: -EPIPE;
                        break;
                }
                BUG_ON(chunk > count);
                count -= chunk;
                buffer += chunk;
        }
        set_fs(oldseg);
        file->f_pos = iocb.ki_pos;
        return err;
}

static inline int readpipe(struct file *file, void *buffer, unsigned int count)
{
	return rwpipe(file, buffer, count, file->f_op->aio_read, FMODE_READ);
}

static inline int writepipe(struct file *file, void *buffer, unsigned int count)
{
	return rwpipe(file, buffer, count, file->f_op->aio_write, FMODE_WRITE);
}

#define outbead(SOCK, CODE, STRUCT, VALUES...) ({ \
	struct { struct head head; STRUCT body; } PACKED message = \
		{ { CODE, sizeof(STRUCT) }, { VALUES } }; \
	writepipe(SOCK, &message, sizeof(message)); })

/*
 * This gets the job done but it sucks as an internal interface: there
 * is no reason to deal with fds at all, we just want to receive the
 * (struct file *), we do not want to have to wrap the socket in a
 * fd just to call recv_fd, and user space pointer for the (bogus) data
 * payload is just silly.  Never mind the danger of triggering some
 * wierdo signal handling cruft deep in the socket layer.  This kind of
 * posturing - lathering layers of cruft upon cruft - is the stuff
 * Windows is made of, Linux is not supposed to be like that.  Fixing
 * this requires delving into the SCM_RIGHTS path deep inside sys_recvmsg
 * and breaking out the part that actually does the work, to be a usable
 * internal interface.  Put it on the list of things to do.
 */
static int recv_fd(int sock, char *bogus, unsigned *len)
{
	char payload[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_control = payload,
		.msg_controllen = sizeof(payload),
		.msg_iov = &(struct iovec){ .iov_base = bogus, .iov_len = *len },
		.msg_iovlen = 1,
	};
	mm_segment_t oldseg = get_fs();
	struct cmsghdr *cmsg;
	int result;

	set_fs(get_ds());
	result = sys_recvmsg(sock, &msg, 0);
	set_fs(oldseg);

	if (result <= 0)
		return result;
	if (!(cmsg = CMSG_FIRSTHDR(&msg)))
		return -ENODATA;
	if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
		cmsg->cmsg_level != SOL_SOCKET ||
		cmsg->cmsg_type != SCM_RIGHTS)
		return -EBADMSG;

	*len = result;
	return *((int *)CMSG_DATA(cmsg));
}

static void kick(struct block_device *dev)
{
	request_queue_t *q = bdev_get_queue(dev);
	if (q->unplug_fn)
		q->unplug_fn(q);
}

/* ...Useful gizmos */

#define SECTOR_SHIFT 9
#define IS_SNAP_FLAG (1 << 0)
#define REPORT_BIT 1
#define RECOVER_FLAG (1 << 2)
#define FINISH_FLAG (1 << 3)
#define READY_FLAG (1 << 4)
#define NUM_BUCKETS 64
#define MASK_BUCKETS (NUM_BUCKETS - 1)

struct devinfo {
	u64 id;
	unsigned long flags;
	unsigned chunksize_bits;
	unsigned chunkshift;
//	sector_t len;
	int snap, nextid;
	u32 *shared_bitmap; // !!! get rid of this, use the inode cache
	struct inode  *inode; /* the cache */
	struct dm_dev *orgdev;
	struct dm_dev *snapdev;
	struct file *sock;
	struct file *control_socket;
	struct semaphore server_in_sem;
	struct semaphore server_out_sem;
	struct semaphore more_work_sem;
	struct semaphore recover_sem;
	struct semaphore exit1_sem;
	struct semaphore exit2_sem;
	struct semaphore exit3_sem;
	struct semaphore identify_sem;
	struct list_head pending[NUM_BUCKETS];
	struct list_head queries;
	struct list_head releases;
	struct list_head locked;
	spinlock_t pending_lock;
	spinlock_t end_io_lock;
	int dont_switch_lists;
};

static inline int is_snapshot(struct devinfo *info)
{
	return !!(info->flags & IS_SNAP_FLAG);
}

static inline int running(struct devinfo *info)
{
	return !(info->flags & FINISH_FLAG);
}

static inline int worker_running(struct devinfo *info)
{
        return !(info->flags & (FINISH_FLAG|RECOVER_FLAG));
}

static inline int worker_ready(struct devinfo *info)
{
        return (worker_running(info) && (info->flags & READY_FLAG));
}

static void report_error(struct devinfo *info)
{
	if (test_and_set_bit(REPORT_BIT, &info->flags))
		return;
	up(&info->more_work_sem);
	while (down_interruptible(&info->recover_sem))
		;
	info->flags |= RECOVER_FLAG;
}

/* Static caches, shared by all ddsnap instances */

static struct kmem_cache *pending_cache;
static struct kmem_cache *end_io_cache;
static struct super_block *snapshot_super;

/* We cache query results because we are greedy about speed */

#ifdef CACHE
static u64 *snap_map_cachep(struct address_space *mapping, chunk_t chunk, struct page **p)
{
	u32 page_index;
	u32 page_pos;
	struct page *page;
	u64 *exceptions;

	page_index = chunk / (PAGE_SIZE / sizeof(u64));
	page_pos = chunk % (PAGE_SIZE / sizeof(u64));

	page = find_or_create_page(mapping, page_index, GFP_KERNEL);
	if (page) {
		/* Clean page if it's a new one */
		if (!Page_Uptodate(page)) {
			memset(page_address(page), 0, PAGE_SIZE);
			SetPageUptodate(page);
		}

		exceptions = page_address(page);
		*p = page;
		return &exceptions[page_pos];
	}
	return NULL;
}

static inline int get_unshared_bit(struct devinfo *info, chunk_t chunk)
{
	return (info->shared_bitmap[chunk >> 5] >> (chunk & 31)) & 1;
}

static inline void set_unshared_bit(struct devinfo *info, chunk_t chunk)
{
	info->shared_bitmap[chunk >> 5] |= 1 << (chunk & 31);
}
#endif

/* Hash table matches up query replies to pending requests */

struct pending {
	unsigned id;
	u64 chunk;
	unsigned chunks;
	struct bio *bio;
	typeof(jiffies) timestamp;
	struct list_head list;
};

struct ddsnap_pending_stats {
	typeof(jiffies) max_time;
	typeof(jiffies) min_time;
	typeof(jiffies) avg_time;
};

static void ddsnap_stats_update(struct ddsnap_pending_stats *statp, typeof(jiffies) time)
{
	statp->max_time = (statp->max_time < time) ? time : statp->max_time;
	statp->min_time = (statp->min_time > time) ? time : statp->min_time;
	statp->avg_time += time;
}

static void ddsnap_stats_display(struct ddsnap_pending_stats *statp, unsigned total)
{
	printk("(%u)\n", total);
	if (!total)
		return;
	printk("pending time max:min:avg "); 
	printk("%u ", jiffies_to_msecs(statp->max_time));
	printk("%u ", jiffies_to_msecs(statp->min_time));
	printk("%u\n", jiffies_to_msecs(statp->avg_time) / total);
}

static void show_pending_requests(struct list_head *hlist, unsigned *total, struct ddsnap_pending_stats *pending_stats)
{
	struct list_head *list;
	typeof(jiffies) now = jiffies;
	list_for_each(list, hlist) {
		struct pending *pending = list_entry(list, struct pending, list);
		ddsnap_stats_update(pending_stats, (now - pending->timestamp));
		printk("%u:%Lx:%u ", pending->id, pending->chunk, jiffies_to_msecs(now - pending->timestamp));
		*total = *total + 1;
	}
}

static void show_pending(struct devinfo *info)
{
	unsigned i, total = 0;
	struct ddsnap_pending_stats pending_stats = { 0, ~0UL, 0 }, query_stats = { 0, ~0UL, 0 };

	spin_lock(&info->pending_lock);
	warn("Pending server queries...");
	for (i = 0; i < NUM_BUCKETS; i++) {
		if (!list_empty(info->pending + i)) {
			if (!total)
				printk("[%u]: ", i);
			show_pending_requests(info->pending + i, &total, &pending_stats);
		}
	}
	ddsnap_stats_display(&pending_stats, total);

	if (!list_empty(&info->queries)) {
		total = 0;
		warn("Queued queries...");
		show_pending_requests(&info->queries, &total, &query_stats);
		ddsnap_stats_display(&query_stats, total);
	}
	spin_unlock(&info->pending_lock);
}

static inline unsigned hash_pending(unsigned id)
{
	return id & MASK_BUCKETS;
}

/* Ah, now it gets interesting.  Called in interrupt context */

struct hook {
	struct devinfo *info;
	sector_t sector;
	/* needed only for end_io, make it a union */
	bio_end_io_t *old_end_io;
	void *old_private;
	typeof(jiffies) timestamp;
	/* needed after end_io, for release, make it a union */
	struct list_head list;
};

static int snapshot_read_end_io(struct bio *bio, unsigned int done, int error)
{
	struct hook *hook = bio->bi_private;
	struct devinfo *info = hook->info;
	unsigned long irqflags;

	trace(warn("sector %Lx", (long long)hook->sector);)
	spin_lock_irqsave(&info->end_io_lock, irqflags);
	bio->bi_end_io = hook->old_end_io;
	bio->bi_private = hook->old_private;
	hook->old_end_io = NULL;
	if (!(info->flags & READY_FLAG))
		kmem_cache_free(end_io_cache, hook);
	else if (info->dont_switch_lists == 0)
		list_move(&hook->list, &info->releases);
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
	up(&info->more_work_sem);

	return bio->bi_end_io(bio, done, error);
}

static void show_lock_requests(struct list_head *hlist)
{
	struct list_head *list;
	struct hook *hook;
	unsigned total = 0;
	typeof(jiffies) now = jiffies;
	struct ddsnap_pending_stats lock_stats = { 0, ~0UL, 0 };

	list_for_each(list, hlist) {
		hook = list_entry(list, struct hook, list);
		ddsnap_stats_update(&lock_stats, (now - hook->timestamp));
		printk("%Lx:%u ", (long long)hook->sector, jiffies_to_msecs(now - hook->timestamp));
		total++;
	}
	ddsnap_stats_display(&lock_stats, total);
}

static void show_locked(struct devinfo *info)
{
	unsigned long irqflags;
	spin_lock_irqsave(&info->end_io_lock, irqflags);
	warn("Locked read requests...");
	show_lock_requests(&info->locked);
	warn("Read requests to release...");
	show_lock_requests(&info->releases);
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
}

/* This is the part that does all the work. */

int replied_rw(struct dm_target *target, struct rw_request *body, unsigned length, int rw, int snap, int failed_io)
{
	struct devinfo *info = target->private;
	struct chunk_range *p = body->ranges;
	unsigned shift = info->chunksize_bits - SECTOR_SHIFT, mask = (1 << shift) - 1;
	unsigned long irqflags;
	int i, j, submitted = 0;

	trace(show_pending(info);)
	if(snap) {
		trace(warn("id = %u, %u ranges, %s %s", body->id, body->count,
			rw == READ? "read from": "write to", snap? "snapshot": "origin");)
	}
			
	for (i = 0; i < body->count; i++) { // !!! check for length overrun
		unsigned chunks = p->chunks, id = body->id;
		struct list_head *list, *bucket = info->pending + hash_pending(id);
		struct pending *pending;
		struct bio *bio;

		trace(warn("[%Lx/%x]", p->chunk, chunks);)
		assert(chunks == 1);

		spin_lock(&info->pending_lock);
		list_for_each(list, bucket)
			if ((pending = list_entry(list, struct pending, list))->id == id)
				goto found;
		warn("Can't find pending rw for chunk %u:%Lx", id, p->chunk);
		spin_unlock(&info->pending_lock);
		return -1;
found:
		list_del(&pending->list);
		spin_unlock(&info->pending_lock);

		bio = pending->bio;
		trace(warn("Handle pending IO sector %Lx", (long long)bio->bi_sector);)

		if(failed_io) {
			warn("Unable to handle pending IO server %Lx", (long long)bio->bi_sector);
			kmem_cache_free(pending_cache, pending);
			bio_io_error(bio, bio->bi_size);
			/* we need to call bio_io_error to release the bio buffer for all bio requests */
			continue;
		}

		if (chunks != pending->chunks) {
			warn("Message mismatch, expected %x got %x", chunks, chunks);
			kmem_cache_free(pending_cache, pending);
			bio_io_error(bio, bio->bi_size);
			return -1;
		}

		++p;
		/*
		 * If we're only doing I/O to the snapshot we don't need to
		 * lock the chunk.  If it's shared with the origin (and we're
		 * reading), lock the chunk and give a callback routine that
		 * will release it (and kick ddsnapd) when the I/O completes.
		 */
		if (snap) {
			chunk_t *p2 = (chunk_t *)p;
			for (j = 0; j < chunks; j++) {
				u64 physical = (*p2++ << shift) + (bio->bi_sector & mask);
				trace(warn("logical %Lx = physical %Lx", (u64)bio->bi_sector, physical));
				bio->bi_bdev = info->snapdev->bdev;
				bio->bi_sector = physical;
			}
			p = (struct chunk_range *)p2;
		} else if (rw == READ) {
			/* snapshot read from origin */
			struct hook *hook;
			trace(warn("hook end_io for %Lx", (long long)bio->bi_sector));
			hook = kmem_cache_alloc(end_io_cache, GFP_NOIO|__GFP_NOFAIL); // !!! union with pending
			*hook = (struct hook){
				.info = info,
				.sector = bio->bi_sector,
				.old_end_io = bio->bi_end_io,
				.timestamp = jiffies,
				.old_private = bio->bi_private };
			bio->bi_end_io = snapshot_read_end_io;
			bio->bi_private = hook;
			spin_lock_irqsave(&info->end_io_lock, irqflags);
			list_add(&hook->list, &info->locked);
			spin_unlock_irqrestore(&info->end_io_lock, irqflags);
		}

		generic_make_request(bio);
		submitted++;
#ifdef CACHE
		for (j = 0; j < p->chunks; j++)
			set_unshared_bit(info, chunk + j);
#endif
		kmem_cache_free(pending_cache, pending);
	}
	if (submitted){
		kick(info->orgdev->bdev);
		kick(info->snapdev->bdev);
	}

	return 0;
}

/*
 * There happen to be four flavors of server replies to rw queries, two
 * write and two read, but the symmetry ends there.  Only one flavor
 * (write) is for origin IO, because origin reads do not need global
 * synchronization.  The remaining three flavors are for snapshot IO.
 * Snapshot writes are always to the snapshot store, so there is only
 * one flavor.  On the other hand, snapshot reads can be from either
 * the origin or the snapshot store.  Only the server can know which.
 * Either or both kinds of snapshot read reply are possible for a given
 * query, which is where things get nasty.  These two kinds of replies
 * can be interleaved arbitrarily along the original read request, and
 * to just to add a little more spice, the server may not send back the
 * results for an entire query in one message (it may decide to service
 * other queries first, or replly about the 'easiest' chunks first). The
 * client has to match up all these reply fragments to the original
 * request and decide what to do.  Such bizarre fragmentation of the
 * incoming request is unavoidable, it results from write access
 * patterns to the origin.  We just have to grin and deal with it.  So
 * without further ado, here is how the various reply flavors
 *
 * - Origin write replies just have logical ranges, since origin physical 
 *   address is the same as logical.
 *
 * - Snapshot read replies come back in two separate messages, one for
 *   the origin reads (if any) and one for the snapstore reads (if any),
 *   the latter includes snapstore addresses.  Origin reads are globally
 *   locked by the server, so we must send release messages on
 *   completion.
 *
 * - Snapshot writes are always to the snapstore, so snapstore write
 *   replies always include snapstore addresses.
 *
 * We know whether we're supposed to be a snapshot or origin client,
 * but we only use that knowledge as a sanity check.  The message codes
 * tell us explicitly whether the IO target is origin or snapstore.
 */

/*
 * For now, we just block on incoming message traffic, so this daemon
 * can't do any other useful work.  It could if we used nonblocking pipe
 * IO but we have been too lazy to implement it so far.  So we have one
 * more daemon than we really need, and maybe we will get energetic one
 * day soon and get rid of it.
 *
 * When it comes time to destroy things, the daemon has to be kicked
 * out of its blocking wait, if it is in one, which it probably is.  We
 * do that by shutting down the socket.  This unblocks the waiters and
 * feeds them errors.  Does this work for all flavors of sockets?  I
 * don't know.  It obviously should, but we've seen some pretty silly
 * limitations in our long life, so nothing would surprise us at this
 * point.
 */
static int incoming(struct dm_target *target)
{
	struct devinfo *info = target->private;
	struct messagebuf message; // !!! have a buffer in the target->info
	struct file *sock;
	struct task_struct *task = current;
	int err, length;
	char *err_msg;
	u32 chunksize_bits;
#ifdef CONFIG_DM_DDSNAP_SWAP
	struct socket *vm_sock;
#endif

	daemonize("%s/%x", "ddcli", info->snap);
	while (down_interruptible(&info->exit2_sem))
		;
	trace_on(warn("Client thread started, pid=%i for snapshot %d", current->pid, info->snap);)
connect:
	trace(warn("Request socket connection");)
	outbead(info->control_socket, NEED_SERVER, struct { });
	trace(warn("Wait for socket connection");)
	while (down_interruptible(&info->server_in_sem))
		;
	trace(warn("got socket %p", info->sock);)
	sock = info->sock;
#ifdef CONFIG_DM_DDSNAP_SWAP
	vm_sock = SOCKET_I(info->sock->f_dentry->d_inode);
	warn("setup sk_vmio");
	sk_set_vmio(vm_sock->sk);
#endif

	while (running(info)) { // stop on module exit
		int rw = READ, to_snap = 1, failed_io = 0;

		trace(warn("wait message");)
		if ((err = readpipe(sock, &message.head, sizeof(message.head))))
			goto socket_error;
		length = message.head.length;
		if (length > maxbody) //!!! FIXME: shouldn't limit message sizes
			goto message_too_long;
		trace(warn("%x/%u", message.head.code, length);)
		if ((err = readpipe(sock, &message.body, length)))
			goto socket_error;
	
		switch (message.head.code) {
		case ORIGIN_WRITE_ERROR:
			warn("Origin write failure");
			failed_io = 1;
		case ORIGIN_WRITE_OK:
			rw = WRITE;
			to_snap = 0;
			break;

		case SNAPSHOT_WRITE_ERROR:
			warn("Snapshot %u write failure", info->snap); 
			failed_io = 1;
		case SNAPSHOT_WRITE_OK:
			rw = WRITE;
			to_snap = 1;
			break;

		case SNAPSHOT_READ_ORIGIN_ERROR:
			warn("Snapshot %u read from origin failure", info->snap); 
			failed_io = 1;
		case SNAPSHOT_READ_ORIGIN_OK:
			rw = READ;
			to_snap = 0;
			break;

		case SNAPSHOT_READ_ERROR:
			warn("Snapshot %u read failure", info->snap); 
			failed_io = 1;
		case SNAPSHOT_READ_OK:
			rw = READ;
			to_snap = 1; 
			break;

		case IDENTIFY_OK:
			chunksize_bits = ((struct identify_ok *)message.body)->chunksize_bits;
			trace_on(warn("identify succeeded. chunksize %u", chunksize_bits););
			info->flags |= READY_FLAG;
			info->chunksize_bits = chunksize_bits;
			info->chunkshift     = chunksize_bits - SECTOR_SHIFT;
			// FIXME: get rid of .chunks = 1 to get rid of bio splitting for origin device
			//if (is_snapshot(info))
			// FIXME: rewrite ddsnapd pending code to get rid of bio splitting for snapshot device
				target->split_io = 1 << info->chunkshift;

			up(&info->server_out_sem);
			if (outbead(info->control_socket, CONNECT_SERVER_OK, struct { }) < 0)
				warn("unable to send CONNECT_SERVER_OK message to agent");
			up(&info->identify_sem);
			continue;
			
		case IDENTIFY_ERROR:
			err     = ((struct identify_error *)message.body)->err; 
			err_msg = ((struct identify_error *)message.body)->msg;
			length -= sizeof(err);
			err_msg[length - 1] = '\0';

			warn("unable to identify snapshot device with id %d, error: %s", 
			     info->snap, err_msg);

			message.head.code = CONNECT_SERVER_ERROR;
			message.head.length = length + sizeof(err);
			if (writepipe(info->control_socket, &message.head, sizeof(message.head)) < 0)
				warn("can't send msg head");
			if (writepipe(info->control_socket, &err, sizeof(err)) < 0) 
				warn("can't send out err");
			if (writepipe(info->control_socket, err_msg, length) < 0)
				warn("unable to send message CONNECT_SERVER_ERROR to agent");
			up(&info->identify_sem);
			continue;
			
		case PROTOCOL_ERROR:
			// !!! FIXME with a nice error message
			warn("caught a protocol error message");
			continue;
			
		default: 
		{
			struct protocol_error pe = { 	.err = ERROR_UNKNOWN_MESSAGE, 
							.culprit = message.head.code };
			err_msg = "Unknown message accepted by client kernel thread"; 
			warn("Unknown message %x, length %u. sending protocol error back to server",
				       	message.head.code, message.head.length);
			message.head.code = PROTOCOL_ERROR;
			message.head.length = sizeof(struct protocol_error) + strlen(err_msg) + 1;
			if (writepipe(sock, &message.head, sizeof(message.head)) < 0 || 
				writepipe(sock, &pe, sizeof(struct protocol_error)) < 0 ||
				writepipe(sock, err_msg, strlen(err_msg) + 1) < 0)
				warn("unable to send protocol error message");
			continue;
		}

		} // switch statement

		if (length < sizeof(struct rw_request))
			goto message_too_short;

		replied_rw(target, (void *)message.body, length, rw, to_snap, failed_io);
	}
out:
	warn("%s exiting for snapshot %d", task->comm, info->snap);
	up(&info->exit2_sem); /* !!! will crash if module unloaded before ret executes */
	return 0;
message_too_long:
	warn("message %x too long (%u bytes)", message.head.code, message.head.length);
	goto out;
message_too_short:
	warn("message %x too short (%u bytes)", message.head.code, message.head.length);
	goto out;
socket_error:
	warn("socket error %i", err);
	if (!running(info))
		goto out;

	warn("halting worker for snapshot %d",info->snap);
	report_error(info);
	goto connect;
}

/*
 * Here is our nonblocking worker daemon.  It handles all events other
 * than incoming socket traffic.  At the moment, its only job is to
 * send read release messages that can't be sent directly from the read
 * end_io function, which executes in interrupt context.  But soon its
 * duties will be expanded to include submitting IO that was blocked
 * because no server pipe is connected yet, or something broke the
 * pipe.  It may also have to resubmit some server queries, if the
 * server dies for some reason and a new one is incarnated to take its
 * place.  We also want to check for timed-out queries here.  Sure, we
 * have heartbeating in the cluster, but why not have the guy who knows
 * what to expect do the checking?  When we do detect timeouts, we will
 * punt the complaint upstairs using some interface that hasn't been
 * invented yet, because nobody has thought too deeply about what you
 * need to do, to detect faults really quickly and reliably.
 *
 * We throttle this daemon using a counting semaphore: each up on the
 * semaphore causes the daemon to loop through its polling sequence
 * once.  So we make sure we up the daemon's semaphore every time we
 * queue an event.  The daemon may well process more than one event per
 * cycle (we want that, actually, because then it can do some, e.g.,
 * message batching if it wants to) and will therefore end up looping
 * a few times without doing any work.  This is harmless, and much much
 * less nasty than missing an event.  When there are no pending events,
 * the daemon sleeps peacefully.  Killing the daemon is easy, we just
 * pull down the running flag and up the work semaphore, which causes
 * our faithful worker to drop out the bottom.
 */
void upload_locks(struct devinfo *info)
{
	unsigned long irqflags;
	struct hook *hook;
	struct list_head *entry, *tmp;

	spin_lock_irqsave(&info->end_io_lock, irqflags);
	info->dont_switch_lists = 1;
	while(!list_empty(&info->releases)){
		entry = info->releases.prev;
		hook = list_entry(entry, struct hook, list);
		list_del(entry);
		kmem_cache_free(end_io_cache, hook);
	}
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
	list_for_each_safe(entry, tmp, &info->locked){
		chunk_t chunk;

		hook = list_entry(entry, struct hook, list);
		spin_lock_irqsave(&info->end_io_lock, irqflags);
		if (hook->old_end_io == NULL){
			list_del(entry);
			kmem_cache_free(end_io_cache, hook);
			spin_unlock_irqrestore(&info->end_io_lock, irqflags);
			continue;
		}
		spin_unlock_irqrestore(&info->end_io_lock, irqflags);
		chunk = hook->sector >> info->chunkshift;
		outbead(info->sock, UPLOAD_LOCK, struct rw_request1, .count = 1, .ranges[0].chunk = chunk, .ranges[0].chunks = 1);
	}
	outbead(info->sock, FINISH_UPLOAD_LOCK, struct {});
	spin_lock_irqsave(&info->end_io_lock, irqflags);
	/*
	 * If the worker's not ready, don't move locked bios to the releases
	 * list.
	 */
	if (worker_ready(info)) {
		list_for_each_safe(entry, tmp, &info->locked){
			hook = list_entry(entry, struct hook, list);
			if (hook->old_end_io == NULL)
				list_move(&hook->list, &info->releases);
		}
	}
	info->dont_switch_lists = 0;
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
}

static void requeue_queries(struct devinfo *info)
{
	unsigned i;

	trace(show_pending(info);)
	spin_lock(&info->pending_lock);
	warn("");
	for (i = 0; i < NUM_BUCKETS; i++) {
		struct list_head *bucket = info->pending + i;

		while (!list_empty(bucket)) {
			struct list_head *entry = bucket->next;
			/* struct pending *pending = list_entry(entry, struct pending, list);
			 * trace(warn("requeue %u:%Lx", pending->id, pending->chunk);)
                         */
			list_move(entry, &info->queries);
			up(&info->more_work_sem);
		}
	}
	spin_unlock(&info->pending_lock);
	trace(show_pending(info);)
}

static int worker(struct dm_target *target)
{
	struct devinfo *info = target->private;
	struct task_struct *task = current;
	int err;

	daemonize("%s/%x", "ddwrk", info->snap);
	current->flags |= PF_LESS_THROTTLE;
	current->flags |= PF_MEMALLOC;
	trace_on(warn("Worker thread started, pid=%i for snapshot %d", current->pid, info->snap);)
	while (down_interruptible(&info->exit1_sem))
		;
	goto recover; /* just for now we'll always upload locks, even on fresh start */
restart:
	while (worker_running(info)) {
		unsigned long irqflags;
		while (down_interruptible(&info->more_work_sem))
			;

		/* Send message for each pending request. */
		spin_lock(&info->pending_lock);
		while (!list_empty(&info->queries) && worker_ready(info)) {
			struct list_head *entry = info->queries.prev;
			struct pending *pending = list_entry(entry, struct pending, list);
			/* Only refer a pending request when we hold pending_lock to 
			 * protect against the race in flush_pending_bio */
			u64 chunk = pending->chunk;
			unsigned chunks = pending->chunks;
			struct bio *bio = pending->bio;
			unsigned id = pending->id;

			list_del(entry);
			list_add(&pending->list, info->pending + hash_pending(id));
			spin_unlock(&info->pending_lock);
			trace(show_pending(info);)

			while (down_interruptible(&info->server_out_sem))
				;
			trace(warn("Server query [%Lx/%x]", chunk, chunks);)
			if ((err = outbead(info->sock,
				bio_data_dir(bio) == WRITE? QUERY_WRITE: QUERY_SNAPSHOT_READ,
				struct rw_request1,
					.id = id, .count = 1,
					.ranges[0].chunk = chunk,
					.ranges[0].chunks = chunks)))
				goto report;
			up(&info->server_out_sem);
			spin_lock(&info->pending_lock);
		}
		spin_unlock(&info->pending_lock);

		/* Send message for each pending read release. */
		spin_lock_irqsave(&info->end_io_lock, irqflags);
		while (!list_empty(&info->releases) && worker_ready(info)) {
			struct list_head *entry = info->releases.prev;
			struct hook *hook = list_entry(entry, struct hook, list);
			chunk_t chunk = hook->sector >> info->chunkshift;

			list_del(entry);
			spin_unlock_irqrestore(&info->end_io_lock, irqflags);
			trace(warn("release sector %Lx, chunk %Lx", (long long)hook->sector, chunk);)
			kmem_cache_free(end_io_cache, hook);
			while (down_interruptible(&info->server_out_sem))
				;
			if ((err = outbead(info->sock, FINISH_SNAPSHOT_READ, struct rw_request1,
				.count = 1, .ranges[0].chunk = chunk, .ranges[0].chunks = 1)))
				goto report;
			up(&info->server_out_sem);
			spin_lock_irqsave(&info->end_io_lock, irqflags);
		}
		spin_unlock_irqrestore(&info->end_io_lock, irqflags);

		trace(warn("Yowza! More work?");)
	}
	if ((info->flags & RECOVER_FLAG)) {
		while (down_interruptible(&info->server_out_sem))
			;
		up(&info->more_work_sem);
		goto recover;
	}
finish:
	trace_on(warn("%s exiting for snapshot %d", task->comm, info->snap);)
	up(&info->exit1_sem); /* !!! crashes if module unloaded before ret executes */
	return 0;

report:
	warn("worker socket error %i", err);
	report_error(info);
recover:
	trace_on(warn("worker recovering for snapshot %d", info->snap);)
	while (down_interruptible(&info->recover_sem))
		;
	if ((info->flags & FINISH_FLAG))
		goto finish;
	if (is_snapshot(info))
		upload_locks(info);
	requeue_queries(info);
	trace_on(warn("worker resuming for snapshot %d", info->snap);)

	info->flags &= ~(RECOVER_FLAG|(1 << REPORT_BIT));
	up(&info->recover_sem);
	goto restart;
}

/*
 * To handle agent failure, we just turn off our ddsnap "ready" flag and
 * fail all queued IOs. The second part is quite a mess because IO can be 
 * queued in different places in the driver.  Sigh.  We will just try to 
 * do this tidiest job we can on this messy feature.  In general, wherever
 * a bio can wait on a synchronizer before further processing, we need to
 * unblock the synchronizer (e.g., up the semaphore or ->shutdow a socket)
 * then check the device ready flag to see if the request should be failed
 * immediately. In the case of waiting for a server reply, we could fail
 * the IO after it is removed from the pending hash, or go diving into the
 * hash lists and remove/fail everything,whichever is less code.  IO that
 * has already been submitted to an underlying device should be allowed to
 * succeed, even if the completion is hooked for further processing.
 */
static void flush_list(struct devinfo *info, struct list_head *flush_list)
{
	struct list_head *entry, *tmp;
	list_for_each_safe(entry, tmp, flush_list) {
		struct pending *pending = list_entry(entry, struct pending, list);
		struct bio *bio=pending->bio;
		list_del(entry);
		kmem_cache_free(pending_cache, pending);
		bio_io_error(bio, bio->bi_size);
	}
}

static void flush_pending_bio(struct devinfo *info)
{
	struct list_head *entry, *tmp;
	int i;
	unsigned long irqflags;

	warn("flush_pending_bio");
	spin_lock(&info->pending_lock);
	flush_list(info, &info->queries);
	for (i = 0; i < NUM_BUCKETS; i++)
		flush_list(info, info->pending+i);
	spin_unlock(&info->pending_lock);

	spin_lock_irqsave(&info->end_io_lock, irqflags);
	list_for_each_safe(entry, tmp, &info->releases) {
		struct hook *hook = list_entry(entry, struct hook, list);
		list_del(entry);
		kmem_cache_free(end_io_cache, hook);
	}
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
	up(&info->more_work_sem);
	warn("flush_pending_bio done");

}

static int shutdown_socket(struct file *socket)
{
	struct socket *sock = SOCKET_I(socket->f_dentry->d_inode);
	return sock->ops->shutdown(sock, RCV_SHUTDOWN);
}

/*
 * Yikes, a third daemon, that makes four including the user space
 * monitor.  This daemon proliferation is due to not using poll, which
 * we should fix at some point.  Or maybe we should wait for aio to
 * work properly for sockets, and use that instead.  Either way, we
 * can combine the two socket-waiting daemons into one, which will look
 * nicer in ps.  Practically speaking, it doesn't matter a whole lot
 * though, if we just stay lazy and have too many daemons.
 *
 * At least, combine this code with incoming, with just the switches
 * different.
 */
static int control(struct dm_target *target)
{
	struct task_struct *task = current;
	struct devinfo *info = target->private;
	struct messagebuf message; // !!! have a buffer in the target->info
	struct file *sock;
	int err, length;
	char *err_msg;

	daemonize("%s/%x", "ddcon", info->snap);
	trace_on(warn("Control thread started, pid=%i for snapshot %d", current->pid, info->snap);)
	sock = info->control_socket;
	trace(warn("got socket %p", sock);)

	while (down_interruptible(&info->exit3_sem))
		;
	while (running(info)) {
		trace(warn("wait message");)
		if ((err = readpipe(sock, &message.head, sizeof(message.head))))
			goto socket_error;
		trace(warn("got message header code %x, length %u", message.head.code, message.head.length);)
		length = message.head.length;
		if (length > maxbody) //!!! FIXME: shouldn't limit message sizes
			goto message_too_long;
		trace(warn("%x/%u", message.head.code, length);)
		if ((err = readpipe(sock, &message.body, length)))
			goto socket_error;
	
		switch (message.head.code) {
		case SET_IDENTITY:
			info->id = ((struct set_id *)message.body)->id;
			warn("id set: %Lu", info->id);
			break;
		case CONNECT_SERVER: {
			unsigned len = 4;
			char bogus[len];
			int sock_fd = get_unused_fd(), fd;

			if (sock_fd < 0) {
				warn("Can't get fd, error %i", sock_fd);
				break;
			}
			fd_install(sock_fd, sock);
			if ((fd = recv_fd(sock_fd, bogus, &len)) < 0) {
				warn("recv_fd failed, error %i", fd);
				put_unused_fd(sock_fd);
				break;
			}
			trace(warn("Received socket %i", fd);)
			info->sock = fget(fd);
			/* we have two counts on the file, one from recv_fd and the other from fget */
			sys_close(fd); /* drop one count here */
			current->files->fd_array[sock_fd] = NULL; // should we grab file_lock?
			put_unused_fd(sock_fd);
			up(&info->server_in_sem);
			if (outbead(info->sock, IDENTIFY, struct identify, 
						.id = info->id, .snap = info->snap, 
						.off = target->begin, .len = target->len) < 0) {
				warn("unable to send IDENTIFY message");
				goto out;
			}
			up(&info->recover_sem); /* worker uploads locks now */
			break;
		}
		case PROTOCOL_ERROR:
			// !!! FIXME: add a new 
			warn("caught a protocol error message");
	   		break;		
		default: 
		{
			struct protocol_error pe = { 	.err = ERROR_UNKNOWN_MESSAGE, 
							.culprit = message.head.code };
			err_msg = "Unknown message accepted by control kernel thread"; 
			warn("Unknown message %x, length %u. sending protocol error back to server",
				       	message.head.code, message.head.length);
			message.head.code = PROTOCOL_ERROR;
			message.head.length = sizeof(struct protocol_error) + strlen(err_msg) + 1;
			if (writepipe(sock, &message.head, sizeof(message.head)) < 0 || 
				writepipe(sock, &pe, sizeof(struct protocol_error)) < 0 ||
				writepipe(sock, err_msg, strlen(err_msg) + 1) < 0)
				warn("unable to send protocol error message");
			continue;
		}
		
		} // switch statement
	}
out:
	warn("%s exiting for snapshot %d", task->comm, info->snap);
	up(&info->identify_sem); // unblock any process pending on ddsnap create
	up(&info->exit3_sem); /* !!! will crash if module unloaded before ret executes */
	return 0;
message_too_long:
	warn("message %x too long (%u bytes)", message.head.code, message.head.length);
	goto out;
socket_error:
	warn("socket error %i", err);
	if (!(info->flags & FINISH_FLAG)) {
		info->flags &= ~READY_FLAG;
		flush_pending_bio(info);
		// FIXME: send server a disconnect request, notifying it this is a explicit shutdown
		if (info->sock)
			shutdown_socket(info->sock);
	}
	goto out;
}

/*
 * This is the device mapper mapping method, which does one of three things:
 * (1) tells device mapper to go ahead and submit the request with a default
 * identity mapping (return 1) (2) tells device mapper to forget about the
 * request (return 0), goes off and does its own thing, or (3) on a bad
 * day, tells device mapper to fail the IO (return negative errnum).
 *
 * This is pretty simple: we just hand any origin reads back to device mapper
 * after filling in the origin device.  Then, we check the cache to see if
 * if conditions are right to map the request locally, otherwise we need help
 * from the server, so we remember the request in the pending hash and send
 * off the appropriate server query.
 *
 * To make this a little more interesting, our server connection may be broken
 * at the moment, or may not have been established yet, in which case we have
 * to defer the request until the server becomes available.
 */
static int ddsnap_map(struct dm_target *target, struct bio *bio, union map_info *context)
{
	struct devinfo *info = target->private;
	struct pending *pending;
	chunk_t chunk;
	unsigned id;

	if (!(info->flags & READY_FLAG)) {
		if (printk_ratelimit())
			warn("snapshot device with id %d is not ready", info->snap);
		return -EIO;
	}
	
	bio->bi_bdev = info->orgdev->bdev;
	if (bio_data_dir(bio) == READ && !is_snapshot(info))
		return 1;

	chunk = bio->bi_sector >> info->chunkshift;
	trace(warn("map %Lx/%x, chunk %Lx", (long long)bio->bi_sector, bio->bi_size, chunk);)
	//assert(bio->bi_size <= 1 << info->chunksize_bits);
#ifdef CACHE
	if (is_snapshot(info)) { // !!! use page cache for both
		struct page *page;
		u64 *exception = snap_map_cachep(info->inode->i_mapping, chunk, &page);
	
		if (!exception) {
			printk("Failed to get a page for sector %ld\n", bio->bi_sector);
			return -1;
		}

		u64 exp_chunk = *exception;
		UnlockPage(page);
		if (exp_chunk) {
			bio->bi_sector = bio->bi_sector + ((exp_chunk - chunk) << info->chunkshift);
			return 1;
		}
	} else {
		if (info->shared_bitmap && get_unshared_bit(info, chunk))
			return 1;
	}
#endif

	/* rob: check to see if the socket is connected, otherwise failed and don't place request on the queue */
	pending = kmem_cache_alloc(pending_cache, GFP_NOIO|__GFP_NOFAIL);
	spin_lock(&info->pending_lock);
	id = info->nextid;
	info->nextid = (id + 1) & ~(-1UL << RW_ID_BITS);
	*pending = (struct pending){ .id = id, .bio = bio, .chunk = chunk, .chunks = 1, .timestamp = jiffies };
	if (!worker_ready(info)) {
		spin_unlock(&info->pending_lock);
		kmem_cache_free(pending_cache, pending);
		return -EIO;
	}
	list_add(&pending->list, &info->queries);
	spin_unlock(&info->pending_lock);
	up(&info->more_work_sem);
	return 0;
}

/*
 * Carefully crafted not to care about how far we got in the process
 * of instantiating our client.  As such, it serves both for error
 * abort and device unload destruction.  We have to scour our little
 * world for resources and give them all back, including any pending
 * requests, context structures and daemons.  The latter have to be
 * convince to exit on demand, and we must be sure they have exited,
 * so we synchronize that with semaphores.  This isn't 100% foolproof'
 * there is still the possibility that the destructor could gain
 * control between the time a daemon ups its exit semaphore and when
 * it has actually returned to its caller.  In that case, the module
 * could be unloaded and the exiting thread will segfault.  This is
 * a basic flaw in Linux that I hope to get around to fixing at some
 * point, one way or another.
 */

/* Jiaying: add proc entries to pass stat info to user space.
 * edit ddsnap_seq_show to add more passed infomation in the snapshot files *
 * use ddsnap_add_proc to add a new file in /proc/driver/ddsnap dir */

/* ddsnap_pending_queries does the same show_pending includes */
static int ddsnap_pending_queries(struct devinfo *info)
{
	unsigned i, total = 0;
	struct list_head *list;

	spin_lock(&info->pending_lock);
	for (i = 0; i < NUM_BUCKETS; i++) {
		list_for_each(list, info->pending + i)
			total++;
	}
	spin_unlock(&info->pending_lock);
	return total;
}

static int ddsnap_queries_queries(struct devinfo *info)
{
	unsigned total = 0;
	struct list_head *list;

	spin_lock(&info->pending_lock);
	list_for_each(list, &info->queries)
		total++;
	spin_unlock(&info->pending_lock);
	return total;
}

static int ddsnap_releases_queries(struct devinfo *info)
{
	unsigned total = 0;
	struct list_head *list;
	unsigned long irqflags;

	spin_lock_irqsave(&info->end_io_lock, irqflags);
	list_for_each(list, &info->releases)
		total++;
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
	return total;
}


static int ddsnap_locked_queries(struct devinfo *info)
{
	unsigned total = 0;
	struct list_head *list;
	unsigned long irqflags;

	spin_lock_irqsave(&info->end_io_lock, irqflags);
	list_for_each(list, &info->locked)
		total++;
	spin_unlock_irqrestore(&info->end_io_lock, irqflags);
	return total;
}

static int ddsnap_seq_show(struct seq_file *seq, void *offset)
{
	struct dm_target *target = (struct dm_target *) seq->private;
	struct devinfo *info;

	BUG_ON(!target);
	info = (struct devinfo *) target->private;
	BUG_ON(!info);
	seq_printf(seq, "ddsnap inflight bio vecs %u, pending requests: %u, query requests %u, release requests %u, locked requests %u\n", dm_inflight_total(target), ddsnap_pending_queries(info), ddsnap_queries_queries(info), ddsnap_releases_queries(info), ddsnap_locked_queries(info));
	return 0;
}

static int ddsnap_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, ddsnap_seq_show, PDE(inode)->data);
}

static struct file_operations ddsnap_proc_fops = {
	.open = ddsnap_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct proc_dir_entry *ddsnap_proc_root=NULL; // initialized in dm_ddsnap_init
static struct semaphore ddsnap_proc_sem;

static void ddsnap_add_proc(const char *name, struct dm_target *target)
{
	struct proc_dir_entry *info;
	down(&ddsnap_proc_sem);
	BUG_ON(!ddsnap_proc_root);
	info = create_proc_entry(name, 0, ddsnap_proc_root);
	BUG_ON(!info);
	info->owner = THIS_MODULE;
	info->data = target;
	info->proc_fops = &ddsnap_proc_fops;
	up(&ddsnap_proc_sem);
}

static void ddsnap_remove_proc(const char *name)
{
	down(&ddsnap_proc_sem);
	remove_proc_entry(name, ddsnap_proc_root);
	up(&ddsnap_proc_sem);
}

/* Jiaying: end of proc functions */

/* start of sysrq-'z' operation */
static void sysrq_handle_ddsnap(int key, struct tty_struct *tty)
{
	struct dm_target *target;
	struct devinfo *info;
	struct proc_dir_entry *de = ddsnap_proc_root;
	struct mapped_device *md;

	if (!ddsnap_proc_root) {
		printk("NULL ddsnap proc directory\n");
		return;
	}
	down(&ddsnap_proc_sem);
	/* print out stat info for each entry under /proc/drivers/ddsnap */
	for (de = de->subdir; de; de = de->next) {
		target = (struct dm_target *) de->data;
		info = (struct devinfo *) target->private;
		printk("ddsnap device %s, ", de->name);
		md = dm_table_get_md(target->table);
		printk("state %s, ", dm_suspended(md) ? "SUSPENDED" : ((info->flags & READY_FLAG) ? "ACTIVE" : "NOT READY"));
		dm_put(md);
		printk("inflight bio vecs %u, pending requests: %u, query requests %u, release requests %u, locked requests %u\n", dm_inflight_total(target), ddsnap_pending_queries(info), ddsnap_queries_queries(info), ddsnap_releases_queries(info), ddsnap_locked_queries(info));
		show_pending(info);
		show_locked(info);
	}
	up(&ddsnap_proc_sem);
}

static struct sysrq_key_op sysrq_ddsnap_op = {
	.handler	= sysrq_handle_ddsnap,
	.help_msg	= "ddsnap",
	.action_msg	= "DDSNAP",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};
/* end of sysrq operation */

static void ddsnap_destroy(struct dm_target *target)
{
	struct devinfo *info = target->private;
	int err; /* I have no mouth but I must scream */
	char proc_name[8];
	trace(warn("%p", target);)
	if (!info)
		return;
	/* Unblock helper threads */
	info->flags |= FINISH_FLAG;
	warn("Unblocking helper threads");
	up(&info->server_in_sem); // unblock incoming thread
	up(&info->server_out_sem); // unblock io request threads
	up(&info->recover_sem); // unblock worker recovery
	
	warn("closing socket connections");
	if (info->sock && (err = shutdown_socket(info->sock)))
		warn("server socket shutdown error %i", err);
	if (info->control_socket && (err = shutdown_socket(info->control_socket)))
		warn("control socket shutdown error %i", err);

	up(&info->more_work_sem);

	// !!! wrong! the thread might be just starting, think about this some more
	// ah, don't let ddsnap_destroy run while ddsnap_create is spawning threads
	while (down_interruptible(&info->exit1_sem))
		;
	warn("thread 1 exited");
	while (down_interruptible(&info->exit2_sem))
		;
	warn("thread 2 exited");
	while (down_interruptible(&info->exit3_sem))
		;
	warn("thread 3 exited");

	if (info->sock) {
#ifdef CONFIG_DM_DDSNAP_SWAP
		struct socket *vm_sock = SOCKET_I(info->sock->f_dentry->d_inode);
		warn("clear sk_vmio");
		sk_clear_vmio(vm_sock->sk);
#endif
		fput(info->sock);
	}
	if (info->control_socket)
		fput(info->control_socket);
	if (info->inode)
		iput(info->inode);
	if (info->shared_bitmap)
		vfree(info->shared_bitmap);
	if (info->snapdev)
		dm_put_device(target, info->snapdev);
	if (info->orgdev)
		dm_put_device(target, info->orgdev);
	snprintf(proc_name, 8, "%d", info->snap); 
	ddsnap_remove_proc(proc_name);
	kfree(info);
}

/*
 * Woohoo, we are going to instantiate a new cluster snapshot virtual
 * device, what fun.
 */
static int get_control_socket(char *sockname)
{
	mm_segment_t oldseg = get_fs();
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname); // !!! check too long
	int sock = sys_socket(AF_UNIX, SOCK_STREAM, 0), err = 0;

	trace(warn("Connect to control socket %s", sockname);)
	if (sock <= 0)
		return sock;
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;

	set_fs(get_ds());
	while ((err = sys_connect(sock, (struct sockaddr *)&addr, addr_len)) == -ECONNREFUSED)
		break;
//		yield();
	set_fs(oldseg);

	return err? err: sock;
}

/*
 * Round up to nearest 2**k boundary
 * !!! lose this
 */
static inline ulong round_up(ulong n, ulong size)
{
	return (n + size - 1) & ~(size - 1);
}

static int ddsnap_create(struct dm_target *target, unsigned argc, char **argv)
{
	u64 chunksize_bits = 12; // !!! when chunksize isn't always 4K, have to move all this to identify reply handler
	struct devinfo *info;
	int err, i, snap, flags = 0;
	char *error;
#ifdef CACHE
	unsigned bm_size;
#endif
	/* 
	 * We are holding the big kernel lock grabbed in do_ioctl
	 * We doubt if BKL is required to prevent any races in device mapper methods
	 * FIXME: get rid of BKL from device mapper ioctls
	 */
	unlock_kernel();

	error = "ddsnap usage: snapdev orgdev sockname snapnum";
	err = -EINVAL;
	if (argc != 4)
		goto eek;

	snap = simple_strtol(argv[3], NULL, 0);
	if (snap >= 0)
		flags |= IS_SNAP_FLAG;

	err = -ENOMEM;
	error = "can't get kernel memory";
	if (!(info = kmalloc(sizeof(struct devinfo), GFP_NOIO|__GFP_NOFAIL)))
		goto eek;

	*info = (struct devinfo){ 
		.flags = flags, .snap = snap,
		.chunksize_bits = chunksize_bits,
		.chunkshift = chunksize_bits - SECTOR_SHIFT};
	target->private = info;
	sema_init(&info->server_in_sem, 0);
	sema_init(&info->server_out_sem, 0);
	sema_init(&info->recover_sem, 0);
	sema_init(&info->exit1_sem, 1);
	sema_init(&info->exit2_sem, 1);
	sema_init(&info->exit3_sem, 1);
	sema_init(&info->more_work_sem, 0);
	sema_init(&info->identify_sem, 0);
	spin_lock_init(&info->pending_lock);
	spin_lock_init(&info->end_io_lock);
	INIT_LIST_HEAD(&info->queries);
	INIT_LIST_HEAD(&info->releases);
	INIT_LIST_HEAD(&info->locked);
	for (i = 0; i < NUM_BUCKETS; i++)
		INIT_LIST_HEAD(&info->pending[i]);

	error = "Can't get snapshot device";
	if ((err = dm_get_device(target, argv[0], 0, 0, dm_table_get_mode(target->table), &info->snapdev)))
		goto eek;
	
	error = "Can't get origin device";
	if ((err = dm_get_device(target, argv[1], 0, 0, dm_table_get_mode(target->table), &info->orgdev)))
		goto eek;

	error = "Can't connect control socket";
	if ((err = get_control_socket(argv[2])) < 0)
		goto eek;
	info->control_socket = fget(err);
	sys_close(err);

#ifdef CACHE
	bm_size = round_up((target->len  + 7) >> (chunksize_bits + 3), sizeof(u32)); barf // !!! wrong
	error = "Can't allocate bitmap for origin";
	if (!(info->shared_bitmap = vmalloc(bm_size)))
		goto eek;
	memset(info->shared_bitmap, 0, bm_size);
	if (!(info->inode = new_inode(snapshot_super)))
		goto eek;
#endif

	error = "Can't start daemon";
	if ((err = kernel_thread((void *)incoming, target, CLONE_FILES)) < 0)
		goto eek;
	if ((err = kernel_thread((void *)worker, target, CLONE_FILES)) < 0)
		goto eek;
	if ((err = kernel_thread((void *)control, target, CLONE_FILES)) < 0)
		goto eek;
	warn("Created snapshot device snapstore=%s origin=%s socket=%s snapshot=%i", argv[0], argv[1], argv[2], snap);
	ddsnap_add_proc(argv[3], target); // use snapshot number as file name

	/*
	 * This down call needs to be interruptible. Otherwise, a 'dmsetup create' command
	 * is not killable when 'ddsnap server' isn't there, which is not desiable to users.
	 */
	err = down_interruptible(&info->identify_sem);
	if (err == -EINTR) {
		err = -ERESTARTSYS;
		goto eek;
	}
	if (!(info->flags & READY_FLAG)) {
			warn("snapshot device %d failed to be identified", info->snap);
			error = "Can't identify snapshot";
			err = -EINVAL;
			goto eek;
	}
	lock_kernel(); // FIXME, see unlock_kernel above
	return 0;

eek:	warn("Virtual device create error %i: %s!", err, error);
	ddsnap_destroy(target);
	target->error = error;
	lock_kernel(); // FIXME, see unlock_kernel above
	return err;

	{ void *useme = show_pending; useme = useme; }
}

/* Is this actually useful?  It's really trying to be a message */

static int ddsnap_status(struct dm_target *target, status_type_t type, char *result, unsigned int maxlen)
{
	char orgbuffer[32];
	char snapbuffer[32];
	struct devinfo *info = target->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		format_dev_t(orgbuffer, info->orgdev->bdev->bd_dev);
		format_dev_t(snapbuffer, info->snapdev->bdev->bd_dev);
		snprintf(result, maxlen, "%s %s %u",
			 orgbuffer, snapbuffer, 1 << info->chunksize_bits);
		break;
	}

	return 0;
}

static struct target_type ddsnap = {
	.name = "ddsnap",
	.version = {0, 0, 0},
	.module = THIS_MODULE,
	.ctr = ddsnap_create,
	.dtr = ddsnap_destroy,
	.map = ddsnap_map,
	.status = ddsnap_status,
};

int __init dm_ddsnap_init(void)
{
	int err = -ENOMEM;
	char *what = "Cache create";
	if (!(pending_cache = kmem_cache_create("ddsnap-pending",
		sizeof(struct pending), __alignof__(struct pending), 0, NULL, NULL)))
		goto bad1;
	if (!(end_io_cache = kmem_cache_create("ddsnap-endio",
		sizeof(struct hook), __alignof__(struct hook), 0, NULL, NULL)))
		goto bad2;
	what = "register";
	if ((err = dm_register_target(&ddsnap)))
		goto bad3;
#ifdef CACHE
	err = -ENOMEM;
	what = "create snapshot superblock";
	if (!(snapshot_super = alloc_super()))
		goto bad4;
#endif
	/* Jiaying: register /proc/driver/ddsnap */
	ddsnap_proc_root = proc_mkdir("ddsnap", proc_root_driver);
	if (!ddsnap_proc_root) {
		printk("cannot create /proc/driver/ddsnap entry\n");
#ifdef CACHE
		goto bad4;
#endif
		goto bad3;
	}
	ddsnap_proc_root->owner = THIS_MODULE;

	register_sysrq_key('z', &sysrq_ddsnap_op);
	sema_init(&ddsnap_proc_sem, 1);

	return 0;

#ifdef CACHE
bad4:
	dm_unregister_target(&ddsnap);
#endif
bad3:
	kmem_cache_destroy(end_io_cache);
bad2:
	kmem_cache_destroy(pending_cache);
bad1:
	DMERR("%s failed\n", what);
	return err;
}

void dm_ddsnap_exit(void)
{
	int err;
	if ((err = dm_unregister_target(&ddsnap)))
		DMERR("Snapshot unregister failed %d", err);
	if (pending_cache)
		kmem_cache_destroy(pending_cache);
	if (end_io_cache)
		kmem_cache_destroy(end_io_cache);
	kfree(snapshot_super);
	remove_proc_entry("ddsnap", proc_root_driver); //Jiaying: remove /proc/driver/ddsnap
	unregister_sysrq_key('z', &sysrq_ddsnap_op);
}

module_init(dm_ddsnap_init);
module_exit(dm_ddsnap_exit);

MODULE_LICENSE("GPL");
