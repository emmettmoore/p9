#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"init.h"
#include	"pool.h"
#include	"reboot.h"
#include	<tos.h>

Mach *m;

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)
#define	MAXCONF		64

enum {
	/* space for syscall args, return PC, top-of-stack struct */
	Ustkheadroom	= sizeof(Sargs) + sizeof(uintptr) + sizeof(Tos),
};

usize	segpgsizes = (1<<PGSHIFT);	/* only page size available */
Conf conf;
char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;
uchar *sp;	/* user stack of init proc */
int delaylink;

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

extern void mmuinit0(void);
extern void (*i8237alloc)(void);

void
main(void)
{
	mach0init();
	options();
	ioinit();
	i8250console();
	fmtinit();
	screeninit();

	print("\nPlan 9\n");

	trapinit0();
	mmuinit0();

	i8042init();
	i8253init();
	cpuidentify();
	meminit();
	confinit();
	archinit();
	xinit();
	if(i8237alloc != nil)
		i8237alloc();
	trapinit();
	printinit();
	cpuidprint();
	mmuinit();
	if(arch->intrinit)	/* launches other processors on an mp */
		arch->intrinit();
	timersinit();
	mathinit();
	i8042kbdenable();
	if(arch->clockenable)
		arch->clockenable();
	procinit0();
	initseg();
	if(delaylink){
		bootlinks();
		pcimatch(0, 0, 0);
	}else
		links();
	conf.monitor = 1;
	chandevreset();
	pageinit();
	i8253link();
	swapinit();
	userinit();
	active.thunderbirdsarego = 1;
	schedinit();
}

void
mach0init(void)
{
	conf.nmach = 1;
	MACHP(0) = (Mach*)CPU0MACH;
	m->pdb = (PTE*)CPU0PDB;
	m->gdt = (Segdesc*)CPU0GDT;

	machinit();

	active.machs = 1;
	active.exiting = 0;
}

void
machinit(void)
{
	int machno;
	PTE *pdb;
	Segdesc *gdt;

	machno = m->machno;
	pdb = m->pdb;
	gdt = m->gdt;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pdb = pdb;
	m->gdt = gdt;
	m->perf.period = 1;

	/*
	 * For polled uart output at boot, need
	 * a default delay constant. 100000 should
	 * be enough for a while. Cpuidentify will
	 * calculate the real value later.
	 */
	m->loopconst = 100000;
}

void
init0(void)
{
	int i;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	chandevinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", arch->id, conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "386", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		for(i = 0; i < nconf; i++){
			if(confname[i][0] != '*')
				ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
}

