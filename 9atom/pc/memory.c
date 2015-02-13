/*
 * Size memory and create the kernel page-tables on the fly while doing so.
 * Called from main(), this code should only be run by the bootstrap processor.
 *
 * MemMin is what the bootstrap code in l.s has already mapped;
 * MemMax is the limit of physical memory to scan.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

#define MEMDEBUG	0

enum {
	MemUPA		= 0,		/* unbacked physical address */
	MemRAM		= 1,		/* physical memory */
	MemUMB		= 2,		/* upper memory block (<16MB) */
	MemReserved	= 3,
	NMemType	= 4,

	KB		= 1024,

	/*
	 * for a kernel to boot, we need MemMin > end+sizeof pagetables.
	 */
	MemMin	= 8*MB,
	MemMax	= (3*1024+768)*MB,	/* maximum physical memory to check */
};

typedef struct Map Map;
struct Map {
	uintmem	size;		/* bytes */
	uintmem	addr;
};

typedef struct RMap RMap;
struct RMap {
	char*	name;
	Map*	map;
	Map*	mapend;

	Lock;
};

/* 
 * Memory allocation tracking.
 */
static Map mapupa[64];
static RMap rmapupa = {
	"unallocated unbacked physical memory",
	mapupa,
	&mapupa[nelem(mapupa)-1],
};

static Map xmapupa[16];
static RMap xrmapupa = {
	"unbacked physical memory",
	xmapupa,
	&xmapupa[nelem(xmapupa)-1],
};

static Map mapram[16];
static RMap rmapram = {
	"physical memory",
	mapram,
	&mapram[nelem(mapram)-1],
};

static Map mapumb[64];
static RMap rmapumb = {
	"upper memory block",
	mapumb,
	&mapumb[nelem(mapumb)-1],
};

static Map mapumbrw[16];
static RMap rmapumbrw = {
	"UMB device memory",
	mapumbrw,
	&mapumbrw[nelem(mapumbrw)-1],
};

void
mapprint(RMap *rmap)
{
	Map *mp;

	print("%s\n", rmap->name);	
	for(mp = rmap->map; mp->size; mp++)
		print("\t%P %P (%llud)\n", mp->addr, mp->addr+mp->size, (uvlong)mp->size);
}

void
memdebug(void)
{
	uintmem maxpa, maxpa1, maxpa2;

	maxpa = (nvramread(0x18)<<8)|nvramread(0x17);
	maxpa1 = (nvramread(0x31)<<8)|nvramread(0x30);
	maxpa2 = (nvramread(0x16)<<8)|nvramread(0x15);
	print("maxpa = %#P -> %#P, maxpa1 = %#P maxpa2 = %#P\n",
		maxpa, MB+maxpa*KB, maxpa1, maxpa2);

	mapprint(&rmapram);
	mapprint(&rmapumb);
	mapprint(&rmapumbrw);
	mapprint(&rmapupa);
}

void
mapfree(RMap* rmap, uintmem addr, uintmem size)
{
	Map *mp;
	uintmem t;

	if(size <= 0)
		return;

	lock(rmap);
	for(mp = rmap->map; mp->addr <= addr && mp->size; mp++)
		;

	if(mp > rmap->map && (mp-1)->addr+(mp-1)->size == addr){
		(mp-1)->size += size;
		if(addr+size == mp->addr){
			(mp-1)->size += mp->size;
			while(mp->size){
				mp++;
				(mp-1)->addr = mp->addr;
				(mp-1)->size = mp->size;
			}
		}
	}
	else{
		if(addr+size == mp->addr && mp->size){
			mp->addr -= size;
			mp->size += size;
		}
		else do{
			if(mp >= rmap->mapend){
				print("mapfree: %s: losing %#P, %P\n",
					rmap->name, addr, size);
				break;
			}
			t = mp->addr;
			mp->addr = addr;
			addr = t;
			t = mp->size;
			mp->size = size;
			mp++;
			size = t;
		}while(size != 0);
	}
	unlock(rmap);
}

