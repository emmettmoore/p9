/*
 * Memory mappings.  Life was easier when 2G of memory was enough.
 *
 * The kernel memory starts at KZERO, with the text loaded at KZERO+1M
 * (9load sits under 1M during the load).  The memory from KZERO to the
 * top of memory is mapped 1-1 with physical memory, starting at physical
 * address 0.  All kernel memory and data structures (i.e., the entries stored
 * into conf.mem) must sit in this physical range: if KZERO is at 0xF0000000,
 * then the kernel can only have 256MB of memory for itself.
 * 
 * The 256M below KZERO comprises three parts.  The lowest 4M is the
 * virtual page table, a virtual address representation of the current 
 * page table tree.  The second 4M is used for temporary per-process
 * mappings managed by kmap and kunmap.  The remaining 248M is used
 * for global (shared by all procs and all processors) device memory
 * mappings and managed by vmap and vunmap.  The total amount (256M)
 * could probably be reduced somewhat if desired.  The largest device
 * mapping is that of the video card, and even though modern video cards
 * have embarrassing amounts of memory, the video drivers only use one
 * frame buffer worth (at most 16M).  Each is described in more detail below.
 *
 * The VPT is a 4M frame constructed by inserting the pdb into itself.
 * This short-circuits one level of the page tables, with the result that 
 * the contents of second-level page tables can be accessed at VPT.  
 * We use the VPT to edit the page tables (see mmu) after inserting them
 * into the page directory.  It is a convenient mechanism for mapping what
 * might be otherwise-inaccessible pages.  The idea was borrowed from
 * the Exokernel.
 *
 * The VPT doesn't solve all our problems, because we still need to 
 * prepare page directories before we can install them.  For that, we
 * use tmpmap/tmpunmap, which map a single page at TMPADDR.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

/*
 * Simple segment descriptors with no translation.
 */
#define	DATASEGM(p) 	{ 0xFFFF, SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	EXECSEGM(p) 	{ 0xFFFF, SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	EXEC16SEGM(p) 	{ 0xFFFF, SEGG|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	TSSSEGM(b,p)	{ ((b)<<16)|sizeof(Tss),\
			  ((b)&0xFF000000)|(((b)>>16)&0xFF)|SEGTSS|SEGPL(p)|SEGP }

Segdesc gdt[NGDT] =
{
[NULLSEG]	{ 0, 0},		/* null descriptor */
[KDSEG]		DATASEGM(0),		/* kernel data/stack */
[KESEG]		EXECSEGM(0),		/* kernel code */
[UDSEG]		DATASEGM(3),		/* user data/stack */
[UESEG]		EXECSEGM(3),		/* user code */
[TSSSEG]	TSSSEGM(0,0),		/* tss segment */
[KESEG16]		EXEC16SEGM(0),	/* kernel code 16-bit */
};

static int didmmuinit;
static void stackswitch(uintptr);
static void memglobal(void);
void dumpmmu(PTE*);

#define	vpt ((PTE*)VPT)
#define	VPTX(va)		(((u32int)(va))>>12)
#define	vpd(n) (vpt+VPTX(VPT)+(n)*PTE2PG)
#define	kvpd	vpd(KSEG)
#define	VPDBX(va)	(((u32int)(va))>>21)

#define	MMUDEBUG	0
#define	D(n)	if(MMUDEBUG>=(n))

void checkmmu(uintptr va, uintmem pa);

void
mmuinit0(void)
{
	memmove(m->gdt, gdt, sizeof gdt);
}

/*
 * set up a pat mappings.  the system depends
 * on the first 4 mappings not changing.
 */
enum{
	Patmsr	= 0x277,
};

static uchar pattab[8] = {
	PATWB,
	PATWT,
	PATUCMINUS,
	PATUC, 

	PATWB,
	PATWT,
	PATUCMINUS,
	PATUC,
};

static ulong patflags[8] = {
	0,
					PTEWT,
			PTEPCD,
			PTEPCD |	PTEWT,
	PTEPAT,
	PTEPAT | 			PTEWT,
	PTEPAT |	PTEPCD,
	PTEPAT |	PTEPCD |	PTEWT,
};

static void
setpatreg(int rno, int type)
{
	int i;
	ulong s;
	vlong pat;

	s = splhi();
	rdmsr(Patmsr, &pat);

	pat &= ~(0xffull<<rno*8);
	pat |= (vlong)type<<rno*8;
	wrmsr(Patmsr, pat);
	splx(s);

	if(m->machno == 0)
		print("pat: %.16llux\n", pat);
	for(i = 0; i < 64; i += 8)
		pattab[i>>3] = pat>>i;
}

static void
patinit(void)
{
	setpatreg(7, PATWC);
}

