#include <minix/config.h>
#if (CHIP == M68000)

#if (MACHINE == AMIGA)
#define MPX
#include <minix/amtransfer.h>
#endif

#include "const.h"

!
! Only the registers that are considered scratch by the compiler used
! to compile the kernel are saved. Other registers are saved by the
! prolog generated by the kernel compiler. NOTE: d0, a0 and a1 are
! used by the asm interrupt handler. At least these *must* be saved.
!
#ifdef ALCYON
#define FREEREGS d0-d2/a0-a2
#define SPACEREGS 24
#endif
#ifdef ACK
#ifdef FOR_SOZOBON
#define FREEREGS d0-d2/a0-a2
#define SPACEREGS 24
#else
#define FREEREGS d0-d2/a0-a1
#define SPACEREGS 20
#endif /* FOR_SOZOBON */
#endif
#if !defined(ACK) && !defined(ALCYON)
#define FREEREGS d0-d1/a0-a1
#define SPACEREGS 16
#endif

#ifdef ACK
! section definition
	.sect	.text
	.sect	.rom
	.sect	.data
	.sect	.bss
#endif

!
! public labels
!
	.define _lock
	.define _unlock
	.define _restore
	.define _kreboot
	.define	_test_and_set
	.define	_get_mem_size
	.define _sizes
	.define _invicache
	.define	_invdcache
	.define	_proctyp
	.define	_fpptyp
	.define	_mmutyp
#if (MACHINE == ATARI && ATARI_TYPE == DETECT_TYPE)
	.define	_atari_type
#endif /* MACHINE == ATARI && ATARI_TYPE == DETECT_TYPE */
#ifdef ACK
	.define	.trpim
	.define	.trppc
#endif

!
! external references
!
	.extern _clock_handler
	.extern _held_head
	.extern _k_reenter
	.extern	_proc_ptr
	.extern	_rdy_head
	.extern	_main
	.extern	_trap
#if (MACHINE == ATARI)
	.extern	_none
	.extern	_rupt
	.extern	_timint
	.extern	_dmaint
#if (NR_RS_LINES > 0)
	.extern	_siaint
#else
#define	_siaint	_none
#endif /* NR_RS_LINES */
	.extern	_aciaint
	.extern	_piaint
	.extern	_iob
#endif
#if (MACHINE == AMIGA)
	.extern _lvl1_int
	.extern _rbf_int
	.extern _ciaa_int
	.extern _ciab_int
	.extern _vbl_int
	.extern _aud_int
	.extern _nmi_int
#endif
	.extern _sys_call
	.extern _lock_pick_proc
!	.extern	_end
!	.extern	_edata


!
! offsets into a proc table entry (see proc.h)
!
sava6	= 56
savsp	= 60
savpc	= 64
savsr	= 68
savtt	= 77

	.sect	.text

#if (MACHINE == ATARI)
.data4	0		!   0	0 reset initial sp; cannot be written on ATARI
#endif

#if (MACHINE == AMIGA)
.data4	TRANSDAT_VERSION !  0	0 reset initial sp, will be cleared by loader
#endif

.data4	start		!   1	4 reset initial pc
.data4	buserr		!   2	8 bus error
.data4	adrerr		!   3	c adress error
.data4	illtrp		!   4  10 illegal instruction
.data4	zertrp		!   5  14 zero divide
.data4	chktrp		!   6  18 chk/chk2 instruction
.data4	trptrp		!   7  1c trapcc/trapv instruction
.data4	prvtrp		!   8  20 priviledge violation
.data4	trc		!   9  24 trace
.data4	atrp		!  10  28 line 1010 emulator
.data4	ftrp		!  11  2c line 1111 emulator
.data4	trp		!  12  30 reserved
.data4	cpvtrp		!  13  34 coprocessor protocol violation
.data4	fortrp		!  14  38 format error
.data4	trp		!  15  3c uninitialized interrupt
/*
.data4	trp, trp, trp, trp, trp, trp, trp, conf ! 16 - 23 reserved
*/
.data4	trp, trp, trp, trp, trp, trp, trp, trp ! 16 - 23 reserved
.data4	intr0		!  24  60 spurious interrupt
.data4	intr1		!  25  64 level 1 interrupt vector
.data4	intr2		!  26  68 level 2 interrupt autovector
.data4	intr3		!  27  6c level 3 interrupt autovector
.data4	intr4		!  28  70 level 4 interrupt autovector
.data4	intr5		!  29  74 level 5 interrupt autovector
.data4	intr6		!  30  78 level 6 interrupt autovector
.data4	intr7		!  31  7c level 7 interrupt autovector
.data4	sys		!  32  80 trap #0, used for system calls
			!  33 - 47 trap #1-15
.data4		 trpntrp, trpntrp, trpntrp, trpntrp, trpntrp, trpntrp, trpntrp
.data4	trpntrp, trpntrp, trpntrp, trpntrp, trpntrp, trpntrp, trpntrp, trpntrp
			!  48 - 55  fpcp errors
