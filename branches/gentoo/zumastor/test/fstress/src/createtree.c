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
#include <sys/param.h>
#include <rpc/rpc.h>
#include <assert.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "nfs.h"
#include "mount.h"
#include "nameset.h"
#include "operation.h"
#include "distribution.h"
#include "createtree.h"
#include "queue.h"
#include "timer.h"

static dist_func_t cr_d_cnts, cr_d_weights;
static dist_func_t cr_l_cnts, cr_l_weights;
static dist_func_t cr_f_cnts, cr_f_weights, cr_f_sizes;
static int cr_scale = 1;
static unsigned char *filedata = NULL;

static int cr_d_max, cr_f_max, cr_l_max; /* left to create */

static int created = 0;

/* ------------------------------------------------------- */

#define CR_MAX_OUTSTANDING 8

struct cr_rec {
	char *parentfhdata, name[64];
	nameset_entry_t nse, parent_nse;

	Q_ENTRY(cr_rec) link;

	/* directory */
	int subdirs, subfiles, subsymlinks, maxdepth;
	int mkdir_done, subdirs_done, subfiles_done, subsymlinks_done;

	/* file */
	int size;
	int create_done, size_done;	

	/* symlink */
	int symlink_done;
};

static Q_HEAD(cr_worklist, cr_rec) cr_worklist;

static int cr_dir(struct cr_rec *d);
static int cr_file(struct cr_rec *f);
static int cr_symlink(struct cr_rec *f);

/* ------------------------------------------------------- */

static void
cr_mkdir_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct cr_rec *d = (struct cr_rec *)arg;
	struct mkdir_res *res = reply ? &reply->u.mkdir_res : NULL;

	assert(d->nse->type == NFDIR);

	if (reply == NULL) {
		report_error(NONFATAL, "cr_mkdir cancelled");
		nameset_dele(d->nse);
		free(arg);
		return;
	}
	assert(reply->direction == REPLY);
	if (res->status != NFS_OK) {
		
		/*
		 * special case this common error.
		 */
		if (res->status == NFSERR_EXIST) {
			report_error(NONFATAL, "mkdir returned NFSERR_EXIST.\n"
				     "this will happen if you are re-running"
				     "fstress without first deleting the old"
				     "fstress created files");
		}
		
		report_error(FATAL, "cr_mkdir error %d: %s", 
			     res->status, nfs_errstr(res->status));
		nameset_dele(d->nse);
		free(arg);
		return;
	}
	assert(res->optfh.present);
	assert(*((u_int64_t *)res->optfh.fh.data) != 0);
	nameset_setfh(d->nse, res->optfh.fh.data, res->optfh.fh.len);

	assert(d->mkdir_done == 0);
	d->mkdir_done = 1;
	Q_INSERT_HEAD(&cr_worklist, d, link);
}

static void
cr_create_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct cr_rec *f = (struct cr_rec *)arg;
	struct create_res *res = reply ? &reply->u.create_res : NULL;

	assert(f->nse->type == NFREG);
	assert(f->create_done == 0);

	if (reply == NULL) {
		report_error(NONFATAL, "cr_create cancelled");
		nameset_dele(f->nse);
		free(arg);
		return;
	}
	assert(reply->direction == REPLY);
	if (res->status != NFS_OK) {
		report_error(FATAL, "cr_create error %d: %s", 
			     res->status, nfs_errstr(res->status));
		nameset_dele(f->nse);
		free(arg);
		return;
	}
	assert(res->optfh.present);
	assert(*((u_int64_t *)res->optfh.fh.data) != 0);
	nameset_setfh(f->nse, res->optfh.fh.data, res->optfh.fh.len);

	f->create_done = 1;
	Q_INSERT_HEAD(&cr_worklist, f, link);
}

static void
cr_write_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct cr_rec *f = (struct cr_rec *)arg;
	struct write_res *res = reply ? &reply->u.write_res : NULL;

	assert(f->nse->type == NFREG);
	assert(f->create_done == 1);

	if (reply == NULL) {
		report_error(NONFATAL, "cr_write cancelled");
		free(arg);
		return;
	}
	assert(reply->direction == REPLY);
	if (res->status != NFS_OK) {
#ifdef EXTRA_OUTPUT
		report_error(NONFATAL, "cr_write error %d: %s", 
			     res->status, nfs_errstr(res->status));
#endif
		free(arg);
		return;
	}
	f->size_done += res->count;

	if (res->wcc_data.after.present &&
	    res->wcc_data.after.fattr.fa_size != f->size_done) {
		report_error(NONFATAL, "cr_write response with bad size (got %d, expected %d)",
			     (int)res->wcc_data.after.fattr.fa_size,
			     f->size_done);
	}

	Q_INSERT_HEAD(&cr_worklist, f, link);
}

