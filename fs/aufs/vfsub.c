/*
 * Copyright (C) 2005-2009 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * sub-routines for VFS
 */

#include <linux/uaccess.h>
#include "aufs.h"

int vfsub_update_h_iattr(struct path *h_path, int *did)
{
	int err;
	struct kstat st;
	struct super_block *h_sb;

	/* for remote fs, leave work for its getattr or d_revalidate */
	/* for bad i_attr fs, handle them in aufs_getattr() */
	/* still some fs may acquire i_mutex. we need to skip them */
	err = 0;
	if (!did)
		did = &err;
	h_sb = h_path->dentry->d_sb;
	*did = (!au_test_fs_remote(h_sb) && au_test_fs_refresh_iattr(h_sb));
	if (*did)
		err = vfs_getattr(h_path->mnt, h_path->dentry, &st);

	return err;
}

/* ---------------------------------------------------------------------- */

struct file *vfsub_filp_open(const char *path, int oflags, int mode)
{
	struct file *file;

	lockdep_off();
	file = filp_open(path, oflags, mode);
	lockdep_on();
	if (IS_ERR(file))
		goto out;
	vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/

 out:
	return file;
}

int vfsub_path_lookup(const char *name, unsigned int flags,
		      struct nameidata *nd)
{
	int err;

	/* lockdep_off(); */
	err = path_lookup(name, flags, nd);
	/* lockdep_on(); */
	if (!err && nd->path.dentry->d_inode)
		vfsub_update_h_iattr(&nd->path, /*did*/NULL); /*ignore*/
	return err;
}

struct dentry *vfsub_lookup_one_len(const char *name, struct dentry *parent,
				    int len)
{
	struct path path = {
		.mnt = NULL
	};

	IMustLock(parent->d_inode);

	path.dentry = lookup_one_len(name, parent, len);
	if (IS_ERR(path.dentry))
		goto out;
	if (path.dentry->d_inode)
		vfsub_update_h_iattr(&path, /*did*/NULL); /*ignore*/

 out:
	return path.dentry;
}

struct dentry *vfsub_lookup_hash(struct nameidata *nd)
{
	struct path path = {
		.mnt = nd->path.mnt
	};

	IMustLock(nd->path.dentry->d_inode);

	path.dentry = lookup_hash(nd);
	if (!IS_ERR(path.dentry) && path.dentry->d_inode)
		vfsub_update_h_iattr(&path, /*did*/NULL); /*ignore*/

	return path.dentry;
}

/* ---------------------------------------------------------------------- */

struct dentry *vfsub_lock_rename(struct dentry *d1, struct au_hinode *hdir1,
				 struct dentry *d2, struct au_hinode *hdir2)
{
	struct dentry *d;

	lockdep_off();
	d = lock_rename(d1, d2);
	lockdep_on();
	au_hin_suspend(hdir1);
	if (hdir1 != hdir2)
		au_hin_suspend(hdir2);

	return d;
}

void vfsub_unlock_rename(struct dentry *d1, struct au_hinode *hdir1,
			 struct dentry *d2, struct au_hinode *hdir2)
{
	au_hin_resume(hdir1);
	if (hdir1 != hdir2)
		au_hin_resume(hdir2);
	lockdep_off();
	unlock_rename(d1, d2);
	lockdep_on();
}

/* ---------------------------------------------------------------------- */

int vfsub_create(struct inode *dir, struct path *path, int mode)
{
	int err;

	IMustLock(dir);

	if (au_test_fs_null_nd(dir->i_sb))
		err = vfs_create(dir, path->dentry, mode, NULL);
	else {
		struct nameidata h_nd;

		memset(&h_nd, 0, sizeof(h_nd));
		h_nd.flags = LOOKUP_CREATE;
		h_nd.intent.open.flags = O_CREAT | FMODE_READ;
		h_nd.intent.open.create_mode = mode;
		h_nd.path.dentry = path->dentry->d_parent;
		h_nd.path.mnt = path->mnt;
		path_get(&h_nd.path);
		err = vfs_create(dir, path->dentry, mode, &h_nd);
		path_put(&h_nd.path);
	}

	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}

	return err;
}

int vfsub_symlink(struct inode *dir, struct path *path, const char *symname)
{
	int err;

	IMustLock(dir);

	err = vfs_symlink(dir, path->dentry, symname);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}
	return err;
}

