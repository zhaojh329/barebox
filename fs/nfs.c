/*
 * nfs.c - barebox NFS driver
 *
 * Copyright (c) 2014 Uwe Kleine-König, Pengutronix
 * Copyright (c) 2012 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) Masami Komiya <mkomiya@sonare.it> 2004
 *
 * Based on U-Boot NFS code which is based on NetBSD code with
 * major changes to support nfs3.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <net.h>
#include <driver.h>
#include <fs.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <fs.h>
#include <init.h>
#include <linux/stat.h>
#include <linux/err.h>
#include <kfifo.h>
#include <linux/sizes.h>
#include <byteorder.h>
#include <globalvar.h>
#include <parseopt.h>

#define SUNRPC_PORT     111

#define PROG_PORTMAP    100000
#define PROG_NFS        100003
#define PROG_MOUNT      100005

#define MSG_CALL        0
#define MSG_REPLY       1

#define PORTMAP_GETPORT 3

#define MOUNT_ADDENTRY	1
#define MOUNT_UMOUNT	3

#define NFSPROC3_GETATTR	1
#define NFSPROC3_LOOKUP		3
#define NFSPROC3_READLINK	5
#define NFSPROC3_READ		6
#define NFSPROC3_READDIR	16

#define NFS3_FHSIZE      64
#define NFS3_COOKIEVERFSIZE	8

/* values of enum ftype3 */
#define NF3REG	1
#define NF3DIR	2
#define NF3BLK	3
#define NF3CHR	4
#define NF3LNK	5
#define NF3SOCK	6
#define NF3FIFO	7

/* values for enum nfsstat3 */
#define NFS3_OK			    0
#define NFS3ERR_PERM		    1
#define NFS3ERR_NOENT		    2
#define NFS3ERR_IO   		    5
#define NFS3ERR_NXIO 		    6
#define NFS3ERR_ACCES		   13
#define NFS3ERR_EXIST		   17
#define NFS3ERR_XDEV		   18
#define NFS3ERR_NODEV		   19
#define NFS3ERR_NOTDIR		   20
#define NFS3ERR_ISDIR		   21
#define NFS3ERR_INVAL		   22
#define NFS3ERR_FBIG		   27
#define NFS3ERR_NOSPC		   28
#define NFS3ERR_ROFS		   30
#define NFS3ERR_MLINK		   31
#define NFS3ERR_NAMETOOLONG	   63
#define NFS3ERR_NOTEMPTY	   66
#define NFS3ERR_DQUOT		   69
#define NFS3ERR_STALE		   70
#define NFS3ERR_REMOTE		   71
#define NFS3ERR_BADHANDLE 	10001
#define NFS3ERR_NOT_SYNC	10002
#define NFS3ERR_BAD_COOKIE	10003
#define NFS3ERR_NOTSUPP		10004
#define NFS3ERR_TOOSMALL	10005
#define NFS3ERR_SERVERFAULT	10006
#define NFS3ERR_BADTYPE		10007
#define NFS3ERR_JUKEBOX		10008

static void *nfs_packet;
static int nfs_len;

struct rpc_call {
	uint32_t id;
	uint32_t type;
	uint32_t rpcvers;
	uint32_t prog;
	uint32_t vers;
	uint32_t proc;
	uint32_t data[0];
};

struct rpc_reply {
	uint32_t id;
	uint32_t type;
	uint32_t rstatus;
	uint32_t verifier;
	uint32_t v2;
	uint32_t astatus;
	uint32_t data[0];
};

#define NFS_TIMEOUT	(2 * SECOND)
#define NFS_MAX_RESEND	5

struct nfs_priv {
	struct net_connection *con;
	IPaddr_t server;
	char *path;
	uint16_t mount_port;
	unsigned manual_mount_port:1;
	uint16_t nfs_port;
	unsigned manual_nfs_port:1;
	uint32_t rpc_id;
	uint32_t rootfh_len;
	char rootfh[NFS3_FHSIZE];
};

struct file_priv {
	struct kfifo *fifo;
	void *buf;
	uint32_t filefh_len;
	char filefh[NFS3_FHSIZE];
	struct nfs_priv *npriv;
};

static uint64_t nfs_timer_start;

static int nfs_state;
#define STATE_DONE			1
#define STATE_START			2

