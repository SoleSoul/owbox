/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   gravity.h for owbox
   Copyright (c) 2026        owbox contributors

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#ifndef __ob_gravity_h
#define __ob_gravity_h

/* The X11 window gravity values.  Openbox's geometry code is written in terms
   of them, and Xwayland clients still send them in their size hints. */
#define ForgetGravity    0
#define NorthWestGravity 1
#define NorthGravity     2
#define NorthEastGravity 3
#define WestGravity      4
#define CenterGravity    5
#define EastGravity      6
#define SouthWestGravity 7
#define SouthGravity     8
#define SouthEastGravity 9
#define StaticGravity    10

/* WM_NORMAL_HINTS flags, see client.positioned/sized */
#define USPosition (1L << 0)
#define USSize     (1L << 1)
#define PPosition  (1L << 2)
#define PSize      (1L << 3)

#endif
