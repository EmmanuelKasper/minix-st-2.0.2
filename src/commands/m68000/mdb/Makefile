CFLAGS	= -O -DVOLATILE= -I../../../include -I../../.. \
	-D_MINIX -D_POSIX_SOURCE -DREADLINE=1 -DHANDLE_MANY_SYMS

CMD	= mdb
OBJ	= mdb.o mdbexp.o mdbdis.o
HDR	= user.h
LIBS	= -ledit

all:	$(CMD)

install:	/usr/bin/mdb

/usr/bin/mdb:	mdb
	install -cs -o bin -g bin -m 555 mdb /usr/bin/mdb
	
clean:
	rm -f *.o core

clobber:
	rm -f *.o core $(CMD)

$(CMD):	$(OBJ)
	$(CC) -o $@ $(OBJ) $(LIBS)
	install -S 128k $@
$(OBJ):	$(HDR)
