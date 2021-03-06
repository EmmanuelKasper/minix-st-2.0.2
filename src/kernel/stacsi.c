/*
 * This file contains a driver for the Hard Disk Controller (HDC)
 * on the Atari ST.
 * If yor drive is an old Supra drive you perhaps want to compile 
 * using -DOLDSUPRA. The code added with #ifdef OLDSUPRA/#endif is taken 
 * from usenet and not verified (since I do not have such a drive)
 *
 * The driver supports two operations: read a block and write a block.
 * It accepts two messages, one for reading and one for writing,
 * both using message format m2 and with the same parameters:
 *
 *    m_type      DEVICE    PROC_NR     COUNT    POSITION  ADRRESS
 * ----------------------------------------------------------------
 * |  DEV_READ  | device  | proc nr |  bytes  |  offset | buf ptr |
 * |------------+---------+---------+---------+---------+---------|
 * | DEV_WRITE  | device  | proc nr |  bytes  |  offset | buf ptr |
 * |--------------------------------------------------------------|
 * |SCATTERED_IO| device  | proc nr | requests|         | iov ptr |
#if (HD_CLOCK == 1)
 * |------------+---------+---------+---------+---------+---------|
 * | DEV_IOCTL  | device  | proc nr |func code|         | address |
#endif 
 * |------------+---------+---------+---------+---------+---------|
 * | DEV_OPEN   | device  | proc nr |         |         |         |
 * |------------+---------+---------+---------+---------+---------|
 * | DEV_CLOSE  | device  | proc nr |         |         |         |
 * ----------------------------------------------------------------
 *
 * The file contains one entry point:
 *
 *	winchester_task: main entry when system is brought up
 *
 * A single ST may have several controllers.
#if (USE_LUN1 == 1)
 * Each controller support up to two drives.
#endif
 * Each physical drive contains normally up to 4 partitions, but when extended
 * partitioning is used this can be up to 12.
 * each physical drive contains 16 minor devices, so that
 *	DEVICE = (contr << (MINOR_SHIFT+1) | (drive << MINOR_SHIFT | minor;
 * These 16 minor devices per drive are interpreted as follows:
 *	d+0:	whole disk
 *	d+1:	partition 1	according to drive descriptor
 *	d+2:	partition 2	according to drive descriptor
 *	d+3:	partition 3	according to drive descriptor
 *	d+4:	partition 4	according to drive descriptor
 *	d+5:	partition 5	according to drive descriptor
 *	d+6:	partition 6	according to drive descriptor
 *	d+7:	partition 7	according to drive descriptor
 *	d+8:	partition 8	according to drive descriptor
 *	d+9:	partition 9	according to drive descriptor
 *	d+10:	partition 10	according to drive descriptor
 *	d+11:	partition 11	according to drive descriptor
 *	d+12:	partition 12	according to drive descriptor
 *	d+13:	drive descriptor (sector 0)
 *	d+14:	not used (later: whole disk but no badblock translation)
 *	d+15:	not used (later: head into shipping position)
 *
 * The suggested naming scheme is hd followed by the drive number and a 
 * 	letter (a-p) specifiying the partition. 
 *	E.g. /dev/hd1c will correspond with partition 2 on drive 1
 *		drive 1 is the drive at controller 0, lun 1.
 *
 * TODO:
 * - support removable media (hooks are already present)
 * - find out which partitions are minix and which not
 * - obey bad sector info
 * - only access partitions with pi_id == "MIX" (prog to modify pi_id?)
 */

#include "kernel.h"
#if (MACHINE == ATARI)
#include "driver.h"
#include "drvlib.h"

#include <string.h>

#include "staddr.h"
#include "stmfp.h"
#include "sthdc.h"
#include "stdma.h"

#if (NR_ACSI_DRIVES != 0)

#define	winchester_task	acsi_task

#define	ERR		(-1)
#define	USE_BUF		(DMA_BUF_SIZE > BLOCK_SIZE)
#define TRACE(x)	/* x */
#define DEBUG(x)	x
#define	TRACE1(x)	x
#define	STRICT_PARTITIONING 0

/* Parameters for the disk drive. */
#define SECTOR_SIZE	512	/* physical sector size in bytes */
#define MINORS_DRIVE	16
#define MINOR_SHIFT	4	/* 2 ** MINOR_SHIFT == #minors/drive */
#define	MAX_MINOR	(NR_ACSI_DRIVES<<MINOR_SHIFT)
#define MAX_ERRORS	10	/* how often to try rd/wt before quitting */
#define MAX_SEC0_ERRORS	 3	/* how often to try rd/wt sector 0 before */
				/* quitting, must not be too high because */
				/* non existing drives give error messages*/
#if ENABLE_SHIPPING
#define	ALWAYS_ALARM	0	/* 1 only for real hardware, if you are   */
						/* suspicious		  */
#else
#define	ALWAYS_ALARM	0
#endif /* ENABLE_SHIPPING */

#if (USE_LUN1 == 1)
#define	TARGET(drive)	((drive>>1)<<5)
#define	LUN(drive)	((drive&1)<<5)	/* LUN is lowest bit of drive */
#else
#define	TARGET(drive)	(drive<<5)
#define	LUN(drive)	(0)
#endif /* USE_LUN1 */

#if (!USE_BUF && (NR_IOREQS * BLOCK_SIZE) >= 0x10000L) || (DMA_BUF_SIZE >= 0x10000L)
#define	COUNTER_TYPE	unsigned long
#else
#define	COUNTER_TYPE	unsigned int
#endif /* !USE_BUF && (NR_IOREQS*BLOCK_SIZE) >= 0x10000L || DMA_BUF_SIZE >= 0x10000L */

/* dis/enable interrupts */
#define IENABLE()	MFP->mf_ierb |= IB_DINT
#define IDISABLE()	MFP->mf_ierb &= ~IB_DINT

