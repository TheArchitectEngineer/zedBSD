/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One GPU the nvrtx driver keeps, and the parts of the GSP boot path that
 * touch the kernel: the register window, DMA memory, firmware files and the
 * worker thread.
 */

#ifndef DRIVERS_GPU_NVRTX_GPU_H
#define DRIVERS_GPU_NVRTX_GPU_H

#include <stddef.h>
#include <stdint.h>

#include <drivers/generic/dma.h>
#include <drivers/pci/pci.h>
#include <kern/lock.h>

#include "drivers/gpu/nvrtx/nvrtx-disp.h"
#include "drivers/gpu/nvrtx/nvrtx-gsp.h"
#include "drivers/gpu/nvrtx/nvrtx-mem.h"
#include "drivers/gpu/nvrtx/nvrtx-private.h"
#include "drivers/gpu/nvrtx/nvrtx-work.h"

struct nvrtx_node;

/* The firmware directory the GSP files are read from, for every TU10x chip. */
#define NVRTX_FIRMWARE_DIRECTORY	"/lib/firmware/nvidia/tu102/"

/* The registers P1a reads: GFW's devinit is done when bit 0 of the first is set and the second's low byte is 0xFF. */
#define NVRTX_REG_DEVINIT_LEVEL		0x118128U
#define NVRTX_REG_DEVINIT_PROGRESS	0x118234U
#define NVRTX_DEVINIT_COMPLETED		0xffU

/* The registers P0 reads: the VRAM size and the display's VGA workspace. */
#define NVRTX_REG_VRAM_SIZE		0x100ce0U
#define NVRTX_REG_VGA_WORKSPACE		0x625f04U

/* The largest VBIOS the driver reads. */
#define NVRTX_VBIOS_MAX_BYTES		0x100000U

/* The chunk the firmware image is held in: whole pages, under the bus's segment limit. */
#define NVRTX_IMAGE_CHUNK_BYTES		0x100000U

/* The largest GSP image the three-level table describes with one middle-level page (512 x 512 pages). */
#define NVRTX_IMAGE_MAX_BYTES		0x40000000U

/*
 * The handles the driver gives its objects of the resource manager, the
 * values nouveau uses (R5: the space's root was refused under the earlier
 * handles).  A client handle is in the resource server's client range,
 * 0xc1d0 in the high half and the client's number in the low half; the
 * resource server re-encodes a client handle outside it.  The channel's
 * handle carries its channel number.
 */
#define NVRTX_HANDLE_CLIENT		0xc1d00000U
#define NVRTX_HANDLE_DEVICE		0xde1d0000U
#define NVRTX_HANDLE_SUBDEVICE		0x5d1d0000U
#define NVRTX_HANDLE_SPACE		0x90f10000U
#define NVRTX_HANDLE_CHANNEL		0xf1f00008U
#define NVRTX_HANDLE_COPY		0xc5b50008U

/*
 * The graphics context's objects (5f).  The golden context's space, channel
 * (chid 1) and 3D object take the next handles of nouveau's families; the
 * scrubber's are the resource manager's fixed ones, by which it finds the
 * scrubber channel of the client.
 */
#define NVRTX_HANDLE_GOLDEN_SPACE	0x90f10001U
#define NVRTX_HANDLE_GOLDEN_CHANNEL	0xf1f00001U
#define NVRTX_HANDLE_GOLDEN_THREED	0x97000001U
#define NVRTX_HANDLE_SCRUBBER_SPACE	0xdada0042U
#define NVRTX_HANDLE_SCRUBBER_CHANNEL	0xdada0045U
#define NVRTX_HANDLE_SCRUBBER_THREED	0xdada0046U

/*
 * The first compute launch's objects (P9c, ws037-p008): its address space
 * (owned by the driver), its graphics channel (chid 3) and the compute
 * object on it, in nouveau's families.
 */
#define NVRTX_HANDLE_COMPUTE_SPACE	0x90f10003U
#define NVRTX_HANDLE_COMPUTE_CHANNEL	0xf1f00003U
#define NVRTX_HANDLE_COMPUTE		0xc5c00003U