void
userinit(void)
{
	void *v;
	Proc *p;
	Segment *s;
	Page *pg;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	p->fpstate = FPinit;
	fpoff();

	/*
	 * Kernel Stack
	 *
	 * N.B. make sure there's enough space for syscall to check
	 *	for valid args and 
	 *	4 bytes for gotolabel's return PC
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-(sizeof(Sargs)+BY2WD);

	/*
	 * User Stack
	 *
	 * N.B. cannot call newpage() with clear=1, because pc kmap
	 * requires up != nil.  use tmpmap instead.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKTOP);
	p->seg[SSEG] = s;
	pg = newpage(0, 0, USTKTOP-(1<<s->lgpgsize), s->lgpgsize);
	v = tmpmap(pg);
	memset(v, 0, BY2PG);
	segpage(s, pg);
	bootargs(v);
	tmpunmap(v);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, UTZERO+BY2PG);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(0, 0, UTZERO, s->lgpgsize);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	v = tmpmap(pg);
	memset(v, 0, BY2PG);
	if(sizeof initcode > BY2PG)
		panic("initcode too big");
	memmove(v, initcode, sizeof initcode);
	tmpunmap(v);

	ready(p);
}

uchar *
pusharg(char *p)
{
	int n;

	n = strlen(p)+1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}

void
bootargs(void *base)
{
 	int i, ac;
	uchar *av[32];
	uchar **lsp;
	char *cp = BOOTLINE;
	char buf[64];

	sp = (uchar*)base + BY2PG - Ustkheadroom;

	ac = 0;
	av[ac++] = pusharg("/386/9dos");

	/* when boot is changed to only use rc, this code can go away */
	cp[BOOTLINELEN-1] = 0;
	buf[0] = 0;
	if(strncmp(cp, "fd", 2) == 0){
		sprint(buf, "local!#f/fd%lddisk", strtol(cp+2, 0, 0));
		av[ac++] = pusharg(buf);
	} else if(strncmp(cp, "sd", 2) == 0){
		sprint(buf, "local!#S/sd%c%c/fs", *(cp+2), *(cp+3));
		av[ac++] = pusharg(buf);
	} else if(strncmp(cp, "ether", 5) == 0)
		av[ac++] = pusharg("-n");

	/* 4 byte word align stack */
	sp = (uchar*)((ulong)sp & ~3);

	/* build argc, argv on stack */
	sp -= (ac+1)*sizeof(sp);
	lsp = (uchar**)sp;
	for(i = 0; i < ac; i++)
		*lsp++ = av[i] + ((USTKTOP - BY2PG) - (ulong)base);
	*lsp = 0;
	sp += (USTKTOP - BY2PG) - (ulong)base - sizeof(ulong);
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

static void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();

	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memset(BOOTLINE, 0, BOOTLINELEN);
	memmove(BOOTARGS, p, n);
	poperror();
	free(p);
}

void
confinit(void)
{
	char *p;
	int i, userpcnt;
	uintmem kpages;

	if(p = getconf("*kernelpercent"))
		userpcnt = 100 - strtol(p, 0, 0);
	else
		userpcnt = 0;

	conf.npage = 0;
	for(i=0; i<nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nimage = 200;

	if(cpuserver) {
		if(userpcnt < 10)
			userpcnt = 70;
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Hack for the big boys. Only good while physmem < 4GB.
		 * Give the kernel fixed max + enough to allocate the
		 * page pool.
		 * This is an overestimate as conf.upages < conf.npages.
		 * The patch of nimage is a band-aid, scanning the whole
		 * page list in imagereclaim just takes too long.
		 */
		if(kpages > (64*MB + conf.npage*sizeof(Page))/BY2PG)
			conf.nimage = 2000;
	} else {
		if(userpcnt < 10) {
			if(conf.npage*BY2PG < 16*MB)
				userpcnt = 40;
			else
				userpcnt = 60;
		}
		kpages = conf.npage - (conf.npage*userpcnt)/100;

		/*
		 * Make sure terminals with low memory get at least
		 * 4MB on the first Image chunk allocation.
		 */
		if(conf.npage*BY2PG < 16*MB)
			imagmem->minarena = 4*1024*1024;
	}

	/*
	 * can't go past the end of virtual memory
	 * (ulong)-KZERO is 2^32 - KZERO
	 */
	if(kpages > ((ulong)-KZERO)/BY2PG)
		kpages = ((ulong)-KZERO)/BY2PG;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kpages *= BY2PG;
	kpages -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image)
		+ conf.nswap;
	mainmem->maxsize = kpages;
	if(!cpuserver){
		/*
		 * give terminals lots of image memory, too; the dynamic
		 * allocation will balance the load properly, hopefully.
		 * be careful with 32-bit overflow.
		 */
		imagmem->maxsize = kpages;
	}
}

static char* mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(void)
{
	int i;
	u32int status;
	char *msg, note[ERRMAX];

	status = up->fpsave.status;

	/*
	 * Some attention should probably be paid here to the
	 * exception masks and error summary.
	 */
	msg = "unknown exception";
	for(i = 1; i <= 5; i++){
		if(!((1<<i) & status))
			continue;
		msg = mathmsg[i];
		break;
	}
	if(status & 0x01){
		if(status & 0x40){
			if(status & 0x200)
				msg = "stack overflow";
			else
				msg = "stack underflow";
		}else
			msg = "invalid operation";
	}
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=%#ux",
		msg, (uintptr)up->fpsave.pc, status);
	postnote(up, 1, note, NDebug);
}

