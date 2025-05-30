/* parser.c -- convert the command line args into an expression tree.
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

/* config.h must always come first. */
#include <config.h>

/* system headers. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <regex.h>
#include <sys/stat.h>
#include <unistd.h>


/* gnulib headers. */
#include <fnmatch.h>
#include "intprops.h"
#include "modechange.h"
#include "mountlist.h"
#include "parse-datetime.h"
#include "print.h"
#include "progname.h"
#include "quotearg.h"
#include "regextype.h"
#include "safe-atoi.h"
#include "selinux-at.h"
#include "splitstring.h"
#include "stat-time.h"
#include "xalloc.h"
#include "xstrtod.h"
#include "xstrtol.h"

/* At the moment, we include this after gnulib headers, since it uses
   some of the same names for function attribute macros as gnulib does,
   since I plan to make gcc-attributes a gnulib module.  However, for
   now, I haven't made the wholesale edits to gnulib that this would
   require.   Including this file last simply minimises the number of
   compiler warnings about macro redefinition (in gnulib headers).
*/
#include "gcc-function-attributes.h"

/* find headers. */
#include "buildcmd.h"
#include "defs.h"
#include "fdleak.h"
#include "findutils-version.h"
#include "system.h"

#if ! HAVE_ENDGRENT
# define endgrent() ((void) 0)
#endif

#if ! HAVE_ENDPWENT
# define endpwent() ((void) 0)
#endif

#ifndef UID_T_MAX
# define UID_T_MAX TYPE_MAXIMUM (uid_t)
#endif

#ifndef GID_T_MAX
# define GID_T_MAX TYPE_MAXIMUM (gid_t)
#endif

/* Roll our own isnan rather than using <math.h>.  */
#ifndef isnan
# define isnan(x) ((x) != (x))
#endif

