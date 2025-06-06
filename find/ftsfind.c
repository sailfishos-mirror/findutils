/* find -- search for files in a directory hierarchy (fts version)
   Copyright (C) 1990-2025 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/* This file was written by James Youngman, based on oldfind.c.

   GNU find was written by Eric Decker <cire@soe.ucsc.edu>,
   with enhancements by David MacKenzie <djm@gnu.org>,
   Jay Plett <jay@silence.princeton.nj.us>,
   and Tim Wood <axolotl!tim@toad.com>.
   The idea for -print0 and xargs -0 came from
   Dan Bernstein <brnstnd@kramden.acf.nyu.edu>.
*/

/* config.h must always be included first. */
#include <config.h>


/* system headers. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

/* gnulib headers. */
#include "argv-iter.h"
#include "cloexec.h"
#include "closeout.h"
#include "fts_.h"
#include "intprops.h"
#include "progname.h"
#include "quotearg.h"
#include "same-inode.h"
#include "save-cwd.h"
#include "xgetcwd.h"
#include "xalloc.h"

/* find headers. */
#include "defs.h"
#include "dircallback.h"
#include "fdleak.h"
#include "unused-result.h"
#include "system.h"


#undef  STAT_MOUNTPOINTS


/* FTS_TIGHT_CYCLE_CHECK tries to work around Savannah bug #17877
 * (but actually using it doesn't fix the bug).
 */
static int ftsoptions = FTS_NOSTAT|FTS_TIGHT_CYCLE_CHECK|FTS_CWDFD|FTS_VERBATIM;

static int prev_depth = INT_MIN; /* fts_level can be < 0 */
static int curr_fd = -1;


static bool find (char *arg) __attribute_warn_unused_result__;
static bool process_all_startpoints (int argc, char *argv[]) __attribute_warn_unused_result__;



static void
left_dir (void)
{
  if (ftsoptions & FTS_CWDFD)
    {
      if (curr_fd >= 0)
        {
          close (curr_fd);
          curr_fd = -1;
        }
    }
  else
    {
      /* do nothing. */
    }
}

/*
 * Signal that we are now inside a directory pointed to by dir_fd.
 * The caller can't tell if this is the first time this happens, so
 * we have to be careful not to call dup() more than once
 */
static void
inside_dir (int dir_fd)
{
  if (ftsoptions & FTS_CWDFD)
    {
      assert (dir_fd == AT_FDCWD || dir_fd >= 0);

      state.cwd_dir_fd = dir_fd;
      if (curr_fd < 0)
        {
          if (AT_FDCWD == dir_fd)
            {
              curr_fd = AT_FDCWD;
            }
          else if (dir_fd >= 0)
            {
              curr_fd = dup_cloexec (dir_fd);
            }
          else
            {
              /* curr_fd is invalid, but dir_fd is also invalid.
               * This should not have happened.
               */
              assert (curr_fd >= 0 || dir_fd >= 0);
            }
        }
    }
  else
    {
      /* FTS_CWDFD is not in use.  We can always assume that
       * AT_FDCWD refers to the directory we are currently searching.
       *
       * Therefore there is nothing to do.
       */
    }
}



#ifdef STAT_MOUNTPOINTS
static void init_mounted_dev_list (void);
#endif

#define HANDLECASE(N) case N: return #N;

static const char *
get_fts_info_name (int info)
{
  static char buf[1 + INT_BUFSIZE_BOUND (info) + 1];
  switch (info)
    {
      HANDLECASE(FTS_D);
      HANDLECASE(FTS_DC);
      HANDLECASE(FTS_DEFAULT);
      HANDLECASE(FTS_DNR);
      HANDLECASE(FTS_DOT);
      HANDLECASE(FTS_DP);
      HANDLECASE(FTS_ERR);
      HANDLECASE(FTS_F);
      HANDLECASE(FTS_INIT);
      HANDLECASE(FTS_NS);
      HANDLECASE(FTS_NSOK);
      HANDLECASE(FTS_SL);
      HANDLECASE(FTS_SLNONE);
      HANDLECASE(FTS_W);
    default:
      sprintf (buf, "[%d]", info);
      return buf;
    }
}

