/* override default macros from ../port/usb.h */
#undef	dprint
#undef	ddprint
#undef	deprint
#undef	ddeprint
#define dprint(...)	do if(ehcidebug)print(__VA_ARGS__); while(0)
#define ddprint(...)	do if(ehcidebug>1)print(__VA_ARGS__); while(0)
#define deprint(...)	do if(ehcidebug || ep->debug)print(__VA_ARGS__); while(0)
#define ddeprint(...)	do if(ehcidebug>1 || ep->debug>1)print(__VA_ARGS__); while(0)

typedef struct Ctlr Ctlr;
typedef struct Eopio Eopio;
typedef struct Isoio Isoio;
typedef struct Poll Poll;
typedef struct Qh Qh;
typedef struct Qtree Qtree;

#pragma incomplete Ctlr;
#pragma incomplete Eopio;
#pragma incomplete Isoio;
#pragma incomplete Poll;
#pragma incomplete Qh;
#pragma incomplete Qtree;

struct Poll
{
	Lock;
	Rendez;
	int	must;
	int	does;
};

struct Ctlr
{
	Rendez;			/* for waiting to async advance doorbell */
	Lock;			/* for ilock. qh lists and basic ctlr I/O */
	QLock	portlck;	/* for port resets/enable... (and doorbell) */
	int	active;		/* in use or not */
	Ecapio*	capio;		/* Capability i/o regs */
	Eopio*	opio;		/* Operational i/o regs */

	int	nframes;	/* 1024, 512, or 256 frames in the list */
	u32int*	frames;		/* periodic frame list (hw) */
	Qh*	qhs;		/* async Qh circular list for bulk/ctl */
	Qtree*	tree;		/* tree of Qhs for the periodic list */
	int	ntree;		/* number of dummy qhs in tree */
	Qh*	intrqhs;		/* list of (not dummy) qhs in tree  */
	Isoio*	iso;		/* list of active Iso I/O */
	uint	load;
	uint	isoload;
	int	nintr;		/* number of interrupts attended */
	int	ntdintr;	/* number of intrs. with something to do */
	int	nqhintr;	/* number of async td intrs. */
	int	nisointr;	/* number of periodic td intrs. */
	int	nreqs;
	Poll	poll;
};

/*
 * Operational registers (hw)
 */
struct Eopio
{
	u32int	cmd;		/* 00 command */
	u32int	sts;		/* 04 status */
	u32int	intr;		/* 08 interrupt enable */
	u32int	frno;		/* 0c frame index */
	u32int	seg;		/* 10 bits 63:32 of EHCI datastructs (unused) */
	u32int	frbase;		/* 14 frame list base pa (note: just 32 bits), 4096-byte boundary */
	u32int	link;		/* 18 link for async list */
	uchar	d2c[0x40-0x1c];	/* 1c dummy */
	u32int	config;		/* 40 1: all ports default-routed to this HC */
	u32int	portsc[1];	/* 44 Port status and control, one per port */

	/*
	 * these are present on the Kirkwood USB controller.
	 * are they standard now?  Marvell doesn't document them publically.
	 */
	uchar	_pad0[0x64-0x48];
	u32int	otgsc;
	u32int	usbmode;
	u32int	epsetupsts;
	u32int	epprime;
	u32int	epflush;
	u32int	epsts;
	u32int	epcompl;
	u32int	epctl[6];

	/* freescale registers */
	uchar	_pad1[0x2c0-0x98];
	u32int	snoop1;
	u32int	snoop2;
	u32int	agecntthresh;
	u32int	prictl;		/* priority control */
	u32int	sictl;		/* system interface control */

	uchar	_pad2[0x3c0-0x2d4];
	u32int	ctl;
};

extern int ehcidebug;
extern Ecapio *ehcidebugcapio;
extern int ehcidebugport;

void	ehcilinkage(Hci *hp);
void	ehcimeminit(Ctlr *ctlr);
void	ehcirun(Ctlr *ctlr, int on);