void
mmuinit(void)
{
	uintptr x;
	PTE *p;
	ushort ptr[3];
	int i;

	didmmuinit = 1;

	if(0 && m->machno == 0){
		print("vpt=%#.8ux vpd=%#p kvpd=%p kmap=%#.8ux\n",
			VPT, vpd(0), kvpd, KMAP);
		print("USTKTOP %#.8ux TSTKTOP %#.8ux VMAP=%#.8ux\n", USTKTOP, TSTKTOP, VMAP);
		print("VPTX: KMAP %d VPT %d KZERO %d\n", VPTX(KMAP), VPTX(VPT), VPTX(KZERO));
	}

	memglobal();
	for(i=0; i<NPDPTE; i++)
		if(i != KSEG)
			m->pdb[PDX(VPT)+i] = 0;
	m->pdb[PDX(VPT)+KSEG] = PADDR(m->pdb)|PTEWRITE|PTEVALID;
if(0)dumpmmu(m->pdpt);

	m->tss = malloc(sizeof(Tss));
	if(m->tss == nil)
		panic("mmuinit: no memory");
	memset(m->tss, 0, sizeof(Tss));
	m->tss->iomap = 0xDFFF<<16;

	/*
	 * We used to keep the GDT in the Mach structure, but it
	 * turns out that that slows down access to the rest of the
	 * page.  Since the Mach structure is accessed quite often,
	 * it pays off anywhere from a factor of 1.25 to 2 on real
	 * hardware to separate them (the AMDs are more sensitive
	 * than Intels in this regard).  Under VMware it pays off
	 * a factor of about 10 to 100.
	 */
	memmove(m->gdt, gdt, sizeof gdt);
	x = (uintptr)m->tss;
	m->gdt[TSSSEG].d0 = (x<<16)|sizeof(Tss);
	m->gdt[TSSSEG].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;

	ptr[0] = sizeof(gdt)-1;
	x = (uintptr)m->gdt;
	ptr[1] = x & 0xFFFF;
	ptr[2] = (x>>16) & 0xFFFF;
	lgdt(ptr);

	ptr[0] = sizeof(Segdesc)*256-1;
	x = IDTADDR;
	ptr[1] = x & 0xFFFF;
	ptr[2] = (x>>16) & 0xFFFF;
	lidt(ptr);

	if(m->machno == 0) {
		/* make kernel text unwritable */
		for(x = KTZERO; x < (uintptr)etext; x += BY2PG){
			p = mmuwalk(m->pdb, x, 2, 0);
			if(p == nil)
				panic("mmuinit");
			*p &= ~PTEWRITE;
		}
	}
	stackswitch((uintptr)m + MACHSIZE);
	putcr3(PADDR(m->pdpt));
	ltr(TSSSEL);
	tmpunmap((void*)TMPADDR);	/* clear existing mapping */

	if(MACHP(0)->cpuiddx & Pat)
		patinit();

	if(MMUDEBUG > 1)
		dumpmmu(m->pdpt);
}

/* 
 * On processors that support it, we set the PTEGLOBAL bit in
 * page table and page directory entries that map kernel memory.
 * Doing this tells the processor not to bother flushing them
 * from the TLB when doing the TLB flush associated with a 
 * context switch (write to CR3).  Since kernel memory mappings
 * are never removed, this is safe.  (If we ever remove kernel memory
 * mappings, we can do a full flush by turning off the PGE bit in CR4,
 * writing to CR3, and then turning the PGE bit back on.) 
 *
 * See also mmukmap below.
 * 
 * Processor support for the PTEGLOBAL bit is enabled in devarch.c.
 */
static void
memglobal(void)
{
	int i, j;
	PTE *pde, *pte;

	/* only need to do this once, on bootstrap processor */
	if(m->machno != 0)
		return;

	if(!m->havepge)
		return;

	pde = m->pdb;
	for(i=PDX(KZERO); i<PTE2PG; i++){
		if(pde[i] & PTEVALID){
			pde[i] |= PTEGLOBAL;
			if(!(pde[i] & PTESIZE)){
				pte = KADDR(pde[i]&~(BY2PG-1));
				for(j=0; j<PTE2PG; j++)
					if(pte[j] & PTEVALID)
						pte[j] |= PTEGLOBAL;
			}
		}
	}			
}

/*
 * Flush all the user-space and device-mapping mmu info
 * for this process, because something has been deleted.
 * It will be paged back in on demand.
 */
void
flushmmu(void)
{
	int s;

	s = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(s);
}

/*
 * Flush a single page mapping from the tlb.
 */
static void
flushpg(uintptr va)
{
	if(X86FAMILY(m->cpuidax) >= 4)
		invlpg(va);
	else
		putcr3(getcr3());
}

/*
 * Initialise a new page directory pointer table (PAE).
 * Pdb is KSEG's.
 */
void
pdptinit(PTE *pdpt, PTE *pdb)
{
	int i;

	for(i = 0; i < NPDPTE; i++){
		if(i != KSEG)
			pdpt[i] = 0;
	}
	pdpt[KSEG] = PADDR(pdb) | PTEVALID;
}

/*
 * Allocate a new page to the page directory. 
 * We keep a small cache of pre-initialized
 * page directories in each mach.
 */
static Page*
mmupdballoc(int seg)
{
	int s;
	Page *page;
	PTE *pdb;

	s = splhi();
	m->pdballoc++;
	if(seg != KSEG || m->pdbpool == nil){
		spllo();
		page = newpage(0, 0, 0, PGSHIFT);
		page->va = (uintptr)vpd(seg);
		splhi();
		pdb = tmpmap(page);
		if(seg == KSEG){
			memmove(pdb, m->pdb, BY2PG);
			pdb[PDX(VPT)+seg] = page->pa |PTEWRITE|PTEVALID;
			tmpunmap(pdb);
		}else{
			memset(pdb, 0, BY2PG);
			tmpunmap(pdb);
			pdb = vpd(KSEG);
			pdb[PDX(VPT)+seg] = page->pa |PTEWRITE|PTEVALID;
		}
	}else{
		page = m->pdbpool;
		m->pdbpool = page->next;
		m->pdbcnt--;
	}
	splx(s);
	return page;
}

static void
mmupdbfree(Proc *proc, int seg, Page *p)
{
	if(islo())
		panic("mmupdbfree: islo");
	m->pdbfree++;
	if(seg != KSEG || m->pdbcnt >= 10){
		p->next = proc->mmufree;
		proc->mmufree = p;
	}else{
		p->next = m->pdbpool;
		m->pdbpool = p;
		m->pdbcnt++;
	}
}