.data4	fpcptrp, fpcptrp, fpcptrp, fpcptrp, fpcptrp, fpcptrp, fpcptrp, fpcptrp
			!  56 - 58  pmmu errors
.data4	pmmutrp, pmmutrp, pmmutrp
			!  59 - 63  reserved
.data4	trp,	trp,	trp,	 trp,	  trp
			!  rest are user-defined vectors
#if (MACHINE == ATARI)
! Asynchronous interrupts
.data4	pia		! par printer busy
#if (NR_RS_LINES > 0)
.data4	sia4		! rs232 DCD
.data4	sia4		! rs232 CTS
#else
.data4	non
.data4	non
#endif /* NR_RS_LINES > 0 */
.data4	iob3		! not handled (blitter done)
.data4	tim3		! timer D
.data4	clk		! timer C
.data4	acia		! keyboard/MIDI
.data4	dma		! fdc/acsi
.data4	tim1		! timer B
#if (NR_RS_LINES > 0)
.data4	sia3		! rs232 transmit error
.data4	sia2		! rs232 transmit buffer empty
.data4	sia1		! rs232 receive error
.data4	sia0		! rs232 receive buffer full
#else
.data4	non
.data4	non
.data4	non
.data4	non
#endif /* NR_RS_LINES > 0 */
.data4	tim0		! timer A
.data4	iob6		! not handled (rs232 RI signal)
.data4	iob7		! not handled (monochrome monitor detect)
#if (ATARI_TYPE == TT)
.data4	iob8		! not handled (unused)
.data4	iob9		! not handled (unused)
.data4	non		! scc dma interrupt
.data4	non		! ring indicator scc b
.data4	tim7		! timer D
.data4	tim6		! timer C
.data4	non		! reserved
.data4	scsidma		! scsi dma interrupt
.data4	tim5		! timer B
.data4	sia8		! rs232 transmit error
.data4	sia7		! rs232 transmit buffer empty
.data4	sia6		! rs232 receive error
.data4	sia5		! rs232 receive buffer full
.data4	tim4		! timer A
.data4	non		! rtc irq (cleared by reading rtc)
.data4	scsi		! scsi controller irq
#else
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
#endif /* ATARI_TYPE == TT */
! Asynchronous interrupts end here. rest is unused
#endif
#if (MACHINE == AMIGA)
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
#endif
#if (ATARI_TYPE >= STE)
.data4	non60,	non61,	non62,	non63,	non64,	non65,	non66,	non67
.data4	non68,	non69,	non6a,	non6b,	non6c,	non6d,	non6e,	non6f
#else
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
#endif /* ATARI_TYPE >= STE */
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
.data4	non,	non,	non,	non,	non,	non,	non,	non
!
#if (MACHINE == ATARI)
! gap to allow ramdisk to survive
!
	.space	0x0200
!
#endif
! startup code, just after interrupt vectors
! call _main and do process switch when it returns
!
start:
!	reset
	move.w	#0x2700,sr
	move.l	#k_stktop,sp	! load sp so exceptions can be stored

	clr.b 	_fpptyp		! assume no fpu

#ifdef FIXED_CPU
# ifndef MMU_TYP
#  define MMU_TYP	0
# endif /* MMU_TYP */
# ifndef FPU_TYP
#  define FPU_TYP	0
#endif /* FPU_TYP */
	move.b	#FPU_TYP,_fputyp
	move.b	#MMU_TYP,_mmutyp ! MMU type
	move.b	#FIXED_CPU,_proctyp ! CPU type
#else
! first determine the mmu type
	move.l	0x2c,a0		! save bus error exception handler
	move.l	#proctype,0x2c	! install own error handler
	clr.b 	_mmutyp		! no mmu
	.data4 0xf0002400	! pflusha
	move.b	#1,_mmutyp	! assume it is a 68851. If it is a 68030
				! instead we will detect this later on
proctype:
	move.l	a0,0x2c		! restore old invalid instruction exception handler
	
! now determine proc type
! warning: the detection code for 68040 has never been tested!!!
	move.l	0x10,a0		! save invalid instruction exception handler
	move.l	#fputype,0x10	! install own error handler
! It is at least a 68000/68008 
	clr.b 	_proctyp
! see if it is an 68010 or up
	.68010
	move ccr,d0
	.68000
! since we came here the move instruction is legal so 68010 or up
	move.b	#1,_proctyp
! see if it is 68020 or up. If so also flush and enable the instruction cache
	move.l	#0x3919,d0
	.data4	0x4e7b0002		! movec.l d0,cacr
	move.b	#2,_proctyp
! see if it is 68030 or 68040
! this is done by reading the cacr register back 
	.data4	0x4e7a0002		! movec.l cacr,d0