static void
visit (FTS *p, FTSENT *ent, struct stat *pstat)
{
  struct predicate *eval_tree;

  state.have_stat = (ent->fts_info != FTS_NS) && (ent->fts_info != FTS_NSOK);
  state.rel_pathname = ent->fts_accpath;
  state.cwd_dir_fd   = p->fts_cwd_fd;

  /* Apply the predicates to this path. */
  eval_tree = get_eval_tree ();
  apply_predicate (ent->fts_path, pstat, eval_tree);

  /* Deal with any side effects of applying the predicates. */
  if (state.stop_at_current_level)
    {
      fts_set (p, ent, FTS_SKIP);
    }
}

/* We've detected a file system loop.   This is caused by one of
 * two things:
 *
 * 1. Option -L is in effect and we've hit a symbolic link that
 *    points to an ancestor.  This is harmless.  We won't traverse the
 *    symbolic link.
 *
 * 2. We have hit a real cycle in the directory hierarchy.  In this
 *    case, we issue a diagnostic message (POSIX requires this) and we
 *    will skip that directory entry.
 */
static void
issue_loop_warning (FTSENT * ent)
{
  if (S_ISLNK(ent->fts_statp->st_mode))
    {
      error (0, 0,
             _("Symbolic link %s is part of a loop in the directory hierarchy; we have already visited the directory to which it points."),
             safely_quote_err_filename (0, ent->fts_path));
    }
  else
    {
      /* We have found an infinite loop.  POSIX requires us to
       * issue a diagnostic.  Usually we won't get to here
       * because when the leaf optimisation is on, it will cause
       * the subdirectory to be skipped.  If /a/b/c/d is a hard
       * link to /a/b, then the link count of /a/b/c is 2,
       * because the ".." entry of /a/b/c/d points to /a, not
       * to /a/b/c.
       */
      error (0, 0,
             _("File system loop detected; "
               "the following directory is part of the cycle: %s"),
             safely_quote_err_filename (0, ent->fts_path));
    }
}

/*
 * Return true if NAME corresponds to a file which forms part of a
 * symbolic link loop.  The command
 *      rm -f a b; ln -s a b; ln -s b a
 * produces such a loop.
 */
static bool
symlink_loop (const char *name)
{
  struct stat stbuf;
  const int rv = options.xstat (name, &stbuf);
  return (0 != rv) && (ELOOP == errno);
}


