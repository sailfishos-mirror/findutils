#! /bin/sh
# updatedb -- build a locate pathname database
# Copyright (C) 1994-2025 Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# csh original by James Woods; sh conversion by David MacKenzie.

#exec 2> /tmp/updatedb-trace.txt
#set -x

version='
updatedb (@PACKAGE_NAME@) @VERSION@
@COPYRIGHT@
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by Eric B. Decker, James Youngman, and Kevin Dalley.
'

# File path names are not actually text, anyway (since there is no
# mechanism to enforce any constraint that the basename of a
# subdirectory has the same character encoding as the basename of its
# parent).  The practical effect is that, depending on the way a
# particular system is configured and the content of its filesystem,
# passing all the file names in the system through "sort" may generate
# character encoding errors in text-based tools like "sort".  To avoid
# this, we set LC_ALL=C.  This will, presumably, not work perfectly on
# systems where LC_ALL is not the way to do locale configuration or
# some other seting can override this.
LC_ALL=C
export LC_ALL

# We can't use substitution on PACKAGE_URL below because it
# (correctly) points to https://www.gnu.org/software/findutils/ instead
# of the bug reporting page.
usage="\
Usage: $0 [--findoptions='-option1 -option2...']
       [--localpaths='dir1 dir2...'] [--netpaths='dir1 dir2...']
       [--prunepaths='dir1 dir2...'] [--prunefs='fs1 fs2...']
       [--output=dbfile] [--netuser=user] [--localuser=user]
       [--dbformat] [--version] [--help]

Please see also the documentation at @PACKAGE_URL@.
Report (and track progress on fixing) bugs in the updatedb
program via the @PACKAGE_NAME@ bug-reporting page at
@PACKAGE_BUGREPORT_URL@ or, if
you have no web access, by sending email to <@PACKAGE_BUGREPORT@>.
"
changeto=/

for arg
do
  # If we are unable to fork, the back-tick operator will
  # fail (and the shell will emit an error message).  When
  # this happens, we exit with error value 71 (EX_OSERR).
  # Alternative candidate - 75, EX_TEMPFAIL.
  opt=`echo $arg|sed 's/^\([^=]*\).*/\1/'`  || exit 71
  val=`echo $arg|sed 's/^[^=]*=\(.*\)/\1/'` || exit 71
  case "$opt" in
    --findoptions) FINDOPTIONS="$val" ;;
    --localpaths) SEARCHPATHS="$val" ;;
    --netpaths) NETPATHS="$val" ;;
    --prunepaths) PRUNEPATHS="$val" ;;
    --prunefs) PRUNEFS="$val" ;;
    --output) LOCATE_DB="$val" ;;
    --netuser) NETUSER="$val" ;;
    --localuser) LOCALUSER="$val" ;;
    --changecwd)  changeto="$val" ;;
    --dbformat)   dbformat="$val" ;;
    --version) fail=0; echo "$version" || fail=1; exit $fail ;;
    --help)    fail=0; echo "$usage"   || fail=1; exit $fail ;;
    *) echo "updatedb: invalid option $opt
Try '$0 --help' for more information." >&2
       exit 1 ;;
  esac
done

frcode_options=""
case "$dbformat" in
    "")
        # Default, use LOCATE02
        ;;
    LOCATE02)
        ;;
    slocate)
        frcode_options="$frcode_options -S 1"
        ;;
    *)
        # The "old" database format is no longer supported.
        echo "Unsupported locate database format ${dbformat}: Supported formats are:" >&2
        echo "LOCATE02, slocate" >&2
        exit 1
esac


if @SORT_SUPPORTS_Z@
then
    sort="@SORT@ -z"
    print_option="-print0"
    frcode_options="$frcode_options -0"
else
    sort="@SORT@"
    print_option="-print"
fi

getuid() {
    # format of "id" output is ...
    # uid=1(daemon) gid=1(other)
    # for `id's that don't understand -u
    id | cut -d'(' -f 1 | cut -d'=' -f2
}

# figure out if su supports the -s option
select_shell() {
    if su "$1" -s $SHELL -c false < /dev/null  ; then
	# No.
	echo ""
    else
	if su "$1" -s $SHELL -c true < /dev/null  ; then
	    # Yes.
	    echo "-s $SHELL"
        else
	    # su is unconditionally failing.  We won't be able to
	    # figure out what is wrong, so be conservative.
	    echo ""
	fi
    fi
}


# You can set these in the environment, or use command-line options,
# to override their defaults:

# Any global options for find?
: ${FINDOPTIONS=}

# What shell shoud we use?  We should use a POSIX-ish sh.
: ${SHELL="/bin/sh"}

# Non-network directories to put in the database.
: ${SEARCHPATHS="/"}

# Network (NFS, AFS, RFS, etc.) directories to put in the database.
: ${NETPATHS=}