/*
 * A user-space memory segment has been deleted, or the
 * process is exiting.  Clear all the pde entries for user-space
 * memory mappings and device mappings.  Any entries that
 * are needed will be paged back in as necessary.
 */
static void
mmuptefree(Proc* proc)
{
	int s;
	PTE *pdb;
	Page **last, *page;

	if(proc->mmuused == nil)
		return;
	pdb = vpd(0);
	s = splhi();
	last = &proc->mmuused;
	for(page = *last; page != nil; page = page->next){
		int seg = page->daddr>>9;
		if(MMUDEBUG || seg > 3 || proc->mmupdb[seg] == nil)
			print("%d: mmuptefree: daddr=%#lux seg=%d %#p\n", m->machno, page->daddr, seg, proc->mmupdb[seg]);
		pdb[page->daddr] = 0;
		last = &page->next;
	}
	splx(s);
	*last = proc->mmufree;
	proc->mmufree = proc->mmuused;
	proc->mmuused = 0;
}

static void
stackswitch(uintptr stack)
{
	Tss *tss;

	tss = m->tss;
	tss->ss0 = KDSEL;
	tss->esp0 = stack;
	tss->ss1 = KDSEL;
	tss->esp1 = stack;
	tss->ss2 = KDSEL;
	tss->esp2 = stack;
}

void
mmuidle(void)
{
	m->pdpt[0] = 0;
	m->pdpt[1] = 0;
	m->pdpt[2] = 0;
	m->pdpt[KSEG] = PADDR(m->pdb) | PTEVALID;
	putcr3(PADDR(m->pdpt));
}

void
mmuswitch(Proc* proc)
{
	PTE *pdb;

	if(proc->mmupdb[KSEG] != nil){
		pdb = tmpmap(proc->mmupdb[KSEG]);
		pdb[PDX(MACHADDR)] = m->pdb[PDX(MACHADDR)];
		tmpunmap(pdb);
		for(int i = 0; i < NPDPTE; i++){
			if(proc->mmupdb[i] != nil)
				m->pdpt[i] = proc->mmupdb[i]->pa | PTEVALID;
			else
				m->pdpt[i] = 0;
		}
	}else
		mmuidle();

	stackswitch((uintptr)(proc->kstack+KSTACK));
	putcr3(PADDR(m->pdpt));

	if(proc->newtlb){
		mmuptefree(proc);
		proc->newtlb = 0;
		//putcr3(getcr3());	/* probably not needed */
	}
}

/*
 * Release any pages allocated for a page directory base or page-tables
 * for this process:
 *   switch to the prototype pdb for this processor (m->pdb);
 *   call mmuptefree() to place all pages used for page-tables (proc->mmuused)
 *   onto the process' free list (proc->mmufree). This has the side-effect of
 *   cleaning any user entries in the pdb (proc->mmupdb);
 *   if there's a pdb put it in the cache of pre-initialised pdb's
 *   for this processor (m->pdbpool) or on the process' free list;
 *   finally, place any pages freed back into the free pool (palloc).
 * This routine is only called from schedinit() with palloc locked.
 */
void
mmurelease(Proc* proc)
{
	Page *page, *next;
	PTE *pdb;

	if(islo())
		panic("mmurelease: islo");
	stackswitch((uintptr)m + MACHSIZE);
	if(proc->mmupdb[KSEG] != nil && PPN(m->pdpt[KSEG]) != proc->mmupdb[KSEG]->pa){
		D(1)print("%d: *** pdb difference\n", m->machno);
		mmuswitch(proc);
	}
	if(proc->kmaptable != nil){
		if(proc->mmupdb[KSEG] == nil)
			panic("mmurelease: no mmupdb");
		if(--proc->kmaptable->ref)
			panic("mmurelease: kmap ref %d", proc->kmaptable->ref);
		if(proc->nkmap)
			panic("mmurelease: nkmap %d", proc->nkmap);
		/*
		 * remove kmaptable from pdb before putting pdb up for reuse.
		 */
		pdb = vpd(KSEG);
		if(PPN(pdb[PDX(KMAP)]) != proc->kmaptable->pa)
			panic("mmurelease: bad kmap pde %#P kmap %#P",
				pdb[PDX(KMAP)], proc->kmaptable->pa);
		pdb[PDX(KMAP)] = 0;
		/*
		 * move kmaptable to free list.
		 */
		pagechainhead(proc->kmaptable);
		proc->kmaptable = nil;
	}
	if(proc->mmuused != nil)
		mmuptefree(proc);

	mmuidle();

	for(int i = 0; i < nelem(proc->mmupdb); i++){
		if(proc->mmupdb[i] != nil){
			mmupdbfree(proc, i, proc->mmupdb[i]);
			proc->mmupdb[i] = nil;
		}
	}
	for(page = proc->mmufree; page != nil; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %d", page->ref);
		pagechainhead(page);
	}
	if(proc->mmufree != nil)
		pagewake();
	proc->mmufree = nil;
	proc->newtlb = 0;
}

/*
 * Allocate and install pdb for the current process.
 */
static void
upallocpdb(int seg)
{
	int s;
	PTE *pdb;
	Page *page;
	
	if(up->mmupdb[seg] != nil)
		return;
	page = mmupdballoc(seg);
	s = splhi();
	if(up->mmupdb[seg] != nil){
		/*
		 * Perhaps we got an interrupt while
		 * mmupdballoc was sleeping and that
		 * interrupt allocated an mmupdb?
		 * Seems unlikely.
		 */
		mmupdbfree(up, seg, page);
		splx(s);
		return;
	}
	if(seg == KSEG){
		pdb = tmpmap(page);
		pdb[PDX(MACHADDR)] = m->pdb[PDX(MACHADDR)];
		tmpunmap(pdb);
	}
	up->mmupdb[seg] = page;
	m->pdpt[seg] = page->pa | PTEVALID;
	putcr3(PADDR(m->pdpt));
	splx(s);
}

