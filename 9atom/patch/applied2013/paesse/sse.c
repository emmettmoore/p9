#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

extern	void	ssesave(FPsave*);
extern	void	sserestore(FPsave*);
extern	void	sseclear(void);
extern	void	ldmxcsr(uint);

/* assume all m's have the same sse cap */
enum{
	Fxrstor		= 1<<24,

	/* cr0 */
	Moncoproc	= 1<<1,
	X87em		= 1<<2,
	TS		= 1<<3,	/* task switch */
	NE		= 1<<4,	/* x87 numeric error */

	/* cr4 */
	Osfxsr		= 1<<9,	/* enable sse */
	Osxmexcept	= 1<<10,	/* enable #XM exception */
};

enum {
	SSEie		= 1<<0,	/* invalid instruction */
	SSEde		= 1<<1,	/* denormal */
	SSEze		= 1<<2,	/* div by zero */
	SSEoe		= 1<<3,	/* overflow */
	SSEue		= 1<<4,	/* underflow */
	SSEpe		= 1<<5,	/* precision loss */
	SSEdaz		= 1<<6,	/* denormals are zero */
	SSEim		= 1<<7,	/* invalid instruction mask */
	SSEdm		= 1<<8,	/* denormal mask */
	SSEzm		= 1<<9,	/* div by zero mask */
	SSEom		= 1<<10,	/* overflow mask */
	SSEum		= 1<<11,	/* underflow mask */
	SSEpm		= 1<<12,	/* precision loss mask */
	SSErc		= 3<<13,	/* rounding control */
	SSEfz		= 1<<15,	/* */
	
	SSEmask	= SSEum | SSEpm
};

typedef struct Fxsave Fxsave;
struct Fxsave {
	u16int	fcw;			/* x87 control word */
	u16int	fsw;			/* x87 status word */
	u8int	ftw;			/* x87 tag word */
	u8int	zero;			/* 0 */
	u16int	fop;			/* last x87 opcode */
	u64int	rip;			/* last x87 instruction pointer */
	u64int	rdp;			/* last x87 data pointer */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
	uchar	st[128];		/* shared 64-bit media and x87 regs */
	uchar	xmm[256];		/* 128-bit media regs */
	uchar	ign[96];		/* reserved, ignored */
};

static void
sseenable(void)
{
//	putcr0(getcr0() & ~(X87em | Moncoproc));
	fpinit();
	ldmxcsr(SSEmask);
}

static void
sseoff(void)
{
//	putcr0(getcr0() & ~(X87em | Moncoproc) | NE | TS);
	fpoff();
}

static void
ssenote(void)
{
iprint("%d: %lud: see note\n", m->machno, up->pid);
	postnote(up, 1, "sse error", NDebug);
}

static void
sseover(Ureg*, void*)
{
	pexit("sse error", 0);
}

static void
sseerror(Ureg *u, void*)
{
	/*
	 *  save floating point state to check out error
	 */
iprint("%lud: sserror\n", up? up->pid: -1);
	fp->env(up->fpusave);
	ssenote();

	if((u->pc & 0xf0000000) == KZERO)
		panic("sse error in kernel pc=%#p", u->pc);
}

void
ssesaveoff(FPsave *p)
{
	ssesave(p);
	sseclear();
}

static void
sserestoreen(FPsave *p)
{
	sseenable();
	sserestore(p);
}

static int
sseinit(void)
{
	if(m->cpuiddx & (Sse2 | Fxrstor) != (Sse2 | Fxrstor))
		return -1;
	putcr4(getcr4() | Osfxsr | Osxmexcept);
	sseoff();
	return 0;
}

static void
ssemachinit(void)
{
	sseinit();
}

static uint
ssegetfsw(FPsave *f)
{
	Fxsave *x;

	x = (Fxsave*)f->sse;
	return x->fsw;
}

static FPArch sse = {
	"sse",
	ssemachinit,		/* machinit */
	sseoff,			/* off */
	sseenable,		/* init */
	ssesaveoff,		/* save */
	sserestoreen,		/* restore */
	ssesave,			/* env */
	sseclear,			/* clear */
	ssegetfsw,
};

static int
enabled(char *s)
{
	return s != nil && (cistrcmp(s, "enable") == 0 || atoi(s) > 0);
}

void
sselink(void)
{
	if(!enabled(getconf("*sse")) || sseinit() == -1)
		return;
	print("sseinit\n");
	trapenable(VectorSIMD, sseerror, 0, "simderror");
	// trapenable(Vector#XE, sseover, 0, "simdover");
	fp = &sse;
}