/* The first draw's 3D object (P9g, ws037-p007), on the compute launch's channel, in nouveau's family. */
#define NVRTX_HANDLE_DRAW		0x97000003U

/*
 * The work channel's objects (stage D1, ws037-p007): its address space
 * (owned by the driver), its graphics channel (chid 4) and the 3D and
 * compute objects on it, in nouveau's families.
 */
#define NVRTX_HANDLE_WORK_SPACE		0x90f10004U
#define NVRTX_HANDLE_WORK_CHANNEL	0xf1f00004U
#define NVRTX_HANDLE_WORK_THREED	0x97000004U
#define NVRTX_HANDLE_WORK_COMPUTE	0xc5c00004U

/*
 * The first draw's place in the compute launch's range: its system-memory
 * pages from NVRTX_DRAW_VA_PAGES (after the launch's own), and its VRAM
 * colour target at NVRTX_DRAW_VA_VRAM.
 */
#define NVRTX_DRAW_VA_PAGES		0x28010000ULL
#define NVRTX_DRAW_PAGES		78U
#define NVRTX_DRAW_VA_VRAM		0x28100000ULL
#define NVRTX_DRAW_VRAM_BYTES		0x40000ULL

/* The compute launch's semaphore page in its range, which the draw's fences release too. */
#define NVRTX_COMPUTE_SEMAPHORE_VA	0x28005000ULL

/*
 * The VRAM the graphics context's arena reserves from the top of the last
 * usable region (P5g), and the compute arena's (P9c, below the graphics
 * arena).  The display's surfaces (P8b) are taken below both.
 */
#define NVRTX_GR_ARENA_RESERVE		0x8800000ULL
#define NVRTX_COMPUTE_ARENA_RESERVE	0xa800000ULL

/* The hash-table entries the display's channels add (P8b): two objects for each of two channels. */
#define NVRTX_DISP_RAMHT_USED_MAX	24U

/* How long a call to the resource manager waits for its reply. */
#define NVRTX_CALL_TIMEOUT_US		5000000U

/* The two falcons the boot drives: the GSP's own and the secure processor's. */
#define NVRTX_FALCON_GSP_BASE		0x110000U
#define NVRTX_FALCON_SEC2_BASE		0x840000U

/*
 * One falcon: its register base and, for the secure processor, its bit in
 * the PMC enable register (from the device information table).
 */
struct nvrtx_falcon {
	uint32_t base;
	uint32_t pmc_mask;
};

/* How far the GSP boot got; each later state includes the earlier ones. */
enum nvrtx_gsp_state {
	NVRTX_GSP_NONE = 0,
	NVRTX_GSP_PREPARED,
	NVRTX_GSP_REGION_SET,
	NVRTX_GSP_RUNNING,
	NVRTX_GSP_FAILED,
	/* A falcon did not halt or the region could not be torn down: memory is kept until power-off. */
	NVRTX_GSP_FAILED_HOLD
};

/*
 * One coherent DMA buffer the GSP reads, with the device it came from.
 */
struct nvrtx_buffer {
	struct drv_dma_buffer dma;
	int allocated;
};

/*
 * A firmware file the boot reads, small enough to hold in kernel memory.
 */
struct nvrtx_blob {
	uint8_t *data;
	size_t size;
};

/*
 * The memory the GSP boot prepares in P0 and keeps while the GSP may read it.
 *
 * Every buffer stays allocated until the GSP is stopped (P7) or, when the
 * boot never started the GSP, until the worker gives the preparation back.
 */
struct nvrtx_boot_memory {
	/* The GSP firmware image (.fwimage) in 1 MiB chunks, and its three-level table. */
	struct nvrtx_pages image;
	struct nvrtx_buffer *image_chunks;
	struct nvrtx_pages table;
	struct nvrtx_buffer table_buffer;
	uint8_t *table_cpu;
	uint64_t table_bus;
	size_t image_size;

	/* The image's signature, the RISC-V bootloader's payload and the placement record. */
	struct nvrtx_buffer signature;
	size_t signature_size;
	struct nvrtx_buffer bootloader;
	struct nvrtx_buffer record;