/*
 * common types used in more than one request:
 *
 * typedef uint32 count3;
 * typedef uint32 gid3;
 * typedef uint32 mode3;
 * typedef uint32 uid3;
 *
 * typedef uint64 cookie3;
 * typedef uint64 fileid3;
 * typedef uint64 size3;
 *
 * typedef string filename3<>;
 * typedef string nfspath3<>;
 *
 * typedef opaque cookieverf3[NFS3_COOKIEVERFSIZE];
 *
 * enum ftype3 {
 * 	NF3REG = 1,
 * 	NF3DIR = 2,
 * 	NF3BLK = 3,
 * 	NF3CHR = 4,
 * 	NF3LNK = 5,
 * 	NF3SOCK = 6,
 * 	NF3FIFO = 7
 * };
 *
 * struct specdata3 {
 * 	uint32 specdata1;
 * 	uint32 specdata2;
 * };
 *
 * struct nfs_fh3 {
 * 	opaque data<NFS3_FHSIZE>;
 * }
 *
 * struct nfstime3 {
 * 	uint32 seconds;
 * 	uint32 nseconds;
 * };
 *
 * struct fattr3 {
 * 	ftype3 type;
 * 	mode3 mode;
 * 	uint32_t nlink;
 * 	uid3 uid;
 * 	gid3 gid;
 * 	size3 size;
 * 	size3 used;
 * 	specdata3 rdev;
 * 	uint64_t fsid;
 * 	fileid3 fileid;
 * 	nfstime3 atime;
 * 	nfstime3 mtime;
 * 	nfstime3 ctime;
 * };
 *
 * struct diropargs3 {
 * 	nfs_fh3 dir;
 * 	filename3 name;
 * }
 *
 * union post_op_attr switch (bool attributes_follow) {
 * case TRUE:
 * 	fattr3 attributes;
 * case FALSE:
 * 	void;
 * };
 */

struct xdr_stream {
	__be32 *p;
	void *buf;
	__be32 *end;
};

#define xdr_zero 0
#define XDR_QUADLEN(l)		(((l) + 3) >> 2)

struct nfs_dir {
	DIR dir;

	/*
	 * stream points to the next entry3 in the reply member of READDIR3res
	 * (if any, to the end indicator otherwise).
	 */
	struct xdr_stream stream;
	struct dirent ent;
	struct file_priv *priv;
	uint64_t cookie;
	char cookieverf[NFS3_COOKIEVERFSIZE];
};

static void xdr_init(struct xdr_stream *stream, void *buf, int len)
{
	stream->p = stream->buf = buf;
	stream->end = stream->buf + len;
}

static __be32 *__xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p = xdr->p;
	__be32 *q = p + XDR_QUADLEN(nbytes);

        if (q > xdr->end || q < p)
		return NULL;
	xdr->p = q;
	return p;
}

static __be32 *xdr_inline_decode(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p;

	if (nbytes == 0)
		return xdr->p;
	if (xdr->p == xdr->end)
		return NULL;
	p = __xdr_inline_decode(xdr, nbytes);

	return p;
}

/*
 * name is expected to point to a buffer with a size of at least 256 bytes.
 */
static int decode_filename(struct xdr_stream *xdr, char *name, u32 *length)
{
	__be32 *p;
	u32 count;

	p = xdr_inline_decode(xdr, 4);
	if (!p)
		goto out_overflow;
	count = ntoh32(net_read_uint32(p));
	if (count > 255)
		goto out_nametoolong;
	p = xdr_inline_decode(xdr, count);
	if (!p)
		goto out_overflow;
	memcpy(name, p, count);
	name[count] = 0;
	*length = count;
	return 0;

out_nametoolong:
	printf("%s: returned a too long filename: %u\n", __func__, count);
	return -ENAMETOOLONG;

out_overflow:
	printf("%s: premature end of packet\n", __func__);
	return -EIO;
}

/*
 * rpc_add_credentials - Add RPC authentication/verifier entries
 */
static uint32_t *rpc_add_credentials(uint32_t *p)
{
	/*
	 * *BSD refuses AUTH_NONE, so use AUTH_UNIX. An empty hostname is OK for
	 * both Linux and *BSD.
	 */

	/* Provide an AUTH_UNIX credential.  */
	*p++ = hton32(1);		/* AUTH_UNIX */
	*p++ = hton32(20);		/* auth length: 20 + strlen(hostname) */
	*p++ = hton32(0);		/* stamp */
	*p++ = hton32(0);		/* hostname string length */
	/* memcpy(p, "", 0); p += 0; <- empty host name */

	*p++ = 0;			/* uid */
	*p++ = 0;			/* gid */
	*p++ = 0;			/* auxiliary gid list */

	/* Provide an AUTH_NONE verifier.  */
	*p++ = 0;			/* AUTH_NONE */
	*p++ = 0;			/* auth length */

	return p;
}

static int rpc_check_reply(unsigned char *pkt,
		int rpc_prog, uint32_t rpc_id, int *nfserr)
{
	uint32_t *data;
	struct rpc_reply rpc;

	*nfserr = 0;

	if (!pkt)
		return -EAGAIN;

	memcpy(&rpc, pkt, sizeof(rpc));

	if (ntoh32(rpc.id) != rpc_id) {
		if (rpc_id - ntoh32(rpc.id) == 1)
			/* stale packet, wait a bit longer */
			return 0;

		return -EINVAL;
	}

	if (rpc.rstatus  ||
	    rpc.verifier ||
	    rpc.astatus ) {
		return -EINVAL;
	}

	if (rpc_prog == PROG_PORTMAP)
		return 0;

	data = (uint32_t *)(pkt + sizeof(struct rpc_reply));
	*nfserr = ntoh32(net_read_uint32(data));
	*nfserr = -*nfserr;

	debug("%s: state: %d, err %d\n", __func__, nfs_state, *nfserr);

	return 0;
}

