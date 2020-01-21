/* This file handles signals, which are asynchronous events and are generally
 * a messy and unpleasant business.  Signals can be generated by the KILL
 * system call, or from the keyboard (SIGINT) or from the clock (SIGALRM).
 * In all cases control eventually passes to check_sig() to see which processes
 * can be signaled.  The actual signaling is done by sig_proc().
 *
 * The entry points into this file are:
#if (OLDSIGNAL_COMPAT == 1)
 *   do_signal:	     perform the SIGNAL system call
#endif
 *   do_sigaction:   perform the SIGACTION system call
 *   do_sigpending:  perform the SIGPENDING system call
 *   do_sigprocmask: perform the SIGPROCMASK system call
 *   do_sigreturn:   perform the SIGRETURN system call
 *   do_sigsuspend:  perform the SIGSUSPEND system call
 *   do_kill:	perform the KILL system call
 *   do_ksig:	accept a signal originating in the kernel (e.g., SIGINT)
 *   do_alarm:	perform the ALARM system call by calling set_alarm()
 *   set_alarm:	tell the clock task to start or stop a timer
 *   do_pause:	perform the PAUSE system call
 *   do_reboot: kill all processes, then reboot system
 *   sig_proc:	interrupt or terminate a signaled process
 *   check_sig: check which processes to signal with sig_proc()
 */

#include "mm.h"
#include <sys/stat.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <signal.h>
#include <sys/sigcontext.h>
#include <string.h>
#include "mproc.h"
#include "param.h"

#define CORE_MODE	0777	/* mode to use on core image files */
#define DUMPED          0200	/* bit set in status when core dumped */
#define DUMP_SIZE	((INT_MAX / BLOCK_SIZE) * BLOCK_SIZE)
				/* buffer size for core dumps */

FORWARD _PROTOTYPE( void check_pending, (void)				);
FORWARD _PROTOTYPE( void dump_core, (struct mproc *rmp)			);
FORWARD _PROTOTYPE( void unpause, (int pro)				);

#if (OLDSIGNAL_COMPAT == 1)
/*===========================================================================*
 *				do_signal				     *
 *===========================================================================*/
PUBLIC int do_signal()
{
/* Perform the signal(sig, func) call by setting bits to indicate that a 
 * signal is to be caught or ignored.
 *
 * This function exists only for compatibility with old binaries.  In the
 * new implementation, signal(2) is a library function built on top of the
 * POSIX sigaction(2) system call.  The present system level function ties
 * into the mechanism used to implement sigaction().
 *
 * Since the process invoking this system call is oblivious to signal
 * masks, it is assumed that the signal is not in the process signal
 * mask, and is therefore also not pending. 
 */

  sigset_t old_ignore;

  if (sig < 1 || sig > _NSIG) return(EINVAL);
  if (sig == SIGKILL) return(OK);	/* SIGKILL may not ignored/caught */
  old_ignore = mp->mp_ignore;

  sigemptyset(&mp->mp_sigact[sig].sa_mask);
  sigdelset(&mp->mp_sigmask, sig);
  mp->mp_sigact[sig].sa_flags = SA_COMPAT | SA_NODEFER;
  mp->mp_sigact[sig].sa_handler = func;

  if (func == SIG_IGN) {
	sigaddset(&mp->mp_ignore, sig);
	sigdelset(&mp->mp_catch, sig);
  } else {
	sigdelset(&mp->mp_ignore, sig);
	if (func == SIG_DFL)
		sigdelset(&mp->mp_catch, sig);
  	else {
		sigaddset(&mp->mp_catch, sig);
		if (sig != SIGILL && sig != SIGTRAP)
			mp->mp_sigact[sig].sa_flags |= SA_RESETHAND;
		mp->mp_func = func;
  	}
  }
  if (sigismember(&old_ignore, sig)) return(1);
  return(OK);
}
#endif /* OLDSIGNAL_COMPAT */

/*===========================================================================*
 *			       do_sigaction				     *
 *===========================================================================*/
