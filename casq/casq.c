/*
 * TODO:
 *   - move PTR stuff to its own file
 *   - implement PTR for amd64 kernel (if time)
 *   - review the code, make sure it adheres to the style guide
 *       (tabs for indents, no unnecessary { }, etc.)
 */
/*
 * This alternate concurrent queue was written by Caleb Malchik, Emmett Moore,
 * and Dan Defossez for a senior capstone project at Tufts University. The
 * algorithm is based on the paper ``Simple,  Fast,  and  Practical
 * Non-Blocking and Blocking Concurrent Queue Algorithms'' by Maged M. Michael
 * and Michael L. Scott:
 * http://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf
 * It started as a rewrite of parts of qio(9), but the new algorithm proved
 * incompatible with the qio interface. Also relys on some functions defined in
 * qio.c.
 *
 * Things about the code to be aware of:
 *  - Locks are not used, so the queue cannot be protected by locks.
 *  - The first block on the queue, pointed to be bfirst, is a dummy node.
 *  - blast does not always point to the last block in the list.
 *
 * The paper lists the following invariants necessary for safety:
 *  1. The linked list is always connected.
 *  2. Nodes are only inserted after the last node in the linked list.
 *  3. Nodes are only deleted from the beginning of the linked list.
 *  4. bfirst always points to the first node of the linked list (the dummy).
 *  5. blast always points to a node in the linked list.
 *
 * Many functions could be ported from qio(9). Qwindow, qfull, and qhangup
 * (with q->inilim) would be easily implemented for casq. Qlen (with q->dlen)
 * could be implemented similarly to casqsize, with the same caviats about
 * up-to-dateness. Casqputnolim, (like qpassnolim but only for single blocks)
 * could also be added very easily. Any more complex read or write operations
 * such as qbwrite and qread would be much more difficult and may not be
 * feasible to implement with this lock-free algorithm.
 * - Caleb Malchik (cmalchiK@gmail.com)
 */

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
 *  but faster with many readers/writers on multicore systems.
 */
typedef struct CasQueue	CasQueue;

struct CasQueue
{
	Lock;

	Block*	bfirst;		/* dummy node */
	Block*	blast;		/* not always the end of the list */

	int	len;		/* bytes allocated to queue */
	int	limit;		/* max bytes in queue */
	int	closed;
};

/*
 * called by non-interrupt code
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
 * enqueue a block
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

/*
 * dequeue a block
 */
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

/*
 * may not be up to date if there are reads/writes in progress
 */
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

