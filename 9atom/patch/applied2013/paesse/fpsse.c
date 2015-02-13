#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define	dprint(...) print(__VA_ARGS__)
#define diprint(...) iprint(__VA_ARGS__)

extern	void	x87off(void);
extern	void	x87init(void);

extern	void	ssesave(FPsave*);
extern	void	sserestore(FPsave*);
extern	void	sseclear(void);
extern	void	ldmxcsr(u32int);

/* assume all m's have the same sse cap */
enum{
	Fxrstor		= 1<<24,

	/* cr0 */
	Moncoproc	= 1<<1,
	X87em		= 1<<2,
	TS		= 1<<3,	/* task switch */
	NE		= 1<<5,	/* x87 numeric error */

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

	SSEflags		= SSEie | SSEde | SSEze | SSEoe | SSEue | SSEpe,
	SSEmask	= SSEdm | SSEum | SSEpm
};

typedef struct Fxsave Fxsave;
struct Fxsave {
	u16int	fcw;			/* x87 control word */
	u16int	fsw;			/* x87 status word */
	u8int	ftw;			/* x87 tag word */
	u8int	zero;			/* 0 */
	u16int	fop;			/* last x87 opcode */
	u32int	ip;			/* last x87 instruction pointer */
	u16int	cs;			/* last x87 code segment */
	u16int	r1;
	u32int	dp;			/* data pointer */
	u16int	ds;			/* data pointer segment */
	u16int	r2;
	u32int	mxcsr;			/* sse control and status */
	u32int	mxcsrmask;		/* supported sse feature bits */
	uchar	st[128];		/* shared 64-bit media and x87 regs */
	uchar	xmm[256];		/* 128-bit media regs */
	uchar	ign[96];		/* reserved, ignored */
};

#define savetox(save)	((Fxsave*)ROUNDUP(PTR2UINT(save), Fpalign))

static void
sseenable(void)
{
//	putcr0(getcr0() & ~(X87em | Moncoproc));
	x87init();
	ldmxcsr(SSEmask);
}

static void
sseoff(void)
{
//	putcr0(getcr0() & ~(X87em | Moncoproc) | NE | TS);
	x87off();
}

static void
sseover(Ureg*, void*)
{
	pexit("sse error", 0);
}

static void
sserestoreen(FPsave *p)
{
	sseenable();
	sserestore(p);
}

static void
sseclonestate(FPsave *t, FPsave *s)
{
	Fxsave *r;

	memmove(t, s, sizeof(FPsave));
	r = savetox(t);
	r->ftw = 0;			/* all x87 registers invalid (clear stack) */
	r->fsw &= ~SSEflags;		/* clear x87 status */
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

static char *mathmsg[] =
{
	nil,	/* handled below */
	"denormalized operand",
	"division by zero",
	"numeric overflow",
	"numeric underflow",
	"precision loss",
};

static void
mathnote(uint status)
{
	char *msg, note[ERRMAX];
	int i;
	Fxsave *x;

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
	x = savetox(&up->fpsave);
	snprint(note, sizeof note, "sys: fp: %s fppc=%#p status=%#ux",
		msg, (uintptr)x->ip, status);
	postnote(up, 1, note, NDebug);
}

static void
sseerror(Ureg *ur, void*)
{
	Fxsave *x;

	/*
	 *  save floating point state to check out error
	 */
	dprint("%lud: %s: %s: sserror\n", up->pid, up->user, up->text);
	ssesave(&up->fpsave);
	x = savetox(&up->fpsave);
	mathnote(x->mxcsr & SSEflags);

	if((ur->pc & 0xf0000000) == KZERO)
		panic("sse error in kernel pc=%#p", ur->pc);
}

/*
 *  math coprocessor error
 */
static void
x87error(Ureg *ur, void*)
{
	Fxsave *x;

	dprint("%lud: %s: %s: x87error\n", up->pid, up->user, up->text);
	/*
	 *  a write cycle to port 0xF0 clears the interrupt latch attached
	 *  to the error# line from the 387
	 */
	if(!(m->cpuiddx & 0x01))
		outb(0xF0, 0xFF);

	/*
	 *  save floating point state to check out error
	 */
	ssesave(&up->fpsave);
	x = savetox(&up->fpsave);
	mathnote(x->fsw & ~x->fcw);

	if((ur->pc & 0xf0000000) == KZERO)
		panic("fp: status %ux fppc=%#p pc=%#p",
			x->fsw, (uintptr)x->ip, ur->pc);
}

/*
 *  math coprocessor emulation fault
 */
static void
sseemu(Ureg *ureg, void*)
{
	Fxsave *x;

	if(up->fpstate & FPillegal){
		/* someone did floating point in a note handler */
		postnote(up, 1, "sys: floating point in note handler", NDebug);
		return;
	}
	switch(up->fpstate){
	case FPinit:
		sseenable();
extern void ssezero(void);
ssezero();
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
		x = savetox(&up->fpsave);
		if((x->fsw & ~x->fcw) & 0x07F){
			diprint("%lud: mathnote (on restore)\n", up->pid);
			mathnote(x->fsw);
			break;
		}
		if(x->mxcsr & SSEflags){
			diprint("%lud: sse mathnote (on restore)\n", up->pid);
			mathnote(x->mxcsr & SSEflags);
			break;
		}
		x->fsw &= ~SSEim;
		sserestoreen(&up->fpsave);
		up->fpstate = FPactive;
		break;
	case FPactive:
		panic("math emu pid %lud %s pc %#p", 
			up->pid, up->text, ureg->pc);
		break;
	}
}

long 
ssedevprocio(Proc *p, void *va, long n, uvlong offset, int write)
{
	uchar *fp;
	int sz;

	fp = (uchar*)savetox(&p->fpsave);
	sz = Fpsize;

	if(offset >= sz)
		n = 0;
	else if(offset+n > sz)
		n = sz - offset;

	if(write)
		memmove(fp+offset, va, n);
	else
		memmove(va, fp+offset, n);
	return n;
}

extern long (*fpudevprocio)(Proc*, void*, long, uvlong, int);

static void
ssemathinit(void)
{
	if(m->cpuiddx & (Sse2 | Fxrstor) != (Sse2 | Fxrstor))
		panic("no sse2 support");

	trapenable(VectorCERR, x87error, 0, "x87error");
	trapenable(VectorCNA, sseemu, 0, "sseemu");
	trapenable(VectorSIMD, sseerror, 0, "sseerror");
	trapenable(VectorCSO, sseover, 0, "sseover");		// Vector#XE
	fpudevprocio = ssedevprocio;
}

void
fpoff(void)
{
	sseoff();
}

void
fpclear(void)
{
	sseclear();
}

void
fpsave(FPsave *save)
{
	ssesave(save);
	sseclear();
}

void
mathinit(void)
{
	if(m->machno == 0)
		ssemathinit();
	sseinit();
}