static void
consider_visiting (FTS *p, FTSENT *ent)
{
  struct stat statbuf;
  mode_t mode;
  int ignore, isdir;

  if (options.debug_options & DebugSearch)
    fprintf (stderr,
             "consider_visiting (early): %s: "
             "fts_info=%-6s, fts_level=%2d, prev_depth=%d "
             "fts_path=%s, fts_accpath=%s\n",
             quotearg_n_style (0, options.err_quoting_style, ent->fts_path),
             get_fts_info_name (ent->fts_info),
             (int)ent->fts_level, prev_depth,
             quotearg_n_style (1, options.err_quoting_style, ent->fts_path),
             quotearg_n_style (2, options.err_quoting_style, ent->fts_accpath));

  if (ent->fts_info == FTS_DP)
    {
      left_dir ();
    }
  else if (ent->fts_level > prev_depth || ent->fts_level==0)
    {
      left_dir ();
    }
  inside_dir (p->fts_cwd_fd);
  prev_depth = ent->fts_level;

  statbuf.st_ino = ent->fts_statp->st_ino;

  /* Cope with various error conditions. */
  if (ent->fts_info == FTS_ERR)
    {
      nonfatal_target_file_error (ent->fts_errno, ent->fts_path);
      return;
    }
  if (ent->fts_info == FTS_DNR)
    {
      /* Ignore ENOENT error for vanished directories.  */
      if (ENOENT == ent->fts_errno && options.ignore_readdir_race)
        return;
      nonfatal_target_file_error (ent->fts_errno, ent->fts_path);
      if (options.do_dir_first)
        {
          /* Return for unreadable directories without -depth.
           * With -depth, the directory itself has to be processed, yet the
           * error message above has to be output.
           */
          return;
        }
    }
  else if (ent->fts_info == FTS_DC)
    {
      issue_loop_warning (ent);
      state.exit_status = EXIT_FAILURE;
      return;
    }
  else if (ent->fts_info == FTS_SLNONE)
    {
      /* fts_read() claims that ent->fts_accpath is a broken symbolic
       * link.  That would be fine, but if this is part of a symbolic
       * link loop, we diagnose the problem and also ensure that the
       * eventual return value is nonzero.   Note that while the path
       * we stat is local (fts_accpath), we print the full path name
       * of the file (fts_path) in the error message.
       */
      if (symlink_loop (ent->fts_accpath))
        {
          nonfatal_target_file_error (ELOOP, ent->fts_path);
          return;
        }
    }
  else if (ent->fts_info == FTS_NS)
    {
      if (ent->fts_level == 0)
        {
          /* e.g., nonexistent starting point */
          nonfatal_target_file_error (ent->fts_errno, ent->fts_path);
          return;
        }
      else
        {
          /* The following if statement fixes Savannah bug #19605
           * (failure to diagnose a symbolic link loop)
           */
          if (symlink_loop (ent->fts_accpath))
            {
              nonfatal_target_file_error (ELOOP, ent->fts_path);
              return;
            }
          else
            {
             /* Ignore ENOENT error for vanished files.  */
             if (ENOENT == ent->fts_errno && options.ignore_readdir_race)
                 return;

              nonfatal_target_file_error (ent->fts_errno, ent->fts_path);
              /* Continue despite the error, as file name without stat info
               * might be better than not even processing the file name. This
               * can lead to repeated error messages later on, though, if a
               * predicate requires stat information.
               *
               * Not printing an error message here would be even more wrong,
               * though, as this could cause the contents of a directory to be
               * silently ignored, as the directory wouldn't be identified as
               * such.
               */
            }

        }
    }

  /* Cope with the usual cases. */
  if (ent->fts_info == FTS_NSOK
      || ent->fts_info == FTS_NS /* e.g. symlink loop */)
    {
      assert (!state.have_stat);
      assert (ent->fts_info == FTS_NSOK || state.type == 0);
      mode = state.type;
    }
  else
    {
      state.have_stat = true;
      state.have_type = true;
      statbuf = *(ent->fts_statp);
      state.type = mode = statbuf.st_mode;

      if (00000 == mode)
        {
          /* Savannah bug #16378. */
          error (0, 0, _("WARNING: file %s appears to have mode 0000"),
                 quotearg_n_style (0, options.err_quoting_style, ent->fts_path));
        }
    }

  /* update state.curdepth before calling digest_mode(), because digest_mode
   * may call following_links().
   */
  state.curdepth = ent->fts_level;
  if (mode)
    {
      if (!digest_mode (&mode, ent->fts_path, ent->fts_name, &statbuf, 0))
        return;
    }

  /* examine this item. */
  ignore = 0;
  isdir = S_ISDIR(mode)
    || (FTS_D  == ent->fts_info)
    || (FTS_DP == ent->fts_info)
    || (FTS_DC == ent->fts_info);

  if (isdir && (ent->fts_info == FTS_NSOK))
    {
      /* This is a directory, but fts did not stat it, so
       * presumably would not be planning to search its
       * children.  Force a stat of the file so that the
       * children can be checked.
       */
      fts_set (p, ent, FTS_AGAIN);
      return;
    }

  if (options.maxdepth >= 0)
    {
      if (ent->fts_level >= options.maxdepth)
        {
          fts_set (p, ent, FTS_SKIP); /* descend no further */

          if (ent->fts_level > options.maxdepth)
            ignore = 1;         /* don't even look at this one */
        }
    }

  if ( (ent->fts_info == FTS_D) && !options.do_dir_first )
    {
      /* this is the preorder visit, but user said -depth */
      ignore = 1;
    }
  else if ( (ent->fts_info == FTS_DP) && options.do_dir_first )
    {
      /* this is the postorder visit, but user didn't say -depth */
      ignore = 1;
    }
  else if (ent->fts_level < options.mindepth)
    {
      ignore = 1;
    }

  if (options.debug_options & DebugSearch)
    fprintf (stderr,
             "consider_visiting (late): %s: "
             "fts_info=%-6s, isdir=%d ignore=%d have_stat=%d have_type=%d \n",
             quotearg_n_style (0, options.err_quoting_style, ent->fts_path),
             get_fts_info_name (ent->fts_info),
             isdir, ignore, state.have_stat, state.have_type);

  if (!ignore)
    {
      visit (p, ent, &statbuf);
    }

  if (ent->fts_info == FTS_DP)
    {
      /* we're leaving a directory. */
      state.stop_at_current_level = false;
    }
}