/* timing constants */
#define	HDC_DELAY	0	/* for dma[rw]dat() 			*/
#define	HDC_POLL	30000	/* polling of MFP for interrupt bit 	*/
#define COMMAND_DELAY	1000	/* delay between two acsi commands 	*/
#define	SHIP_DELAY	50	/* delay for ship command, mult by HDC_POLL */
#define	INT_OFF		0	/* wait for HDC by polling 		*/
#define	INT_ON		1	/* expect interrupt from HDC 		*/
#define	WAKEUP		(30*HZ)	/* long alarm time			*/
#define	REMOVE_ALARM()	if (acsi_command == OK) clock_mess(0, NULL)
#if ALWAYS_ALARM
#define	SHORT_WAKEUP	(1*HZ)	/* short alarm time			*/
#endif /* ALWAYS_ALARM */

#if (FAST_DISK != 0)
PRIVATE	int opening;		/* opening a device, delay between commands */
#endif /* FAST_DISK */

PRIVATE struct pi pi[MAX_MINOR];/* begin and size of any minor device 	*/
PRIVATE message mess;		/* message buffer for in and out 	*/

PRIVATE struct acsi_disk {
  unsigned open_ct;			/* in-use count			*/
  struct device part[MINORS_DRIVE];	/* partitions on disk		*/
} acsi_disk[NR_ACSI_DRIVES], *acsi_wn;

PRIVATE struct trans {
  struct iorequest_s *iop;		/* belongs to this I/O request	*/
  unsigned long block;			/* first sector to transfer	*/
  unsigned count;			/* byte count			*/
  phys_bytes phys;			/* user physical address	*/
  phys_bytes dma;			/* dma address			*/
} acsitrans[NR_IOREQS];

PRIVATE int acsi_tasknr;		/* ACSI task number		*/
PRIVATE struct trans *acsi_tp;		/* to add transfer requests	*/
PRIVATE COUNTER_TYPE acsi_count;	/* number of bytes to transfer	*/
PRIVATE unsigned long acsi_nextblock;	/* next block to transfer	*/
PRIVATE int acsi_opcode;		/* DEV_READ or DEV_WRITE	*/
PRIVATE int acsi_drive;			/* selected drive		*/
PRIVATE struct proc *acsi_w_proc;	/* process doing the request	*/
PRIVATE struct device *acsi_dv;		/* device's base and size	*/
PRIVATE	acsi_command;			/* actual ACSI command		*/

FORWARD _PROTOTYPE( struct device *acsi_prepare, (int device)		);
FORWARD	_PROTOTYPE( char *acsi_name, (void)				);
FORWARD	_PROTOTYPE( int acsi_open, (struct driver *dp, message *m_ptr)	);
FORWARD	_PROTOTYPE( int acsi_close, (struct driver *dp, message *m_ptr)	);
FORWARD	_PROTOTYPE( int acsi_schedule, (int proc_nr, struct iorequest_s *iop));
FORWARD	_PROTOTYPE( int acsi_finish, (void)				);
FORWARD _PROTOTYPE( int acsi_transfer, (struct trans *tp, COUNTER_TYPE count));
FORWARD _PROTOTYPE( int acsi_diocntl, (struct driver *dp, message *mp)	);
FORWARD	_PROTOTYPE( void acsi_geometry, (struct partition *entry)	);

FORWARD _PROTOTYPE( int do_open, (void)					);
FORWARD _PROTOTYPE( int do_xfer, (phys_bytes address,
			long sector, int count, int rw)			);
#if ENABLE_SHIPPING
FORWARD	_PROTOTYPE( int cmd_xfer, (int command, int ship)		);
#endif /* ENABLE_SHIPPING */
FORWARD _PROTOTYPE( int do_cmd, (unsigned char *cmd, int par_cnt,
			int blk_cnt, int wrbit, int int_mode)		);
FORWARD _PROTOTYPE( int poll, (int delay)				);
FORWARD _PROTOTYPE( void hdcint, (void)					);
FORWARD _PROTOTYPE( void acsi_timeout, (void)				);
FORWARD _PROTOTYPE( void request_sense, (int command, long sector)	);

/* Entry points to this driver */
PRIVATE struct driver acsi_dtab = {
 acsi_name,		/* current device's name			*/
 acsi_open,		/* open or mount request, initialize device	*/
 acsi_close,		/* release device				*/
 acsi_diocntl,		/* get or set partition's geometry		*/
 acsi_prepare,		/* prepare for I/O on a given minor device	*/
 acsi_schedule,		/* precompute cylinder, head, sector, etc.	*/
 acsi_finish,		/* do th I/O					*/
 nop_cleanup,		/* no cleanup needed				*/
 acsi_geometry		/* tell the geometry of the disk		*/
};

/*===========================================================================*
 *				winchester_task				     *
 *===========================================================================*/
PUBLIC void winchester_task()
{
  acsi_tasknr = proc_number(proc_ptr);
  
  driver_task(&acsi_dtab);
}

/*===========================================================================*
 *				acsi_prepare				     *
 *===========================================================================*/
PRIVATE struct device *acsi_prepare(device)
int device;
{
/* Prepare for I/O on a device */
  acsi_count = 0;			/* Nothing to transfer yet	*/
  
  if (device >= 0 && device < MAX_MINOR) {	/* no subpartition	*/
  	acsi_drive = device >> MINOR_SHIFT;	/* save drive number	*/
  	acsi_wn = &acsi_disk[acsi_drive];
  	acsi_dv = &acsi_wn->part[device % MINORS_DRIVE];
  } else
  	return(NIL_DEV);

  return(acsi_drive < NR_ACSI_DRIVES ? acsi_dv : NIL_DEV);
}

/*===========================================================================*
 *				acsi_name				     *
 *===========================================================================*/
PRIVATE char *acsi_name()
{
/* Return a name for the current drive */
  static char name[] = "hd0p";
  
  name[2] = '0' + acsi_drive;
  name[3] = 'a' + (acsi_dv - &acsi_wn->part[0]);

  return name;
}

