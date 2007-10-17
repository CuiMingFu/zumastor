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
 * we're using raw sockets and building all the mount and NFS messages
 * ourself.  as a result, we only care about getting the right wire format
 * and no interaction with any client NFS implementation.  all constants
 * use the FreeBSD kernel names, but for portability reasons they're
 * duplicated here without ties to any system include files.  (ultimately,
 * these are from the NFSv3 spec, and not specific to any implementation.)
 */

/*
 * mount constants (rpcsvc/mount.h)
 */

#define MOUNTPROG               100005
#define MOUNTVERS3              3
#define MNTPATHLEN              1024
#define FHSIZE3                 64
#define MOUNTPROC_MNT           1

/*
 * NFSv3 constants 
 */
#define	NFS_PROG                100003
#define	NFS_VER3                3
#define	NFSX_V3FHMAX            64
#define	NFS_MAXNAMLEN           255
#define	NFS_MAXPATHLEN          1024


/* "file" types */
#define NFNON                   0
#define NFREG                   1
#define NFDIR                   2
#define NFBLK                   3
#define NFCHR                   4
#define NFLNK                   5
#define NFSOCK                  6
#define NFFIFO                  7

/* operation results status */
#define	NFS_OK                  0
#define	NFSERR_PERM		1
#define	NFSERR_NOENT		2
#define	NFSERR_IO		5
#define	NFSERR_NXIO		6
#define	NFSERR_ACCES		13
#define	NFSERR_EXIST		17
#define	NFSERR_XDEV		18	/* Version 3 only */
#define	NFSERR_NODEV		19
#define	NFSERR_NOTDIR		20
#define	NFSERR_ISDIR		21
#define	NFSERR_INVAL		22	/* Version 3 only */
#define	NFSERR_FBIG		27
#define	NFSERR_NOSPC		28
#define	NFSERR_ROFS		30
#define	NFSERR_MLINK		31	/* Version 3 only */
#define	NFSERR_NAMETOL		63
#define	NFSERR_NOTEMPTY		66
#define	NFSERR_DQUOT		69
#define	NFSERR_STALE		70
#define	NFSERR_REMOTE		71	/* Version 3 only */
#define	NFSERR_WFLUSH		99	/* Version 2 only */
#define	NFSERR_BADHANDLE	10001	/* The rest Version 3 only */
#define	NFSERR_NOT_SYNC		10002
#define	NFSERR_BAD_COOKIE	10003
#define	NFSERR_NOTSUPP		10004
#define	NFSERR_TOOSMALL		10005
#define	NFSERR_SERVERFAULT	10006
#define	NFSERR_BADTYPE		10007
#define	NFSERR_JUKEBOX		10008
#define NFSERR_TRYLATER		NFSERR_JUKEBOX
#define	NFSERR_STALEWRITEVERF	30001	/* Fake return for nfs_commit() */

/* operation opcodes */
#define	NFSPROC_NULL            0
#define	NFSPROC_GETATTR         1
#define	NFSPROC_SETATTR         2
#define	NFSPROC_LOOKUP          3
#define	NFSPROC_ACCESS          4
#define	NFSPROC_READLINK        5
#define	NFSPROC_READ            6
#define	NFSPROC_WRITE           7
#define	NFSPROC_CREATE          8
#define	NFSPROC_MKDIR           9
#define	NFSPROC_SYMLINK         10
#define	NFSPROC_MKNOD           11
#define	NFSPROC_REMOVE          12
#define	NFSPROC_RMDIR           13
#define	NFSPROC_RENAME          14
#define	NFSPROC_LINK            15
#define	NFSPROC_READDIR         16
#define	NFSPROC_READDIRPLUS     17
#define	NFSPROC_FSSTAT          18
#define	NFSPROC_FSINFO          19
#define	NFSPROC_PATHCONF        20
#define	NFSPROC_COMMIT          21
#define	NQNFSPROC_GETLEASE	22
#define	NQNFSPROC_VACATED	23
#define	NQNFSPROC_EVICTED	24
#define NFSPROC_NOOP		25
#define	NFS_NPROCS		26

/* create modes */
#define NFSV3CREATE_UNCHECKED   0
#define NFSV3CREATE_GUARDED     1
#define NFSV3CREATE_EXCLUSIVE   2

/* write methods */
#define NFSV3WRITE_UNSTABLE     0
#define NFSV3WRITE_DATASYNC     1
#define NFSV3WRITE_FILESYNC     2

/* sattr time update modes */
#define NFSV3SATTRTIME_DONTCHANGE 0
#define NFSV3SATTRTIME_TOSERVER 1
#define NFSV3SATTRTIME_TOCLIENT 2

/* access mode checks */
#define NFSV3ACCESS_READ        0x01
#define NFSV3ACCESS_LOOKUP      0x02
#define NFSV3ACCESS_MODIFY      0x04
#define NFSV3ACCESS_EXTEND      0x08
#define NFSV3ACCESS_DELETE      0x10
#define NFSV3ACCESS_EXECUTE     0x20

/*
 * encoding/decoding macros.
 */

#define fxdr_hyper(f) \
        ((((u_quad_t)ntohl(((u_int32_t *)(f))[0])) << 32) | \
         (u_quad_t)(ntohl(((u_int32_t *)(f))[1])))
#define txdr_hyper(f, t) { \
        ((u_int32_t *)(t))[0] = htonl((u_int32_t)((f) >> 32)); \
        ((u_int32_t *)(t))[1] = htonl((u_int32_t)((f) & 0xffffffff)); \
}