	/* The start page of the GSP's operating system, its three logs and the start arguments. */
	struct nvrtx_buffer libos;
	struct nvrtx_buffer logs[3];
	struct nvrtx_buffer rmargs;

	/* The shared queue memory and the channel's staging buffer. */
	struct nvrtx_buffer queues;
	struct nvrtx_pages queue_pages;
	uint8_t *queue_cpu;
	uint64_t queue_bus;
	uint8_t *staging;

	/* The two FWSEC programs, each with its command written. */
	struct nvrtx_buffer fwsec_frts;
	struct nvrtx_buffer fwsec_sb;
};

/*
 * The first address space and channel (stages P4 to P6).
 *
 * The VRAM block holds the page tables, the instance block and USERD; the
 * system-memory buffers are the ring, the push buffer, the semaphore page and
 * the method buffer.  The flags say which resource-manager objects exist, so
 * the stop frees only those.
 */
struct nvrtx_engine_state {
	uint64_t vram_block;
	struct nvrtx_buffer ring;
	struct nvrtx_buffer push;
	struct nvrtx_buffer semaphore;
	struct nvrtx_buffer methods;
	uint32_t methods_size;
	struct nvrtx_engine_pick pick;
	int space_made;
	int root_set;
	int channel_made;
	int scheduled;
	int copy_made;
};

/*
 * The graphics context (stage P5g, nvrtx.gr=1).
 *
 * The arena is a run of VRAM below the first block that holds two sets of
 * page tables (the golden context's and the scrubber's), the two channels'
 * instance blocks and USERD, and the context buffers.  The global buffers
 * made for the golden context stay for every later graphics channel; each
 * buffer has the same address in both spaces.  The flags say which
 * resource-manager objects exist, so the stop frees only those.
 */
struct nvrtx_gr_state {
	uint64_t arena;
	struct nvrtx_gr_buffer buffers[NVRTX_GR_BUFFER_MAX];
	size_t buffer_count;
	uint64_t golden_physical[NVRTX_GR_BUFFER_MAX];
	uint64_t scrubber_physical[NVRTX_GR_BUFFER_MAX];
	uint64_t virtual_address[NVRTX_GR_BUFFER_MAX];
	struct nvrtx_buffer golden_methods;
	struct nvrtx_buffer scrubber_methods;
	int golden_space_made;
	int golden_channel_made;
	int golden_threed_made;
	int scrubber_space_made;
	int scrubber_channel_made;
	int scrubber_threed_made;
	int scrubber_enabled;
};

/*
 * The first compute launch (stage P9c, nvrtx.compute=1).
 *
 * The arena is a run of VRAM below the graphics arena that holds the
 * space's page tables, the channel's instance block and USERD, and the
 * channel's own context buffers (the global ones are the golden
 * context's).  The system-memory buffers are the ring, the push buffer,
 * the semaphore, the program, the QMD, constant buffer 0, the result and
 * the method buffer.  The flags say which resource-manager objects exist,
 * so the stop frees only those.
 */
struct nvrtx_compute_state {
	uint64_t arena;
	uint64_t physical[NVRTX_GR_BUFFER_MAX];
	struct nvrtx_buffer ring;
	struct nvrtx_buffer push;
	struct nvrtx_buffer semaphore;
	struct nvrtx_buffer program;
	struct nvrtx_buffer qmd;
	struct nvrtx_buffer constants;
	struct nvrtx_buffer result;
	struct nvrtx_buffer methods;
	int space_made;
	int root_set;
	int channel_made;
	int compute_made;
	int scheduled;
};

/*
 * One run of the VRAM the driver handed out (a buffer object).
 *
 * The VRAM run is whole 4 KiB pages; while bar1_mapped is set the run is
 * also mapped at bar1 in the CPU's window, so the CPU reads and writes it
 * there.  The owner frees it with nvrtx_bo_free(), which unmaps it first.
 */
struct nvrtx_bo {
	uint64_t vram;
	uint64_t size;
	uint64_t bar1;
	int bar1_mapped;
};