/*===========================================================================*
 *				acsi_open				     *
 *===========================================================================*/
PRIVATE int acsi_open(dp, m_ptr)
struct driver *dp;
message *m_ptr;
{
/* Device open: read the partition table(s) */
  int r;

  if (acsi_prepare(m_ptr->DEVICE) == NIL_DEV) return(ENXIO);
  
  acsi_wn->open_ct++;
#if (FAST_DISK != 0)
  opening = TRUE;
#endif /* FAST_DISK == 0 */
  
#if 0 
  if (acsi_wn->open_ct == 1) {
  	/* partition the disk */
  	partition(&acsi_dtab, acsi_drive * DEV_PER_DRIVE, P_PRIMARY);
  }
#else
  if (acsi_wn->open_ct == 1) {
	r = do_open();
	if (r != OK) {
		acsi_wn->open_ct--;
		return(r);
	}
  }
#endif
#if (FAST_DISK != 0)
  opening = FALSE;
#endif /* FAST_DISK == 0 */
  return(OK);
}

/*===========================================================================*
 *				acsi_close				     *
 *===========================================================================*/
 PRIVATE int acsi_close(dp, m_ptr)
 struct driver *dp;
 message *m_ptr;
 {
/* Device closes: Release a device */
  int minor, r;
  message mess;

  TRACE(printf("ACSI: Closing device %d\n", m_ptr->DEVICE));
  if (acsi_prepare(m_ptr->DEVICE) == NIL_DEV) return(ENXIO);
  minor = acsi_drive << MINOR_SHIFT;

  if (pi[minor].pi_flag == PI_INVALID)
	if (acsi_wn->open_ct == 0) {
  		printf("hd: Closing non open device %d\n", m_ptr->DEVICE);
  		return(OK);
  	}
#if 0
  /* if mp->COUNT != 0 then the caller still has another pointer to the
     device (created by a dup or fork call), so don't do the final close */
  /* not in Minix 2 */
  if (m_ptr->COUNT != 0) {
	printf("m_ptr->COUNT = %d, != 0\n", m_ptr->COUNT);
	/*return(OK);*/
  }
#endif
  if (--acsi_wn->open_ct != 0) {
	TRACE(printf("ACSI close: drive open counter = %d\n", acsi_wn->open_ct));
	return(OK);
  }
#if ENABLE_SHIPPING
  if ((m_ptr->DEVICE & 15) == 15) {
	int i;
	dmagrab(WINCHESTER, hdcint);
	r = cmd_xfer(HD_SHP, 0);
#if 0
	for (i = SHIP_DELAY; i > 0 && r != OK; i--)
		r = poll(HDC_POLL);
#endif
	if (r != OK) {
		TRACE(
		printf("ACSI: ship command failed for drive %d\n", acsi_drive));
		r = EIO;
	}
	dmafree(WINCHESTER);
  }
#endif /* ENABLE_SHIPPING */
  for (r = 0; r < MINORS_DRIVE; r++, minor++)
	pi[minor].pi_flag = PI_INVALID;
  return(OK);
}

/*===========================================================================*
 *				acsi_schedule				     *
 *===========================================================================*/
PRIVATE int acsi_schedule(proc_nr, iop)
int proc_nr;			/* process doing the request		*/
struct iorequest_s *iop;	/* pointer to read or write request	*/
{
  int r, opcode;
  unsigned long pos;
  unsigned nbytes, count;
  unsigned long block;
  phys_bytes user_phys, dma_phys;

  /* This many bytes to read/write */
  nbytes = iop->io_nbytes;
  if ((nbytes & SECTOR_MASK) != 0) return(iop->io_nbytes = EINVAL);
  
  /* From/to this position on the device */
  pos = iop->io_position;
  if ((pos & SECTOR_MASK) != 0) return(iop->io_nbytes = EINVAL);

  /* To/from this user address */
  user_phys = numap(proc_nr, (vir_bytes) iop->io_buf, (vir_bytes) nbytes);
  if (user_phys == 0) return(iop->io_nbytes = EINVAL);

  /* Save proc */
  acsi_w_proc = proc_addr(proc_nr);
  
  /* Read or write? */
  opcode = iop->io_request & ~OPTIONAL_IO;

  /* Which block on disk and how close to EOF */
  if (pos >= acsi_dv->dv_size) return(OK);		/* at EOF	*/
  if (pos + nbytes > acsi_dv->dv_size) nbytes = acsi_dv->dv_size - pos;
  block = (acsi_dv->dv_base + pos) >> SECTOR_SHIFT;
  
  if (USE_BUF && acsi_count > 0 &&  block != acsi_nextblock) {
  	/* This new request can't be chained to the job being built */
  	if ((r = acsi_finish()) != OK) return(r);
  }

  /* The next consecutive block */
  if (USE_BUF) acsi_nextblock = block + (nbytes >> SECTOR_SHIFT);

  /* While there are "unscheduled" bytes in the request */
  do {
  	count = nbytes;
  
  	if (USE_BUF) {
  		if (acsi_count == DMA_BUF_SIZE) {
  			/* Can't transfer more than the buffer allows */
  			if ((r = acsi_finish()) != OK) return(r);
  		}
  		if (acsi_count + count > DMA_BUF_SIZE)
  			count = DMA_BUF_SIZE - acsi_count;
  	} else {
  		if (acsi_tp == acsitrans + NR_IOREQS) {
  			/* All transfer slots in use */
 	 		if ((r = acsi_finish()) != OK) return(r);
  		}
  	}
  	
  	if (acsi_count == 0) {
  		/* The first request in a row, initialize */
  		acsi_opcode = opcode;
  		acsi_tp = acsitrans;
  	}
  	
  	if (USE_BUF) {
  		dma_phys = tmp_phys + acsi_count;
  	} else {
  		dma_phys = user_phys;
  	}
  	
  	/* Store I/O parameters */
  	acsi_tp->iop = iop;
  	acsi_tp->block = block;
  	acsi_tp->count = count;
  	acsi_tp->phys = user_phys;
	acsi_tp->dma = dma_phys;

  	/* Update counters */
  	acsi_tp++;
  	acsi_count += count;
  	block += count >> SECTOR_SHIFT;
  	user_phys += count;
  	nbytes -= count;
  } while (nbytes > 0);