! if d0 = 0 we have movec and cacr, but we haven't enabled a cache
! with movec.l d0,cacr. So it must be an 68040
	tst.w	d0
	beq	got_68040
	and.w	#0xff00,d0
! if no there are no bits set in the second byte then we have no data
! cache and thus an 68020
	beq	fputype
	move.b	#3,_proctyp
! if it is an 68030 then also set mmutyp
	move.b	#3,_mmutyp
	bra	fputype
got_68040:
	move.l	#0x80008000,d0		! enable both caches
	.data4	0x4e7b0002		! movec.l d0,cacr
	move.b	#4,_proctyp
	move.b	#4,_mmutyp
	move.b	#4,_fpptyp
  	! even if the 68040 has a fpu on board check if a coprocessor
	! with the full fpp instruction set is present
fputype:
	move.l	a0,0x10		! restore old invalid instruction exception handler

! now determine fpp type
	move.l	0x2c,a0		! save bus error exception handler
	move.l	#got_types,0x2c	! install own error handler
	.data2	0xf027		! fsave	-(a7)
! if we got here there is a fpp chip
! discriminate between 68881 and 68882
	cmp.b	#0x18,1(a7)	! if this byte contains 0x18 it is a 68881
				! if it contains 0x38 it is a 68882
	bne 	got_68882
	move.b	#1,_fpptyp
	bra	got_types
got_68882:
	move.b	#2,_fpptyp
got_types:
	move.l	a0,0x2c		! restore old invalid instruction exception handler

#endif /* FIXED_CPU */

	move.l	#k_stktop,sp	! forget the exception data on the stack

#if (MACHINE == ATARI)
#if (ATARI_TYPE == DETECT_TYPE)
	move.w	#ST,_atari_type	! preset
	move.l	8,a0		! save old bus error exception handler

! No need to check for TT, must be set in minix/config.h
!	move.l	#berr1,8	! install own error handler
!
! Check for TT SCSI Chip
!	move.b	0xffff8781,d0
!	move.w	#TT,_atari_type
!	bra	detected
!berr1:
!	move.l	#k_stktop,sp	! forget the exception data on the stack

	move.l	#berr2,8	! install own error handler

! the next two instructions enable the cache and set the processor speed to
! 16 Mhz. This only works on Mega STe computers and causes a bus error on
! "normal" ST-s. In that case the instructions are just skipped
	bset 	#0,0xffff8e21	! enable Mega STe cache
	bset 	#1,0xffff8e21	! Mega STe 16 Mhz speed
	move.w	#MSTE,_atari_type
	bra	detected
berr2:
	move.l	#k_stktop,sp	! forget the exception data on the stack
	move.l	#detected,8
	move.b	0xffff820c,d0	! Video base register low byte
	move.w	#STE,_atari_type
detected:

#else
	move.l	8,a0		! save old bus error exception handler
	move.l	#berr,8		! install own error handler

! the next two instructions enable the cache and set the processor speed to
! 16 Mhz. This only works on Mega STe computers and causes a bus error on
! "normal" ST-s. In that case the instructions are just skipped
	bset 	#0,0xffff8e21	! enable Mega STe cache
	bset 	#1,0xffff8e21	! Mega STe 16 Mhz speed
berr:
#endif /* ATARI_TYPE == DETECT_TYPE */

	move.l	#k_stktop,sp	! forget the exception data on the stack
	move.l	a0,8		! restore old bus error exception handler
#endif

! the following code could be used to clear the bss area
!	move.l	#_end,d0
!	move.l	#_edata,a0
!	bra	L2
!L1:
!	move.w	#0,(a0)+
!L2:
!	cmp.l	d0,a0
!	bcs	L1
	jsr	_main
	move.b	#0x00,0xffff8240
	bra	restart

!
! the vector handlers for synchronous traps. save full context.
!
buserr:
	move.b	#2,.vectnum
	tst.b	_proctyp
	bne	gentrp
	add.l	#8,sp		! remove extra context on 68000 processor
	bra	gentrp
adrerr:
	move.b	#3,.vectnum
	tst.b	_proctyp
	bne	gentrp
	add.l	#8,sp		! remove extra context on 68000 processor
	bra	gentrp
trp:
	move.b	#12,.vectnum
	bra	gentrp
illtrp:
	move.b	#4,.vectnum
	bra	gentrp
zertrp:
	move.b	#5,.vectnum
	bra	gentrp
chktrp:
	move.b	#6,.vectnum
	bra	gentrp
trptrp:
	move.b	#7,.vectnum
	bra	gentrp
prvtrp:
	move.b	#8,.vectnum
	bra	gentrp
atrp:
	move.b	#10,.vectnum
	bra	gentrp
ftrp:
	move.b	#11,.vectnum
	bra	gentrp
cpvtrp:
	move.b	#13,.vectnum
	bra	gentrp
fortrp:
	move.b	#14,.vectnum
	bra	gentrp