/*
 * Special PAT flags for certain memory ranges.
 */
typedef struct Memflags Memflags;
struct Memflags
{
	uintmem	pa;
	usize	len;
	uint	flags;
};
static Memflags mftab[128];
static int nmftab;

static uint
memflags(uintmem pa)
{
	Memflags *tab, *m;
	int n, i;
	
	tab = mftab;
	n = nmftab;
	while(n > 0){
		i = n/2;
		m = tab+i;
		if(m->pa < pa){
			if(pa - m->pa < m->len)
				return m->flags;
			tab += i+1;
			n -= i+1;
		}else
			n = i;
	}
	return 0;			
}

static uint
memflagspde(uint flag)
{
	if(flag & PTESIZE)
		flag |= PTEDPAT;
	return flag;
}

void
addmemflags(uintmem pa, usize len, uint flags)
{
	Memflags *m;
	
	if(nmftab >= nelem(mftab))
		panic("addmemflags");
	
	for(m=mftab+nmftab; m > mftab && (m-1)->pa > pa; m--)
		*m = *(m-1);
	m->pa = pa;
	m->len = len;
	m->flags = flags;
	nmftab++;
}

/*
 * Update the mmu in response to a user fault.  pa may have PTEWRITE set.
 */
void
putmmu(uintptr va, uintmem pa, Page *upage)
{
	int s, seg;
	Page *page;
	PTE old, *pdir, *pdp;

	if(up->mmupdb[KSEG] == nil)
		upallocpdb(KSEG);
	seg = PDPTX(va);
D(1)print("%d: put %#p %.8#P seg %d pdb %#p\n", m->machno, va, pa, seg, up->mmupdb[seg]);
	if(up->mmupdb[seg] == nil)
		upallocpdb(seg);
	pdir = vpd(seg);

	pdp = &pdir[PDX(va)];
	if(upage->lgsize == XPGSHIFT){	/* big fella */
		old = *pdp;
		if(old & PTEVALID){
			if((old & PTESIZE) == 0){
				/*
				 * A big page is replacing a page table page:
				 * should not happen often, if ever; instead of
				 * finding them on mmuused, just release them all,
				 * and flush the TLB in any case.
				 */
				mmuptefree(up);
				putcr3(getcr3());
			}else
				flushpg(va);
		}
		*pdp = pa|memflagspde(memflags(pa))|PTEUSER|PTESIZE|PTEVALID;
D(2)print("%d: big put: old=%#P vpt=%#p VPTX=%#ux pa=%#P\n", m->machno, old, vpt, VPTX(va), pa);
		return;
	}
	/*
	 * We should be able to get through this with interrupts
	 * turned on (if we get interrupted we'll just pick up 
	 * where we left off) but we get many faults accessing
	 * vpt[] near the end of this function, and they always happen
	 * after the process has been switched out and then 
	 * switched back, usually many times in a row (perhaps
	 * it cannot switch back successfully for some reason).
	 * 
	 * In any event, I'm tired of searching for this bug.  
	 * Turn off interrupts during putmmu even though
	 * we shouldn't need to.		- rsc
	 */

	s = splhi();
	if(!(*pdp&PTEVALID)){
		if(up->mmufree == 0){
			spllo();
			page = newpage(0, 0, 0, PGSHIFT);
			splhi();
		}
		else{
			page = up->mmufree;
			up->mmufree = page->next;
		}
		pdir[PDX(va)] = PPN(page->pa)|PTEUSER|PTEWRITE|PTEVALID;
		/* page is now mapped into the VPT - clear it */
		page->va = VPT+VPDBX(va)*BY2PG;
		memset((void*)page->va, 0, BY2PG);
		page->daddr = VPDBX(va);
D(1)print("%d: pdir %#p pdx %ld %#P va %#p\n", m->machno, pdir, page->daddr, pdir[PDX(va)], page->va);
		page->next = up->mmuused;
		up->mmuused = page;
	}
	old = vpt[VPTX(va)];
D(2)print("%d: put: old=%#P vpt=%#p VPTX=%#ux pa=%#P\n", m->machno, old, vpt, VPTX(va), pa);
	vpt[VPTX(va)] = pa|memflags(pa)|PTEUSER|PTEVALID;
checkmmu(va, pa);
//dumpmmu(m->pdpt);
	if(old&PTEVALID)
		flushpg(va);
//	if(getcr3() != up->mmupdb->pa)
	if(getcr3() != PADDR(m->pdpt))
		print("bad cr3 %#ux %#P\n", getcr3(), PADDR(m->pdpt));
	splx(s);
}

/*
 * Double-check the user MMU.
 * Error checking only.
 */