/*
 * rpc_req - synchronous RPC request
 */
static int rpc_req(struct nfs_priv *npriv, int rpc_prog, int rpc_proc,
		uint32_t *data, int datalen)
{
	struct rpc_call pkt;
	unsigned short dport;
	int ret;
	unsigned char *payload = net_udp_get_payload(npriv->con);
	int nfserr;
	int tries = 0;

	npriv->rpc_id++;

	pkt.id = hton32(npriv->rpc_id);
	pkt.type = hton32(MSG_CALL);
	pkt.rpcvers = hton32(2);	/* use RPC version 2 */
	pkt.prog = hton32(rpc_prog);
	pkt.proc = hton32(rpc_proc);

	debug("%s: prog: %d, proc: %d\n", __func__, rpc_prog, rpc_proc);

	if (rpc_prog == PROG_PORTMAP) {
		dport = SUNRPC_PORT;
		pkt.vers = hton32(2);
	} else if (rpc_prog == PROG_MOUNT) {
		dport = npriv->mount_port;
		pkt.vers = hton32(3);
	} else {
		dport = npriv->nfs_port;
		pkt.vers = hton32(3);
	}

	memcpy(payload, &pkt, sizeof(pkt));
	memcpy(payload + sizeof(pkt), data, datalen * sizeof(uint32_t));

	npriv->con->udp->uh_dport = hton16(dport);

again:
	ret = net_udp_send(npriv->con,
			sizeof(pkt) + datalen * sizeof(uint32_t));

	nfs_timer_start = get_time_ns();

	nfs_state = STATE_START;
	nfs_packet = NULL;

	while (nfs_state != STATE_DONE) {
		if (ctrlc()) {
			ret = -EINTR;
			break;
		}
		net_poll();

		if (is_timeout(nfs_timer_start, NFS_TIMEOUT)) {
			tries++;
			if (tries == NFS_MAX_RESEND)
				return -ETIMEDOUT;
			goto again;
		}

		ret = rpc_check_reply(nfs_packet, rpc_prog,
				npriv->rpc_id, &nfserr);
		if (!ret) {
			ret = nfserr;
			break;
		}
	}

	return ret;
}

/*
 * rpc_lookup_req - Lookup RPC Port numbers
 */
static int rpc_lookup_req(struct nfs_priv *npriv, uint32_t prog, uint32_t ver)
{
	uint32_t data[16];
	int ret;
	uint32_t port;

	data[0] = 0; data[1] = 0;	/* auth credential */
	data[2] = 0; data[3] = 0;	/* auth verifier */
	data[4] = hton32(prog);
	data[5] = hton32(ver);
	data[6] = hton32(17);	/* IP_UDP */
	data[7] = 0;

	ret = rpc_req(npriv, PROG_PORTMAP, PORTMAP_GETPORT, data, 8);
	if (ret)
		return ret;

	port = ntoh32(net_read_uint32(nfs_packet + sizeof(struct rpc_reply)));
	return port;
}

static uint32_t *nfs_add_uint32(uint32_t *p, uint32_t val)
{
	*p++ = hton32(val);
	return p;
}

static uint32_t *nfs_add_uint64(uint32_t *p, uint64_t val)
{
	uint64_t nval = hton64(val);

	memcpy(p, &nval, 8);
	return p + 2;
}

static uint32_t *nfs_add_fh3(uint32_t *p, unsigned fh_len, const char *fh)
{
	*p++ = hton32(fh_len);

	/* zero padding */
	if (fh_len & 3)
		p[fh_len / 4] = 0;

	memcpy(p, fh, fh_len);
	p += DIV_ROUND_UP(fh_len, 4);
	return p;
}

static uint32_t *nfs_add_filename(uint32_t *p,
		uint32_t filename_len, const char *filename)
{
	*p++ = hton32(filename_len);

	/* zero padding */
	if (filename_len & 3)
		p[filename_len / 4] = 0;

	memcpy(p, filename, filename_len);
	p += DIV_ROUND_UP(filename_len, 4);
	return p;
}

/* This is a 1:1 mapping for Linux, the compiler optimizes it out */
static const struct {
	uint32_t nfsmode;
	unsigned short statmode;
} nfs3_mode_bits[] = {
	{ 0x00001, S_IXOTH },
	{ 0x00002, S_IWOTH },
	{ 0x00004, S_IROTH },
	{ 0x00008, S_IXGRP },
	{ 0x00010, S_IWGRP },
	{ 0x00020, S_IRGRP },
	{ 0x00040, S_IXUSR },
	{ 0x00080, S_IWUSR },
	{ 0x00100, S_IRUSR },
	{ 0x00200, S_ISVTX },
	{ 0x00400, S_ISGID },
	{ 0x00800, S_ISUID },
};

