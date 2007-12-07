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
 * build nfs v3 packets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <netinet/in.h>
#include <rpc/rpc.h>

#include "porting.h"
#include "nfs_constants.h"
#include "report.h"
#include "msg.h"
#include "rpc.h"
#include "nfs.h"
#include "queue.h"

static int
msg_zeroup4(msg_t m, int off)
{
	u_int32_t zero = 0;
	
	if (off != -1 && (off & 0x3)) {
		off = msg_insert(m, off, 4 - (off & 0x3), (caddr_t)&zero);
	}
	return off;
}

static int
roundup4(int in)
{
	return (in == -1 ? in : (in + 3) & ~3);
}

/* ------------------------------------------------------- */

struct alloc_data {
	void *data;
	int len;
	struct alloc_data *next;
	/* variable size data follows */
};

void *
nfsmsg_alloc_data(struct nfsmsg *nm, int len)
{
	struct alloc_data *ad;

	if (len <= nm->alloc_data_reservoir_remaining) {
		int roff = sizeof(nm->alloc_data_reservoir) - 
			nm->alloc_data_reservoir_remaining;
		/* be nice and keep things word aligned */
		len = (len + (sizeof(long) - 1)) & ~(sizeof(long) - 1);
		nm->alloc_data_reservoir_remaining -= len;
		return &nm->alloc_data_reservoir[roff];
	}

	if ((ad = malloc(sizeof(struct alloc_data) + len)) == NULL) {
		report_perror(FATAL, "malloc");
		return NULL;
	}
	ad->data = ad + 1;
	ad->len = len;
	ad->next = nm->alloc_data;
	nm->alloc_data = ad; /* chain */
	return ad->data;
}

void
nfsmsg_prep(struct nfsmsg *nm, int direction)
{
	nm->alloc_data_reservoir_remaining = 
		sizeof(nm->alloc_data_reservoir);
	nm->alloc_data = NULL;

	nm->direction = direction;
}

void
nfsmsg_rele(struct nfsmsg *nm)
{
	struct alloc_data *ad;

	while ((ad = nm->alloc_data) != NULL) {
		nm->alloc_data = ad->next;
		free(ad);
	}
	nm->alloc_data = NULL;
}

/* ------------------------------------------------------- */

static int
msg_insert_fh(msg_t m, int off, struct fh *fh)
{
	assert(0 < fh->len && fh->len <= NFSX_V3FHMAX);
	off = msg_insert_htonl_int32(m, off, fh->len);
	off = msg_insert(m, off, fh->len, fh->data);
	return msg_zeroup4(m, off);
}

static int
msg_insert_optfh(msg_t m, int off, struct optfh *optfh)
{
	off = msg_insert_htonl_int32(m, off, optfh->present);
	if (optfh->present) {
		off = msg_insert_fh(m, off, &optfh->fh);
	}
	return off;
}

static int
msg_insert_fn(msg_t m, int off, struct fn *fn)
{
	assert(0 < fn->len && fn->len <= NFS_MAXNAMLEN);
	off = msg_insert_htonl_int32(m, off, fn->len);
	off = msg_insert(m, off, fn->len, fn->data);
	return msg_zeroup4(m, off);
}

static int
msg_insert_path(msg_t m, int off, struct path *path)
{
	assert(0 < path->len && path->len <= NFS_MAXPATHLEN);
	off = msg_insert_htonl_int32(m, off, path->len);
	off = msg_insert(m, off, path->len, path->data);
	return msg_zeroup4(m, off);
}

static int
msg_insert_time(msg_t m, int off, struct nfstime *time)
{
	off = msg_insert_htonl_int32(m, off, time->sec);
	off = msg_insert_htonl_int32(m, off, time->nsec);
	return off;
}

static int
msg_insert_opttime(msg_t m, int off, struct opttime *opttime)
{
	off = msg_insert_htonl_int32(m, off, opttime->present);
	if (opttime->present) {
		off = msg_insert_time(m, off, &opttime->time);
	}
	return off;
}

static int
msg_insert_sattr(msg_t m, int off, struct sattr *sattr)
{
	off = msg_insert_htonl_int32(m, off, sattr->sa_mode.present);
	if (sattr->sa_mode.present) {
		off = msg_insert_htonl_int32(m, off, sattr->sa_mode.sa_mode);
	}
	off = msg_insert_htonl_int32(m, off, sattr->sa_uid.present);
	if (sattr->sa_uid.present) {
		off = msg_insert_htonl_int32(m, off, sattr->sa_uid.sa_uid);
	}
	off = msg_insert_htonl_int32(m, off, sattr->sa_gid.present);
	if (sattr->sa_gid.present) {
		off = msg_insert_htonl_int32(m, off, sattr->sa_gid.sa_gid);
	}
	off = msg_insert_htonl_int32(m, off, sattr->sa_size.present);
	if (sattr->sa_size.present) {
		off = msg_insert_htonl_int64(m, off, sattr->sa_size.sa_size);
	}
	if (sattr->sa_atime.present) {
		off = msg_insert_htonl_int32(m, off, NFSV3SATTRTIME_TOCLIENT);
		off = msg_insert_time(m, off, &sattr->sa_atime.sa_atime);
	} else {
		off = msg_insert_htonl_int32(m, off, NFSV3SATTRTIME_DONTCHANGE);
	}
	if (sattr->sa_mtime.present) {
		off = msg_insert_htonl_int32(m, off, NFSV3SATTRTIME_TOCLIENT);
		off = msg_insert_time(m, off, &sattr->sa_mtime.sa_mtime);
	} else {
		off = msg_insert_htonl_int32(m, off, NFSV3SATTRTIME_DONTCHANGE);
	}
	return off;
}

static int
msg_insert_fattr(msg_t m, int off, struct fattr *fattr)
{
	off = msg_insert_htonl_int32(m, off, fattr->fa_type);
	off = msg_insert_htonl_int32(m, off, fattr->fa_mode);
	off = msg_insert_htonl_int32(m, off, fattr->fa_nlink);
	off = msg_insert_htonl_int32(m, off, fattr->fa_uid);
	off = msg_insert_htonl_int32(m, off, fattr->fa_gid);
	off = msg_insert_htonl_int64(m, off, fattr->fa_size);
	off = msg_insert_htonl_int64(m, off, fattr->fa_used);
	off = msg_insert_htonl_int64(m, off, fattr->fa_spec);
	off = msg_insert_htonl_int64(m, off, fattr->fa_fsid);
	off = msg_insert_htonl_int64(m, off, fattr->fa_fileid);
	off = msg_insert_time(m, off, &fattr->fa_atime);
	off = msg_insert_time(m, off, &fattr->fa_mtime);
	off = msg_insert_time(m, off, &fattr->fa_ctime);
	return off;
}

static int
msg_insert_optfattr(msg_t m, int off, struct optfattr *optfattr)
{
	off = msg_insert_htonl_int32(m, off, optfattr->present);
	if (optfattr->present) {
		off = msg_insert_fattr(m, off, &optfattr->fattr);
	}
	return off;
}

static int
msg_insert_wcc_data(msg_t m, int off, struct wcc_data *wcc_data)
{
	off = msg_insert_htonl_int32(m, off, wcc_data->before.present);
	if (wcc_data->before.present) {
		off = msg_insert_htonl_int64(m, off, wcc_data->before.size);
		off = msg_insert_time(m, off, &wcc_data->before.mtime);
		off = msg_insert_time(m, off, &wcc_data->before.ctime);
	}
	off = msg_insert_optfattr(m, off, &wcc_data->after);
	return off;
}

