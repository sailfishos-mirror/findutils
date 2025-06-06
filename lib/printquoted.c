/* printquoted.c -- print a specified string with any necessary quoting.

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
/* config.h must be included first. */
#include <config.h>

/* system headers. */
#include <stdio.h>
#include <stdlib.h>

/* gnulib headers. */
#include "xalloc.h"

/* find headers. */
#include "printquoted.h"

/*
 * Print S according to the format FORMAT, but if the destination is a tty,
 * convert any potentially-dangerous characters.  The logic in this function
 * was taken from ls.c in coreutils (at Sun Jun  5 20:42:51 2005 UTC).
 */
int
print_quoted (FILE *fp,
              const struct quoting_options *qopts,
              bool dest_is_tty,
              const char *format,
              const char *s)
{
  int rv;

  if (dest_is_tty)
    {
      char smallbuf[BUFSIZ];
      size_t len = quotearg_buffer (smallbuf, sizeof smallbuf, s, -1, qopts);
      char *buf;
      if (len < sizeof smallbuf)
        buf = smallbuf;
      else
        {
          /* The original coreutils code uses alloca(), but I don't
           * want to take on the anguish of introducing alloca() to
           * 'find'.
           * XXX: newsflash: we already have alloca().
           */
          buf = xmalloc (len + 1);
          quotearg_buffer (buf, len + 1, s, -1, qopts);
        }

      /* Replace any remaining funny characters with '?'. */
      len = qmark_chars (buf, len);
      buf[len] = 0;

      rv = fprintf (fp, format, buf);   /* Print the quoted version */
      if (buf != smallbuf)
        {
          free (buf);
          buf = NULL;
        }
    }
  else
    {
      /* no need to quote things. */
      rv = fprintf (fp, format, s);
    }
  return rv;
}