PUBLIC int do_sigaction()
{
  int r;
  struct sigaction svec;
  struct sigaction *svp;

  if (sig_nr == SIGKILL) return(OK);
  if (sig_nr < 1 || sig_nr > _NSIG) return (EINVAL);
  svp = &mp->mp_sigact[sig_nr];
  if ((struct sigaction *) sig_osa != (struct sigaction *) NULL) {
	r = sys_copy(MM_PROC_NR,D, (phys_bytes) svp,
		who, D, (phys_bytes) sig_osa, (phys_bytes) sizeof(svec));
	if (r != OK) return(r);
  }

  if ((struct sigaction *) sig_nsa == (struct sigaction *) NULL) return(OK);

  /* Read in the sigaction structure. */
  r = sys_copy(who, D, (phys_bytes) sig_nsa,
		MM_PROC_NR, D, (phys_bytes) &svec, (phys_bytes) sizeof(svec));
  if (r != OK) return(r);

  if (svec.sa_handler == SIG_IGN) {
	sigaddset(&mp->mp_ignore, sig_nr);
	sigdelset(&mp->mp_sigpending, sig_nr);
	sigdelset(&mp->mp_catch, sig_nr);
  } else {
	sigdelset(&mp->mp_ignore, sig_nr);
	if (svec.sa_handler == SIG_DFL)
		sigdelset(&mp->mp_catch, sig_nr);
	else
		sigaddset(&mp->mp_catch, sig_nr);
  }
  mp->mp_sigact[sig_nr].sa_handler = svec.sa_handler;
  sigdelset(&svec.sa_mask, SIGKILL);
  mp->mp_sigact[sig_nr].sa_mask = svec.sa_mask;
#if (OLDSIGNAL_COMPAT == 1)
  mp->mp_sigact[sig_nr].sa_flags = svec.sa_flags & ~SA_COMPAT;
#else
  mp->mp_sigact[sig_nr].sa_flags = svec.sa_flags;
#endif
  mp->mp_sigreturn = (vir_bytes) sig_ret;
  return(OK);
}

/*===========================================================================*
 *                            do_sigpending                                  *
 *===========================================================================*/
PUBLIC int do_sigpending()
{
  ret_mask = (long) mp->mp_sigpending;
  return OK;
}

/*===========================================================================*
 *                            do_sigprocmask                                 *
 *===========================================================================*/
PUBLIC int do_sigprocmask()
{
/* Note that the library interface passes the actual mask in sigmask_set,
 * not a pointer to the mask, in order to save a sys_copy.  Similarly,
 * the old mask is placed in the return message which the library
 * interface copies (if requested) to the user specified address.
 *
 * The library interface must set SIG_INQUIRE if the 'act' argument
 * is NULL.
 */

  int i;

  ret_mask = (long) mp->mp_sigmask;

  switch (sig_how) {
      case SIG_BLOCK:
	sigdelset((sigset_t *)&sig_set, SIGKILL);
	for (i = 1; i < _NSIG; i++) {
		if (sigismember((sigset_t *)&sig_set, i))
			sigaddset(&mp->mp_sigmask, i);
	}
	break;

      case SIG_UNBLOCK:
	for (i = 1; i < _NSIG; i++) {
		if (sigismember((sigset_t *)&sig_set, i))
			sigdelset(&mp->mp_sigmask, i);
	}
	check_pending();
	break;

      case SIG_SETMASK:
	sigdelset((sigset_t *)&sig_set, SIGKILL);
	mp->mp_sigmask = (sigset_t)sig_set;
	check_pending();
	break;

      case SIG_INQUIRE:
	break;

      default:
	return(EINVAL);
	break;
  }
  return OK;
}

/*===========================================================================*
 *                            do_sigsuspend                                  *
 *===========================================================================*/
PUBLIC int do_sigsuspend()
{
  mp->mp_sigmask2 = mp->mp_sigmask;	/* save the old mask */
  mp->mp_sigmask = (sigset_t) sig_set;
  sigdelset(&mp->mp_sigmask, SIGKILL);
  mp->mp_flags |= SIGSUSPENDED;
  dont_reply = TRUE;
  check_pending();
  return OK;
}