  return(OK);
}

/*===========================================================================*
 *				acsi_finish				     *
 *===========================================================================*/
PRIVATE int acsi_finish()
{
  struct trans *tp = acsitrans, *tp2;
  COUNTER_TYPE count;
  int r, errors = 0, many = USE_BUF;
  
  if (acsi_count == 0) return(OK);		/* spurious finish	*/
  
  do {
  	if (acsi_opcode == DEV_WRITE) {
  		tp2 = tp;
  		count = 0;
  		do {
			if (USE_BUF || tp2->dma == tmp_phys) {
				phys_copy(tp2->phys, tp2->dma,
					(phys_bytes) tp2->count);
			}
			count += tp2->count;
	  		tp2++;
  		} while (many && count < acsi_count);
  	} else {
  		count = many ? acsi_count : tp->count;
  	}
  	
  	/* Perform the transfer */
  	r = acsi_transfer(tp, count);
  	
  	if (r != OK) {
  		if (++errors == MAX_ERRORS)
			return(tp->iop->io_nbytes = EIO);
  		many = 0;
  		continue;
  	}
  	errors = 0;
  	
  	acsi_count -= count;
  	
  	do {
  		if (acsi_opcode == DEV_READ) {
			if (USE_BUF || tp->dma == tmp_phys) {
					phys_copy(tp->dma, tp->phys,
						(phys_bytes) tp->count);
			}
  		}
  		tp->iop->io_nbytes -= tp->count;
  		count -= tp->count;
  		tp++;
  	} while (count > 0);
  } while (acsi_count > 0);
  
  return(OK);
}
  	
/*===========================================================================*
 *				acsi_geometry				     *
 *===========================================================================*/
PRIVATE void acsi_geometry(entry)
struct partition *entry;
{
  entry->cylinders = (acsi_wn->part[0].dv_size >> SECTOR_SHIFT) / (64 * 32);
  entry->heads = 64;
  entry->sectors = 32;
}

/*===========================================================================*
 *				do_open					     *
 *===========================================================================*/