static int nfs_fattr3_to_stat(uint32_t *p, struct stat *s)
{
	uint32_t mode;
	size_t i;

	/* offsetof(struct fattr3, type) = 0 */
	switch (ntoh32(net_read_uint32(p + 0))) {
	case NF3REG:
		s->st_mode = S_IFREG;
		break;
	case NF3DIR:
		s->st_mode = S_IFDIR;
		break;
	case NF3BLK:
		s->st_mode = S_IFBLK;
		break;
	case NF3CHR:
		s->st_mode = S_IFCHR;
		break;
	case NF3LNK:
		s->st_mode = S_IFLNK;
		break;
	case NF3SOCK:
		s->st_mode = S_IFSOCK;
		break;
	case NF3FIFO:
		s->st_mode = S_IFIFO;
		break;
	default:
		printf("%s: invalid mode %x\n",
				__func__, ntoh32(net_read_uint32(p + 0)));
		return -EIO;
	}

	/* offsetof(struct fattr3, mode) = 4 */
	mode = ntoh32(net_read_uint32(p + 1));
	for (i = 0; i < ARRAY_SIZE(nfs3_mode_bits); ++i) {
		if (mode & nfs3_mode_bits[i].nfsmode)
			s->st_mode |= nfs3_mode_bits[i].statmode;
	}

	/* offsetof(struct fattr3, size) = 20 */
	s->st_size = ntoh64(net_read_uint64(p + 5));

	return 0;
}

static uint32_t *nfs_read_post_op_attr(uint32_t *p, struct stat **s)
{
	struct stat dummy;
	/*
	 * union post_op_attr switch (bool attributes_follow) {
	 * case TRUE:
	 * 	fattr3 attributes;
	 * case FALSE:
	 * 	void;
	 * };
	 */

	if (ntoh32(net_read_uint32(p++))) {
		nfs_fattr3_to_stat(p, s ? *s : &dummy);
		p += 21;
	} else if (s) {
		/* no attributes available */
		*s = NULL;
	}

	return p;
}

/*
 * nfs_mount_req - Mount an NFS Filesystem
 */
static int nfs_mount_req(struct nfs_priv *npriv)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int pathlen;
	int ret;

	pathlen = strlen(npriv->path);

	debug("%s: %s\n", __func__, npriv->path);

	p = &(data[0]);
	p = rpc_add_credentials(p);

	*p++ = hton32(pathlen);
	if (pathlen & 3)
		*(p + pathlen / 4) = 0;

	memcpy (p, npriv->path, pathlen);
	p += (pathlen + 3) / 4;

	len = p - &(data[0]);

	ret = rpc_req(npriv, PROG_MOUNT, MOUNT_ADDENTRY, data, len);
	if (ret)
		return ret;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;

	npriv->rootfh_len = ntoh32(net_read_uint32(p++));
	if (npriv->rootfh_len > NFS3_FHSIZE) {
		printf("%s: file handle too big: %lu\n", __func__,
				(unsigned long)npriv->rootfh_len);
		return -EIO;
	}
	memcpy(npriv->rootfh, p, npriv->rootfh_len);
	p += DIV_ROUND_UP(npriv->rootfh_len, 4);

	return 0;
}

/*
 * nfs_umountall_req - Unmount all our NFS Filesystems on the Server
 */
static void nfs_umount_req(struct nfs_priv *npriv)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int pathlen;

	pathlen = strlen(npriv->path);

	p = &(data[0]);
	p = rpc_add_credentials(p);

	p = nfs_add_filename(p, pathlen, npriv->path);

	len = p - &(data[0]);

	rpc_req(npriv, PROG_MOUNT, MOUNT_UMOUNT, data, len);
}

/*
 * nfs_lookup_req - Lookup Pathname
 *
 * *s is set to NULL if LOOKUP3resok doesn't contain obj_attributes.
 */