/*===========================================================================*
 *                               do_sigreturn				     *
 *===========================================================================*/
PUBLIC int do_sigreturn()
{
/* A user signal handler is done.  Restore context and check for
 * pending unblocked signals.
 */

  int r;

  mp->mp_sigmask = (sigset_t) sig_set;
  sigdelset(&mp->mp_sigmask, SIGKILL);

  r = sys_sigreturn(who, (vir_bytes)sig_context, sig_flags);
  check_pending();
  return(r);
}

/*===========================================================================*
 *				do_kill					     *
 *===========================================================================*/
PUBLIC int do_kill()
{
/* Perform the kill(pid, signo) system call. */

  return check_sig(pid, sig_nr);
}


/*===========================================================================*
 *				do_ksig					     *
 *===========================================================================*/
PUBLIC int do_ksig()
{
/* Certain signals, such as segmentation violations and DEL, originate in the
 * kernel.  When the kernel detects such signals, it sets bits in a bit map.
 * As soon as MM is awaiting new work, the kernel sends MM a message containing
 * the process slot and bit map.  That message comes here.  The File System
 * also uses this mechanism to signal writing on broken pipes (SIGPIPE).
 */

  register struct mproc *rmp;
  int i, proc_nr;
  pid_t proc_id, id;
  sigset_t sig_map;

  /* Only kernel may make this call. */
  if (who != HARDWARE) return(EPERM);
  dont_reply = TRUE;		/* don't reply to the kernel */
  proc_nr = mm_in.SIG_PROC;
  rmp = &mproc[proc_nr];
  if ( (rmp->mp_flags & IN_USE) == 0 || (rmp->mp_flags & HANGING) ) return(OK);
  proc_id = rmp->mp_pid;
  sig_map = (sigset_t) mm_in.SIG_MAP;
  mp = &mproc[0];		/* pretend kernel signals are from MM */
  mp->mp_procgrp = rmp->mp_procgrp;	/* get process group right */
#if (MACHINE == ATARI)
  /* Stack faults are passed from kernel to MM as pseudo-signal SIGSTKFLT. */
  if (sigismember(&sig_map, SIGSTKFLT)) stack_fault(proc_nr);
#endif /* MACHINE == ATARI */
  /* Check each bit in turn to see if a signal is to be sent.  Unlike
   * kill(), the kernel may collect several unrelated signals for a
   * process and pass them to MM in one blow.  Thus loop on the bit
   * map. For SIGINT and SIGQUIT, use proc_id 0 to indicate a broadcast
   * to the recipient's process group.  For SIGKILL, use proc_id -1 to
   * indicate a systemwide broadcast.
   */
  for (i = 1; i <= _NSIG; i++) {
	if (!sigismember(&sig_map, i)) continue;
	switch (i) {
#if (MACHINE == ATARI)
	    case SIGSTKFLT:
		continue;	/* XXX - not sure if this special case
	    			 * is still necessary; however it would be
	    			 * nice to eliminate the special test for
	    			 * SIGSTKFLT above */
#endif /* MACHINE == ATARI */
	    case SIGINT:
	    case SIGQUIT:
		id = 0; break;	/* broadcast to process group */
	    case SIGKILL:
		id = -1; break;	/* broadcast to all except INIT */
	    case SIGALRM:
		/* Disregard SIGALRM when the target process has not
		 * requested an alarm.  This only applies for a KERNEL
		 * generated signal.
		 */
		if ((rmp->mp_flags & ALARM_ON) == 0) continue;
		rmp->mp_flags &= ~ALARM_ON;
		/* fall through */
	    default:
		id = proc_id;
		break;
	}
	check_sig(id, i);
	sys_endsig(proc_nr);	/* tell kernel it's done */
  }
  return(OK);
}


/*===========================================================================*
 *				do_alarm				     *
 *===========================================================================*/
