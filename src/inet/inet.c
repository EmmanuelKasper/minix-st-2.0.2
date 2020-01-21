/*	this file contains the interface of the network software with rest of
	minix. Furthermore it contains the main loop of the network task.

Copyright 1995 Philip Homburg

The valid messages and their parameters are:

from FS:
 __________________________________________________________________
|		|           |         |       |          |         |
| m_type	|   DEVICE  | PROC_NR |	COUNT |	POSITION | ADDRESS |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_OPEN 	| minor dev | proc nr | mode  |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CLOSE 	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_IOCTL	| minor dev | proc nr |       |	NWIO..	 | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_READ	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_WRITE	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CANCEL	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|

from DL_ETH:
 _______________________________________________________________________
|		|           |         |          |            |         |
| m_type	|  DL_PORT  | DL_PROC |	DL_COUNT |  DL_STAT   | DL_TIME |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_INT 	| minor dev | proc nr | rd_count |  0  | stat |  time   |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_REPLY	| minor dev | proc nr | rd_count | err | stat |  time   |         |
|_______________|___________|_________|__________|____________|_________|
*/

#include "inet.h"

#define _MINIX_SOURCE 1
#define _MINIX

#include <unistd.h>
#include <sys/stat.h>

#include "mq.h"
#include "proto.h"
#include "generic/type.h"

#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/eth.h"
#include "generic/event.h"
#if !CRAMPED
#include "generic/arp.h"
#include "generic/ip.h"
#include "generic/psip.h"
#include "generic/sr.h"
#include "generic/tcp.h"
#include "generic/udp.h"
#endif

THIS_FILE

#ifdef BUF_CONSISTENCY_CHECK
extern int inet_buf_debug;
#endif

_PROTOTYPE( void main, (void) );

FORWARD _PROTOTYPE( void nw_init, (void) );

PUBLIC void main()
{
	mq_t *mq;
	int r;
	int source;
	struct stat stb;

	DBLOCK(1, printf("%s\n", version));

#ifdef BUF_CONSISTENCY_CHECK
	inet_buf_debug= 100;
	if (inet_buf_debug)
	{
		ip_warning(( "buffer consistency check enabled" ));
	}
#endif

	nw_init();
	while (TRUE)
	{
#ifdef BUF_CONSISTENCY_CHECK
		if (inet_buf_debug)
		{
			static int buf_debug_count= 0;

			if (buf_debug_count++ > inet_buf_debug)
			{
				buf_debug_count= 0;
				if (!bf_consistency_check())
					break;
			}
		}
#endif
		if (ev_head)
		{
			ev_process();
			continue;
		}
		if (clck_call_expire)
		{
			clck_expire_timers();
			continue;
		}
		mq= mq_get();
		if (!mq)
			ip_panic(("out of messages"));

		r= receive (ANY, &mq->mq_mess);
		if (r<0)
		{
			ip_panic(("unable to receive: %d", r));
		}
		reset_time();
		source= mq->mq_mess.m_source;
		if (source == FS_PROC_NR)
		{
			sr_rec(mq);
		}
#if ENABLE_ETH
		else if (source == DL_ETH)
		{
compare(mq->mq_mess.m_type, ==, DL_TASK_REPLY);
			eth_rec(&mq->mq_mess);
			mq_free(mq);
		}
#endif /* ENABLE_ETH */
		else if (source == SYN_ALRM_TASK)
		{
			clck_tick (&mq->mq_mess);
			mq_free(mq);
		}
		else
		{
			ip_panic(("message from unknown source: %d",
				mq->mq_mess.m_source));
		}
	}
	ip_panic(("task is not allowed to terminate"));
}

PRIVATE void nw_init()
{
	mq_init();
	bf_init();
	clck_init();
	sr_init();
#if ENABLE_ETH
	eth_init();
#endif /* ENABLE_ETH */
#if ENABLE_ARP
	arp_init();
#endif
#if ENABLE_PSIP
	psip_init();
#endif
#if ENABLE_IP
	ip_init();
#endif
#if ENABLE_TCP
	tcp_init();
#endif
#if ENABLE_UDP
	udp_init();
#endif
}

#if !CRAMPED
PUBLIC void panic0(file, line)
char *file;
int line;
{
	printf("panic at %s, %d: ", file, line);
}

PUBLIC void panic()
{
	printf("\ninet stacktrace: ");
	stacktrace();
	sys_abort(RBT_PANIC);
}

#else /* CRAMPED */

PUBLIC void panic(file, line)
char *file;
int line;
{
	printf("panic at %s, %d\n", file, line);
	sys_abort(RBT_PANIC);
}
#endif

#if !NDEBUG
PUBLIC void bad_assertion(file, line, what)
char *file;
int line;
char *what;
{
	panic0(file, line);
	printf("assertion \"%s\" failed", what);
	panic();
}


PUBLIC void bad_compare(file, line, lhs, what, rhs)
char *file;
int line;
unsigned long lhs;
char *what;
unsigned long rhs;
{
	panic0(file, line);
	printf("compare (0x%lu) %s (0x%lu) failed", lhs, what, rhs);
	panic();
}
#endif /* !NDEBUG */

/*
 * $PchId: inet.c,v 1.12 1996/12/17 07:58:19 philip Exp $
 */