void
checkmmu(uintptr va, uintmem pa)
{
	int seg;
	PTE *pdir;

	seg = PDPTX(va);
	if(up->mmupdb[KSEG] == nil || up->mmupdb[seg] == nil){
		print("%d: %ld: seg %d %#p\n", m->machno, up->pid, seg, up->mmupdb[seg]);
		return;
	}
	if(!(m->pdpt[seg]&PTEVALID) || PPN(m->pdpt[seg]) != PPN(up->mmupdb[seg]->pa))
		print("%d: %ld: seg %d ptpd %#P proc %#P\n",
			m->machno, up->pid, seg, m->pdpt[seg], up->mmupdb[seg]->pa);
//	print("%ld: pdb0 %#P pdb3 %#P updb0 %#P updp1 %#P\n",
//		up->pid, m->pdpt[0], m->pdpt[KSEG], up->mmupdb[0]->pa,
//		up->mmupdb[KSEG]->pa);
	pdir = vpd(seg);
//	print("%ld: pdir %#p %#P vpt %p + %#ux -> %#P\n",
//		up->pid, pdir, pdir[PDX(va)], vpt, VPTX(va), vpt[VPTX(va)]);
	if(!(pdir[PDX(va)]&PTEVALID) || !(vpt[VPTX(va)]&PTEVALID))
		return;
	if(PPN(vpt[VPTX(va)]) != PPN(pa)){
		print("%d: %ld %s: va=%#p pa=%#P pte=%#P\n",
			m->machno, up->pid, up->text,
			va, pa, vpt[VPTX(va)]);
		dumpmmu(m->pdpt);
		//dumpmmu(up->mmupdb);
	}
	else if(0)
		print("%ld %#p ok\n", up->pid, va);
}

/*
 * Walk the page-table pointed to by pdb and return a pointer
 * to the entry for virtual address va at the requested level.
 * If the entry is invalid and create isn't requested then bail
 * out early. Otherwise, for the 2nd level walk, allocate a new
 * page-table page and register it in the 1st level.  This is used
 * only to edit kernel mappings, which use pages from kernel memory,
 * so it's okay to use KADDR to look at the tables.
 * Pdb is always the KSEG portion.
 */
PTE*
mmuwalk(PTE* pdb, uintptr va, int level, int create)
{
	PTE *table;
	void *map;

	table = &pdb[PDX(va)];
	if(!(*table & PTEVALID) && create == 0)
		return 0;

	switch(level){

	default:
		return 0;

	case 1:
		return table;

	case 2:
		if(*table & PTESIZE)
			panic("mmuwalk2: va %#p entry %#P", va, *table);
		if(!(*table & PTEVALID)){
			/*
			 * Have to call low-level allocator from
			 * memory.c if we haven't set up the xalloc
			 * tables yet.
			 */
			if(didmmuinit)
				map = xspanalloc(BY2PG, BY2PG, 0);
			else
				map = rampage();
			if(map == nil)
				panic("mmuwalk xspanalloc failed");
			*table = PADDR(map)|PTEWRITE|PTEVALID;
		}
		table = KADDR(PPN(*table));
		return &table[PTX(va)];
	}
}

/*
 * Device mappings are shared by all procs and processors and
 * live in the virtual range VMAP to VMAP+VMAPSIZE.  The master
 * copy of the mappings is stored in mach0->pdb, and they are
 * paged in from there as necessary by vmapsync during faults.
 */

static Lock vmaplock;

static int findhole(PTE *a, int n, int count);
static uintptr vmapalloc(usize size);
static void pdbunmap(PTE*, uintptr, int);

/*
 * Add a device mapping to the vmap range.
 * Remember the flags so putmmu can maintain
 * consistent mappings.
 */
static void*
vmapflags(uintmem pa, usize size, uint flags)
{
	int osize;
	uintptr o, va;
	
	if(0 && MACHP(0)->havepge)
		flags |= PTEGLOBAL;

	/*
	 * might be asking for less than a page.
	 */
	osize = size;
	o = pa & (BY2PG-1);
	pa -= o;
	size += o;

	size = ROUND(size, BY2PG);
	if(pa == 0){
		print("vmap pa=0 pc=%#p\n", getcallerpc(&pa));
		return nil;
	}
	ilock(&vmaplock);
	if((va = vmapalloc(size)) == 0
	|| pdbmap(MACHP(0)->pdb, pa|flags, va, size) < 0){
		iunlock(&vmaplock);
		return 0;
	}
	iunlock(&vmaplock);
	/* avoid trap on local processor
	for(i=0; i<size; i+=BY2XPG)
		vmapsync(va+i);
	*/
	USED(osize);
//	print("%d:  vmap %#P %d => %#p\n", m->machno, pa+o, osize, va+o);
	addmemflags(pa, size, flags);
	return (void*)(va + o);
}

void*
vmap(uintmem pa, usize size)
{
	return vmapflags(pa, size, PTEUNCACHED|PTEWRITE);
}

void*
vmappat(uintmem pa, usize size, int pattype)
{
	int i;

	if(MACHP(0)->cpuiddx & Pat)
		for(i = 0; i < nelem(pattab); i++)
			if(pattab[i] == pattype)
				return vmapflags(pa, size, patflags[i]|PTEWRITE);
	return vmap(pa, size);
}

static int
findhole(PTE *a, int n, int count)
{
	int have, i;
	
	have = 0;
	for(i=0; i<n; i++){
		if(a[i] == 0)
			have++;
		else
			have = 0;
		if(have >= count)
			return i+1 - have;
	}
	return -1;
}

/*
 * Look for free space in the vmap.
 */
static uintptr
vmapalloc(usize size)
{
	int i, n, o;
	PTE *vpdb;
	int vpdbsize;
	
	vpdb = &MACHP(0)->pdb[PDX(VMAP)];
	vpdbsize = VMAPSIZE/BY2XPG;

	if(size >= BY2XPG){
		n = (size+BY2XPG-1) / BY2XPG;
		if((o = findhole(vpdb, vpdbsize, n)) != -1)
			return VMAP + o*BY2XPG;
		return 0;
	}
	n = (size+BY2PG-1) / BY2PG;
	for(i=0; i<vpdbsize; i++)
		if((vpdb[i]&PTEVALID) && !(vpdb[i]&PTESIZE))
			if((o = findhole(KADDR(PPN(vpdb[i])), BY2PG/sizeof(PTE), n)) != -1)
				return VMAP + i*BY2XPG + o*BY2PG;
	if((o = findhole(vpdb, vpdbsize, 1)) != -1)
		return VMAP + o*BY2XPG;
		
	/*
	 * could span page directory entries, but not worth the trouble.
	 * not going to be very much contention.
	 */
	return 0;
}