static void
cr_symlink_callback(void *arg, struct nfsmsg *reply, u_int32_t xid)
{
	struct cr_rec *l = (struct cr_rec *)arg;
	struct symlink_res *res = reply ? &reply->u.symlink_res : NULL;

	assert(l->nse->type == NFLNK);
	assert(l->symlink_done == 0);

	if (reply == NULL) {
		report_error(NONFATAL, "cr_symlink cancelled");
		nameset_dele(l->nse);
		free(arg);
		return;
	}
	assert(reply->direction == REPLY);
	if (res->status != NFS_OK) {
		report_error(NONFATAL, "cr_symlink error %d: %s", 
			     res->status, nfs_errstr(res->status));
		nameset_dele(l->nse);
		free(arg);
		return;
	}
	assert(res->optfh.present);
	assert(*((u_int64_t *)res->optfh.fh.data) != 0);
	nameset_setfh(l->nse, res->optfh.fh.data, res->optfh.fh.len);

	l->symlink_done = 1;
	Q_INSERT_HEAD(&cr_worklist, l, link);
}

/* ------------------------------------------------------- */

static int
cr_newdir(nameset_entry_t parent, int maxdepth)
{
	int weight = (*cr_d_weights)();
	struct cr_rec *d;

	if ((d = malloc(sizeof(struct cr_rec))) == NULL) {
		report_perror(FATAL, "malloc error");
		return -1;
	}
	bzero(d, sizeof(struct cr_rec));

	if ((d->nse = nameset_alloc(parent, NFDIR, weight)) == NULL) {
		report_error(FATAL, "nameset_alloc error");
		free(d);
		return -1;
	}
	if (nameset_getfname(d->nse, d->name, sizeof(d->name)) < 0) {
		report_error(FATAL, "nameset_getfname error");
		free(d);
		return -1;
	}
	d->parent_nse = parent;
	d->maxdepth = maxdepth - 1;

	d->subdirs = (*cr_d_cnts)();
	d->subfiles = (*cr_f_cnts)();
	d->subsymlinks = (*cr_l_cnts)();

	/*
	 * if count is negative, scale it according to load level.
	 */
	if (d->subdirs < 0) {
		d->subdirs = -(d->subdirs) * cr_scale;
	}
	if (d->subfiles < 0) {
		d->subfiles = -(d->subfiles) * cr_scale;
	}
	if (d->subsymlinks < 0) {
		d->subsymlinks = -(d->subsymlinks) * cr_scale;
	}

	/*
	 * prune things.  first, enforce the max dirtree depth.
	 * then, enforce limits on dirs, files, and symlinks.
	 */
	if (d->maxdepth <= 0) {
		d->subdirs = 0;
	}
	if (cr_d_max != -1) {
		if (d->subdirs > cr_d_max) {
			d->subdirs = cr_d_max;
		}
		cr_d_max -= d->subdirs;
	}
	if (cr_f_max != -1) {
		if (d->subfiles > cr_f_max) {
			d->subfiles = cr_f_max;
		}
		cr_f_max -= d->subfiles;
	}
	if (cr_l_max != -1) {
		if (d->subsymlinks > cr_l_max) {
			d->subsymlinks = cr_l_max;
		}
		cr_l_max -= d->subsymlinks;
	}

	d->nse->size = d->subdirs + d->subfiles; /* eventually */

	Q_INSERT_HEAD(&cr_worklist, d, link);
	return 0;
}

static int
cr_newfile(nameset_entry_t parent)
{
	int weight = (*cr_f_weights)();
	struct cr_rec *f;

	if ((f = malloc(sizeof(struct cr_rec))) == NULL) {
		report_perror(FATAL, "malloc error");
		return -1;
	}
	bzero(f, sizeof(struct cr_rec));

	if ((f->nse = nameset_alloc(parent, NFREG, weight)) == NULL) {
		report_error(FATAL, "nameset_alloc error");
		free(f);
		return -1;
	}
	if (nameset_getfname(f->nse, f->name, sizeof(f->name)) < 0) {
		report_error(FATAL, "nameset_getfname error");
		free(f);
		return -1;
	}
	f->parent_nse = parent;

	f->size = (*cr_f_sizes)();

	f->nse->size = f->size; /* eventually */

	Q_INSERT_HEAD(&cr_worklist, f, link);
	return 0;
}

static int
cr_newsymlink(nameset_entry_t parent)
{
	int weight = (*cr_l_weights)();
	struct cr_rec *l;

	if ((l = malloc(sizeof(struct cr_rec))) == NULL) {
		report_perror(FATAL, "malloc error");
		return -1;
	}
	bzero(l, sizeof(struct cr_rec));

	if ((l->nse = nameset_alloc(parent, NFLNK, weight)) == NULL) {
		report_error(FATAL, "nameset_alloc error");
		free(l);
		return -1;
	}
	if (nameset_getfname(l->nse, l->name, sizeof(l->name)) < 0) {
		report_error(FATAL, "nameset_getfname error");
		free(l);
		return -1;
	}
	l->parent_nse = parent;

	Q_INSERT_HEAD(&cr_worklist, l, link);
	return 0;
}

