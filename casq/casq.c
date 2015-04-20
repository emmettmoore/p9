#define THREEINCH
#include "stuff.h"
#include <stdio.h>

#define PTRSCREEN 0x1FFFFFFF
#define PTRLEN    32
#define PTRHDRLEN 3

#define PTR(p)                 ((Block*) ((int)p & PTRSCREEN))
#define PTRPLUS(p)             ((Block*) ((int)p +  (1 << (PTRLEN - PTRHDRLEN))))
#define PTRCOUNT(p)           ((Block*) ((int)p & ~PTRSCREEN))
#define PTRCOMBINE(p1, p2)     ((Block*) ((int)p1 | ((int) PTRCOUNT(PTRPLUS(p2)))))

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

	q = malloc(sizeof(*q));
	if(q == 0)
		return 0;

	/* add dummy node */
	dummy = allocb(0);
	q->bfirst = dummy;
	q->blast = q->bfirst;
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
	int len, qlen;
    
	if(q->closed)
		error(Ehungup);

	len = BALLOC(b);
    b->next = 0;
    for(;;) {
        if (q->len > q->limit)
            return -1;
        tail = q->blast;
        next = PTR(tail)->next;
        if (tail == q->blast) {
            if (PTR(next) == nil) {
                if (cas(&PTR(tail)->next, next, PTRCOMBINE(PTR(b), next))) {
                    break;
                }
            } else {
                cas(&q->blast, tail, PTRCOMBINE(PTR(next), tail));
            }
        }
    }
    cas(&q->blast, tail, PTRCOMBINE(PTR(b), tail));

	/* atomic replacement for q->len += BALLOC(b) */
	len = BALLOC(PTR(b));
	for(;;) {
		qlen = q->len;
		if(cas(&q->len, qlen, qlen + len))
			break;
	}

    return len;
}

Block*
casqget(CasQueue *q)
{
	Block *head, *tail, *next, *b;
	int len, qlen;

	if(q->closed)
		error(Ehungup);

	if(PTR(q->bfirst) == PTR(q->blast)) /* queue empty */
		return nil;
	for (;;){
		head = q->bfirst;
		tail = q->blast;
		next = PTR(head)->next;
		if (head == q->bfirst) {
			if (PTR(head) == PTR(tail)) {
				if (PTR(next) == nil)
					return nil;
				cas(&q->blast, tail, PTRCOMBINE(PTR(next), tail)); /* swing tail */
			} else {
				b = copyblock(PTR(next), BLEN(PTR(next)));
				if (cas(&q->bfirst, head, PTRCOMBINE(PTR(next), head))) /* dequeue */
					break;
				freeb(PTR(b));
			}
		}
	}
	freeb(PTR(head));

	/* atomic replacement for q->len -= BALLOC(b) */
	len = BALLOC(b);
	for(;;) {
		qlen = q->len;
		if(cas(&q->len, qlen, qlen - len))
			break;
	}

	return b;
}

/* may not be up to date if there are reads/writes in progress */
int
casqsize(CasQueue *q)
{
    return q->len;
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