PRIVATE int do_open()
{
  /* this function initialises the partition table for a device */
  static struct hi hi;		/* buffer for drive descriptor */
  register int r, maxminor, minor, dev;
  int errors;
 
  dev = acsi_drive;

  /* this function is called too often (on every partition mount) */
  minor = dev << MINOR_SHIFT;
  maxminor = minor + NPARTS + ADDPARTS;
  /* read sector 0 of the drive */
  dmagrab(WINCHESTER, hdcint);
#if ENABLE_SHIPPING
  for (errors = 0; errors < MAX_SEC0_ERRORS; errors++) {
	r = cmd_xfer(HD_TUR, 0);
	if (r == OK)
		break;
  }
  if (r != OK) {
	printf("ACSI: test unit ready failed for drive %d\n", dev);
	TRACE(printf("Sending load for %d\n", dev));
	r = cmd_xfer(HD_SHP, 1);		/* in case it has been shipped */
  }
  if (r != OK) {
	printf("ASCI: load for drive %d failed\n", dev);
	dmafree(WINCHESTER);
	return(EIO);
  }
#endif /* ENABLE_SHIPPING */
  /* retry a number of times */
  for (errors = 0; errors < MAX_SEC0_ERRORS; errors++) {
	r = do_xfer((phys_bytes)&hi, 0L, 1, DEV_READ);
	if (r == OK)
		break;		/* if successful, exit loop */
  }
  dmafree(WINCHESTER);
  /* if the read failed return */
  /* if the read succeeded fill partition table */
  /* note that since pi is in the data segment all entries are */
  /* initialised to 0 (which is also PI_INVALID) */
    
  if (r != OK)
	return(EIO);

  /* the first minor number is for the whole device */
  /* don't overwrite partition table; start at sector 1 */
  pi[minor].pi_start = 1;
  pi[minor].pi_size = hi.hd_size - 1;
  pi[minor].pi_flag = PI_VALID;

  for (r = 0, minor++; (r < NPARTS) && (minor <= maxminor); r++) {
      if (strncmp(hi.hd_pi[r].pi_id, "XGM", 3) == 0)
      {	/* yikes, an extended partition */
      	/* only one extended partition can occur in the root sector */
	struct hi hiext; /* buffer for extended part. drive descriptor */
	register int s, ext; /* ext gives the number of the extended part. */
	long offset;	 /* where the next extended root sector is */
	
        ext = r;
	hiext.hd_pi[ext].pi_start = 0;
	/* extended partitions contain an extended root sector
	   which is filled with one regular, and zero or one
	   extended partitions. 
	   The start of all extended partitions is relative to the
	   extended partition in the real root sector
          */
          while (ext >= 0)
          {
	    offset = hi.hd_pi[r].pi_start + hiext.hd_pi[ext].pi_start;
	    ext = -1;
	    dmagrab(WINCHESTER, hdcint);
	    /* retry a number of times */
	    for (errors = 0; errors < MAX_SEC0_ERRORS; errors++) {
	        s = do_xfer((phys_bytes)&hiext,
	                    offset, 1, DEV_READ);
	        if (s == OK)
		    break;		/* if successful, exit loop */
	    }
	    dmafree(WINCHESTER);
	    for (s = 0; (s < NPARTS) && (minor <= maxminor); s++) {
	    	if (hiext.hd_pi[s].pi_flag == PI_VALID)
	    	{
	            if (strncmp(hiext.hd_pi[s].pi_id, "XGM", 3) == 0)
	                ext = s;
		    else 
		    {
			if (strncmp(hiext.hd_pi[s].pi_id, "GEM", 3) != 0 &&
			    strncmp(hiext.hd_pi[s].pi_id, "BGM", 3) != 0 &&
			    strncmp(hiext.hd_pi[s].pi_id, "MIX", 3) != 0 )
					pi[minor].pi_flag = PI_INVALID;
			else {
			    pi[minor] = hiext.hd_pi[s];
			    pi[minor].pi_start += offset;
			    /* some sanity checks */
#if STRICT_PARTITIONING
			    if ((pi[minor].pi_start + pi[minor].pi_size - 1 >
			           hi.hd_pi[r].pi_start + hi.hd_pi[r].pi_size) ||
			        (pi[minor].pi_start < hi.hd_pi[r].pi_start) ||
			        (pi[minor].pi_size < 0))
				    pi[minor].pi_flag = PI_INVALID;
#else
			    if ((pi[minor].pi_start + pi[minor].pi_size >
			           hi.hd_size) ||
			        (pi[minor].pi_start < hi.hd_pi[r].pi_start) ||
			        (pi[minor].pi_size < 0))
				    pi[minor].pi_flag = PI_INVALID;

			    /* Some partitioning tools make the size of the
			     * extended partition a few sectors too small.
			     * TOS doesn't care, so we do. Adjust the size.
			     */
			    else if (pi[minor].pi_start + pi[minor].pi_size >
				hi.hd_pi[r].pi_start + hi.hd_pi[r].pi_size) {
				/* partition extends extended partition */
				if (pi[minor].pi_start + pi[minor].pi_size - 10 <=
				    hi.hd_pi[r].pi_start + hi.hd_pi[r].pi_size) {
				    /* only a few sectors, adjust */
				    hi.hd_pi[r].pi_size =
					pi[minor].pi_start + pi[minor].pi_size
						- hi.hd_pi[r].pi_start;
				    printf("Warning: adjusted extended partition size\n");
				} else
				    pi[minor].pi_flag = PI_INVALID;
			    }
#endif /* STRICT_PARTITIONING */
			}
			minor++;
		    }
	    	}
	    }
	}
      } else {
	if (strncmp(hi.hd_pi[r].pi_id, "GEM", 3) != 0 &&
	    strncmp(hi.hd_pi[r].pi_id, "BGM", 3) != 0 &&
	    strncmp(hi.hd_pi[r].pi_id, "MIX", 3) != 0 )
			pi[minor].pi_flag = PI_INVALID;
	else
			pi[minor] = hi.hd_pi[r];
	minor++;
      }
  }
#if (ADDPARTS != 0)
  for (r = 0; (r < ADDPARTS) && (minor <= maxminor); r++, minor++){
	/* It is better to check. The danger is too big, in case you
	 * boot with a wrong diskette. (jw)
	 */
	if (strncmp(hi.hd_addpi[r].pi_id, "GEM", 3) != 0 &&
	    strncmp(hi.hd_addpi[r].pi_id, "BGM", 3) != 0 &&
	    strncmp(hi.hd_addpi[r].pi_id, "MIX", 3) != 0 )
			pi[minor].pi_flag = PI_INVALID;
	else
			pi[minor] = hi.hd_addpi[r];
  }
#endif
  /* the next partition is for sector 0 */
  pi[maxminor + 1].pi_size = 1;
  pi[maxminor + 1].pi_flag = PI_VALID;

  minor = dev << MINOR_SHIFT;
  for (r = 0; r < MINORS_DRIVE; r++, minor++)
     if (pi[minor].pi_flag != PI_INVALID)
     {
	    
	/* some sanity checks */
        if ((pi[minor].pi_start + pi[minor].pi_size -1 > hi.hd_size) ||
	    (pi[minor].pi_start < 0) ||
	    (pi[minor].pi_size < 0)) {
		pi[minor].pi_flag = PI_INVALID;
	} else {
		/* fill in disk partition info */
		acsi_disk[dev].part[r].dv_base = pi[minor].pi_start<<SECTOR_SHIFT;
		acsi_disk[dev].part[r].dv_size = pi[minor].pi_size<<SECTOR_SHIFT;
	}    
  	DEBUG(
		printf("hd%d%c: 1st=%D/0x%lx, tot=%D/0x%lx\n", minor >> MINOR_SHIFT,
			(minor % MINORS_DRIVE) + 'a',
			pi[minor].pi_start, pi[minor].pi_start,
			pi[minor].pi_size,  pi[minor].pi_size
		);
	);
      }
  return(OK);
}

/*===========================================================================*
 *				acsi_transfer				     *
 *===========================================================================*/
PRIVATE int acsi_transfer(tp, count)
struct trans *tp;
COUNTER_TYPE count;		/* byte count to transfer */
{
  register long		secnum;
  register phys_bytes	address;
  int			r, errors;

  secnum = tp->block;
  address = tp->dma;
  count >>= SECTOR_SHIFT;  /* /= SECTOR_SIZE; */

  TRACE(printf("hd%d: %s: sec=%D; cnt=%d, adr=%D, proc_nr=%d\n",
	acsi_drive, acsi_opcode == DEV_READ ? "read" : "write", secnum, count, address, proc_number(acsi_w_proc)));

  acsi_w_proc->p_physio = 1;		/* disable (un)shadowing */
  dmagrab(WINCHESTER, hdcint);
  /* This loop allows a failed operation to be repeated. */
  for (errors = 0; errors < MAX_ERRORS; errors++) {
	r = do_xfer(address, secnum, (int)count, acsi_opcode);
	if (r == OK)
		break;		/* if successful, exit loop */
  }
  dmafree(WINCHESTER);
  acsi_w_proc->p_physio = 0;		/* enable (un)shadowing */
  if (r != OK)
	return(EIO);

  return(OK);
}

