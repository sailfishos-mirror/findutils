/* fstype.c -- determine type of filesystems that files are on
   Copyright (C) 1990, 91, 92, 93, 94, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.
*/

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>. */

#include "defs.h"

#include "../gnulib/lib/dirname.h"
#include "modetype.h"
#include <errno.h>
#ifdef STDC_HEADERS
#include <stdlib.h>
#else
extern int errno;
#endif

/* Need declaration of function `xstrtoumax' */
#include "../gnulib/lib/xstrtol.h"

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define _(Text) Text
#endif
#ifdef gettext_noop
# define N_(String) gettext_noop (String)
#else
/* See locate.c for explanation as to why not use (String) */
# define N_(String) String
#endif

static char *filesystem_type_uncached PARAMS((char *path, char *relpath, struct stat *statp));

#ifdef FSTYPE_MNTENT		/* 4.3BSD, SunOS, HP-UX, Dynix, Irix.  */
#include <mntent.h>
#if !defined(MOUNTED)
# if defined(MNT_MNTTAB)	/* HP-UX.  */
#  define MOUNTED MNT_MNTTAB
# endif
# if defined(MNTTABNAME)	/* Dynix.  */
#  define MOUNTED MNTTABNAME
# endif
#endif
#endif

#ifdef FSTYPE_GETMNT		/* Ultrix.  */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/fs_types.h>
#endif

#ifdef FSTYPE_USG_STATFS	/* SVR3.  */
#include <sys/statfs.h>
#include <sys/fstyp.h>
#endif

#ifdef FSTYPE_STATVFS		/* SVR4.  */
#include <sys/statvfs.h>
#include <sys/fstyp.h>
#endif

#ifdef FSTYPE_STATFS		/* 4.4BSD.  */
#include <sys/param.h>		/* NetBSD needs this.  */
#include <sys/mount.h>

#ifndef HAVE_F_FSTYPENAME_IN_STATFS
#ifndef MFSNAMELEN		/* NetBSD defines this.  */
static char *
fstype_to_string (t)
     short t;
{
#ifdef INITMOUNTNAMES		/* Defined in 4.4BSD, not in NET/2.  */
  static char *mn[] = INITMOUNTNAMES;
  if (t >= 0 && t <= MOUNT_MAXTYPE)
    return mn[t];
  else
    return "?";
#else /* !INITMOUNTNAMES */
  switch (t)
    {
    case MOUNT_UFS:
      return "ufs";
    case MOUNT_NFS:
      return "nfs";
#ifdef MOUNT_PC
    case MOUNT_PC:
      return "pc";
#endif
#ifdef MOUNT_MFS
    case MOUNT_MFS:
      return "mfs";
#endif
#ifdef MOUNT_LO
    case MOUNT_LO:
      return "lofs";
#endif
#ifdef MOUNT_TFS
    case MOUNT_TFS:
      return "tfs";
#endif
#ifdef MOUNT_TMP
    case MOUNT_TMP:
      return "tmp";
#endif
#ifdef MOUNT_MSDOS
    case MOUNT_MSDOS:
      return "msdos";
#endif
#ifdef MOUNT_ISO9660
    case MOUNT_ISO9660:
      return "iso9660fs";
#endif
    default:
      return "?";
    }
#endif /* !INITMOUNTNAMES */
}
#endif /* !MFSNAMELEN */
#endif /* !HAVE_F_FSTYPENAME_IN_STATFS */
#endif /* FSTYPE_STATFS */

#ifdef FSTYPE_AIX_STATFS	/* AIX.  */
#include <sys/vmount.h>
#include <sys/statfs.h>

#define FSTYPE_STATFS		/* Otherwise like 4.4BSD.  */
#define f_type f_vfstype

static char *
fstype_to_string (t)
     short t;
{
  switch (t)
    {
    case MNT_AIX:
#if 0				/* NFS filesystems are actually MNT_AIX. */
      return "aix";
#endif
    case MNT_NFS:
      return "nfs";
    case MNT_JFS:
      return "jfs";
    case MNT_CDROM:
      return "cdrom";
    default:
      return "?";
    }
}
#endif /* FSTYPE_AIX_STATFS */

#ifdef AFS
#include <netinet/in.h>
#include <afs/venus.h>
#if __STDC__
/* On SunOS 4, afs/vice.h defines this to rely on a pre-ANSI cpp.  */
#undef _VICEIOCTL
#define _VICEIOCTL(id)  ((unsigned int ) _IOW('V', id, struct ViceIoctl))
#endif
#ifndef _IOW
/* AFS on Solaris 2.3 doesn't get this definition.  */
#include <sys/ioccom.h>
#endif

static int
in_afs (path)
     char *path;
{
  static char space[2048];
  struct ViceIoctl vi;

  vi.in_size = 0;
  vi.out_size = sizeof (space);
  vi.out = space;

  if (pioctl (path, VIOC_FILE_CELL_NAME, &vi, 1)
      && (errno == EINVAL || errno == ENOENT))
	return 0;
  return 1;
}
#endif /* AFS */

/* Nonzero if the current filesystem's type is known.  */
static int fstype_known = 0;