trpntrp:
	move.b	#33,.vectnum
	bra	gentrp
fpcptrp:
	move.b	#48,.vectnum
	bra	gentrp
pmmutrp:
	move.b	#56,.vectnum
	bra	gentrp

gentrp:
	bsr	save
	jsr	_trap
	bra	restart
trc:
	move.b	#9,.vectnum
	btst	#5,(sp) 	! tracing through trap?
	beq	gentrp		! No, do normal trace processing
	rte			! do not trace; execute system call
non:
	bsr	save
	jsr	_none
	bra	restart

#if (ATARI_TYPE >= STE)
non60:
non61:
non62:
non63:
non64:
non65:
non66:
non67:
non68:
non69:
non6a:
non6b:
non6c:
non6d:
non6e:
non6f:
!	bsr	save
!	bra	restart
	rte
#endif /* ATARI_TYPE >= STE */

sys:
	move.b	#32,.vectnum
	bsr	save		! d0, d1 and a0 not modified
	bsr	_unlock 	! do not lock during sys_call
	move.l	a0,-(sp)	! m_ptr
	move.w	d1,-(sp)	! src_dest
	move.w	d0,-(sp)	! SEND/RECEIVE/BOTH
				! warning: _sys_call may change _proc_ptr
	jsr	_sys_call	! sys_call(func,src_dest,m_ptr)
	move.l	d0,(a6)		! a6 is set by save to _proc_ptr
	add.l	#8,sp

	move.w	#0x2700,sr
	tst.b	.pick_proc	! _proc_ptr changed in a interrupt routine?
	beq	1f
	sf	.pick_proc	! clear .pick_proc
	jsr	_lock_pick_proc	! _proc_ptr changed -> call _lock_pick_proc
1:	bra	restart

#if (MACHINE == ATARI)
intr0:
intr1:
intr3:
intr5:
intr6:
intr7:
	bsr	save
	jsr	_rupt			! call service routine
	bra	restart
intr2:
	! this one does not affect task switching
	! must be avoided because it comes so frequent
	! execute spl2()
	or.w	#0x200,sr
	rte
intr4:
	! if _progvdu is set, call interrupt handler to load
	! new values in the video registers, and clear _progvdu.
	! if _progvdu is not set, just rte
	tst.w	_progvdu
 	bne	dovbl
  	rte
 
 dovbl:	movem.l	FREEREGS,-(sp)
! 	move.w	#0,_progvdu		! will be done by vbl()
	move.w	#0,d0
	move.l	#_vbl,a0
	bra	genint

!
! The vector handlers for asynchronous traps. context saved only if necessary.
!
iob3:
	movem.l	FREEREGS,-(sp)
	move.w	#3,d0
	move.l	#_iob,a0
	bra	genint
iob6:
	movem.l	FREEREGS,-(sp)
	move.w	#6,d0
	move.l	#_iob,a0
	bra	genint
iob7:
	movem.l	FREEREGS,-(sp)
	move.w	#7,d0
	move.l	#_iob,a0
	bra	genint
iob8:
	movem.l	FREEREGS,-(sp)
	move.w	#8,d0
	move.l	#_iob,a0
	bra	genint
iob9:
	movem.l	FREEREGS,-(sp)
	move.w	#9,d0
	move.l	#_iob,a0
	bra	genint

tim0:
	movem.l	FREEREGS,-(sp)
	move.w	#0,d0
	move.l	#_timint,a0
	bra	genint
tim1:
	movem.l	FREEREGS,-(sp)
	move.w	#1,d0
	move.l	#_timint,a0
	bra	genint
tim3:
	movem.l	FREEREGS,-(sp)
	move.w	#3,d0
	move.l	#_timint,a0
	bra	genint
tim4:
	movem.l	FREEREGS,-(sp)
	move.w	#4,d0
	move.l	#_timint,a0
	bra	genint
tim5:
	movem.l	FREEREGS,-(sp)
	move.w	#5,d0
	move.l	#_timint,a0
	bra	genint
tim6:
	movem.l	FREEREGS,-(sp)
	move.w	#6,d0
	move.l	#_timint,a0
	bra	genint
tim7:
	movem.l	FREEREGS,-(sp)
	move.w	#7,d0
	move.l	#_timint,a0
	bra	genint

clk:
	sub.w	#1,clkcnt
	beq	cont
	rte
cont:
	move.w	#4,clkcnt
	movem.l	FREEREGS,-(sp)
	move.l	#_clock_handler,a0
	bra	genint
dma:
	movem.l	FREEREGS,-(sp)
	move.l	#_dmaint,a0
	bra	genint
#if (ATARI_TYPE == TT)
scsidma:
	movem.l	FREEREGS,-(sp)
	move.l	#_scsidmaint,a0
	bra	genint
scsi:
	movem.l	FREEREGS,-(sp)
	move.l	#_scsiint,a0
	bra	genint