PUBLIC int do_alarm()
{
/* Perform the alarm(seconds) system call. */

  return(set_alarm(who, seconds));
}


/*===========================================================================*
 *				set_alarm				     *
 *===========================================================================*/
PUBLIC int set_alarm(proc_nr, sec)
int proc_nr;			/* process that wants the alarm */
int sec;			/* how many seconds delay before the signal */
{
/* This routine is used by do_alarm() to set the alarm timer.  It is also used
 * to turn the timer off when a process exits with the timer still on.
 */

  message m_sig;
  int remaining;

  if (sec != 0)
	mproc[proc_nr].mp_flags |= ALARM_ON;
  else
	mproc[proc_nr].mp_flags &= ~ALARM_ON;

  /* Tell the clock task to provide a signal message when the time comes.
   *
   * Large delays cause a lot of problems.  First, the alarm system call
   * takes an unsigned seconds count and the library has cast it to an int.
   * That probably works, but on return the library will convert "negative"
   * unsigneds to errors.  Presumably no one checks for these errors, so
   * force this call through.  Second, If unsigned and long have the same
   * size, converting from seconds to ticks can easily overflow.  Finally,
   * the kernel has similar overflow bugs adding ticks.
   *
   * Fixing this requires a lot of ugly casts to fit the wrong interface
   * types and to avoid overflow traps.  DELTA_TICKS has the right type
   * (clock_t) although it is declared as long.  How can variables like
   * this be declared properly without combinatorial explosion of message
   * types?
   */
  m_sig.m_type = SET_ALARM;
  m_sig.CLOCK_PROC_NR = proc_nr;
  m_sig.DELTA_TICKS = (clock_t) (HZ * (unsigned long) (unsigned) sec);
  if ( (unsigned long) m_sig.DELTA_TICKS / HZ != (unsigned) sec)
	m_sig.DELTA_TICKS = LONG_MAX;	/* eternity (really CLOCK_T_MAX) */
  if (sendrec(CLOCK, &m_sig) != OK) panic("alarm er", NO_NUM);
  remaining = (int) m_sig.SECONDS_LEFT;
  if (remaining != m_sig.SECONDS_LEFT || remaining < 0)
	remaining = INT_MAX;	/* true value is not representable */
  return(remaining);
}


/*===========================================================================*
 *				do_pause				     *
 *===========================================================================*/
PUBLIC int do_pause()
{
/* Perform the pause() system call. */

  mp->mp_flags |= PAUSED;
  dont_reply = TRUE;
  return(OK);
}


/*=====================================================================*
 *			    do_reboot				       *
 *=====================================================================*/
PUBLIC int do_reboot()
{
  register struct mproc *rmp = mp;
  char monitor_code[64];

  if (rmp->mp_effuid != SUPER_USER)   return EPERM;

  switch (reboot_flag) {
  case RBT_HALT:
  case RBT_REBOOT:
  case RBT_PANIC:
  case RBT_RESET:
	break;
  case RBT_MONITOR:
	if (reboot_size > sizeof(monitor_code)) return EINVAL;
	memset(monitor_code, 0, sizeof(monitor_code));
	if (sys_copy(who, D, (phys_bytes) reboot_code,
		MM_PROC_NR, D, (phys_bytes) monitor_code,
		(phys_bytes) reboot_size) != OK) return EFAULT;
	if (monitor_code[sizeof(monitor_code)-1] != 0) return EINVAL;
	break;
  default:
	return EINVAL;
  }

  /* Kill all processes except init. */
  check_sig(-1, SIGKILL);

  tell_fs(EXIT, INIT_PROC_NR, 0, 0);	/* cleanup init */

  tell_fs(SYNC,0,0,0);

  sys_abort(reboot_flag, monitor_code);
  /* NOTREACHED */
}


/*===========================================================================*
 *				sig_proc				     *
 *===========================================================================*/