uintmem
mapalloc(RMap* rmap, uintmem addr, int size, int align)
{
	Map *mp;
	uintmem maddr, oaddr;

	lock(rmap);
	for(mp = rmap->map; mp->size; mp++){
		maddr = mp->addr;

		if(addr){
			/*
			 * A specific address range has been given:
			 *   if the current map entry is greater then
			 *   the address is not in the map;
			 *   if the current map entry does not overlap
			 *   the beginning of the requested range then
			 *   continue on to the next map entry;
			 *   if the current map entry does not entirely
			 *   contain the requested range then the range
			 *   is not in the map.
			 */
			if(maddr > addr)
				break;
			if(mp->size < addr - maddr)	/* maddr+mp->size < addr, but no overflow */
				continue;
			if(addr - maddr > mp->size - size)	/* addr+size > maddr+mp->size, but no overflow */
				break;
			maddr = addr;
		}

		if(align > 0)
			maddr = ((maddr+align-1)/align)*align;
		if(mp->addr+mp->size-maddr < size)
			continue;

		oaddr = mp->addr;
		mp->addr = maddr+size;
		mp->size -= maddr-oaddr+size;
		if(mp->size == 0){
			do{
				mp++;
				(mp-1)->addr = mp->addr;
				(mp-1)->size = mp->size;
			}while(mp->size);
		}

		unlock(rmap);
		if(oaddr != maddr)
			mapfree(rmap, oaddr, maddr-oaddr);

		return maddr;
	}
	unlock(rmap);

	return 0;
}

/*
 * Allocate from the ram map directly to make page tables.
 * Called by mmuwalk during e820scan.
 */
void*
rampage(void)
{
	uintmem m;
	void *p;

	m = mapalloc(&rmapram, 0, BY2PG, BY2PG);
	if(m == 0 || (m & ~(uintmem)0xFFFFFFFF) != 0)
		return nil;
	p = KADDR(m);
	memset(p, 0, BY2PG);
	return p;
}

static void
umbexclude(char *p)
{
	int size;
	ulong addr;
	char *op, *rptr;

	if(p == nil)
		return;
	while(p && *p != '\0' && *p != '\n'){
		op = p;
		addr = strtoul(p, &rptr, 0);
		if(rptr == nil || rptr == p || *rptr != '-'){
			print("umbexclude: invalid argument <%s>\n", op);
			break;
		}
		p = rptr+1;

		size = strtoul(p, &rptr, 0) - addr + 1;
		if(size <= 0){
			print("umbexclude: bad range <%s>\n", op);
			break;
		}
		if(rptr != nil && *rptr == ',')
			*rptr++ = '\0';
		p = rptr;

		mapalloc(&rmapumb, addr, size, 0);
	}
}

