/*
 * Copyright (C) 2005-2009 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * super_block operations
 */

#ifndef __AUFS_SUPER_H__
#define __AUFS_SUPER_H__

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/aufs_type.h>
#include "rwsem.h"
#include "spl.h"
#include "wkq.h"

typedef ssize_t (*au_readf_t)(struct file *, char __user *, size_t, loff_t *);
typedef ssize_t (*au_writef_t)(struct file *, const char __user *, size_t,
			       loff_t *);

/* policies to select one among multiple writable branches */
struct au_wbr_copyup_operations {
	int (*copyup)(struct dentry *dentry);
};

struct au_wbr_create_operations {
	int (*create)(struct dentry *dentry, int isdir);
	int (*init)(struct super_block *sb);
	int (*fin)(struct super_block *sb);
};

struct au_wbr_mfs {
	struct mutex	mfs_lock; /* protect this structure */
	unsigned long	mfs_jiffy;
	unsigned long	mfs_expire;
	aufs_bindex_t	mfs_bindex;

	unsigned long long	mfsrr_bytes;
	unsigned long long	mfsrr_watermark;
};

/* sbinfo status flags */
/*
 * set true when refresh_dirs() failed at remount time.
 * then try refreshing dirs at access time again.
 * if it is false, refreshing dirs at access time is unnecesary
 */
