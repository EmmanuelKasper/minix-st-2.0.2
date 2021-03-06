#CFLAGS	= -O -I../include -D_MINIX -D_POSIX_SOURCE -DACK \
#	-D_EM_WSIZE=2 -DLOAD1MB -DATARI_ST
CFLAGS	= -O -I../include -D_MINIX -D_POSIX_SOURCE -DACK \
	-D_EM_WSIZE=2 -DATARI_ST
BOOTS_FLAGS = -DDETERMINE_MEM -DUSE_PHYSTOP
#BOOTS_FLAGS = -DLOAD_1MB
MAKEFLAGS= -f makeack.soz

CC	= cc
SRC1	= buildnew.c boot.s type.s init.c menu.c
SRC2	= outmix.h fakeunix.c getstruc.c putstruc.c

PARTS	= ../kernel/kernel ../mm/mm ../fs/fs $(inet) init menu
ALL	= programs
LD	= /usr/lib/ld
CV	= /usr/lib/cv
#l	= /usr/local/sozobon/liba
l	= /usr/lib/m68000
i	= ../include
HEAD	= $l/../head.o
#HEAD	= $l/crtso.o
LIBS	= $l/libc.a $l/libe.a
os	= minix.img
build	= build
inet	= ../inet/inet
#inet	=


all:	$(ALL)

cp cmp:	all

clean:
	rm -f *.o

clobber:
	rm -f $(ALL) $(build) boot_?d type_?d minix_?d init menu

programs: $(build)
	cd ../kernel && $(MAKE) $(MAKEFLAGS)
	cd ../mm && $(MAKE) $(MAKEFLAGS)
	cd ../fs && $(MAKE) $(MAKEFLAGS)
	@if [ `exec ./tell_config ENABLE_NETWORKING` = 0 ]; then \
		$(MAKE) $(MAKEFLAGS) $(os) inet= ; \
	else \
		echo "cd ../inet && $(MAKE) $(MAKEFLAGS)" && \
		(cd ../inet && $(MAKE) $(MAKEFLAGS) ) && \
		$(MAKE) $(MAKEFLAGS) $(os) inet=../inet/inet; \
	fi
	
$(build):	$(build).c outmix.h getstruc.c putstruc.c $i/minix/config.h $i/minix/const.h
	$(CC) $(CFLAGS) $(build).c -o $@
	install -S 96k $@

$(os):	$(build) boot_dd $(PARTS)
	./$(build) boot_dd $(PARTS) $@

ps:	ps.c ../include/minix/config.h ../kernel/const.h \
		../kernel/type.h ../kernel/proc.h ../mm/mproc.h \
		../fs/fproc.h ../fs/const.h
	$(CC) $(CFLAGS) -o $@ $<
	install -S 24kw $@

/usr/bin/ps:	ps
	install -cs -o bin -g kmem -m 2755 $? $@

minix_fd:	$(build) boot_fd $(PARTS)
	./$(build) boot_fd $(PARTS) $@

minix_dd:	$(build) boot_dd $(PARTS)
	./$(build) boot_dd $(PARTS) $@

type_fd type_dd:	type.s
	cp type.s $@.s
	$(CC) $(CFLAGS) $(BOOTS_FLAGS) -DACK -D$@ -c $@.s
	$(LD) -c -o $@.out $@.o
	$(CV) =510 $@.out $@.mix
	dd if=$@.s of=$@.tmp bs=1 count=1 seek=512
	{ \
		dd if=$@.mix bs=1 skip=32; \
		dd if=$@.tmp; \
	} | dd obs=1b | dd of=$@ count=1
	rm -f $@.s $@.o $@.out $@.mix $@.tmp

boot_fd boot_dd:	boot.s
	cp boot.s $@.s
	$(CC) $(CFLAGS) $(BOOTS_FLAGS) -DACK -D$@ -c $@.s
	$(LD) -s -c -o $@.out $@.o
	$(CV) =510 $@.out $@.mix
	dd if=$@.mix of=$@ bs=1 skip=32
	rm -f $@.s $@.o $@.out $@.mix

floppy: $(ALL)
	dd if=minix.img of=/dev/dd0s9 bs=1024

init:	init.o $(HEAD) $(LIBS)
	$(LD) -c -o init.out $(HEAD) init.o $(LIBS)
	$(CV) init.out $@
	install -S 1k $@
	rm -f init.out

menu:	menu.o $(HEAD) $(LIBS)
	$(LD) -c -o menu.out $(HEAD) menu.o $(LIBS)
	$(CV) menu.out $@
	rm -f menu.out

../kernel/kernel: ../include/minix/config.h
	cd ../kernel; make $(MAKEFLAGS) kernel

../fs/fs: ../include/minix/config.h
	cd ../fs; make $(MAKEFLAGS) fs

../mm/mm: ../include/minix/config.h
	cd ../mm; make $(MAKEFLAGS) mm