static void
umbscan(void)
{
	char *s;
	uchar o[2], *p;

	s = getconf("*noumb");
	if(s && strcmp(s, "0") != 0)
		umbexclude("0xd0000-0xfffff");
	/*
	 * Scan the Upper Memory Blocks (0xA0000->0xF0000) for pieces
	 * which aren't used; they can be used later for devices which
	 * want to allocate some virtual address space.
	 * Check for two things:
	 * 1) device BIOS ROM. This should start with a two-byte header
	 *    of 0x55 0xAA, followed by a byte giving the size of the ROM
	 *    in 512-byte chunks. These ROM's must start on a 2KB boundary.
	 * 2) device memory. This is read-write.
	 * There are some assumptions: there's VGA memory at 0xA0000 and
	 * the VGA BIOS ROM is at 0xC0000. Also, if there's no ROM signature
	 * at 0xE0000 then the whole 64KB up to 0xF0000 is theoretically up
	 * for grabs; check anyway.
	 */
	p = KADDR(0xD0000);
	while(p < (uchar*)KADDR(0xE0000)){
		/*
		 * Check for the ROM signature, skip if valid.
		 */
		if(p[0] == 0x55 && p[1] == 0xAA){
			p += p[2]*512;
			continue;
		}

		/*
		 * Is it writeable? If yes, then stick it in
		 * the UMB device memory map. A floating bus will
		 * return 0xff, so add that to the map of the
		 * UMB space available for allocation.
		 * If it is neither of those, ignore it.
		 */
		o[0] = p[0];
		p[0] = 0xCC;
		o[1] = p[2*KB-1];
		p[2*KB-1] = 0xCC;
		if(p[0] == 0xCC && p[2*KB-1] == 0xCC){
			p[0] = o[0];
			p[2*KB-1] = o[1];
			mapfree(&rmapumbrw, PADDR(p), 2*KB);
		}
		else if(p[0] == 0xFF && p[1] == 0xFF)
			mapfree(&rmapumb, PADDR(p), 2*KB);
		p += 2*KB;
	}

	p = KADDR(0xE0000);
	if(p[0] != 0x55 || p[1] != 0xAA){
		p[0] = 0xCC;
		p[64*KB-1] = 0xCC;
		if(p[0] != 0xCC && p[64*KB-1] != 0xCC)
			mapfree(&rmapumb, PADDR(p), 64*KB);
	}

	umbexclude(getconf("umbexclude"));
}

static void
lowraminit(void)
{
	uintmem n, pa, x;
	uchar *bda;

	/*
	 * Initialise the memory bank information for conventional memory
	 * (i.e. less than 640KB). The base is the first location after the
	 * bootstrap processor MMU information and the limit is obtained from
	 * the BIOS data area.
	 */
	x = PADDR(CPU0END);
	bda = (uchar*)KADDR(0x400);
	n = ((bda[0x14]<<8)|bda[0x13])*KB-x;
	mapfree(&rmapram, x, n);
	memset(KADDR(x), 0, n);			/* keep us honest */

	x = PADDR(PGROUND((uintptr)end));
	pa = MemMin;
	if(x > pa)
		panic("kernel too big");
	mapfree(&rmapram, x, pa-x);
	memset(KADDR(x), 0, pa-x);		/* keep us honest */
}