#endif /* ATARI_TYPE == TT */
acia:
	movem.l	FREEREGS,-(sp)
	move.l	#_aciaint,a0
	bra	genint
sia0:
	movem.l	FREEREGS,-(sp)
	move.w	#0,d0
	move.l	#_siaint,a0
	bra	genint
sia1:
	movem.l	FREEREGS,-(sp)
	move.w	#1,d0
	move.l	#_siaint,a0
	bra	genint
sia2:
	movem.l	FREEREGS,-(sp)
	move.w	#2,d0
	move.l	#_siaint,a0
	bra	genint
sia3:
	movem.l	FREEREGS,-(sp)
	move.w	#3,d0
	move.l	#_siaint,a0
	bra	genint
sia4:
	movem.l	FREEREGS,-(sp)
	move.w	#4,d0
	move.l	#_siaint,a0
	bra	genint
sia5:
	movem.l	FREEREGS,-(sp)
	move.w	#5,d0
	move.l	#_siaint,a0
	bra	genint
sia6:
	movem.l	FREEREGS,-(sp)
	move.w	#6,d0
	move.l	#_siaint,a0
	bra	genint
sia7:
	movem.l	FREEREGS,-(sp)
	move.w	#7,d0
	move.l	#_siaint,a0
	bra	genint
sia8:
	movem.l	FREEREGS,-(sp)
	move.w	#8,d0
	move.l	#_siaint,a0
	bra	genint

pia:
	movem.l	FREEREGS,-(sp)
	move.l	#_piaint,a0
	bra	genint
#endif

#if (MACHINE == AMIGA)
intr0:
	movem.l	freeregs,-(sp)
	move.l	_no_int,a0			! call service routine
	bra	genint
intr1:
	movem.l	freeregs,-(sp)
	move.l	_lvl1_int,a0
	bra	genint
intr2:
	movem.l	freeregs,-(sp)
	move.l	_ciaa_int,a0
	bra	genint
intr3:
	movem.l	freeregs,-(sp)
	move.l	#_vbl_int,a0
	bra	genint
intr4:
	movem.l	freeregs,-(sp)
	move.l	#_aud_int,a0
	bra	genint
intr5:
	movem.l	freeregs,-(sp)
	move.l	#_rbf_int,a0
	bra	genint
intr6:
	movem.l	freeregs,-(sp)
	move.l	#_ciab_int,a0
	bra	genint
intr7:
	movem.l	freeregs,-(sp)
	move.l	#_nmi_int,a0
	bra	genint
#endif

genint:
	add.b	#1,_k_reenter		! from -1 if not reentering
	bne	3f
	! NOTE: the following 4 instructions will not cause a 
	!       context switch if a kernel process with pc < restart is
	!	interrupted. Currently such a process is always 
	!	busy with a trap or irq. If in the future this is
	!	not the case for some reason (e.g. every task has its
	!	own virtual address), then process switches may 
	!	not be done (while required)
	btst	#5,SPACEREGS(sp)	! check if we did not interrupt a
	beq	2f			! trap or irq, first test if the
					! interrupted process is in user mode
					! if so, no problem, jump to label 2
	cmp.l	#restart,2+SPACEREGS(sp)! if we interupted a process in
	bge	2f			! supervisor mode check if the old pc
					! was smaller than restart. 
					! if not, no problem, jump to label 2
	add.b	#1,_k_reenter		! else probably interrupted trap or irq
					! so increment k_reenter twice because
					! the interrupted routine maybe has not
					! gotten to incrementing k_reenter
					! we have to do it here since otherwise
					! we might end up doing a save of an
					! interrupted user process (due to a
					! context switch) but we can
					! not save both the usp and ssp
					! incrementing k_reenter twice avoids
					! the context switch!

					! it is now clear that we are not
					! a "level 0" interrupt service.
					! only a "level 0" service may change
					! proc_ptr.
	move.l	_proc_ptr,-(sp)		! save _proc_ptr
	move.w	d0,-(sp)		! push (potential) argument
	jsr	(a0)			! call service routine
	add.l	#2,sp			! pop argument
	move.l	(sp)+,d0

	move.w	#0x2700,sr
	sub.b	#2,_k_reenter
	cmp.l	_proc_ptr,d0
					! if proc_ptr was changed, we have
					! to restore it. on the other hand,
					! the "level 0" service must be informed
					! that a context-switch is now
					! necessary/possible or we would
					! eventually stay in idle for ever.
	beq	1f			! _proc_ptr changed?
	st	.pick_proc		!    yes -> force pick_proc
	move.l	d0,_proc_ptr		!           and restore _proc_ptr
1:
	movem.l	(sp)+,FREEREGS
	rte

