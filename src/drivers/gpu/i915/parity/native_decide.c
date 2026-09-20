/*
 * WS031 Linux-parity -- the N0 decision (native_precheck.h), pure: it reads only the report.  zedBSD project code.
 */
#include "native_precheck.h"

void
parity_native_decide(struct parity_native_report *r)
{
	unsigned p;

	r->overlap = 0;
	/* the driver writes GGTT pages [driver_ggtt_first, ggtt_pages): no firmware scanout may live there */
	if (r->fb_in_aperture && r->fb_ggtt_first + r->fb_ggtt_pages > r->driver_ggtt_first)
		r->overlap = 1;
	for (p = 0u; p < 4u; p++)
		if ((r->active_pipes & (1u << p)) != 0u && (r->pipe[p].plane_surf >> 12) + 1u > r->driver_ggtt_first)
			r->overlap = 1;
	/* every condition (so one native run names every wall it can see), then the primary in decision order */
	r->conditions = 0u;
	if (r->active_pipes != 0u)
		r->conditions |= PARITY_N0_C_ACTIVE_PIPE;
	for (p = 0u; p < 4u; p++)
		if (r->pipe[p].cls == PARITY_N0_READ_ERROR)
			r->conditions |= PARITY_N0_C_PIPE_READ_ERROR;
	if (r->overlap)
		r->conditions |= PARITY_N0_C_GGTT_OVERLAP;
	if (!r->hypervisor && r->vtd_enabled) {
		if (!r->vtd_readable)
			r->conditions |= PARITY_N0_C_VTD_UNREADABLE;
		else {
			if ((r->vtd_gsts & 0x80000000u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_TRANSLATION;
			if ((r->vtd_pmen & 0x80000001u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_PMR;
			if ((r->vtd_gsts & 0x02000000u) != 0u)
				r->conditions |= PARITY_N0_C_VTD_IR_ENABLED;
		}
	}
	if (r->asls != 0u)
		r->conditions |= PARITY_N0_C_OPREGION_REGISTER;
	if (r->vbt_valid && r->parser_sha_known && !r->vbt_same_bytes)
		r->conditions |= PARITY_N0_C_VBT_DIFFERS;

	r->proceed = 0;
	r->primary_stop = 0u;
	if (r->conditions & PARITY_N0_C_ACTIVE_PIPE) {
		r->primary_stop = PARITY_N0_C_ACTIVE_PIPE;
		r->reason = "a pipe is active (firmware display): the takeover (N1: readout + crtc_disable_noatomic) is not ported";
	} else if (r->conditions & PARITY_N0_C_PIPE_READ_ERROR) {
		r->primary_stop = PARITY_N0_C_PIPE_READ_ERROR;
		r->reason = "a pipe's power state / registers did not read as register values: not shown inactive";
	} else if (r->conditions & PARITY_N0_C_GGTT_OVERLAP) {
		r->primary_stop = PARITY_N0_C_GGTT_OVERLAP;
		r->reason = "the firmware scanout overlaps the GGTT pages this driver writes";
	} else if (r->conditions & PARITY_N0_C_VTD_UNREADABLE) {
		r->primary_stop = PARITY_N0_C_VTD_UNREADABLE;
		r->reason = "the GPU's VT-d unit is enabled but its status is not readable";
	} else if (r->conditions & (PARITY_N0_C_VTD_TRANSLATION | PARITY_N0_C_VTD_PMR)) {
		r->primary_stop = r->conditions & (PARITY_N0_C_VTD_TRANSLATION | PARITY_N0_C_VTD_PMR);
		r->reason = "the GPU's VT-d unit translates / protects memory (firmware DMA protection) and zedBSD has no IOMMU driver";
	} else {
		r->proceed = 1;
		r->reason = r->hypervisor ? "start conditions match the prepared path (no active pipe, no overlap; VT-d: guest view, "
			"the host owns the unit)" : "start conditions match the prepared path (no active pipe, no overlap, DMA untranslated)";
	}

}