/*
 * Remove a device mapping from the vmap range.
 * Since pdbunmap does not remove page tables, just entries,
 * the call need not be interlocked with vmap.
 */
void
vunmap(void *v, usize size)
{
	int i;
	uintptr va, o;
	Mach *nm;
	Proc *p;
	
	/*
	 * might not be aligned
	 */
	va = (uintptr)v;
	o = va&(BY2PG-1);
	va -= o;
	size += o;
	size = ROUND(size, BY2PG);
	
	if(va < VMAP || va+size > VMAP+VMAPSIZE)
		panic("vunmap va=%#p size=%#lux pc=%#p",
			va, size, getcallerpc(&v));

	pdbunmap(MACHP(0)->pdb, va, size);
	
	/*
	 * Flush mapping from all the tlbs and copied pdbs.
	 * This can be (and is) slow, since it is called only rarely.
	 * It is possible for vunmap to be called with up == nil,
	 * e.g. from the reset/init driver routines during system
	 * boot. In that case it suffices to flush the MACH(0) TLB
	 * and return.
	 */
	if(!active.thunderbirdsarego){
		putcr3(getcr3());
		return;
	}
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		if(p->state == Dead)
			continue;
		if(p != up)
			p->newtlb = 1;
	}
	for(i=0; i<conf.nmach; i++){
		nm = MACHP(i);
		if(nm != m)
			nm->flushmmu = 1;
	}
	flushmmu();
	for(i=0; i<conf.nmach; i++){
		nm = MACHP(i);
		if(nm != m)
			while((active.machs&(1<<nm->machno)) && nm->flushmmu)
				;
	}
}

/*
 * Add kernel mappings for va -> pa for a section of size bytes.
 * Pdb is always the KSEG portion.
 */
int
pdbmap(PTE *pdb, uintmem pa, uintptr va, int size)
{
	int pse;
	ulong pgsz;
	PTE *pte, *table;
	ulong flag, off;
	
	flag = pa&0xFFF;
	pa &= ~0xFFF;

	if((MACHP(0)->cpuiddx & Pse) && (getcr4() & 0x10))
		pse = 1;
	else
		pse = 0;

	for(off=0; off<size; off+=pgsz){
		table = &pdb[PDX(va+off)];
		if((*table&PTEVALID) && (*table&PTESIZE))
			panic("vmap: va=%#p pa=%#P pde=%#P",
				va+off, pa+off, *table);

		/*
		 * Check if it can be mapped using a big page:
		 * va, pa aligned and size >= big page size and processor can do it.
		 */
		if(pse && (pa+off)%BY2XPG == 0 && (va+off)%BY2XPG == 0 && (size-off) >= BY2XPG){
			*table = (pa+off)|memflagspde(flag)|PTESIZE|PTEVALID;
			pgsz = BY2XPG;
		}else{
			pte = mmuwalk(pdb, va+off, 2, 1);
			if(*pte&PTEVALID)
				panic("vmap: va=%#p pa=%#P pte=%#P",
					va+off, pa+off, *pte);
			*pte = (pa+off)|flag|PTEVALID;
			pgsz = BY2PG;
		}
	}
	return 0;
}

/*
 * Remove mappings.  Must already exist, for sanity.
 * Only used for kernel mappings, so okay to use KADDR.
 */
static void
pdbunmap(PTE *pdb, uintptr va, int size)
{
	uintptr vae;
	PTE *table;
	
	vae = va+size;
	while(va < vae){
		table = &pdb[PDX(va)];
		if(!(*table & PTEVALID)){
			panic("vunmap: not mapped pde");
			/* 
			va = (va+BY2XPG) & ~(BY2XPG-1);
			continue;
			*/
		}
		if(*table & PTESIZE){
			if(va & BY2XPG-1)
				panic("vunmap: misaligned: %#p", va);
			*table = 0;
			va += BY2XPG;
			continue;
		}
		table = KADDR(PPN(*table));
		if(!(table[PTX(va)] & PTEVALID))
			panic("vunmap: not mapped");
		table[PTX(va)] = 0;
		va += BY2PG;
	}
}

/*
 * Handle a fault by bringing vmap up to date.
 * Only copy pdb entries and they never go away,
 * so no locking needed.
 */
int
vmapsync(uintptr va)
{
	PTE entry, *table;

	if(va < VMAP || va >= VMAP+VMAPSIZE)
		return 0;
D(1)print("%d: vmapsync %#p\n", m->machno, va);

	entry = MACHP(0)->pdb[PDX(va)];
	if(!(entry&PTEVALID))
		return 0;
	if(!(entry&PTESIZE)){
		/* make sure entry will help the fault */
		table = KADDR(PPN(entry));
		if(!(table[PTX(va)]&PTEVALID))
			return 0;
	}
	kvpd[PDX(va)] = entry;
	/*
	 * TLB doesn't cache negative results, so no flush needed.
	 */
	return 1;
}