2:
	move.l	_proc_ptr,a1		! save _proc_ptr
	move.l	a1,.proc_ptr
	move.l	2+SPACEREGS(sp),savpc(a1) ! save pc for profiling in clock.c

	move.w	d0,-(sp)		! push argument
	jsr	(a0)			! call service routine
	add.l	#2,sp			! pop argument
	
	move.w	#0x2700,sr
	sub.b	#1,_k_reenter
	move.l	.proc_ptr,d0		! _proc_ptr changed?
	cmp.l	_proc_ptr,d0
	bne	1f
	tst.b	.pick_proc	! _proc_ptr changed in a interrupt routine?
	beq	2f
	sf	.pick_proc	! clear .pick_proc
	jsr	_lock_pick_proc	! _proc_ptr changed -> call _lock_pick_proc
	move.l	.proc_ptr,d0
	cmp.l	_proc_ptr,d0
	beq	2f

1:	move.l	_proc_ptr,.proc_ptr
	move.l	d0,_proc_ptr
	movem.l	(sp)+,FREEREGS

	bsr	save
	move.l	.proc_ptr,_proc_ptr
	bra	restart
2:
	movem.l	(sp)+,FREEREGS
	cmp.l	#0,_held_head		! are there unserviced interrupts?
	bne	1f			! yes: let restart take care
	rte

1:	bsr	save
	bra	restart
3:
	move.l	_proc_ptr,-(sp)		! save _proc_ptr
	move.w	d0,-(sp)		! push argument
	jsr	(a0)			! call service routine
	add.l	#2,sp			! pop argument
	move.l	(sp)+,d0

	move.w	#0x2700,sr
	sub.b	#1,_k_reenter
	cmp.l	_proc_ptr,d0
	beq	1f			! _proc_ptr changed?
	st	.pick_proc		!    yes -> force pick_proc
	move.l	d0,_proc_ptr		!           and restore _proc_ptr
1:
	movem.l	(sp)+,FREEREGS
	rte

! perform task switch by save and restart
!
save:
	move.w	#0x2700,sr
	move.l	a6,-(sp)
	move.l	_proc_ptr,a6
	movem.l	d0-d7/a0-a5,(a6)
	move.l	(sp)+,sava6(a6) 		! a6
	move.l	usp,a1
	btst	#5,4(sp)			! test old S-bit
	beq	save_a1_ok
	lea	10(sp),a1			! assume 68000
	tst.b	_proctyp
	beq	save_a1_ok			! yes, it was really a 68000
	! it was not a 68000, look at the frame number in the frame
	move.l	#0,d6
	move.w	10(sp),d6			! format/offset word
	move.l	#10,d7
	lsr.l	d7,d6
	lea	.framesizes,a1
	add.l	d6,a1
	move.l	(a1),a1
	add.l	sp,a1
save_a1_ok:
	move.l	a1,savsp(a6)			! old sp: usp or ksp
	move.b	.vectnum,savtt(a6)		! trap type
	move.l	(sp)+,a1			! return address
	move.w	(sp)+,savsr(a6) 		! sr
	move.l	(sp)+,savpc(a6) 		! pc
	add.b	#1,_k_reenter			! from -1 if not reentering
	lea	k_stktop,sp

#ifdef FPP
! ifdef fpp, this could be inline to make it faster, also check to see
! if next process is a task, which does not use the fpp

	tst.b	_fpptyp
	beq	no_fpp1
	movem.l	d0/d1/a0/a1,-(sp)	! save return addr
	move.l	a6,-(sp)		! push proc_ptr
	jsr	_fpp_save		! save fpp state
	add.l	#4,sp
	movem.l	(sp)+,d0/d1/a0/a1
no_fpp1:
#endif
	jmp	(a1)

restart:
! Flush any held-up interrupts.
! This reenables interrupts, so the current interrupt handler may reenter.
! This does not matter, because the current handler is about to exit and no
! other handlers can reenter since flushing is only done when k_reenter == 0.

	move.w	#0x2700,sr
	tst.b	_k_reenter
	bne	over_call_unhold
	cmp.l	#0,_held_head
	beq	over_call_unhold
	jsr	_unhold
over_call_unhold:
	jsr	_checksp
	move.l	_proc_ptr,a6
	clr.b	savtt(a6)			! trap type
	clr.b	.vectnum
	move.l	savsp(a6),a0			! old sp: usp or ksp
	btst	#5,savsr(a6)			! test old S-bit
	bne	L6				! jump if S-bit on
#if (SHADOWING == 0)
! pmmu load new map for user task
	move.l	a0,-(sp)			! save a0
	move.l	a6,-(sp)			! proc_ptr
	jsr	_pmmu_restore			! pmmu_restore(proc_ptr)
	move.l	(sp)+,a6
	move.l	(sp)+,a0			! restore a0
#endif
#ifdef FPP
	tst.b	_fpptyp
	beq	no_fpp2
	move.l	a0,-(sp)			! save a0
	move.l	a6,-(sp)			! proc_ptr
	jsr	_fpp_restore			! fpp_restore(proc_ptr)
	move.l	(sp)+,a6
	move.l	(sp)+,a0			! restore a0
