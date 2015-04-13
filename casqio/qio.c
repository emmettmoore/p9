/*
stuff we don't care about:
- Qflow
- wakeup stuff
- q->state and state stuff
want to return -1 when writing to full queue:
ilock(q);
if(q->len > q->limit)
	full = 1;
iunlock(q);

if(full)
	return -1;
*/

#define THREEINCH
#include "stuff.h"

char Ehungup[30] = "i/o on hungup channel";


/*
 *  non-locking IO queue. Fewer features than Queue, but faster in simple concurrent
 *  structures.
 */
typedef struct CasQueue	CasQueue;

struct CasQueue
{
	Lock;

	Block*	bfirst;		/* buffer */
	Block*	blast;

	int	len;		/* bytes allocated to queue */
	int	limit;		/* max bytes in queue */
};

/*
 *  Not necessarily needed to be ilocked... because only one process can open a 
 *  queue before it is used by multiple processes. Right?
 */
CasQueue*
casqopen()
{
	CasQueue *q;
	Block *dummy;

	q = malloc(sizeof(CasQueue));
	if(q == 0)
		return 0;

	/* add dummy node */
	dummy = allocb(0);
	if(waserror()) {
		freeb(dummy);
		nexterror();
	}
	q->bfirst = dummy;
	q->blast = q->bfirst;
	dummy->next = nil;

	return q;
}

Block*
casqget(CasQueue *q)
{
	Block *head;
	Block *tail;
	Block *next;
	Block *ret;
	if(q->bfirst == q->blast) /* queue empty */
		return nil;
	for (;;){
		head = q->bfirst;
		tail = q->blast;
		next = head->next;
		if (head == q->bfirst) {
			if (head == tail) {
				if (next == nil) {
					return nil;
				}
				cas(&q->blast, tail, next); /* swing tail */
			} 
			else {
				ret = copyblock(next, BLEN(next));
				if (cas(&q->bfirst, head, next)) { /* dequeue */
					break;
				}
				freeb(ret);
			}
		}
	}

	freeb(head);
	ilock(q);
	q->len -= BALLOC(next);
	q->dlen -= BLEN(next);
	iunlock(q);
	QDEBUG checkb(ret, "casqget");
	return ret;
}

/*
 * called when no more reads or writes will happen
 */
void
casqfree(CasQueue *q)
{
	Block *bfirst;

	if(q == nil)
		return;

	/* mark it */
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
	iunlock(q);

	/* free queued blocks */
	freeblist(bfirst);
	free(q);
}

/*
 *  change queue limit
 */
void
casqsetlimit(CasQueue *q, int limit)
{
	q->limit = limit;
}