/*
 * The work channel (stage D1, nvrtx.work=1): the graphics channel the GPU
 * device keeps for the work it is given (nvrtx-work.h has its layout).
 *
 * The arena is a buffer object of the VRAM allocator holding the space's
 * page tables, the channel's instance block and USERD, and the channel's
 * own context buffers (the global ones are the golden context's).  The
 * system-memory buffers are the ring, the semaphore, the fences' push
 * buffers (one slot per ring entry) and the method buffer.  The made flags
 * say which resource-manager objects exist, so the stop frees only those.
 *
 * lock serializes the submissions and the mappings: put is the next ring
 * entry, fence the last value a submission asked the semaphore to reach,
 * entry_fence the fence each ring entry's submission ended with (so an
 * entry and its fence slot are reused only after that work is done).  va
 * hands out the addresses of the runs mapped for users.  ready is set once
 * the channel ran its first fence; nothing is submitted before.
 */
struct nvrtx_work_state {
	struct nvrtx_bo arena;
	uint64_t physical[NVRTX_GR_BUFFER_MAX];
	struct nvrtx_buffer ring;
	struct nvrtx_buffer semaphore;
	struct nvrtx_buffer fences;
	struct nvrtx_buffer methods;
	struct nvrtx_heap va;
	struct mutex lock;
	uint32_t put;
	uint32_t fence;
	uint32_t entry_fence[NVRTX_WORK_RING_ENTRIES];
	int arena_made;
	int space_made;
	int root_set;
	int channel_made;
	int threed_made;
	int compute_made;
	int scheduled;
	int ready;
};

/*
 * The first draw (stage P9g, nvrtx.draw=1), on the compute launch's
 * channel.
 *
 * The system-memory buffers are the push buffer, the four programs (a
 * vertex and a fragment program, each in one colour and textured),
 * constant buffer 0, the vertices, the texture with its header and
 * sampler, a zero page and the colour target.  The VRAM colour target
 * is a buffer object when the VRAM allocator is up (read back through
 * the CPU's window when it maps), else the compute arena's last MiB (read
 * back word by word).  The flags say what exists, so the stop and the
 * release free only that.
 */
struct nvrtx_draw_state {
	struct nvrtx_buffer push;
	struct nvrtx_buffer programs;
	struct nvrtx_buffer constants;
	struct nvrtx_buffer vertices;
	struct nvrtx_buffer texture;
	struct nvrtx_buffer headers;
	struct nvrtx_buffer samplers;
	struct nvrtx_buffer zero;
	struct nvrtx_buffer target;
	struct nvrtx_bo vram_bo;
	int vram_bo_made;
	uint64_t vram_target;
	int threed_made;
};

/*
 * The VRAM the driver hands out and the CPU's window onto it (stage P4m).
 *
 * The VRAM allocator covers the last usable region below the fixed arenas
 * of P4 to P9c; the BAR1 allocator covers the window the driver's page
 * tables map.  Following nouveau, the BAR1 root (top level) is the resource
 * manager's, from the static information; the driver writes its own lower
 * levels and the root's first entry.  lock serializes both allocators and
 * the page entries; it is taken before the GPU's window_lock.  The flags
 * say what exists, so the release gives back only that.
 */
struct nvrtx_memory_state {
	struct mutex lock;
	int ready;
	struct nvrtx_heap vram;
	int bar1_ready;
	struct nvrtx_heap bar1;
	uint64_t bar1_root;
	uint64_t bar1_root_entry;
	uint64_t bar1_tables;
	uint64_t bar1_window;
	uint64_t bar1_bus;
	/* Where the CPU's mapping of the window starts: after the console's part, else 0. */
	uint64_t bar1_cpu_from;
	int bar1_claimed;
	struct drv_pci_mapping bar1_mapping;
	int bar1_mapped;
};

