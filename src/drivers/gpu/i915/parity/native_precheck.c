/*
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
#define VTD_VER       0x00u                     /* version: non-zero on a real unit */
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

int
parity_opregion_read_data(uint32_t asls, struct parity_opregion_data *out)
{
	static uint8_t opcopy[PARITY_OPREGION_SIZE];
	static uint8_t vbtcopy[16384];
	void *op = 0, *vb = 0;
	unsigned i;

	memset(out, 0, sizeof(*out));
	out->runtime_enabled = 0;
	out->runtime_reason = "ACPI_RUNTIME_UNAVAILABLE (no AML / ACPI event delivery in zedBSD): the OpRegion is used as data only";
	out->present = asls != 0u;
	if (!out->present)
		return 0;
	/* READ-ONLY view: this driver cannot write the shared mailboxes through it */
	if (hal_space_map_device((hal_physaddr_t)asls, PARITY_OPREGION_SIZE, HAL_SPACE_READ, &op) != HAL_OK || op == 0)
		return -1;
	out->mapped = 1;
	for (i = 0u; i < PARITY_OPREGION_SIZE; i++)
		opcopy[i] = ((const volatile uint8_t *)op)[i];
	(void)hal_space_unmap_device(op, PARITY_OPREGION_SIZE);
	if (parity_opregion_locate_vbt(opcopy, sizeof(opcopy), asls, &out->op) != 0)
		return -1;
	if (out->op.src == PARITY_OPVBT_RVDA) {
		uint32_t len = out->op.rvds;

		if (len > sizeof(vbtcopy) || out->op.rvda_inside)
			return -1;
		if (hal_space_map_device((hal_physaddr_t)out->op.vbt_phys, len, HAL_SPACE_READ, &vb) != HAL_OK || vb == 0)
			return -1;
		for (i = 0u; i < len; i++)
			vbtcopy[i] = ((const volatile uint8_t *)vb)[i];
		(void)hal_space_unmap_device(vb, len);
		out->vbt_size = len;
	} else {
		out->vbt_size = out->op.vbt_max;
		for (i = 0u; i < out->vbt_size; i++)
			vbtcopy[i] = opcopy[out->op.vbt_offset + i];
	}
	out->vbt_valid = parity_vbt_validate(vbtcopy, out->vbt_size);
	if (out->vbt_valid) {
		out->vbt = vbtcopy;
		parity_sha256(vbtcopy, out->vbt_size, out->vbt_sha256);
	}
	return 0;
}

