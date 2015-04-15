#include "stuff.h"

char Ehungup[30] = "i/o on hungup channel";

/*
 *  lock-free queue. only simple enqueue/dequeue of blocks,
 *  but faster with multiple readers/writers
 */
typedef struct CasQueue	CasQueue;

struct CasQueue
{
	Lock;

	Block*	bfirst;		/* buffer */
	Block*	blast;

	int	len;		/* bytes allocated to queue */
	int	limit;		/* max bytes in queue */
	int	closed;
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
    q->relsize = 0;
	dummy->next = nil;

	q->closed = 0;

	return q;
}

/*
 * Enqueues one block to the end of the queue
 * Based on non-blocking algo 
 */ 
void
casqput(CasQueue *q, Block *b) {
    Block *tail, *next;
    
	if(q->closed)
		error(Ehungup);

    b->next = 0;
    for(;;) {
        tail = q->blast;
        next = tail->next;
        if (tail == q->blast) {
            if (next == nil) {
                b->relsize = tail->relsize + BALLOC(b);
                if (cas(&tail->next, next, b)) {
                    break;
                }
            } else {
                cas(&q->blast, tail, next);
            }
        }
    }
    cas(&q->blast, tail, node);
}



Block*
casqget(CasQueue *q)
{
	Block *head, *tail, *next, *b;

	if(q->closed)
		error(Ehungup);

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
	q->len -= BALLOC(next);
	// QDEBUG checkb(b, "casqget");
	return b;
}

/* Returns size of queue. Guaranteed to be accurate within
 * one block (i.e. if tail is falling behind)
 */
ulong
casqsize(CasQueue *q)
{
    return q->blast->relsize - q->bfirst->relsize;
}

/*
 * writers and readers not guaranteed to be done when this returns
 */
void
casqclose(CasQueue *q)
{
	q->closed = 1;
}

/*
 * call after casqclose, after remaining readers and writers are synced
 */
void
casqfree(CasQueue *q)
{
	Block *bfirst;

	if(q == nil)
		return;

	freeblist(q->bfirst);
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