int vfsub_mknod(struct inode *dir, struct path *path, int mode, dev_t dev)
{
	int err;

	IMustLock(dir);

	err = vfs_mknod(dir, path->dentry, mode, dev);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}
	return err;
}

int vfsub_link(struct dentry *src_dentry, struct inode *dir, struct path *path)
{
	int err;

	IMustLock(dir);

	lockdep_off();
	err = vfs_link(src_dentry, dir, path->dentry);
	lockdep_on();
	if (!err) {
		struct path tmp = *path;
		int did;

		/* fuse has different memory inode for the same inumber */
		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
			tmp.dentry = src_dentry;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}
	return err;
}

int vfsub_rename(struct inode *src_dir, struct dentry *src_dentry,
		 struct inode *dir, struct path *path)
{
	int err;
	struct path tmp = {
		.dentry	= path->dentry->d_parent,
		.mnt	= path->mnt
	};

	IMustLock(dir);
	IMustLock(src_dir);

	lockdep_off();
	err = vfs_rename(src_dir, src_dentry, dir, path->dentry);
	lockdep_on();
	if (!err) {
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = src_dentry;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
			tmp.dentry = src_dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}
	return err;
}

int vfsub_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err;

	IMustLock(dir);

	err = vfs_mkdir(dir, path->dentry, mode);
	if (!err) {
		struct path tmp = *path;
		int did;

		vfsub_update_h_iattr(&tmp, &did);
		if (did) {
			tmp.dentry = path->dentry->d_parent;
			vfsub_update_h_iattr(&tmp, /*did*/NULL);
		}
		/*ignore*/
	}
	return err;
}

int vfsub_rmdir(struct inode *dir, struct path *path)
{
	int err;

	IMustLock(dir);

	lockdep_off();
	err = vfs_rmdir(dir, path->dentry);
	lockdep_on();
	if (!err) {
		struct path tmp = {
			.dentry	= path->dentry->d_parent,
			.mnt	= path->mnt
		};

		vfsub_update_h_iattr(&tmp, /*did*/NULL); /*ignore*/
	}

	return err;
}

/* ---------------------------------------------------------------------- */

ssize_t vfsub_read_u(struct file *file, char __user *ubuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;

	err = vfs_read(file, ubuf, count, ppos);
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

/* todo: kernel_read()? */
ssize_t vfsub_read_k(struct file *file, void *kbuf, size_t count,
		     loff_t *ppos)
{
	ssize_t err;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = vfsub_read_u(file, (char __user *)kbuf, count, ppos);
	set_fs(oldfs);
	return err;
}

ssize_t vfsub_write_u(struct file *file, const char __user *ubuf, size_t count,
		      loff_t *ppos)
{
	ssize_t err;

	lockdep_off();
	err = vfs_write(file, ubuf, count, ppos);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

ssize_t vfsub_write_k(struct file *file, void *kbuf, size_t count, loff_t *ppos)
{
	ssize_t err;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = vfsub_write_u(file, (const char __user *)kbuf, count, ppos);
	set_fs(oldfs);
	return err;
}

int vfsub_readdir(struct file *file, filldir_t filldir, void *arg)
{
	int err;

	lockdep_off();
	err = vfs_readdir(file, filldir, arg);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&file->f_path, /*did*/NULL); /*ignore*/
	return err;
}

long vfsub_splice_to(struct file *in, loff_t *ppos,
		     struct pipe_inode_info *pipe, size_t len,
		     unsigned int flags)
{
	long err;

	lockdep_off();
	err = do_splice_to(in, ppos, pipe, len, flags);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&in->f_path, /*did*/NULL); /*ignore*/
	return err;
}

long vfsub_splice_from(struct pipe_inode_info *pipe, struct file *out,
		       loff_t *ppos, size_t len, unsigned int flags)
{
	long err;

	lockdep_off();
	err = do_splice_from(pipe, out, ppos, len, flags);
	lockdep_on();
	if (err >= 0)
		vfsub_update_h_iattr(&out->f_path, /*did*/NULL); /*ignore*/
	return err;
}

/* ---------------------------------------------------------------------- */