no_fpp2:
#endif
	move.l	a0,usp
	bra	L7
L6:
	move.l	a0,sp
L7:
	tst.b	_proctyp
	beq	L8
	move.w	#0,-(sp)			! clear the format word
	jsr	_invicache
	jsr	_invdcache
L8:
	move.l	savpc(a6),-(sp) 		! pc
	move.w	savsr(a6),-(sp) 		! sr
	movem.l (a6),d0-d7/a0-a6
	move.w	#0x2700,sr
	sub.b	#1,_k_reenter
	rte

_invicache:
	cmp.b	#1,_proctyp
	ble	invi1			! no instruction cache in 68000/010
	cmp.b	#4,_proctyp
	bge	invi4			! different for 68040
	move.l	d0,-(sp)
	.data4	0x4e7a0002		! movec.l cacr,d0
	bset	#3,d0			! set the i cache clear bit
	.data4	0x4e7b0002		! movec.l d0,cacr
	move.l	(sp)+,d0
	rts	
invi4:
	.data2	0xf498			! cinv instruction cache
invi1:
	rts

_invdcache:
	cmp.b	#2,_proctyp
	ble	invd1			! no data cache in 68000/010/020
	cmp.b	#4,_proctyp
	bge	invd4			! different for 68040
	move.l	d0,-(sp)
	.data4	0x4e7a0002		! movec.l cacr,d0
	bset	#11,d0			! set the d cache clear bit
	.data4	0x4e7b0002		! movec.l d0,cacr
	move.l	(sp)+,d0
	rts	
invd4:
	.data2	0xf458			! cinv data cache
invd1:
	rts

_kreboot:
	move.l	4,a0
	jmp	(a0)
_lock:
	move.w	sr,d0
	move.w	#0x2700,sr
	rts
_restore:
	move.w	4(sp),sr
	rts
_unlock:
#if (MACHINE == ATARI)
	move.w	#0x2200,sr		! spl2() to block HBL interrupt
#else
	move.w	#0x2000,sr
#endif
	rts
_test_and_set:
	move.l	4(sp),a0
	tas	(a0)
	smi	d0
	ext.w	d0
	rts
	
_get_mem_size:
	cmp.b	#2,_proctyp
	ble	nocache1	
	cmp.b	#4,_proctyp
	bge	cache4	
	move.l	#0x0019,d0	! disable data cache
	.data4	0x4e7b0002	! movec.l d0,cacr
	bra	nocache1
cache4:
	move.l	#0x8000,d0	! disable data cache
	.data4	0x4e7b0002	! movec.l d0,cacr
nocache1:
	move.l	4(sp),a0	! start address
	link	a2,#0		! save stack pointer in case an exception occurs
	move.l	8,a1		! save old bus error exception handler

#if MULTIBOARD
	cmp.l	#0x400000,a0	! multiboard is allways at 4MB
	bne	multiend
	cmp.b	#2,_proctyp
	bge	multiend
	move.l	#nomulti,8	! install own error handler
	link	a2,#0		! save stack pointer
	move.w	0xd50000,d0	! if Multiboard present, this succeeds
				! and activates it.
				! If not, a bus error occurs and we
				! end up at nomulti
#if (FULL_MULTIBOARD_TEST == 0)
nomulti:
#endif /* FULL_MULTIBOARD_CHECK */
	unlk	a2		! clean up stack
	move.l	a1,8		! restore exception handler

#if FULL_MULTIBOARD_TEST
	move.l	#0x400000,a1	! check memory size
	move.l	#0x80808080,(a1)
	move.l	#0x800000,a1
	move.l	#0x40404040,(a1)
	move.l	#0x600000,a1
	move.l	#0x20202020,(a1)
	move.l	#0x400000,a1
	cmp.l	#0x20202020,(a1)
	beq	mb2
	cmp.l	#0x20402040,(a1)
	beq	mb2
	cmp.l	#0x40204020,(a1)
	beq	mb2
	cmp.l	#0x20802080,(a1)
	beq	mb2
	cmp.l	#0x80208020,(a1)
	beq	mb2
	cmp.l	#0x40404040,(a1)
	beq	mb4
	cmp.l	#0x80408040,(a1)
	beq	mb4
	cmp.l	#0x40804080,(a1)
	beq	mb4
	cmp.l	#0x80808080,(a1)
	beq	mb8
	move.l	#0x400000,a0		! return 0 byte of memory
	bra	tstend1
mb8:	move.l	#0x800000,mbmemsize
	move.l	#0xbffff0,d0
	bra	kbsum
mb4:	move.l	#0x400000,mbmemsize
	move.l	#0x7ffff0,d0
	bra	kbsum
mb2:	move.l	#0x200000,mbmemsize
	move.l	#0x5ffff0,d0