/*===========================================================================*
 *				do_xfer					     *
 *===========================================================================*/
PRIVATE int do_xfer(address, sector, count, rw)
phys_bytes 	address;
long		sector;
int		count;
int		rw;
{
  register int	r, s, wrbit;
  unsigned char cmd[6];

  /*
   * Carry out the transfer. All parameters have been checked and
   * are set up properly.
   *
   * Every single byte written to the hdc will cause an interrupt.
   * Thus disable interrupts while communicating with hdc. Ready test
   * will be done by busy waiting. Only for real hard disk operations
   * interrupts will be enabled.
   */
  TRACE(printf("hd drive:%d address:0x%lx sector:%ld count=%d cmd:%s\n",
	acsi_drive, address, sector, count, (rw==DEV_READ)?"READ":"WRITE")
  );
  dmaaddr(address);		/* DMA address setup */
  if (rw == DEV_READ) {
	wrbit = 0;
        cmd[0] = TARGET(acsi_drive) | (HD_RD&0x1F);
  } else {
	wrbit = WRBIT;
        cmd[0] = TARGET(acsi_drive) | (HD_WR&0x1F);
  }
  cmd[1] = LUN(acsi_drive) | (((int)(sector>>16))&0x1F);
  cmd[2] = ((int)sector >> 8) & 0xff;
  cmd[3] = sector & 0xff;
  cmd[4] = count & 0xff;
  cmd[5] = 0;
  r = do_cmd(cmd, 6, count, wrbit, INT_ON);	/* command parameters */
  if (r == OK) {
#if ALWAYS_ALARM
	acsi_command = rw;
	clock_mess(WAKEUP, acsi_timeout);
#endif /* ALWAYS_ALARM */

	receive(HARDWARE, &mess);	/* Wait for interrupt. */

#if ALWAYS_ALARM
	REMOVE_ALARM();
	if (acsi_command != OK) {
		/* XXX: should we leave here or do we get some usefull information
		   from request sense?
		 */
		dmardat(FDC, HDC_DELAY);	/* finish HDC command */
		return(ERR);
	}
#endif /* ALWAYS_ALARM */
#ifdef OLDSUPRA
	IDISABLE();
#endif
	s = dmardat(wrbit | FDC | HDC | A0, HDC_DELAY);
	/* certain old supra drives may sometimes generate 
	   spurious errors which need to be masked out */
	if (s & HDC_CC) {
		request_sense(rw, sector);
		r = ERR;
	}
  }
  dmardat(FDC, HDC_DELAY);		/* finish HDC command */

#if (FAST_DISK == 0)
  for (s = 0; s< COMMAND_DELAY; s++) /* nothing */;
#else
  if (opening == TRUE)
	for (s = 0; s< COMMAND_DELAY; s++) /* nothing */;
#endif /* FAST_DISK == 0 */
  return(r);
}

#if ENABLE_SHIPPING
/*===========================================================================*
 *				cmd_xfer				     *
 *===========================================================================*/
PRIVATE int cmd_xfer(command, ship)
int 		command;
int		ship;
{
  int		i, s, r;
  unsigned char cmd[6];

  /* do_xfer() for HDC commands without DMA setup and disk io. */

  TRACE(printf("hd drive:%d command:%x ship=%d\n", drive, command, ship));

  if (command != HD_TUR && command != HD_RST && command != HD_SHP) {
	TRACE(printf("cmd_xfer: illegal command %x\n", command));
	return(ERR);
  }

  cmd[0] = TARGET(acsi_drive) | (command&0x1F);
  cmd[1] = LUN(acsi_drive);
  cmd[2] = 0;
  cmd[3] = 0;
  cmd[4] = ship & 0x01;
  cmd[5] = 0;
  r = do_cmd(cmd, 6, 0, 0, (command == HD_TUR) ? INT_OFF : INT_ON);
  if (r == OK) {
	if (command == HD_TUR) {
		for (r = 1, i = 0; r != OK, i < 4; i++)
			r = poll(HDC_POLL);
		s = dmardat(FDC | HDC | A0, HDC_DELAY);
		/* certain old supra drives may sometimes generate 
	  	   spurious errors which need to be masked out */
		if (s & HDC_CC) {
			request_sense(command, 0L);
			r = ERR;
		}
  	} else {
		acsi_command = command;
		clock_mess(WAKEUP, acsi_timeout);
		receive(HARDWARE, &mess);
		REMOVE_ALARM();
		if (acsi_command == OK) {	/* received interrupt */
			s = dmardat(FDC | HDC | A0, HDC_DELAY);
			/* certain old supra drives may sometimes generate 
		  	   spurious errors which need to be masked out */
			if (s & HDC_CC) {
				request_sense(command, 0L);
				r = ERR;
			}
		} else
			TRACE1(printf("ACSI: ALARM caught drive %d command 0x%x\n",
					acsi_drive, command));
	}
  } else {
	TRACE1(printf("do_cmd() failed for drive %d command 0x%x\n",
							acsi_drive, command));
  }
  dmardat(FDC, HDC_DELAY);	/* finish HDC command */
  return r;
}
#endif /* ENABLE_SHIPPING */

/*===========================================================================*
 *				do_cmd					     *
 *===========================================================================*/
