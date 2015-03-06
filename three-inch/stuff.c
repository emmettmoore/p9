#define THREEINCH
#include "stuff.h"

/*
int up;
*/

/*
 *  sleep if a condition is not true.  Another process will
 *  awaken us after it sets the condition.  When we awaken
 *  the condition may no longer be true.
 *
 *  we lock both the process and the rendezvous to keep r->p
 *  and p->r synchronized.
 */
void
sleep(Rendez *r, int (*f)(void*), void *arg)
{
	int s;
	void (*pt)(Proc*, int, vlong);

	s = splhi();

	if(up->nlocks)
		print("process %lud sleeps with %ud locks held, last lock %#p locked at pc %#p, sleep called from %#p\n",
			up->pid, up->nlocks, up->lastlock, lockgetpc(up->lastlock), getcallerpc(&r));
	lock(r);
	lock(&up->rlock);
	if(r->p){
		print("double sleep called from %#p, %lud %lud\n", getcallerpc(&r), r->p->pid, up->pid);
		dumpstack();
	}

	/*
	 *  Wakeup only knows there may be something to do by testing
	 *  r->p in order to get something to lock on.
	 *  Flush that information out to memory in case the sleep is
	 *  committed.
	 */
	r->p = up;

	if((*f)(arg) || up->notepending){
		/*
		 *  if condition happened or a note is pending
		 *  never mind
		 */
		r->p = nil;
		unlock(&up->rlock);
		unlock(r);
	} else {
		/*
		 *  now we are committed to
		 *  change state and call scheduler
		 */
		pt = proctrace;
		if(pt)
			pt(up, 5, 0); // SSleep is defined as 5 somewhere
		up->state = Wakeme;
		up->r = r;

		/* statistics */
		m->cs++;

		procsave(up);
		if(setlabel(&up->sched)) {
			/*
			 *  here when the process is awakened
			 */
			procrestore(up);
			spllo();
		} else {
			/*
			 *  here to go to sleep (i.e. stop Running)
			 */
			unlock(&up->rlock);
			unlock(r);
			gotolabel(&m->sched);
		}
	}

	if(up->notepending) {
		up->notepending = 0;
		splx(s);
		if(up->procctl == Proc_exitme && up->closingfgrp)
			forceclosefgrp();
		error(Eintr);
	}

	splx(s);
}


/* /port/allocb.c */
void
freeb(Block *b)
{
/*	void *dead = (void*)Bdead;
	long ref;

	if(b == nil || (ref = _xdec(&b->ref)) > 0)
		return;

	if(ref < 0){
		dumpstack();
		panic("ref %ld callerpc %#p", ref, getcallerpc(&b));
	}
*/
	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
/*	if(b->free) {
		b->free(b);
		return;
	}
	if(b->flag & BINTR) {
		ilock(&ialloc);
		ialloc.bytes -= b->lim - b->base;
		iunlock(&ialloc);
	}
*/
	/* poison the block in case someone is still holding onto it */
/*
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	free(b);
*/
}

/* /9/port/devcons.c */
void
panic(char *fmt, ...)
{
	/*
	int n, s;
	va_list arg;
	char buf[PRINTSIZE];

	kprintoq = nil;*/	/* don't try to write to /dev/kprint */
/*
	if(panicking)
		for(;;);
	panicking = 1;

	s = splhi();
	strcpy(buf, "panic: ");
	va_start(arg, fmt);
	n = vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	iprint("%s\n", buf);
	if(consdebug)
		(*consdebug)();
	splx(s);
	prflush();
	buf[n] = '\n';
	putstrn(buf, n+1);
	dumpstack();

	exit(1);
*/
}

/* /port/allocb.c */
Hdrspc = 64;

/* /port/allocb.c */
static Block*
_allocb(int size)
{
	Block *b;
	uchar *p;
	int n;

	n = BLOCKALIGN + ROUNDUP(size+Hdrspc, BLOCKALIGN) + sizeof(Block);
	if((p = malloc(n)) == nil)
		return nil;

	b = (Block*)(p + n - sizeof(Block));	/* block at end of allocated space */
	b->base = p;
	b->next = nil;
	b->list = nil;
	b->free = 0;
	b->flag = 0;

	/* align base and bounds of data */
	b->lim = (uchar*)(PTR2UINT(b) & ~(BLOCKALIGN-1));

	/* align start of writable data, leaving space below for added headers */
	b->rp = b->lim - ROUNDUP(size, BLOCKALIGN);
	b->wp = b->rp;

	if(b->rp < b->base || b->lim - b->rp < size)
		panic("_allocb");

	return b;
}
Block*
allocb(int size)
{
	Block *b = nil;

	/* Check in a process and wait until successful.
	 * Can still error out of here, though.
     */
	if((b = _allocb(size)) == nil){
		xsummary();
		mallocsummary();
		panic("allocb: no memory for %d bytes", size);
	}
	setmalloctag(b, getcallerpc(&size));

	return b;
    
}

/* /9/port/taslock.c */
void
ilock(Lock *l)
{
	(void) l;
}


/* /9/port/taslock.c */
void
iunlock(Lock *l)
{
	(void) l;
}

Proc*
wakeup(Rendez *r)
{
	Proc *p = nil;
	/*
	int s;

	s = splhi();

	lock(r);
	p = r->p;

	if(p != nil){
		lock(&p->rlock);
		if(p->state != Wakeme || p->r != r){
			iprint("%p %p %d\n", p->r, r, p->state);
			panic("wakeup: state");
		}
		r->p = nil;
		p->r = nil;
		ready(p);
		unlock(&p->rlock);
	}
	unlock(r);

	splx(s);

	*/
	return p;
}

/* /9/port/allocb.c */
Block*
iallocb(int size)
{
	Block *b = nil;
	/*
	static int m1, m2, mp;

	if(ialloc.bytes > conf.ialloc){
		if((m1++%10000)==0){
			if(mp++ > 1000){
				active.exiting = 1;
				exit(0);
			}
			iprint("iallocb: limited %lud/%lud\n",
				ialloc.bytes, conf.ialloc);
		}
		return nil;
	}

	if((b = _allocb(size)) == nil){
		if((m2++%10000)==0){
			if(mp++ > 1000){
				active.exiting = 1;
				exit(0);
			}
			iprint("iallocb: no memory %lud/%lud\n",
				ialloc.bytes, conf.ialloc);
		}
		return nil;
	}
	setmalloctag(b, getcallerpc(&size));
	b->flag = BINTR;

	ilock(&ialloc);
	ialloc.bytes += b->lim - b->base;
	iunlock(&ialloc);
	*/
	return b;
}

void
nexterror(void)
{
	return;
}

void sched(void)
{
	/* slightly simpler than round robin is the "one process only" scheduler */
}
/* in a .s file, I believe. Not tryna do that right now */
int setlabel(Label*)
{
	return 0;
}

void error (char * str)
{
	(void) str;
}

/* /port/qmalloc.c */
void xsummary() {}
void mallocsummary() {}