PUBLIC void sig_proc(rmp, signo)
register struct mproc *rmp;	/* pointer to the process to be signaled */
int signo;			/* signal to send to process (1 to _NSIG) */
{
/* Send a signal to a process.  Check to see if the signal is to be caught,
 * ignored, or blocked.  If the signal is to be caught, coordinate with
 * KERNEL to push a sigcontext structure and a sigframe structure onto
 * the catcher's stack.  Also, KERNEL will reset the program counter and
 * stack pointer, so that when the process next runs, it will be executing
 * the signal handler.  When the signal handler returns,  sigreturn(2)
 * will be called.  Then KERNEL will restore the signal context from the
 * sigcontext structure.
 *
 * If there is insufficient stack space, kill the process.
 */

  vir_bytes new_sp;
  int slot;
  int sigflags;
  struct sigmsg sm;

  slot = (int) (rmp - mproc);
  if (!(rmp->mp_flags & IN_USE)) {
	printf("MM: signal %d sent to dead process %d\n", signo, slot);
	panic("", NO_NUM);
  }
  if (rmp->mp_flags & HANGING) {
	printf("MM: signal %d sent to HANGING process %d\n", signo, slot);
	panic("", NO_NUM);
  }
  if (rmp->mp_flags & TRACED && signo != SIGKILL) {
	/* A traced process has special handling. */
	unpause(slot);
	stop_proc(rmp, signo);	/* a signal causes it to stop */
	return;
  }
  /* Some signals are ignored by default. */
  if (sigismember(&rmp->mp_ignore, signo)) return; 

  if (sigismember(&rmp->mp_sigmask, signo)) {
	/* Signal should be blocked. */
	sigaddset(&rmp->mp_sigpending, signo);
	return;
  }
  sigflags = rmp->mp_sigact[signo].sa_flags;
  if (sigismember(&rmp->mp_catch, signo)) {
	if (rmp->mp_flags & SIGSUSPENDED)
		sm.sm_mask = rmp->mp_sigmask2;
	else
		sm.sm_mask = rmp->mp_sigmask;
	sm.sm_signo = signo;
	sm.sm_sighandler = (vir_bytes) rmp->mp_sigact[signo].sa_handler;
	sm.sm_sigreturn = rmp->mp_sigreturn;
	sys_getsp(slot, &new_sp);
	sm.sm_stkptr = new_sp;
#if (OLDSIGNAL_COMPAT == 1)
	if (sigflags & SA_COMPAT) {
		/* Make room for an old style stack frame. */
		new_sp -= SIG_PUSH_BYTES;
	} else
#endif
	/* Make room for the sigcontext and sigframe struct. */
#ifdef ORIGINAL_20_BUT_FALSE
	/* obviously not updated from 1.6.x */
	new_sp -= sizeof(struct sigcontext)
				 + 3 * sizeof(char *) + 2 * sizeof(int);
#else
	new_sp -= sizeof(struct sigcontext) + sizeof(struct sigframe);
#endif

	if (adjust(rmp, rmp->mp_seg[D].mem_len, new_sp) != OK)
		goto doterminate;

	rmp->mp_sigmask |= rmp->mp_sigact[signo].sa_mask;
	if (sigflags & SA_NODEFER)
		sigdelset(&rmp->mp_sigmask, signo);
	else
		sigaddset(&rmp->mp_sigmask, signo);

	if (sigflags & SA_RESETHAND) {
		sigdelset(&rmp->mp_catch, signo);
		rmp->mp_sigact[signo].sa_handler = SIG_DFL;
	}

#if (OLDSIGNAL_COMPAT == 1)
  	if (sigflags & SA_COMPAT)
		sys_oldsig(slot, signo, rmp->mp_func);
	else
#endif
	sys_sendsig(slot, &sm);
	sigdelset(&rmp->mp_sigpending, signo);
	/* If process is hanging on PAUSE, WAIT, SIGSUSPEND, tty, pipe, etc.,
	 * release it.
	 */
	unpause(slot);
	return;
  }
doterminate:
  /* Signal should not or cannot be caught.  Terminate the process. */
  rmp->mp_sigstatus = (char) signo;
  if (sigismember(&core_sset, signo)) {
	/* Switch to the user's FS environment and dump core. */
	tell_fs(CHDIR, slot, FALSE, 0);
	dump_core(rmp);
  }
  mm_exit(rmp, 0);		/* terminate process */
}


