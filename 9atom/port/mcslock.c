#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "../port/edf.h"

extern void cgascreenputr(Rune);
#define	D(c)	if(0)cgascreenputr((c))

static void
mcslock(Lock *lk, LockEntry *ql)
{
	LockEntry *pred;

	D('!');
	ql->next = nil;
	ql->locked = 0;
	pred = fasp(&lk->head, ql);
	if(pred != nil){
		ql->locked = 1;
		sfence();	/* ensure reader sees updated value */
		pred->next = ql;
		while(monmwait(&ql->locked, 1) == 1)
			{}
	}
}

static int
mcscanlock(Lock *lk, LockEntry *ql)
{
	D('?');
	ql->next = nil;
	ql->locked = 0;
	return casp(&lk->head, nil, ql);
}

static LockEntry*
mcsunlock(Lock *lk, LockEntry *ql)
{
	D('#');
	if(ql->next != nil || !casp(&lk->head, ql, nil)){
		/* successor, wait for list to catch up */
		while(ql->next == nil)
			{}
		ql->next->locked = 0;
		sfence();
	}
	return ql;
}

static LockEntry*
allocle(Lock *l, uintptr pc)
{
	LockEntry *a;
	int i;

	a = &MACHP(m->machno)->locks[0];
	if(a->used != nil){
		i = nelem(m->locks)-1;
		while(--i >= 0){
			a++;
			if(a->used == nil)
				break;
		}
		if(i < 0)
			panic("allocle: need more m->locks");
	}
	a->used = l;	/* must be first, to claim against interrupts */
	a->pc = pc;
	a->p = up;
	a->m = MACHP(m->machno);
	a->isilock = 0;
	return a;
}

static LockEntry*
findle(Lock *l)
{
	LockEntry *a;

	a = l->e;
	if(a->used != l)
		panic("findle");
	return a;
}

int
lock(Lock *l)
{
	LockEntry *ql;

	if(up != nil)
		up->nlocks++;
	ql = allocle(l, getcallerpc(&l));
	mcslock(l, ql);
	l->e = ql;
	return 0;
}

int
canlock(Lock *l)
{
	LockEntry *ql;

	if(up != nil)
		up->nlocks++;
	ql = allocle(l, getcallerpc(&l));
	if(mcscanlock(l, ql)){
		l->e = ql;
		return 1;
	}
	ql->used = nil;
	if(up != nil)
		up->nlocks--;
	return 0;
}

void
unlock(Lock *l)
{
	LockEntry *ql;

	if(l->head == nil){
		print("unlock: not locked: pc %#p\n", getcallerpc(&l));
		return;
	}
	ql = findle(l);
	if(ql->isilock)
		panic("unlock of ilock: pc %#p", getcallerpc(&l));
	if(ql->p != up)
		panic("unlock: up changed: pc %#p, acquired at pc %#p, lock p %#p, unlock up %#p",
			getcallerpc(&l), ql->pc, ql->p, up);
	mcsunlock(l, ql);
	ql->used = nil;
	if(up != nil && --up->nlocks == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

void
ilock(Lock *l)
{
	uintptr pc;
	Mpl pl;
	LockEntry *ql;

	pc = getcallerpc(&l);
	pl = splhi();
	ql = allocle(l, pc);
	ql->isilock = 1;
	ql->pl = pl;
	/* the old taslock code would splx(s) to allow interrupts while waiting (if not nested) */
	mcslock(l, ql);

	l->e = ql;
	m->ilockdepth++;
	m->ilockpc = pc;
	if(up != nil)
		up->lastilock = l;
}

void
iunlock(Lock *l)
{
	Mpl pl;
	LockEntry *ql;

	if(islo())
		panic("iunlock while lo: pc %#p\n", getcallerpc(&l));
	ql = findle(l);
	if(!ql->isilock)
		panic("iunlock of lock: pc %#p\n", getcallerpc(&l));
	if(ql->m != MACHP(m->machno)){
		panic("iunlock by mach%d, locked by mach%d: pc %#p\n",
			m->machno, ql->m->machno, getcallerpc(&l));
	}
	mcsunlock(l, ql);
	pl = ql->pl;
	ql->used = nil;
	m->ilockdepth--;
	if(up != nil)
		up->lastilock = nil;
	splx(pl);
}

int
ownlock(Lock *l)
{
	int i;

	for(i = 0; i < nelem(m->locks); i++)
		if(m->locks[i].used == l)
			return 1;
	return 0;
}

uintptr
lockgetpc(Lock *l)
{
	LockEntry *ql;

	if(l != nil && (ql = l->e) != nil && ql->used == l)
		return ql->pc;
	return 0;
}

void
locksetpc(Lock *l, uintptr pc)
{
	LockEntry *ql;

	if(l != nil && (ql = l->e) != nil  && ql->used == l && ql->m == MACHP(m->machno))
		ql->pc = pc;
}
