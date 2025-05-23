/* finddata.c -- global data for "find".
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

/* system headers would go here, but we include none. */

/* gnulib headers. */
#include "save-cwd.h"

/* find headers. */
#include "defs.h"


struct options options;
struct state state;
struct saved_cwd *initial_wd = NULL;