/* ------------------------------------------------------- */

static int
cr_dir(struct cr_rec *d)
{
	assert(d->nse->type == NFDIR);

	if (op_barrier(CR_MAX_OUTSTANDING) < 0) {
		report_error(FATAL, "op_barrier error");
		return -1;
	}

	if (d->mkdir_done == 0) {
		struct nfsmsg *nm;
		struct mkdir_arg *arg;

		if ((nm = op_alloc(NFSPROC_MKDIR)) == NULL) {
			report_error(FATAL, "op_alloc error");
			return -1;
		}
		arg = &nm->u.mkdir_arg;

		arg->where.dir.len = d->parent_nse->fhlen;
		arg->where.dir.data = d->parent_nse->fhdata;
		arg->where.name.len = strlen(d->name);
		arg->where.name.data = d->name;
		arg->sattr.sa_mode.present = 1;
		arg->sattr.sa_mode.sa_mode = 00755; /*rwxr-xr-x*/
		arg->sattr.sa_uid.present = 0;
		arg->sattr.sa_gid.present = 0;
		arg->sattr.sa_size.present = 0;
		arg->sattr.sa_atime.present = 0;
		arg->sattr.sa_mtime.present = 0;
		
		if (op_send(nm, cr_mkdir_callback, d, NULL) < 0) {
			report_error(FATAL, "op_send error");
			return -1;
		}
		return 0; /* resume via callback */
	}

	while (d->subfiles_done < d->subfiles) {
		cr_newfile(d->nse);
		d->subfiles_done++;
		/* do not return. create in parallel */
	}

	while (d->subsymlinks_done < d->subsymlinks) {
		cr_newsymlink(d->nse);
		d->subsymlinks_done++;
		/* do not return. create in parallel */
	}

	while (d->subdirs_done < d->subdirs) {
		cr_newdir(d->nse, d->maxdepth);
		d->subdirs_done++;
		/* do not return. create in parallel */
	}

	free(d);
	created++;
	return 0;
}

static int
cr_file(struct cr_rec *f)
{
	assert(f->nse->type == NFREG);

	if (op_barrier(CR_MAX_OUTSTANDING) < 0) {
		report_error(FATAL, "op_barrier error");
		return -1;
	}

	if (f->create_done == 0) {
		struct nfsmsg *nm;
		struct create_arg *arg;

		if ((nm = op_alloc(NFSPROC_CREATE)) == NULL) {
			report_error(FATAL, "op_alloc error");
			return -1;
		}
		arg = &nm->u.create_arg;

		arg->where.dir.len = f->parent_nse->fhlen;
		arg->where.dir.data = f->parent_nse->fhdata;
		arg->where.name.len = strlen(f->name);
		arg->where.name.data = f->name;
		arg->createmode = NFSV3CREATE_UNCHECKED;
		arg->sattr.sa_mode.present = 1;
		arg->sattr.sa_mode.sa_mode = 00644; /*rw-r--r--*/
		arg->sattr.sa_uid.present = 0;
		arg->sattr.sa_gid.present = 0;
		arg->sattr.sa_size.present = 0;
		arg->sattr.sa_atime.present = 0;
		arg->sattr.sa_mtime.present = 0;

		if (op_send(nm, cr_create_callback, f, NULL) < 0) {
			report_error(FATAL, "op_send error");
			return -1;
		}
		return 0; /* resume via callback */
	}

	if (f->size_done < f->size) {
		struct nfsmsg *nm;
		struct write_arg *write_arg;

		/*
		 * if small payloads skip up to the next 8KB boundary.
		 */
		if (MAX_PAYLOAD_SIZE < 8192 && f->size_done + 8192 < f->size) {
			f->size_done = (f->size_done + 8191) & ~8191;
		}

		if ((nm = op_alloc(NFSPROC_WRITE)) == NULL) {
			report_error(FATAL, "op_alloc error");
			return -1;
		}
		write_arg = &nm->u.write_arg;

		write_arg->fh.len = f->nse->fhlen;
		write_arg->fh.data = f->nse->fhdata;
		write_arg->offset = f->size_done;
		write_arg->count = MIN(MAX_PAYLOAD_SIZE, f->size - f->size_done);
		write_arg->stable = NFSV3WRITE_UNSTABLE;
		write_arg->data.len = write_arg->count;
		write_arg->data.data = filedata;

		if (op_send(nm, cr_write_callback, f, NULL) < 0) {
			report_error(FATAL, "op_send error");
			return -1;
		}
		return 0; /* resume via callback */
	}

	free(f);
	created++;
	return 0;
}

