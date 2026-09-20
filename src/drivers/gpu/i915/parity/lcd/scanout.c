/*
 * WS031 Linux-parity — the scanout buffer (see scanout.h).  zedBSD project code.
 *
 * The layout and placement numbers follow the reference's rules for this one layout;
 * each is named where it is used:
 *   pitch alignment 64        intel_fb_stride_alignment(): a linear surface whose pitch is
 *                             within the plane's maximum stride uses 64 bytes
 *                             (i915_gem_dumb_create() rounds width * cpp up to 64 as well)
 *   stride units   pitch/64   skl_plane_stride() / skl_plane_stride_mult(): a linear surface
 *                             expresses its stride in 64-byte chunks
 *   alignment      256 KiB    intel_surf_alignment() -> intel_linear_alignment(), DISPLAY_VER >= 9
 *   guard          168 PTEs   i915_gem_object_pin_to_display_plane(): VTD_GUARD of scratch on both
 *                             sides when VT-d is active.  A VFIO guest cannot see whether the
 *                             HOST translates its DMA, so the guard is applied always
 *                             (a superset of the reference's behaviour; recorded, not hidden)
 *   cache          clflush    __i915_gem_object_flush_for_display(): the CPU's dirty cache lines
 *                             are flushed before the display reads the pages
 *   PTE            present    gen8_ggtt_pte_encode(): address | PRESENT; the cache level chosen
 *                             for a display pin has no PTE bits on this generation
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <errno.h>
#include "../gt_mem.h"
#include "scanout.h"

#define SCANOUT_STRIDE_ALIGN   64u
#define SCANOUT_SURF_ALIGN     (256u * 1024u)
#define SCANOUT_VTD_GUARD      168u
#define SCANOUT_MAX_STRIDE     131072u          /* skl_plane_max_stride(), DISPLAY_VER >= 13: 128 KiB (65536 pixels would allow more) */

int
parity_scanout_create(struct parity_gt_mem *gm, uint32_t width, uint32_t height,
	uint32_t format, uint64_t modifier, struct parity_scanout *so)
{
	if (gm == 0 || so == 0)
		return -EINVAL;
	/* an object in any other state owns backing / GGTT / a pin: never overwrite its record */
	if (so->state != PARITY_SCANOUT_NONE || so->obj != 0)
		return -EBUSY;
	memset(so, 0, sizeof(*so));
	/* the one layout this stage supports; anything else is refused before any allocation */
	if (format != PARITY_FOURCC_XRGB8888 || modifier != PARITY_MOD_LINEAR ||
	    width == 0u || height == 0u || width > 8192u || height > 4096u)
		return -EINVAL;

	so->gm = gm;
	so->width = width;
	so->height = height;
	so->format = format;
	so->modifier = modifier;
	so->cpp = 4u;
	so->pitch = (width * so->cpp + SCANOUT_STRIDE_ALIGN - 1u) & ~(SCANOUT_STRIDE_ALIGN - 1u);
	if (so->pitch > SCANOUT_MAX_STRIDE)
		return -EINVAL;
	so->stride_units = so->pitch / SCANOUT_STRIDE_ALIGN;
	so->size = so->pitch * height;
	so->alignment = SCANOUT_SURF_ALIGN;
	so->guard_pages = SCANOUT_VTD_GUARD;

	so->obj = parity_gt_object_create(gm, so->size);
	if (so->obj == 0) {
		memset(so, 0, sizeof(*so));
		return -ENOMEM;
	}
	so->cpu = (uint32_t *)so->obj->cpu;
	so->state = PARITY_SCANOUT_ALLOCATED;
	return 0;
}

int
parity_scanout_pin(struct parity_scanout *so, const char *owner)
{
	int rc;

	if (so == 0 || so->state != PARITY_SCANOUT_ALLOCATED)
		return so != 0 && so->state >= PARITY_SCANOUT_PINNED ? -EBUSY : -EINVAL;
	rc = parity_gt_display_bind(so->gm, so->obj, so->alignment / PARITY_GT_PAGE_BYTES, so->guard_pages);
	if (rc != 0)
		return rc;
	/* the plane's surface register holds a 32-bit, 4 KiB aligned GGTT offset */
	if ((so->obj->ggtt_offset >> 32) != 0u || (so->obj->ggtt_offset & (so->alignment - 1u)) != 0u) {
		parity_gt_display_unbind(so->gm, so->obj);
		return -ERANGE;
	}
	so->surf = so->obj->ggtt_offset;
	so->pin_owner = owner;
	so->state = PARITY_SCANOUT_PINNED;
	parity_scanout_publish(so);
	return 0;
}

void
parity_scanout_publish(struct parity_scanout *so)
{
	if (so == 0 || so->state < PARITY_SCANOUT_ALLOCATED || so->cpu == 0)
		return;
	parity_gt_clflush(so->cpu, so->size);
	so->publishes++;
}

int
parity_scanout_begin(struct parity_scanout *so)
{
	/* a second display may read the same buffer: the users are counted, the state is their union */
	if (so == 0 || (so->state != PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_IN_USE))
		return -EINVAL;
	so->users++;
	so->state = PARITY_SCANOUT_IN_USE;
	return 0;
}

void
parity_scanout_end(struct parity_scanout *so)
{
	/* the buffer is given back when the LAST display has let go of it */
	if (so != 0 && so->state == PARITY_SCANOUT_IN_USE && so->users != 0u && --so->users == 0u)
		so->state = PARITY_SCANOUT_PINNED;
}

int
parity_scanout_unpin(struct parity_scanout *so)
{
	if (so == 0)
		return -EINVAL;
	if (so->state == PARITY_SCANOUT_IN_USE || so->state == PARITY_SCANOUT_ABANDONED) {
		so->refused_unpin++;
		return -EBUSY;
	}
	if (so->state != PARITY_SCANOUT_PINNED)
		return -EINVAL;
	parity_gt_display_unbind(so->gm, so->obj);
	so->surf = 0u;
	so->pin_owner = 0;
	so->state = PARITY_SCANOUT_ALLOCATED;
	return 0;
}

int
parity_scanout_destroy(struct parity_scanout *so)
{
	if (so == 0)
		return -EINVAL;
	if (so->state >= PARITY_SCANOUT_PINNED) {
		so->refused_destroy++;
		return -EBUSY;
	}
	if (so->state != PARITY_SCANOUT_ALLOCATED)
		return -EINVAL;
	parity_gt_object_destroy(so->gm, so->obj);
	memset(so, 0, sizeof(*so));
	return 0;
}

void
parity_scanout_abandon(struct parity_scanout *so)
{
	if (so == 0 || so->state < PARITY_SCANOUT_PINNED)
		return;
	so->obj->keep = 1;
	so->state = PARITY_SCANOUT_ABANDONED;
	kern_logf("i915: parity scanout: ABANDONED at GGTT 0x%llx (%u bytes, owner %s): the display was not "
		"shown to have stopped; pages, mapping and PTEs are kept\n",
		(unsigned long long)so->surf, so->size, so->pin_owner != 0 ? so->pin_owner : "-");
}