struct au_vfsub_mkdir_args {
	int *errp;
	struct inode *dir;
	struct path *path;
	int mode;
};

static void au_call_vfsub_mkdir(void *args)
{
	struct au_vfsub_mkdir_args *a = args;
	*a->errp = vfsub_mkdir(a->dir, a->path, a->mode);
}

int vfsub_sio_mkdir(struct inode *dir, struct path *path, int mode)
{
	int err, do_sio, wkq_err;

	do_sio = au_test_h_perm_sio(dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio)
		err = vfsub_mkdir(dir, path, mode);
	else {
		struct au_vfsub_mkdir_args args = {
			.errp	= &err,
			.dir	= dir,
			.path	= path,
			.mode	= mode
		};
		wkq_err = au_wkq_wait(au_call_vfsub_mkdir, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

struct au_vfsub_rmdir_args {
	int *errp;
	struct inode *dir;
	struct path *path;
};

static void au_call_vfsub_rmdir(void *args)
{
	struct au_vfsub_rmdir_args *a = args;
	*a->errp = vfsub_rmdir(a->dir, a->path);
}

int vfsub_sio_rmdir(struct inode *dir, struct path *path)
{
	int err, do_sio, wkq_err;

	do_sio = au_test_h_perm_sio(dir, MAY_EXEC | MAY_WRITE);
	if (!do_sio)
		err = vfsub_rmdir(dir, path);
	else {
		struct au_vfsub_rmdir_args args = {
			.errp	= &err,
			.dir	= dir,
			.path	= path
		};
		wkq_err = au_wkq_wait(au_call_vfsub_rmdir, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

struct notify_change_args {
	int *errp;
	struct path *path;
	struct iattr *ia;
};

static void call_notify_change(void *args)
{
	struct notify_change_args *a = args;
	struct inode *h_inode;

	h_inode = a->path->dentry->d_inode;
	IMustLock(h_inode);

	*a->errp = -EPERM;
	if (!IS_IMMUTABLE(h_inode) && !IS_APPEND(h_inode)) {
		lockdep_off();
		*a->errp = notify_change(a->path->dentry, a->ia);
		lockdep_on();
		if (!*a->errp)
			vfsub_update_h_iattr(a->path, /*did*/NULL); /*ignore*/
	}
	AuTraceErr(*a->errp);
}

int vfsub_notify_change(struct path *path, struct iattr *ia)
{
	int err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	call_notify_change(&args);

	return err;
}

int vfsub_sio_notify_change(struct path *path, struct iattr *ia)
{
	int err, wkq_err;
	struct notify_change_args args = {
		.errp	= &err,
		.path	= path,
		.ia	= ia
	};

	wkq_err = au_wkq_wait(call_notify_change, &args);
	if (unlikely(wkq_err))
		err = wkq_err;

	return err;
}

/* ---------------------------------------------------------------------- */

struct unlink_args {
	int *errp;
	struct inode *dir;
	struct path *path;
};

static void call_unlink(void *args)
{
	struct unlink_args *a = args;
	struct dentry *d = a->path->dentry;
	struct inode *h_inode;
	const int stop_sillyrename = (au_test_nfs(d->d_sb)
				      && atomic_read(&d->d_count) == 1);

	IMustLock(a->dir);

	if (!stop_sillyrename)
		dget(d);
	h_inode = d->d_inode;
	if (h_inode)
		atomic_inc(&h_inode->i_count);

	lockdep_off();
	*a->errp = vfs_unlink(a->dir, d);
	lockdep_on();
	if (!*a->errp) {
		struct path tmp = {
			.dentry = d->d_parent,
			.mnt	= a->path->mnt
		};
		vfsub_update_h_iattr(&tmp, /*did*/NULL); /*ignore*/
	}

	if (!stop_sillyrename)
		dput(d);
	if (h_inode)
		iput(h_inode);

	AuTraceErr(*a->errp);
}

/*
 * @dir: must be locked.
 * @dentry: target dentry.
 */
int vfsub_unlink(struct inode *dir, struct path *path, int force)
{
	int err;
	struct unlink_args args = {
		.errp	= &err,
		.dir	= dir,
		.path	= path
	};

	if (!force)
		call_unlink(&args);
	else {
		int wkq_err;

		wkq_err = au_wkq_wait(call_unlink, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}

	return err;
}