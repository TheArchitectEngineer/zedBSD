#!/usr/bin/env python3
"""WS031 E-120 round 54: N0 -- the native precheck (read-only, before the first display write) and the OpRegion VBT
locator (reference intel_opregion_setup(): RVDA for 2.0 physical / 2.1+ relative, else mailbox #4).
usage: round54.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (s.count(old), old[:100])
    return s.replace(old, new)

open(root + P + "opregion_vbt.h", "w").write(r"""/*
 * WS031 Linux-parity -- where the ACPI OpRegion keeps the VBT (intel_opregion_setup(), v6.8.12).  zedBSD project code;
 * the offsets and the decision order are the reference's (struct opregion_header / struct opregion_asle,
 * OPREGION_*_OFFSET, MBOX_*).  Pure: it reads a copy of the 8 KiB OpRegion and says where the VBT is; the caller maps
 * and validates it (intel_bios_is_valid_vbt).  intel_load_vbt_firmware() and the DMI quirk list are not consulted here.
 */
#ifndef PARITY_OPREGION_VBT_H
#define PARITY_OPREGION_VBT_H

#include <stdint.h>
#include <stddef.h>

#define PARITY_OPREGION_SIZE        0x2000u
#define PARITY_OPREGION_ASLE_OFFSET 0x300u
#define PARITY_OPREGION_VBT_OFFSET  0x400u
#define PARITY_OPREGION_ASLE_EXT    0x1c00u

enum parity_opregion_vbt_src { PARITY_OPVBT_NONE = 0, PARITY_OPVBT_RVDA, PARITY_OPVBT_MAILBOX4 };

struct parity_opregion_info {
	int signature_ok;
	uint32_t size_kib, mboxes;
	uint8_t major, minor, revision;
	uint64_t rvda;                  /* as stored */
	uint32_t rvds;
	int rvda_relative;              /* 2.1+: an offset from the OpRegion base */
	int rvda_inside;                /* 2.1+ and rvda < OPREGION_SIZE: the reference WARNs */
	/* the candidate the reference tries first: RVDA (physical address), else mailbox #4 (offset in the OpRegion) */
	int src;
	uint64_t vbt_phys;              /* RVDA */
	uint32_t vbt_offset, vbt_max;   /* mailbox #4: offset and the size the reference allows */
};

/* 0, or -1 when the buffer is too short / the signature is wrong (nothing else is filled then) */
int parity_opregion_locate_vbt(const uint8_t *op, size_t len, uint64_t asls, struct parity_opregion_info *out);

#endif /* PARITY_OPREGION_VBT_H */
""")
open(root + P + "opregion_vbt.c", "w").write(r"""/*
 * WS031 Linux-parity -- see opregion_vbt.h.  zedBSD project code.
 */
#include "opregion_vbt.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