static int
cr_symlink(struct cr_rec *l)
{
	static char *sdata = "foobar";

	assert(l->nse->type == NFLNK);

	if (op_barrier(CR_MAX_OUTSTANDING) < 0) {
		report_error(FATAL, "op_barrier error");
		return -1;
	}

	if (l->symlink_done == 0) {
		struct nfsmsg *nm;
		struct symlink_arg *arg;

		if ((nm = op_alloc(NFSPROC_SYMLINK)) == NULL) {
			report_error(FATAL, "op_alloc error");
			return -1;
		}
		arg = &nm->u.symlink_arg;

		arg->where.dir.len = l->parent_nse->fhlen;
		arg->where.dir.data = l->parent_nse->fhdata;
		arg->where.name.len = strlen(l->name);
		arg->where.name.data = l->name;
		arg->symlinkdata.sattr.sa_mode.present = 1;
		arg->symlinkdata.sattr.sa_mode.sa_mode = 00777; /*rwxrwxrwx*/
		arg->symlinkdata.sattr.sa_uid.present = 0;
		arg->symlinkdata.sattr.sa_gid.present = 0;
		arg->symlinkdata.sattr.sa_size.present = 0;
		arg->symlinkdata.sattr.sa_atime.present = 0;
		arg->symlinkdata.sattr.sa_mtime.present = 0;
		arg->symlinkdata.path.len = strlen(sdata);
		arg->symlinkdata.path.data = sdata;

		if (op_send(nm, cr_symlink_callback, l, NULL) < 0) {
			report_error(FATAL, "op_send error");
			return -1;
		}
		return 0; /* resume via callback */
	}

	free(l);
	created++;
	return 0;
}

/* ------------------------------------------------------- */

int
createtree(struct fh *rootfh, int depth,
	   int d_max, dist_func_t d_cnts, dist_func_t d_weights, 
	   int f_max, dist_func_t f_cnts, dist_func_t f_weights,
	   int l_max, dist_func_t l_cnts, dist_func_t l_weights,
	   dist_func_t f_sizes, int scale)
{
	int i, num_dirs_at_root = 1, create_reported = 0, ret=-1;
	nameset_entry_t rootnse;
	struct cr_rec *cr;
	int rexmit_max_preserve = 0;

	/*
	 * remember the distributions.
	 */
	cr_d_cnts = d_cnts;
	cr_d_weights = d_weights;
	cr_d_max = d_max;
	cr_f_cnts = f_cnts;
	cr_f_weights = f_weights;
	cr_f_max = f_max;
	cr_l_cnts = l_cnts;
	cr_l_weights = l_weights;
	cr_l_max = l_max;
	cr_f_sizes = f_sizes;
	cr_scale = scale;

	Q_INIT(&cr_worklist);

	/*
	 * create a root name entry.
	 */
	if ((rootnse = nameset_alloc(NULL, NFDIR, 0/*never pick*/)) == NULL) {
		report_error(FATAL, "nameset_alloc error");
		goto out;
	}
	nameset_setfh(rootnse, rootfh->data, rootfh->len);

	/*
	 * set up the file contents data block.
	 */
	if ((filedata = malloc(8192)) == NULL) {
		report_perror(FATAL, "malloc error");
		goto out;
	}
	memset(filedata, 'x', 8192);

	rexmit_max_preserve = rexmit_max;
	rexmit_max = 2; /* rexmit a few times before cancel */

	/*
	 * go!
	 */
	for (i=0 ; i<num_dirs_at_root ; i++) {
		cr_newdir(rootnse, depth);
	}

	/*
	 * create everything in parallel.
	 */
	while ((cr = Q_FIRST(&cr_worklist)) != NULL) {
		/*
		 * some feedback that things are moving along...
		 */
		if (created - create_reported >= 100) {
			printf("%d ", created);
			fflush(stdout);
			create_reported = created;
		}

		Q_REMOVE(&cr_worklist, cr, link);
		switch(cr->nse->type) {
		case NFDIR:
			cr_dir(cr);
			break;
		case NFREG:
			cr_file(cr);
			break;
		case NFLNK:
			cr_symlink(cr);
			break;
		default:
			report_error(FATAL, "bad type %d", cr->nse->type);
			goto out;
		}
		/*
		 * wait for replies b/c they register new worklist items.
		 */
		if (Q_FIRST(&cr_worklist) == NULL) {
			op_barrier(0);
		}
	}

	/*
	 * wait for everything to finish. (redundant with above loop.)
	 */
	if (op_barrier(0) < 0) {
		report_error(FATAL, "op_barrier error");
		goto out;
	}

	ret = 0;
 out:
	rexmit_max = rexmit_max_preserve;
	if (filedata) {
		free(filedata);
		filedata = NULL;
	}
	return ret;
}