/*
 * The display under the GSP (stages P8a and P8b).
 *
 * The instance memory is 64 KiB of VRAM the resource manager keeps the
 * display's objects in.  The masks say which heads, windows and outputs the
 * hardware has and which heads the firmware (GOP) lit; the first lit head,
 * its output (SOR) and protocol are what P8b takes over.  The flags say
 * which resource-manager objects exist, so the stop frees only those.
 *
 * P8b adds the core channel and the head's first window channel (their
 * push buffers in system memory, kept until the GSP has stopped), the
 * notifiers' 4 KiB of VRAM after the instance memory, the hash-table slots
 * and context DMA objects it wrote into the instance memory, the head's
 * armed state it copied, and the two test surfaces.
 *
 * P8c adds the head's cursor channel (a PIO channel, made before the GPU
 * device is registered) and core_pending: set while a core update the
 * driver sent without waiting (a cursor's) may not have finished, cleared
 * once its notifier says it has.  Both are the GPU device's, under its lock.
 */
struct nvrtx_display_state {
	uint64_t instance;
	uint32_t head_count;
	uint32_t head_mask;
	uint32_t window_mask;
	uint32_t output_mask;
	uint32_t lit_mask;
	int gop_head_found;
	uint32_t gop_head;
	uint32_t gop_display_id;
	uint32_t gop_sor;
	uint32_t gop_protocol;
	int common_made;
	int root_made;

	/* P8a: the display's own client and objects, and each output's resource, type and protocol by id bit. */
	int client_made;
	int device_made;
	int subdevice_made;
	int hotplug_made;
	int dp_irq_made;
	uint32_t output_or[32];
	uint32_t output_type[32];
	uint32_t output_protocol[32];

	/* P8b: the outputs connected, what the lit output's EDID says, and the LUTs' VRAM. */
	uint32_t connected_mask;
	struct nvrtx_disp_sink sink;
	struct nvrtx_disp_timing timing;
	int timing_from_edid;
	uint64_t olut;
	uint64_t ilut;
	/* The window's notifier the next flip uses (its slot's two halves in turn). */
	uint32_t window_ntfy;

	/* P8b: the channels (the core, every window, window-immediate and cursor), their push buffers and what the takeover wrote. */
	struct nvrtx_buffer core_buffer;
	struct nvrtx_buffer window_buffers[NVRTX_DISP_WINDOWS];
	struct nvrtx_buffer wimm_buffers[NVRTX_DISP_WINDOWS];
	uint32_t windows_made;
	uint32_t wimms_made;
	uint32_t cursors_made;
	struct nvrtx_push core_push;
	struct nvrtx_push window_push;
	uint32_t window;
	uint64_t sync;
	uint32_t object_next;
	uint32_t ramht_slots[NVRTX_DISP_RAMHT_USED_MAX];
	uint32_t ramht_count;
	uint32_t armed[NVRTX_DISP_HEAD_COPIED];
	uint32_t sor_control;
	struct nvrtx_disp_mode mode;
	uint32_t assigned_sor;
	struct nvrtx_disp_surface surfaces[2];
	struct nvrtx_bo surface_objects[2];
	int core_made;
	int window_made;
	int cursor_made;
	int core_pending;
	/* Set once P8b shows its surface on the head: the GPU device (P8c) may take the display. */
	int taken;
};

/*
 * The firmware's console on the GPU the machine boots on (ws037-p006, the
 * native boot): the picture the GOP left at the start of the VRAM window,
 * which the window reaches in its physical mode until the resource manager
 * makes it virtual.  When the console is kept, the resource manager is told
 * its size (it keeps that much VRAM from offset 0, as for the vendor's
 * driver), P4m maps the window's first pages back onto it, P8b shows it on
 * the taken-over head, and a GSP that stops leaves the window in physical
 * mode again.
 *
 * kept says the GSP boot may go on with the console; mapped says the window
 * reaches it again (P4m).  size is the VRAM it takes in whole 64 KiB, pitch
 * its row in bytes; bar1_block is the window's mode register as the
 * firmware left it.
 */
struct nvrtx_console {
	int kept;
	int mapped;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t bar1_block;
};