/*
 * KMap is used to map individual pages into virtual memory.
 * It is rare to have more than a few KMaps at a time (in the 
 * absence of interrupts, only two at a time are ever used,
 * but interrupts can stack).  The mappings are local to a process,
 * so we can use the same range of virtual address space for
 * all processes without any coordination.
 */
#define kpt (vpt+VPTX(KMAP))
#define NKPT (KMAPSIZE/BY2PG)

KMap*
kmap(Page *page)
{
	int i, o, s;
	PTE *pdir, *pte;

	if(up == nil)
		panic("kmap: up=0 pc=%#p", getcallerpc(&page));
	if(up->mmupdb[KSEG] == nil)
		upallocpdb(KSEG);
	if(up->nkmap < 0)
		panic("kmap %lud %s: nkmap=%d", up->pid, up->text, up->nkmap);
	pdir = vpd(KSEG);

	if(page->lgsize > PGSHIFT){	/* big kmap */
		s = splhi();
		up->nkmap++;
		o = up->lastkxmap+1;
		pte = &pdir[PDX(KXMAP)];
		for(i=0; i<NKXMAP; i++){
			if(pte[(i+o)%NKXMAP] == 0){
				o = (i+o)%NKXMAP;
				pte[o] = page->pa|PTESIZE|PTEWRITE|PTEVALID;
				up->lastkxmap = o;
				splx(s);
D(1)print("%d: kmap big entry %d %#P\n", m->machno, o, page->pa);
				return (KMap*)(KXMAP+o*BY2XPG);
			}
		}
		panic("out of kxmap");
		return nil;
	}
	
	/*
	 * Splhi shouldn't be necessary here, but paranoia reigns.
	 * See comment in putmmu above.
	 */
	s = splhi();
	up->nkmap++;
	if(!(pdir[PDX(KMAP)]&PTEVALID)){
		/* allocate page table page */
		if(KMAPSIZE > BY2XPG)
			panic("bad kmapsize");
		if(up->kmaptable != nil)
			panic("kmaptable");
		spllo();
		up->kmaptable = newpage(0, 0, 0, PGSHIFT);
		up->kmaptable->va = (uintptr)kpt;
		splhi();
		pdir[PDX(KMAP)] = up->kmaptable->pa|PTEWRITE|PTEVALID;
		flushpg((uintptr)kpt);
		memset(kpt, 0, BY2PG);
		kpt[0] = page->pa|PTEWRITE|PTEVALID;
		up->lastkmap = 0;
		splx(s);
D(1)print("%d: kmap kpt %#p kmapdir %#P pdir %#p PDX %ld -> %#P\n", m->machno, kpt, up->kmaptable->pa, pdir, PDX(KMAP), page->pa);
		return (KMap*)KMAP;
	}
	if(up->kmaptable == nil)
		panic("no kmaptable");
	o = up->lastkmap+1;
	for(i=0; i<NKPT; i++){
		if(kpt[(i+o)%NKPT] == 0){
			o = (i+o)%NKPT;
			kpt[o] = page->pa|PTEWRITE|PTEVALID;
			up->lastkmap = o;
			splx(s);
D(1)print("%d: kmap' entry %d %#P\n", m->machno, o, page->pa);
			return (KMap*)(KMAP+o*BY2PG);
		}
	}
	panic("out of kmap");
	return nil;
}

void
kunmap(KMap *k)
{
	uintptr va;
	PTE *pte;

	if(up->mmupdb[KSEG] == nil)
		panic("kunmap: no kseg");
	va = (uintptr)k;
	if(va >= KMAP && va < KMAP+KMAPSIZE){
		if(!(kvpd[PDX(KMAP)]&PTEVALID))
			panic("kunmap: no kmaps");
		pte = &vpt[VPTX(va)];
	}else if(va >= KXMAP && va < KXMAP+KXMAPSIZE){	/* big page */
		pte = &kvpd[PDX(va)];
	}else{
		panic("kunmap: bad address %#p pc=%#p", va, getcallerpc(&k));
		SET(pte);	/* not reached */
	}
	if(!(*pte&PTEVALID))
		panic("kunmap: not mapped %#p pc=%#p", va, getcallerpc(&k));
	*pte = 0;
	up->nkmap--;
	if(up->nkmap < 0)
		panic("kunmap %lud %s: nkmap=%d", up->pid, up->text, up->nkmap);
	flushpg(va);
}

/*
 * Temporary one-page mapping used to edit page directories.
 */

void*
tmpmap(Page *p)
{
	PTE *entry;
	
	if(islo())
		panic("tmpaddr: islo");

	/* mapped in physical memory window? */
	if(p->pa < (uintptr)-KZERO)
		return KADDR(p->pa);

	/*
	 * PDX(TMPADDR) == PDX(MACHADDR), so this
	 * entry is private to the processor and shared 
	 * between up->mmupdb (if any) and m->pdb.
	 */
	entry = &vpt[VPTX(TMPADDR)];
	if(*entry & PTEVALID)
		panic("tmpmap: already mapped entry=%#P pa=%#P from %#p",
			*entry, p->pa, getcallerpc(&p));
	*entry = p->pa|PTEWRITE|PTEVALID;
	/* was previously invalid: no need for flushpg */
	return (void*)TMPADDR;
}

void
tmpunmap(void *v)
{
	PTE *entry;

	if(islo())
		panic("tmpaddr: islo");
	if(v != (void*)TMPADDR){
		if((uintptr)v >= KZERO)
			return;
		panic("tmpunmap: bad address");
	}
	entry = &vpt[VPTX(TMPADDR)];
	if(!(*entry&PTEVALID) && *entry != 0)
		panic("tmpmap: not mapped entry=%#P", *entry);
	*entry = 0;
	flushpg(TMPADDR);
}

