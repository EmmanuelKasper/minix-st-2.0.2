# change if you prefer support for another national keyboard version
#KEYMAP	= keymap.uk.h
KEYMAP	= keymap.ge.h

DEFS	= -I$i -DACK -DVS_FLOPPY=1
CC	= cc
CFLAGS	= $(DEFS) -O

AS	= cc -c
LD	= /usr/lib/em_led
CV	= /usr/lib/cv
l	= /usr/lib/m68000
i	= ../include
LIBS	= $l/libc.a $l/libe.a

FLOPPY	= stfloppy.o
#SCREEN  = screen.o
SCREEN  = 
SOBJ	= stmpx.o klib68k.o stdskclks.o fpps.o pmmus.o rs2.o
COBJ	= stmain.o proc.o system.o stshadow.o \
	  tty.o clock.o memory.o stdma.o $(FLOPPY) stacsi.o \
	  stcon.o stkbd.o stvdu.o stfnt.o stprint.o rs232.o \
 	  table.o dmp.o misc.o stdskclk.o $(SCREEN) stscsi.o fpp.o pmmu.o \
 	  pty.o driver.o drvlib.o stnet.o
OBJ	= $(SOBJ) $(COBJ)
MAP	= keymap.ge.h keymap.uk.h keymap.us.h
HDR	= const.h glo.h kernel.h proc.h proto.h \
	  stacia.h staddr.h stdma.h stfdc.h sthdc.h stmfp.h \
	  stram.h stsound.h stvideo.h tty.h type.h stscsi.h stvdu.h
DEP	= $i/errno.h $i/limits.h $i/sgtty.h $i/signal.h \
	  $i/minix/boot.h $i/minix/callnr.h $i/minix/com.h \
	  $i/minix/config.h $i/minix/const.h $i/minix/type.h \
	  $i/sys/types.h keymap.h $(HDR)
ALL	= kernel

all:	$(ALL)

cp cmp:	all

clean:
	rm -f $(OBJ)

clobber: clean
	rm -f $(ALL) tmp

kernel:	$(OBJ) $(LIBS)
	$(LD) -c -o tmp $(OBJ) $(LIBS)
	$(CV) -R tmp $@
	install -S 0 $@
	rm tmp

stmpx.o:	stmpx.s
	$(AS) $(DEFS) -c stmpx.s
klib68k.o:	klib68k.s
	$(AS) $(DEFS) -c klib68k.s
stdskclks.o:	stdskclks.s
	$(AS) $(DEFS) -c stdskclks.s
rs2.o:		rs2.s
	$(AS) $(DEFS) -c rs2.s
fpps.o:		fpps.s
	$(AS) $(DEFS) -c fpps.s
pmmus.o:	pmmus.s
	$(AS) $(DEFS) -c pmmus.s

# too crude
$(OBJ):	$(DEP)

keymap.h:	$(KEYMAP)
	rm -f $@; cp $? $@
