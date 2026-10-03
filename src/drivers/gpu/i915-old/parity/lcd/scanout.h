/*
 * WS031 Linux-parity — the scanout buffer: a framebuffer the DISPLAY ENGINE reads.
 *
 * First form, deliberately narrow: XRGB8888, DRM_FORMAT_MOD_LINEAR, rotation 0, no
 * scaling, no compression / aux planes, one colour plane.  With a linear modifier the
 * reference does not use a display page table even on hardware that has one
 * (intel_fb_modifier_uses_dpt(): HAS_DPT && modifier != LINEAR), so the ordinary GGTT
 * path is the reference's own path for this buffer, not a shortcut.  A tiled buffer
 * must revisit that decision from the DPT test on.
 *
 * What this object keeps apart (they are different things with different owners):
 *   backing      DMA pages and their size                       (gt_mem object)
 *   cpu          the CPU mapping used to draw into it
 *   ggtt         the reserved GGTT range: guards + pages, and the display alignment
 *   layout       format / modifier / pitch / height            (what the plane is told)
 *   surf         the address the plane's surface register gets
 *   users        who pinned it, and whether the display is scanning it out
 *
 * The memory image is 32 bits per pixel.  The LINK carries 18 bpp to this panel; that
 * number belongs to the link computation and never sizes this buffer.
 *
 * Lifetime: create -> pin -> [publish after CPU writes] -> scanout_begin ... scanout_end
 * -> unpin -> destroy.  A buffer the display is (or may still be) reading is never
 * unpinned or freed: unpin/destroy refuse it, and if a stop could not be confirmed the
 * owner calls parity_scanout_abandon(), which keeps the pages and the mapping for ever
 * and records that, rather than producing a clean-looking teardown.
 */
#ifndef PARITY_SCANOUT_H
#define PARITY_SCANOUT_H

#include <stdint.h>

struct parity_gt_mem;
struct parity_gt_object;

#define PARITY_FOURCC_XRGB8888 0x34325258u      /* 'XR24' */
#define PARITY_MOD_LINEAR 0ull

enum parity_scanout_state {
	PARITY_SCANOUT_NONE = 0,
	PARITY_SCANOUT_ALLOCATED,       /* backing + CPU mapping */
	PARITY_SCANOUT_PINNED,          /* + GGTT range, surf address valid */
	PARITY_SCANOUT_IN_USE,          /* the display engine reads it */
	PARITY_SCANOUT_ABANDONED        /* stop not confirmed: never released */
};

struct parity_scanout {
	/* layout */
	uint32_t width, height;
	uint32_t format;                /* PARITY_FOURCC_XRGB8888 */
	uint64_t modifier;              /* PARITY_MOD_LINEAR */
	unsigned cpp;                   /* bytes per pixel of the MEMORY image: 4 */
	uint32_t pitch;                 /* bytes per row: width * cpp rounded up to the stride alignment */
	uint32_t stride_units;          /* pitch / 64: the PLANE_STRIDE encoding for a linear surface */
	uint32_t size;                  /* pitch * height */
	/* placement requirements */
	uint32_t alignment;             /* bytes: intel_surf_alignment() */
	unsigned guard_pages;           /* scratch PTEs on each side: VTD_GUARD */
	/* backing, mapping, GGTT */
	struct parity_gt_mem *gm;
	struct parity_gt_object *obj;
	uint32_t *cpu;
	uint64_t surf;                  /* GGTT offset for PLANE_SURF (fits 32 bits, 4 KiB aligned) */
	/* users */
	int state;
	const char *pin_owner;
	unsigned users;                 /* how many displays read it now (begin / end); IN_USE while > 0 */
	unsigned publishes;
	unsigned refused_unpin, refused_destroy;
};

/*
 * 0 or a negative errno: -EINVAL unsupported layout, -ENOMEM backing, -EBUSY the storage holds a buffer (any state but
 * NONE: it is left exactly as it was).  The storage must start zeroed (static, or memset by its owner before first use);
 * destroy returns it to that state.  Nothing stays allocated on failure.
 */
int parity_scanout_create(struct parity_gt_mem *gm, uint32_t width, uint32_t height,
	uint32_t format, uint64_t modifier, struct parity_scanout *so);
/* Reserves the GGTT range (alignment + guards), writes the PTEs, flushes the CPU cache for the display. */
int parity_scanout_pin(struct parity_scanout *so, const char *owner);
/* After the CPU changed pixels: make them visible to the display engine (i915_gem_object_flush_if_display). */
void parity_scanout_publish(struct parity_scanout *so);
/* The display engine starts / has provably stopped reading the buffer. */
int parity_scanout_begin(struct parity_scanout *so);
void parity_scanout_end(struct parity_scanout *so);
/* -EBUSY while the display reads it. */
int parity_scanout_unpin(struct parity_scanout *so);
/* -EBUSY while pinned or in use. */
int parity_scanout_destroy(struct parity_scanout *so);
/* The stop could not be confirmed: keep pages, mapping and PTEs for ever; teardown leaves them alone. */
void parity_scanout_abandon(struct parity_scanout *so);

#endif /* PARITY_SCANOUT_H */
