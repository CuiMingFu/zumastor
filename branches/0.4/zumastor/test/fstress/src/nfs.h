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

struct fh {
	u_int32_t len;
	char *data;
};
struct optfh {
	u_int32_t present;
	struct fh fh;
};
struct fn {
	u_int32_t len;
	char *data;
};
struct path {
	u_int32_t len;
	char *data;
};
struct nfstime {
	u_int32_t sec;
	u_int32_t nsec;
};
struct opttime {
	u_int32_t present;
	struct nfstime time;
};
struct sattr {
	struct {
		u_int32_t present;
		u_int32_t sa_mode;
	} sa_mode;
	struct {
		u_int32_t present;
		u_int32_t sa_uid;
	} sa_uid;
	struct {
		u_int32_t present;
		u_int32_t sa_gid;
	} sa_gid;
	struct {
		u_int32_t present;
		u_int64_t sa_size;
	} sa_size;
	struct {
		u_int32_t present;
		struct nfstime sa_atime;
	} sa_atime;
	struct {
		u_int32_t present;
		struct nfstime sa_mtime;
	} sa_mtime;
};
struct fattr {
	u_int32_t fa_type;
	u_int32_t fa_mode;
	u_int32_t fa_nlink;
	u_int32_t fa_uid;
	u_int32_t fa_gid;
	u_int64_t fa_size;
	u_int64_t fa_used;
	u_int64_t fa_spec;
	u_int64_t fa_fsid;
	u_int64_t fa_fileid;
	struct nfstime fa_atime;
	struct nfstime fa_mtime;
	struct nfstime fa_ctime;
};
struct optfattr {
	u_int32_t present;
	struct fattr fattr;
};
struct symlinkdata {
	struct sattr sattr;
	struct path path;
};
struct wcc_attr {
	u_int32_t present;
	u_int64_t size;
	struct nfstime mtime;
	struct nfstime ctime;
};
struct wcc_data {
	struct wcc_attr before;
	struct optfattr after;
};
struct diroparg {
	struct fh dir;
	struct fn name;
};

struct opaque {
	u_int32_t len;
	char *data;
};

struct entry {
	u_int64_t fileid;
	struct fn name;
	u_int64_t cookie;
	struct entry *nextentry;
};
struct dirlist {
	struct entry *entries;
	u_int32_t eof;
};

struct entryplus {
	u_int64_t fileid;
	struct fn name;
	u_int64_t cookie;
	struct optfattr optfattr;
	struct optfh optfh;
	struct entryplus *nextentry;
};
struct dirlistplus {
	struct entryplus *entries;
	u_int32_t eof;
};

/* ------------------------------------------------------- */
/*
 * nfs arguments and responses.
 */

struct getattr_arg {
	struct fh fh;
};
struct getattr_res {
	u_int32_t status;
	struct fattr fattr;
};
struct getattr_resfail {
	u_int32_t status;
};

struct setattr_arg {
	struct fh fh;
	struct sattr sattr;
	struct opttime guard_ctime;
};
struct setattr_res {
	u_int32_t status;
	struct wcc_data wcc_data;
};
struct setattr_resfail {
	u_int32_t status;
	struct wcc_data wcc_data;
};

struct lookup_arg {
	struct diroparg what;
};
struct lookup_res {
	u_int32_t status;
	struct fh fh;
	struct optfattr obj_optfattr;
	struct optfattr dir_optfattr;
};
struct lookup_resfail {
	u_int32_t status;
	struct optfattr dir_optfattr;
};

struct access_arg {
	struct fh fh;
	u_int32_t access;
};
struct access_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int32_t access;
};
struct access_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};	

struct readlink_arg {
	struct fh fh;
};
struct readlink_res {
	u_int32_t status;
	struct optfattr optfattr;
	struct path path;
};
struct readlink_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};	

struct read_arg {
	struct fh fh;
	u_int64_t offset;
	u_int32_t count;
};
struct read_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int32_t count;
	u_int32_t eof;
	struct opaque data;
};
struct read_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct write_arg {
	struct fh fh;
	u_int64_t offset;
	u_int32_t count;
	u_int32_t stable;
	struct opaque data;
};
struct write_res {
	u_int32_t status;
	struct wcc_data wcc_data;
	u_int32_t count;
	u_int32_t committed;
	u_int64_t verf;
};
struct write_resfail {
	u_int32_t status;
	struct wcc_data wcc_data;
};

