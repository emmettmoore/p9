#include "stuff.h"

/* /port/proc.c * */
/* TI_Proc* */
Proc*
wakeup(Rendez *r)
{
	Proc *p = nil;

	lock(r);
	p = r->p;

    /* if proc is sleeping */
	if(p != nil){
		lock(&p->rlock);
		if(p->state != Wakeme || p->r != r){
			panic("wakeup: state");
		}
		r->p = nil;
		p->r = nil;
        semrelease(&sleepsem, 1);
		unlock(&p->rlock);
	}
	unlock(r);


	return p;
}


/*  sleep if a condition is not true.  Another process will
 *  awaken us after it sets the condition.  When we awaken
 *  the condition may no longer be true.
 *
 *  we lock both the process and the rendezvous to keep r->p
 *  and p->r synchronized.
 *
 * /port/proc.c
 */
void
k_sleep(Rendez *r, int (*f)(void*), void *arg)
{
	Proc* currp = malloc(sizeof(*currp)); // XXX TODO XXX spoof "current process" AKA up
	//currp->pid = getpid();
	lock(r);
	lock(&currp->rlock);
	if(r->p){
		panic("double lock in sleep\n");
	}

	/*
	 *  Wakeup only knows there may be something to do by testing
	 *  r->p in order to get something to lock on.
	 *  Flush that information out to memory in case the sleep is
	 *  committed.
	 */
	r->p = currp; // XXX
    
	if( (*f)(arg) ){
		/*  if condition happened never mind (not sleeping). */
		r->p = nil;
		unlock(&currp->rlock);
		unlock(r);
	} else {
		/*  now we are committed to
		 *  change state and call scheduler
		 */
		currp->state = Wakeme;
		currp->r = r;

        unlock(&currp->rlock);
        unlock(r);
        semacquire(&sleepsem, 1);

        /*
		if(setlabel(&currp->sched)) {
			//  here when the process is awakened
		} else {
			
			//here to go to sleep (i.e. stop Running)
			
			unlock(&currp->rlock);
			unlock(r);
			// gotolabel(&m->sched); XXX
		}
        */
	}
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
	int n, s;
	va_list arg;
	char buf[PRINTSIZE];

//	kprintoq = nil;	/* don't try to write to /dev/kprint */

//	if(panicking)
//		for(;;);
//	panicking = 1;

//	s = splhi();

	strcpy(buf, "panic: ");
	va_start(arg, fmt);
	n = vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	print("%s\n", buf);
//	if(consdebug)
//		(*consdebug)();
//	splx(s);
//	prflush();
//	buf[n] = '\n';
//	putstrn(buf, n+1);
//	dumpstack();

	exits("panic exit");
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

/* /9/port/allocb.c */
Block*
iallocb(int size)
{
    return allocb(size);

	/*
	Block *b = nil;
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
	return b;
	*/
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