static int
msg_insert_diroparg(msg_t m, int off, struct diroparg *doa)
{
	off = msg_insert_fh(m, off, &doa->dir);
	off = msg_insert_fn(m, off, &doa->name);
	return off;
}

static int
msg_insert_opaque(msg_t m, int off, struct opaque *opaque)
{
	off = msg_insert_htonl_int32(m, off, opaque->len);
	/* 
	 * len but no data is legal -- caller can attach a data mbuf.
	 */
	if (opaque->len && opaque->data) { 
		off = msg_insert(m, off, opaque->len, opaque->data);
	}
	return msg_zeroup4(m, off);
}

static int
msg_insert_dirlist(msg_t m, int off, struct dirlist *dirlist)
{
	struct entry *entry = dirlist->entries;
	u_int32_t present = 1, absent = 0;

	while (entry != NULL) {
		off = msg_insert_htonl_int32(m, off, present);
		off = msg_insert_htonl_int64(m, off, entry->fileid);
		off = msg_insert_fn(m, off, &entry->name);
		off = msg_insert_htonl_int64(m, off, entry->cookie);
		entry = entry->nextentry;
	}
	off = msg_insert_htonl_int32(m, off, absent);
	off = msg_insert_htonl_int32(m, off, dirlist->eof);
	return off;
}

static int
msg_insert_dirlistplus(msg_t m, int off, struct dirlistplus *dirlist)
{
	struct entryplus *entry = dirlist->entries;
	u_int32_t present = 1, absent = 0;

	while (entry != NULL) {
		off = msg_insert_htonl_int32(m, off, present);
		off = msg_insert_htonl_int64(m, off, entry->fileid);
		off = msg_insert_fn(m, off, &entry->name);
		off = msg_insert_htonl_int64(m, off, entry->cookie);
		off = msg_insert_optfattr(m, off, &entry->optfattr);
		off = msg_insert_optfh(m, off, &entry->optfh);
		entry = entry->nextentry;
	}
	off = msg_insert_htonl_int32(m, off, absent);
	off = msg_insert_htonl_int32(m, off, dirlist->eof);
	return off;
}

/* ------------------------------------------------------- */

static int
nfs_build_call(msg_t m, int off, struct nfsmsg *nm)
{
	switch (nm->proc) {
	case NFSPROC_NULL:
		break;
	case NFSPROC_GETATTR: {
		struct getattr_arg *arg = &nm->u.getattr_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		break;
	}
	case NFSPROC_SETATTR: {
		struct setattr_arg *arg = &nm->u.setattr_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_sattr(m, off, &arg->sattr);
		off = msg_insert_opttime(m, off, &arg->guard_ctime);
		break;
	}
	case NFSPROC_LOOKUP: {
		struct lookup_arg *arg = &nm->u.lookup_arg;
		off = msg_insert_diroparg(m, off, &arg->what);
		break;
	}
	case NFSPROC_ACCESS: {
		struct access_arg *arg = &nm->u.access_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_htonl_int32(m, off, arg->access);
		break;
	}
	case NFSPROC_READLINK: {
		struct readlink_arg *arg = &nm->u.readlink_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		break;
	}
	case NFSPROC_READ: {
		struct read_arg *arg = &nm->u.read_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_htonl_int64(m, off, arg->offset);
		off = msg_insert_htonl_int32(m, off, arg->count);
		break;
	}
	case NFSPROC_WRITE: {
		struct write_arg *arg = &nm->u.write_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_htonl_int64(m, off, arg->offset);
		off = msg_insert_htonl_int32(m, off, arg->count);
		off = msg_insert_htonl_int32(m, off, arg->stable);
		off = msg_insert_opaque(m, off, &arg->data);
		break;
	}
	case NFSPROC_CREATE: {
		struct create_arg *arg = &nm->u.create_arg;
		off = msg_insert_diroparg(m, off, &arg->where);
		off = msg_insert_htonl_int32(m, off, arg->createmode);
		switch (arg->createmode) {
		case NFSV3CREATE_UNCHECKED:
		case NFSV3CREATE_GUARDED:
			off = msg_insert_sattr(m, off, &arg->sattr);
			break;
		case NFSV3CREATE_EXCLUSIVE:
			off = msg_insert_htonl_int64(m, off, arg->createverf);
			break;
		}
		break;
	}
	case NFSPROC_MKDIR: {
		struct mkdir_arg *arg = &nm->u.mkdir_arg;
		off = msg_insert_diroparg(m, off, &arg->where);
		off = msg_insert_sattr(m, off, &arg->sattr);
		break;
	}
	case NFSPROC_SYMLINK: {
		struct symlink_arg *arg = &nm->u.symlink_arg;
		off = msg_insert_diroparg(m, off, &arg->where);
		off = msg_insert_sattr(m, off, &arg->symlinkdata.sattr);
		off = msg_insert_path(m, off, &arg->symlinkdata.path);
		break;
	}
	case NFSPROC_MKNOD: {
		struct mknod_arg *arg = &nm->u.mknod_arg;
		off = msg_insert_diroparg(m, off, &arg->where);
		off = msg_insert_htonl_int32(m, off, arg->type);
		switch(arg->type) {
		case NFCHR:
		case NFBLK:
			off = msg_insert_sattr(m, off, &arg->sattr);
			off = msg_insert_htonl_int64(m, off, arg->spec);
			break;
		case NFSOCK:
		case NFFIFO:
			off = msg_insert_sattr(m, off, &arg->sattr);
			break;
		}
		break;
	}
	case NFSPROC_REMOVE: {
		struct remove_arg *arg = &nm->u.remove_arg;
		off = msg_insert_diroparg(m, off, &arg->what);
		break;
	}
	case NFSPROC_RMDIR: {
		struct rmdir_arg *arg = &nm->u.rmdir_arg;
		off = msg_insert_diroparg(m, off, &arg->what);
		break;
	}
	case NFSPROC_RENAME: {
		struct rename_arg *arg = &nm->u.rename_arg;
		off = msg_insert_diroparg(m, off, &arg->from);
		off = msg_insert_diroparg(m, off, &arg->to);
		break;
	}
	case NFSPROC_LINK: {
		struct link_arg *arg = &nm->u.link_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_diroparg(m, off, &arg->link);
		break;
	}
	case NFSPROC_READDIR: {
		struct readdir_arg *arg = &nm->u.readdir_arg;
		off = msg_insert_fh(m, off, &arg->dir);
		off = msg_insert_htonl_int64(m, off, arg->cookie);
		off = msg_insert_htonl_int64(m, off, arg->cookieverf);
		off = msg_insert_htonl_int32(m, off, arg->count);
		break;
	}
	case NFSPROC_READDIRPLUS: {
		struct readdirplus_arg *arg = &nm->u.readdirplus_arg;
		off = msg_insert_fh(m, off, &arg->dir);
		off = msg_insert_htonl_int64(m, off, arg->cookie);
		off = msg_insert_htonl_int64(m, off, arg->cookieverf);
		off = msg_insert_htonl_int32(m, off, arg->dircount);
		off = msg_insert_htonl_int32(m, off, arg->maxcount);
		break;
	}
	case NFSPROC_FSSTAT: {
		struct fsstat_arg *arg = &nm->u.fsstat_arg;
		off = msg_insert_fh(m, off, &arg->fsroot);
		break;
	}
	case NFSPROC_FSINFO: {
		struct fsinfo_arg *arg = &nm->u.fsinfo_arg;
		off = msg_insert_fh(m, off, &arg->fsroot);
		break;
	}
	case NFSPROC_PATHCONF: {
		struct pathconf_arg *arg = &nm->u.pathconf_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		break;
	}
	case NFSPROC_COMMIT: {
		struct commit_arg *arg = &nm->u.commit_arg;
		off = msg_insert_fh(m, off, &arg->fh);
		off = msg_insert_htonl_int64(m, off, arg->offset);
		off = msg_insert_htonl_int32(m, off, arg->count);
		break;
	}
	default:
		report_error(FATAL, "nfs_build_call: unknown proc %d", 
			     nm->proc);
		assert(0);
		break;
	}
	return (off == -1);
}