int
parity_opregion_locate_vbt(const uint8_t *op, size_t len, uint64_t asls, struct parity_opregion_info *out)
{
	static const char sig[16] = { 'I','n','t','e','l','G','r','a','p','h','i','c','s','M','e','m' };
	const uint8_t *asle;
	unsigned i;

	if (op == 0 || out == 0 || len < PARITY_OPREGION_SIZE)
		return -1;
	for (i = 0u; i < sizeof(*out); i++)
		((uint8_t *)out)[i] = 0u;
	for (i = 0u; i < 16u; i++)
		if (op[i] != (uint8_t)sig[i])
			return -1;
	out->signature_ok = 1;
	out->size_kib = le32(op + 16);
	out->revision = op[21];          /* struct { u8 rsvd, revision, minor, major } over at 0x14 */
	out->minor = op[22];
	out->major = op[23];
	out->mboxes = le32(op + 0x58);
	asle = op + PARITY_OPREGION_ASLE_OFFSET;
	out->rvda = le64(asle + 186);    /* struct opregion_asle: rvda after ... fdss(u64) fdsp stat */
	out->rvds = le32(asle + 194);
	/* opregion->asle exists when MBOX_ASLE (bit 2) is set */
	if (out->major >= 2u && (out->mboxes & 0x4u) != 0u && out->rvda != 0u && out->rvds != 0u) {
		out->rvda_relative = out->major > 2u || out->minor >= 1u;
		out->rvda_inside = out->rvda_relative && out->rvda < PARITY_OPREGION_SIZE;
		out->src = PARITY_OPVBT_RVDA;
		out->vbt_phys = out->rvda_relative ? asls + out->rvda : out->rvda;
		return 0;
	}
	/* mailbox #4: up to the ASLE ext mailbox when that one is in use, else to the end of the OpRegion */
	out->src = PARITY_OPVBT_MAILBOX4;
	out->vbt_offset = PARITY_OPREGION_VBT_OFFSET;
	out->vbt_max = ((out->mboxes & 0x10u) != 0u ? PARITY_OPREGION_ASLE_EXT : PARITY_OPREGION_SIZE) - PARITY_OPREGION_VBT_OFFSET;
	return 0;
}
""")

open(root + P + "native_precheck.h", "w").write(r"""/*
 * WS031 Linux-parity -- N0: what the firmware left, recorded before the driver's first display write, and whether the
 * prepared start path applies.  zedBSD project code.  READ-ONLY: nothing here writes a register, the OpRegion or the
 * VT-d unit.  Registers of a power well that is off are "not readable" (never read as "disabled").
 *
 * Decision: PROCEED only when (a) no pipe is active and no pipe is unreadable, (b) the VT-d unit that translates the
 * GPU is not translating and has no protected memory region enabled (zedBSD has no IOMMU driver), (c) the firmware
 * framebuffer's GGTT pages do not overlap the pages this driver writes.  Otherwise STOP, with the exact reason, before
 * any display write (N1 -- the takeover of a firmware display -- is not ported yet).
 */
#ifndef PARITY_NATIVE_PRECHECK_H
#define PARITY_NATIVE_PRECHECK_H

#include <stdint.h>
#include "opregion_vbt.h"

struct osdep_mmio;

struct parity_native_pipe {
	int readable;                   /* pipe or transcoder power domain on */
	uint32_t transconf, trans_ddi_func, pipesrc, plane_ctl, plane_surf, plane_stride, plane_size;
};

struct parity_native_report {
	int hypervisor;                 /* CPUID.1:ECX[31] */
	/* OpRegion / VBT */
	uint32_t asls;
	int opregion_mapped;
	struct parity_opregion_info op;
	int vbt_mapped, vbt_valid, vbt_matches_pin;
	uint32_t vbt_size;
	uint8_t vbt_sha256[32];
	/* the VT-d unit of the GPU (GFXVTBAR through the MCHBAR mirror) */
	uint64_t gfxvtbar;
	int vtd_enabled, vtd_readable;
	uint32_t vtd_gsts, vtd_pmen;
	/* the firmware framebuffer */
	int fb_present;
	uint64_t fb_base, fb_size;
	int fb_in_aperture;
	uint32_t fb_ggtt_first, fb_ggtt_pages;
	/* display */
	struct parity_native_pipe pipe[4];
	unsigned active_pipes, unreadable_pipes;
	uint32_t pll_enable[2], ddi_buf_ctl_a, pp_status, pp_control, blc_ctl, blc_duty;
	/* the pages this driver will write in the GGTT */
	uint32_t driver_ggtt_first, ggtt_pages;
	int overlap;
	int proceed;
	const char *reason;
};

struct parity_native_deps {
	struct osdep_mmio *mmio;
	uint32_t asls;
	uint64_t gmadr_base, gmadr_size;
	uint32_t ggtt_pages;            /* entries in the GGTT */
	uint32_t driver_ggtt_first;     /* the first GGTT page the driver writes (top window + display window) */
	/* the reference's readout gate: is the pipe's / transcoder's power domain on (no write) */
	int (*pipe_powered)(void *ctx, unsigned pipe);
	void *ctx;
	const uint8_t *vbt_pin;         /* the explicit blob's pinned sha256 (may be 0) */
};

/* fills *r; returns r->proceed */
int parity_native_precheck(const struct parity_native_deps *d, struct parity_native_report *r);
void parity_native_log(const struct parity_native_report *r);