/*
 *  math coprocessor error
 */
static void
matherror(Ureg *ur, void*)
{
	/*
	 *  a write cycle to port 0xF0 clears the interrupt latch attached
	 *  to the error# line from the 387
	 */
	if(!(m->cpuiddx & 0x01))
		outb(0xF0, 0xFF);

	/*
	 *  save floating point state to check out error
	 */
	fpenv(&up->fpsave);
	mathnote();

	if((ur->pc & 0xf0000000) == KZERO)
		panic("fp: status %ux fppc=%#p pc=%#p",
			up->fpsave.status, (uintptr)up->fpsave.pc, ur->pc);
}

/*
 *  math coprocessor emulation fault
 */
static void
mathemu(Ureg *ureg, void*)
{
	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		fpinit();
		up->fpstate = FPactive;
		break;
	case FPinactive:
		/*
		 * Before restoring the state, check for any pending
		 * exceptions, there's no way to restore the state without
		 * generating an unmasked exception.
		 * More attention should probably be paid here to the
		 * exception masks and error summary.
		 */
		if((up->fpsave.status & ~up->fpsave.control) & 0x07F){
			mathnote();
			break;
		}
		fprestore(&up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		panic("math emu pid %ld %s pc 0x%lux", 
			up->pid, up->text, ureg->pc);
		break;
	}
}

/*
 *  math coprocessor segment overrun
 */
static void
mathover(Ureg*, void*)
{
	pexit("math overrun", 0);
}

void
mathinit(void)
{
	trapenable(VectorCERR, matherror, 0, "matherror");
	if(X86FAMILY(m->cpuidax) == 3)
		intrenable(IrqIRQ13, matherror, 0, BUSUNKNOWN, "matherror");
	trapenable(VectorCNA, mathemu, 0, "mathemu");
	trapenable(VectorCSO, mathover, 0, "mathover");
}

/*
 *  set up floating point for a new process
 */
void
procsetup(Proc*p)
{
	p->fpstate = FPinit;
	fpoff();
}

void
procrestore(Proc *p)
{
	uvlong t;

	if(p->kp)
		return;
	cycles(&t);
	p->pcycles -= t;
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	uvlong t;

	cycles(&t);
	p->pcycles += t;
	if(p->fpstate == FPactive){
		if(p->state == Moribund)
			fpclear();
		else{
			/*
			 * Fpsave() stores without handling pending
			 * unmasked exeptions. Postnote() can't be called
			 * here as sleep() already has up->rlock, so
			 * the handling of pending exceptions is delayed
			 * until the process runs again and generates an
			 * emulation fault to activate the FPU.
			 */
			fpsave(&p->fpsave);
		}
		p->fpstate = FPinactive;
	}

	/*
	 * While this processor is in the scheduler, the process could run
	 * on another processor and exit, returning the page tables to
	 * the free list where they could be reallocated and overwritten.
	 * When this processor eventually has to get an entry from the
	 * trashed page tables it will crash.
	 *
	 * If there's only one processor, this can't happen.
	 * You might think it would be a win not to do this in that case,
	 * especially on VMware, but it turns out not to matter.
	 */
	mmuflushtlb();
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	/*
	 * setting exiting will make hzclock() on each processor call exit(0),
	 * which calls shutdown(0) and arch->reset(), which on mp systems is
	 * mpshutdown, from which there is no return: the processor is idled
	 * or initiates a reboot.  clearing our bit in machs avoids calling
	 * exit(0) from hzclock() on this processor.
	 */
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once)
		iprint("cpu%d: exiting\n", m->machno);

	/* wait for any other processors to shutdown */
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}

	if(active.ispanic){
		if(!cpuserver)
			for(;;)
				halt();
		delay(10000);
	}else
		delay(1000);
}

