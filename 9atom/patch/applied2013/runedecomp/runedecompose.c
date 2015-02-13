#include <u.h>
#include <libc.h>
#include "runedecompose.h"

static uint*
bsearch32(uint c, uint *t, int n, int ne)
{
	uint *p;
	int m;

	while(n > 1) {
		m = n/2;
		p = t + m*ne;
		if(c >= p[0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n && c == t[0])
		return t;
	return 0;
}

static uvlong*
bsearch64(uvlong c, uvlong *t, int n, int ne)
{
	uvlong *p;
	int m;

	while(n > 1) {
		m = n/2;
		p = t + m*ne;
		if(c >= p[0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n && c == t[0])
		return t;
	return 0;
}

int
runedecompose(Rune a, Rune *d)
{
	uint *p;
	uvlong *q;

	if(a <= 0xffff){
		p = bsearch32(a, __decompose2, nelem(__decompose2)/2, 2);
		if(p){
			d[0] = p[1] >> 16;
			d[1] = p[1] & 0xffff;
			return 0;
		}
	}
	q = bsearch64(a, __decompose264, nelem(__decompose264)/2, 2);
	if(q){
		d[0] = q[1] >> 32;
		d[1] = q[1] & 0xfffffff;
		return 0;
	}
	return -1;
}