void
parity_opregion_log(const struct parity_opregion_data *d)
{
	if (!d->present) {
		kern_logf("i915: parity P2 opregion: absent (ASLS=0): no OpRegion data, no runtime protocol\n");
		return;
	}
	kern_logf("i915: parity P2 opregion: data=%s ver=%u.%u mboxes=0x%x VBT via %s size=%u valid=%d | runtime=DISABLED (%s)\n",
		d->mapped && d->op.signature_ok ? "AVAILABLE" : "UNAVAILABLE", d->op.major, d->op.minor, d->op.mboxes,
		d->op.src == PARITY_OPVBT_RVDA ? "RVDA" : d->op.src == PARITY_OPVBT_MAILBOX4 ? "mailbox#4" : "none", d->vbt_size,
		d->vbt_valid, d->runtime_reason);
	kern_logf("i915: parity P2 opregion runtime mailboxes as found (observed only; this driver writes none of them): "
		"ACPI drdy=0x%x csts=0x%x cevt=0x%x chpd=0x%x clid=0x%x | ASLE ardy=0x%x aslc=0x%x tche=0x%x\n", d->op.acpi_drdy,
		d->op.acpi_csts, d->op.acpi_cevt, d->op.acpi_chpd, d->op.acpi_clid, d->op.asle_ardy, d->op.asle_aslc, d->op.asle_tche);
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
	r->vtd_ver = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_VER);
	r->vtd_gsts = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_GSTS);
	r->vtd_pmen = *(const volatile uint32_t *)((const volatile uint8_t *)v + VTD_PMEN);
	(void)hal_space_unmap_device(v, 0x1000u);
	/* a real unit reports its version (non-zero, not all-ones); zeros / all-ones = nothing decodes there (e.g. a guest's
	 * view of the host's address) -- then GSTS / PMEN are not register values */
	r->vtd_readable = r->vtd_ver != 0u && r->vtd_ver != 0xffffffffu;
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

	/* HSW_PWR_WELL_CTL2: the STATE bits the power decision reads; all-ones is not a register value */
	r->pwr_well_ctl = osdep_mmio_read32(m, 0x45404u);
	for (p = 0u; p < 4u; p++) {
		struct parity_native_pipe *q = &r->pipe[p];
		uint32_t o = 0x1000u * p;

		if (r->pwr_well_ctl == 0xffffffffu) {
			q->cls = PARITY_N0_READ_ERROR;
			r->unreadable_pipes |= 1u << p;
			continue;
		}
		q->readable = d->pipe_powered(d->ctx, p);
		if (!q->readable) {
			q->cls = PARITY_N0_POWER_OFF;
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
		if (q->transconf == 0xffffffffu || q->plane_ctl == 0xffffffffu) {
			q->cls = PARITY_N0_READ_ERROR;
			continue;
		}
		q->cls = PARITY_N0_READABLE_INACTIVE;
		if ((q->transconf & 0x80000000u) != 0u || (q->plane_ctl & 0x80000000u) != 0u) {
			q->cls = PARITY_N0_READABLE_ACTIVE;
			r->active_pipes |= 1u << p;
		}
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
		r->dpclka_cfgcr0 = osdep_mmio_read32(m, 0x164280u);
	}
}

int
parity_native_precheck(const struct parity_native_deps *d, struct parity_native_report *r)
{

	memset(r, 0, sizeof(*r));
	r->hypervisor = cpu_hypervisor();
	r->ggtt_pages = d->ggtt_pages;
	r->driver_ggtt_first = d->driver_ggtt_first;
	read_opregion(d, r);
	read_vtd(d, r);
	read_fb(d, r);
	read_display(d, r);
	r->parser_src = d->parser_src;
	r->parser_size = d->parser_size;
	if (d->parser_sha256 != 0) {
		unsigned i;

		r->parser_sha_known = 1;
		r->vbt_same_bytes = r->vbt_valid;
		for (i = 0u; i < 32u; i++) {
			r->parser_sha256[i] = d->parser_sha256[i];
			if (r->vbt_sha256[i] != d->parser_sha256[i])
				r->vbt_same_bytes = 0;
		}
	}
	parity_native_decide(r);
	return r->proceed;
}

static const struct parity_native_report *n0_last;

/* the last record again (the runner's final output), or nothing when N0 did not run */
void
parity_native_log_again(void)
{
	if (n0_last == 0)
		return;
	kern_logf("i915: parity N0 ---- summary (repeated at the end of the run) ----\n");
	parity_native_log(n0_last);
}

