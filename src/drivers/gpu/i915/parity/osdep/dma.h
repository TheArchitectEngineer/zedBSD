/*
 * WS031 Linux-parity OS adaptation layer — DMA contract.
 *
 * The single choke point through which the ported driver reaches DMA.  It keeps
 * three things the working reference keeps and the old code blurred:
 *
 *   1. Address kinds are distinct (address_types.h): a CPU/guest-physical address
 *      is mapped to a device DMA address before it can reach a GPU page table.
 *   2. Each entry point preserves its OWN return contract, matching the Linux API
 *      it stands for.  There is no generic "non-zero means failure".
 *        - osdep_dma_set_info       : 0 / -errno
 *        - osdep_dma_map_sg         : mapped segment count (>=1) / 0        [dma_map_sg]
 *        - osdep_dma_map_sgtable    : 0 / -errno                            [dma_map_sgtable]
 *        - osdep_dma_map_page       : osdep_dma_addr_t (osdep_dma_mapping_failed)
 *   3. A mapping is an owned resource with a pin count.  While pinned into a GPU
 *      page table it cannot be unmapped or freed (lifetime safety).
 *
 * The actual address production lives behind a backend vtable, so the contract
 * can be exercised GPU-free with a mock backend and driven by the real
 * drv_dma_* backend in the driver.  Unimplemented backend ops are recorded as
 * such (never faked as a hardware "not supported").
 */
#ifndef PARITY_OSDEP_DMA_H
#define PARITY_OSDEP_DMA_H

#include <stdint.h>
#include "address_types.h"
#include "trace.h"

enum osdep_dma_dir {
	OSDEP_DMA_TO_DEVICE = 0,
	OSDEP_DMA_FROM_DEVICE,
	OSDEP_DMA_BIDIRECTIONAL,
};

/* One scatter entry BEFORE mapping: a physical run the CPU owns. */
struct osdep_sg_entry {
	osdep_cpu_phys_t phys;
	uint32_t length;
};

/* One mapped segment AFTER mapping: a device DMA run. */
struct osdep_dma_segment {
	osdep_dma_addr_t addr;
	uint32_t length;
};

#ifndef OSDEP_DMA_MAX_SEGMENTS
#define OSDEP_DMA_MAX_SEGMENTS 64u
#endif
/*
 * A bookkeeping bound of this adaptation layer (the Linux DMA API has none).
 * Each slot carries OSDEP_DMA_MAX_SEGMENTS segments, so the table is ~1 KiB
 * per slot and lives in .bss.  256 slots made it 268 KiB while the probe maps
 * nothing through it today; 32 keeps room for real use.  Recorded because the
 * kernel image must stay below the 8 MiB physical end the UEFI memory map
 * leaves free (the image is placed at 2 MiB; see ws031 E-91 / P6-c4b).
 */
#ifndef OSDEP_DMA_MAX_MAPPINGS
#define OSDEP_DMA_MAX_MAPPINGS 32u
#endif

/*
 * An owned mapping.  orig_nents is the count the caller must give back to unmap
 * (dma_unmap_sg takes the ORIGINAL entry count, not the coalesced one); nents is
 * the coalesced segment count the hardware walk uses.  They can differ.
 */
struct osdep_dma_mapping {
	uint32_t resource_id;
	int in_use;                 /* slot occupied */
	int pinned;                 /* bound into a GPU page table; unmap/free refused */
	enum osdep_dma_dir dir;
	unsigned orig_nents;        /* input entry count (for unmap) */
	unsigned nents;             /* mapped segment count (for hardware walk) */
	struct osdep_dma_segment segs[OSDEP_DMA_MAX_SEGMENTS];
};

/*
 * Backend vtable.  Each op returns a backend-level result the contract layer
 * translates.  A NULL op means "unimplemented": the contract records
 * OSDEP_TR_UNIMPL and fails the call (it never pretends the hardware refused).
 */
struct osdep_dma_backend {
	const char *name;
	unsigned address_bits;      /* device DMA address width */
	uint64_t max_segment;       /* max single DMA segment size */
	int coherent;               /* non-zero if mappings are cache-coherent */

	/* Set streaming+coherent mask and max segment. Returns 0 / -errno. */
	int (*set_info)(void *priv, unsigned mask_bits, uint64_t max_segment);

