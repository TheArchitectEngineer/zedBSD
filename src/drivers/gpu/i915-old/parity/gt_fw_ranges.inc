/*
 * WS031 Linux-parity — Gen12 register -> forcewake domain map.
 *
 * GENERATED from the 6.8.12 reference (intel_uncore.c, __gen12_fw_ranges,
 * which uncore_forcewake_init() assigns to ADL-P) by
 * scratchpad/gen_fw_ranges.py.  43 ranges; the always-on entries (domain 0)
 * are omitted because "no match" already means always-on.
 */
	/* 0x0 .. 0x1fff always-on */
	{ 0x2000u, 0x26ffu, OSDEP_FW_RENDER },
	{ 0x2700u, 0x27ffu, OSDEP_FW_GT },
	{ 0x2800u, 0x2affu, OSDEP_FW_RENDER },
	{ 0x2b00u, 0x2fffu, OSDEP_FW_GT },
	{ 0x3000u, 0x3fffu, OSDEP_FW_RENDER },
	{ 0x4000u, 0x51ffu, OSDEP_FW_GT },
	{ 0x5200u, 0x7fffu, OSDEP_FW_RENDER },
	{ 0x8000u, 0x813fu, OSDEP_FW_GT },
	{ 0x8140u, 0x815fu, OSDEP_FW_RENDER },
	/* 0x8160 .. 0x81ff always-on */
	{ 0x8200u, 0x82ffu, OSDEP_FW_GT },
	{ 0x8300u, 0x84ffu, OSDEP_FW_RENDER },
	{ 0x8500u, 0x94cfu, OSDEP_FW_GT },
	{ 0x94d0u, 0x955fu, OSDEP_FW_RENDER },
	/* 0x9560 .. 0x97ff always-on */
	{ 0x9800u, 0xafffu, OSDEP_FW_GT },
	{ 0xb000u, 0xb3ffu, OSDEP_FW_RENDER },
	{ 0xb400u, 0xcfffu, OSDEP_FW_GT },
	/* 0xd000 .. 0xd7ff always-on */
	{ 0xd800u, 0xd8ffu, OSDEP_FW_RENDER },
	{ 0xd900u, 0xdbffu, OSDEP_FW_GT },
	{ 0xdc00u, 0xefffu, OSDEP_FW_RENDER },
	{ 0xf000u, 0x147ffu, OSDEP_FW_GT },
	{ 0x14800u, 0x1ffffu, OSDEP_FW_RENDER },
	{ 0x20000u, 0x20fffu, OSDEP_FW_MEDIA_VDBOX0 },
	{ 0x21000u, 0x21fffu, OSDEP_FW_MEDIA_VDBOX2 },
	{ 0x22000u, 0x23fffu, OSDEP_FW_GT },
	/* 0x24000 .. 0x2417f always-on */
	{ 0x24180u, 0x249ffu, OSDEP_FW_GT },
	{ 0x24a00u, 0x251ffu, OSDEP_FW_RENDER },
	{ 0x25200u, 0x255ffu, OSDEP_FW_GT },
	{ 0x25600u, 0x2567fu, OSDEP_FW_MEDIA_VDBOX0 },
	{ 0x25680u, 0x259ffu, OSDEP_FW_MEDIA_VDBOX2 },
	{ 0x25a00u, 0x25a7fu, OSDEP_FW_MEDIA_VDBOX0 },
	{ 0x25a80u, 0x2ffffu, OSDEP_FW_MEDIA_VDBOX2 },
	{ 0x30000u, 0x3ffffu, OSDEP_FW_GT },
	/* 0x40000 .. 0x1bffff always-on */
	{ 0x1c0000u, 0x1c3fffu, OSDEP_FW_MEDIA_VDBOX0 },
	/* 0x1c4000 .. 0x1c7fff always-on */
	{ 0x1c8000u, 0x1cbfffu, OSDEP_FW_MEDIA_VEBOX0 },
	{ 0x1cc000u, 0x1cffffu, OSDEP_FW_MEDIA_VDBOX0 },
	{ 0x1d0000u, 0x1d3fffu, OSDEP_FW_MEDIA_VDBOX2 },