PRIVATE int do_cmd(cmd, cmd_cnt, blk_cnt, wrbit, int_mode)
unsigned char *cmd;
int cmd_cnt;
int blk_cnt;
int wrbit;
int int_mode;
{
  TRACE(printf("do_cmd: cmd: %lx, cmd_cnt: %d, blk_cnt %d, wrbit: %d\n",
	       cmd, cmd_cnt, blk_cnt, wrbit));
  IDISABLE();
#if (AHDILIKE == 1)
  DMA->dma_mode = FDC;
  /* fill the sector count register */
  dmacomm(wrbit | FDC | SCREG, blk_cnt, HDC_DELAY);

  DMA->dma_mode = wrbit | FDC | HDC;
  --cmd_cnt;
  dmawcmd((short) *cmd++, wrbit | FDC | HDC | A0);
  if (poll(COMMAND_DELAY) != OK)
	return(ERR);
  while (--cmd_cnt > 0)	/* do not handle the last byte */
  {
    dmawcmd((short) *cmd++, wrbit | FDC | HDC | A0);
    if (poll(COMMAND_DELAY) != OK)
	return(ERR);
  }
  if (int_mode == INT_ON) IENABLE();
  /* write the last command byte */
  dmawdat(wrbit | FDC | HDC | A0, *cmd, HDC_DELAY);
  DMA->dma_mode = wrbit | FDC | HDC | A0;
  DMA->dma_data = 0;
#else
  DMA->dma_mode = FDC | HDC;
  --cmd_cnt;
  dmawcmd((short) *cmd++, wrbit | FDC | HDC | A0);
  DMA->dma_mode = FDC | HDC | A0;
  if (poll(COMMAND_DELAY) != OK)
	return(ERR);
  while (--cmd_cnt > 0)	/* do not handle the last byte */
  {
    dmawcmd((short) *cmd++, FDC | HDC | A0);
    if (poll(COMMAND_DELAY) != OK)
	return(ERR);
  }
  if (int_mode == INT_ON) IENABLE();
  /* fill the sector count register */
  dmacomm(wrbit | FDC | SCREG, blk_cnt, HDC_DELAY);
  /* write the last command byte */
  dmawdat(wrbit | FDC | HDC | A0, *cmd, HDC_DELAY);
  DMA->dma_mode = wrbit;
#endif /* AHDILIKE */
  return(OK);
}

/*===========================================================================*
 *				poll					     *
 *===========================================================================*/
PRIVATE int poll(delay)
int delay;
{
  /* wait until hdc signals ready - or return ERROR after t tries */
  for ( ; --delay >= 0; )
	if ((MFP->mf_gpip & IO_DINT) == 0)
		return(OK);
  return(ERR);
}

/*===========================================================================*
 *				hdcint					     *
 *===========================================================================*/
PRIVATE void hdcint()
{
  acsi_command = OK;
  interrupt(WINCHESTER);
}
/*===========================================================================*
 *				acsi_timeout				     *
 *===========================================================================*/
PRIVATE void acsi_timeout()
{
  if (acsi_command != OK) {
	TRACE1(printf("ACSI: caught alarm\n"));
  	interrupt(WINCHESTER);
  } else
	TRACE1(printf("ACSI: ignoring alarm\n"));
}

/*===========================================================================*
 *				request_sense				     *
 *===========================================================================*/
PRIVATE void request_sense(command, sector)
int command;
long sector;
{
  int 		i, j, s;
  char 		buf[4][4], *cs;
  unsigned char cmd[6];

  dmardat(FDC, HDC_DELAY);		/* finish HDC command */
  /* perform a request sense command to get the error status */
  dmaaddr((phys_bytes)buf);		/* DMA address setup */
  /* we need to do request sense 4 times since due to buffering in
     the dma controller will not transfer the data if it is less than
     16 bytes. request sense returns only four bytes...
     That's also the reason for the strange declaration of buf.
   */

  cmd[0] = TARGET(acsi_drive) | (HD_RQS&0x1F);
  cmd[1] = 0;
  cmd[2] = 0;
  cmd[3] = 0;
  cmd[4] = 0;	/* this gives 4 bytes ofnon extended sense data */
  cmd[5] = 0;

  for (i = 0; i < 4; i++)
  {
#if (FAST_DISK == 0)
	for (s = 0; s< COMMAND_DELAY; s++) /* nothing */;
#else
	if (opening == TRUE)
		for (s = 0; s< COMMAND_DELAY; s++) /* nothing */;
#endif /* FAST_DISK == 0 */
	do_cmd(cmd, 6, 1, 0, INT_ON);	/* command parameters */
#if ALWAYS_ALARM
	acsi_command = HD_RQS;
	clock_mess(SHORT_WAKEUP, acsi_timeout);
#endif /* ALWAYS_ALARM */
	receive(HARDWARE, &mess);	/* Wait for interrupt. */
#if ALWAYS_ALARM
	REMOVE_ALARM();
	if (acsi_command != OK)
		TRACE1(printf("ACSI: alarm caught during REQUEST SENSE on drive %d\n",
					acsi_drive));
#endif /* ALWAYS_ALARM */
	dmardat(FDC | HDC | A0, HDC_DELAY); /* read status */
        TRACE(
		printf("status bytes (hex): %x, %x, %x, %x\n",
			     buf[0][0], buf[0][1], buf[0][2], buf[0][3]);
        );
	for (j = 0; j < 400; j++) /* wait at least 130 us */;
  }
  TRACE1(printf("ACSI (RQS): ");
  switch (command) {
  case DEV_READ:  cs = "read"; break;
  case DEV_WRITE: cs = "write"; break;
  case HD_TUR:    cs = "test unit ready"; break;
  case HD_SHP:    cs = "ship"; break;
  case HD_RST:    cs = "restore"; break;
  default:        cs = "unknown";
  }
  printf("%s: drive=%d sector=%D status=0x%x\n",
					cs, acsi_drive, sector, buf[0][0]));
}

#include <sgtty.h>
#define XFERSIZE 13
/*===========================================================================*
 *				acsi_diocntl				     *
 *===========================================================================*/