#endif /* PARITY_NATIVE_PRECHECK_H */
""")

open(root + P + "native_precheck.c", "w").write(r"""/*
 * WS031 Linux-parity -- N0 native precheck (see native_precheck.h).  zedBSD project code.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/platform.h>
#include <hal/hal.h>
#include "bootloader/include/amd64-handoff.h"
#include <string.h>
#include "osdep/mmio.h"
#include "bios.h"
#include "native_precheck.h"

#define MCHBAR_MIRROR 0x140000u
#define GFXVTBAR_LO   (MCHBAR_MIRROR + 0x5400u)   /* MCHBAR 0x5400: GFXVTBAR (bit 0 enable) */
#define VTD_GSTS      0x1cu                     /* bit 31 TES translation enabled */
#define VTD_PMEN      0x64u                     /* bit 0 EPM (enable), bit 31 PRS (status) */

static int cpu_hypervisor(void)
{
	uint32_t a = 1u, b, c, d;

	__asm__ __volatile__("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
	(void)b; (void)d;
	return (int)((c >> 31) & 1u);
}

static void read_opregion(const struct parity_native_deps *d, struct parity_native_report *r)
{
	static uint8_t copy[PARITY_OPREGION_SIZE];
	void *op = 0, *vb = 0;
	unsigned i;

	r->asls = d->asls;
	if (d->asls == 0u)
		return;
	if (hal_space_map_device((hal_physaddr_t)d->asls, PARITY_OPREGION_SIZE, HAL_SPACE_READ, &op) != HAL_OK || op == 0)
		return;
	r->opregion_mapped = 1;
	for (i = 0u; i < PARITY_OPREGION_SIZE; i++)
		copy[i] = ((const volatile uint8_t *)op)[i];
	(void)hal_space_unmap_device(op, PARITY_OPREGION_SIZE);
	if (parity_opregion_locate_vbt(copy, sizeof(copy), d->asls, &r->op) != 0)
		return;
	if (r->op.src == PARITY_OPVBT_RVDA) {
		static uint8_t vcopy[16384];
		uint32_t n = r->op.rvds;

		if (n > sizeof(vcopy) || r->op.rvda_inside)
			return;
		if (hal_space_map_device((hal_physaddr_t)r->op.vbt_phys, n, HAL_SPACE_READ, &vb) != HAL_OK || vb == 0)
			return;
		r->vbt_mapped = 1;
		for (i = 0u; i < n; i++)
			vcopy[i] = ((const volatile uint8_t *)vb)[i];
		(void)hal_space_unmap_device(vb, n);
		r->vbt_size = n;
		r->vbt_valid = parity_vbt_validate(vcopy, n);
		parity_sha256(vcopy, n, r->vbt_sha256);
	} else {
		r->vbt_mapped = 1;
		r->vbt_size = r->op.vbt_max;
		r->vbt_valid = parity_vbt_validate(copy + r->op.vbt_offset, r->op.vbt_max);
		parity_sha256(copy + r->op.vbt_offset, r->op.vbt_max, r->vbt_sha256);
	}
	if (d->vbt_pin != 0 && r->vbt_valid) {
		r->vbt_matches_pin = 1;
		for (i = 0u; i < 32u; i++)
			if (r->vbt_sha256[i] != d->vbt_pin[i])
				r->vbt_matches_pin = 0;
	}
}

static void read_vtd(const struct parity_native_deps *d, struct parity_native_report *r)
{
	void *v = 0;
	uint64_t base;

	r->gfxvtbar = (uint64_t)osdep_mmio_read32(d->mmio, GFXVTBAR_LO) | (uint64_t)osdep_mmio_read32(d->mmio, GFXVTBAR_LO + 4u) << 32;
	r->vtd_enabled = (int)(r->gfxvtbar & 1u);
	base = r->gfxvtbar & ~0xfffull;
	if (!r->vtd_enabled || base == 0u)
		return;
	if (hal_space_map_device((hal_physaddr_t)base, 0x1000u, HAL_SPACE_READ, &v) != HAL_OK || v == 0)
		return;
	r->vtd_gsts = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_GSTS);
	r->vtd_pmen = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_PMEN);
	(void)hal_space_unmap_device(v, 0x1000u);
	/* an all-ones read is not a register value (nothing decodes there, e.g. a guest) */
	r->vtd_readable = r->vtd_gsts != 0xffffffffu;
}

