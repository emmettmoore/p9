#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include <tos.h>
#include "ureg.h"

#include "arm.h"

/*
 * A lot of this stuff doesn't belong here
 * but this is a convenient dumping ground for
 * later sorting into the appropriate buckets.
 */

/* Give enough context in the ureg to produce a kernel stack for
 * a sleeping process
 */
void
setkernur(Ureg* ureg, Proc* p)
{
	ureg->pc = p->sched.pc;
	ureg->sp = p->sched.sp+4;
	ureg->r14 = PTR2UINT(sched);
}

/*
 * called in syscallfmt.c, sysfile.c, sysproc.c
 */
void
validalign(uintptr addr, uint)
{
	if(addr & 3){
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

/* go to user space */
void
kexit(Ureg*)
{
	uvlong t;
	Tos *tos;

	/* precise time accounting, kernel exit */
	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = up->pcycles;
	tos->cyclefreq = m->cpuhz;
	tos->pid = up->pid;
	tos->machno = m->machno;

	/* make visible immediately to user phase */
	l1cache->wbse(tos, sizeof *tos);
}

/*
 *  return the userpc the last exception happened at
 */
uintptr
userpc(void)
{
	Ureg *ureg = up->dbgreg;
	return ureg->pc;
}

/* This routine must save the values of registers the user is not permitted
 * to write from devproc and then restore the saved values before returning.
 */
void
setregisters(Ureg* ureg, char* pureg, char* uva, int n)
{
	USED(ureg, pureg, uva, n);
}

/*
 *  this is the body for all kproc's
 */
static void
linkproc(void)
{
	spllo();
	up->kpfun(up->kparg);
	pexit("kproc exiting", 0);
}

/*
 *  setup stack and initial PC for a new kernel proc.  This is architecture
 *  dependent because of the starting stack location
 */
void
kprocchild(Proc *p, void (*func)(void*), void *arg)
{
	p->sched.pc = PTR2UINT(linkproc);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK);

	p->kpfun = func;
	p->kparg = arg;
}

/*
 *  pc output by dumpaproc
 */
uintptr
dbgpc(Proc* p)
{
	Ureg *ureg;

	ureg = p->dbgreg;
	if(ureg == 0)
		return 0;

	return ureg->pc;
}

/*
 * initialize floating point for a proc structure.
 */
void
procfpinit(Proc *p)
{
	p->fpstate = FPinit;
}

/*
 *  set mach dependent process state to run a new process
 */
void
procsetup(Proc* p)
{
	fpusysprocsetup(p);
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc* p)
{
	uvlong t;

	cycles(&t);
	p->pcycles += t;

	fpuprocsave(p);
	l1cache->wbse(p, sizeof *p);		/* is this needed? */
	l1cache->wb();				/* is this needed? */
}

void
procrestore(Proc* p)
{
	uvlong t;

	if(p->kp)
		return;
	cycles(&t);
	p->pcycles -= t;
	wakewfi();		/* in case there's another runnable proc */

	/* let it fault in at first use */
//	fpuprocrestore(p);
	l1cache->wb();			/* system is more stable with this */
}

void
clonefpu(PFPU *t, PFPU *s)
{
	t->fpusave = s->fpusave;
}

int
userureg(Ureg* ureg)
{
	return (ureg->psr & PsrMask) == PsrMusr;
}
