/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <stdarg.h>
#include "doprnt.h"

#define SPRINTF_UNLIMITED -1
struct sprintf_state {
      char *buf;
      unsigned int len;
      unsigned int max;
};

static void
savechar(char *arg, int c)
{
	struct sprintf_state *state = (struct sprintf_state *)arg;
	
	if (state->max != SPRINTF_UNLIMITED)
	{
		if (state->len == state->max)
			return;
	}

	state->len++;
	*state->buf = c;
	state->buf++;
}

int vsprintf(char *s, const char *fmt, va_list args)
{
	struct sprintf_state state;
	state.max = SPRINTF_UNLIMITED;
	state.len = 0;
	state.buf = s;

	_doprnt(fmt, args, 0, (void (*)()) savechar, (char *) &state);
	*(state.buf) = '\0';

	return state.len;
}

int vsnprintf(char *s, int size, const char *fmt, va_list args)
{
	struct sprintf_state state;
	state.max = size;
	state.len = 0;
	state.buf = s;

	_doprnt(fmt, args, 0, (void (*)()) savechar, (char *) &state);
	*(state.buf) = '\0';

	return state.len;
}

int sprintf(char *s, const char *fmt, ...)
{
	va_list	args;
	int err;

	va_start(args, fmt);
	err = vsprintf(s, fmt, args);
	va_end(args);

	return err;
}

int snprintf(char *s, int size, const char *fmt, ...)
{
	va_list	args;
	int err;

	va_start(args, fmt);
	err = vsnprintf(s, size, fmt, args);
	va_end(args);

	return err;
}

