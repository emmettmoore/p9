#define THREEINCH
#include "stuff.h"

#define PTRSCREEN 0x1FFFFFFF
#define PTRLEN    32
#define PTRHDRLEN 3

#define PTR(p)            (p & PTRSCREEN)
#define PTRPLUS(p)             (p + (1 << (PTRLEN - PTRHDRLEN)))
#define PTRCOUNT(p1)           (p & ~(PTRSCREEN))
#define PTRCOMBINE(p1, p2)     (p1 & (PTRCOUNT(PTRPLUS(p2))))
extern char Ehungup[30];

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
casqopen(int limit)
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
    dummy->relsize = 0;
	dummy->next = nil;

	q->closed = 0;
    q->limit = limit;

	return q;
}

/*
 * Enqueues one block to the end of the queue
 * Based on non-blocking algo 
 */ 
int
casqput(CasQueue *q, Block *b) {
    Block *tail, *next;
    
	if(q->closed)
		error(Ehungup);

    b->next = 0;
    for(;;) {
        if (q->len > q->limit) {
            return -1;
        }
        tail = q->blast;
        next = PTR(tail)->next;
        if (tail == q->blast) {
            if (PTR(next) == nil) {
                b->relsize = tail->relsize + BALLOC(b);
                if (cas(&PTR(tail)->next, next, PTRCOMBINE(PTR(b), next))) {
                    break;
                }
            } else {
                cas(&q->blast, tail, PTRCOMBINE(PTR(next), tail));
            }
        }
    }
    cas(&q->blast, tail, PTRCOMBINE(PTR(b), tail));
    return BALLOC(b);

}



Block*
casqget(CasQueue *q)
{
	Block *head, *tail, *next, *b;

	if(q->closed)
		error(Ehungup);

	if(PTR(q->bfirst) == PTR(q->blast)) /* queue empty */
		return nil;
	for (;;){
		head = q->bfirst;
		tail = q->blast;
		next = head->next;
		if (head == q->bfirst) {
			if (PTR(head) == PTR(tail)) {
				if (PTR(next) == nil)
					return nil;
				cas(&q->blast, tail, PTRCOMBINE(PTR(next), tail)); /* swing tail */
			} else {
				b = copyblock(PTR(next), BLEN(PTR(next))); // TODO see if we can move copyblock out of loop
				if (cas(&q->bfirst, head, PTRCOMBINE(PTR(next), head))) /* dequeue */
                    //call ptrcombine(next, tail)
					break;
				freeb(PTR(b));
			}
		}
	}

	freeb(PTR(head));
	q->len -= BALLOC(PTR(next));
	// QDEBUG checkb(b, "casqget");
	return PTR(b);
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

