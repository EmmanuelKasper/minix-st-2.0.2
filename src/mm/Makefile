# Set O=o for ANSI compiler and 68000 K&R compiler, or O=s for PC K&R compiler.
O=o
#O=s

# Set b=/usr for /usr/include or b=<null string> for /include.
#b=/usr
b=/usr/lib/m68000

# Directories
i = ../include
s = $i/sys
h = $i/minix
l = $b
#l = ../lib

# Programs, flags, and libraries
CC = exec cc
LD = /usr/lib/em_led
CV = /usr/lib/cv

# Set compiler flags.
CFLAGS = -I$i -O -DSHARP_BANG=1 -DUSE_RW_SEG=1

# Set LDFLAGS to -i for PC or <null string> for 68000.
#LDFLAGS =  -i
LDFLAGS = 
#HEAD = $l/head.$O
HEAD = ../lib/head.$O
#LIB = $l/libc.A $l/libe.A $l/end.A
LIB = $l/libc.a $l/libe.a

a = mm.h $h/config.h $s/types.h $h/const.h $h/type.h \
  $i/ansi.h $i/fcntl.h $i/unistd.h $h/syslib.h \
  $i/limits.h $i/errno.h \
  const.h type.h proto.h glo.h

OBJ = 	main.$O forkexit.$O break.$O exec.$O \
	signal.$O alloc.$O utility.$O table.$O putk.$O trace.$O \
	getset.$O

mm:	Makefile $(HEAD) $(OBJ) $(LIB)
#	$(CC) -o $@ $(LDFLAGS)  $(OBJ)
	$(LD) -c -o tmp $(LDFLAGS) $(HEAD) $(OBJ) $(LIB)
	$(CV) tmp $@
	@rm -f tmp

clean:
	@rm -f $(OBJ) mm *.bak mm

alloc.$O:	$a

break.$O:	$a
break.$O:	$i/signal.h
break.$O:	mproc.h
break.$O:	param.h

exec.$O:	$a
exec.$O:	$s/stat.h
exec.$O:	$h/callnr.h
exec.$O:	$i/signal.h
exec.$O:	mproc.h
exec.$O:	param.h

forkexit.$O:	$a
forkexit.$O:	$s/wait.h
forkexit.$O:	$h/callnr.h
forkexit.$O:	mproc.h
forkexit.$O:	param.h

getset.$O:	$a
getset.$O:	$h/callnr.h
getset.$O:	mproc.h
getset.$O:	param.h

main.$O:	$a
main.$O:	$h/callnr.h
main.$O:	$h/com.h
main.$O:	$i/signal.h
main.$O:	mproc.h
main.$O:	param.h

putk.$O:	$a
putk.$O:	$h/com.h

signal.$O:	$a
signal.$O:	$s/stat.h
signal.$O:	$h/callnr.h
signal.$O:	$h/com.h
signal.$O:	$i/signal.h
signal.$O:	mproc.h
signal.$O:	param.h

table.$O:	$a
table.$O:	$h/callnr.h
table.$O:	mproc.h
table.$O:	param.h

trace.$O:	$a
trace.$O:	$i/signal.h
trace.$O:	mproc.h
trace.$O:	param.h

utility.$O:	$a
utility.$O:	$s/stat.h
utility.$O:	$h/callnr.h
utility.$O:	$h/com.h
utility.$O:	mproc.h