/*===========================================================================*
 *				check_sig				     *
 *===========================================================================*/
PUBLIC int check_sig(proc_id, signo)
pid_t proc_id;			/* pid of proc to sig, or 0 or -1, or -pgrp */
int signo;			/* signal to send to process (0 to _NSIG) */
{
/* Check to see if it is possible to send a signal.  The signal may have to be
 * sent to a group of processes.  This routine is invoked by the KILL system
 * call, and also when the kernel catches a DEL or other signal.
 */

  register struct mproc *rmp;
  int count;			/* count # of signals sent */
  int error_code;

  if (signo < 0 || signo > _NSIG) return(EINVAL);

  /* Return EINVAL for attempts to send SIGKILL to INIT alone. */
  if (proc_id == INIT_PID && signo == SIGKILL) return(EINVAL);

  /* Search the proc table for processes to signal.  (See forkexit.c about
   * pid magic.)
   */
  count = 0;
  error_code = ESRCH;
  for (rmp = &mproc[INIT_PROC_NR]; rmp < &mproc[NR_PROCS]; rmp++) {
	if ( (rmp->mp_flags & IN_USE) == 0) continue;
	if (rmp->mp_flags & HANGING && signo != 0) continue;

	/* Check for selection. */
	if (proc_id > 0 && proc_id != rmp->mp_pid) continue;
	if (proc_id == 0 && mp->mp_procgrp != rmp->mp_procgrp) continue;
	if (proc_id == -1 && rmp->mp_pid == INIT_PID) continue;
	if (proc_id < -1 && rmp->mp_procgrp != -proc_id) continue;

	/* Check for permission. */
	if (mp->mp_effuid != SUPER_USER
	    && mp->mp_realuid != rmp->mp_realuid
	    && mp->mp_effuid != rmp->mp_realuid
	    && mp->mp_realuid != rmp->mp_effuid
	    && mp->mp_effuid != rmp->mp_effuid) {
		error_code = EPERM;
		continue;
	}

	count++;
	if (signo == 0) continue;

	/* 'sig_proc' will handle the disposition of the signal.  The
	 * signal may be caught, blocked, ignored, or cause process
	 * termination, possibly with core dump.
	 */
	sig_proc(rmp, signo);

	if (proc_id > 0) break;	/* only one process being signaled */
  }

  /* If the calling process has killed itself, don't reply. */
  if ((mp->mp_flags & IN_USE) == 0 || (mp->mp_flags & HANGING))
	dont_reply = TRUE;
  return(count > 0 ? OK : error_code);
}


/*===========================================================================*
 *                               check_pending				     *
 *===========================================================================*/
PRIVATE void check_pending()
{
  /* Check to see if any pending signals have been unblocked.  The
   * first such signal found is delivered.
   *
   * If multiple pending unmasked signals are found, they will be
   * delivered sequentially.
   *
   * There are several places in this file where the signal mask is
   * changed.  At each such place, check_pending() should be called to
   * check for newly unblocked signals.
   */

  int i;

  for (i = 1; i < _NSIG; i++) {
	if (sigismember(&mp->mp_sigpending, i) &&
		!sigismember(&mp->mp_sigmask, i)) {
		sigdelset(&mp->mp_sigpending, i);
		sig_proc(mp, i);
		break;
	}
  }
}


/*===========================================================================*
 *				unpause					     *
 *===========================================================================*/