#define AuSi_FAILED_REFRESH_DIRS	1
#define AuSi_MAINTAIN_PLINK		(1 << 1)	/* ioctl */
#define au_ftest_si(sbinfo, name)	((sbinfo)->au_si_status & AuSi_##name)
#define au_fset_si(sbinfo, name) \
	{ (sbinfo)->au_si_status |= AuSi_##name; }
#define au_fclr_si(sbinfo, name) \
	{ (sbinfo)->au_si_status &= ~AuSi_##name; }

struct au_branch;
struct au_sbinfo {
	/* nowait tasks in the system-wide workqueue */
	struct au_nowait_tasks	si_nowait;

	struct rw_semaphore	si_rwsem;

	/* branch management */
	unsigned int		si_generation;

	/* see above flags */
	unsigned char		au_si_status;

	aufs_bindex_t		si_bend;
	aufs_bindex_t		si_last_br_id;
	struct au_branch	**si_branch;

	/* policy to select a writable branch */
	unsigned char		si_wbr_copyup;
	unsigned char		si_wbr_create;
	struct au_wbr_copyup_operations *si_wbr_copyup_ops;
	struct au_wbr_create_operations *si_wbr_create_ops;

	/* round robin */
	atomic_t		si_wbr_rr_next;

	/* most free space */
	struct au_wbr_mfs	si_wbr_mfs;

	/* mount flags */
	/* include/asm-ia64/siginfo.h defines a macro named si_flags */
	unsigned int		si_mntflags;

	/* external inode number (bitmap and translation table) */
	au_readf_t		si_xread;
	au_writef_t		si_xwrite;
	struct file		*si_xib;
	struct mutex		si_xib_mtx; /* protect xib members */
	unsigned long		*si_xib_buf;
	unsigned long		si_xib_last_pindex;
	int			si_xib_next_bit;
	aufs_bindex_t		si_xino_brid;
	/* reserved for future use */
	/* unsigned long long	si_xib_limit; */	/* Max xib file size */

	/* readdir cache time, max, in HZ */
	unsigned long		si_rdcache;

	/*
	 * If the number of whiteouts are larger than si_dirwh, leave all of
	 * them after au_whtmp_ren to reduce the cost of rmdir(2).
	 * future fsck.aufs or kernel thread will remove them later.
	 * Otherwise, remove all whiteouts and the dir in rmdir(2).
	 */
	unsigned int		si_dirwh;

	/*
	 * rename(2) a directory with all children.
	 */
	/* reserved for future use */
	/* int			si_rendir; */

	/* pseudo_link list */
	struct au_splhead	si_plink;
	wait_queue_head_t	si_plink_wq;

	/*
	 * sysfs and lifetime management.
	 * this is not a small structure and it may be a waste of memory in case
	 * of sysfs is disabled, particulary when many aufs-es are mounted.
	 * but using sysfs is majority.
	 */
	struct kobject		si_kobj;

	/* dirty, necessary for unmounting, sysfs and sysrq */
	struct super_block	*si_sb;
};

/* ---------------------------------------------------------------------- */

/* policy to select one among writable branches */
#define AuWbrCopyup(sbinfo, args...) \
	((sbinfo)->si_wbr_copyup_ops->copyup(args))
#define AuWbrCreate(sbinfo, args...) \
	((sbinfo)->si_wbr_create_ops->create(args))

/* flags for si_read_lock()/aufs_read_lock()/di_read_lock() */
#define AuLock_DW		1		/* write-lock dentry */
#define AuLock_IR		(1 << 1)	/* read-lock inode */
#define AuLock_IW		(1 << 2)	/* write-lock inode */
#define AuLock_FLUSH		(1 << 3)	/* wait for 'nowait' tasks */
#define AuLock_DIR		(1 << 4)	/* target is a dir */
#define au_ftest_lock(flags, name)	((flags) & AuLock_##name)
#define au_fset_lock(flags, name)	{ (flags) |= AuLock_##name; }
#define au_fclr_lock(flags, name)	{ (flags) &= ~AuLock_##name; }

/* ---------------------------------------------------------------------- */

/* super.c */
extern struct file_system_type aufs_fs_type;
struct inode *au_iget_locked(struct super_block *sb, ino_t ino);

/* sbinfo.c */
void au_si_free(struct kobject *kobj);
int au_si_alloc(struct super_block *sb);
int au_sbr_realloc(struct au_sbinfo *sbinfo, int nbr);

unsigned int au_sigen_inc(struct super_block *sb);
aufs_bindex_t au_new_br_id(struct super_block *sb);

void aufs_read_lock(struct dentry *dentry, int flags);
void aufs_read_unlock(struct dentry *dentry, int flags);
void aufs_write_lock(struct dentry *dentry);
void aufs_write_unlock(struct dentry *dentry);
void aufs_read_and_write_lock2(struct dentry *d1, struct dentry *d2, int isdir);
void aufs_read_and_write_unlock2(struct dentry *d1, struct dentry *d2);

/* wbr_policy.c */
extern struct au_wbr_copyup_operations au_wbr_copyup_ops[];
extern struct au_wbr_create_operations au_wbr_create_ops[];
int au_cpdown_dirs(struct dentry *dentry, aufs_bindex_t bdst);

/* ---------------------------------------------------------------------- */

static inline struct au_sbinfo *au_sbi(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* ---------------------------------------------------------------------- */

/* lock superblock. mainly for entry point functions */
/*
 * si_noflush_read_lock, si_noflush_write_lock,
 * si_read_unlock, si_write_unlock, si_downgrade_lock
 */
AuSimpleLockRwsemFuncs(si_noflush, struct super_block *sb,
		       &au_sbi(sb)->si_rwsem);
AuSimpleUnlockRwsemFuncs(si, struct super_block *sb, &au_sbi(sb)->si_rwsem);

static inline void si_read_lock(struct super_block *sb, int flags)
{
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);
	si_noflush_read_lock(sb);
}

static inline void si_write_lock(struct super_block *sb)
{
	au_nwt_flush(&au_sbi(sb)->si_nowait);
	si_noflush_write_lock(sb);
}

static inline int si_read_trylock(struct super_block *sb, int flags)
{
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);
	return si_noflush_read_trylock(sb);
}

static inline int si_write_trylock(struct super_block *sb, int flags)
{
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);
	return si_noflush_write_trylock(sb);
}

/* ---------------------------------------------------------------------- */

static inline aufs_bindex_t au_sbend(struct super_block *sb)
{
	return au_sbi(sb)->si_bend;
}

static inline unsigned int au_mntflags(struct super_block *sb)
{
	return au_sbi(sb)->si_mntflags;
}

static inline unsigned int au_sigen(struct super_block *sb)
{
	return au_sbi(sb)->si_generation;
}

static inline struct au_branch *au_sbr(struct super_block *sb,
				       aufs_bindex_t bindex)
{
	return au_sbi(sb)->si_branch[0 + bindex];
}

static inline void au_xino_brid_set(struct super_block *sb, aufs_bindex_t brid)
{
	au_sbi(sb)->si_xino_brid = brid;
}

static inline aufs_bindex_t au_xino_brid(struct super_block *sb)
{
	return au_sbi(sb)->si_xino_brid;
}

#endif /* __KERNEL__ */
#endif /* __AUFS_SUPER_H__ */