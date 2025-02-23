/* fdleak.h -- detect file descriptor leaks
   Copyright (C) 2010-2025 Free Software Foundation, Inc.

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
#ifndef FDLEAK_H
# define FDLEAK_H

# include <stdbool.h>           /* for bool */

void remember_non_cloexec_fds (void);
void forget_non_cloexec_fds (void);
void complain_about_leaky_fds (void);
bool fd_leak_check_is_enabled (void);

int open_cloexec(const char *path, int flags, ...);

#endif