static bool
find (char *arg)
{
  char * arglist[2];
  FTS *p;
  FTSENT *ent;

  state.starting_path_length = strlen (arg);
  inside_dir (AT_FDCWD);

  arglist[0] = arg;
  arglist[1] = NULL;

  switch (options.symlink_handling)
    {
    case SYMLINK_ALWAYS_DEREF:
      ftsoptions |= FTS_COMFOLLOW|FTS_LOGICAL;
      break;

    case SYMLINK_DEREF_ARGSONLY:
      ftsoptions |= FTS_COMFOLLOW|FTS_PHYSICAL;
      break;

    case SYMLINK_NEVER_DEREF:
      ftsoptions |= FTS_PHYSICAL;
      break;
    }

  if (options.stay_on_filesystem)
    ftsoptions |= FTS_XDEV;

  p = fts_open (arglist, ftsoptions, NULL);
  if (NULL == p)
    {
      error (0, errno, _("cannot search %s"),
             safely_quote_err_filename (0, arg));
      state.exit_status = EXIT_FAILURE;
    }
  else
    {
      int level = INT_MIN;

      while ( (errno=0, ent=fts_read (p)) != NULL )
        {
          if (state.execdirs_outstanding && ((int)ent->fts_level != level))
            {
              /* If we changed level, perform any outstanding
               * execdirs.  If we see a sequence of directory entries
               * like this: fffdfffdfff, we could build a command line
               * of 9 files, but this simple-minded implementation
               * builds a command line for only 3 files at a time
               * (since fts descends into the directories).
               */
              complete_pending_execdirs ();
            }
          level = (int)ent->fts_level;

          state.already_issued_stat_error_msg = false;
          state.have_stat = false;
          state.have_type = !!ent->fts_statp->st_mode;
          state.type = state.have_type ? ent->fts_statp->st_mode : 0;
          consider_visiting (p, ent);
        }
      /* fts_read returned NULL; distinguish between "finished" and "error". */
      if (errno)
        {
          error (0, errno,
                 "failed to read file names from file system at or below %s",
                 safely_quote_err_filename (0, arg));
          state.exit_status = EXIT_FAILURE;
          return false;
        }

      if (0 != fts_close (p))
        {
          /* Here we break the abstraction of fts_close a bit, because we
           * are going to skip the rest of the start points, and return with
           * nonzero exit status.  Hence we need to issue a diagnostic on
           * stderr. */
          error (0, errno,
                 _("failed to restore working directory after searching %s"),
                 arg);
          state.exit_status = EXIT_FAILURE;
          return false;
        }
      p = NULL;
    }
  return true;
}


