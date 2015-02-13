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

usize	segpgsizes = (1<<PGSHIFT)|(1<<XPGSHIFT);	/* only page sizes available by default */
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
	kbdenable();
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
	m->pdpt = (PTE*)CPU0PDPT;
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
	PTE *pdb, *pdpt;
	Segdesc *gdt;

	machno = m->machno;
	pdpt = m->pdpt;
	pdb = m->pdb;
	gdt = m->gdt;
	memset(m, 0, sizeof(Mach));
	m->machno = machno;
	m->pdpt = pdpt;
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
	int i, kpcnt;
	uintmem kpages, kpagelim, kmem, ppages;

	conf.npage = 0;
	for(i=0; i<nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage;

	kpcnt = 40;
	if(p = getconf("*kernelpercent"))
		kpcnt = strtol(p, nil, 0);

	/* can't go past the end of virtual memory */
	kpages = (conf.npage*kpcnt)/100;
	kpagelim = ((uintptr)-KZERO)/BY2PG;
	if(kpages > kpagelim)
		kpages = kpagelim;

	conf.upages = conf.npage - kpages;
	conf.ialloc = (kpages/2)*BY2PG;

	/*
	 * face up to the fact that pae doesn't give us more address space.
	 * allow for enough small pages to cover ~6gb with 256mb of kernel
	 * space.  upages isn't used much, except if we happen to be using
	 * the pool library.  
	 */
	ppages = (conf.upages*sizeof(Page))/BY2PG;
	if((vlong)(kpages - ppages) < 64*MB/BY2PG){
		ppages = kpages * 61 / 100;
		if(ppages * BY2PG / sizeof(Page) < conf.upages)
			conf.upages = ppages * BY2PG / sizeof(Page);
	}

	/* hand-waving attempt to support small memory machines */
	conf.nproc = 200;
	conf.nimage = 200;
	if((vlong)(kpages - ppages)*10/100 > 2000*sizeof(Proc)/BY2PG
	&& conf.upages >= 256*MB/BY2PG){
		conf.nproc = 2000;
		conf.nimage = 2000;
	}

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
	kmem = kpages*BY2PG;
	kmem -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image);
	mainmem->maxsize = kmem;
	imagmem->maxsize = kmem;		/* dynamicly balance */
	print("kmem %P = PGSZ*(%P+%P) upages %lud\n",
		kmem, kpages, ppages, conf.upages);
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
	mmuidle();
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
reboot(void *entry, void *code, usize size)
{
	int i;
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
	m->pdb[PDX(0)] = m->pdb[PDX(KZERO)];	/* identity map 0:2mb */
	m->pdpt[KSEG] = PADDR(m->pdb) | PTEVALID;
	m->pdpt[0] = m->pdpt[KSEG];			/* extra cheat: use pdb[KSEG] as pdb for phys 0 */
	putcr3(PADDR(m->pdpt));

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
			isa->port = strtoul(p+5, &p, 0);
		else if(cistrncmp(p, "irq=", 4) == 0)
			isa->irq = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "dma=", 4) == 0)
			isa->dma = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "mem=", 4) == 0)
			isa->mem = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "size=", 5) == 0)
			isa->size = strtoul(p+5, &p, 0);
		else if(cistrncmp(p, "freq=", 5) == 0)
			isa->freq = strtoul(p+5, &p, 0);
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
	/* this is an admission that the scheduler is not very good */
	if(m->machno != 0)
		halt();
	else
		pause();
}