	/*
	 * Map orig_nents input entries into out[0..out_cap).  Returns the number
	 * of segments produced (>=1), possibly < orig_nents (coalescing), or 0 on
	 * failure.  Must set each out segment's device addr + length.
	 */
	int (*map_sg)(void *priv, const struct osdep_sg_entry *in, unsigned orig_nents,
		      struct osdep_dma_segment *out, unsigned out_cap, enum osdep_dma_dir dir);
	void (*unmap_sg)(void *priv, const struct osdep_dma_segment *segs, unsigned nents,
			 unsigned orig_nents, enum osdep_dma_dir dir);

	/* Map one physical run. Returns device addr, or OSDEP_DMA_MAPPING_ERROR. */
	uint64_t (*map_page)(void *priv, uint64_t phys, uint32_t size, enum osdep_dma_dir dir);
	void (*unmap_page)(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir);

	void (*sync_for_device)(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir);
	void (*sync_for_cpu)(void *priv, uint64_t dma, uint32_t size, enum osdep_dma_dir dir);
};

struct osdep_dma_device {
	const struct osdep_dma_backend *backend;
	void *priv;                 /* backend private */
	struct osdep_trace *trace;  /* may be NULL */

	unsigned mask_bits;         /* set by osdep_dma_set_info */
	uint64_t max_segment;
	int info_set;

	uint32_t next_resource_id;
	struct osdep_dma_mapping mappings[OSDEP_DMA_MAX_MAPPINGS];
};

/* Bind a device to a backend.  Does not touch hardware. */
void osdep_dma_device_init(struct osdep_dma_device *dev,
			   const struct osdep_dma_backend *backend, void *priv,
			   struct osdep_trace *trace);

/* set_dma_info: max segment + streaming/coherent mask.  Returns 0 / -errno. */
int osdep_dma_set_info(struct osdep_dma_device *dev, unsigned mask_bits, uint64_t max_segment);

unsigned osdep_dma_address_bits(const struct osdep_dma_device *dev);
uint64_t osdep_dma_max_segment(const struct osdep_dma_device *dev);
int osdep_dma_is_coherent(const struct osdep_dma_device *dev);

/*
 * Map a scatter list.  dma_map_sg contract: returns the mapped segment count
 * (>=1) on success, 0 on failure.  On success a mapping handle is returned via
 * *out_m for ownership/lifetime tracking.
 */
int osdep_dma_map_sg(struct osdep_dma_device *dev, const struct osdep_sg_entry *in,
		     unsigned orig_nents, enum osdep_dma_dir dir,
		     struct osdep_dma_mapping **out_m);

/* dma_map_sgtable contract: returns 0 on success, -errno on failure. Same mapping via *out_m. */
int osdep_dma_map_sgtable(struct osdep_dma_device *dev, const struct osdep_sg_entry *in,
			  unsigned orig_nents, enum osdep_dma_dir dir,
			  struct osdep_dma_mapping **out_m);

/* Unmap.  Refused (returns -EBUSY, nothing freed) while the mapping is pinned. */
int osdep_dma_unmap_sg(struct osdep_dma_device *dev, struct osdep_dma_mapping *m);

/* Map one physical run.  Check the result with osdep_dma_mapping_failed(). */
osdep_dma_addr_t osdep_dma_map_page(struct osdep_dma_device *dev, osdep_cpu_phys_t phys,
				    uint32_t size, enum osdep_dma_dir dir);
void osdep_dma_unmap_page(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
			  uint32_t size, enum osdep_dma_dir dir);

/*
 * Pin / unpin a mapping into a GPU page table.  Pin returns the device DMA
 * address of segment 0 as an osdep_dma_addr_t (the value that belongs in a PTE);
 * a CPU physical address can never reach this path.  Unpin releases the pin.
 */
osdep_dma_addr_t osdep_dma_pin(struct osdep_dma_device *dev, struct osdep_dma_mapping *m);
void osdep_dma_unpin(struct osdep_dma_device *dev, struct osdep_dma_mapping *m);

void osdep_dma_sync_for_device(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
			       uint32_t size, enum osdep_dma_dir dir);
void osdep_dma_sync_for_cpu(struct osdep_dma_device *dev, osdep_dma_addr_t addr,
			    uint32_t size, enum osdep_dma_dir dir);

/* Count of live (mapped, not yet unmapped) mappings — for leak checks in tests. */
unsigned osdep_dma_live_mappings(const struct osdep_dma_device *dev);

#endif /* PARITY_OSDEP_DMA_H */
