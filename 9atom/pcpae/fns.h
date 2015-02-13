#include "../port/portfns.h"

void	aamloop(int);
Dirtab*	addarchfile(char*, int, long(*)(Chan*,void*,long,vlong), long(*)(Chan*,void*,long,vlong));
int	adec(int*);
int	ainc(int*);
void	archinit(void);
void	bootargs(void*);
usize	cankaddr(uintmem);
int	cas(int*, int, int);
void	clockintr(Ureg*, void*);
int	cmpswap486(long*, long, long);
#define	cmpswap(a, b, c) cmpswap486(a, b, c)
void	(*coherence)(void);
void	cpuid1(Cpuidreg*);
int	cpuidentify(void);
void	cpuid(int, Cpuidreg*);
void	cpuidprint(void);
void	cycles(uvlong*);
void	delay(int);
void	fpclear(void);
void	fpinit(void);
void	fpoff(void);
void	fprestore(FPsave*);
void	fpsave(FPsave*);
char*	getconf(char*);
u32int	getcr0(void);
u32int	getcr2(void);
u32int	getcr3(void);
u32int	getcr4(void);
void	guesscpuhz(int);
void	halt(void);
int	i8042auxcmd(int);
int	i8042auxcmds(uchar*, int);
void	i8042auxenable(void (*)(int));
void	i8042init(void);
void	i8042kbdenable(void);
void	i8042reset(void);
void*	i8250alloc(int, int, int);
void	i8250console(void);
void	i8250mouse(char*, int (*)(Queue*, int), int);
void	i8250setmouseputc(char*, int (*)(Queue*, int));
void	i8253enable(void);
void	i8253init(void);
void	i8253link(void);
uvlong	i8253read(uvlong*);
void	i8253timerset(uvlong);
int	i8259disable(Vctl*);
int	i8259enable(Vctl*);
void	i8259init(void);
int	i8259isr(int);
void	i8259off(void);
void	i8259on(void);
void	idlehands(void);
void	idle(void);
int	inb(int);
u32int	inl(int);
void	insb(int, void*, int);
u16int	ins(int);
void	insl(int, void*, int);
void	inss(int, void*, int);
int	intrdisable(int, void (*)(Ureg *, void *), void*, int, char*);
int	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
void	introff(void);
void	intron(void);
void	invlpg(uintptr);
int	ioalloc(int, int, int, char*);
void	iofree(int);
void	ioinit(void);
int	ioreserve(int, int, int, char*);
int	iounused(int, int);
int	iprint(char*, ...);
int	isaconfig(char*, int, ISAConf*);
u32int	k10monmwait32(u32int*, u32int);
void*	kaddr(uintmem);
#define	kmapinval()
void	lgdt(ushort[3]);
void	lidt(ushort[3]);
void	links(void);
void	ltr(u32int);
void	mach0init(void);
void	mathinit(void);
void	mb586(void);
void	meminit(void);
void	memorysummary(void);
void	mfence(void);
void	mmuidle(void);
void	mmuinit(void);
PTE*	mmuwalk(PTE*, uintptr, int, int);
u32int	(*monmwait32)(u32int*, u32int);
#define	monmwait(v, o)	((int)monmwait32((u32int*)(v), (u32int)(o)))
u32int	nopmonmwait32(u32int*, u32int);
uchar	nvramread(int);
void	nvramwrite(int, uchar);
void	outb(int, int);
void	outl(int, u32int);
void	outsb(int, void*, int);
void	outs(int, u16int);
void	outsl(int, void*, int);
void	outss(int, void*, int);
uintmem	paddr(void*);
void	pause(void);
int	pcicap(Pcidev*, int);
int	pcicfgr16(Pcidev*, int);
int	pcicfgr32(Pcidev*, int);
int	pcicfgr8(Pcidev*, int);
void	pcicfgw16(Pcidev*, int, int);
void	pcicfgw32(Pcidev*, int, int);
void	pcicfgw8(Pcidev*, int, int);
void	pciclrbme(Pcidev*);
void	pciclrioe(Pcidev*);
void	pciclrmwi(Pcidev*);
int	pcigetpms(Pcidev*);
void	pcihinv(Pcidev*);
uchar	pciipin(Pcidev*, uchar);
void	pcireset(void);
int	pciscan(int, Pcidev**);
void	pcisetbme(Pcidev*);
void	pcisetioe(Pcidev*);
void	pcisetmwi(Pcidev*);
int	pcisetpms(Pcidev*, int);
void	pcmcisread(PCMslot*);
int	pcmcistuple(int, int, int, void*, int);
PCMmap*	pcmmap(int, u32int, int, int);
int	(*_pcmspecial)(char *, ISAConf *);
int	pcmspecial(char*, ISAConf*);
void	(*_pcmspecialclose)(int);
void	pcmspecialclose(int);
void	pcmunmap(int, PCMmap*);
int	pdbmap(PTE*, uintmem, uintptr, int);
void	pdptinit(PTE*, PTE*);
void	procrestore(Proc*);
void	procsave(Proc*);
void	procsetup(Proc*);
void	putcr0(u32int);
void	putcr3(u32int);
void	putcr4(u32int);
void*	rampage(void);
void	rdmsr(int, vlong*);
void	realmode(Ureg*);
void	screeninit(void);
void	(*screenputs)(char*, int);
void	sfence(void);
int	strtotbdf(char*, char**, int);
void	syncclock(void);
void	syscallfmt(int, uintptr, va_list);
void	sysretfmt(int, va_list, long, uvlong, uvlong);
int	tas(void*);
void*	tmpmap(Page*);
void	tmpunmap(void*);
void	touser(void*);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit0(void);
void	trapinit(void);
uvlong	tscticks(uvlong*);
void	umbfree(uintptr, int);
uintptr	umbmalloc(uintmem, int, int);
uintmem	upaalloc(int, int);
void	upafree(uintmem, int);
void	upareserve(uintmem, int);
#define	userureg(ur) (((ur)->cs & 0xFFFF) == UESEL)
#define	validalign(addr, sz)				/* x86 doesn't care */
void	vectortable(void);
void*	vmappat(uintmem, usize, int);
int	vmapsync(uintptr);
void*	vmap(uintmem, usize);
void	vunmap(void*, usize);
void	wbinvd(void);
void	wrmsr(int, vlong);
int	xchgw(ushort*, int);
#define	mmuflushtlb() putcr3(PADDR(m->pdpt))
Pcidev*	pcimatch(Pcidev*, int, int);
Pcidev*	pcimatchtbdf(int);
#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define	KADDR(a)	kaddr(a)
#define PADDR(a)	paddr((void*)(a))
#define	dcflush(a, b)
#define BIOSSEG(a)	KADDR(((uint)(a))<<4)

int	cas32(void*, u32int, u32int);
int	cas64(void*, u64int, u64int);
u32int	fas32(u32int*, u32int);
u64int	fas64(u64int*, u64int);

#define	cas(p, e, n)	cas32((p), (u32int)(e), (u32int)(n))
#define casp(p, e, n)	cas32((p), (u32int)(e), (u32int)(n))
#define	fas(p, v)		((int)fas32((u32int*)(p), (u32int)(v)))
#define	fasp(p, v)	((void*)fas32((u32int*)(p), (u32int)(v)))

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