# Directories to not put in the database, which would otherwise be.
: ${PRUNEPATHS="
/afs
/amd
/proc
/sfs
/tmp
/usr/tmp
/var/tmp
"}

# Trailing slashes result in regex items that are never matched, which
# is not what the user will expect.   Therefore we now reject such
# constructs.
for p in $PRUNEPATHS; do
    case "$p" in
	/*/)   echo "$0: $p: pruned paths should not contain trailing slashes" >&2
	       exit 1
    esac
done

# The same, in the form of a regex that find can use.
test -z "$PRUNEREGEX" &&
  PRUNEREGEX=`echo $PRUNEPATHS|sed -e 's,^,\\\(^,' -e 's, ,$\\\)\\\|\\\(^,g' -e 's,$,$\\\),'`

# The database file to build.
: ${LOCATE_DB=@LOCATE_DB@}

# Directory to hold intermediate files.
if test -z "$TMPDIR"; then
  if test -d /var/tmp; then
    : ${TMPDIR=/var/tmp}
  elif test -d /usr/tmp; then
    : ${TMPDIR=/usr/tmp}
  else
    : ${TMPDIR=/tmp}
  fi
fi
export TMPDIR

# The user to search network directories as.
: ${NETUSER=daemon}

# The directory containing the subprograms.
if test -n "$LIBEXECDIR" ; then
    : LIBEXECDIR already set, do nothing
else
    : ${LIBEXECDIR=@libexecdir@}
fi

# The directory containing find.
if test -n "$BINDIR" ; then
    : BINDIR already set, do nothing
else
    : ${BINDIR=@bindir@}
fi

# The names of the utilities to run to build the database.
: ${find:=${BINDIR}/@find@}
: ${frcode:=${LIBEXECDIR}/@frcode@}

make_tempdir () {
    # This implementation is adapted from the GNU Autoconf manual.
    {
        tmp=`
    (umask 077 && mktemp -d "$TMPDIR/updatedbXXXXXX") 2>/dev/null
    ` &&
        test -n "$tmp" && test -d "$tmp"
    } || {
	# This method is less secure than mktemp -d, but it's a fallback.
	#
	# We use $$ as well as $RANDOM since $RANDOM may not be available.
	# We also add a time-dependent suffix.  This is actually somewhat
	# predictable, but then so is $$.  POSIX does not require date to
	# support +%N.
	ts=`date +%N%S || date +%S 2>/dev/null`
        tmp="$TMPDIR"/updatedb"$$"-"${RANDOM:-}${ts}"
        (umask 077 && mkdir "$tmp")
    }
    echo "$tmp"
}

checkbinary () {
    if test -x "$1" ; then
	: ok
    else
      eval echo "updatedb needs to be able to execute $1, but cannot." >&2
      exit 1
    fi
}

for binary in $find $frcode
do
  checkbinary $binary
done


: ${PRUNEFS="
9P
NFS
afs
autofs
cifs
coda
devfs
devpts
ftpfs
fuse.s3fs
iso9660
mfs
ncpfs
nfs
nfs4
proc
shfs
smbfs
sysfs
"}

if test -n "$PRUNEFS"; then
prunefs_exp=`echo $PRUNEFS |sed -e 's/\([^ ][^ ]*\)/-o -fstype \1/g' \
 -e 's/-o //' -e 's/$/ -o/'`
else
  prunefs_exp=''
fi

# Make and code the file list.
# Sort case insensitively for users' convenience.

rm -f $LOCATE_DB.n
trap 'rm -f $LOCATE_DB.n; exit' HUP TERM

if {
cd "$changeto"
if test -n "$SEARCHPATHS"; then
  if [ "$LOCALUSER" != "" ]; then
    # : A1
    su $LOCALUSER `select_shell $LOCALUSER` -c \
    "$find $SEARCHPATHS $FINDOPTIONS \
     \\( $prunefs_exp \
     -type d -regex '$PRUNEREGEX' \\) -prune -o $print_option"
  else
    # : A2
    $find $SEARCHPATHS $FINDOPTIONS \
     \( $prunefs_exp \
     -type d -regex "$PRUNEREGEX" \) -prune -o $print_option
  fi
fi

if test -n "$NETPATHS"; then
myuid=`getuid`
if [ "$myuid" = 0 ]; then
    # : A3
    su $NETUSER `select_shell $NETUSER` -c \
     "$find $NETPATHS $FINDOPTIONS \\( -type d -regex '$PRUNEREGEX' -prune \\) -o $print_option" ||
    exit $?
  else
    # : A4
    $find $NETPATHS $FINDOPTIONS \( -type d -regex "$PRUNEREGEX" -prune \) -o $print_option ||
    exit $?
  fi
fi
} | $sort | $frcode $frcode_options > $LOCATE_DB.n
then
    : OK so far
    true
else
    rv=$?
    echo "Failed to generate $LOCATE_DB.n" >&2
    rm -f $LOCATE_DB.n
    exit $rv
fi

# To avoid breaking locate while this script is running, put the
# results in a temp file, then rename it atomically.
if test -s $LOCATE_DB.n; then
  chmod 644 ${LOCATE_DB}.n
  mv ${LOCATE_DB}.n $LOCATE_DB
else
  echo "updatedb: new database would be empty" >&2
  rm -f $LOCATE_DB.n
fi

exit 0
