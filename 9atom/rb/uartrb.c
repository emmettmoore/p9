#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uartp8250.h"

extern PhysUart rbphysuart;	/* BOTCH; should use p8250 directly */

void
wave(char *s)
{
	u32int *io;

	io = (u32int*)PHYSCONS;
	for(; *s; s++){
		io[Thr] = *s;
		microdelay(100);
	}
}

void
waveprint(char *fmt, ...)
{
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	wave(buf);
}

static uint
ioget(void *v, int i)
{
	u32int *io;

	io = v;
	return io[i];
}

static void
ioset(void *v, int i, uint x)
{
	u32int *io;

	io = v;
	io[i] = x;
}

static void
rbinterrupt(void *v)
{
	i8250interrupt(nil, v);
}

static int
itr(Uart *u, int on)
{
	Ctlr *c;

	c = u->regs;
	if(on)
		intrenable(c->irq, rbinterrupt, u /*, u->name*/);
	else
		/* this causes hangs. please debug. (on a pc; not tried yet) */
//		return intrdisable(c->irq, rbinterrupt, u, u->name);
		return -1;
	return 0;
}

PhysUart rbphysuart;

static Ctlr rbctlr = {
	.reg	= (void*)PHYSCONS,
	.get	= ioget,
	.set	= ioset,
	.itr	= itr,
	.irq	= ILduart0,
};

static Uart rbuart = {
	.regs	= &rbctlr,
	.name	= "cons",
	.freq	= 3686000,	/* Not used, we use the global i8250freq */
	.baud	= 115200,	/* switching doesn't work */
	.phys	= &rbphysuart,
	.special	= 1,
	.console	= 0,
	.next	= nil,
};

static Uart*
rb8250pnp(void)
{
	return &rbuart;
}

void*
i8250alloc(int io, int irq, int tbdf)
{
	Ctlr *ctlr;

	if((ctlr = malloc(sizeof(Ctlr))) != nil){
		ctlr->reg = (void*)io;
		ctlr->irq = irq;
		ctlr->tbdf = tbdf;
		ctlr->get = ioget;
		ctlr->set = ioset;
		ctlr->itr = itr;
	}
	return ctlr;
}

int
i8250console(void)
{
	int n;
	char *cmd;
	static int once;

	if(once == 0){
		once = 1;
		memmove(&rbphysuart, &p8250physuart, sizeof(PhysUart));
		rbphysuart.name = "rb";
		rbphysuart.pnp = rb8250pnp;
	}

	n = uartconsconf(&cmd);
	if(n == 0){
		qlock(&rbuart);
		uartctl(&rbuart, "z");
		uartctl(&rbuart, cmd);
		qunlock(&rbuart);
	}
	return 0;
}