static void
ramscan(ulong maxmem)
{
	uintptr *k0, kzero, map, *va, vbase, x;
	uintmem pa, maxpa, maxkpa;
	PTE *table, *pte;
	int nvalid[NMemType];

	/*
	 * The bootstrap code has has created a prototype page
	 * table which maps the first MemMin of physical memory to KZERO.
	 * The page directory is at m->pdb and the first page of
	 * free memory is after the per-processor MMU information.
	 */
	pa = MemMin;

	/*
	 * Check if the extended memory size can be obtained from the CMOS.
	 * If it's 0 then it's either not known or >= 64MB. Always check
	 * at least 24MB in case there's a memory gap (up to 8MB) below 16MB;
	 * in this case the memory from the gap is remapped to the top of
	 * memory.
	 * The value in CMOS is supposed to be the number of KB above 1MB.
	 */
	if(maxmem == 0){
		x = (nvramread(0x18)<<8)|nvramread(0x17);
		if(x == 0 || x >= (63*KB))
			maxpa = MemMax;
		else
			maxpa = MB+x*KB;
		if(maxpa < 24*MB)
			maxpa = 24*MB;
	}else
		maxpa = maxmem;
	maxkpa = (u32int)-KZERO;	/* 2^32 - KZERO */

	/*
	 * March up memory from MemMin to maxpa 1MB at a time,
	 * mapping the first page and checking the page can
	 * be written and read correctly. The page tables are created here
	 * on the fly, allocating from low memory as necessary.
	 */
	k0 = (ulong*)KADDR(0);
	kzero = *k0;
	map = 0;
	x = 0x12345678;
	memset(nvalid, 0, sizeof(nvalid));
	
	/*
	 * Can't map memory to KADDR(pa) when we're walking because
	 * can only use KADDR for relatively low addresses.  Instead,
	 * map each 4MB we scan to the virtual address range 4MB-8MB
	 * while we are scanning.
	 */
	vbase = MemMin;
	while(pa < maxpa){
		/*
		 * Map the page. Use mapalloc(&rmapram, ...) to make
		 * the page table if necessary, it will be returned to the
		 * pool later if it isn't needed.  Map in a fixed range (the second 4M)
		 * because high physical addresses cannot be passed to KADDR.
		 */
		va = (void*)(vbase + pa%(4*MB));
		table = &m->pdb[PDX(va)];
		if(pa%(4*MB) == 0){
			if(map == 0 && (map = mapalloc(&rmapram, 0, BY2PG, BY2PG)) == 0)
				break;
			memset(KADDR(map), 0, BY2PG);
			*table = map|PTEWRITE|PTEVALID;
			memset(nvalid, 0, sizeof(nvalid));
		}
		table = KADDR(PPN(*table));
		pte = &table[PTX(va)];

		*pte = pa|PTEWRITE|PTEUNCACHED|PTEVALID;
		mmuflushtlb();
		/*
		 * Write a pattern to the page and write a different
		 * pattern to a possible mirror at KZERO. If the data
		 * reads back correctly the chunk is some type of RAM (possibly
		 * a linearly-mapped VGA framebuffer, for instance...) and
		 * can be cleared and added to the memory pool. If not, the
		 * chunk is marked uncached and added to the UMB pool if <16MB
		 * or is marked invalid and added to the UPA pool.
		 */
		*va = x;
		*k0 = ~x;
		if(*va == x){
			nvalid[MemRAM] += MB/BY2PG;
			mapfree(&rmapram, pa, MB);

			do{
				*pte++ = pa|PTEWRITE|PTEVALID;
				pa += BY2PG;
			}while(pa % MB);
			mmuflushtlb();
			/* memset(va, 0, MB); so damn slow to memset all of memory */
		}
		else if(pa < 16*MB){
			nvalid[MemUMB] += MB/BY2PG;
			mapfree(&rmapumb, pa, MB);

			do{
				*pte++ = pa|PTEWRITE|PTEUNCACHED|PTEVALID;
				pa += BY2PG;
			}while(pa % MB);
		}
		else{
			nvalid[MemUPA] += MB/BY2PG;
			mapfree(&rmapupa, pa, MB);

			*pte = 0;
			pa += MB;
		}
		/*
		 * Done with this 4MB chunk, review the options:
		 * 1) not physical memory and >=16MB - invalidate the PDB entry;
		 * 2) physical memory - use the 4MB page extension if possible;
		 * 3) not physical memory and <16MB - use the 4MB page extension
		 *    if possible;
		 * 4) mixed or no 4MB page extension - commit the already
		 *    initialised space for the page table.
		 */
		if(pa%(4*MB) == 0 && pa >= 32*MB && nvalid[MemUPA] == (4*MB)/BY2PG){
			/*
			 * If we encounter a 4MB chunk of missing memory
			 * at a sufficiently high offset, call it the end of
			 * memory.  Otherwise we run the risk of thinking
			 * that video memory is real RAM.
			 */
			break;
		}
		if(pa <= maxkpa && pa%(4*MB) == 0){
			table = &m->pdb[PDX(KADDR(pa - 4*MB))];
			if(nvalid[MemUPA] == (4*MB)/BY2PG)
				*table = 0;
			else if(nvalid[MemRAM] == (4*MB)/BY2PG && (m->cpuiddx & 0x08))
				*table = (pa - 4*MB)|PTESIZE|PTEWRITE|PTEVALID;
			else if(nvalid[MemUMB] == (4*MB)/BY2PG && (m->cpuiddx & 0x08))
				*table = (pa - 4*MB)|PTESIZE|PTEWRITE|PTEUNCACHED|PTEVALID;
			else{
				*table = map|PTEWRITE|PTEVALID;
				map = 0;
			}
		}
		mmuflushtlb();
		x += 0x3141526;
	}
	/*
	 * If we didn't reach the end of the 4MB chunk, that part won't
	 * be mapped.  Commit the already initialised space for the page table.
	 */
	if(pa % (4*MB) && pa <= maxkpa){
		m->pdb[PDX(KADDR(pa))] = map|PTEWRITE|PTEVALID;
		map = 0;
	}
	if(map)
		mapfree(&rmapram, map, BY2PG);

	m->pdb[PDX(vbase)] = 0;
	mmuflushtlb();

	mapfree(&rmapupa, pa, (u32int)-pa);
	*k0 = kzero;
}