static bool
process_all_startpoints (int argc, char *argv[])
{
  /* Did the user pass starting points on the command line?  */
  bool argv_starting_points = 0 < argc && !looks_like_expression (argv[0], true);

  FILE *stream = NULL;
  char const* files0_filename_quoted = NULL;

  struct argv_iterator *ai;
  if (options.files0_from)
    {
      /* Option -files0-from must not be combined with passing starting points
       * on the command line.  */
      if (argv_starting_points)
        {
          error (0, 0, _("extra operand %s"), safely_quote_err_filename (0, argv[0]));
          error (EXIT_FAILURE, 0,
                   _("file operands cannot be combined with -files0-from"));
        }

      if (0 == strcmp (options.files0_from, "-"))
        {
          /* Option -files0-from with argument "-" (=stdin) must not be combined
           * with the -ok, -okdir actions: getting the user confirmation would
           * mess with stdin.  */
          if (options.ok_prompt_stdin)
            {
              error (EXIT_FAILURE, 0,
                     _("option -files0-from reading from standard input"
                       " cannot be combined with -ok, -okdir"));
            }
          files0_filename_quoted = safely_quote_err_filename (0, _("(standard input)"));
          stream = stdin;
        }
      else
        {
          files0_filename_quoted = safely_quote_err_filename (0, options.files0_from);
          stream = fopen (options.files0_from, "r");
          if (stream == NULL)
            {
              error (EXIT_FAILURE, errno, _("cannot open %s for reading"),
                     files0_filename_quoted);
            }

          const int fd = fileno (stream);
          assert (fd >= 0);
          if (options.ok_prompt_stdin)
            {
              /* Check if the given file is associated to the same stream as
               * standard input - which is not allowed with -ok, -okdir.  This
               * is the case with special device names symlinks for stdin like
               *   $ find -files0-from /dev/stdin -ok
               * or when the given FILE is also associated to stdin:
               *   $ find -files0-from FILE -ok < FILE
               */
              struct stat sb1, sb2;
              if (fstat (fd, &sb1) == 0 && fstat (STDIN_FILENO, &sb2) == 0
                    && SAME_INODE (sb1, sb2))
                {
                  error (EXIT_FAILURE, 0,
                         _("option -files0-from: standard input must not refer"
                           " to the same file when combined with -ok, -okdir:"
                           " %s"),
                         files0_filename_quoted);
                }
            }
          set_cloexec_flag (fd, true);
       }
      ai = argv_iter_init_stream (stream);
    }
  else
    {
      if (!argv_starting_points)
        {
          /* If no starting points are given on the comman line, then
           * fall back to processing the current directory, i.e., ".".
           * We use a temporary variable here because some actions modify
           * the path temporarily.  Hence if we use a string constant,
           * we get a coredump.  The best example of this is if we say
           * "find -printf %H" (note, not "find . -printf %H").
           */
          char defaultpath[2] = ".";
          return find (defaultpath);
        }

      /* Process the starting point(s) from the command line.  */
      ai = argv_iter_init_argv (argv);
    }

  if (!ai)
    xalloc_die ();

  bool ok = true;
  while (true)
    {
      enum argv_iter_err ai_err;
      char *file_name = argv_iter (ai, &ai_err);
      if (!file_name)
        {
          switch (ai_err)
            {
            case AI_ERR_EOF:
              goto argv_iter_done;
            case AI_ERR_READ:  /* may only happen with -files0-from  */
              error (0, errno, _("%s: read error"), files0_filename_quoted);
              state.exit_status = EXIT_FAILURE;
              ok = false;
              goto argv_iter_done;
            case AI_ERR_MEM:
              xalloc_die ();
            default:
              assert (!"unexpected error code from argv_iter");
            }
        }
      /* Report and skip any empty file names before invoking fts.
         This works around a glitch in fts, which fails immediately
         (without looking at the other file names) when given an empty
         file name.  */
      if (!file_name[0])
        {
          /* Diagnose a zero-length file name.  When it's one
             among many, knowing the record number may help.  */
          if (options.files0_from == NULL)
            error (0, ENOENT, "%s", safely_quote_err_filename (0, file_name));
          else
            {
              /* Using the standard 'filename:line-number:' prefix here is
                 not totally appropriate, since NUL is the separator, not NL,
                 but it might be better than nothing.  */
              unsigned long int file_number = argv_iter_n_args (ai);
              error (0, 0, "%s:%lu: %s", files0_filename_quoted, file_number,
                     _("invalid zero-length file name"));
            }
          state.exit_status = EXIT_FAILURE;
          ok = false;
          continue;
        }

      /* Terminate loop when processing the start points from command line,
         and reaching the first expression.  */
      if (!options.files0_from && looks_like_expression (file_name, true))
        break;

      state.starting_path_length = strlen (file_name); /* TODO: is this redundant? */
      if (!find (file_name))
        {
          ok = false;
          goto argv_iter_done;
        }
    }
 argv_iter_done:

  argv_iter_free (ai);

  if (ok && options.files0_from && (ferror (stream) || fclose (stream) != 0))
    error (EXIT_FAILURE, 0, _("error reading %s"), files0_filename_quoted);

  return ok;
}