static void
apshut(void *v)
{
	int i;

	i = (int)v;
	for(;;){
		procwired(up, i);
		sched();
		if(m->machno == i)
			break;
		print("cpu%d: resched %.2ux\n", m->machno, active.machs);
	}
	splhi();
	if(arch)
		arch->introff();
//	else
		i8259off();
	lock(&active);
	active.machs &= ~(1<<i);
	unlock(&active);
	print("cpu%d: halt %.2ux\n", m->machno, active.machs);
	idle();
}

void
reboot(void *entry, void *code, ulong size)
{
	int i;
	PTE *pdb;
	void (*f)(ulong, ulong, ulong);

	writeconf();

	/*
	 * the boot processor is cpu0.  execute this function on it
	 * so that the new kernel has the same cpu0.  this only matters
	 * because the hardware has a notion of which processor was the
	 * boot processor and we look at it at start up.
	 */
	for(;;){
		procwired(up, 0);
		sched();
		if(m->machno == 0)
			break;
	}

	for(i = 1; i < MAXMACH; i++)
		if(active.machs & 1<<i)
			kproc("apshutdown", apshut, (void*)i);

	while(active.machs != 1)
		sched();

	print("cpu%d: stop %.2ux\n", m->machno, active.machs);
	splhi();
	if(arch){
		arch->introff();
		if(arch->intrreset)
			arch->intrreset();
	}
//	else
		i8259off();

	/* turn off buffered serial console */
	serialoq = nil;

	/* shutdown devices */
	chandevshutdown();

	/*
	 * Modify the machine page table to directly map the low 4MB of memory
	 * This allows the reboot code to turn off the page mapping
	 */
	pdb = m->pdb;
	pdb[PDX(0)] = pdb[PDX(KZERO)];
	mmuflushtlb();

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));

	if(entry == nil){
		print("halted\n");
		for(;;);
	}

	print("cpu%d: rebooting...\n", m->machno);

	/* off we go - never to return; rationale for coherence? */
	coherence();
	(*f)(PADDR(entry), PADDR(code), size);
}

void
exit(int ispanic)
{
	shutdown(ispanic);
	arch->reset();
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[32], *p;
	int i;

	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return 0;

	isa->type = "";
	strecpy(isa->buf, isa->buf+sizeof isa->buf, p);
	isa->nopt = tokenize(isa->buf, isa->opt, NISAOPT);
	for(i = 0; i < isa->nopt; i++){
		p = isa->opt[i];
		if(cistrncmp(p, "type=", 5) == 0)
			isa->type = p + 5;
		else if(cistrncmp(p, "port=", 5) == 0)
			isa->port = strtoull(p+5, &p, 0);
		else if(cistrncmp(p, "irq=", 4) == 0)
			isa->irq = strtoull(p+4, &p, 0);
		else if(cistrncmp(p, "dma=", 4) == 0)
			isa->dma = strtoull(p+4, &p, 0);
		else if(cistrncmp(p, "mem=", 4) == 0)
			isa->mem = strtoull(p+4, &p, 0);
		else if(cistrncmp(p, "size=", 5) == 0)
			isa->size = strtoull(p+5, &p, 0);
		else if(cistrncmp(p, "freq=", 5) == 0)
			isa->freq = strtoull(p+5, &p, 0);
		else if(cistrncmp(p, "tbdf=", 5) == 0)
			isa->tbdf = strtotbdf(p+5, &p, 0);
	}
	return 1;
}

/*
 *  put the processor in the halt state if we've no processes to run.
 *  an interrupt will get us going again.
 */
void
idlehands(void)
{
	if(conf.nmach == 1 /*&& m->machno != 0*/)
		halt();
}