void
parity_native_log(const struct parity_native_report *r)
{
	unsigned p, i;
	char sha[17], psha[17];

	n0_last = r;
	for (i = 0u; i < 8u; i++) {
		static const char hx[] = "0123456789abcdef";
		sha[2u * i] = hx[r->vbt_sha256[i] >> 4];
		sha[2u * i + 1u] = hx[r->vbt_sha256[i] & 15u];
	}
	sha[16] = '\0';
	for (i = 0u; i < 8u; i++) {
		static const char hx2[] = "0123456789abcdef";
		psha[2u * i] = hx2[r->parser_sha256[i] >> 4];
		psha[2u * i + 1u] = hx2[r->parser_sha256[i] & 15u];
	}
	psha[16] = '\0';
	kern_logf("i915: parity N0 platform: hypervisor=%d (CPUID.1:ECX[31]; recorded, not a driver mode)\n", r->hypervisor);
	kern_logf("i915: parity N0 opregion: ASLS=0x%08x mapped=%d sig=%d ver=%u.%u.%u size=%uKiB mboxes=0x%x | VBT via %s "
		"(rvda=0x%llx rvds=%u relative=%d inside=%d phys=0x%llx) mapped=%d size=%u valid=%d sha256=%s.. matches explicit pin=%d\n",
		r->asls, r->opregion_mapped, r->op.signature_ok, r->op.major, r->op.minor, r->op.revision, r->op.size_kib, r->op.mboxes,
		r->op.src == PARITY_OPVBT_RVDA ? "RVDA" : r->op.src == PARITY_OPVBT_MAILBOX4 ? "mailbox#4" : "none",
		(unsigned long long)r->op.rvda, r->op.rvds, r->op.rvda_relative, r->op.rvda_inside, (unsigned long long)r->op.vbt_phys,
		r->vbt_mapped, r->vbt_size, r->vbt_valid, r->vbt_mapped ? sha : "-", r->vbt_matches_pin);
	kern_logf("i915: parity N0 vt-d (GPU unit): GFXVTBAR=0x%llx enabled=%d view=%s VER=0x%08x readable=%d GSTS=0x%08x "
		"(TES bit31) PMEN=0x%08x (EPM bit0 / PRS bit31)\n", (unsigned long long)r->gfxvtbar, r->vtd_enabled,
		r->hypervisor ? "guest" : "native", r->vtd_ver, r->vtd_readable, r->vtd_gsts, r->vtd_pmen);
	kern_logf("i915: parity N0 vt-d GSTS decoded: TES=%u RTPS=%u FLS=%u AFLS=%u WBFS=%u QIES=%u IRES=%u IRTPS=%u CFIS=%u | "
		"PMEN EPM=%u PRS=%u%s\n", (r->vtd_gsts >> 31) & 1u, (r->vtd_gsts >> 30) & 1u, (r->vtd_gsts >> 29) & 1u,
		(r->vtd_gsts >> 28) & 1u, (r->vtd_gsts >> 27) & 1u, (r->vtd_gsts >> 26) & 1u, (r->vtd_gsts >> 25) & 1u,
		(r->vtd_gsts >> 24) & 1u, (r->vtd_gsts >> 23) & 1u, r->vtd_pmen & 1u, (r->vtd_pmen >> 31) & 1u,
		r->vtd_readable ? "" : " (not register values)");
	kern_logf("i915: parity N0 VBT: observed %s %u bytes sha256=%s.. | adopted by the parser: %s %u bytes sha256=%s.. | "
		"same bytes=%d\n", r->op.src == PARITY_OPVBT_RVDA ? "OPREGION(RVDA)" : r->op.src == PARITY_OPVBT_MAILBOX4 ?
		"OPREGION(mailbox#4)" : "none", r->vbt_size, r->vbt_mapped ? sha : "-",
		r->parser_src == PARITY_VBT_SRC_EXPLICIT_BLOB ? "EXPLICIT_BLOB" : r->parser_src == PARITY_VBT_SRC_OPREGION ? "OPREGION" :
		r->parser_src == PARITY_VBT_SRC_PCI_ROM ? "PCI_ROM" : "NONE", r->parser_size, r->parser_sha_known ? psha : "-",
		r->vbt_same_bytes);
	kern_logf("i915: parity N0 firmware framebuffer: present=%d base=0x%llx size=0x%llx in_aperture=%d GGTT pages %u..%u | "
		"driver writes GGTT pages %u..%u overlap=%d\n", r->fb_present, (unsigned long long)r->fb_base,
		(unsigned long long)r->fb_size, r->fb_in_aperture, r->fb_ggtt_first, r->fb_ggtt_first + r->fb_ggtt_pages,
		r->driver_ggtt_first, r->ggtt_pages, r->overlap);
	for (p = 0u; p < 4u; p++) {
		const struct parity_native_pipe *q = &r->pipe[p];

		if (!q->readable) {
			kern_logf("i915: parity N0 pipe %c: %s -- not_readable (its wells' STATE is off: counted inactive, as the reference readout "
				"concludes; its registers were not read)\n", 'A' + (int)p,
				q->cls == PARITY_N0_READ_ERROR ? "READ_ERROR (the power state did not read as a register value; NOT counted "
				"inactive)" : "POWER_OFF");
			continue;
		}
		kern_logf("i915: parity N0 pipe %c: %s\n", 'A' + (int)p, q->cls == PARITY_N0_READABLE_ACTIVE ? "READABLE_ACTIVE" :
			q->cls == PARITY_N0_READ_ERROR ? "READ_ERROR (a pipe register read all-ones)" : "READABLE_INACTIVE");
		kern_logf("i915: parity N0 pipe %c: TRANSCONF=0x%08x TRANS_DDI_FUNC_CTL=0x%08x PIPESRC=0x%08x PLANE_CTL=0x%08x "
			"PLANE_SURF=0x%08x STRIDE=0x%08x SIZE=0x%08x\n", 'A' + (int)p, q->transconf, q->trans_ddi_func, q->pipesrc,
			q->plane_ctl, q->plane_surf, q->plane_stride, q->plane_size);
	}
	if (!r->pipe[0].readable)
		kern_logf("i915: parity N0 pipe-A domain: not read (pipe A's wells are off)\n");
	else
		kern_logf("i915: parity N0 pipe-A domain: DPLL0_ENABLE=0x%08x DPLL1_ENABLE=0x%08x DPCLKA_CFGCR0=0x%08x (PHY A -> DPLL %u) "
			"DDI_BUF_CTL_A=0x%08x PP_STATUS=0x%08x PP_CONTROL=0x%08x BLC_PWM_CTL=0x%08x BLC_PWM_DUTY=0x%08x\n", r->pll_enable[0],
			r->pll_enable[1], r->dpclka_cfgcr0, r->dpclka_cfgcr0 & 3u, r->ddi_buf_ctl_a, r->pp_status, r->pp_control, r->blc_ctl,
			r->blc_duty);
	kern_logf("i915: parity N0 decision: %s -- %s (active pipes 0x%x, unreadable 0x%x)\n", r->proceed ? "PROCEED" : "STOP before "
		"any display write", r->reason, r->active_pipes, r->unreadable_pipes);
	kern_logf("i915: parity N0 conditions: primary=0x%x observed=0x%x [%s%s%s%s%s%s%s%s%s]\n", r->primary_stop, r->conditions,
		(r->conditions & PARITY_N0_C_ACTIVE_PIPE) ? " ACTIVE_PIPE" : "",
		(r->conditions & PARITY_N0_C_PIPE_READ_ERROR) ? " PIPE_READ_ERROR" : "",
		(r->conditions & PARITY_N0_C_GGTT_OVERLAP) ? " GGTT_OVERLAP" : "",
		(r->conditions & PARITY_N0_C_VTD_UNREADABLE) ? " VTD_UNREADABLE" : "",
		(r->conditions & PARITY_N0_C_VTD_TRANSLATION) ? " VTD_TRANSLATION" : "",
		(r->conditions & PARITY_N0_C_VTD_PMR) ? " VTD_PMR" : "",
		(r->conditions & PARITY_N0_C_VTD_IR_ENABLED) ? " VTD_IR_ENABLED(check the MSI path)" : "",
		(r->conditions & PARITY_N0_C_OPREGION_REGISTER) ? " OPREGION_PRESENT(data only; runtime disabled)" : "",
		(r->conditions & PARITY_N0_C_VBT_DIFFERS) ? " VBT_DIFFERS" : "");
}