/*
 * These could go back to being macros once the kernel is debugged,
 * but the extra checking is nice to have.
 */
void*
kaddr(uintmem pa)
{
	if(sizeof(uintmem) > 4){
		if(pa & ~(uintmem)0xFFFFFFFF)
			panic("kaddr: pa=%#P", pa);
	}
	if(pa > (uintptr)-KZERO)
		panic("kaddr: pa=%#P", pa);
	return (void*)(pa+KZERO);
}

uintmem
paddr(void *v)
{
	uintptr va;
	
	va = (uintptr)v;
	if(va < KZERO)
		panic("paddr: va=%#p pc=%#p", va, getcallerpc(&v));
	return va-KZERO;
}

/*
 * More debugging.
 */
void
countpagerefs(ulong *ref, int print)
{
	int i, n;
	Mach *mm;
	Page *pg;
	Proc *p;
	
	n = 0;
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		for(int j=0; j<NPDPTE; j++){
			if(p->mmupdb[j]){
				if(print){
					if(ref[pagenumber(p->mmupdb[j])])
						iprint("page %#P is proc %d (pid %lud) pdb\n",
							p->mmupdb[j]->pa, i, p->pid);
					continue;
				}
				if(ref[pagenumber(p->mmupdb[j])]++ == 0)
					n++;
				else
					iprint("page %#P is proc %d (pid %lud) pdb but has other refs!\n",
						p->mmupdb[j]->pa, i, p->pid);
			}
		}
		if(p->kmaptable){
			if(print){
				if(ref[pagenumber(p->kmaptable)])
					iprint("page %#P is proc %d (pid %lud) kmaptable\n",
						p->kmaptable->pa, i, p->pid);
				continue;
			}
			if(ref[pagenumber(p->kmaptable)]++ == 0)
				n++;
			else
				iprint("page %#P is proc %d (pid %lud) kmaptable but has other refs!\n",
					p->kmaptable->pa, i, p->pid);
		}
		for(pg=p->mmuused; pg; pg=pg->next){
			if(print){
				if(ref[pagenumber(pg)])
					iprint("page %#P is on proc %d (pid %lud) mmuused\n",
						pg->pa, i, p->pid);
				continue;
			}
			if(ref[pagenumber(pg)]++ == 0)
				n++;
			else
				iprint("page %#P is on proc %d (pid %lud) mmuused but has other refs!\n",
					pg->pa, i, p->pid);
		}
		for(pg=p->mmufree; pg; pg=pg->next){
			if(print){
				if(ref[pagenumber(pg)])
					iprint("page %#P is on proc %d (pid %lud) mmufree\n",
						pg->pa, i, p->pid);
				continue;
			}
			if(ref[pagenumber(pg)]++ == 0)
				n++;
			else
				iprint("page %#P is on proc %d (pid %lud) mmufree but has other refs!\n",
					pg->pa, i, p->pid);
		}
	}
	if(!print)
		iprint("%d pages in proc mmu\n", n);
	n = 0;
	for(i=0; i<conf.nmach; i++){
		mm = MACHP(i);
		for(pg=mm->pdbpool; pg; pg=pg->next){
			if(print){
				if(ref[pagenumber(pg)])
					iprint("page %#P is in cpu%d pdbpool\n",
						pg->pa, i);
				continue;
			}
			if(ref[pagenumber(pg)]++ == 0)
				n++;
			else
				iprint("page %#P is in cpu%d pdbpool but has other refs!\n",
					pg->pa, i);
		}
	}
	if(!print){
		iprint("%d pages in mach pdbpools\n", n);
		for(i=0; i<conf.nmach; i++)
			iprint("cpu%d: %d pdballoc, %d pdbfree\n",
				i, MACHP(i)->pdballoc, MACHP(i)->pdbfree);
	}
}

void
checkfault(ulong, ulong)
{
}

/*
 * Return the number of bytes that can be accessed via KADDR(pa).
 * If pa is not a valid argument to KADDR, return 0.
 */
usize
cankaddr(uintmem pa)
{
	if(sizeof(uintmem) > 4){
		if(pa & ~(uintmem)0xFFFFFFFF)
			return 0;
	}
	if(pa >= (uintptr)-KZERO)
		return 0;
	return (uintptr)-KZERO - pa;
}

void
dumpmmu(PTE *pdpt)
{
	int i, j, k;
	PTE *pdb, *pte, entry;
	uintptr va;

	for(i=0; i<NPDPTE; i++){
		print("cpu %d pdpte %d %#P:\n", m->machno, i, pdpt[i]);
		if((pdpt[i] & PTEVALID) == 0)
			continue;
		va = i<<30;
		pdb = vpd(i);
		print("\tpdb%d %#P %#p va=%#p\n", i, PPN(pdpt[i]), pdb, va);
		for(j=0; j<PTE2PG; j++){
			va = (i<<30) | (j<<21);
			entry = pdb[j];
			if(entry & PTEVALID && (i != KSEG || (entry&PTESIZE) == 0)){
				print("\t\t%4ux %#P %#p va=%#p\n", j, entry, &pdb[j], va);
				if(entry & PTESIZE)
					continue;
				if(PPN(entry) > (uintptr)-KZERO){
					print("\t\t\tppn %#P (> KZERO)\n", PPN(entry));
					continue;
				}
				pte = KADDR((u32int)PPN(entry));
				for(k=0; k<PTE2PG; k++){
					entry = pte[k];
					if(entry & PTEVALID)
						print("\t\t\t%4ux %#P %#p va=%#p\n", k, entry, &pte[k], va | (k<<12));
				}
			}
		}
	}
}