static int nfs_lookup_req(struct file_priv *priv,
		uint32_t filename_len, const char *filename, struct stat **s)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int ret;

	/*
	 * struct LOOKUP3args {
	 * 	diropargs3 what;
	 * };
	 *
	 * struct LOOKUP3resok {
	 * 	nfs_fh3 object;
	 * 	post_op_attr obj_attributes;
	 * 	post_op_attr dir_attributes;
	 * };
	 *
	 * struct LOOKUP3resfail {
	 * 	post_op_attr dir_attributes;
	 * };
	 *
	 * union LOOKUP3res switch (nfsstat3 status) {
	 * case NFS3_OK:
	 * 	LOOKUP3resok resok;
	 * default:
	 * 	LOOKUP3resfail resfail;
	 * };
	 */

	p = &(data[0]);
	p = rpc_add_credentials(p);

	/* what.dir */
	p = nfs_add_fh3(p, priv->filefh_len, priv->filefh);

	/* what.name */
	p = nfs_add_filename(p, filename_len, filename);

	len = p - &(data[0]);

	ret = rpc_req(priv->npriv, PROG_NFS, NFSPROC3_LOOKUP, data, len);
	if (ret)
		return ret;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;

	priv->filefh_len = ntoh32(net_read_uint32(p++));
	if (priv->filefh_len > NFS3_FHSIZE) {
		debug("%s: file handle too big: %lu\n", __func__,
				(unsigned long)priv->filefh_len);
		return -EIO;
	}
	memcpy(priv->filefh, p, priv->filefh_len);
	p += DIV_ROUND_UP(priv->filefh_len, 4);

	if (s)
		nfs_read_post_op_attr(p, s);

	return 0;
}

static int nfs_attr_req(struct file_priv *priv, struct stat *s)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int ret;

	/*
	 * struct GETATTR3args {
	 * 	nfs_fh3 object;
	 * }
	 *
	 * struct GETATTR3resok {
	 * 	fattr3 obj_attributes;
	 * };
	 *
	 * union GETATTR3res switch (nfsstat3 status) {
	 * case NFS3_OK:
	 * 	GETATTR3resok resok;
	 * default:
	 * 	void;
	 * }
	 */

	p = &(data[0]);
	p = rpc_add_credentials(p);

	/* object */
	p = nfs_add_fh3(p, priv->filefh_len, priv->filefh);

	len = p - &(data[0]);

	ret = rpc_req(priv->npriv, PROG_NFS, NFSPROC3_GETATTR, data, len);
	if (ret)
		return ret;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;

	nfs_fattr3_to_stat(p, s);

	return 0;
}

/*
 * returns with dir->stream pointing to the first entry
 * of dirlist3 res.resok.reply
 */
static void *nfs_readdirattr_req(struct file_priv *priv, struct nfs_dir *dir)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int ret;
	void *buf;

	/*
	 * struct READDIR3args {
	 * 	nfs_fh3 dir;
	 * 	cookie3 cookie;
	 * 	cookieverf3 cookieverf;
	 * 	count3 count;
	 * };
	 *
	 * struct entry3 {
	 * 	fileid3 fileid;
	 * 	filename3 name;
	 * 	cookie3 cookie;
	 * 	entry3 *nextentry;
	 * };
	 *
	 * struct dirlist3 {
	 * 	entry3 *entries;
	 * 	bool eof;
	 * };
	 *
	 * struct READDIR3resok {
	 * 	post_op_attr dir_attributes;
	 * 	cookieverf3 cookieverf;
	 * 	dirlist3 reply;
	 * };
	 *
	 * struct READDIR3resfail {
	 * 	post_op_attr dir_attributes;
	 * };
	 *
	 * union READDIR3res switch (nfsstat3 status) {
	 * case NFS3_OK:
	 * 	READDIR3resok resok;
	 * default:
	 * 	READDIR3resfail resfail;
	 * };
	 */

	p = &(data[0]);
	p = rpc_add_credentials(p);

	p = nfs_add_fh3(p, priv->filefh_len, priv->filefh);
	p = nfs_add_uint64(p, dir->cookie);

	memcpy(p, dir->cookieverf, NFS3_COOKIEVERFSIZE);
	p += NFS3_COOKIEVERFSIZE / 4;

	p = nfs_add_uint32(p, 1024); /* count */

	ret = rpc_req(priv->npriv, PROG_NFS, NFSPROC3_READDIR, data, p - data);
	if (ret)
		return NULL;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;
	p = nfs_read_post_op_attr(p, NULL);

	/* update cookieverf */
	memcpy(dir->cookieverf, p, NFS3_COOKIEVERFSIZE);
	p += NFS3_COOKIEVERFSIZE / 4;

	len = nfs_packet + nfs_len - (void *)p;
	if (!len) {
		printf("%s: huh, no payload left\n", __func__);
		return NULL;
	}

	buf = xzalloc(len);

	memcpy(buf, p, len);

	xdr_init(&dir->stream, buf, len);

	/* now xdr points to dirlist3 res.resok.reply */

	return buf;
}

/*
 * nfs_read_req - Read File on NFS Server
 */