PRIVATE int acsi_diocntl(dp, mp)
struct driver *dp;
register message *mp;
{
  register phys_bytes	uaddress, kaddress;
  register int r, drive, wrbit = 0;
  int i, pnr;
  struct device *dv;
  struct partition entry;
  struct   scsi_cmd kcmd;
  /* the acsi controller writes in chunks of 16 bytes. If the request is not
     a multiple of 16 and > SECTOR_SIZE the request is rejected. If the request
     is not a multiple of 16 and < SECTOR_SIZE buf is used and the data is 
     later on copied to the user address. 
     buf is also used to temporarily hold the command bytes */
  static   _VOLATILE unsigned char buf[SECTOR_SIZE];

  pnr = mp->PROC_NR;
  switch(mp->TTY_REQUEST) {
#if (HD_CLOCK == 1)
     case DC_RBMS100:
     case DC_RBMS200:
     case DC_RSUPRA:
     case DC_RICD:
     case DC_WBMS100:
     case DC_WBMS200:
	  uaddress = numap(pnr, (vir_bytes) mp->ADDRESS, (vir_bytes) XFERSIZE);
	  break;
#endif /* HD_CLOCK */
     case SCSI_READ:
     case SCSI_WRITE:
     	  if ((mp->DEVICE & 15) != 15)
     	  	return(EINVAL);
	  uaddress = numap(pnr, (vir_bytes) mp->ADDRESS, (vir_bytes) sizeof(struct scsi_cmd));
	  break;
     case DIOCSETP:
     case DIOCGETP:
     	  if ((dv = acsi_prepare(mp->DEVICE)) == NIL_DEV) return (ENXIO);
     	  uaddress = numap(pnr, (vir_bytes) mp->ADDRESS, (vir_bytes) sizeof(entry));
     	  break;
     default:
	  return(EINVAL);
  }
  if (uaddress == 0)
	return(EINVAL);
  switch(mp->TTY_REQUEST) {
#if (HD_CLOCK == 1)
     case DC_RBMS100:
     case DC_RBMS200:
     case DC_RSUPRA:
     case DC_RICD:
	  r = do_xbms(uaddress,XFERSIZE,DEV_READ,(int)mp->TTY_REQUEST);
	  break;
     case DC_WBMS100:
     case DC_WBMS200:
	  r = do_xbms(uaddress,XFERSIZE,DEV_WRITE,(int)mp->TTY_REQUEST);
	  break;
#endif /* HD_CLOCK */
     case DIOCSETP:
     	  /* Copy just this one parition table entry */
     	  phys_copy(uaddress, (phys_bytes)&entry, (phys_bytes) sizeof(entry));
     	  dv->dv_base = entry.base;
     	  dv->dv_size = entry.size;
     	  break;
     case DIOCGETP:
	  /* return a partition table entry and the geometry of the drive */
	  entry.base = dv->dv_base;
	  entry.size = dv->dv_size;
	  acsi_geometry(&entry);
	  phys_copy((phys_bytes)&entry, uaddress, (phys_bytes) sizeof(entry));
	  break;
     case SCSI_WRITE:
	  wrbit = WRBIT;
     case SCSI_READ:
	  /* copy the command block */
	  kaddress = umap(proc_ptr, S, (vir_bytes) &kcmd, (vir_bytes) sizeof(kcmd));
	  phys_copy(uaddress, kaddress, (vir_bytes) sizeof(kcmd));

	  /* copy the command bytes */
	  uaddress = numap(pnr, (vir_bytes) kcmd.cmd, (vir_bytes) kcmd.cmd_cnt);
	  if (uaddress == 0)
		return(EINVAL);
	  kaddress = umap(proc_ptr, S, (vir_bytes) buf, (vir_bytes) kcmd.cmd_cnt);
	  phys_copy(uaddress, kaddress, (vir_bytes) kcmd.cmd_cnt);

	  uaddress = numap(pnr, (vir_bytes) kcmd.buf, (vir_bytes) kcmd.size);
	  if (uaddress == 0)
		return(EINVAL);

	  if (kcmd.size > SECTOR_SIZE)
		return(EINVAL);

	  if (wrbit == WRBIT)
	  {
	      phys_copy(uaddress, kaddress, (vir_bytes) kcmd.size);
	  }

	  drive = mp->DEVICE >> MINOR_SHIFT;
	  buf[0] = TARGET(drive) | buf[0];
	  buf[1] = LUN(drive) | buf[1];
	  dmagrab(WINCHESTER, hdcint);
	  dmaaddr(kaddress);		/* DMA address setup */
printf("about to ioctl cmd %x %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
	  r = do_cmd(buf, kcmd.cmd_cnt,
				(int) ((kcmd.size + 511)/512), wrbit, INT_ON);
	  if (r == OK) {
#if ALWAYS_ALARM
		acsi_command = wrbit ? DEV_WRITE : DEV_READ;
		clock_mess(SHORT_WAKEUP, acsi_timeout);
#endif /* ALWAYS_ALARM */
		receive(HARDWARE, &mess);	/* Wait for interrupt. */
printf("after cmd %x %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
#ifdef OLDSUPRA
		IDISABLE();
#endif
#if ALWAYS_ALARM
		REMOVE_ALARM();
		if (acsi_command != OK) {
			printf("ACSI: ALARM caught drive %d command 0x%x\n",
					acsi_drive, acsi_command);
			/* should we leave here? */
		}
#endif /* ALWAYS_ALARM */
		r = dmardat(wrbit | FDC | HDC | A0, HDC_DELAY);

		if ((kcmd.size & 0xf) && (wrbit != WRBIT)) /* flush dma queue */
			request_sense(wrbit ? DEV_WRITE : DEV_READ, 0L);
	  }
	  dmardat(FDC, HDC_DELAY);		/* finish HDC command */
#if (FAST_DISK == 0)
	  for (i = 0; i< COMMAND_DELAY; i++) ;	/* nothing */;
#else
	  if (opening == TRUE)
		for (i = 0; i< COMMAND_DELAY; i++) ; /* nothing */;
#endif /* FAST_DISK == 0 */
	  dmafree(WINCHESTER);
	  if (wrbit != WRBIT)
	  {
	      phys_copy(kaddress, uaddress, (phys_bytes) kcmd.size);
	  }
	  break;
  }

  return(r);
}

#endif /* NR_ACSI_DRIVES > 0 */
#endif /* MACHINE == ATARI */