/*
 * One GPU the driver keeps after N1, when the boot asked for the GSP path.
 *
 * Created by the attach and owned by the PCI binding; the worker thread runs
 * the GSP boot and is the only writer of the state while it runs.  The
 * register window stays mapped for as long as the binding lasts.
 */
struct nvrtx_gpu {
	struct nvrtx_device identity;
	struct drv_pci_mapping registers;
	int registers_claimed;
	struct drv_dma_device *dma;

	/* The firmware files read in P0. */
	struct nvrtx_blob booter_load;
	struct nvrtx_blob booter_unload;
	struct nvrtx_blob bootloader_file;
	struct nvrtx_blob falcon_bootloader;
	struct nvrtx_booter load;
	struct nvrtx_booter unload;
	struct nvrtx_bootloader bootloader_desc;
	struct nvrtx_falcon_bl falcon_bl;

	/* The VBIOS read in P0 and its FWSEC program. */
	uint8_t *vbios_data;
	size_t vbios_size;
	struct nvrtx_vbios vbios;
	struct nvrtx_fwsec fwsec;

	/* The two falcons. */
	struct nvrtx_falcon gsp_falcon;
	struct nvrtx_falcon sec2_falcon;

	/* The placement in the VRAM and the memory the GSP reads. */
	struct nvrtx_wpr_layout layout;
	struct nvrtx_boot_memory memory;
	struct nvrtx_channel channel;
	/* The last call's sequence number, and the buffer each received message is copied into. */
	uint32_t call_sequence;
	uint8_t *reply;
	uint8_t *call;

	/* The first address space and channel. */
	struct nvrtx_engine_state engine;

	/* The graphics golden context and the scrubber. */
	struct nvrtx_gr_state gr;

	/* The display. */
	struct nvrtx_display_state display;

	/* The first compute launch, and the first draw on its channel. */
	struct nvrtx_compute_state compute;
	struct nvrtx_draw_state draw;

	/* The work channel the GPU device keeps (stage D1). */
	struct nvrtx_work_state work;

	/* The VRAM allocator and the BAR1 window. */
	struct nvrtx_memory_state memory_state;

	/* The firmware's console kept through the boot (written by the worker before P0, read after). */
	struct nvrtx_console console;

	/*
	 * Serializes the register window onto the VRAM (PRAMIN) and the TLB
	 * invalidate, which every thread that writes the VRAM or the page
	 * tables shares; set up when the function is kept.
	 */
	struct mutex window_lock;

	/* The static information P3 read, and the resource manager's own client and subdevice. */
	uint8_t *static_info;
	uint32_t internal_client;
	uint32_t internal_subdevice;

	/*
	 * The registered GPU device (stage P8c), and whether the GSP stays up
	 * for it: then detach refuses, and the GPU is kept until power-off.
	 */
	struct nvrtx_node *node;
	int resident;

	/*
	 * Set while the GPU counts in the kernel's hw.gpu.attaching (the
	 * graphical login waits for its display while it does): from the start
	 * of the worker that may register the device to the worker's end.
	 */
	int attach_counted;

	/* Set by the worker; read by detach only after the worker has ended. */
	enum nvrtx_gsp_state state;
	/* Set just before FWSEC-FRTS runs: from then on the end of the boot tears the region down or reports it. */
	int region_written;
	volatile int worker_running;
};

uint32_t nvrtx_rd32(const struct nvrtx_gpu *gpu, uint32_t offset);
void nvrtx_wr32(const struct nvrtx_gpu *gpu, uint32_t offset, uint32_t value);

int nvrtx_buffer_alloc(struct nvrtx_gpu *gpu, size_t size, struct nvrtx_buffer *buffer);
void nvrtx_buffer_free(struct nvrtx_gpu *gpu, struct nvrtx_buffer *buffer);

int nvrtx_file_load(const char *name, size_t limit, struct nvrtx_blob *blob);
void nvrtx_file_unload(struct nvrtx_blob *blob);
int nvrtx_file_load_image(struct nvrtx_gpu *gpu, const char *name);

int nvrtx_rom_read(struct nvrtx_gpu *gpu);