/*
 * BIOS Int 0x15 E820 memory map.
 */
enum
{
	SMAP = ('S'<<24)|('M'<<16)|('A'<<8)|'P',
	Ememory = 1,
	Ereserved = 2,
	Carry = 1,
};

typedef struct Emap Emap;
struct Emap
{
	u64int	base;
	u64int	len;
	u32int	type;
};
static Emap emap[48];
int nemap;

static char *etypes[] =
{
	"type=0",
	"memory",
	"reserved",
	"acpi reclaim",
	"acpi nvs",
};

static int
emapcmp(const void *va, const void *vb)
{
	Emap *a, *b;
	
	a = (Emap*)va;
	b = (Emap*)vb;
	if(a->base < b->base)
		return -1;
	if(a->base > b->base)
		return 1;
	if(a->len < b->len)
		return -1;
	if(a->len > b->len)
		return 1;
	return a->type - b->type;
}

static void
map(uintmem base, uintmem len, int type)
{
	uintmem e, n;
	PTE *table, flags;
	uintmem maxkpa;
	
	/*
	 * Split any call crossing MemMin to make below simpler.
	 */
	if(base < MemMin && len > MemMin-base){
		n = MemMin - base;
		map(base, n, type);
		map(MemMin, len-n, type);
		return;
	}
	
	/*
	 * Let lowraminit and umbscan hash out the low MemMin.
	 */
	if(base < MemMin)
		return;

	/*
	 * Any non-memory below 16*MB is used as upper mem blocks.
	 */
	if(type == MemUPA && base < 16*MB && base+len > 16*MB){
		map(base, 16*MB-base, MemUMB);
		map(16*MB, len-(16*MB-base), MemUPA);
		return;
	}
	
	/*
	 * Memory below CPU0END is reserved for the kernel
	 * and already mapped.
	 */
	if(base < PADDR(CPU0END)+BY2PG){
		n = PADDR(CPU0END)+BY2PG - base;
		if(len <= n)
			return;
		map(PADDR(CPU0END), len-n, type);
		return;
	}
	
	/*
	 * Memory between KTZERO and end is the kernel itself
	 * and is already mapped.
	 */
	if(base < PADDR(KTZERO) && base+len > PADDR(KTZERO)){
		map(base, PADDR(KTZERO)-base, type);
		return;
	}
	if(PADDR(KTZERO) < base && base < PADDR(PGROUND((uintptr)end))){
		n = PADDR(PGROUND((uintptr)end));
		if(len <= n)
			return;
		map(PADDR(PGROUND((uintptr)end)), len-n, type);
		return;
	}
	
	/*
	 * Now we have a simple case.
	 */
	// print("map %#P %#P %d\n", base, base+len, type);
	switch(type){
	case MemRAM:
		mapfree(&rmapram, base, len);
		flags = PTEWRITE|PTEVALID;
		break;
	case MemUMB:
		mapfree(&rmapumb, base, len);
		flags = PTEWRITE|PTEUNCACHED|PTEVALID;
		break;
	case MemUPA:
		mapfree(&rmapupa, base, len);
		flags = 0;
		break;
	default:
	case MemReserved:
		flags = 0;
		break;
	}
	
	/*
	 * bottom MemMin is already mapped - just twiddle flags.
	 * (not currently used - see above)
	 */
	if(base < MemMin){
		table = KADDR(PPN(m->pdb[PDX(base)]));
		e = base+len;
		base = PPN(base);
		for(; base<e; base+=BY2PG)
			table[PTX(base)] |= flags;
		return;
	}
	
	/*
	 * Only map from KZERO to 2^32.
	 */
	if(flags){
		maxkpa = (uintptr)-KZERO;
		if(base >= maxkpa)
			return;
		if(len > maxkpa-base)
			len = maxkpa - base;
		pdbmap(m->pdb, base|flags, base+KZERO, len);
	}
}