static int nfs_read_req(struct file_priv *priv, uint64_t offset,
		uint32_t readlen)
{
	uint32_t data[1024];
	uint32_t *p;
	int len;
	int ret;
	uint32_t rlen, eof;

	/*
	 * struct READ3args {
	 * 	nfs_fh3 file;
	 * 	offset3 offset;
	 * 	count3 count;
	 * };
	 *
	 * struct READ3resok {
	 * 	post_op_attr file_attributes;
	 * 	count3 count;
	 * 	bool eof;
	 * 	opaque data<>;
	 * };
	 *
	 * struct READ3resfail {
	 * 	post_op_attr file_attributes;
	 * };
	 *
	 * union READ3res switch (nfsstat3 status) {
	 * case NFS3_OK:
	 * 	READ3resok resok;
	 * default:
	 * 	READ3resfail resfail;
	 * };
	 */
	p = &(data[0]);
	p = rpc_add_credentials(p);

	p = nfs_add_fh3(p, priv->filefh_len, priv->filefh);
	p = nfs_add_uint64(p, offset);
	p = nfs_add_uint32(p, readlen);

	len = p - &(data[0]);

	ret = rpc_req(priv->npriv, PROG_NFS, NFSPROC3_READ, data, len);
	if (ret)
		return ret;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;

	p = nfs_read_post_op_attr(p, NULL);

	rlen = ntoh32(net_read_uint32(p));

	/* skip over count */
	p += 1;

	eof = ntoh32(net_read_uint32(p));

	/*
	 * skip over eof and count embedded in the representation of data
	 * assuming it equals rlen above.
	 */
	p += 2;

	if (readlen && !rlen && !eof)
		return -EIO;

	kfifo_put(priv->fifo, (char *)p, rlen);

	return 0;
}

static void nfs_handler(void *ctx, char *packet, unsigned len)
{
	char *pkt = net_eth_to_udp_payload(packet);

	nfs_state = STATE_DONE;
	nfs_packet = pkt;
	nfs_len = len;
}

static int nfs_create(struct device_d *dev, const char *pathname, mode_t mode)
{
	return -ENOSYS;
}

static int nfs_unlink(struct device_d *dev, const char *pathname)
{
	return -ENOSYS;
}

static int nfs_mkdir(struct device_d *dev, const char *pathname)
{
	return -ENOSYS;
}

static int nfs_rmdir(struct device_d *dev, const char *pathname)
{
	return -ENOSYS;
}

static int nfs_truncate(struct device_d *dev, FILE *f, ulong size)
{
	return -ENOSYS;
}

static struct file_priv *nfs_do_open(struct device_d *dev,
		const char *filename, struct stat **s)
{
	struct file_priv *priv;
	struct nfs_priv *npriv = dev->priv;
	int ret;
	const char *tok;

	debug("%s: filename = %s\n", __func__, filename);
	priv = xzalloc(sizeof(*priv));

	priv->npriv = npriv;

	if (!*filename) {
		priv->filefh_len = npriv->rootfh_len;
		memcpy(priv->filefh, npriv->rootfh, npriv->rootfh_len);
		return priv;
	}

	filename++;

	priv->filefh_len = npriv->rootfh_len;
	memcpy(priv->filefh, npriv->rootfh, NFS3_FHSIZE);

	while (*filename) {
		size_t flen;

		tok = strchr(filename, '/');
		if (tok)
			flen = tok - filename;
		else
			flen = strlen(filename);

		ret = nfs_lookup_req(priv, flen, filename, s);
		if (ret)
			goto out;

		if (tok)
			filename += flen + 1;
		else
			break;
	}

	return priv;

out:
	free(priv);

	return ERR_PTR(ret);
}

static void nfs_do_close(struct file_priv *priv)
{
	if (priv->fifo)
		kfifo_free(priv->fifo);

	free(priv);
}

static struct file_priv *nfs_do_stat(struct device_d *dev,
		const char *filename, struct stat *s)
{
	struct file_priv *priv;
	int ret;
	struct stat **sptr = &s;

	debug("%s: filename = %s\n", __func__, filename);
	priv = nfs_do_open(dev, filename, sptr);
	if (IS_ERR(priv))
		return priv;

	if (!*sptr) {
		/*
		 * The nfs server didn't provide obj_attributes in the lookup
		 * reply, so ask for them explicitly.
		 */
		ret = nfs_attr_req(priv, s);
		if (ret) {
			nfs_do_close(priv);
			return ERR_PTR(ret);
		}
	}

	return priv;
}

static int nfs_readlink_req(struct file_priv *priv, char* buf, size_t size)
{
	uint32_t data[1024];
	uint32_t *p;
	uint32_t len;
	int ret;

	/*
	 * struct READLINK3args {
	 * 	nfs_fh3 symlink;
	 * };
	 *
	 * struct READLINK3resok {
	 * 	post_op_attr symlink_attributes;
	 * 	nfspath3 data;
	 * };
	 *
	 * struct READLINK3resfail {
	 * 	post_op_attr symlink_attributes;
	 * }
	 *
	 * union READLINK3res switch (nfsstat3 status) {
	 * case NFS3_OK:
	 * 	READLINK3resok resok;
	 * default:
	 * 	READLINK3resfail resfail;
	 * };
	 */
	p = &(data[0]);
	p = rpc_add_credentials(p);

	p = nfs_add_fh3(p, priv->filefh_len, priv->filefh);

	len = p - &(data[0]);

	ret = rpc_req(priv->npriv, PROG_NFS, NFSPROC3_READLINK, data, len);
	if (ret)
		return ret;

	p = nfs_packet + sizeof(struct rpc_reply) + 4;

	p = nfs_read_post_op_attr(p, NULL);

	len = ntoh32(net_read_uint32(p)); /* new path length */
	p++;

	if (len > size)
		return -ENOMEM;

	memcpy(buf, p, len);

	return 0;
}

