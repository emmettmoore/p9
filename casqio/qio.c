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
//	if(waserror()) { // TODO: see if we need error handling stuff.
//		freeb(dummy);//       if we do there should be a poperror()
//		nexterror();
//	}
	q->bfirst = dummy;
	q->blast = q->bfirst;
	dummy->next = nil;

	return q;
}

Block*
casqget(CasQueue *q)
{
	Block *head, *tail, *next, *b;
	if(q->bfirst == q->blast) /* queue empty */
		return nil;
	for (;;){
		head = q->bfirst;
		tail = q->blast;
		next = head->next;
		if (head == q->bfirst) {
			if (head == tail) {
				if (next == nil)
					return nil;
				cas(&q->blast, tail, next); /* swing tail */
			} else {
				b = copyblock(next, BLEN(next)); // TODO see if we can move copyblock out of loop
				if (cas(&q->bfirst, head, next)) /* dequeue */
					break;
				freeb(b);
			}
		}
	}

	freeb(head);
	ilock(q); // TODO use cas here to avoid locking
	q->len -= BALLOC(next);
	iunlock(q);
	QDEBUG checkb(b, "casqget");
	return b;
}

/*
 * called when no more reads or writes will happen
 * TODO: might be able to improve interface with a "closed" state
 * then may not have to be sure reads/writes are done in order to close/free
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

// TODO what will we be using for our enqueue function?
// there is no function to enqueue a single block.
// maybe qpass?
// or maybe a new function, casqput which just adds a single block
// qproduce also seems within reach (allocating blocks before enqueueing to get to a certain length isn't an issue)



/*
 * Enqueues one block to the end of the queue
 * Based on non-blocking algo 
 */ 
void
casqput(CasQueue *q, Block *b) {
    Block *tail, *next, *node;
    
    b->next = 0;
    for(;;) {
        tail = q->blast;
        next = tail->next;
        if (tail == q->blast) {
            if (next == nil) {
                if cas(&tail->next, next, node) {
                    ilock(q); // TODO use cas here to avoid locking
                    q->len += BALLOC(next);
                    iunlock(q);
                    break;
                }
            } else {
                cas(&q->blast, tail, next);
            }
        }
    }
    cas(q->blast, tail, node);
}