static int
e820scan(void)
{
	char *s;
	int i;
	uint cont;
	uvlong last, base, len;
	Ureg u;
	Emap *e;

	if((s = getconf("*e820")) != nil){
		for(nemap = 0; nemap < nelem(emap); nemap++){
		ignore:
			if(*s == 0)
				break;
			e = emap + nemap;
			e->type = strtoull(s, &s, 16);
			if(*s != ' ')
				break;
			e->base = strtoull(s, &s, 16);
			if(*s != ' ')
				break;
			e->len = strtoull(s, &s, 16) - e->base;
			if(*s != ' ' && *s != 0 || e->len == 0)
				break;
			if(e->type != Ememory || e->base > (1ull<<8*sizeof(uintmem)) - 1)
				goto ignore;
		}
	}else if((s = getconf("e820")) != nil){
		for(nemap = 0; nemap < nelem(emap); nemap++){
			if(*s == 0)
				break;
			e = emap + nemap;
			e->base = strtoull(s, &s, 16);
			if(*s != ' ')
				break;
			e->len = strtoull(s, &s, 16) - e->base;
			if(*s != ' ' && *s != 0 || e->len == 0)
				break;
			e->type = Ememory;
		}
	}else{
		if(getconf("*norealmode") || getconf("*noe820scan"))
			return -1;
		cont = 0;
		for(i=0; i<nelem(emap); i++){
			memset(&u, 0, sizeof u);
			u.ax = 0xE820;
			u.bx = cont;
			u.cx = 20;
			u.dx = SMAP;
			u.es = (PADDR(RMBUF)>>4)&0xF000;
			u.di = PADDR(RMBUF)&0xFFFF;
			u.trap = 0x15;
			realmode(&u);
			cont = u.bx;
			if((u.flags&Carry) || u.ax != SMAP || u.cx != 20)
				break;
			e = &emap[nemap++];
			*e = *(Emap*)RMBUF;
			if(u.bx == 0)
				break;
		}
	}
	if(nemap == 0)
		return -1;

	qsort(emap, nemap, sizeof emap[0], emapcmp);

	last = 0;
	for(i=0; i<nemap; i++){	
		e = &emap[i];
		print("E820: %.8llux %.8llux ", e->base, e->base+e->len);
		if(e->type < nelem(etypes))
			print("%s\n", etypes[e->type]);
		else
			print("type=%ud\n", e->type);
		if(e->base >= (1ull<<8*sizeof(uintmem)) - 1)
			break;
		base = e->base;
		len = e->len;
		/*
		 * If the map skips addresses, mark them reserved.
		 */
		if(last < e->base)
			map(last, e->base-last, MemReserved);
		last = base+len;
		if(e->type == Ememory)
			map(base, len, MemRAM);
		else
			map(base, len, MemReserved);
	}
	/* might not work for pae */
	if(last < (1LL<<32))
		map(last, (u32int)-last, MemUPA);
	return 0;
}