static bool parse_accesscheck   (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_amin          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_and           (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_anewer        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_cmin          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_cnewer        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_comma         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_daystart      (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_delete        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_d             (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_depth         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_empty         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_exec          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_execdir       (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_false         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_files0_from   (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_fls           (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_fprintf       (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_follow        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_fprint        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_fprint0       (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_fstype        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_gid           (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_group         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_ilname        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_iname         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_inum          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_ipath         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_iregex        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_iwholename    (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_links         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_lname         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_ls            (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_maxdepth      (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_mindepth      (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_mmin          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_name          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_negate        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_newer         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_newerXY       (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_noleaf        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_nogroup       (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_nouser        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_nowarn        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_ok            (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_okdir         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_or            (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_path          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_perm          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_print0        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_printf        (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_prune         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_regex         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_regextype     (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_samefile      (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_size          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_time          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_true          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_type          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_uid           (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_used          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_user          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_wholename     (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_xdev          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_ignore_race   (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_noignore_race (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_warn          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_xtype         (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_quit          (const struct parser_table*, char *argv[], int *arg_ptr);
static bool parse_context       (const struct parser_table*, char *argv[], int *arg_ptr);

static bool parse_help (const struct parser_table* entry, char **argv, int *arg_ptr)
  _GL_ATTRIBUTE_NORETURN;
static bool parse_version       (const struct parser_table*, char *argv[], int *arg_ptr)
  _GL_ATTRIBUTE_NORETURN;


static bool insert_type (char **argv, int *arg_ptr,
                         const struct parser_table *entry,
                         PRED_FUNC which_pred);
static bool insert_regex (char *argv[], int *arg_ptr,
                          const struct parser_table *entry,
                          int regex_options);
static bool insert_exec_ok (const char *action,
                            const struct parser_table *entry,
                            char *argv[],
                            int *arg_ptr);
static bool get_comp_type (const char **str,
                           enum comparison_type *comp_type);
static bool get_relative_timestamp (const char *str,
                                    struct time_val *tval,
                                    struct timespec origin,
                                    double sec_per_unit,
                                    const char *overflowmessage);
static bool get_num (const char *str,
                     uintmax_t *num,
                     enum comparison_type *comp_type);
static struct predicate* insert_num (char *argv[], int *arg_ptr,
                                     const struct parser_table *entry);
static void open_output_file (const char *path, struct format_val *p);
static void open_stdout (struct format_val *p);
static bool stream_is_tty(FILE *fp);
static bool parse_noop (const struct parser_table* entry,
                        char **argv, int *arg_ptr);

#define PASTE(x,y) x##y


#define PARSE_OPTION(what,suffix) \
  { (ARG_OPTION), (what), PASTE(parse_,suffix), NULL }

#define PARSE_POSOPT(what,suffix) \
  { (ARG_POSITIONAL_OPTION), (what), PASTE(parse_,suffix), NULL }

#define PARSE_TEST(what,suffix) \
  { (ARG_TEST), (what), PASTE(parse_,suffix), PASTE(pred_,suffix) }

#define PARSE_TEST_NP(what,suffix) \
  { (ARG_TEST), (what), PASTE(parse_,suffix), NULL }

#define PARSE_ACTION(what,suffix) \
  { (ARG_ACTION), (what), PASTE(parse_,suffix), PASTE(pred_,suffix) }

#define PARSE_PUNCTUATION(what,suffix) \
  { (ARG_PUNCTUATION), (what), PASTE(parse_,suffix), PASTE(pred_,suffix) }


/* Predicates we cannot handle in the usual way.  If you add an entry
 * to this table, double-check the switch statement in
 * pred_sanity_check() to make sure that the new case is being
 * correctly handled.
 */
static struct parser_table const parse_entry_newerXY =
  {
    ARG_SPECIAL_PARSE, "newerXY",            parse_newerXY, pred_newerXY /* BSD  */
  };

/* GNU find predicates that are not mentioned in POSIX.2 are marked `GNU'.
   If they are in some Unix versions of find, they are marked `Unix'. */

static struct parser_table const parse_table[] =
{
  PARSE_PUNCTUATION("!",                     negate), /* POSIX */
  PARSE_PUNCTUATION("not",                   negate),        /* GNU */
  PARSE_PUNCTUATION("(",                     openparen), /* POSIX */
  PARSE_PUNCTUATION(")",                     closeparen), /* POSIX */
  PARSE_PUNCTUATION(",",                     comma),         /* GNU */
  PARSE_PUNCTUATION("a",                     and), /* POSIX */
  PARSE_TEST       ("amin",                  amin),          /* GNU */
  PARSE_PUNCTUATION("and",                   and),              /* GNU */
  PARSE_TEST       ("anewer",                anewer),        /* GNU */
  {ARG_TEST,       "atime",                  parse_time, pred_atime}, /* POSIX */
  PARSE_TEST       ("cmin",                  cmin),          /* GNU */
  PARSE_TEST       ("cnewer",                cnewer),        /* GNU */
  {ARG_TEST,       "ctime",                  parse_time, pred_ctime}, /* POSIX */
  PARSE_TEST       ("context",               context),      /* GNU */
  PARSE_POSOPT     ("daystart",              daystart),      /* GNU */
  PARSE_ACTION     ("delete",                delete), /* GNU, Mac OS, FreeBSD */
  PARSE_OPTION     ("d",                     d), /* Mac OS X, FreeBSD, NetBSD, OpenBSD, but deprecated  in favour of -depth */
  PARSE_OPTION     ("depth",                 depth), /* POSIX */
  PARSE_TEST       ("empty",                 empty),         /* GNU */
  {ARG_ACTION,      "exec",    parse_exec, pred_exec}, /* POSIX */
  {ARG_TEST,        "executable",            parse_accesscheck, pred_executable}, /* GNU, 4.3.0+ */
  PARSE_ACTION     ("execdir",               execdir), /* *BSD, GNU */
  PARSE_OPTION     ("files0-from",           files0_from),   /* GNU */
  PARSE_ACTION     ("fls",                   fls),           /* GNU */
  PARSE_POSOPT     ("follow",                follow),  /* GNU, Unix */
  PARSE_ACTION     ("fprint",                fprint),        /* GNU */
  PARSE_ACTION     ("fprint0",               fprint0),       /* GNU */
  {ARG_ACTION,      "fprintf", parse_fprintf, pred_fprintf}, /* GNU */
  PARSE_TEST       ("fstype",                fstype),  /* GNU, Unix */
  PARSE_TEST       ("gid",                   gid),           /* GNU */
  PARSE_TEST       ("group",                 group), /* POSIX */
  PARSE_OPTION     ("ignore_readdir_race",   ignore_race),   /* GNU */
  PARSE_TEST       ("ilname",                ilname),        /* GNU */
  PARSE_TEST       ("iname",                 iname),         /* GNU */
  PARSE_TEST       ("inum",                  inum),    /* GNU, Unix */
  PARSE_TEST       ("ipath",                 ipath), /* GNU, deprecated in favour of iwholename */
  PARSE_TEST_NP    ("iregex",                iregex),        /* GNU */
  PARSE_TEST_NP    ("iwholename",            iwholename),    /* GNU */
  PARSE_TEST       ("links",                 links), /* POSIX */
  PARSE_TEST       ("lname",                 lname),         /* GNU */
  PARSE_ACTION     ("ls",                    ls),      /* GNU, Unix */
  PARSE_OPTION     ("maxdepth",              maxdepth),      /* GNU */
  PARSE_OPTION     ("mindepth",              mindepth),      /* GNU */
  PARSE_TEST       ("mmin",                  mmin),          /* GNU */
  PARSE_OPTION     ("mount",                 xdev),         /* Unix */
  {ARG_TEST,       "mtime",                  parse_time, pred_mtime}, /* POSIX */
  PARSE_TEST       ("name",                  name),
#ifdef UNIMPLEMENTED_UNIX
  PARSE(ARG_UNIMPLEMENTED, "ncpio",          ncpio),        /* Unix */
#endif
  PARSE_TEST       ("newer",                 newer), /* POSIX */
  {ARG_TEST,       "atime",                  parse_time, pred_atime}, /* POSIX */
  PARSE_OPTION     ("noleaf",                noleaf),        /* GNU */
  PARSE_TEST       ("nogroup",               nogroup), /* POSIX */
  PARSE_TEST       ("nouser",                nouser), /* POSIX */
  PARSE_OPTION     ("noignore_readdir_race", noignore_race), /* GNU */
  PARSE_POSOPT     ("nowarn",                nowarn),        /* GNU */
  PARSE_POSOPT     ("warn",                  warn),          /* GNU */
  PARSE_PUNCTUATION("o",                     or), /* POSIX */
  PARSE_PUNCTUATION("or",                    or),            /* GNU */
  PARSE_ACTION     ("ok",                    ok), /* POSIX */
  PARSE_ACTION     ("okdir",                 okdir), /* GNU (-execdir is BSD) */
  PARSE_TEST       ("path",                  path), /* POSIX */
  PARSE_TEST       ("perm",                  perm), /* POSIX */
  PARSE_ACTION     ("print",                 print), /* POSIX */
  PARSE_ACTION     ("print0",                print0),        /* GNU */
  {ARG_ACTION,      "printf",   parse_printf, NULL},         /* GNU */
  PARSE_ACTION     ("prune",                 prune), /* POSIX */
  PARSE_ACTION     ("quit",                  quit),          /* GNU */
  {ARG_TEST,       "readable",            parse_accesscheck, pred_readable}, /* GNU, 4.3.0+ */
  PARSE_TEST       ("regex",                 regex),         /* GNU */
  PARSE_POSOPT     ("regextype",             regextype),     /* GNU */
  PARSE_TEST       ("samefile",              samefile),      /* GNU */
  PARSE_TEST       ("size",                  size), /* POSIX */
  PARSE_TEST       ("type",                  type), /* POSIX */
  PARSE_TEST       ("uid",                   uid),           /* GNU */
  PARSE_TEST       ("used",                  used),          /* GNU */
  PARSE_TEST       ("user",                  user), /* POSIX */
  PARSE_TEST_NP    ("wholename",             wholename), /* GNU, replaced -path, but now -path is standardized since POSIX 2008 */
  {ARG_TEST,       "writable",               parse_accesscheck, pred_writable}, /* GNU, 4.3.0+ */
  PARSE_OPTION     ("xdev",                  xdev), /* POSIX */
  PARSE_TEST       ("xtype",                 xtype),         /* GNU */
#ifdef UNIMPLEMENTED_UNIX
  /* It's pretty ugly for find to know about archive formats.
     Plus what it could do with cpio archives is very limited.
     Better to leave it out.  */
  PARSE(ARG_UNIMPLEMENTED,      "cpio",                  cpio), /* Unix */
#endif
  /* gnulib's stdbool.h might have made true and false into macros,
   * so we can't leave naked 'true' and 'false' tokens, so we have
   * to expand the relevant entries longhand.
   */
  {ARG_TEST, "false",                 parse_false,   pred_false}, /* GNU */
  {ARG_TEST, "true",                  parse_true,    pred_true }, /* GNU */
  /* Internal pseudo-option, therefore 3 minus: ---noop.  */
  {ARG_NOOP, "--noop",                NULL,          pred_true }, /* GNU, internal use only */

  /* Various other cases that don't fit neatly into our macro scheme. */
  {ARG_TEST, "help",                  parse_help,    NULL},       /* GNU */
  {ARG_TEST, "-help",                 parse_help,    NULL},       /* GNU */
  {ARG_TEST, "version",               parse_version, NULL},       /* GNU */
  {ARG_TEST, "-version",              parse_version, NULL},       /* GNU */
  {0, NULL, NULL, NULL}
};


static const char *first_nonoption_arg = NULL;
static const struct parser_table *noop = NULL;

static int
fallback_getfilecon (int fd, const char *name, char **p, int prev_rv)
{
  /* Our original getfilecon () call failed.  Perhaps we can't follow a
   * symbolic link.  If that might be the problem, lgetfilecon () the link.
   * Otherwise, admit defeat. */
  switch (errno)
    {
      case ENOENT:
      case ENOTDIR:
        if (options.debug_options & DebugStat)
          {
            fprintf (stderr, "fallback_getfilecon(): getfilecon(%s) failed; falling "
                     "back on lgetfilecon()\n", name);
          }
        return lgetfileconat (fd, name, p);

      case EACCES:
      case EIO:
      case ELOOP:
      case ENAMETOOLONG:
#ifdef EOVERFLOW
      case EOVERFLOW:        /* EOVERFLOW is not #defined on UNICOS. */
#endif
      default:
        return prev_rv;
    }
}

/* optionh_getfilecon () implements the getfilecon operation when the
 * -H option is in effect.
 *
 * If the item to be examined is a command-line argument, we follow
 * symbolic links.  If the getfilecon () call fails on the command-line
 * item, we fall back on the properties of the symbolic link.
 *
 * If the item to be examined is not a command-line argument, we
 * examine the link itself. */
static int
optionh_getfilecon (int fd, const char *name, char **p)
{
  int rv;
  if (0 == state.curdepth)
    {
      /* This file is from the command line; dereference the link (if it is
         a link). */
      rv = getfileconat (fd, name, p);
      if (0 == rv)
        return 0;               /* success */
      else
        return fallback_getfilecon (fd, name, p, rv);
    }
  else
    {
      /* Not a file on the command line; do not dereference the link. */
      return lgetfileconat (fd, name, p);
    }
}

/* optionl_getfilecon () implements the getfilecon operation when the
 * -L option is in effect.  That option makes us examine the thing the
 * symbolic link points to, not the symbolic link itself. */
static int
optionl_getfilecon (int fd, const char *name, char **p)
{
  int rv = getfileconat (fd, name, p);
  if (0 == rv)
    return 0;                  /* normal case. */
  else
    return fallback_getfilecon (fd, name, p, rv);
}

/* optionp_getfilecon () implements the stat operation when the -P
 * option is in effect (this is also the default).  That option makes
 * us examine the symbolic link itself, not the thing it points to. */
static int
optionp_getfilecon (int fd, const char *name, char **p)
{
  return lgetfileconat (fd, name, p);
}

void
check_option_combinations (const struct predicate *p)
{
  enum { seen_delete=1u, seen_prune=2u };
  unsigned int predicates = 0u;

  while (p)
    {
      if (p->pred_func == pred_delete)
        predicates |= seen_delete;
      else if (p->pred_func == pred_prune)
        predicates |= seen_prune;
      p = p->pred_next;
    }

  if ((predicates & seen_prune) && (predicates & seen_delete))
    {
      /* The user specified both -delete and -prune.  One might test
       * this by first doing
       *    find dirs   .... -prune ..... -print
       * to find out what's going to get deleted, and then switch to
       *    find dirs   .... -prune ..... -delete
       * once we are happy.  Unfortunately, the -delete action also
       * implicitly turns on -depth, which will affect the behaviour
       * of -prune (in fact, it makes it a no-op).  In this case we
       * would like to prevent unfortunate accidents, so we require
       * the user to have explicitly used -depth.
       *
       * We only get away with this because the -delete predicate is not
       * in POSIX.   If it was, we couldn't issue a fatal error here.
       */
      if (!options.explicit_depth)
        {
          /* This fixes Savannah bug #20865. */
          error (EXIT_FAILURE, 0,
                 _("The -delete action automatically turns on -depth, "
                   "but -prune does nothing when -depth is in effect.  "
                   "If you want to carry on anyway, just explicitly use "
                   "the -depth option."));
        }
    }
}


static const struct parser_table*
get_noop (void)
{
  int i;
  if (NULL == noop)
    {
      for (i = 0; parse_table[i].parser_name != NULL; i++)
        {
          if (ARG_NOOP ==parse_table[i].type)
            {
              noop = &(parse_table[i]);
              break;
            }
        }
    }
  return noop;
}

static int
get_stat_Ytime (const struct stat *p,
                char what,
                struct timespec *ret)
{
  switch (what)
    {
    case 'a':
      *ret = get_stat_atime (p);
      return 1;
    case 'B':
      *ret = get_stat_birthtime (p);
      return (ret->tv_nsec >= 0);
    case 'c':
      *ret = get_stat_ctime (p);
      return 1;
    case 'm':
      *ret = get_stat_mtime (p);
      return 1;
    default:
      assert (0);
      abort ();
    }
}

void
set_follow_state (enum SymlinkOption opt)
{
  switch (opt)
    {
    case SYMLINK_ALWAYS_DEREF:  /* -L */
      options.xstat = optionl_stat;
      options.x_getfilecon = optionl_getfilecon;
      options.no_leaf_check = true;
      break;

    case SYMLINK_NEVER_DEREF:   /* -P (default) */
      options.xstat = optionp_stat;
      options.x_getfilecon = optionp_getfilecon;
      /* Can't turn no_leaf_check off because the user might have specified
       * -noleaf anyway
       */
      break;

    case SYMLINK_DEREF_ARGSONLY: /* -H */
      options.xstat = optionh_stat;
      options.x_getfilecon = optionh_getfilecon;
      options.no_leaf_check = true;
    }

  options.symlink_handling = opt;

  if (options.debug_options & DebugStat)
    {
      /* For DebugStat, the choice is made at runtime within debug_stat()
       * by checking the contents of the symlink_handling variable.
       */
      options.xstat = debug_stat;
    }
}


void
parse_begin_user_args (char **args, int argno,
                       const struct predicate *last,
                       const struct predicate *predicates)
{
  (void) args;
  (void) argno;
  (void) last;
  (void) predicates;
  first_nonoption_arg = NULL;
}

void
parse_end_user_args (char **args, int argno,
                     const struct predicate *last,
                     const struct predicate *predicates)
{
  /* does nothing */
  (void) args;
  (void) argno;
  (void) last;
  (void) predicates;
}

static bool
should_issue_warnings (void)
{
  if (options.posixly_correct)
    return false;
  else
    return options.warnings;
}


/* Check that it is legal to find the given primary in its
 * position and return it.
 */
static const struct parser_table*
found_parser (const char *original_arg, const struct parser_table *entry)
{
  /* If this is an option, but we have already had a
   * non-option argument, the user may be under the
   * impression that the behaviour of the option
   * argument is conditional on some preceding
   * tests.  This might typically be the case with,
   * for example, -maxdepth.
   *
   * The options -daystart and -follow are exempt
   * from this treatment, since their positioning
   * in the command line does have an effect on
   * subsequent tests but not previous ones.  That
   * might be intentional on the part of the user.
   */
  if (entry->type != ARG_POSITIONAL_OPTION)
    {
      if (entry->type == ARG_NOOP)
        return NULL;  /* internal use only, trap -noop here.  */

      /* Something other than -follow/-daystart.
       * If this is an option, check if it followed
       * a non-option and if so, issue a warning.
       */
      if (entry->type == ARG_OPTION)
        {
          if ((first_nonoption_arg != NULL)
              && should_issue_warnings ())
            {
              /* option which follows a non-option */
              error (0, 0,
                     _("warning: you have specified the global option %s "
                       "after the argument %s, but global options are not "
                       "positional, i.e., %s affects tests specified before it "
                       "as well as those specified after it.  "
                       "Please specify global options before other arguments."),
                     original_arg,
                     first_nonoption_arg,
                     original_arg);
            }
        }
      else
        {
          /* Not an option or a positional option,
           * so remember we've seen it in order to
           * use it in a possible future warning message.
           */
          if (first_nonoption_arg == NULL)
            {
              first_nonoption_arg = original_arg;
            }
        }
    }

  return entry;
}


/* Return a pointer to the parser function to invoke for predicate
   SEARCH_NAME.
   Return NULL if SEARCH_NAME is not a valid predicate name. */

const struct parser_table*
find_parser (const char *search_name)
{
  int i;
  const char *original_arg = search_name;

  /* Ugh.  Special case -newerXY. */
  if (0 == strncmp ("-newer", search_name, 6)
      && (8 == strlen (search_name)))
    {
      return found_parser (original_arg, &parse_entry_newerXY);
    }

  if (*search_name == '-')
    search_name++;

  for (i = 0; parse_table[i].parser_name != NULL; i++)
    {
      if (strcmp (parse_table[i].parser_name, search_name) == 0)
        {
          /* FIXME >4.11: fix parser to disallow dashed operators like '-!'.
           * Meanwhile, issue a warning.  */
          if (   (original_arg < search_name) /* with '-' */
              && (ARG_PUNCTUATION == parse_table[i].type)
              && (   search_name[0] == '!' || search_name[0] == ','
                  || search_name[0] == '(' || search_name[0] == ')')
              && (search_name[1] == '\0'))
            {
              error (0, 0,
                     _("warning: operator '%s' (with leading dash '-') will "
                       "no longer be accepted in future findutils releases!"),
                    original_arg);
            }

          return found_parser (original_arg, &parse_table[i]);
        }
    }
  return NULL;
}

static float
estimate_file_age_success_rate (float num_days)
{
  if (num_days < 0.1f)
    {
      /* Assume 1% of files have timestamps in the future */
      return 0.01f;
    }
  else if (num_days < 1.0f)
    {
      /* Assume 30% of files have timestamps today */
      return 0.3f;
    }
  else if (num_days > 100.0f)
    {
      /* Assume 30% of files are very old */
      return 0.3f;
    }
  else
    {
      /* Assume 39% of files are between 1 and 100 days old. */
      return 0.39f;
    }
}

static float
estimate_timestamp_success_rate (time_t when)
{
  /* This calculation ignores the nanoseconds field of the
   * origin, but I don't think that makes much difference
   * to our estimate.
   */
  int num_days = (options.cur_day_start.tv_sec - when) / 86400;
  return estimate_file_age_success_rate (num_days);
}

/* Collect an argument from the argument list, or
 * return false.
 */
static bool
collect_arg_nonconst (char **argv, int *arg_ptr, char **collected_arg)
{
  if ((argv == NULL) || (argv[*arg_ptr] == NULL))
    {
      *collected_arg = NULL;
      return false;
    }
  else
    {
      *collected_arg = argv[*arg_ptr];
      (*arg_ptr)++;
      return true;
    }
}

static bool
collect_arg (char **argv, int *arg_ptr, const char **collected_arg)
{
  char *arg;
  const bool result = collect_arg_nonconst (argv, arg_ptr, &arg);
  *collected_arg = arg;
  return result;
}



static bool
collect_arg_stat_info (char **argv, int *arg_ptr, struct stat *p,
                       const char **argument)
{
  const char *filename;
  if (collect_arg (argv, arg_ptr, &filename))
    {
      *argument = filename;
      if (0 != (options.xstat)(filename, p))
        {
          fatal_target_file_error (errno, filename);
        }
      return true;
    }
  else
    {
      *argument = NULL;
      return false;
    }
}

/* The parsers are responsible to continue scanning ARGV for
   their arguments.  Each parser knows what is and isn't
   allowed for itself.

   ARGV is the argument array.
   *ARG_PTR is the index to start at in ARGV,
   updated to point beyond the last element consumed.

   The predicate structure is updated with the new information. */


static bool
parse_and (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = get_new_pred_noarg (entry);
  our_pred->pred_func = pred_and;
  our_pred->p_type = BI_OP;
  our_pred->p_prec = AND_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  return true;
}

static bool
parse_anewer (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct stat stat_newer;
  const char *arg;

  set_stat_placeholders (&stat_newer);
  if (collect_arg_stat_info (argv, arg_ptr, &stat_newer, &arg))
    {
      struct predicate *our_pred = insert_primary (entry, arg);
      our_pred->args.reftime.xval = XVAL_ATIME;
      our_pred->args.reftime.ts = get_stat_mtime (&stat_newer);
      our_pred->args.reftime.kind = COMP_GT;
      our_pred->est_success_rate = estimate_timestamp_success_rate (stat_newer.st_mtime);
      return true;
    }
  return false;
}

bool
parse_closeparen (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = get_new_pred_noarg (entry);
  our_pred->pred_func = pred_closeparen;
  our_pred->p_type = CLOSE_PAREN;
  our_pred->p_prec = NO_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  return true;
}

static bool
parse_cnewer (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct stat stat_newer;
  const char *arg;

  set_stat_placeholders (&stat_newer);
  if (collect_arg_stat_info (argv, arg_ptr, &stat_newer, &arg))
    {
      struct predicate *our_pred = insert_primary (entry, arg);
      our_pred->args.reftime.xval = XVAL_CTIME; /* like -newercm */
      our_pred->args.reftime.ts = get_stat_mtime (&stat_newer);
      our_pred->args.reftime.kind = COMP_GT;
      our_pred->est_success_rate = estimate_timestamp_success_rate (stat_newer.st_mtime);
      return true;
    }
  return false;
}

static bool
parse_comma (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = get_new_pred_noarg (entry);
  our_pred->pred_func = pred_comma;
  our_pred->p_type = BI_OP;
  our_pred->p_prec = COMMA_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->est_success_rate = 1.0f;
  return true;
}

static bool
parse_daystart (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct tm *local;

  (void) entry;
  (void) argv;
  (void) arg_ptr;

  if (options.full_days == false)
    {
      options.cur_day_start.tv_sec += DAYSECS;
      options.cur_day_start.tv_nsec = 0;
      local = localtime (&options.cur_day_start.tv_sec);
      options.cur_day_start.tv_sec -= (local
                                       ? (local->tm_sec + local->tm_min * 60
                                          + local->tm_hour * 3600)
                                       : options.cur_day_start.tv_sec % DAYSECS);
      options.full_days = true;
    }
  return true;
}

static bool
parse_delete (const struct parser_table* entry, char *argv[], int *arg_ptr)
{
  struct predicate *our_pred;
  (void) argv;
  (void) arg_ptr;

  our_pred = insert_primary_noarg (entry);
  our_pred->side_effects = our_pred->no_default_print = true;
  /* -delete implies -depth */
  options.do_dir_first = false;

  /* We do not need stat information because we check for the case
   * (errno==EISDIR) in pred_delete.
   */
  our_pred->need_stat = our_pred->need_type = false;

  our_pred->est_success_rate = 1.0f;
  return true;
}

static bool
parse_depth (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  (void) argv;

  options.do_dir_first = false;
  options.explicit_depth = true;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_d (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  if (should_issue_warnings ())
    {
      error (0, 0,
             _("warning: the -d option is deprecated; please use "
               "-depth instead, because the latter is a "
               "POSIX-compliant feature."));
    }
  return parse_depth (entry, argv, arg_ptr);
}

static bool
parse_empty (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  (void) argv;
  (void) arg_ptr;

  our_pred = insert_primary_noarg (entry);
  our_pred->est_success_rate = 0.01f; /* assume 1% of files are empty. */
  return true;
}

static bool
parse_exec (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_exec_ok ("-exec", entry, argv, arg_ptr);
}

static bool
parse_execdir (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_exec_ok ("-execdir", entry, argv, arg_ptr);
}

static bool
insert_false(void)
{
  struct predicate *our_pred;
  const struct parser_table *entry_false;

  entry_false = find_parser("false");
  our_pred = insert_primary_noarg (entry_false);
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->side_effects = our_pred->no_default_print = false;
  our_pred->est_success_rate = 0.0f;
  return true;
}


static bool
parse_false (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  (void) argv;
  (void) arg_ptr;
  return insert_false ();
}

static bool
parse_files0_from (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *filename;
  if (collect_arg (argv, arg_ptr, &filename))
    {
      options.files0_from = filename;
      return true;
    }
  return false;
}

static bool
insert_fls (const struct parser_table* entry, const char *filename)
{
  struct predicate *our_pred = insert_primary_noarg (entry);
  if (filename)
    open_output_file (filename, &our_pred->args.printf_vec);
  else
    open_stdout (&our_pred->args.printf_vec);
  our_pred->side_effects = our_pred->no_default_print = true;
  our_pred->est_success_rate = 1.0f;
  return true;
}


static bool
parse_fls (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *filename;
  if (collect_arg (argv, arg_ptr, &filename))
    {
      if (insert_fls (entry, filename))
        return true;
      else
        --*arg_ptr;             /* don't consume the invalid arg. */
    }
  return false;
}

static bool
parse_follow (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  set_follow_state (SYMLINK_ALWAYS_DEREF);
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_fprint (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  const char *filename;
  if (collect_arg (argv, arg_ptr, &filename))
    {
      our_pred = insert_primary (entry, filename);
      open_output_file (filename, &our_pred->args.printf_vec);
      our_pred->side_effects = our_pred->no_default_print = true;
      our_pred->need_stat = our_pred->need_type = false;
      our_pred->est_success_rate = 1.0f;
      return true;
    }
  else
    {
      return false;
    }
}

static bool
insert_fprint (const struct parser_table* entry, const char *filename)
{
  struct predicate *our_pred = insert_primary (entry, filename);
  if (filename)
    open_output_file (filename, &our_pred->args.printf_vec);
  else
    open_stdout (&our_pred->args.printf_vec);
  our_pred->side_effects = our_pred->no_default_print = true;
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->est_success_rate = 1.0f;
  return true;
}


static bool
parse_fprint0 (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *filename;
  if (collect_arg (argv, arg_ptr, &filename))
    {
      if (insert_fprint (entry, filename))
        return true;
      else
        --*arg_ptr;             /* don't consume the bad arg. */
    }
  return false;
}

static float estimate_fstype_success_rate (const char *fsname)
{
  struct stat dir_stat;
  const char *the_root_dir = "/";
  if (0 == stat (the_root_dir, &dir_stat)) /* it's an absolute path anyway */
    {
      const char *fstype = filesystem_type (&dir_stat, the_root_dir);
      /* Assume most files are on the same file system type as the root fs. */
      if (0 == strcmp (fsname, fstype))
          return 0.7f;
      else
        return 0.3f;
    }
  return 1.0f;
}



static bool
parse_fstype (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *typename;
  if (collect_arg (argv, arg_ptr, &typename))
    {
      if (options.optimisation_level < 2 || is_used_fs_type (typename))
        {
          struct predicate *our_pred = insert_primary (entry, typename);
          our_pred->args.str = typename;

          /* This is an expensive operation, so although there are
           * circumstances where it is selective, we ignore this fact
           * because we probably don't want to promote this test to the
           * front anyway.
           */
          our_pred->est_success_rate = estimate_fstype_success_rate (typename);
          return true;
        }
      else
        {
          /* This filesystem type is not listed in the mount table.
           * Hence this predicate will always return false (with this argument).
           * Substitute a predicate with the same effect as -false.
           */
          if (options.debug_options & DebugTreeOpt)
            {
              fprintf (stderr,
                       "-fstype %s can never succeed, substituting -false\n",
                       typename);
            }
          return insert_false ();
        }
    }
  else
    {
      return false;
    }
}

static bool
parse_gid (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *p = insert_num (argv, arg_ptr, entry);
  if (p)
    {
      p->est_success_rate = (p->args.numinfo.l_val < 100) ? 0.99 : 0.2;
      return true;
    }
  return false;
}


static bool
parse_group (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *groupname;

  if (collect_arg (argv, arg_ptr, &groupname))
    {
      struct predicate *our_pred;
      gid_t gid;
      struct group *cur_gr = getgrnam (groupname);
      endgrent ();
      if (cur_gr != NULL)
        {
          gid = cur_gr->gr_gid;
        }
      else
        {
          uintmax_t num;
          if ((xstrtoumax (groupname, NULL, 10, &num, "") != LONGINT_OK)
                || (GID_T_MAX < num))
            {
              error (EXIT_FAILURE, 0,
                     _("invalid group name or GID argument to -group: %s"),
                     quotearg_n_style (0, options.err_quoting_style,
                                       groupname));
            }
          gid = num;
        }
      our_pred = insert_primary (entry, groupname);
      our_pred->args.gid = gid;
      our_pred->est_success_rate = (our_pred->args.gid < 100) ? 0.99 : 0.2;
      return true;
    }
  return false;
}

static bool
parse_help (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  (void) argv;
  (void) arg_ptr;

  usage (EXIT_SUCCESS);
}

static float
estimate_pattern_match_rate (const char *pattern, int is_regex)
{
  if (strpbrk (pattern, "*?[") || (is_regex && strpbrk(pattern, ".")))
    {
      /* A wildcard; assume the pattern matches most files. */
      return 0.8f;
    }
  else
    {
      return 0.1f;
    }
}

static bool
parse_ilname (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *name;
  if (collect_arg (argv, arg_ptr, &name))
    {
      struct predicate *our_pred = insert_primary (entry, name);
      our_pred->args.str = name;
      /* Use the generic glob pattern estimator to figure out how many
       * links will match, but bear in mind that most files won't be links.
       */
      our_pred->est_success_rate = 0.1f * estimate_pattern_match_rate (name, 0);
      return true;
    }
  else
    {
      return false;
    }
}


/* sanity check the fnmatch() function to make sure that case folding
 * is supported (as opposed to just having the flag ignored).
 */
static bool
fnmatch_sanitycheck (void)
{
  static bool checked = false;
  if (!checked)
    {
      if (0 != fnmatch ("foo", "foo", 0)
          || 0 == fnmatch ("Foo", "foo", 0)
          || 0 != fnmatch ("Foo", "foo", FNM_CASEFOLD))
        {
          error (EXIT_FAILURE, 0,
                 _("sanity check of the fnmatch() library function failed."));
          return false;
        }
      checked = true;
    }
  return checked;
}


static void
check_name_arg (const char *pred, const char *alt, const char *arg)
{
  if (should_issue_warnings () && strchr (arg, '/') && (0 != strcmp ("/", arg)))
    {
      error (0, 0,
             _("warning: %s matches against basenames only, "
                "but the given pattern contains a directory separator (%s), "
                "thus the expression will evaluate to false all the time.  "
                "Did you mean %s?"),
             safely_quote_err_filename (0, pred),
             safely_quote_err_filename (1, "/"),
             safely_quote_err_filename (2, alt));
    }
}



static bool
parse_iname (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *name;
  fnmatch_sanitycheck ();
  if (collect_arg (argv, arg_ptr, &name))
    {
      struct predicate *our_pred;
      check_name_arg ("-iname", "-iwholename", name);

      our_pred = insert_primary (entry, name);
      our_pred->need_stat = our_pred->need_type = false;
      our_pred->args.str = name;
      our_pred->est_success_rate = estimate_pattern_match_rate (name, 0);
      return true;
    }
  return false;
}

static bool
parse_inum (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *p =  insert_num (argv, arg_ptr, entry);
  if (p)
    {
      /* inode number is exact match only, so very low proportions of
       * files match
       */
      p->est_success_rate = 1e-6;
      p->need_inum = true;
      p->need_stat = false;
      p->need_type = false;
      return true;
    }
  return false;
}

static bool
parse_iregex (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_regex (argv, arg_ptr, entry, RE_ICASE|options.regex_options);
}

static bool
parse_links (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *p = insert_num (argv, arg_ptr, entry);
  if (p)
    {
      if (p->args.numinfo.l_val == 1)
        p->est_success_rate = 0.99;
      else if (p->args.numinfo.l_val == 2)
        p->est_success_rate = 0.01;
      else
        p->est_success_rate = 1e-3;
      return true;
    }
  return false;
}

static bool
parse_lname (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *name;
  fnmatch_sanitycheck ();
  if (collect_arg (argv, arg_ptr, &name))
    {
      struct predicate *our_pred = insert_primary (entry, name);
      our_pred->args.str = name;
      our_pred->est_success_rate = 0.1f * estimate_pattern_match_rate (name, 0);
      return true;
    }
  return false;
}

static bool
parse_ls (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) &argv;
  (void) &arg_ptr;
  return insert_fls (entry, NULL);
}

static bool
insert_depthspec (const struct parser_table* entry, char **argv, int *arg_ptr,
                  int *limitptr)
{
  const char *depthstr;
  int depth_len;
  const char *predicate = argv[(*arg_ptr)-1];
  if (collect_arg (argv, arg_ptr, &depthstr))
    {
      depth_len = strspn (depthstr, "0123456789");
      if ((depth_len > 0) && (depthstr[depth_len] == 0))
        {
          (*limitptr) = safe_atoi (depthstr, options.err_quoting_style);
          if (*limitptr >= 0)
            {
              return parse_noop (entry, argv, arg_ptr);
            }
        }
      error (EXIT_FAILURE, 0,
             _("Expected a positive decimal integer argument to %s, but got %s"),
             predicate,
             quotearg_n_style (0, options.err_quoting_style, depthstr));
      /* NOTREACHED */
      return false;
    }
  /* missing argument */
  return false;
}


static bool
parse_maxdepth (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_depthspec (entry, argv, arg_ptr, &options.maxdepth);
}

static bool
parse_mindepth (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_depthspec (entry, argv, arg_ptr, &options.mindepth);
}


static bool
do_parse_xmin (const struct parser_table* entry,
               char **argv,
               int *arg_ptr,
               enum xval xv)
{
  const char *minutes;
  const int saved_argc = *arg_ptr;

  if (collect_arg (argv, arg_ptr, &minutes))
    {
      struct time_val tval;
      struct timespec origin = options.cur_day_start;
      tval.xval = xv;
      origin.tv_sec += DAYSECS;
      if (get_relative_timestamp (minutes, &tval, origin, 60,
                                  "arithmetic overflow while converting %s "
                                  "minutes to a number of seconds"))
        {
          struct predicate *our_pred = insert_primary (entry, minutes);
          our_pred->args.reftime = tval;
          our_pred->est_success_rate = estimate_timestamp_success_rate (tval.ts.tv_sec);
          return true;
        }
      else
        {
          /* Don't consume the invalid argument. */
          *arg_ptr = saved_argc;
        }
    }
  return false;
}
static bool
parse_amin (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return do_parse_xmin (entry, argv, arg_ptr, XVAL_ATIME);
}

static bool
parse_cmin (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return do_parse_xmin (entry, argv, arg_ptr, XVAL_CTIME);
}


static bool
parse_mmin (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return do_parse_xmin (entry, argv, arg_ptr, XVAL_MTIME);
}

static bool
parse_name (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *name;
  fnmatch_sanitycheck ();
  if (collect_arg (argv, arg_ptr, &name))
    {
      struct predicate *our_pred;
      check_name_arg ("-name", "-wholename", name);

      our_pred = insert_primary (entry, name);
      our_pred->need_stat = our_pred->need_type = false;
      our_pred->args.str = name;
      our_pred->est_success_rate = estimate_pattern_match_rate (name, 0);
      return true;
    }
  return false;
}

static bool
parse_negate (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) &argv;
  (void) &arg_ptr;

  our_pred = get_new_pred_chk_op (entry, NULL);
  our_pred->pred_func = pred_negate;
  our_pred->p_type = UNI_OP;
  our_pred->p_prec = NEGATE_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  return true;
}

static bool
parse_newer (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  struct stat stat_newer;
  const char *arg;

  set_stat_placeholders (&stat_newer);
  if (collect_arg_stat_info (argv, arg_ptr, &stat_newer, &arg))
    {
      our_pred = insert_primary (entry, arg);
      our_pred->args.reftime.ts = get_stat_mtime (&stat_newer);
      our_pred->args.reftime.xval = XVAL_MTIME;
      our_pred->args.reftime.kind = COMP_GT;
      our_pred->est_success_rate = estimate_timestamp_success_rate (stat_newer.st_mtime);
      return true;
    }
  return false;
}


static bool
parse_newerXY (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) argv;
  (void) arg_ptr;

  if ((argv == NULL) || (argv[*arg_ptr] == NULL))
    {
      return false;
    }
  else if (8u != strlen (argv[*arg_ptr]))
    {
      return false;
    }
  else
    {
      char x, y;
      const char validchars[] = "aBcmt";

      assert (0 == strncmp ("-newer", argv[*arg_ptr], 6));
      x = argv[*arg_ptr][6];
      y = argv[*arg_ptr][7];


#if !defined HAVE_STRUCT_STAT_ST_BIRTHTIME && !defined HAVE_STRUCT_STAT_ST_BIRTHTIMENSEC && !defined HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC && !defined HAVE_STRUCT_STAT_ST_BIRTHTIM_TV_NSEC
      if ('B' == x || 'B' == y)
        {
          error (0, 0,
                 _("This system does not provide a way to find the birth time of a file."));
          return false;
        }
#endif

      /* -newertY (for any Y) is invalid. */
      if (x == 't'
          || (NULL == strchr (validchars, x))
          || (NULL == strchr ( validchars, y)))
        {
          return false;
        }
      else
        {
          struct predicate *our_pred;

          /* Because this item is ARG_SPECIAL_PARSE, we have to advance arg_ptr
           * past the test name (for most other tests, this is already done)
           */
          if (argv[1+*arg_ptr] == NULL)
            {
              error (EXIT_FAILURE, 0, _("The %s test needs an argument"),
                     quotearg_n_style (0, options.err_quoting_style, argv[*arg_ptr]));
            }
          else
            {
              (*arg_ptr)++;
            }

          our_pred = insert_primary (entry, argv[*arg_ptr]);


          switch (x)
            {
            case 'a':
              our_pred->args.reftime.xval = XVAL_ATIME;
              break;
            case 'B':
              our_pred->args.reftime.xval = XVAL_BIRTHTIME;
              break;
            case 'c':
              our_pred->args.reftime.xval = XVAL_CTIME;
              break;
            case 'm':
              our_pred->args.reftime.xval = XVAL_MTIME;
              break;
            default:
              assert (strchr (validchars, x));
              assert (0);
            }

          if ('t' == y)
            {
              if (!parse_datetime (&our_pred->args.reftime.ts,
                                   argv[*arg_ptr],
                                   &options.start_time))
                {
                  error (EXIT_FAILURE, 0,
                         _("I cannot figure out how to interpret %s as a date or time"),
                         quotearg_n_style (0, options.err_quoting_style, argv[*arg_ptr]));
                }
            }
          else
            {
              struct stat stat_newer;

              /* Stat the named file. */
              set_stat_placeholders (&stat_newer);
              if ((*options.xstat) (argv[*arg_ptr], &stat_newer))
                fatal_target_file_error (errno, argv[*arg_ptr]);

              if (!get_stat_Ytime (&stat_newer, y, &our_pred->args.reftime.ts))
                {
                  /* We cannot extract a timestamp from the struct stat. */
                  error (EXIT_FAILURE, 0,
                         _("Cannot obtain birth time of file %s"),
                         safely_quote_err_filename (0, argv[*arg_ptr]));
                }
            }
          our_pred->args.reftime.kind = COMP_GT;
          our_pred->est_success_rate = estimate_timestamp_success_rate (our_pred->args.reftime.ts.tv_sec);
          (*arg_ptr)++;

          assert (our_pred->pred_func != NULL);
          assert (our_pred->pred_func == pred_newerXY);
          assert (our_pred->need_stat);
          return true;
        }
    }
}


static bool
parse_noleaf (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.no_leaf_check = true;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_nogroup (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) &argv;
  (void) &arg_ptr;

  our_pred = insert_primary (entry, NULL);
  our_pred->est_success_rate = 1e-4;
  return true;
}

static bool
parse_nouser (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  (void) argv;
  (void) arg_ptr;


  our_pred = insert_primary_noarg (entry);
  our_pred->est_success_rate = 1e-3;
  return true;
}

static bool
parse_nowarn (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.warnings = false;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_ok (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_exec_ok ("-ok", entry, argv, arg_ptr);
}

static bool
parse_okdir (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_exec_ok ("-okdir", entry, argv, arg_ptr);
}

bool
parse_openparen (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = get_new_pred_chk_op (entry, NULL);
  our_pred->pred_func = pred_openparen;
  our_pred->p_type = OPEN_PAREN;
  our_pred->p_prec = NO_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  return true;
}

static bool
parse_or (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = get_new_pred_noarg (entry);
  our_pred->pred_func = pred_or;
  our_pred->p_type = BI_OP;
  our_pred->p_prec = OR_PREC;
  our_pred->need_stat = our_pred->need_type = false;
  return true;
}

static bool
is_feasible_path_argument (const char *arg, bool foldcase)
{
  const char *last = strrchr (arg, '/');
  if (last && !last[1])
    {
      /* The name ends with "/". */
      if (matches_start_point (arg, foldcase))
        {
          /* "-path foo/" can succeed if one of the start points is "foo/". */
          return true;
        }
      else
        {
          return false;
        }
    }
  return true;
}


static bool
insert_path_check (const struct parser_table* entry, char **argv, int *arg_ptr,
                   const char *predicate_name, PREDICATEFUNCTION pred)
{
  const char *name;
  bool foldcase = false;

  if (pred == pred_ipath)
    foldcase = true;

  fnmatch_sanitycheck ();

  if (collect_arg (argv, arg_ptr, &name))
    {
      struct predicate *our_pred = insert_primary_withpred (entry, pred, name);
      our_pred->need_stat = our_pred->need_type = false;
      our_pred->args.str = name;
      our_pred->est_success_rate = estimate_pattern_match_rate (name, 0);

      if (!options.posixly_correct
          && !is_feasible_path_argument (name, foldcase))
        {
          error (0, 0, _("warning: -%s %s will not match anything "
                         "because it ends with /."),
                 predicate_name, name);
          our_pred->est_success_rate = 1.0e-8;
        }
      return true;
    }
  return false;
}

/* For some time, -path was deprecated (at RMS's request) in favour of
 * -iwholename.  See the node "GNU Manuals" in standards.texi for the
 * rationale for this (basically, GNU prefers the use of the phrase
 * "file name" to "path name".
 *
 * We do not issue a warning that this usage is deprecated
 * since it is standardized since POSIX 2008.
 */
static bool
parse_path (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_path_check (entry, argv, arg_ptr, "path", pred_path);
}

static bool
parse_wholename (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_path_check (entry, argv, arg_ptr, "wholename", pred_path);
}

/* -ipath was deprecated (at RMS's request) in favour of
 * -iwholename.   See the node "GNU Manuals" in standards.texi
 * for the rationale for this (basically, GNU prefers the use
 * of the phrase "file name" to "path name".
 * However, -path is now standardised so I un-deprecated -ipath.
 */
static bool
parse_ipath (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_path_check (entry, argv, arg_ptr, "ipath", pred_ipath);
}

static bool
parse_iwholename (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_path_check (entry, argv, arg_ptr, "iwholename", pred_ipath);
}


static bool
parse_perm (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  mode_t perm_val[2];
  float rate;
  int mode_start = 0;
  enum permissions_type kind = PERM_EXACT;
  struct mode_change *change;
  struct predicate *our_pred;
  const char *perm_expr;

  if (!collect_arg (argv, arg_ptr, &perm_expr))
    return false;

  switch (perm_expr[0])
    {
    case '-':
      mode_start = 1;
      kind = PERM_AT_LEAST;
      rate = 0.2;
      break;

    case '/':                   /* GNU extension */
      mode_start = 1;
      kind = PERM_ANY;
      rate = 0.3;
      break;

    default:
      /* For example, '-perm 0644', which is valid and matches
       * only files whose mode is exactly 0644.
       */
      mode_start = 0;
      kind = PERM_EXACT;
      rate = 0.01;
      break;
    }

  change = mode_compile (perm_expr + mode_start);

  /* Reject invalid modes, or modes of the form +NUMERICMODE.
     The latter were formerly accepted as a GNU extension, but that
     extension was incompatible with how GNU 'chmod' treats these modes now,
     and it would be confusing if 'find' continued to support it.  */
  if (NULL == change
      || (perm_expr[0] == '+' && '0' <= perm_expr[1] && perm_expr[1] < '8'))
    {
      error (EXIT_FAILURE, 0, _("invalid mode %s"),
             quotearg_n_style (0, options.err_quoting_style, perm_expr));
    }
  perm_val[0] = mode_adjust (0, false, 0, change, NULL);
  perm_val[1] = mode_adjust (0, true, 0, change, NULL);
  free (change);

  if (('/' == perm_expr[0]) && (0 == perm_val[0]) && (0 == perm_val[1]))
    {
      /* The meaning of -perm /000 will change in the future.  It
       * currently matches no files, but like -perm -000 it should
       * match all files.
       *
       * Starting in 2005, we used to issue a warning message
       * informing the user that the behaviour would change in the
       * future.  We have now changed the behaviour and issue a
       * warning message that the behaviour recently changed.
       */
      error (0, 0,
             _("warning: you have specified a mode pattern %s (which is "
               "equivalent to /000). The meaning of -perm /000 has now been "
               "changed to be consistent with -perm -000; that is, while it "
               "used to match no files, it now matches all files."),
             perm_expr);

      kind = PERM_AT_LEAST;

      /* The "magic" number below is just the fraction of files on my
       * own system that "-type l -xtype l" fails for (i.e. unbroken symlinks).
       * Actual totals are 1472 and 1073833.
       */
      rate = 0.9986; /* probably matches anything but a broken symlink */
    }

  our_pred = insert_primary (entry, perm_expr);
  our_pred->est_success_rate = rate;
  our_pred->args.perm.kind = kind;
  memcpy (our_pred->args.perm.val, perm_val, sizeof perm_val);
  return true;
}

bool
parse_print (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = insert_primary_noarg (entry);
  /* -print has the side effect of printing.  This prevents us
     from doing undesired multiple printing when the user has
     already specified -print. */
  our_pred->side_effects = our_pred->no_default_print = true;
  our_pred->need_stat = our_pred->need_type = false;
  open_stdout (&our_pred->args.printf_vec);
  return true;
}

static bool
parse_print0 (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  (void) argv;
  (void) arg_ptr;
  return insert_fprint (entry, NULL);
}

static bool
parse_printf (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  char *format;
  const int saved_argc = *arg_ptr;

  if (collect_arg_nonconst (argv, arg_ptr, &format))
    {
      struct format_val fmt;
      open_stdout (&fmt);
      if (insert_fprintf (&fmt, entry, format))
        {
          return true;
        }
      else
        {
          *arg_ptr = saved_argc; /* don't consume the invalid argument. */
          return false;
        }
    }
  return false;
}

static bool
parse_fprintf (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *filename;
  char *format;
  int saved_argc = *arg_ptr;

  if (collect_arg (argv, arg_ptr, &filename))
    {
      if (collect_arg_nonconst (argv, arg_ptr, &format))
        {
          struct format_val fmt;
          open_output_file (filename, &fmt);
          saved_argc = *arg_ptr;

          if (insert_fprintf (&fmt, entry, format))
            return true;
        }
    }
  *arg_ptr = saved_argc; /* don't consume the invalid argument. */
  return false;
}

static bool
parse_prune (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = insert_primary_noarg (entry);
  if (options.do_dir_first == false)
    our_pred->need_stat = our_pred->need_type = false;
  /* -prune has a side effect that it does not descend into
     the current directory. */
  our_pred->side_effects = true;
  our_pred->no_default_print = false;
  return true;
}

static bool
parse_quit  (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred = insert_primary_noarg (entry);
  (void) argv;
  (void) arg_ptr;
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->side_effects = true; /* Exiting is a side effect... */
  our_pred->no_default_print = false; /* Don't inhibit the default print, though. */
  our_pred->est_success_rate = 1.0f;
  return true;
}


static bool
parse_regextype (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *type_name;
  if (collect_arg (argv, arg_ptr, &type_name))
    {
      /* collect the regex type name */
      options.regex_options = get_regex_type (type_name);
      return parse_noop (entry, argv, arg_ptr);
    }
  return false;
}


static bool
parse_regex (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_regex (argv, arg_ptr, entry, options.regex_options);
}

static bool
insert_regex (char **argv,
              int *arg_ptr,
              const struct parser_table *entry,
              int regex_options)
{
  const char *rx;
  if (collect_arg (argv, arg_ptr, &rx))
    {
      struct re_pattern_buffer *re;
      const char *error_message;
      struct predicate *our_pred = insert_primary_withpred (entry, pred_regex, rx);
      our_pred->need_stat = our_pred->need_type = false;
      re = xmalloc (sizeof (struct re_pattern_buffer));
      our_pred->args.regex = re;
      re->allocated = 100;
      re->buffer = xmalloc (re->allocated);
      re->fastmap = NULL;

      re_set_syntax (regex_options);
      re->syntax = regex_options;
      re->translate = NULL;

      error_message = re_compile_pattern (rx, strlen (rx), re);
      if (error_message)
        {
           error (EXIT_FAILURE, 0,
                 _("failed to compile regular expression '%s': %s"),
                 rx, error_message);
        }
      our_pred->est_success_rate = estimate_pattern_match_rate (rx, 1);
      return true;
    }
  return false;
}

static bool
parse_size (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  char *arg;
  uintmax_t num;
  char suffix;
  enum comparison_type c_type;

  int blksize = 512;
  int len;

  /* XXX: cannot (yet) convert to use collect_arg() as this
   * function modifies the args in-place.
   */
  if ((argv == NULL) || (argv[*arg_ptr] == NULL))
    return false;
  arg = argv[*arg_ptr];

  len = strlen (arg);
  if (len == 0)
    error (EXIT_FAILURE, 0, _("invalid null argument to -size"));

  suffix = arg[len - 1];
  switch (suffix)
    {
    case 'b':
      blksize = 512;
      arg[len - 1] = '\0';
      break;

    case 'c':
      blksize = 1;
      arg[len - 1] = '\0';
      break;

    case 'k':
      blksize = 1024;
      arg[len - 1] = '\0';
      break;

    case 'M':                   /* Mebibytes */
      blksize = 1024*1024;
      arg[len - 1] = '\0';
      break;

    case 'G':                   /* Gibibytes */
      blksize = 1024*1024*1024;
      arg[len - 1] = '\0';
      break;

    case 'w':
      blksize = 2;
      arg[len - 1] = '\0';
      break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      suffix = 0;
      break;

    default:
      error (EXIT_FAILURE, 0,
             _("invalid -size type `%c'"), argv[*arg_ptr][len - 1]);
    }
  /* TODO: accept fractional mebibytes etc. ? */
  if (!get_num (arg, &num, &c_type))
    {
      char tail[2];
      tail[0] = suffix;
      tail[1] = 0;

      error (EXIT_FAILURE, 0,
             _("Invalid argument `%s%s' to -size"),
             arg, tail);
      return false;
    }
  our_pred = insert_primary (entry, arg);
  our_pred->args.size.kind = c_type;
  our_pred->args.size.blocksize = blksize;
  our_pred->args.size.size = num;
  our_pred->need_stat = true;
  our_pred->need_type = false;

  if (COMP_GT == c_type)
    our_pred->est_success_rate = (num*blksize > 20480) ? 0.1 : 0.9;
  else if (COMP_LT == c_type)
    our_pred->est_success_rate = (num*blksize > 20480) ? 0.9 : 0.1;
  else
    our_pred->est_success_rate = 0.01;

  (*arg_ptr)++;
  return true;
}


static bool
parse_samefile (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  /* General idea: stat the file, remember device and inode numbers.
   * If a candidate file matches those, it's the same file.
   */
  struct predicate *our_pred;
  struct stat st, fst;
  int fd, openflags;
  const char *filename;

  set_stat_placeholders (&st);
  if (!collect_arg_stat_info (argv, arg_ptr, &st, &filename))
    return false;

  set_stat_placeholders (&fst);
  /* POSIX systems are free to re-use the inode number of a deleted
   * file.  To ensure that we are not fooled by inode reuse, we hold
   * the file open if we can.  This would prevent the system reusing
   * the file.
   */
  fd = -3;                      /* -3 means uninitialized */
  openflags = O_RDONLY;

  if (options.symlink_handling == SYMLINK_NEVER_DEREF)
    {
      if (options.open_nofollow_available)
        {
          assert (O_NOFOLLOW != 0);
          openflags |= O_NOFOLLOW;
          fd = -1;              /* safe to open it. */
        }
      else
        {
          if (S_ISLNK(st.st_mode))
            {
              /* no way to ensure that a symlink will not be followed
               * by open(2), so fall back on using lstat().  Accept
               * the risk that the named file will be deleted and
               * replaced with another having the same inode.
               *
               * Avoid opening the file.
               */
              fd = -2;          /* Do not open it */
            }
          else
            {
              fd = -1;
              /* Race condition here: the file might become a symlink here. */
            }
        }
    }
  else
    {
      /* We want to dereference the symlink anyway */
      fd = -1;                  /* safe to open it without O_NOFOLLOW */
    }

  assert (fd != -3);            /* check we made a decision */
  if (fd == -1)
    {
      /* Race condition here.  The file might become a
       * symbolic link in between our call to stat and
       * the call to open_cloexec.
       */
      fd = open_cloexec (filename, openflags);

      if (fd >= 0)
        {
          /* We stat the file again here to prevent a race condition
           * between the first stat and the call to open(2).
           */
          if (0 != fstat (fd, &fst))
            {
              fatal_target_file_error (errno, filename);
            }
          else
            {
              /* Worry about the race condition.  If the file became a
               * symlink after our first stat and before our call to
               * open, fst may contain the stat information for the
               * destination of the link, not the link itself.
               */
              if ((*options.xstat) (filename, &st))
                fatal_target_file_error (errno, filename);

              if ((options.symlink_handling == SYMLINK_NEVER_DEREF)
                  && (!options.open_nofollow_available))
                {
                  if (S_ISLNK(st.st_mode))
                    {
                      /* We lost the race.  Leave the data in st.  The
                       * file descriptor points to the wrong thing.
                       */
                      close (fd);
                      fd = -1;
                    }
                  else
                    {
                      /* Several possibilities here:
                       * 1. There was no race
                       * 2. The file changed into a symlink after the stat and
                       *    before the open, and then back into a non-symlink
                       *    before the second stat.
                       *
                       * In case (1) there is no problem.  In case (2),
                       * the stat() and fstat() calls will have returned
                       * different data.  O_NOFOLLOW was not available,
                       * so the open() call may have followed a symlink
                       * even if the -P option is in effect.
                       */
                      if ((st.st_dev == fst.st_dev)
                          && (st.st_ino == fst.st_ino))
                        {
                          /* No race.  No need to copy fst to st,
                           * since they should be identical (modulo
                           * differences in padding bytes).
                           */
                        }
                      else
                        {
                          /* We lost the race.  Leave the data in st.  The
                           * file descriptor points to the wrong thing.
                           */
                          close (fd);
                          fd = -1;
                        }
                    }
                }
              else
                {
                  st = fst;
                }
            }
        }
    }

  our_pred = insert_primary (entry, filename);
  our_pred->args.samefileid.ino = st.st_ino;
  our_pred->args.samefileid.dev = st.st_dev;
  our_pred->args.samefileid.fd  = fd;
  our_pred->need_type = false;
  /* smarter way: compare type and inode number first. */
  /* TODO: maybe optimize this away by being optimistic */
  our_pred->need_stat = true;
  our_pred->est_success_rate = 0.01f;
  return true;
}

static bool
parse_true (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  (void) argv;
  (void) arg_ptr;

  our_pred = insert_primary_noarg (entry);
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->est_success_rate = 1.0f;
  return true;
}

static bool
parse_noop (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  return parse_true (get_noop (), argv, arg_ptr);
}

static bool
parse_accesscheck (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  (void) argv;
  (void) arg_ptr;
  our_pred = insert_primary_noarg (entry);
  our_pred->need_stat = our_pred->need_type = false;
  our_pred->side_effects = our_pred->no_default_print = false;
  if (pred_is(our_pred, pred_executable))
    our_pred->est_success_rate = 0.2;
  else
    our_pred->est_success_rate = 0.9;
  return true;
}

static bool
parse_type (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_type (argv, arg_ptr, entry, pred_type);
}

static bool
parse_uid (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *p = insert_num (argv, arg_ptr, entry);
  if (p)
    {
      p->est_success_rate = (p->args.numinfo.l_val < 100) ? 0.99 : 0.2;
      return true;
    }
  return false;
}

static bool
parse_used (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;
  struct time_val tval;
  const char *offset_str;
  const char *errmsg = "arithmetic overflow while converting %s days to a number of seconds";

  if (collect_arg (argv, arg_ptr, &offset_str))
    {
      /* The timespec is actually a delta value, so we use an origin of 0. */
      struct timespec zero = {0,0};
      if (get_relative_timestamp (offset_str, &tval, zero, DAYSECS, errmsg))
        {
          our_pred = insert_primary (entry, offset_str);
          our_pred->args.reftime = tval;
          our_pred->est_success_rate = estimate_file_age_success_rate (tval.ts.tv_sec / DAYSECS);
          return true;
        }
      else
        {
          error (EXIT_FAILURE, 0,
                 _("Invalid argument %s to -used"), offset_str);
          /*NOTREACHED*/
          return false;
        }
    }
  else
    {
      return false;             /* missing argument */
    }
}

static bool
parse_user (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  const char *username;

  if (collect_arg (argv, arg_ptr, &username))
    {
      struct predicate *our_pred;
      uid_t uid;
      struct passwd *cur_pwd = getpwnam (username);
      endpwent ();
      if (cur_pwd != NULL)
        {
          uid = cur_pwd->pw_uid;
        }
      else
        {
          uintmax_t num;
          if ((xstrtoumax (username, NULL, 10, &num, "") != LONGINT_OK)
                || (UID_T_MAX < num))
            {
              error (EXIT_FAILURE, 0,
                     _("invalid user name or UID argument to -user: %s"),
                     quotearg_n_style (0, options.err_quoting_style,
                                       username));
            }
          uid = num;
        }
      our_pred = insert_primary (entry, username);
      our_pred->args.uid = uid;
      our_pred->est_success_rate = (our_pred->args.uid < 100) ? 0.99 : 0.2;
      return true;
    }
  return false;
}

static bool
parse_version (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  (void) entry;
  (void) argv;
  (void) arg_ptr;

  display_findutils_version ("find");
  printf (_("Features enabled: "));

#if CACHE_IDS
  printf ("CACHE_IDS(ignored) ");
#endif
#if defined HAVE_STRUCT_DIRENT_D_TYPE
  printf ("D_TYPE ");
#endif
#if defined O_NOFOLLOW
  printf ("O_NOFOLLOW(%s) ",
          (options.open_nofollow_available ? "enabled" : "disabled"));
#endif
#if defined LEAF_OPTIMISATION
  printf ("LEAF_OPTIMISATION ");
#endif
  if (0 < is_selinux_enabled ())
    {
      printf ("SELINUX ");
    }

  if (is_fts_cwdfd_enabled ())
    {
      printf ("FTS(FTS_CWDFD) ");
    }
  else
    {
      printf ("FTS() ");
    }

  printf ("CBO(level=%d) ", (int)(options.optimisation_level));
  printf ("\n");

  exit (EXIT_SUCCESS);
}

static bool
parse_context (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  struct predicate *our_pred;

  if ((argv == NULL) || (argv[*arg_ptr] == NULL))
    return false;

  if (is_selinux_enabled () <= 0)
    {
      error (EXIT_FAILURE, 0,
             _("invalid predicate -context: SELinux is not enabled."));
      return false;
    }
  our_pred = insert_primary (entry, NULL);
  our_pred->est_success_rate = 0.01f;
  our_pred->need_stat = false;
  our_pred->args.scontext = argv[*arg_ptr];

  (*arg_ptr)++;
  return true;
}

static bool
parse_xdev (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.stay_on_filesystem = true;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_ignore_race (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.ignore_readdir_race = true;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_noignore_race (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.ignore_readdir_race = false;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_warn (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  options.warnings = true;
  return parse_noop (entry, argv, arg_ptr);
}

static bool
parse_xtype (const struct parser_table* entry, char **argv, int *arg_ptr)
{
  return insert_type (argv, arg_ptr, entry, pred_xtype);
}

static bool
insert_type (char **argv, int *arg_ptr,
             const struct parser_table *entry,
             PRED_FUNC which_pred)
{
  struct predicate *our_pred;
  const char *typeletter;
  const char *pred_string = which_pred == pred_xtype ? "-xtype" : "-type";

  if (! collect_arg (argv, arg_ptr, &typeletter))
    return false;

  if (!*typeletter)
    {
      error (EXIT_FAILURE, 0,
             _("Arguments to %s should contain at least one letter"),
             pred_string);
      /*NOTREACHED*/
      return false;
    }

  our_pred = insert_primary_withpred (entry, which_pred, typeletter);
  our_pred->est_success_rate = 0.0;

  /* Figure out if we will need to stat the file, because if we don't
   * need to follow symlinks, we can avoid a stat call by using
   * struct dirent.d_type.
   */
  if (which_pred == pred_xtype)
    {
      our_pred->need_stat = true;
      our_pred->need_type = false;
    }
  else
    {
      our_pred->need_stat = false; /* struct dirent is enough */
      our_pred->need_type = true;
    }

  /* From a real system here are the counts of files by type:
     Type   Count  Fraction
     f    4410884  0.875
     d     464722  0.0922
     l     156662  0.0311
     b       4476  0.000888
     c       2233  0.000443
     s         80  1.59e-05
     p         38  7.54e-06
  */

  for (; *typeletter; )
    {
      unsigned int type_cell;
      float rate = 0.01;

      switch (*typeletter)
      {
      case 'b':                 /* block special */
        type_cell = FTYPE_BLK;
        rate = 0.000888f;
        break;
      case 'c':                 /* character special */
        type_cell = FTYPE_CHR;
        rate = 0.000443f;
        break;
      case 'd':                 /* directory */
        type_cell = FTYPE_DIR;
        rate = 0.0922f;
        break;
      case 'f':                 /* regular file */
        type_cell = FTYPE_REG;
        rate = 0.875f;
        break;
      case 'l':                 /* symbolic link */
#ifdef S_IFLNK
        type_cell = FTYPE_LNK;
        rate = 0.0311f;
#else
        type_cell = 0;
        error (EXIT_FAILURE, 0,
               _("%s %c is not supported because symbolic links "
                 "are not supported on the platform find was compiled on."),
               pred_string, (*typeletter));
#endif
        break;
      case 'p':                 /* pipe */
#ifdef S_IFIFO
        type_cell = FTYPE_FIFO;
        rate = 7.554e-6f;
#else
        type_cell = 0;
        error (EXIT_FAILURE, 0,
               _("%s %c is not supported because FIFOs "
                 "are not supported on the platform find was compiled on."),
               pred_string, (*typeletter));
#endif
        break;
      case 's':                 /* socket */
#ifdef S_IFSOCK
        type_cell = FTYPE_SOCK;
        rate = 1.59e-5f;
#else
        type_cell = 0;
        error (EXIT_FAILURE, 0,
               _("%s %c is not supported because named sockets "
                 "are not supported on the platform find was compiled on."),
               pred_string, (*typeletter));
#endif
        break;
      case 'D':                 /* Solaris door */
#ifdef S_IFDOOR
        type_cell = FTYPE_DOOR;
        /* There are no Solaris doors on the example system surveyed
         * above, but if someone uses -type D, they are presumably
         * expecting to find a non-zero number.  We guess at a
         * rate. */
        rate = 1.0e-5f;
#else
        type_cell = 0;
        error (EXIT_FAILURE, 0,
               _("%s %c is not supported because Solaris doors "
                 "are not supported on the platform find was compiled on."),
               pred_string, (*typeletter));
#endif
        break;
      default:                  /* None of the above ... nuke 'em. */
        type_cell = 0;
        error (EXIT_FAILURE, 0,
               _("Unknown argument to %s: %c"), pred_string, (*typeletter));
        /*NOTREACHED*/
        return false;
      }

      if (our_pred->args.types[type_cell])
        {
          error (EXIT_FAILURE, 0,
                 _("Duplicate file type '%c' in the argument list to %s."),
                 (*typeletter), pred_string);
        }

      our_pred->est_success_rate += rate;
      our_pred->args.types[type_cell] = true;

      /* Advance.
       * Currently, only 1-character file types separated by ',' are supported.
       */
      typeletter++;
      if (*typeletter)
        {
          if (*typeletter != ',')
            {
              error (EXIT_FAILURE, 0,
                     _("Must separate multiple arguments to %s using: ','"),
                     pred_string);
              /*NOTREACHED*/
              return false;
            }
          typeletter++;
          if (!*typeletter)
            {
              error (EXIT_FAILURE, 0,
                     _("Last file type in list argument to %s "
                       "is missing, i.e., list is ending on: ','"),
                     pred_string);
              /*NOTREACHED*/
              return false;
            }
        }
    }

  return true;
}


/* Return true if the file accessed via FP is a terminal.
 */
static bool
stream_is_tty (FILE *fp)
{
  int fd = fileno (fp);
  if (-1 == fd)
    {
      return false; /* not a valid stream */
    }
  else
    {
      return isatty (fd) ? true : false;
    }

}






static void
check_path_safety (const char *action)
{
  const char *path = getenv ("PATH");
  const char *path_separators = ":";
  size_t pos, len;

  if (NULL == path)
    {
      /* $PATH is not set.  Assume the OS default is safe.
       * That may not be true on Windows, but I'm not aware
       * of a way to get Windows to avoid searching the
       * current directory anyway.
       */
      return;
    }

  splitstring (path, path_separators, true, &pos, &len);
  do
    {
      if (0 == len || (1 == len && path[pos] == '.'))
        {
          /* empty field signifies . */
          error (EXIT_FAILURE, 0,
                 _("The current directory is included in the PATH "
                   "environment variable, which is insecure in "
                   "combination with the %s action of find.  "
                   "Please remove the current directory from your "
                   "$PATH (that is, remove \".\", doubled colons, "
                   "or leading or trailing colons)"),
                 action);
        }
      else if (path[pos] != '/')
        {
          char *relpath = strndup (&path[pos], len);
          error (EXIT_FAILURE, 0,
                 _("The relative path %s is included in the PATH "
                   "environment variable, which is insecure in "
                   "combination with the %s action of find.  "
                   "Please remove that entry from $PATH"),
                 safely_quote_err_filename (0, relpath ? relpath : &path[pos]),
                 action);
          /*NOTREACHED*/
          free (relpath);
        }
    } while (splitstring (path, path_separators, false, &pos, &len));
}


/* handles both exec and ok predicate */
static bool
insert_exec_ok (const char *action,
                const struct parser_table *entry,
                char **argv,
                int *arg_ptr)
{
  int start, end;               /* Indexes in ARGV of start & end of cmd. */
  int i;                        /* Index into cmd args */
  bool prev_was_braces_only;    /* Previous arg was '{}' (not e.g. 'Q' or '{}x'). */
  bool allow_plus;              /* True if + is a valid terminator */
  int brace_count;              /* Number of instances of {}. */
  const char *brace_arg;        /* Which arg did {} appear in? */
  PRED_FUNC func = entry->pred_func;
  enum BC_INIT_STATUS bcstatus;

  struct predicate *our_pred;
  struct exec_val *execp;       /* Pointer for efficiency. */

  if ((argv == NULL) || (argv[*arg_ptr] == NULL))
    return false;

  our_pred = insert_primary_withpred (entry, func, "(some -exec* arguments)");
  our_pred->side_effects = our_pred->no_default_print = true;
  our_pred->need_type = our_pred->need_stat = false;

  assert(predicate_uses_exec (our_pred));
  execp = &our_pred->args.exec_vec;
  execp->wd_for_exec = NULL;

  if ((func != pred_okdir) && (func != pred_ok))
    {
      allow_plus = true;
      execp->close_stdin = false;
    }
  else
    {
      allow_plus = false;
      /* The -ok* family need user confirmations via stdin.  */
      options.ok_prompt_stdin = true;
      /* If find reads stdin (i.e. for -ok and similar), close stdin
       * in the child to prevent some script from consuming the output
       * intended for find.
       */
      execp->close_stdin = true;
    }


  if ((func == pred_execdir) || (func == pred_okdir))
    {
      execp->wd_for_exec = NULL;
      options.ignore_readdir_race = false;
      check_path_safety (action);
    }
  else
    {
      assert (NULL != initial_wd);
      execp->wd_for_exec = initial_wd;
    }

  our_pred->args.exec_vec.multiple = 0;

  /* Count the number of args with path replacements, up until the ';'.
   * Also figure out if the command is terminated by ";" or by "+".
   */
  start = *arg_ptr;
  for (end = start, prev_was_braces_only=false, brace_count=0, brace_arg=NULL;
       (argv[end] != NULL)
       && ((argv[end][0] != ';') || (argv[end][1] != '\0'));
       end++)
    {
      /* For -exec and -execdir, "{} +" can terminate the command. */
      if (allow_plus && prev_was_braces_only
           && argv[end][0] == '+' && argv[end][1] == 0)
        {
          our_pred->args.exec_vec.multiple = 1;
          break;
        }

      prev_was_braces_only = false;
      if (mbsstr (argv[end], "{}"))
        {
          if (0 == strcmp(argv[end], "{}"))
            {
              /* Savannah bug 66365: + only terminates the predicate
               * immediately after an argument which is exactly, "{}".
               * However, the "{}" in "x{}" should get expanded for
               * the ";" case.
               */
              prev_was_braces_only = true;
            }
          brace_arg = argv[end];
          ++brace_count;

          if (start == end && (func == pred_execdir || func == pred_okdir))
            {
              /* The POSIX standard says that {} replacement should
               * occur even in the utility name.  This is insecure
               * since it means we will be executing a command whose
               * name is chosen according to whatever find finds in
               * the file system.  That can be influenced by an
               * attacker.  Hence for -execdir and -okdir this is not
               * allowed.  We can specify this as those options are
               * not defined by POSIX.
               */
              error (EXIT_FAILURE, 0,
                     _("You may not use {} within the utility name for "
                       "-execdir and -okdir, because this is a potential "
                       "security problem."));
            }
        }
    }

  /* Fail if no command given or no semicolon found. */
  if ((end == start) || (argv[end] == NULL))
    {
      *arg_ptr = end;
      free (our_pred);
      return false;
    }

  if (our_pred->args.exec_vec.multiple)
    {
      const char *suffix;
      if (func == pred_execdir)
        suffix = "dir";
      else
        suffix = "";

      if (brace_count > 1)
        {
          error (EXIT_FAILURE, 0,
                 _("Only one instance of {} is supported with -exec%s ... +"),
                 suffix);
        }
      else if (strlen (brace_arg) != 2u)
        {
          enum { MsgBufSize = 19 };
          char buf[MsgBufSize];
          const size_t needed = snprintf (buf, MsgBufSize, "-exec%s ... {} +", suffix);
          assert (needed <= MsgBufSize);  /* If this assertion fails, correct the value of MsgBufSize. */
          error (EXIT_FAILURE, 0,
                 _("In %s the %s must appear by itself, but you specified %s"),
                 quotearg_n_style (0, options.err_quoting_style, buf),
                 quotearg_n_style (1, options.err_quoting_style, "{}"),
                 quotearg_n_style (2, options.err_quoting_style, brace_arg));
        }
    }

  /* We use a switch statement here so that the compiler warns us when
   * we forget to handle a newly invented enum value.
   *
   * Like xargs, we allow 2KiB of headroom for the launched utility to
   * export its own environment variables before calling something
   * else.
   */
  bcstatus = bc_init_controlinfo (&execp->ctl, 2048u);
  switch (bcstatus)
    {
    case BC_INIT_ENV_TOO_BIG:
    case BC_INIT_CANNOT_ACCOMODATE_HEADROOM:
      error (EXIT_FAILURE, 0, _("The environment is too large for exec()."));
      break;
    case BC_INIT_OK:
      /* Good news.  Carry on. */
      break;
    }
  bc_use_sensible_arg_max (&execp->ctl);


  execp->ctl.exec_callback = launch;

  if (our_pred->args.exec_vec.multiple)
    {
      /* "+" terminator, so we can just append our arguments after the
       * command and initial arguments.
       */
      execp->replace_vec = NULL;
      execp->ctl.replace_pat = NULL;
      execp->ctl.rplen = 0;
      execp->ctl.lines_per_exec = 0; /* no limit */
      execp->ctl.args_per_exec = 0; /* no limit */

      /* remember how many arguments there are */
      execp->ctl.initial_argc = (end-start) - 1;

      /* execp->state = xmalloc(sizeof struct buildcmd_state); */
      bc_init_state (&execp->ctl, &execp->state, execp);

      /* Gather the initial arguments.  Skip the {}. */
      for (i=start; i<end-1; ++i)
        {
          bc_push_arg (&execp->ctl, &execp->state,
                       argv[i], strlen (argv[i])+1,
                       NULL, 0,
                       1);
        }
    }
  else
    {
      /* Semicolon terminator - more than one {} is supported, so we
       * have to do brace-replacement.
       */
      execp->num_args = end - start;

      execp->ctl.replace_pat = "{}";
      execp->ctl.rplen = strlen (execp->ctl.replace_pat);
      execp->ctl.lines_per_exec = 0; /* no limit */
      execp->ctl.args_per_exec = 0; /* no limit */
      execp->replace_vec = xmalloc (sizeof(char*)*execp->num_args);


      /* execp->state = xmalloc(sizeof(*(execp->state))); */
      bc_init_state (&execp->ctl, &execp->state, execp);

      /* Remember the (pre-replacement) arguments for later. */
      for (i=0; i<execp->num_args; ++i)
        {
          execp->replace_vec[i] = argv[i+start];
        }
    }

  if (argv[end] == NULL)
    *arg_ptr = end;
  else
    *arg_ptr = end + 1;

  return true;
}



/* Get a timestamp and comparison type.

   STR is the ASCII representation.
   Set *NUM_DAYS to the number of days/minutes/whatever, taken as being
   relative to ORIGIN (usually the current moment or midnight).
   Thus the sense of the comparison type appears to be reversed.
   Set *COMP_TYPE to the kind of comparison that is requested.
   Issue OVERFLOWMESSAGE if overflow occurs.
   Return true if all okay, false if input error.

   Used by -amin, -cmin, -mmin, -used, -atime, -ctime and -mtime (parsers) to
   get the appropriate information for a time predicate processor. */

static bool
get_relative_timestamp (const char *str,
                        struct time_val *result,
                        struct timespec origin,
                        double sec_per_unit,
                        const char *overflowmessage)
{
  double offset, seconds, nanosec;
  static const long nanosec_per_sec = 1000000000;

  if (get_comp_type (&str, &result->kind))
    {
      /* Invert the sense of the comparison */
      switch (result->kind)
        {
        case COMP_LT: result->kind = COMP_GT; break;
        case COMP_GT: result->kind = COMP_LT; break;
        case COMP_EQ:
          break; /* inversion leaves it unchanged */
        }

      /* Convert the ASCII number into floating-point. */
      if (xstrtod (str, NULL, &offset, strtod))
        {
          if (isnan (offset))
            {
              error (EXIT_FAILURE, 0, _("invalid not-a-number argument: `%s'"),
                     str);
            }

          /* Separate the floating point number the user specified
           * (which is a number of days, or minutes, etc) into an
           * integral number of seconds (SECONDS) and a fraction (NANOSEC).
           */
          nanosec = modf (offset * sec_per_unit, &seconds);
          nanosec *= 1.0e9;     /* convert from fractional seconds to ns. */
          assert (nanosec < nanosec_per_sec);

          /* Perform the subtraction, and then check for overflow.
           * On systems where signed aritmetic overflow does not
           * wrap, this check may be unreliable.   The C standard
           * does not require this approach to work, but I am aware
           * of no platforms where it fails.
           */
          result->ts.tv_sec  = origin.tv_sec - seconds;
          if ((origin.tv_sec < result->ts.tv_sec) != (seconds < 0))
            {
              /* an overflow has occurred. */
              error (EXIT_FAILURE, 0, overflowmessage, str);
            }

          result->ts.tv_nsec = origin.tv_nsec - nanosec;
          if (origin.tv_nsec < nanosec)
            {
              /* Perform a carry operation */
              result->ts.tv_nsec += nanosec_per_sec;
              result->ts.tv_sec  -= 1;
            }
          return true;
        }
      else
        {
          /* Conversion from ASCII to double failed. */
          return false;
        }
    }
  else
    {
      return false;
    }
}

/* Insert a time predicate based on the information in ENTRY.
   ARGV is a pointer to the argument array.
   ARG_PTR is a pointer to an index into the array, incremented if
   all went well.

   Return true if input is valid, false if not.

   A new predicate node is assigned, along with an argument node
   obtained with malloc.

   Used by -atime, -ctime, and -mtime parsers. */

static bool
parse_time (const struct parser_table* entry, char *argv[], int *arg_ptr)
{
  struct predicate *our_pred;
  struct time_val tval;
  enum comparison_type comp;
  const char *timearg, *orig_timearg;
  const char *errmsg = _("arithmetic overflow while converting %s "
                         "days to a number of seconds");
  struct timespec origin;
  const int saved_argc = *arg_ptr;

  if (!collect_arg (argv, arg_ptr, &timearg))
    return false;
  orig_timearg = timearg;

  /* Decide the origin by previewing the comparison type. */
  origin = options.cur_day_start;

  if (get_comp_type (&timearg, &comp))
    {
      /* Remember, we invert the sense of the comparison, so this tests
       * against COMP_LT instead of COMP_GT...
       */
      if (COMP_LT == comp)
        {
          uintmax_t expected = origin.tv_sec + (DAYSECS-1);
          origin.tv_sec += (DAYSECS-1);
          if (expected != (uintmax_t)origin.tv_sec)
            {
              error (EXIT_FAILURE, 0,
                     _("arithmetic overflow when trying to calculate the end of today"));
            }
        }
    }
  /* We discard the value of comp here, as get_relative_timestamp
   * will set tval.kind.  For that to work, we have to restore
   * timearg so that it points to the +/- prefix, if any.  get_comp_type()
   * will have advanced timearg, so we restore it.
   */
  timearg = orig_timearg;

  if (!get_relative_timestamp (timearg, &tval, origin, DAYSECS, errmsg))
    {
      *arg_ptr = saved_argc;    /* don't consume the invalid argument */
      return false;
    }

  our_pred = insert_primary (entry, orig_timearg);
  our_pred->args.reftime = tval;
  our_pred->est_success_rate = estimate_timestamp_success_rate (tval.ts.tv_sec);

  if (options.debug_options & DebugExpressionTree)
    {
      time_t t;

      fprintf (stderr, "inserting %s\n", our_pred->p_name);
      fprintf (stderr, "    type: %s    %s  ",
               (tval.kind == COMP_GT) ? "gt" :
               ((tval.kind == COMP_LT) ? "lt" : ((tval.kind == COMP_EQ) ? "eq" : "?")),
               (tval.kind == COMP_GT) ? " >" :
               ((tval.kind == COMP_LT) ? " <" : ((tval.kind == COMP_EQ) ? ">=" : " ?")));
      t = our_pred->args.reftime.ts.tv_sec;
      fprintf (stderr, "%"PRIuMAX" %s",
               (uintmax_t) our_pred->args.reftime.ts.tv_sec,
               ctime (&t));
      if (tval.kind == COMP_EQ)
        {
          t = our_pred->args.reftime.ts.tv_sec + DAYSECS;
          fprintf (stderr, "                 <  %"PRIuMAX" %s",
                   (uintmax_t) t, ctime (&t));
        }
    }

  return true;
}

/* Get the comparison type prefix (if any) from a number argument.
   The prefix is at *STR.
   Set *COMP_TYPE to the kind of comparison that is requested.
   Advance *STR beyond any initial comparison prefix.

   Return true if all okay, false if input error.  */
static bool
get_comp_type (const char **str, enum comparison_type *comp_type)
{
  switch (**str)
    {
    case '+':
      *comp_type = COMP_GT;
      (*str)++;
      break;
    case '-':
      *comp_type = COMP_LT;
      (*str)++;
      break;
    default:
      *comp_type = COMP_EQ;
      break;
    }
  return true;
}





/* Get a number with comparison information.
   The sense of the comparison information is 'normal'; that is,
   '+' looks for a count > than the number and '-' less than.

   STR is the ASCII representation of the number.
   Set *NUM to the number.
   Set *COMP_TYPE to the kind of comparison that is requested.

   Return true if all okay, false if input error.  */

static bool
get_num (const char *str,
         uintmax_t *num,
         enum comparison_type *comp_type)
{
  char *pend;

  if (str == NULL)
    return false;

  /* Figure out the comparison type if the caller accepts one. */
  if (comp_type)
    {
      if (!get_comp_type (&str, comp_type))
        return false;
    }

  return xstrtoumax (str, &pend, 10, num, "") == LONGINT_OK;
}

/* Insert a number predicate.
   ARGV is a pointer to the argument array.
   *ARG_PTR is an index into ARGV, incremented if all went well.
   *PRED is the predicate processor to insert.

   Return true if input is valid, false if error.

   A new predicate node is assigned, along with an argument node
   obtained with malloc.

   Used by -inum, -uid, -gid and -links parsers. */

static struct predicate *
insert_num (char **argv, int *arg_ptr, const struct parser_table *entry)
{
  const char *numstr;

  if (collect_arg (argv, arg_ptr, &numstr))
  {
    uintmax_t num;
    enum comparison_type c_type;

    if (get_num (numstr, &num, &c_type))
      {
        struct predicate *our_pred = insert_primary (entry, numstr);
        our_pred->args.numinfo.kind = c_type;
        our_pred->args.numinfo.l_val = num;

        if (options.debug_options & DebugExpressionTree)
          {
            fprintf (stderr, "inserting %s\n", our_pred->p_name);
            fprintf (stderr, "    type: %s    %s  ",
                     (c_type == COMP_GT) ? "gt" :
                     ((c_type == COMP_LT) ? "lt" : ((c_type == COMP_EQ) ? "eq" : "?")),
                     (c_type == COMP_GT) ? " >" :
                     ((c_type == COMP_LT) ? " <" : ((c_type == COMP_EQ) ? " =" : " ?")));
            fprintf (stderr, "%"PRIuMAX"\n", our_pred->args.numinfo.l_val);
          }
        return our_pred;
      }
    else
      {
        const char *predicate = argv[(*arg_ptr)-2];
        error (EXIT_FAILURE, 0,
               _("non-numeric argument to %s: %s"),
               predicate,
               quotearg_n_style (0, options.err_quoting_style, numstr));
        /*NOTREACHED*/
        return NULL;
      }
  }
  return NULL;
}

static void
open_output_file (const char *path, struct format_val *p)
{
  p->segment = NULL;
  p->quote_opts = clone_quoting_options (NULL);

  if (!strcmp (path, "/dev/stderr"))
    {
      p->stream = stderr;
      p->filename = _("standard error");
    }
  else if (!strcmp (path, "/dev/stdout"))
    {
      p->stream = stdout;
      p->filename = _("standard output");
    }
  else
    {
      p->stream = sharefile_fopen (state.shared_files, path);
      p->filename = path;

      if (p->stream == NULL)
        {
          fatal_nontarget_file_error (errno, path);
        }
    }

  p->dest_is_tty = stream_is_tty (p->stream);
}

static void
open_stdout (struct format_val *p)
{
  open_output_file ("/dev/stdout", p);
}