struct create_arg {
	struct diroparg where;
	u_int32_t createmode;
	struct sattr sattr; /* if mode is UNCHECKED or GUARDED */
	u_int64_t createverf; /* if mode is EXCLUSIVE */
};
struct create_res {
	u_int32_t status;
	struct optfh optfh;
	struct optfattr optfattr;
	struct wcc_data dir_wcc;
};
struct create_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct mkdir_arg {
	struct diroparg where;
	struct sattr sattr;
};
struct mkdir_res {
	u_int32_t status;
	struct optfh optfh;
	struct optfattr optfattr;
	struct wcc_data dir_wcc;
};
struct mkdir_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct symlink_arg {
	struct diroparg where;
	struct symlinkdata symlinkdata;
};
struct symlink_res {
	u_int32_t status;
	struct optfh optfh;
	struct optfattr optfattr;
	struct wcc_data dir_wcc;
};
struct symlink_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct mknod_arg {
	struct diroparg where;
	u_int32_t type;
	struct sattr sattr;
	u_int64_t spec;
};
struct mknod_res {
	u_int32_t status;
	struct optfh optfh;
	struct optfattr optfattr;
	struct wcc_data dir_wcc;
};	
struct mknod_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct remove_arg {
	struct diroparg what;
};
struct remove_res {
	u_int32_t status;
	struct wcc_data dir_wcc;
};
struct remove_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct rmdir_arg {
	struct diroparg what;
};
struct rmdir_res {
	u_int32_t status;
	struct wcc_data dir_wcc;
};
struct rmdir_resfail {
	u_int32_t status;
	struct wcc_data dir_wcc;
};

struct rename_arg {
	struct diroparg from;
	struct diroparg to;
};
struct rename_res {
	u_int32_t status;
	struct wcc_data fromdir_wcc;
	struct wcc_data todir_wcc;
};
struct rename_resfail {
	u_int32_t status;
	struct wcc_data fromdir_wcc;
	struct wcc_data todir_wcc;
};

struct link_arg {
	struct fh fh;
	struct diroparg link;
};
struct link_res {
	u_int32_t status;
	struct optfattr optfattr;
	struct wcc_data linkdir_wcc;
};
struct link_resfail {
	u_int32_t status;
	struct optfattr optfattr;
	struct wcc_data linkdir_wcc;
};

struct readdir_arg {
	struct fh dir;
	u_int64_t cookie;
	u_int64_t cookieverf;
	u_int32_t count;
};
struct readdir_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int64_t cookieverf;
	struct dirlist dirlist;
};
struct readdir_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct readdirplus_arg {
	struct fh dir;
	u_int64_t cookie;
	u_int64_t cookieverf;
	u_int32_t dircount;
	u_int32_t maxcount;
};
struct readdirplus_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int64_t cookieverf;
	struct dirlistplus dirlist;
};
struct readdirplus_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct fsstat_arg {
	struct fh fsroot;
};
struct fsstat_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int64_t tbytes;
	u_int64_t fbytes;
	u_int64_t abytes;
	u_int64_t tfiles;
	u_int64_t ffiles;
	u_int64_t afiles;
	u_int32_t invarsec;
};
struct fsstat_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct fsinfo_arg {
	struct fh fsroot;
};
struct fsinfo_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int32_t rtmax;
	u_int32_t rtpref;
	u_int32_t rtmult;
	u_int32_t wtmax;
	u_int32_t wtpref;
	u_int32_t wtmult;
	u_int32_t dtpref;
	u_int64_t maxfilesize;
	struct nfstime time_delta;
	u_int32_t properties;
};
struct fsinfo_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct pathconf_arg {
	struct fh fh;
};
struct pathconf_res {
	u_int32_t status;
	struct optfattr optfattr;
	u_int32_t linkmax;
	u_int32_t name_max;
	u_int32_t no_trunc;
	u_int32_t chown_restricted;
	u_int32_t case_insensitive;
	u_int32_t case_preserving;
};
struct pathconf_resfail {
	u_int32_t status;
	struct optfattr optfattr;
};