static int
nfs_build_reply(msg_t m, int off, struct nfsmsg *nm)
{
	if (nm->proc != NFSPROC_NULL) {
		off = msg_insert_htonl_int32(m, off, nm->u.result_status);
	}
	switch (nm->proc) {
	case NFSPROC_NULL:
		break;
	case NFSPROC_GETATTR: {
		if (nm->u.result_status == NFS_OK) {
			struct getattr_res *res = &nm->u.getattr_res;
			off = msg_insert_fattr(m, off, &res->fattr);
		} else {
			;
		}
		break;
	}
	case NFSPROC_SETATTR: {
		if (nm->u.result_status == NFS_OK) {
			struct setattr_res *res = &nm->u.setattr_res;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
		} else {
			struct setattr_resfail *res = &nm->u.setattr_resfail;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
		}
		break;
	}
	case NFSPROC_LOOKUP: {
		if (nm->u.result_status == NFS_OK) {
			struct lookup_res *res = &nm->u.lookup_res;
			off = msg_insert_fh(m, off, &res->fh);
			off = msg_insert_optfattr(m, off, &res->obj_optfattr);
			off = msg_insert_optfattr(m, off, &res->dir_optfattr);
		} else {
			struct lookup_resfail *res = &nm->u.lookup_resfail;
			off = msg_insert_optfattr(m, off, &res->dir_optfattr);
		}
		break;
	}
	case NFSPROC_ACCESS: {
		if (nm->u.result_status == NFS_OK) {
			struct access_res *res = &nm->u.access_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int32(m, off, res->access);
		} else {
			struct access_resfail *res = &nm->u.access_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READLINK: {
		if (nm->u.result_status == NFS_OK) {
			struct readlink_res *res = &nm->u.readlink_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_path(m, off, &res->path);
		} else {
			struct readlink_resfail *res = &nm->u.readlink_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READ: {
		if (nm->u.result_status == NFS_OK) {
			struct read_res *res = &nm->u.read_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int32(m, off, res->count);
			off = msg_insert_htonl_int32(m, off, res->eof);
			off = msg_insert_opaque(m, off, &res->data);
		} else {
			struct read_resfail *res = &nm->u.read_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_WRITE: {
		if (nm->u.result_status == NFS_OK) {
			struct write_res *res = &nm->u.write_res;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
			off = msg_insert_htonl_int32(m, off, res->count);
			off = msg_insert_htonl_int32(m, off, res->committed);
			off = msg_insert_htonl_int64(m, off, res->verf);
		} else {
			struct write_resfail *res = &nm->u.write_resfail;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
		}
		break;
	}
	case NFSPROC_CREATE: {
		if (nm->u.result_status == NFS_OK) {
			struct create_res *res = &nm->u.create_res;
			off = msg_insert_optfh(m, off, &res->optfh);
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct create_resfail *res = &nm->u.create_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_MKDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct mkdir_res *res = &nm->u.mkdir_res;
			off = msg_insert_optfh(m, off, &res->optfh);
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct mkdir_resfail *res = &nm->u.mkdir_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_SYMLINK: {
		if (nm->u.result_status == NFS_OK) {
			struct symlink_res *res = &nm->u.symlink_res;
			off = msg_insert_optfh(m, off, &res->optfh);
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct symlink_resfail *res = &nm->u.symlink_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_MKNOD: {
		if (nm->u.result_status == NFS_OK) {
			struct mknod_res *res = &nm->u.mknod_res;
			off = msg_insert_optfh(m, off, &res->optfh);
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct mknod_resfail *res = &nm->u.mknod_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_REMOVE: {
		if (nm->u.result_status == NFS_OK) {
			struct remove_res *res = &nm->u.remove_res;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct remove_resfail *res = &nm->u.remove_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_RMDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct rmdir_res *res = &nm->u.rmdir_res;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		} else {
			struct rmdir_resfail *res = &nm->u.rmdir_resfail;
			off = msg_insert_wcc_data(m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_RENAME: {
		if (nm->u.result_status == NFS_OK) {
			struct rename_res *res = &nm->u.rename_res;
			off = msg_insert_wcc_data(m, off, &res->fromdir_wcc);
			off = msg_insert_wcc_data(m, off, &res->todir_wcc);
		} else {
			struct rename_resfail *res = &nm->u.rename_resfail;
			off = msg_insert_wcc_data(m, off, &res->fromdir_wcc);
			off = msg_insert_wcc_data(m, off, &res->todir_wcc);
		}
		break;
	}
	case NFSPROC_LINK: {
		if (nm->u.result_status == NFS_OK) {
			struct link_res *res = &nm->u.link_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->linkdir_wcc);
		} else {
			struct link_resfail *res = &nm->u.link_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_wcc_data(m, off, &res->linkdir_wcc);
		}
		break;
	}
	case NFSPROC_READDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct readdir_res *res = &nm->u.readdir_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int64(m, off, res->cookieverf);
			off = msg_insert_dirlist(m, off, &res->dirlist);
		} else {
			struct readdir_resfail *res = &nm->u.readdir_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READDIRPLUS: {
		if (nm->u.result_status == NFS_OK) {
			struct readdirplus_res *res = &nm->u.readdirplus_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int64(m, off, res->cookieverf);
			off = msg_insert_dirlistplus(m, off, &res->dirlist);
		} else {
			struct readdirplus_resfail *res = &nm->u.readdirplus_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_FSSTAT: {
		if (nm->u.result_status == NFS_OK) {
			struct fsstat_res *res = &nm->u.fsstat_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int64(m, off, res->tbytes);
			off = msg_insert_htonl_int64(m, off, res->fbytes);
			off = msg_insert_htonl_int64(m, off, res->abytes);
			off = msg_insert_htonl_int64(m, off, res->tfiles);
			off = msg_insert_htonl_int64(m, off, res->ffiles);
			off = msg_insert_htonl_int64(m, off, res->afiles);
			off = msg_insert_htonl_int32(m, off, res->invarsec);
		} else {
			struct fsstat_resfail *res = &nm->u.fsstat_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_FSINFO: {
		if (nm->u.result_status == NFS_OK) {
			struct fsinfo_res *res = &nm->u.fsinfo_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int32(m, off, res->rtmax);
			off = msg_insert_htonl_int32(m, off, res->rtpref);
			off = msg_insert_htonl_int32(m, off, res->rtmult);
			off = msg_insert_htonl_int32(m, off, res->wtmax);
			off = msg_insert_htonl_int32(m, off, res->wtpref);
			off = msg_insert_htonl_int32(m, off, res->wtmult);
			off = msg_insert_htonl_int32(m, off, res->dtpref);
			off = msg_insert_htonl_int64(m, off, res->maxfilesize);
			off = msg_insert_time(m, off, &res->time_delta);
			off = msg_insert_htonl_int32(m, off, res->properties);
		} else {
			struct fsinfo_resfail *res = &nm->u.fsinfo_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_PATHCONF: {
		if (nm->u.result_status == NFS_OK) {
			struct pathconf_res *res = &nm->u.pathconf_res;
			off = msg_insert_optfattr(m, off, &res->optfattr);
			off = msg_insert_htonl_int32(m, off, res->linkmax);
			off = msg_insert_htonl_int32(m, off, res->name_max);
			off = msg_insert_htonl_int32(m, off, res->no_trunc);
			off = msg_insert_htonl_int32(m, off, res->chown_restricted);
			off = msg_insert_htonl_int32(m, off, res->case_insensitive);
			off = msg_insert_htonl_int32(m, off, res->case_preserving);
		} else {
			struct pathconf_resfail *res = &nm->u.pathconf_resfail;
			off = msg_insert_optfattr(m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_COMMIT: {
		if (nm->u.result_status == NFS_OK) {
			struct commit_res *res = &nm->u.commit_res;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
			off = msg_insert_htonl_int64(m, off, res->verf);
		} else {
			struct commit_resfail *res = &nm->u.commit_resfail;
			off = msg_insert_wcc_data(m, off, &res->wcc_data);
		}
		break;
	}
	default:
		report_error(FATAL, "nfs_build_reply: unknown proc %d", 
			     nm->proc);
		assert(0);
		break;
	}
	return (off == -1);
}

static int
nfs_build(msg_t m, int off, struct nfsmsg *nm)
{
	switch (nm->direction) {
	case CALL:
		return nfs_build_call(m, off, nm);
	case REPLY:
		return nfs_build_reply(m, off, nm);
	}
	return -1;
}

/* ------------------------------------------------------- */

/*
 * parse nfs v3 packets.
 */

static int
msg_extract_boolean(msg_t m, int off, u_int32_t *boolean)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_ntohl_int32(m, off, boolean);
	if (*boolean != 0 && *boolean != 1) {
		report_error(NONFATAL, "msg_extract_boolean: got %d\n", 
			     (int)*boolean);
		return -1;
	}
	return off;
}

static int
msg_extract_fh(struct nfsmsg *nm, msg_t m, int off, struct fh *fh)
{
	if (off == -1) {
		return -1;
	}
	if ((off = msg_extract_ntohl_int32(m, off, &fh->len)) == -1) {
		return -1;
	}
#if 0
	if (fh->len != 28) {
		report_error(NONFATAL, "msg_extract_fh: unexpected fh len %d",
			     fh->len);
		return -1;
	}
#endif
	assert(0 < fh->len && fh->len <= NFSX_V3FHMAX);
	if ((fh->data = nfsmsg_alloc_data(nm, fh->len)) == NULL) {
		report_error(FATAL, "msg_extract_fh: nfsmsg_alloc_data (len=%d) error",
			     fh->len);
		return -1;
	}
	assert(((long)fh->data & 0x3) == 0); /* alignment */
	off = msg_extract(m, off, fh->len, fh->data);
	return roundup4(off);
}

static int
msg_extract_optfh(struct nfsmsg *nm, msg_t m, int off, struct optfh *optfh)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_boolean(m, off, &optfh->present);
	if (optfh->present) {
		off = msg_extract_fh(nm, m, off, &optfh->fh);
	}
	return off;
}

static int
msg_extract_fn(struct nfsmsg *nm, msg_t m, int off, struct fn *fn)
{
	if (off == -1) {
		return -1;
	}
	if ((off = msg_extract_ntohl_int32(m, off, &fn->len)) == -1) {
		return -1;
	}
	assert(0 < fn->len && fn->len <= NFS_MAXNAMLEN);
	if ((fn->data = nfsmsg_alloc_data(nm, fn->len + 1)) == NULL) {
		report_error(FATAL, "msg_extract_fn: nfsmsg_alloc_data (len=%d) error",
			     fn->len);
		return -1;
	}
	off = msg_extract(m, off, fn->len, fn->data);
	fn->data[fn->len] = '\0'; /* fn->data has extra byte for term */
	return roundup4(off);
}

static int
msg_extract_path(struct nfsmsg *nm, msg_t m, int off, struct path *path)
{
	if (off == -1) {
		return -1;
	}
	if ((off = msg_extract_ntohl_int32(m, off, &path->len)) == -1) {
		return -1;
	}
	assert(0 < path->len && path->len <= NFS_MAXPATHLEN);
	if ((path->data = nfsmsg_alloc_data(nm, path->len + 1)) == NULL) {
		report_error(FATAL, "msg_extract_path: nfsmsg_alloc_data (len=%d) error",
			     path->len);
		return -1;
	}
	off = msg_extract(m, off, path->len, path->data);
	path->data[path->len] = '\0'; /* path->data has extra byte for term */
	return roundup4(off);
}

static int
msg_extract_time(msg_t m, int off, struct nfstime *time)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_ntohl_int32(m, off, &time->sec);
	off = msg_extract_ntohl_int32(m, off, &time->nsec);
	return off;
}

static int
msg_extract_opttime(msg_t m, int off, struct opttime *opttime)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_boolean(m, off, &opttime->present);
	if (opttime->present) {
		off = msg_extract_time(m, off, &opttime->time);
	}
	return off;
}

static int
msg_extract_sattr(msg_t m, int off, struct sattr *sattr)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_boolean(m, off, &sattr->sa_mode.present);
	if (sattr->sa_mode.present) {
		off = msg_extract_ntohl_int32(m, off, &sattr->sa_mode.sa_mode);
	}
	off = msg_extract_boolean(m, off, &sattr->sa_uid.present);
	if (sattr->sa_uid.present) {
		off = msg_extract_ntohl_int32(m, off, &sattr->sa_uid.sa_uid);
	}
	off = msg_extract_boolean(m, off, &sattr->sa_gid.present);
	if (sattr->sa_gid.present) {
		off = msg_extract_ntohl_int32(m, off, &sattr->sa_gid.sa_gid);
	}
	off = msg_extract_boolean(m, off, &sattr->sa_size.present);
	if (sattr->sa_size.present) {
		off = msg_extract_ntohl_int64(m, off, &sattr->sa_size.sa_size);
	}
	off = msg_extract_boolean(m, off, &sattr->sa_atime.present);
	if (sattr->sa_atime.present == NFSV3SATTRTIME_TOCLIENT) {
		off = msg_extract_time(m, off, &sattr->sa_atime.sa_atime);
	}
	off = msg_extract_boolean(m, off, &sattr->sa_mtime.present);
	if (sattr->sa_mtime.present == NFSV3SATTRTIME_TOCLIENT) {
		off = msg_extract_time(m, off, &sattr->sa_mtime.sa_mtime);
	}	
	return off;
}

static int
msg_extract_fattr(struct nfsmsg *nm, msg_t m, int off, 
		struct fattr *fattr)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_ntohl_int32(m, off, &fattr->fa_type);
	off = msg_extract_ntohl_int32(m, off, &fattr->fa_mode);
	off = msg_extract_ntohl_int32(m, off, &fattr->fa_nlink);
	off = msg_extract_ntohl_int32(m, off, &fattr->fa_uid);
	off = msg_extract_ntohl_int32(m, off, &fattr->fa_gid);
	off = msg_extract_ntohl_int64(m, off, &fattr->fa_size);
	off = msg_extract_ntohl_int64(m, off, &fattr->fa_used);
	off = msg_extract_ntohl_int64(m, off, &fattr->fa_spec);
	off = msg_extract_ntohl_int64(m, off, &fattr->fa_fsid);
	off = msg_extract_ntohl_int64(m, off, &fattr->fa_fileid);
	off = msg_extract_time(m, off, &fattr->fa_atime);
	off = msg_extract_time(m, off, &fattr->fa_mtime);
	off = msg_extract_time(m, off, &fattr->fa_ctime);
	return off;
}

static int
msg_extract_optfattr(struct nfsmsg *nm, msg_t m, int off, 
		   struct optfattr *optfattr)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_boolean(m, off, &optfattr->present);
	if (optfattr->present) {
		off = msg_extract_fattr(nm, m, off, &optfattr->fattr);
	}
	return off;
}

static int
msg_extract_wcc_data(struct nfsmsg *nm, msg_t m, int off, 
		   struct wcc_data *wcc_data)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_boolean(m, off, &wcc_data->before.present);
	if (wcc_data->before.present) {
		off = msg_extract_ntohl_int64(m, off, &wcc_data->before.size);
		off = msg_extract_time(m, off, &wcc_data->before.mtime);
		off = msg_extract_time(m, off, &wcc_data->before.ctime);
	}
	off = msg_extract_optfattr(nm, m, off, &wcc_data->after);
	return off;
}

static int
msg_extract_diroparg(struct nfsmsg *nm, msg_t m, int off, 
		   struct diroparg *doa)
{
	if (off == -1) {
		return -1;
	}
	off = msg_extract_fh(nm, m, off, &doa->dir);
	off = msg_extract_fn(nm, m, off, &doa->name);
	return off;
}

static int
msg_extract_opaque(struct nfsmsg *nm, msg_t m, int off, 
		 struct opaque *opaque)
{
	if (off == -1) {
		return -1;
	}
	if ((off = msg_extract_ntohl_int32(m, off, &opaque->len)) == -1) {
		return -1;
	}
#ifdef SHORT_RECV_ENABLE
#warning "OPT: DISABLING OPAQUE DATA EXTRACTION"
#else
	if (off + opaque->len > msg_mlen(m)) {
		report_error(FATAL, "msg_extract_opaque: offset (%d) + len (%d) > mlen (%d)",
			     off, opaque->len, msg_mlen(m));
		return -1;
	}
#endif
	opaque->data = msg_mtod(m) + off;
	return roundup4(off + opaque->len);
}

static int
msg_extract_dirlist(struct nfsmsg *nm, msg_t m, int off,
		  struct dirlist *dirlist)
{
	struct entry **entryptr = &dirlist->entries;
	u_int32_t present;

	if (off == -1) {
		return -1;
	}

#ifdef SHORT_RECV_ENABLE
#warning "OPT: DISABLING DIRLIST EXTRACTION"
#else
	off = msg_extract_boolean(m, off, &present);
	while (present) {
		*entryptr = nfsmsg_alloc_data(nm, sizeof(struct entry));
		if (*entryptr == NULL) {
			report_error(FATAL, "msg_extract_dirlist: nfsmsg_alloc_data (len=%d) error",
				     (int)sizeof(struct entry));
			return -1;
		}
		off = msg_extract_ntohl_int64(m, off, &(*entryptr)->fileid);
		off = msg_extract_fn(nm, m, off, &(*entryptr)->name);
		off = msg_extract_ntohl_int64(m, off, &(*entryptr)->cookie);
		entryptr = &(*entryptr)->nextentry;
		off = msg_extract_boolean(m, off, &present);
	}
	*entryptr = NULL;
	off = msg_extract_ntohl_int32(m, off, &dirlist->eof);
#endif
	return off;
}

static int
msg_extract_dirlistplus(struct nfsmsg *nm, msg_t m, int off,
		      struct dirlistplus *dirlist)
{
	struct entryplus **entryptr = &dirlist->entries;
	u_int32_t present;

	if (off == -1) {
		return -1;
	}

#ifdef SHORT_RECV_ENABLE
#warning "OPT: DISABLING DIRLIST EXTRACTION"
#else
	off = msg_extract_boolean(m, off, &present);
	while (present) {
		*entryptr = nfsmsg_alloc_data(nm, sizeof(struct entryplus));
		if (*entryptr == NULL) {
			report_error(FATAL, "msg_extract_dirlistplus: nfsmsg_alloc_data (len=%d) error",
				     (int)sizeof(struct entryplus));
			return -1;
		}
		off = msg_extract_ntohl_int64(m, off, &(*entryptr)->fileid);
		off = msg_extract_fn(nm, m, off, &(*entryptr)->name);
		off = msg_extract_ntohl_int64(m, off, &(*entryptr)->cookie);
		off = msg_extract_optfattr(nm, m, off, &(*entryptr)->optfattr);
		off = msg_extract_optfh(nm, m, off, &(*entryptr)->optfh);
		entryptr = &(*entryptr)->nextentry;
		off = msg_extract_boolean(m, off, &present);
	}
	*entryptr = NULL;
	off = msg_extract_ntohl_int32(m, off, &dirlist->eof);
#endif
	return off;
}

/* ------------------------------------------------------- */

static int
nfs_parse_call(msg_t m, int off, struct nfsmsg *nm)
{	
	switch (nm->proc) {
	case NFSPROC_NULL:
		break;
	case NFSPROC_GETATTR: {
		struct getattr_arg *arg = &nm->u.getattr_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		break;
	}
	case NFSPROC_SETATTR: {
		struct setattr_arg *arg = &nm->u.setattr_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_sattr(m, off, &arg->sattr);
		off = msg_extract_opttime(m, off, &arg->guard_ctime);
		break;
	}
	case NFSPROC_LOOKUP: {
		struct lookup_arg *arg = &nm->u.lookup_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->what);
		break;
	}
	case NFSPROC_ACCESS: {
		struct access_arg *arg = &nm->u.access_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_ntohl_int32(m, off, &arg->access);
		break;
	}
	case NFSPROC_READLINK: {
		struct readlink_arg *arg = &nm->u.readlink_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		break;
	}
	case NFSPROC_READ: {
		struct read_arg *arg = &nm->u.read_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_ntohl_int64(m, off, &arg->offset);
		off = msg_extract_ntohl_int32(m, off, &arg->count);
		break;
	}
	case NFSPROC_WRITE: {
		struct write_arg *arg = &nm->u.write_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_ntohl_int64(m, off, &arg->offset);
		off = msg_extract_ntohl_int32(m, off, &arg->count);
		off = msg_extract_ntohl_int32(m, off, &arg->stable);
		off = msg_extract_opaque(nm, m, off, &arg->data);
		break;
	}
	case NFSPROC_CREATE: {
		struct create_arg *arg = &nm->u.create_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->where);
		off = msg_extract_ntohl_int32(m, off, &arg->createmode);
		switch (arg->createmode) {
		case NFSV3CREATE_UNCHECKED:
		case NFSV3CREATE_GUARDED:
			off = msg_extract_sattr(m, off, &arg->sattr);
			break;
		case NFSV3CREATE_EXCLUSIVE:
			off = msg_extract_ntohl_int64(m, off, &arg->createverf);
			break;
		}
		break;
	}
	case NFSPROC_MKDIR: {
		struct mkdir_arg *arg = &nm->u.mkdir_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->where);
		off = msg_extract_sattr(m, off, &arg->sattr);
		break;
	}
	case NFSPROC_SYMLINK: {
		struct symlink_arg *arg = &nm->u.symlink_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->where);
		off = msg_extract_sattr(m, off, &arg->symlinkdata.sattr);
		off = msg_extract_path(nm, m, off, &arg->symlinkdata.path);
		break;
	}
	case NFSPROC_MKNOD: {
		struct mknod_arg *arg = &nm->u.mknod_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->where);
		off = msg_extract_ntohl_int32(m, off, &arg->type);
		switch(arg->type) {
		case NFCHR:
		case NFBLK:
			off = msg_extract_sattr(m, off, &arg->sattr);
			off = msg_extract_ntohl_int64(m, off, &arg->spec);
			break;
		case NFSOCK:
		case NFFIFO:
			off = msg_extract_sattr(m, off, &arg->sattr);
			break;
		}
		break;
	}
	case NFSPROC_REMOVE: {
		struct remove_arg *arg = &nm->u.remove_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->what);
		break;
	}
	case NFSPROC_RMDIR: {
		struct rmdir_arg *arg = &nm->u.rmdir_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->what);
		break;
	}
	case NFSPROC_RENAME: {
		struct rename_arg *arg = &nm->u.rename_arg;
		off = msg_extract_diroparg(nm, m, off, &arg->from);
		off = msg_extract_diroparg(nm, m, off, &arg->to);
		break;
	}
	case NFSPROC_LINK: {
		struct link_arg *arg = &nm->u.link_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_diroparg(nm, m, off, &arg->link);
		break;
	}
	case NFSPROC_READDIR: {
		struct readdir_arg *arg = &nm->u.readdir_arg;
		off = msg_extract_fh(nm, m, off, &arg->dir);
		off = msg_extract_ntohl_int64(m, off, &arg->cookie);
		off = msg_extract_ntohl_int64(m, off, &arg->cookieverf);
		off = msg_extract_ntohl_int32(m, off, &arg->count);
		break;
	}
	case NFSPROC_READDIRPLUS: {
		struct readdirplus_arg *arg = &nm->u.readdirplus_arg;
		off = msg_extract_fh(nm, m, off, &arg->dir);
		off = msg_extract_ntohl_int64(m, off, &arg->cookie);
		off = msg_extract_ntohl_int64(m, off, &arg->cookieverf);
		off = msg_extract_ntohl_int32(m, off, &arg->dircount);
		off = msg_extract_ntohl_int32(m, off, &arg->maxcount);
		break;
	}
	case NFSPROC_FSSTAT: {
		struct fsstat_arg *arg = &nm->u.fsstat_arg;
		off = msg_extract_fh(nm, m, off, &arg->fsroot);
		break;
	}
	case NFSPROC_FSINFO: {
		struct fsinfo_arg *arg = &nm->u.fsinfo_arg;
		off = msg_extract_fh(nm, m, off, &arg->fsroot);
		break;
	}
	case NFSPROC_PATHCONF: {
		struct pathconf_arg *arg = &nm->u.pathconf_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		break;
	}
	case NFSPROC_COMMIT: {
		struct commit_arg *arg = &nm->u.commit_arg;
		off = msg_extract_fh(nm, m, off, &arg->fh);
		off = msg_extract_ntohl_int64(m, off, &arg->offset);
		off = msg_extract_ntohl_int32(m, off, &arg->count);
		break;
	}
	default:
		report_error(FATAL, "nfs_parse_call: unknown proc %d", 
			     nm->proc);
		assert(0);
		break;
	}
	if (off != -1 && off != msg_mlen(m)) {
		report_error(NONFATAL, "call warning: not all packet (proc %d) consumed (off=%d max=%d delta=%d)", 
			     nm->proc, off, msg_mlen(m), msg_mlen(m) - off);
	}
	return (off == -1);
}

static int
nfs_parse_reply(msg_t m, int off, struct nfsmsg *nm)
{
	if (nm->proc != NFSPROC_NULL) {
		off = msg_extract_ntohl_int32(m, off, &nm->u.result_status);
	}
	switch (nm->proc) {
	case NFSPROC_NULL:
		break;
	case NFSPROC_GETATTR: {
		if (nm->u.result_status == NFS_OK) {
			struct getattr_res *res = &nm->u.getattr_res;
			off = msg_extract_fattr(nm, m, off, &res->fattr);
		} else {
			;
		}
		break;
	}
	case NFSPROC_SETATTR: {
		if (nm->u.result_status == NFS_OK) {
			struct setattr_res *res = &nm->u.setattr_res;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
		} else {
			struct setattr_resfail *res = &nm->u.setattr_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
		}
		break;
	}
	case NFSPROC_LOOKUP: {
		if (nm->u.result_status == NFS_OK) {
			struct lookup_res *res = &nm->u.lookup_res;
			off = msg_extract_fh(nm, m, off, &res->fh);
			off = msg_extract_optfattr(nm, m, off, &res->obj_optfattr);
			off = msg_extract_optfattr(nm, m, off, &res->dir_optfattr);
		} else {
			struct lookup_resfail *res = &nm->u.lookup_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->dir_optfattr);
		}
		break;
	}
	case NFSPROC_ACCESS: {
		if (nm->u.result_status == NFS_OK) {
			struct access_res *res = &nm->u.access_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int32(m, off, &res->access);
		} else {
			struct access_resfail *res = &nm->u.access_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READLINK: {
		if (nm->u.result_status == NFS_OK) {
			struct readlink_res *res = &nm->u.readlink_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_path(nm, m, off, &res->path);
		} else {
			struct readlink_resfail *res = &nm->u.readlink_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READ: {
		if (nm->u.result_status == NFS_OK) {
			struct read_res *res = &nm->u.read_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int32(m, off, &res->count);
			off = msg_extract_ntohl_int32(m, off, &res->eof);
			off = msg_extract_opaque(nm, m, off, &res->data);
		} else {
			struct read_resfail *res = &nm->u.read_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_WRITE: {
		if (nm->u.result_status == NFS_OK) {
			struct write_res *res = &nm->u.write_res;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
			off = msg_extract_ntohl_int32(m, off, &res->count);
			off = msg_extract_ntohl_int32(m, off, &res->committed);
			off = msg_extract_ntohl_int64(m, off, &res->verf);
		} else {
			struct write_resfail *res = &nm->u.write_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
		}
		break;
	}
	case NFSPROC_CREATE: {
		if (nm->u.result_status == NFS_OK) {
			struct create_res *res = &nm->u.create_res;
			off = msg_extract_optfh(nm, m, off, &res->optfh);
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct create_resfail *res = &nm->u.create_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_MKDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct mkdir_res *res = &nm->u.mkdir_res;
			off = msg_extract_optfh(nm, m, off, &res->optfh);
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct mkdir_resfail *res = &nm->u.mkdir_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_SYMLINK: {
		if (nm->u.result_status == NFS_OK) {
			struct symlink_res *res = &nm->u.symlink_res;
			off = msg_extract_optfh(nm, m, off, &res->optfh);
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct symlink_resfail *res = &nm->u.symlink_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_MKNOD: {
		if (nm->u.result_status == NFS_OK) {
			struct mknod_res *res = &nm->u.mknod_res;
			off = msg_extract_optfh(nm, m, off, &res->optfh);
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct mknod_resfail *res = &nm->u.mknod_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_REMOVE: {
		if (nm->u.result_status == NFS_OK) {
			struct remove_res *res = &nm->u.remove_res;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct remove_resfail *res = &nm->u.remove_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_RMDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct rmdir_res *res = &nm->u.rmdir_res;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		} else {
			struct rmdir_resfail *res = &nm->u.rmdir_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->dir_wcc);
		}
		break;
	}
	case NFSPROC_RENAME: {
		if (nm->u.result_status == NFS_OK) {
			struct rename_res *res = &nm->u.rename_res;
			off = msg_extract_wcc_data(nm, m, off, &res->fromdir_wcc);
			off = msg_extract_wcc_data(nm, m, off, &res->todir_wcc);
		} else {
			struct rename_resfail *res = &nm->u.rename_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->fromdir_wcc);
			off = msg_extract_wcc_data(nm, m, off, &res->todir_wcc);
		}
		break;
	}
	case NFSPROC_LINK: {
		if (nm->u.result_status == NFS_OK) {
			struct link_res *res = &nm->u.link_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->linkdir_wcc);
		} else {
			struct link_resfail *res = &nm->u.link_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_wcc_data(nm, m, off, &res->linkdir_wcc);
		}
		break;
	}
	case NFSPROC_READDIR: {
		if (nm->u.result_status == NFS_OK) {
			struct readdir_res *res = &nm->u.readdir_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int64(m, off, &res->cookieverf);
			off = msg_extract_dirlist(nm, m, off, &res->dirlist);
		} else {
			struct readdir_resfail *res = &nm->u.readdir_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_READDIRPLUS: {
		if (nm->u.result_status == NFS_OK) {
			struct readdirplus_res *res = &nm->u.readdirplus_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int64(m, off, &res->cookieverf);
			off = msg_extract_dirlistplus(nm, m, off, &res->dirlist);
		} else {
			struct readdirplus_resfail *res = &nm->u.readdirplus_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_FSSTAT: {
		if (nm->u.result_status == NFS_OK) {
			struct fsstat_res *res = &nm->u.fsstat_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int64(m, off, &res->tbytes);
			off = msg_extract_ntohl_int64(m, off, &res->fbytes);
			off = msg_extract_ntohl_int64(m, off, &res->abytes);
			off = msg_extract_ntohl_int64(m, off, &res->tfiles);
			off = msg_extract_ntohl_int64(m, off, &res->ffiles);
			off = msg_extract_ntohl_int64(m, off, &res->afiles);
			off = msg_extract_ntohl_int32(m, off, &res->invarsec);
		} else {
			struct fsstat_resfail *res = &nm->u.fsstat_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_FSINFO: {
		if (nm->u.result_status == NFS_OK) {
			struct fsinfo_res *res = &nm->u.fsinfo_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int32(m, off, &res->rtmax);
			off = msg_extract_ntohl_int32(m, off, &res->rtpref);
			off = msg_extract_ntohl_int32(m, off, &res->rtmult);
			off = msg_extract_ntohl_int32(m, off, &res->wtmax);
			off = msg_extract_ntohl_int32(m, off, &res->wtpref);
			off = msg_extract_ntohl_int32(m, off, &res->wtmult);
			off = msg_extract_ntohl_int32(m, off, &res->dtpref);
			off = msg_extract_ntohl_int64(m, off, &res->maxfilesize);
			off = msg_extract_time(m, off, &res->time_delta);
			off = msg_extract_ntohl_int32(m, off, &res->properties);
		} else {
			struct fsinfo_resfail *res = &nm->u.fsinfo_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_PATHCONF: {
		if (nm->u.result_status == NFS_OK) {
			struct pathconf_res *res = &nm->u.pathconf_res;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
			off = msg_extract_ntohl_int32(m, off, &res->linkmax);
			off = msg_extract_ntohl_int32(m, off, &res->name_max);
			off = msg_extract_ntohl_int32(m, off, &res->no_trunc);
			off = msg_extract_ntohl_int32(m, off, &res->chown_restricted);
			off = msg_extract_ntohl_int32(m, off, &res->case_insensitive);
			off = msg_extract_ntohl_int32(m, off, &res->case_preserving);
		} else {
			struct pathconf_resfail *res = &nm->u.pathconf_resfail;
			off = msg_extract_optfattr(nm, m, off, &res->optfattr);
		}
		break;
	}
	case NFSPROC_COMMIT: {
		if (nm->u.result_status == NFS_OK) {
			struct commit_res *res = &nm->u.commit_res;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
			off = msg_extract_ntohl_int64(m, off, &res->verf);
		} else {
			struct commit_resfail *res = &nm->u.commit_resfail;
			off = msg_extract_wcc_data(nm, m, off, &res->wcc_data);
		}
		break;
	}
	default:
		report_error(FATAL, "nfs_parse_reply: unknown proc %d", 
			     nm->proc);
		assert(0);
		break;
	}
	if (off != -1 && off != msg_mlen(m)) {
#if 0 
		/* 
		 * these happen for read replies from a FreeBSD NFS server,
		 * over both UDP and TCP.  XXX
		 */
		report_error(NONFATAL, "reply warning: not all packet (proc %d) consumed (off=%d max=%d delta=%d)", 
			     nm->proc, off, msg_mlen(m), msg_mlen(m) - off);
#endif
		return 1;
	}
	return (off == -1);
}

static int
nfs_parse(msg_t m, int off, struct nfsmsg *nm)
{
	switch (nm->direction) {
	case CALL:
		return nfs_parse_call(m, off, nm);
	case REPLY:
		return nfs_parse_reply(m, off, nm);
	}
	return -1;
}

/* ------------------------------------------------------- */

/*
 * maintain a cache of recent client/server <xid,proc,vers> tuples.
 * server replies contian only an xid, this map matches that xid to a proc
 * type and thus how in interpret the packet.
 */
#define XID_DB_SIZE 65536
#define HASHSIZE 128

static struct xid_db_entry {
	u_int32_t xid;  /* host order */
	u_int32_t memory;
	Q_ENTRY(xid_db_entry) link;
	Q_ENTRY(xid_db_entry) hash;
} xid_map[XID_DB_SIZE];
static Q_HEAD(xme_active, xid_db_entry) xme_active;
static Q_HEAD(xme_free, xid_db_entry) xme_free;
static Q_HEAD(xme_hash, xid_db_entry) xme_hash[HASHSIZE];
static int xid_db_ready = 0;

static void
xid_db_init(void)
{
	int i;

	if (xid_db_ready == 0) {
		Q_INIT(&xme_active);
		Q_INIT(&xme_free);
		for (i=0 ; i<HASHSIZE ; i++) {
			Q_INIT(&xme_hash[i]);
		}
		for (i=0 ; i<XID_DB_SIZE ; i++) {
			Q_INSERT_HEAD(&xme_free, &xid_map[i], link);
		}
		xid_db_ready = 1;
	}
}

static void
xid_db_enter(u_int32_t xid, u_int32_t memory)
{
	struct xid_db_entry *xme;
	int hash = xid & (HASHSIZE - 1);

	if (xid_db_ready == 0) {
		xid_db_init();
	}

	if ((xme = Q_LAST(&xme_free, xme_free)) != NULL) {
		Q_REMOVE(&xme_free, xme, link);
	} else {
		xme = Q_LAST(&xme_active, xme_active);
		Q_REMOVE(&xme_active, xme, link);
		Q_REMOVE(&xme_hash[xme->xid & (HASHSIZE-1)], xme, hash);
	}

	xme->xid = xid;
	xme->memory = memory;
	
	Q_INSERT_HEAD(&xme_active, xme, link);
	Q_INSERT_HEAD(&xme_hash[hash], xme, hash);
}

static int
xid_db_lookup(u_int32_t xid, u_int32_t *memory)
{
	struct xid_db_entry *xme;
	int hash = xid & (HASHSIZE - 1);
	
	if (xid_db_ready == 0) {
		xid_db_init();
	}

	Q_FOREACH(xme, &xme_hash[hash], hash) {
		if (xme->xid != xid) {
			continue;
		}
		*memory = xme->memory;
		Q_REMOVE(&xme_hash[xme->xid & (HASHSIZE-1)], xme, hash);
		Q_REMOVE(&xme_active, xme, link);
		Q_INSERT_HEAD(&xme_free, xme, link);
		return 0;
	}
	return -1;
}

/* ------------------------------------------------------- */

int
nfs_send(int sock, int socktype, struct nfsmsg *nm, u_int32_t *xidp)
{
	int rvalue = -1, off;
	u_int32_t xid = (xidp != NULL) ? *xidp : 0;
	msg_t m = 0;
	static int uid = -1, gid = -1;

	if (uid == -1) {
		uid = getuid();
		gid = getgid();
	}

	if ((m = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc failed");
		goto out;
	}
	if ((off = nfs_build(m, 0, nm)) < 0) {
		report_error(FATAL, "nfs_build failed");
		goto out;
	}
	if ((rvalue = rpc_send(sock, socktype, m, NFS_PROG, NFS_VER3, 
			       nm->proc, uid, gid, &xid)) < 0) {
		report_error(FATAL, "rpc_send error");
		goto out;
	}
	xid_db_enter(xid, nm->proc);
	if (xidp) {
		*xidp = xid;
	}
	rvalue = 0;
 out:
	if (m) {
		msg_free(m);
	}
	return rvalue;
}

int
nfs_recv(int sock, int socktype, struct nfsmsg *nm, u_int32_t *xidp)
{
	int rvalue = -1, off;
	u_int32_t xid;
	msg_t m = 0;

	if ((m = msg_alloc()) == NULL) {
		report_error(FATAL, "msg_alloc failed");
		goto out;
	}
	if ((off = rpc_recv(sock, socktype, m, &xid)) < 0) {
		report_error(FATAL, "rpc_recv error");
		goto out;
	}
	if (xid_db_lookup(xid, &nm->proc) < 0) {
		report_error(NONFATAL, "nfs_recv no match for xid");
		goto out;
	}
	if ((off = nfs_parse(m, off, nm)) < 0) {
		report_error(FATAL, "nfs_parse error");
		goto out;
	}
	if (xidp) {
		*xidp = xid;
	}
	rvalue = 0;
 out:
	if (m) {
		msg_free(m);
	}
	return rvalue;
}

/* ------------------------------------------------------- */

static struct {
	int status; char *str;
} nfs_errstrs[] = {
	{ NFS_OK, "NFS_OK" },
	{ NFSERR_PERM, "NFSERR_PERM" },
	{ NFSERR_NOENT, "NFSERR_NOENT" },
	{ NFSERR_IO, "NFSERR_IO" },
	{ NFSERR_NXIO, "NFSERR_NXIO" },
	{ NFSERR_ACCES, "NFSERR_ACCES" },
	{ NFSERR_EXIST, "NFSERR_EXIST" },
	{ NFSERR_XDEV, "NFSERR_XDEV" },
	{ NFSERR_NODEV, "NFSERR_NODEV" },
	{ NFSERR_NOTDIR, "NFSERR_NOTDIR" },
	{ NFSERR_ISDIR, "NFSERR_ISDIR" },
	{ NFSERR_INVAL, "NFSERR_INVAL" },
	{ NFSERR_FBIG, "NFSERR_FBIG" },
	{ NFSERR_NOSPC, "NFSERR_NOSPC" },
	{ NFSERR_ROFS, "NFSERR_ROFS" },
	{ NFSERR_MLINK, "NFSERR_MLINK" },
	{ NFSERR_NAMETOL, "NFSERR_NAMETOL" },
	{ NFSERR_NOTEMPTY, "NFSERR_NOTEMPTY" },
	{ NFSERR_DQUOT, "NFSERR_DQUOT" },
	{ NFSERR_STALE, "NFSERR_STALE" },
	{ NFSERR_REMOTE, "NFSERR_REMOTE" },
	{ NFSERR_WFLUSH, "NFSERR_WFLUSH" },
	{ NFSERR_BADHANDLE, "NFSERR_BADHANDLE" },
	{ NFSERR_NOT_SYNC, "NFSERR_NOT_SYNC" },
	{ NFSERR_BAD_COOKIE, "NFSERR_BAD_COOKIE" },
	{ NFSERR_NOTSUPP, "NFSERR_NOTSUPP" },
	{ NFSERR_TOOSMALL, "NFSERR_TOOSMALL" },
	{ NFSERR_SERVERFAULT, "NFSERR_SERVERFAULT" },
	{ NFSERR_BADTYPE, "NFSERR_BADTYPE" },
	{ NFSERR_JUKEBOX, "NFSERR_JUKEBOX" },
	{ NFSERR_TRYLATER, "NFSERR_TRYLATER" },
	{ NFSERR_STALEWRITEVERF, "NFSERR_STALEWRITEVERF" },
	{ -1, NULL }
};

char *
nfs_errstr(int status)
{
	static char buf[32];
	int i = 0;

	for (i=0 ; nfs_errstrs[i].status != -1 ; i++) {
		if (nfs_errstrs[i].status == status) {
			return nfs_errstrs[i].str;
		}
	}
	snprintf(buf, sizeof(buf), "[status %d]", status);
	return buf;
}

static struct {
	int proc; char *str;
} nfs_procstrs[] = {
	{ NFSPROC_NULL, "NFSPROC_NULL" },
	{ NFSPROC_GETATTR, "NFSPROC_GETATTR" },
	{ NFSPROC_SETATTR, "NFSPROC_SETATTR" },
	{ NFSPROC_LOOKUP, "NFSPROC_LOOKUP" },
	{ NFSPROC_ACCESS, "NFSPROC_ACCESS" },
	{ NFSPROC_READLINK, "NFSPROC_READLINK" },
	{ NFSPROC_READ, "NFSPROC_READ" },
	{ NFSPROC_WRITE, "NFSPROC_WRITE" },
	{ NFSPROC_CREATE, "NFSPROC_CREATE" },
	{ NFSPROC_MKDIR, "NFSPROC_MKDIR" },
	{ NFSPROC_SYMLINK, "NFSPROC_SYMLINK" },
	{ NFSPROC_MKNOD, "NFSPROC_MKNOD" },
	{ NFSPROC_REMOVE, "NFSPROC_REMOVE" },
	{ NFSPROC_RMDIR, "NFSPROC_RMDIR" },
	{ NFSPROC_RENAME, "NFSPROC_RENAME" },
	{ NFSPROC_LINK, "NFSPROC_LINK" },
	{ NFSPROC_READDIR, "NFSPROC_READDIR" },
	{ NFSPROC_READDIRPLUS, "NFSPROC_READDIRPLUS" },
	{ NFSPROC_FSSTAT, "NFSPROC_FSSTAT" },
	{ NFSPROC_FSINFO, "NFSPROC_FSINFO" },
	{ NFSPROC_PATHCONF, "NFSPROC_PATHCONF" },
	{ NFSPROC_COMMIT, "NFSPROC_COMMIT" },
	{ NQNFSPROC_GETLEASE, "NQNFSPROC_GETLEASE" },
	{ NQNFSPROC_VACATED, "NQNFSPROC_VACATED" },
	{ NQNFSPROC_EVICTED, "NQNFSPROC_EVICTED" },
	{ NFSPROC_NOOP, "NFSPROC_NOOP" },
	{ -1, NULL }
};

char *
nfs_procstr(int proc)
{
	static char buf[32];
	int i = 0;

	for (i=0 ; nfs_procstrs[i].proc != -1 ; i++) {
		if (nfs_procstrs[i].proc == proc) {
			return nfs_procstrs[i].str;
		}
	}
	snprintf(buf, sizeof(buf), "[proc %d]", proc);
	return buf;
}
