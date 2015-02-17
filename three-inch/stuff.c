#define THREEINCH
#include "stuff.h"

/*
int up;
*/

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
Block*
allocb(int size)
{
	Block *b = nil;
	/*

	 * Check in a process and wait until successful.
	 * Can still error out of here, though.
	if(up == nil)
		panic("allocb without up: %#p", getcallerpc(&size));
	if((b = _allocb(size)) == nil){
		xsummary();
		mallocsummary();
		panic("allocb: no memory for %d bytes", size);
	}
	setmalloctag(b, getcallerpc(&size));

	 */
	return nil;
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