/* Return a static string naming the type of filesystem that the file PATH,
   described by STATP, is on.
   RELPATH is the file name relative to the current directory.
   Return "unknown" if its filesystem type is unknown.  */

char *
filesystem_type (char *path, char *relpath, struct stat *statp)
{
  static char *current_fstype = NULL;
  static dev_t current_dev;

  if (current_fstype != NULL)
    {
      if (fstype_known && statp->st_dev == current_dev)
	return current_fstype;	/* Cached value.  */
      free (current_fstype);
    }
  current_dev = statp->st_dev;
  current_fstype = filesystem_type_uncached (path, relpath, statp);
  return current_fstype;
}

/* Return a newly allocated string naming the type of filesystem that the
   file PATH, described by STATP, is on.
   RELPATH is the file name relative to the current directory.
   Return "unknown" if its filesystem type is unknown.  */

static char *
filesystem_type_uncached (char *path, char *relpath, struct stat *statp)
{
  char *type = NULL;

#ifdef FSTYPE_MNTENT		/* 4.3BSD, SunOS, HP-UX, Dynix, Irix.  */
  char *table = MOUNTED;
  FILE *mfp;
  struct mntent *mnt;

  mfp = setmntent (table, "r");
  if (mfp == NULL)
    error (1, errno, "%s", table);

  /* Find the entry with the same device number as STATP, and return
     that entry's fstype. */
  while (type == NULL && (mnt = getmntent (mfp)))
    {
      char *devopt;
      dev_t dev;
      struct stat disk_stats;

#ifdef MNTTYPE_IGNORE
      if (!strcmp (mnt->mnt_type, MNTTYPE_IGNORE))
	continue;
#endif

      /* Newer systems like SunOS 4.1 keep the dev number in the mtab,
	 in the options string.	 For older systems, we need to stat the
	 directory that the filesystem is mounted on to get it.

	 Unfortunately, the HPUX 9.x mnttab entries created by automountq
	 contain a dev= option but the option value does not match the
	 st_dev value of the file (maybe the lower 16 bits match?).  */

#if !defined(hpux) && !defined(__hpux__)
      devopt = strstr (mnt->mnt_opts, "dev=");
      if (devopt)
	{
	  uintmax_t u = 0;
	  devopt += 4;
	  if (devopt[0] == '0' && (devopt[1] == 'x' || devopt[1] == 'X'))
	    devopt += 2;
	  xstrtoumax (devopt, NULL, 16, &u, NULL);
	  dev = u;
	}
      else
#endif /* not hpux */
	{
	  if (stat (mnt->mnt_dir, &disk_stats) == -1) {
	    if (errno == EACCES)
	      continue;
	    else
	      error (1, errno, _("error in %s: %s"), table, mnt->mnt_dir);
	  }
	  dev = disk_stats.st_dev;
	}

      if (dev == statp->st_dev)
	type = mnt->mnt_type;
    }

  if (endmntent (mfp) == 0)
    error (0, errno, "%s", table);
#endif

#ifdef FSTYPE_GETMNT		/* Ultrix.  */
  int offset = 0;
  struct fs_data fsd;

  while (type == NULL
	 && getmnt (&offset, &fsd, sizeof (fsd), NOSTAT_MANY, 0) > 0)
    {
      if (fsd.fd_req.dev == statp->st_dev)
	type = gt_names[fsd.fd_req.fstype];
    }
#endif

#ifdef FSTYPE_USG_STATFS	/* SVR3.  */
  struct statfs fss;
  char typebuf[FSTYPSZ];

  if (statfs (relpath, &fss, sizeof (struct statfs), 0) == -1)
    {
      /* Don't die if a file was just removed. */
      if (errno != ENOENT)
	error (1, errno, "%s", path);
    }
  else if (!sysfs (GETFSTYP, fss.f_fstyp, typebuf))
    type = typebuf;
#endif

#ifdef FSTYPE_STATVFS		/* SVR4.  */
  struct statvfs fss;

  if (statvfs (relpath, &fss) == -1)
    {
      /* Don't die if a file was just removed. */
      if (errno != ENOENT)
	error (1, errno, "%s", path);
    }
  else
    type = fss.f_basetype;
#endif

#ifdef FSTYPE_STATFS		/* 4.4BSD.  */
  struct statfs fss;
  char *p;

  if (S_ISLNK (statp->st_mode))
    p = dir_name (relpath);
  else
    p = relpath;

  if (statfs (p, &fss) == -1)
    {
      /* Don't die if symlink to nonexisting file, or a file that was
	 just removed. */
      if (errno != ENOENT)
	error (1, errno, "%s", path);
    }
  else
    {
#ifdef HAVE_F_FSTYPENAME_IN_STATFS
      type = xstrdup (fss.f_fstypename);
#else
      type = fstype_to_string (fss.f_type);
#endif
    }
  if (p != relpath)
    free (p);
#endif

#ifdef AFS
  if ((!type || !strcmp (type, "xx")) && in_afs (relpath))
    type = "afs";
#endif

  /* An unknown value can be caused by an ENOENT error condition.
     Don't cache those values.  */
  fstype_known = (type != NULL);

  return xstrdup (type ? type : _("unknown"));
}