int nvrtx_poll(struct nvrtx_gpu *gpu, uint32_t offset, uint32_t mask, uint32_t value, uint32_t timeout_us);
uint64_t nvrtx_now_us(void);
int nvrtx_falcon_reset(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
int nvrtx_falcon_reset_engine(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
void nvrtx_falcon_start(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
void nvrtx_falcon_load_physical(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
void nvrtx_falcon_dma_system(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, uint32_t context);
uint32_t nvrtx_falcon_code_limit(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon);
int nvrtx_falcon_imem(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, const uint8_t *code, uint32_t length, uint32_t address, uint32_t tag, int secure);
int nvrtx_falcon_dmem(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, const uint8_t *data, uint32_t length, uint32_t address);
int nvrtx_falcon_boot(struct nvrtx_gpu *gpu, const struct nvrtx_falcon *falcon, uint32_t boot_address, uint32_t *mailbox0, uint32_t *mailbox1);

int nvrtx_fwsec_run(struct nvrtx_gpu *gpu, const struct nvrtx_buffer *program);
int nvrtx_booter_run(struct nvrtx_gpu *gpu, const struct nvrtx_booter *booter, uint32_t mailbox0, uint32_t mailbox1);
int nvrtx_region_tear_down(struct nvrtx_gpu *gpu);
void nvrtx_region_bounds(struct nvrtx_gpu *gpu, uint32_t *low, uint32_t *high);
uint32_t nvrtx_frts_result(struct nvrtx_gpu *gpu);

int nvrtx_rpc_send(struct nvrtx_gpu *gpu, uint32_t function, const uint8_t *payload, size_t length, int with_sequence, uint32_t *sequence);
int nvrtx_rpc_wait(struct nvrtx_gpu *gpu, uint32_t function, uint32_t sequence, uint32_t timeout_us, size_t *length);

int nvrtx_rm_allocate(struct nvrtx_gpu *gpu, uint32_t parent, uint32_t handle, uint32_t class_number, const uint8_t *parameters, size_t length);
int nvrtx_rm_control(struct nvrtx_gpu *gpu, uint32_t client, uint32_t object, uint32_t command, const uint8_t *parameters, size_t length, const uint8_t **reply, size_t *reply_length);
int nvrtx_rm_free(struct nvrtx_gpu *gpu, uint32_t object);
int nvrtx_rm_allocate_in(struct nvrtx_gpu *gpu, uint32_t client, uint32_t parent, uint32_t handle, uint32_t class_number, const uint8_t *parameters, size_t length);
int nvrtx_rm_free_in(struct nvrtx_gpu *gpu, uint32_t client, uint32_t object);

int nvrtx_space_start(struct nvrtx_gpu *gpu);
int nvrtx_vram_write(struct nvrtx_gpu *gpu, uint64_t address, const uint8_t *data, size_t length);
int nvrtx_vram_zero(struct nvrtx_gpu *gpu, uint64_t address, size_t length);
int nvrtx_vram_fill(struct nvrtx_gpu *gpu, uint64_t address, uint32_t value, size_t length);
int nvrtx_vram_read32(struct nvrtx_gpu *gpu, uint64_t address, uint32_t *value);
int nvrtx_flush_tlb(struct nvrtx_gpu *gpu, uint64_t root);
int nvrtx_flush_tlb_bar(struct nvrtx_gpu *gpu, uint64_t root);
int nvrtx_channel_start(struct nvrtx_gpu *gpu);
void nvrtx_channel_stop(struct nvrtx_gpu *gpu);
void nvrtx_channel_release(struct nvrtx_gpu *gpu);

int nvrtx_memory_start(struct nvrtx_gpu *gpu);
void nvrtx_memory_release(struct nvrtx_gpu *gpu);
int nvrtx_console_check(struct nvrtx_gpu *gpu);
void nvrtx_console_restore(struct nvrtx_gpu *gpu);
int nvrtx_bo_alloc(struct nvrtx_gpu *gpu, uint64_t size, uint64_t align, struct nvrtx_bo *bo);
int nvrtx_bo_map(struct nvrtx_gpu *gpu, struct nvrtx_bo *bo);
void nvrtx_bo_free(struct nvrtx_gpu *gpu, struct nvrtx_bo *bo);
volatile uint8_t *nvrtx_bo_cpu(struct nvrtx_gpu *gpu, const struct nvrtx_bo *bo);
uint64_t nvrtx_bo_bus(struct nvrtx_gpu *gpu, const struct nvrtx_bo *bo);

int nvrtx_gr_start(struct nvrtx_gpu *gpu);
void nvrtx_gr_stop(struct nvrtx_gpu *gpu);
void nvrtx_gr_release(struct nvrtx_gpu *gpu);
int nvrtx_gr_promote(struct nvrtx_gpu *gpu, uint32_t channel, const uint64_t *physical, bool golden);

int nvrtx_compute_start(struct nvrtx_gpu *gpu);
void nvrtx_compute_stop(struct nvrtx_gpu *gpu);
void nvrtx_compute_release(struct nvrtx_gpu *gpu);
int nvrtx_compute_submit(struct nvrtx_gpu *gpu, uint32_t entry, uint64_t push_va, size_t length);
int nvrtx_compute_wait(struct nvrtx_gpu *gpu, uint32_t value, uint32_t *waited);

int nvrtx_work_start(struct nvrtx_gpu *gpu);
void nvrtx_work_stop(struct nvrtx_gpu *gpu);
void nvrtx_work_release(struct nvrtx_gpu *gpu);
int nvrtx_work_map_vram(struct nvrtx_gpu *gpu, uint64_t physical, uint64_t size, uint64_t *va);
int nvrtx_work_map_system(struct nvrtx_gpu *gpu, const uint64_t *pages, size_t count, uint64_t *va);
int nvrtx_work_unmap(struct nvrtx_gpu *gpu, uint64_t va, uint64_t size);
int nvrtx_work_submit(struct nvrtx_gpu *gpu, uint64_t push_va, size_t length, uint32_t *fence);
int nvrtx_work_wait(struct nvrtx_gpu *gpu, uint32_t fence, uint32_t timeout_us);

int nvrtx_draw_buffers(struct nvrtx_gpu *gpu, uint64_t arena_target);
void nvrtx_draw_pages(const struct nvrtx_gpu *gpu, uint64_t *pages);
int nvrtx_draw_start(struct nvrtx_gpu *gpu);
int nvrtx_draw_stop(struct nvrtx_gpu *gpu);
void nvrtx_draw_release(struct nvrtx_gpu *gpu);

int nvrtx_display_start(struct nvrtx_gpu *gpu);
int nvrtx_display_inherit(struct nvrtx_gpu *gpu);
int nvrtx_display_detect(struct nvrtx_gpu *gpu);
int nvrtx_display_hdmi(struct nvrtx_gpu *gpu);
void nvrtx_display_stop(struct nvrtx_gpu *gpu);

int nvrtx_scanout_start(struct nvrtx_gpu *gpu);
int nvrtx_scanout_frame(struct nvrtx_gpu *gpu, const struct nvrtx_disp_surface *surface);
void nvrtx_scanout_stop(struct nvrtx_gpu *gpu);
void nvrtx_scanout_release(struct nvrtx_gpu *gpu);
int nvrtx_scanout_cursor_start(struct nvrtx_gpu *gpu);
int nvrtx_scanout_cursor_show(struct nvrtx_gpu *gpu, uint64_t offset, uint32_t point);
int nvrtx_scanout_cursor_point(struct nvrtx_gpu *gpu, uint32_t point);
int nvrtx_scanout_cursor_clear(struct nvrtx_gpu *gpu);
int nvrtx_scanout_core_settled(struct nvrtx_gpu *gpu, uint32_t wait_us);
uint32_t nvrtx_scanout_frame_counter(struct nvrtx_gpu *gpu);

int nvrtx_node_start(struct nvrtx_gpu *gpu);

int nvrtx_gsp_start(struct nvrtx_gpu *gpu);
void nvrtx_gsp_release(struct nvrtx_gpu *gpu);

#endif
