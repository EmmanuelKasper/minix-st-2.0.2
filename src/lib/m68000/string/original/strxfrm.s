#include <minix/config.h>
#if ( CHIP == M68000 )
/* strxfrm.s
 *
 *	size_t strxfrm(s1, s2, n)
 *	char *s1;
 *	char *s2;
 *	size_t n;
 *
 * Transforms s2 into s1.  The effect is to make strcmp() act on the
 * transformed strings exactly as strcoll() does on original strings.
 * WARNING: This is a bogus implementation, since I have no idea what
 *          ANSI is prattling about with respect to locale.
 */

	.sect .text;.sect .rom;.sect .data;.sect .bss

	.define _strxfrm

	.sect .text
_strxfrm:
	link	a6,#0
	move.w	16(a6),-(sp)
	move.l	12(a6),-(sp)
	move.l	8(a6),-(sp)
	jsr	_strncpy
	move.l	12(a6),-(sp)
	jsr	_strlen
	lea	14(sp),sp
	unlk	a6
	rts
#endif