static void read_fb(const struct parity_native_deps *d, struct parity_native_report *r)
{
	const struct zbl6_framebuffer *fb = kern_boot_handoff("pcat.framebuffer");

	if (fb == 0 || fb->size == 0u)
		return;
	r->fb_present = 1;
	r->fb_base = fb->physical_base;
	r->fb_size = fb->size;
	if (fb->physical_base >= d->gmadr_base && fb->physical_base + fb->size <= d->gmadr_base + d->gmadr_size) {
		r->fb_in_aperture = 1;
		r->fb_ggtt_first = (uint32_t)((fb->physical_base - d->gmadr_base) >> 12);
		r->fb_ggtt_pages = (uint32_t)((fb->size + 4095u) >> 12);
	}
}

static void read_display(const struct parity_native_deps *d, struct parity_native_report *r)
{
	struct osdep_mmio *m = d->mmio;
	unsigned p;

	for (p = 0u; p < 4u; p++) {
		struct parity_native_pipe *q = &r->pipe[p];
		uint32_t o = 0x1000u * p;

		q->readable = d->pipe_powered(d->ctx, p);
		if (!q->readable) {
			r->unreadable_pipes |= 1u << p;
			continue;
		}
		q->transconf = osdep_mmio_read32(m, 0x70008u + o);
		q->trans_ddi_func = osdep_mmio_read32(m, 0x60400u + o);
		q->pipesrc = osdep_mmio_read32(m, 0x6001cu + o);
		q->plane_ctl = osdep_mmio_read32(m, 0x70180u + o);
		q->plane_stride = osdep_mmio_read32(m, 0x70188u + o);
		q->plane_size = osdep_mmio_read32(m, 0x70190u + o);
		q->plane_surf = osdep_mmio_read32(m, 0x7019cu + o);
		if ((q->transconf & 0x80000000u) != 0u || (q->plane_ctl & 0x80000000u) != 0u)
			r->active_pipes |= 1u << p;
	}
	/* pipe A's domain is on when these are meaningful (PW1 / DDI A); recorded only when pipe A is readable */
	if (r->pipe[0].readable) {
		r->pll_enable[0] = osdep_mmio_read32(m, 0x46010u);
		r->pll_enable[1] = osdep_mmio_read32(m, 0x46014u);
		r->ddi_buf_ctl_a = osdep_mmio_read32(m, 0x64000u);
		r->pp_status = osdep_mmio_read32(m, 0xc7200u);
		r->pp_control = osdep_mmio_read32(m, 0xc7204u);
		r->blc_ctl = osdep_mmio_read32(m, 0xc8250u);
		r->blc_duty = osdep_mmio_read32(m, 0xc8258u);
	}
}

int
parity_native_precheck(const struct parity_native_deps *d, struct parity_native_report *r)
{
	unsigned p;

	memset(r, 0, sizeof(*r));
	r->hypervisor = cpu_hypervisor();
	r->ggtt_pages = d->ggtt_pages;
	r->driver_ggtt_first = d->driver_ggtt_first;
	read_opregion(d, r);
	read_vtd(d, r);
	read_fb(d, r);
	read_display(d, r);
	/* the driver writes GGTT pages [driver_ggtt_first, ggtt_pages): no firmware scanout may live there */
	if (r->fb_in_aperture && r->fb_ggtt_first + r->fb_ggtt_pages > d->driver_ggtt_first)
		r->overlap = 1;
	for (p = 0u; p < 4u; p++)
		if ((r->active_pipes & (1u << p)) != 0u && (r->pipe[p].plane_surf >> 12) + 1u > d->driver_ggtt_first)
			r->overlap = 1;
	r->proceed = 0;
	if (r->active_pipes != 0u)
		r->reason = "a pipe is active (firmware display): the takeover (N1: readout + crtc_disable_noatomic) is not ported";
	else if (r->unreadable_pipes != 0u && !r->hypervisor)
		r->reason = "a pipe's power domain is off: its state is not readable, so 'inactive' is not shown";
	else if (r->overlap)
		r->reason = "the firmware scanout overlaps the GGTT pages this driver writes";
	else if (!r->hypervisor && r->vtd_enabled && !r->vtd_readable)
		r->reason = "the GPU's VT-d unit is enabled but its status is not readable";
	else if (!r->hypervisor && r->vtd_enabled && ((r->vtd_gsts & 0x80000000u) != 0u || (r->vtd_pmen & 0x80000001u) != 0u))
		r->reason = "the GPU's VT-d unit translates / protects memory (firmware DMA protection) and zedBSD has no IOMMU driver";
	else {
		r->proceed = 1;
		r->reason = "start conditions match the prepared path (no active pipe, no overlap, DMA untranslated)";
	}
	return r->proceed;
}