struct commit_arg {
	struct fh fh;
	u_int64_t offset;
	u_int32_t count;
};
struct commit_res {
	u_int32_t status;
	struct wcc_data wcc_data;
	u_int64_t verf;
};
struct commit_resfail {
	u_int32_t status;
	struct wcc_data wcc_data;
};

/* ------------------------------------------------------- */

struct nfsmsg {
	/*
	 * proc and direction discriminate the following union to
	 * either a RPC_CALL (_arg) or RPC_REPLY (_res or _resfail).
	 * u.result_status further discriminates RPC_REPLY between
	 * resok (_res) and resfail (_resfail).
	 */
	u_int32_t proc, direction;
	union {
		u_int32_t result_status; /* first field in all res structs */
		struct getattr_arg getattr_arg;
		struct getattr_res getattr_res;
		struct getattr_resfail getattr_resfail;
		struct setattr_arg setattr_arg;
		struct setattr_res setattr_res;
		struct setattr_resfail setattr_resfail;
		struct lookup_arg lookup_arg;
		struct lookup_res lookup_res;
		struct lookup_resfail lookup_resfail;
		struct access_arg access_arg;
		struct access_res access_res;
		struct access_resfail access_resfail;
		struct readlink_arg readlink_arg;
		struct readlink_res readlink_res;
		struct readlink_resfail readlink_resfail;
		struct read_arg read_arg;
		struct read_res read_res;
		struct read_resfail read_resfail;
		struct write_arg write_arg;
		struct write_res write_res;
		struct write_resfail write_resfail;
		struct create_arg create_arg;
		struct create_res create_res;
		struct create_resfail create_resfail;
		struct mkdir_arg mkdir_arg;
		struct mkdir_res mkdir_res;
		struct mkdir_resfail mkdir_resfail;
		struct symlink_arg symlink_arg;
		struct symlink_res symlink_res;
		struct symlink_resfail symlink_resfail;
		struct mknod_arg mknod_arg;
		struct mknod_res mknod_res;
		struct mknod_resfail mknod_resfail;
		struct remove_arg remove_arg;
		struct remove_res remove_res;
		struct remove_resfail remove_resfail;
		struct rmdir_arg rmdir_arg;
		struct rmdir_res rmdir_res;
		struct rmdir_resfail rmdir_resfail;
		struct rename_arg rename_arg;
		struct rename_res rename_res;
		struct rename_resfail rename_resfail;
		struct link_arg link_arg;
		struct link_res link_res;
		struct link_resfail link_resfail;
		struct readdir_arg readdir_arg;
		struct readdir_res readdir_res;
		struct readdir_resfail readdir_resfail;
		struct readdirplus_arg readdirplus_arg;
		struct readdirplus_res readdirplus_res;
		struct readdirplus_resfail readdirplus_resfail;
		struct fsstat_arg fsstat_arg;
		struct fsstat_res fsstat_res;
		struct fsstat_resfail fsstat_resfail;
		struct fsinfo_arg fsinfo_arg;
		struct fsinfo_res fsinfo_res;
		struct fsinfo_resfail fsinfo_resfail;
		struct pathconf_arg pathconf_arg;
		struct pathconf_res pathconf_res;
		struct pathconf_resfail pathconf_resfail;
		struct commit_arg commit_arg;
		struct commit_res commit_res;
		struct commit_resfail commit_resfail;
	} u;

	struct alloc_data *alloc_data; /* reserved for alloc chaining */
	char alloc_data_reservoir[64];
	int alloc_data_reservoir_remaining;
};

void nfsmsg_prep(struct nfsmsg *nm, int direction);
void nfsmsg_rele(struct nfsmsg *nm);
void *nfsmsg_alloc_data(struct nfsmsg *nm, int len);

int nfs_send(int sock, int socktype, struct nfsmsg *nm, u_int32_t *xidp);
int nfs_recv(int sock, int socktype, struct nfsmsg *nm, u_int32_t *xidp);

char *nfs_errstr(int status);
char *nfs_procstr(int proc);