static int nfs_readlink(struct device_d *dev, const char *filename,
			char *realname, size_t size)
{
	struct file_priv *priv;
	int ret;

	priv = nfs_do_open(dev, filename, NULL);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	ret = nfs_readlink_req(priv, realname, size);
	if (ret) {
		nfs_do_close(priv);
		return ret;
	}

	return 0;
}

static int nfs_open(struct device_d *dev, FILE *file, const char *filename)
{
	struct file_priv *priv;
	struct stat s;

	priv = nfs_do_stat(dev, filename, &s);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	file->priv = priv;
	file->size = s.st_size;

	priv->fifo = kfifo_alloc(1024);
	if (!priv->fifo) {
		free(priv);
		return -ENOMEM;
	}

	return 0;
}

static int nfs_close(struct device_d *dev, FILE *file)
{
	struct file_priv *priv = file->priv;

	nfs_do_close(priv);

	return 0;
}

static int nfs_write(struct device_d *_dev, FILE *file, const void *inbuf,
		size_t insize)
{
	return -ENOSYS;
}

static int nfs_read(struct device_d *dev, FILE *file, void *buf, size_t insize)
{
	struct file_priv *priv = file->priv;

	if (insize > 1024)
		insize = 1024;

	if (insize && !kfifo_len(priv->fifo)) {
		int ret = nfs_read_req(priv, file->pos, insize);
		if (ret)
			return ret;
	}

	return kfifo_get(priv->fifo, buf, insize);
}

static loff_t nfs_lseek(struct device_d *dev, FILE *file, loff_t pos)
{
	struct file_priv *priv = file->priv;

	file->pos = pos;
	kfifo_reset(priv->fifo);

	return file->pos;
}

static DIR *nfs_opendir(struct device_d *dev, const char *pathname)
{
	struct file_priv *priv;
	void *buf = NULL;
	struct nfs_dir *dir;

	priv = nfs_do_open(dev, pathname, NULL);
	if (IS_ERR(priv))
		return NULL;

	dir = xzalloc(sizeof(*dir));
	dir->priv = priv;

	/* cookie == 0 and cookieverf == 0 means start of dir */
	buf = nfs_readdirattr_req(priv, dir);
	if (!buf) {
		debug("%s: nfs_readdirattr_req failed\n", __func__);
		goto err;
	}

	return &dir->dir;

err:
	free(buf);
	free(dir);
	nfs_do_close(priv);
	return NULL;
}

static struct dirent *nfs_readdir(struct device_d *dev, DIR *dir)
{
	struct nfs_dir *ndir = container_of(dir, struct nfs_dir, dir);
	uint32_t *p;
	int ret;
	int len;
	struct xdr_stream *xdr = &ndir->stream;

again:
	p = xdr_inline_decode(xdr, 4);
	if (!p) {
		printf("%s: premature end of packet\n", __func__);
		return NULL;
	}

	if (!net_read_uint32(p)) {
		/* eof? */
		p = xdr_inline_decode(xdr, 4);
		if (!p) {
			printf("%s: premature end of packet\n", __func__);
			return NULL;
		}
		if (net_read_uint32(p))
			return NULL;

		if (!nfs_readdirattr_req(ndir->priv, ndir)) {
			printf("%s: nfs_readdirattr_req failed\n", __func__);
			return NULL;
		}

		goto again;
	}

	/* there is another entry available in the last reply */

	/* skip over fileid */
	p = xdr_inline_decode(xdr, 8);
	if (!p) {
		printf("%s: premature end of packet\n", __func__);
		return NULL;
	}

	ret = decode_filename(xdr, ndir->ent.d_name, &len);
	if (ret)
		return NULL;

	p = xdr_inline_decode(xdr, 8);
	if (!p) {
		printf("%s: premature end of packet\n", __func__);
		return NULL;
	}
	ndir->cookie = ntoh64(net_read_uint64(p));

	return &ndir->ent;
}

static int nfs_closedir(struct device_d *dev, DIR *dir)
{
	struct nfs_dir *ndir = (void *)dir;

	nfs_do_close(ndir->priv);
	free(ndir->stream.buf);
	free(ndir);

	return 0;
}