void
parity_native_log(const struct parity_native_report *r)
{
	unsigned p, i;
	char sha[17];

	for (i = 0u; i < 8u; i++) {
		static const char hx[] = "0123456789abcdef";
		sha[2u * i] = hx[r->vbt_sha256[i] >> 4];
		sha[2u * i + 1u] = hx[r->vbt_sha256[i] & 15u];
	}
	sha[16] = '\0';
	kern_logf("i915: parity N0 platform: hypervisor=%d (CPUID.1:ECX[31]; recorded, not a driver mode)\n", r->hypervisor);
	kern_logf("i915: parity N0 opregion: ASLS=0x%08x mapped=%d sig=%d ver=%u.%u.%u size=%uKiB mboxes=0x%x | VBT via %s "
		"(rvda=0x%llx rvds=%u relative=%d inside=%d phys=0x%llx) mapped=%d size=%u valid=%d sha256=%s.. matches explicit pin=%d\n",
		r->asls, r->opregion_mapped, r->op.signature_ok, r->op.major, r->op.minor, r->op.revision, r->op.size_kib, r->op.mboxes,
		r->op.src == PARITY_OPVBT_RVDA ? "RVDA" : r->op.src == PARITY_OPVBT_MAILBOX4 ? "mailbox#4" : "none",
		(unsigned long long)r->op.rvda, r->op.rvds, r->op.rvda_relative, r->op.rvda_inside, (unsigned long long)r->op.vbt_phys,
		r->vbt_mapped, r->vbt_size, r->vbt_valid, r->vbt_mapped ? sha : "-", r->vbt_matches_pin);
	kern_logf("i915: parity N0 vt-d (GPU unit): GFXVTBAR=0x%llx enabled=%d readable=%d GSTS=0x%08x (TES bit31) PMEN=0x%08x "
		"(EPM bit0 / PRS bit31)\n", (unsigned long long)r->gfxvtbar, r->vtd_enabled, r->vtd_readable, r->vtd_gsts, r->vtd_pmen);
	kern_logf("i915: parity N0 firmware framebuffer: present=%d base=0x%llx size=0x%llx in_aperture=%d GGTT pages %u..%u | "
		"driver writes GGTT pages %u..%u overlap=%d\n", r->fb_present, (unsigned long long)r->fb_base,
		(unsigned long long)r->fb_size, r->fb_in_aperture, r->fb_ggtt_first, r->fb_ggtt_first + r->fb_ggtt_pages,
		r->driver_ggtt_first, r->ggtt_pages, r->overlap);
	for (p = 0u; p < 4u; p++) {
		const struct parity_native_pipe *q = &r->pipe[p];

		if (!q->readable) {
			kern_logf("i915: parity N0 pipe %c: not_readable (power domain off)\n", 'A' + (int)p);
			continue;
		}
		kern_logf("i915: parity N0 pipe %c: TRANSCONF=0x%08x TRANS_DDI_FUNC_CTL=0x%08x PIPESRC=0x%08x PLANE_CTL=0x%08x "
			"PLANE_SURF=0x%08x STRIDE=0x%08x SIZE=0x%08x\n", 'A' + (int)p, q->transconf, q->trans_ddi_func, q->pipesrc,
			q->plane_ctl, q->plane_surf, q->plane_stride, q->plane_size);
	}
	kern_logf("i915: parity N0 pipe-A domain: DPLL0_ENABLE=0x%08x DPLL1_ENABLE=0x%08x DDI_BUF_CTL_A=0x%08x PP_STATUS=0x%08x "
		"PP_CONTROL=0x%08x BLC_PWM_CTL=0x%08x BLC_PWM_DUTY=0x%08x\n", r->pll_enable[0], r->pll_enable[1], r->ddi_buf_ctl_a,
		r->pp_status, r->pp_control, r->blc_ctl, r->blc_duty);
	kern_logf("i915: parity N0 decision: %s -- %s (active pipes 0x%x, unreadable 0x%x)\n", r->proceed ? "PROCEED" : "STOP before "
		"any display write", r->reason, r->active_pipes, r->unreadable_pipes);
}
""")

b = load(P + "bios.c")
if "parity_vbt_explicit_pin" not in b:
    b = b.rstrip(NL) + NL + NL + "/* the explicit blob's pinned sha256 (the N0 precheck compares the OpRegion VBT with it) */" + NL + \
        "const uint8_t *parity_vbt_explicit_pin(void) { return explicit_blob_sha256; }" + NL
    save(P + "bios.c", b)
bh = load(P + "bios.h")
if "parity_vbt_explicit_pin" not in bh:
    bh = rep(bh, "void parity_sha256(const void *data, size_t len, uint8_t out[32]);",
             "void parity_sha256(const void *data, size_t len, uint8_t out[32]);" + NL + "const uint8_t *parity_vbt_explicit_pin(void);")
    save(P + "bios.h", bh)

pc = load(P + "probe.c")
pc = rep(pc, """	/*
	 * P3.6 intel_power_domains_init_hw(i915, false): display-core HW bring-up on""", r"""	/*
	 * N0 (E-120): before the first display write, record what the firmware left and decide whether the prepared start
	 * path applies (native_precheck.h).  Read-only.  STOP = the probe ends here, nothing on the display was touched.
	 */
	{
		static struct parity_native_report n0;
		struct parity_native_deps nd;

		memset(&nd, 0, sizeof(nd));
		nd.mmio = &mmio;
		nd.asls = osdep_pci_read32(&pci, 0xFCu);
		nd.gmadr_base = n0_gmadr_base;
		nd.gmadr_size = n0_gmadr_size;
		nd.ggtt_pages = ggtt_entries;
		nd.driver_ggtt_first = ggtt_entries > PARITY_GT_GGTT_PAGES + PARITY_GT_DISPLAY_PAGES ?
			ggtt_entries - PARITY_GT_GGTT_PAGES - PARITY_GT_DISPLAY_PAGES : 0u;
		nd.pipe_powered = n0_pipe_powered;
		nd.ctx = &power_domains;
		nd.vbt_pin = parity_vbt_explicit_pin();
		n0_pwc = &pwc;
		(void)parity_native_precheck(&nd, &n0);
		parity_native_log(&n0);
		if (!n0.proceed) {
			res.outcome = PARITY_BLOCKED;
			res.where = "native-precheck (before any display write)";
			goto teardown;
		}
	}

	/*
	 * P3.6 intel_power_domains_init_hw(i915, false): display-core HW bring-up on""")
save(P + "probe.c", pc)
pc = load(P + "probe.c")
pc = rep(pc, '#include "eu_test.h"\n', '#include "eu_test.h"\n#include "native_precheck.h"\n')
pc = rep(pc, """			gmadr_start = aperture.bus_address;
			mappable_end = aperture.size;""", """			gmadr_start = aperture.bus_address;
			mappable_end = aperture.size;
			n0_gmadr_base = gmadr_start;
			n0_gmadr_size = mappable_end;""")
i = pc.index("static const char *")
pc = pc[:i] + """/* N0 (E-120): the aperture, and the reference's readout gate for the precheck (no write) */
static uint64_t n0_gmadr_base, n0_gmadr_size;
static struct parity_pw_ctx *n0_pwc;
static int n0_pipe_powered(void *ctx, unsigned pipe)
{
	struct parity_power_domains *pd = ctx;

	return parity_display_power_is_enabled(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pipe), n0_pwc) ||
		parity_display_power_is_enabled(pd, (enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + pipe), n0_pwc);
}

""" + pc[i:]
save(P + "probe.c", pc)
mk = load("platform/amd64/vmunix.mk")
if "native_precheck.c" not in mk:
    mk = rep(mk, "src/drivers/gpu/i915/parity/gt_tlb.c", "src/drivers/gpu/i915/parity/gt_tlb.c src/drivers/gpu/i915/parity/opregion_vbt.c src/drivers/gpu/i915/parity/native_precheck.c")
    save("platform/amd64/vmunix.mk", mk)
print("done")
