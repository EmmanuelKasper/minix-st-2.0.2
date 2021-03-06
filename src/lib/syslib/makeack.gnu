# Makefile for lib/syslib.

CC	= gcc
CFLAGS	= -O -D_MINIX -D_POSIX_SOURCE -I../../include $(XCFLAGS)
CC1	= $(CC) $(CFLAGS) -c

LIBRARY	= ../libc.olb
all:	$(LIBRARY)

OBJECTS	= \
	$(LIBRARY)(sys_abort.o) \
	$(LIBRARY)(sys_copy.o) \
	$(LIBRARY)(sys_endsig.o) \
	$(LIBRARY)(sys_exec.o) \
	$(LIBRARY)(sys_fork.o) \
	$(LIBRARY)(sys_fresh.o) \
	$(LIBRARY)(sys_getmap.o) \
	$(LIBRARY)(sys_getsp.o) \
	$(LIBRARY)(sys_kill.o) \
	$(LIBRARY)(sys_newmap.o) \
	$(LIBRARY)(sys_oldsig.o) \
	$(LIBRARY)(sys_sendsig.o) \
	$(LIBRARY)(sys_sigret.o) \
	$(LIBRARY)(sys_times.o) \
	$(LIBRARY)(sys_trace.o) \
	$(LIBRARY)(sys_xit.o) \
	$(LIBRARY)(taskcall.o) \

$(LIBRARY):	$(OBJECTS)
	ar rv $@ *.o
	rm *.o

clean:
	rm -f *.o

$(LIBRARY)(sys_abort.o):	sys_abort.c
	$(CC1) sys_abort.c

$(LIBRARY)(sys_copy.o):	sys_copy.c
	$(CC1) sys_copy.c

$(LIBRARY)(sys_endsig.o):	sys_endsig.c
	$(CC1) sys_endsig.c

$(LIBRARY)(sys_exec.o):	sys_exec.c
	$(CC1) sys_exec.c

$(LIBRARY)(sys_fork.o):	sys_fork.c
	$(CC1) sys_fork.c

$(LIBRARY)(sys_fresh.o):	sys_fresh.c
��/9    N�     n����    g/.��N�    XOO� ` �| ?�� y    ( �  VgL  	gP] gT  SgN  0gHS gD  g  p  g  pV g  rU g  tR g  vW g  xS g  z`  || V��`  r| M��`h| -��/9    N�    S@�  �� @��     /XOf| d��`6| d��`.| d��`&| l��`| b��`| c��`| p��`| C��Hn��?9   N�r-y   ��\OHn��N�    -@�� n��B(  n��B(  y    J(	XOgJy   "g
A�	-H��` A���-H��?9   Hz�Hn��N�    O� 
 y    J()gJy   "g
A�)-H��` A���-H��?9   Hz��Hn��N�    O� 
 y    ( �  3gS g  g0`R09   @ �? 09   �@@ �? Hz��Hn��N�    O� `<Hh�?< N��X\O/ Hz�`Hn��N�    O� `/9   Hz�FHn��N�    O� /.��N�    .�/.��N�    " XO �A/ Hn��N�    " XO �AR@=@���y    o3���    /9    N�    