static int nfs_stat(struct device_d *dev, const char *filename, struct stat *s)
{
	struct file_priv *priv;

	priv = nfs_do_stat(dev, filename, s);
	if (IS_ERR(priv)) {
		return PTR_ERR(priv);
	} else {
		nfs_do_close(priv);
		return 0;
	}
}

static char *rootnfsopts;

static void nfs_set_rootarg(struct nfs_priv *npriv, struct fs_device_d *fsdev)
{
	char *str, *tmp;
	const char *bootargs;

	str = basprintf("root=/dev/nfs nfsroot=%pI4:%s%s%s", &npriv->server, npriv->path,
			  rootnfsopts[0] ? "," : "", rootnfsopts);

	/* forward specific mount options on demand */
	if (npriv->manual_nfs_port == 1) {
		tmp = basprintf("%s,port=%hu", str, npriv->nfs_port);
		free(str);
		str = tmp;
	}

	if (npriv->manual_mount_port == 1) {
		tmp = basprintf("%s,mountport=%hu", str, npriv->mount_port);
		free(str);
		str = tmp;
	}

	bootargs = dev_get_param(&npriv->con->edev->dev, "linux.bootargs");
	if (bootargs) {
		tmp = basprintf("%s %s", str, bootargs);
		free(str);
		str = tmp;
	}

	fsdev_set_linux_rootarg(fsdev, str);

	free(str);
}

static int nfs_probe(struct device_d *dev)
{
	struct fs_device_d *fsdev = dev_to_fs_device(dev);
	struct nfs_priv *npriv = xzalloc(sizeof(struct nfs_priv));
	char *tmp = xstrdup(fsdev->backingstore);
	char *path;
	int ret;

	dev->priv = npriv;

	debug("nfs: mount: %s\n", fsdev->backingstore);

	path = strchr(tmp, ':');
	if (!path) {
		ret = -EINVAL;
		goto err;
	}

	*path = 0;

	npriv->path = xstrdup(path + 1);

	npriv->server = resolv(tmp);

	debug("nfs: server: %s path: %s\n", tmp, npriv->path);

	npriv->con = net_udp_new(npriv->server, SUNRPC_PORT, nfs_handler, npriv);
	if (IS_ERR(npriv->con)) {
		ret = PTR_ERR(npriv->con);
		goto err1;
	}

	/* Need a priviliged source port */
	net_udp_bind(npriv->con, 1000);

	parseopt_hu(fsdev->options, "mountport", &npriv->mount_port);
	if (!npriv->mount_port) {
		ret = rpc_lookup_req(npriv, PROG_MOUNT, 3);
		if (ret < 0) {
			printf("lookup mount port failed with %d\n", ret);
			goto err2;
		}
		npriv->mount_port = ret;
	} else {
		npriv->manual_mount_port = 1;
	}
	debug("mount port: %hu\n", npriv->mount_port);

	parseopt_hu(fsdev->options, "port", &npriv->nfs_port);
	if (!npriv->nfs_port) {
		ret = rpc_lookup_req(npriv, PROG_NFS, 3);
		if (ret < 0) {
			printf("lookup nfs port failed with %d\n", ret);
			goto err2;
		}
		npriv->nfs_port = ret;
	} else {
		npriv->manual_nfs_port = 1;
	}
	debug("nfs port: %d\n", npriv->nfs_port);

	ret = nfs_mount_req(npriv);
	if (ret) {
		printf("mounting failed with %d\n", ret);
		goto err2;
	}

	nfs_set_rootarg(npriv, fsdev);

	free(tmp);

	return 0;

err2:
	net_unregister(npriv->con);
err1:
	free(npriv->path);
err:
	free(tmp);
	free(npriv);

	return ret;
}

static void nfs_remove(struct device_d *dev)
{
	struct nfs_priv *npriv = dev->priv;

	nfs_umount_req(npriv);

	net_unregister(npriv->con);
	free(npriv->path);
	free(npriv);
}

static struct fs_driver_d nfs_driver = {
	.open      = nfs_open,
	.close     = nfs_close,
	.read      = nfs_read,
	.lseek     = nfs_lseek,
	.opendir   = nfs_opendir,
	.readdir   = nfs_readdir,
	.closedir  = nfs_closedir,
	.stat      = nfs_stat,
	.create    = nfs_create,
	.unlink    = nfs_unlink,
	.mkdir     = nfs_mkdir,
	.rmdir     = nfs_rmdir,
	.write     = nfs_write,
	.truncate  = nfs_truncate,
	.readlink  = nfs_readlink,
	.flags     = 0,
	.drv = {
		.probe  = nfs_probe,
		.remove = nfs_remove,
		.name = "nfs",
	}
};

static int nfs_init(void)
{
	rootnfsopts = xstrdup("v3,tcp");

	globalvar_add_simple_string("linux.rootnfsopts", &rootnfsopts);

	return register_fs_driver(&nfs_driver);
}
coredevice_initcall(nfs_init);