void
meminit(void)
{
	char *p;
	uint i, maxmem;
	uintmem pa, lost;
	Map *mp;
	Confmem *cm;
	PTE *pte;

	if(p = getconf("*maxmem"))
		maxmem = strtoul(p, nil, 0);
	else
		maxmem = 0;

	/*
	 * Set special attributes for memory between 640KB and 1MB:
	 *   VGA memory is writethrough;
	 *   BIOS ROM's/UMB's are uncached;
	 * then scan for useful memory.
	 */
	for(pa = 0xA0000; pa < 0xC0000; pa += BY2PG){
		pte = mmuwalk(m->pdb, (uintptr)KADDR(pa), 2, 0);
		*pte |= PTEWT;
	}
	for(pa = 0xC0000; pa < 0x100000; pa += BY2PG){
		pte = mmuwalk(m->pdb, (uintptr)KADDR(pa), 2, 0);
		*pte |= PTEUNCACHED;
	}
	mmuflushtlb();

	umbscan();
	lowraminit();
	if(e820scan() < 0)
		ramscan(maxmem);

	/*
	 * Set the conf entries describing banks of allocatable memory.
	 */
	for(i=0; i<nelem(mapram) && i<nelem(conf.mem); i++){
		mp = &rmapram.map[i];
		cm = &conf.mem[i];
		cm->base = mp->addr;
		cm->npage = mp->size/BY2PG;
	}

	lost = 0;
	for(; i<nelem(mapram); i++)
		lost += rmapram.map[i].size;
	if(lost)
		print("meminit - lost %#P bytes\n", lost);

	if(MEMDEBUG)
		memdebug();
}

/*
 * Allocate memory from the upper memory blocks.
 */
uintptr
umbmalloc(uintmem addr, int size, int align)
{
	uintmem a;

	a = mapalloc(&rmapumb, addr, size, align);
	if(a != 0)
		return (uintptr)KADDR(a);
	return 0;
}

void
umbfree(uintptr va, int size)
{
	mapfree(&rmapumb, PADDR(va), size);
}

uintptr
umbrwmalloc(uintmem pa, int size, int align)
{
	uintmem a;
	uchar o[2], *p;

	if(a = mapalloc(&rmapumbrw, pa, size, align))
		return (uintptr)KADDR(a);

	/*
	 * Perhaps the memory wasn't visible before
	 * the interface is initialised, so try again.
	 */
	if((a = umbmalloc(pa, size, align)) == 0)
		return 0;
	p = (uchar*)a;
	o[0] = p[0];
	p[0] = 0xCC;
	o[1] = p[size-1];
	p[size-1] = 0xCC;
	if(p[0] == 0xCC && p[size-1] == 0xCC){
		p[0] = o[0];
		p[size-1] = o[1];
		return a;
	}
	umbfree(a, size);

	return 0;
}

void
umbrwfree(ulong addr, int size)
{
	mapfree(&rmapumbrw, PADDR(addr), size);
}

/*
 * Give out otherwise-unused physical address space
 * for use in configuring devices.  Note that unlike umbmalloc
 * before it, upaalloc does not map the physical address
 * into virtual memory.  Call vmap to do that.
 */
uintmem
upaalloc(int size, int align)
{
	uintmem a;

	a = mapalloc(&rmapupa, 0, size, align);
	if(a == 0){
		print("out of physical address space allocating %d\n", size);
		mapprint(&rmapupa);
	}
	return a;
}

void
upafree(uintmem pa, int size)
{
	mapfree(&rmapupa, pa, size);
}

void
upareserve(uintmem pa, int size)
{
	uintmem a;

	a = mapalloc(&rmapupa, pa, size, 0);
	if(a != pa){
		/*
		 * The E820 map may have already reserved some
		 * of the regions claimed by the pci devices.
		 */
	//	print("upareserve: cannot reserve pa=%#P size=%d\n", pa, size);
		if(a != 0)
			mapfree(&rmapupa, a, size);
	}
}

void
memorysummary(void)
{
	memdebug();
}