PRIVATE void unpause(pro)
int pro;			/* which process number */
{
/* A signal is to be sent to a process.  If that process is hanging on a
 * system call, the system call must be terminated with EINTR.  Possible
 * calls are PAUSE, WAIT, READ and WRITE, the latter two for pipes and ttys.
 * First check if the process is hanging on an MM call.  If not, tell FS,
 * so it can check for READs and WRITEs from pipes, ttys and the like.
 */

  register struct mproc *rmp;

  rmp = &mproc[pro];

  /* Check to see if process is hanging on a PAUSE call. */
  if ( (rmp->mp_flags & PAUSED) && (rmp->mp_flags & HANGING) == 0) {
	rmp->mp_flags &= ~PAUSED;
	reply(pro, EINTR, 0, NIL_PTR);
	return;
  }

  /* Check to see if process is hanging on a WAIT call. */
  if ( (rmp->mp_flags & WAITING) && (rmp->mp_flags & HANGING) == 0) {
	rmp->mp_flags &= ~WAITING;
	reply(pro, EINTR, 0, NIL_PTR);
	return;
  }

  /* Check to see if process is hanging on a SIGSUSPEND call. */
  if ((rmp->mp_flags & SIGSUSPENDED) && (rmp->mp_flags & HANGING) == 0) {
	rmp->mp_flags &= ~SIGSUSPENDED;
	reply(pro, EINTR, 0, NIL_PTR);
	return;
  }

  /* Process is not hanging on an MM call.  Ask FS to take a look. */
	tell_fs(UNPAUSE, pro, 0, 0);
}


/*===========================================================================*
 *				dump_core				     *
 *===========================================================================*/
PRIVATE void dump_core(rmp)
register struct mproc *rmp;	/* whose core is to be dumped */
{
/* Make a core dump on the file "core", if possible. */

  int fd, fake_fd, nr_written, seg, slot;
  char *buf;
  vir_bytes current_sp;
  phys_bytes left;		/* careful; 64K might overflow vir_bytes */
  unsigned nr_to_write;		/* unsigned for arg to write() but < INT_MAX */
  long trace_data, trace_off;

  slot = (int) (rmp - mproc);

  /* Can core file be written?  We are operating in the user's FS environment,
   * so no special permission checks are needed.
   */
  if (rmp->mp_realuid != rmp->mp_effuid) return;
  /* if ( (fd = creat(core_name, CORE_MODE)) < 0) return; backport 2.0.3 */
  if ( (fd = open(core_name, O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK,
						CORE_MODE)) < 0) return;
  rmp->mp_sigstatus |= DUMPED;

  /* Make sure the stack segment is up to date.
   * We don't want adjust() to fail unless current_sp is preposterous,
   * but it might fail due to safety checking.  Also, we don't really want 
   * the adjust() for sending a signal to fail due to safety checking.  
   * Maybe make SAFETY_BYTES a parameter.
   */
  sys_getsp(slot, &current_sp);
  adjust(rmp, rmp->mp_seg[D].mem_len, current_sp);

  /* Write the memory map of all segments to begin the core file. */
  if (write(fd, (char *) rmp->mp_seg, (unsigned) sizeof rmp->mp_seg)
      != (unsigned) sizeof rmp->mp_seg) {
	close(fd);
	return;
  }

  /* Write out the whole kernel process table entry to get the regs. */
  trace_off = 0;
  while (sys_trace(3, slot, trace_off, &trace_data) == OK) {
	if (write(fd, (char *) &trace_data, (unsigned) sizeof (long))
	    != (unsigned) sizeof (long)) {
		close(fd);
		return;
	}
	trace_off += sizeof (long);
  }

  /* Loop through segments and write the segments themselves out. */
  for (seg = 0; seg < NR_SEGS; seg++) {
	buf = (char *) ((vir_bytes) rmp->mp_seg[seg].mem_vir << CLICK_SHIFT);
	left = (phys_bytes) rmp->mp_seg[seg].mem_len << CLICK_SHIFT;
	fake_fd = (slot << 8) | (seg << 6) | fd;

	/* Loop through a segment, dumping it. */
	while (left != 0) {
		nr_to_write = (unsigned) MIN(left, DUMP_SIZE);
		if ( (nr_written = write(fake_fd, buf, nr_to_write)) < 0) {
			close(fd);
			return;
		}
		buf += nr_written;
		left -= nr_written;
	}
  }
  close(fd);
}
