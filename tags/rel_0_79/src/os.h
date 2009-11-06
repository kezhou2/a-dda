/* File: os.h
 * $Author$
 * $Date::                            $
 * Descr: determines which operation system is used
 *
 * Copyright (C) 2006-2008 University of Amsterdam
 * This file is part of ADDA.
 *
 * ADDA is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ADDA is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ADDA. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef __os_h
#define __os_h

/* If neither WINDOWS nor POSIX is found, some parts of the program, such as precise timing and
 * file locking, will fail to compile
 */
#ifdef _WIN32
#	define WINDOWS
#	include <windows.h> // all windows functions need this
/* this list is not exhaustive. gcc always defines __POSIX__ on POSIX-compliant systems,
 * however other compilers do not necessarily do the same. You may define it manually
 */
#elif defined(__POSIX__) || defined(unix) || defined (__unix) || defined (__unix__)
#	define POSIX
#endif

#endif // __os_h