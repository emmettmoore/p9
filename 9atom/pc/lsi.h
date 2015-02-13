enum{
	/* registers */
	Doorbell		= 0x00/4,
	Wseq		= 0x04/4,	/* write sequence */
	Hdiag		= 0x08/4,	/* host diagnostic */
	Testba		= 0x0c/4,	/* test base address */
	Diagd		= 0x10/4,	/* diagnostic data */
	Diaga		= 0x14/4,	/* diagnostic address */
	Int		= 0x30/4,	/* interrupt status */
	Intmask		= 0x34/4,	/* interrupt mask */
	Tqueue		= 0x40/4,	/* requerst queue */
	Rqueue		= 0x44/4,	/* reply queue */
	Hqueue		= 0x48/4,	/* high priority queue */

	/* Doorbell read */
	Dbusy		= 1<<27,	/* doorbell busy */
	Dfmask		= 0xffff,		/* fault mask */

	/* Doorbell write; values only in header not in docs */
	Mur		= 0x40,		/* message unit reset */
	Iur		= 0x41,		/* io unit reset */
	Gladhand	= 0x42,		/* handshake */
	Rfr		= 0x43,		/* reply frame removal */
	Hpac		= 0x44,		/* host page access ctl */

	/* handshake subtypes p 2-11 */
	Hconf		= 0x04,		/* configuration */
	Hevent		= 0x07,		/* event notification */
	Hfwd		= 0x09,		/* firmware download */
	Hfwu		= 0x12,		/* firmware upload */
	Hiocfacts	= 0x03,		/* get controller configuration */
	Hiocinit		= 0x02,		/* controller init */
	Hportenable	= 0x06,		/* port enable */
	Hportfacts	= 0x05,		/* get port configuration */
	
	/* Hdiag */
	Cfbs		= 1<<10,	/* clear flash bad sig */
	Drwe		= 1<<9,	/* write enabled */
	Flashbad		= 1<<6,	/* flash bad signature */
	Reseth		= 1<<5,	/* reset by any function */
	Diagenable	= 1<<4,	/* enable diag registers */
	Reset		= 1<<2,	/* reset adapter */
	Disable		= 1<<1,	/* hold in reset state */
	Dmeme		= 1<<0,	/* enable diag bar[1] */

	/* Int */
	Ids		= 1<<31,	/* ioc rx doorbell “host” */
	Ireply		= 1<<3,	/* reply in reply fifo */
	Idr		= 1<<0,	/* ioc set doorbell */
};

static	uchar	wseqmagic[] = { 0xff, 0x4, 0xb, 0x2, 0x7, 0xd, };
