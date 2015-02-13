/*
 * the counters in MCP memory are pointed to by mcp_gen_header.counters_addr
 * the structure has the following layout:
 * ulong		ncnt;		// each MCP can have a different subset counters
 * ulong		cnt[n];		// the counters value
 * ushort		id[n];		// identification of each counter in the array,
 * 				// using counter id values from the list below
 */   

enum{
	Zint,
	Zhex,
};

typedef struct Zcnt Zcnt;
struct Zcnt {
	char	*name;
	int	id;
	int	type;
};

Zcnt ztab[] = {
	"p0_rx_bad_crc8",		1,	Zint,
	"p0_rx_good_crc8",	2,	Zint,
	"pcie_pad_overflow",	3,	Zint,
	"pcie_bad_tlp",		4,	Zint,
	"pcie_send_timeout",	5,	Zint,
	"pcie_send_nacked",	6,	Zint,
	"pcie_link_error",		7,	Zint,
	"eeprom_id",		8,	Zhex,
	"msix_irq_masked",	9,	Zint,
	"pcie_tx_stopped",	10,	Zint,
	"ext_pio_read",		11,	Zint,
	"partial_ext_write",	12,	Zint,
	"ltssm_lane_resync",	13,	Zint,
	"ltssm_misc",		14,	Zhex,
	"ltssm_rxdetect",		15,	Zhex,
	"pcie_fc_no_dllp",		16,	Zhex,
	"pcie_sw_naks",		17,	Zint,
	"pcie_sw_naks_dpend",	18,	Zint,
	"pcie_sw_naks_gotdata",	19,	Zint,
	"pcie_pretimeout0",	20,	Zint,
	"pcie_pretimeout1",	21,	Zint,
	"p0_unaligned",		22,	Zint,
	"pcie_not_in_l0",		23,	Zint,
	"pcie_lane_invalid",	24,	Zint,
	"pcie_tlp_dup",		25,	Zint,
	"pcie_tlp_dup_pad",	26,	Zint,
	"mean_gate_delay",	27,	Zint,
	"mean_gate_delay_low",	28,	Zint,
	"mean_gate_delay_high",	29,	Zint,
	"pcie_invalid_be",		30,	Zint,
};

static void
readcntr(Ctlr *c)
{
	char *name;
	int i, n, l, cntid, type;
	uchar *p, *id;
	ulong off;
	Fwhdr h;

	off = gbit32(c->ram + Hdroff);
	print("header	%p\n", off);
	if(off == 0 || off&3 || off + sizeof h >= c->ramsz){
		print("bad header off %p\n", off);
		return;
	}
	memmove(&h, c->ram + off, sizeof h);
	l = gbit32(h.len);
	print("hlen	%d\n", l);
	if(l < 152 || l > 256) {
		print("bad hlen %d\n", l);
		return;
	}

	off = gbit32(h.cntaddr);
	print("cntaddr	%p\n", off);
	if(off == 0 || off&3 || off + sizeof h >= c->ramsz){
		print("bad cntaddr off %p\n", off);
		return;
	}
	n = gbit32(p = c->ram + gbit32(h.cntaddr));
	print("ncnt	%d\n", n);
	p += 4;
	if(n > 100)
		print("bad n %d\n", n);
	id = p + 4*n;
	for(i = 0; i < n; i++) {
		cntid = gbit16(id + 2*i) - 1;
		name = "unknown";
		type = Zint;
		if(cntid < nelem(ztab)){
			name = ztab[cntid].name;
			type = ztab[cntid].type;
		}
		if(type == Zhex)
			print("%s\t%ux\n", name, gbit32(p + 4*i));
		else
			print("%s\t%ud\n", name, gbit32(p + 4*i));
	}
}