!
! full and destructive test of memory on multiboard
! every byte gets checked.
!
kbsum:	movem.l	d2-d3,-(sp)		! save regs
	move.l	#0x400000,a1
	move.l	#0xaaaaaaaa,d2	! pattern 1
	move.l	#0x55555555,d3	! pattern 2
mtest1:	move.l	#0x7fff,d1	! loop counter
kb1:	move.l	d2,(a1)
	cmp.l	(a1),d2
	bne	mbmerr
	move.l	d3,(a1)
	cmp.l	(a1),d3
	bne	mbmerr
	add.l	#4,a1
	move.l	d2,(a1)
	cmp.l	(a1),d2
	bne	mbmerr
	move.l	d3,(a1)
	cmp.l	(a1),d3
	bne	mbmerr
	add.l	#4,a1
	dbf	d1,kb1
	cmp.l	d0,a1
	bls	mtest1

	move.l	mbmemsize,a0	! memory size in multiboard
kbend:	add.l	#0x400000,a0	! add start address
	movem.l	(sp)+,d2-d3	! restore regs
	bra	tstend1		! a0 = end address of memory
				! a1, d0 changed

mbmerr:	move.l	#0,a0		! no memory found
	bra	kbend

nomulti:			! exception occured, no multiboard present
	unlk	a2		! clean up stack, no multiboard present
				! old exception handler still a1
#endif /* FULL_MULTIBOARD_TEST */

multiend:

#endif /* MULTIBOARD */

	move.l	#tstend,8	! install own error handler
next:
! offset 8 is used to avoid accessing unwritable memory if called with addr 0)
	move.w	8(a0),d0	! save value
	move.w	#0x55aa,8(a0)
	not.w	8(a0)
	cmp.w	#0xaa55,8(a0)
	bne	tstend
	move.w	d0,8(a0)	! restore original value
	add.l	#0x40000,a0	! test every 256 k
	bra	next
tstend:
	move.l	a1,8		! restore old bus error exception handler
	unlk	a2		! remove exception frame.
tstend1:
	cmp.b	#2,_proctyp
	ble	end	
	cmp.b	#4,_proctyp
	bge	endcache4	
	move.l	#0x3919,d0	! re-enable data cache
	.data4	0x4e7b0002	! movec.l d0,cacr
	bra	end
endcache4:
	move.l	#0x8080,d0	! re-enable data cache
	.data4	0x4e7b0002	! movec.l d0,cacr
end:
	sub.l	4(sp),a0	! calculate offset
	move.l	a0,d0
	rts

	.sect	.data

_sizes:
	.data2	0x526F,0,0,0,0,0,0,0
#if (ENABLE_NETWORKING == 1)
	.data2	0,0,0x526F,0
#endif /* ENABLE_NETWORKING == 1 */

#if (MACHINE == ATARI)
conf:
	.extern	_proc
	.data4	_proc
	.extern	_keynorm
	.data4	_keynorm
	.extern	_keyshft
	.data4	_keyshft
	.extern	_keycaps
	.data4	_keycaps
	.extern	_font8
	.data4	_font8
	.extern	_font16
	.data4	_font16
	.extern	_boot_parameters
	.data4	_boot_parameters
# if PIXEL_WONDER
	.extern	_res
	.data4	_res
# else
	.data4	0
# endif
	.data4	0

clkcnt: .data2	4
#if FULL_MULTIBOARD_TEST
mbmemsize:
	.data4	0
#endif /* FULL_MULTIBOARD_TEST */
#if (ATARI_TYPE == DETECT_TYPE)
_atari_type:
	.data2	ATARI_TYPE
#endif /* ATARI_TYPE == DETECT_TYPE */
#endif /* MACHINE == ATARI */

	.space K_STACK_BYTES
k_stktop:

.proc_ptr:			! old proc_ptr
	.data4	0
.pick_proc:			! call pick_proc?
	.data1	0
.vectnum:			! trap/interrupt vector number
	.data1	0
_proctyp:	! processor type, 0: 68000/68008, 1: 68010, 2: 68020,
		!		  3: 68030, 4: 68040
	.data1	0
_fpptyp:	! fpu processor type, 0: none, 1: 68881, 2: 68882, 4: 68040
	.data1	0
_mmutyp:	! mmu type, 0: none, 1: 68851, 3: 68030, 4: 68040
	.data1	0

! below are the frame sizes for 680x0 processors, x>0, size in bytes, +4 !!
! the sizes which are -1 are unused as far as I know.
.framesizes:
	.data4	12, 12, 16, 16, -1, -1, -1, 34
	.data4	62, 24, 36, 96, -1, -1, -1, -1

#ifdef ACK
	.sect	.text
	.extern	_write
	.extern	EXIT
_write:
EXIT:
	bra	EXIT

	.sect	.text
.trpim:	.data2	0
.trppc:	.data4	0
#endif	/* ACK */

	.define	__end
	.sect	.end
__end:
#endif
