/*
 * vsprintf - print formatted output without ellipsis on an array
 */
/* $Header: vsprintf.c,v 1.2 90/11/13 10:56:53 eck Exp $ */

#include	<stdio.h>
#include	<stdarg.h>
#include	"loc_incl.h"

int
vsprintf(char *s, const char *format, va_list arg)
{
	int retval;
	FILE tmp_stream;

	tmp_stream._fd     = -1;
	tmp_stream._flags  = _IOWRITE + _IONBF + _IOWRITING;
	tmp_stream._buf    = (unsigned char *) s;
	tmp_stream._ptr    = (unsigned char *) s;
	tmp_stream._count  = 32767;

	retval = _doprnt(format, arg, &tmp_stream);
	putc('\0',&tmp_stream);

	return retval;
}
