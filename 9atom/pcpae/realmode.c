#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"

/*
 * we depend on aux/realemu to emulate realmode calls.
 */

#define LORMBUF (RMBUF-KZERO)

static void*
lomap(uint off)
{
	static uintptr va;
#define O BY2PG

	if(va == 0){
		va = PTR2UINT(vmappat(O, 0x100000 - O, PATWB));
		if(va == 0)
			panic("can't map lo memory");
	}
	return UINT2PTR(va+off - O);
}

static ulong wtab[] = {
	LORMBUF,	LORMBUF+BY2PG,		/* realmode buffer page */
	0xA0000,	0xC0000,				/* mda/vga range */
};

static long
rmemrw(int isr, void *va, long n, vlong off)
{
	uchar *a;
	int i;

	a = va;
	if(off >= 0x100000)
		return 0;
	if(off < 0 || n < 0)
		error("bad offset/count");
	if(off+n > 0x100000)
		n = 0x100000 - off;

	if(isr){
		if(off == 0){
			if(n >= 4096)
				memmove(a, KADDR(0), 4096);
			else
				memmove(a, KADDR(0), n);
			n -= 4096;
			off += 4096;
			a += 4096;
		}
		if(n > 0)
			memmove(a, lomap((ulong)off), n);
	}else{
		/* writes are more restricted */
		for(i = 0;; i += 2)
			if(i == nelem(wtab))
				error("bad offset/count in write");
			else if(off >= wtab[i] && off+n <= wtab[i+1]){
				memmove(lomap((ulong)off), a, n);
				break;
			}
	}

	return n;
}

static long
rmemread(Chan*, void *a, long n, vlong off)
{
	return rmemrw(1, a, n, off);
}

static long
rmemwrite(Chan*, void *a, long n, vlong off)
{
	return rmemrw(0, a, n, off);
}

void
realmodelink(void)
{
	addarchfile("realmodemem", 0660, rmemread, rmemwrite);
}
