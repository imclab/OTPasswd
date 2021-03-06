/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009, 2010 by Tomasz bla Fortuna <bla@thera.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with otpasswd. If not, see <http://www.gnu.org/licenses/>.
 *
 * DESC:
 *   Simple logging/debugging system.
 **********************************************************************/

#ifndef _PRINT_H_
#define _PRINT_H_

#include "num.h"

/* With DEBUG_POSITIONS messages printed on 
 * stdout get information about their source */
#ifndef DEBUG_POSITIONS
#define DEBUG_POSITIONS 1
#endif

/** Flags possible to print_init */
enum PRINT_FLAGS {
	/** Shown when -v passed, can be printed during normal execution */
	PRINT_NOTICE = 1,
	/** Can be printed during normal execution; will be visible always in utility */
	PRINT_WARN = 2,
	/** Will be visible always, should only be used on errors */
	PRINT_ERROR = 3,
	/** Like above but errors are really critical. Program will die and 
	 * anything otpasswd related can be corrupted. */
	PRINT_CRITICAL = 4,
	/** Print error message but don't preceed it with ERROR: label */
	PRINT_MESSAGE = 5,
	/** Don't preceed with anything */
	PRINT_NONE = 50,

	/** Print to stdout */
	PRINT_STDOUT = 64,

	/** Print to syslog */
	PRINT_SYSLOG = 128,
};

/** Initialize logging system */
extern int print_init(int flags, const char *log_file);

/** Clean up after logging */
extern void print_fini();

/** Set log_level/syslog/stdout to another value */
extern void print_config(int flags);

/** Log some data */
extern int _print(const char *file, const int line, int level, const char *fmt, ...);

/** Log data and preceed it with perror message */
extern int _print_perror(const char *file, const int line, int level, const char *fmt, ...);

#ifdef S_SPLINT_S
	/* Splint doesn't understand variadic macros */
	static inline void print(...) {
		printf("Don't really care.\n");
	}
	#define print_perror print

#else

#	if DEBUG_POSITIONS == 1
#		define print(x, y, ...) _print(__FILE__, __LINE__, (x), (y), ## __VA_ARGS__)
#		define print_perror(x, y, ...) _print_perror(__FILE__, __LINE__, (x), (y), ## __VA_ARGS__)
#	else
#		define print(x, y, ...) _print(NULL, -1, (x), (y), ## __VA_ARGS__)
#		define print_perror(x, y, ...) _print_perror(NULL, -1, (x), (y), ## __VA_ARGS__)
#	endif


#endif

#endif
