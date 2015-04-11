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
	int	dlen;		/* data bytes in queue */
	int	limit;		/* max bytes in queue */
	int	inilim;		/* initial limit */
	int	noblock;	/* true if writes return immediately when q full */
	int	eof;		/* number of eofs read by user */

	char	err[ERRMAX];
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


	q->state |= Qstarve;
	q->eof = 0;
	q->noblock = 0;

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
casqet(CasQueue *q)
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


/* copy functions above this line */

static int
notempty(void *a)
{
	CasQueue *q = a;

	return (q->state & Qclosed) || q->bfirst != q->blast;
}

/*
 *  get next block from a queue (up to a limit)
 */
Block*
casqbread(CasQueue *q, int len)
{
	Block *head;
	Block *tail;
	Block *next;
	Block *b, ret;
	int n;

	if(q->bfirst == q->blast) /* queue empty */
		return nil;
    for (;;) {
        head = q->bfirst;
        tail = q->blast;
        /* if we get here, there's at least one block in the queue */
        if (head == q->bfirst) {
            if (head == tail) {
                if (next == nil) {
                    return nil; /* XXX maybe continue instead? */
                }
                cas(&q->blast, tail, next); /* swing tail */
            } 
            else {
				ret = copyblock(next, BLEN(next));
				if (cas(&q->bfirst, head, next)) { /* dequeue */
                    n = BLEN(ret);
                    /* split block if it's too big; */
                    if (n > len) { /* XXX can we handle this lock free (qputback)? */
                        n -= len;
                        b = allocb(n);
                        memmove(b->wp, ret->rp+len, n);
                        qputback(q, b); /* XXX */
                    }
                    ret->wp = ret->rp + len;
					break;
				}
				freeb(ret);
			}

	freeb(head);
	ilock(q);
	q->len -= BALLOC(next);
	q->dlen -= BLEN(next);
	iunlock(q);
	QDEBUG checkb(ret, "casqbwrite");
	return ret;
}

/* XXX put block on front of queue (i.e. infront of head)
 * not sure if this is possible with algo.
 * prob not. 
 * if not, no reason to implement qbwrite because it 
 */ 
void
qputback(Queue *q, Block *b)
{
	b->next = q->bfirst;
	if(q->bfirst == nil)
		q->blast = b;
	q->bfirst = b;
	q->len += BALLOC(b);
	q->dlen += BLEN(b);
}

#define CASQIO
#ifndef CASQIO

/*
 *  add a block to a queue obeying flow control
 */
long
casqbwrite(CasQueue *q, Block *b)
{
	int n, dowakeup;
	Proc *p;

	counter(Cqbwritecnt);

	n = BLEN(b);

	if(q->bypass){
		(*q->bypass)(q->arg, b);
		return n;
	}

	dowakeup = 0;
	qlock(&q->wlock);
	if(waserror()){
		if(b != nil)
			freeb(b);
		qunlock(&q->wlock);
		nexterror();
	}

	ilock(q);

	/* give up if the queue is closed */
	if(q->state & Qclosed){
		iunlock(q);
		error(q->err);
	}

	/* if nonblocking, don't queue over the limit */
	if(q->len >= q->limit){
		if(q->noblock){
			iunlock(q);
			freeb(b);
			qunlock(&q->wlock);
			poperror();
			return n;
		}
	}

	/* queue the block */
	q->blast->next = b;

	q->blast = b;
	b->next = 0;
	q->len += BALLOC(b);
	q->dlen += n;
	QDEBUG checkb(b, "qbwrite");
	b = nil;

	/* make sure other end gets awakened */
	if(q->state & Qstarve){
		q->state &= ~Qstarve;
		dowakeup = 1;
	}
	iunlock(q);

	/*  get output going again */
	if(q->kick && (dowakeup || (q->state&Qkick)))
		q->kick(q->arg);

	/* wakeup anyone consuming at the other end */
	if(dowakeup){
		p = wakeup(&q->rr);

		/* if we just wokeup a higher priority process, let it run */
		if(p != nil && p->priority > up->priority){
			counter(Cqbwritepricnt);
			sched();
		}
	}

	/*
	 *  flow control, wait for queue to get below the limit
	 *  before allowing the process to continue and queue
	 *  more.  We do this here so that postnote can only
	 *  interrupt us after the data has been queued.  This
	 *  means that things like 9p flushes and ssl messages
	 *  will not be disrupted by software interrupts.
	 *
	 *  Note - this is moderately dangerous since a process
	 *  that keeps getting interrupted and rewriting will
	 *  queue infinite crud.
	 */
	for(;;){
		if(q->noblock || qnotfull(q))
			break;

		ilock(q);
		q->state |= Qflow;
		iunlock(q);
		sleep(&q->wr, qnotfull, q);
	}
	USED(b);

	qunlock(&q->wlock);
	poperror();
	return n;
}

/*
 *  be extremely careful when calling this,
 *  as there is no reference accounting
 */
void
casqfree(CasQueue *q)
{
	qclose(q);
	free(q);
}

/*
 *  Mark a queue as closed.  No further IO is permitted.
 *  All blocks are released.
 */
void
casqclose(CasQueue *q)
{
	Block *bfirst;

	if(q == nil)
		return;

	/* mark it */
	ilock(q);
	q->state |= Qclosed;
	q->state &= ~(Qflow|Qstarve);
	strcpy(q->err, Ehungup);
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
	q->dlen = 0;
	q->noblock = 0;
	iunlock(q);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	wakeup(&q->rr);
	wakeup(&q->wr);
}


/*
 * return space remaining before flow control
 */
int
casqwindow(CasQueue *q)
{
	int l;

	l = q->limit - q->len;
	if(l < 0)
		l = 0;
	return l;
}

/*
 *  return true if we can read without blocking
 */
int
casqcanread(CasQueue *q)
{
	return q->bfirst!=q->blast;
}

/*
 *  change queue limit
 */
void
casqsetlimit(CasQueue *q, int limit)
{
	q->limit = limit;
}

/*
 *  set blocking/nonblocking
 */
void
casqnoblock(CasQueue *q, int onoff)
{
	q->noblock = onoff;
}

/*
 *  flush the output queue
 */
void
casqflush(CasQueue *q)
{
	Block *bfirst;

	/* mark it */
	ilock(q);
	bfirst = q->bfirst;
	q->bfirst = 0;
	q->len = 0;
	q->dlen = 0;
	iunlock(q);

	/* free queued blocks */
	freeblist(bfirst);

	/* wake up readers/writers */
	wakeup(&q->wr);
}
#endif