int
main (int argc, char **argv)
{
  int end_of_leading_options = 0; /* First arg after any -H/-L etc. */
  struct predicate *eval_tree;

  if (argv[0])
    set_program_name (argv[0]);
  else
    set_program_name ("find");

  record_initial_cwd ();

  state.already_issued_stat_error_msg = false;
  state.exit_status = EXIT_SUCCESS;
  state.execdirs_outstanding = false;
  state.cwd_dir_fd = AT_FDCWD;

  if (fd_leak_check_is_enabled ())
    {
      remember_non_cloexec_fds ();
    }

  state.shared_files = sharefile_init ("w");
  if (NULL == state.shared_files)
    {
      error (EXIT_FAILURE, errno,
             _("Failed to initialize shared-file hash table"));
    }

  /* Set the option defaults before we do the locale initialisation as
   * check_nofollow() needs to be executed in the POSIX locale.
   */
  set_option_defaults (&options);

#ifdef HAVE_SETLOCALE
  setlocale (LC_ALL, "");
#endif

  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
  if (atexit (close_stdout))
    error (EXIT_FAILURE, errno, _("The atexit library function failed"));

  /* Check for -P, -H or -L options.  Also -D and -O, which are
   * both GNU extensions.
   */
  end_of_leading_options = process_leading_options (argc, argv);

  if (options.debug_options & DebugStat)
    options.xstat = debug_stat;


  if (options.debug_options & DebugTime)
    fprintf (stderr, "cur_day_start = %s", ctime (&options.cur_day_start.tv_sec));


  /* We are now processing the part of the "find" command line
   * after the -H/-L options (if any).
   */
  eval_tree = build_expression_tree (argc, argv, end_of_leading_options);

  /* safely_chdir() needs to check that it has ended up in the right place.
   * To avoid bailing out when something gets automounted, it checks if
   * the target directory appears to have had a directory mounted on it as
   * we chdir()ed.  The problem with this is that in order to notice that
   * a file system was mounted, we would need to lstat() all the mount points.
   * That strategy loses if our machine is a client of a dead NFS server.
   *
   * Hence if safely_chdir() and wd_sanity_check() can manage without needing
   * to know the mounted device list, we do that.
   */
  if (!options.open_nofollow_available)
    {
#ifdef STAT_MOUNTPOINTS
      init_mounted_dev_list ();
#endif
    }


  /* process_all_startpoints processes the starting points named on
   * the command line.  A false return value from it means that we
   * failed to restore the original context.  That means it would not
   * be safe to call cleanup() since we might complete an execdir in
   * the wrong directory for example.
   */
  if (process_all_startpoints (argc-end_of_leading_options,
                               argv+end_of_leading_options))
    {
      /* If "-exec ... {} +" has been used, there may be some
       * partially-full command lines which have been built,
       * but which are not yet complete.   Execute those now.
       */
      show_success_rates (eval_tree);
      cleanup ();
    }
  return state.exit_status;
}

bool
is_fts_cwdfd_enabled (void)
{
  return ftsoptions & FTS_CWDFD;
}
