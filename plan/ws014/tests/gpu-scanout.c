/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Real GPU/handle/fd ownership, with a display-only peer validating an explicit physical backing. */
#define GPU_SHARING_ENTRY gpu_sharing_unused_main
#include "gpu-sharing.c"
#undef GPU_SHARING_ENTRY
#include <uapi/gpu-scanout.h>
#include <uapi/gpu-fence.h>

/* A native alias deliberately owns no source allocation reference of its own. */
struct scanout_alias {
	const uint64_t *pages;
	struct test_session *session;
};

/* Borrows the last exported allocation while actual kernel handles keep its backing alive. */
static struct sharing_memory *scanout_memory;
/* Selects one finite unsupported address, cache or export profile for the next native trial. */
static unsigned backing_fault;
/* Counts aliases successfully admitted by the independent display-only backend. */
static unsigned native_imports;
/* Counts native aliases retired while their source page pin is still live. */
static unsigned native_destroys;
/* Counts hardware-selection callbacks; failed prerequisites must leave this unchanged. */
static unsigned native_presents;
/* Retains the real source device identity returned by kernel discovery for the companion hint. */
static uint64_t companion;

static int scanout_backing(void *opaque, void *object, struct drv_gpu_scanout_backing *backing);
static int scanout_import(void *opaque, void *session, const struct gpu_image_descriptor *image, const struct drv_gpu_scanout_backing *backing, void **result);
static void scanout_destroy(void *opaque, void *session, void *object);
static int scanout_device(void *opaque, void *session, struct gpu_device_info *request);
static int scanout_constraints(void *opaque, void *session, struct gpu_scanout_constraints *request);
static int scanout_query(void *opaque, void *session, struct gpu_display_info *request);
static int scanout_mode(void *opaque, void *session, struct gpu_display_mode *request);
static int scanout_claim(void *opaque, void *session, struct gpu_display_claim *request);
static int scanout_release(void *opaque, void *session, const struct gpu_display_release *request);
static int scanout_present(void *opaque, void *session, void *object, struct gpu_display_present *request);
static int scanout_wait(void *opaque, void *session, struct gpu_display_wait *request);
static void scanout_fences(struct test_file *display, uint64_t handle, struct process *process);

/*
 * Foreign native import pins immutable source backing through rollback, source exit and native destruction.
 */
int
main(
	void)
{
	struct drv_gpu_ops renderer;
	struct drv_gpu_ops native;
	struct drv_gpu_share_ops sharing;
	struct drv_gpu_scanout_ops scanout;
	struct drv_gpu_display_ops display;
	struct drv_gpu_device *source_device;
	struct drv_gpu_device *display_device;
	struct test_backend source_backend;
	struct test_backend display_backend;
	struct test_file source;
	struct test_file destination;
	struct process process;
	struct gpu_device_info identity;
	struct gpu_scanout_constraints constraints;
	struct gpu_blob_create create;
	struct gpu_resource_export exported;
	struct gpu_resource_import imported;
	unsigned before;
	int error;

	/* The receiving process owns ordinary fd entries and real reference-bearing handles. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);
	memset(&renderer, 0, sizeof(renderer));
	memset(&native, 0, sizeof(native));
	memset(&sharing, 0, sizeof(sharing));
	memset(&scanout, 0, sizeof(scanout));
	memset(&display, 0, sizeof(display));
	memset(&source_backend, 0, sizeof(source_backend));
	memset(&display_backend, 0, sizeof(display_backend));
	renderer.version = DRV_GPU_INTERFACE_VERSION;
	renderer.size = sizeof(renderer);
	renderer.capabilities = GPU_CAP_BLOB | GPU_CAP_SHARE;
	renderer.open = backend_open;
	renderer.close = backend_close;
	renderer.get_info = backend_get_info;
	renderer.blob_create = sharing_blob;
	renderer.resource_destroy = sharing_destroy;
	sharing.export_resource = sharing_export;
	sharing.release = sharing_release;
	sharing.import_resource = sharing_import;
	sharing.get_scanout_backing = scanout_backing;
	renderer.share = &sharing;
	error = drv_gpu_register(&renderer, &source_backend, &source_device);
	assert(error == 0);

	/* The native device implements no renderer, storage allocator, share importer or command decoder. */
	native.version = DRV_GPU_INTERFACE_VERSION;
	native.size = sizeof(native);
	native.capabilities = GPU_CAP_DISPLAY | GPU_CAP_FENCE;
	native.open = backend_open;
	native.close = backend_close;
	native.get_info = backend_get_info;
	native.resource_destroy = scanout_destroy;
	scanout.query_device = scanout_device;
	scanout.constraints = scanout_constraints;
	scanout.import_image = scanout_import;
	native.scanout = &scanout;
	display.query = scanout_query;
	display.mode = scanout_mode;
	display.claim = scanout_claim;
	display.release = scanout_release;
	display.present = scanout_present;
	display.wait = scanout_wait;
	native.display = &display;
	error = drv_gpu_register(&native, &display_backend, &display_device);
	assert(error == 0);
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&destination, "gpu1", O_RDWR);
	assert(error == 0);

	/* Stable kernel identities and concrete DMA requirements are queried before any allocation. */
	memset(&identity, 0, sizeof(identity));
	identity.version = GPU_ABI_VERSION;
	identity.size = sizeof(identity);
	error = sharing_ioctl(&source, GPU_DEVICE_QUERY, &identity);
	assert(error == 0 && identity.device_id != 0U);
	companion = identity.device_id;
	memset(&identity, 0, sizeof(identity));
	identity.version = GPU_ABI_VERSION;
	identity.size = sizeof(identity);
	error = sharing_ioctl(&destination, GPU_DEVICE_QUERY, &identity);
	assert(error == 0 && identity.roles == GPU_DEVICE_DISPLAY && identity.companion_id == companion);
	assert(identity.device_id != companion);
	memset(&constraints, 0, sizeof(constraints));
	constraints.version = GPU_ABI_VERSION;
	constraints.size = sizeof(constraints);
	constraints.display_id = 1U;
	constraints.generation = 1U;
	error = sharing_ioctl(&destination, GPU_DISPLAY_CONSTRAINTS, &constraints);
	assert(error == 0 && constraints.max_dma_address == UINT32_MAX);
	assert(constraints.placement == (GPU_PLACEMENT_DMA32 | GPU_PLACEMENT_CONTIGUOUS | GPU_PLACEMENT_COHERENT));

	memset(&create, 0, sizeof(create));
	create.version = GPU_ABI_VERSION;
	create.size = sizeof(create);
	create.bytes = 4096U;
	create.blob_id = 1U;
	create.flags = GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE;
	error = sharing_ioctl(&source, GPU_BLOB_CREATE, &create);
	assert(error == 0);
	export_prepare(&exported, create.handle);
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == 0);

	/* Ordinary renderer import cannot reinterpret the source device's renderer-local resource ID. */
	memset(&imported, 0, sizeof(imported));
	imported.version = GPU_ABI_VERSION;
	imported.size = sizeof(imported);
	imported.fd = exported.fd;
	error = sharing_ioctl(&destination, GPU_RESOURCE_IMPORT, &imported);
	assert(error == EXDEV);
	imported.flags = GPU_IMPORT_SCANOUT;

	/* The native peer rejects actual inaccessible/cache-incompatible backing rather than trusting flags. */
	for (backing_fault = 1U; backing_fault <= 3U; backing_fault++) {
		error = sharing_ioctl(&destination, GPU_RESOURCE_IMPORT, &imported);
		assert(error == EOPNOTSUPP);
		assert(native_imports == 0U && display_backend.live == 0U);
	}

	backing_fault = 0U;
	before = allocations;
	reject_copyout = 1U;
	error = sharing_ioctl(&destination, GPU_RESOURCE_IMPORT, &imported);
	assert(error == EFAULT);
	assert(allocations == before && native_imports == 1U && native_destroys == 1U);
	assert(shared_allocations == 1U && display_backend.live == 0U);

	/* Copyout rollback did not consume the exporting fd or its immutable allocation description. */
	error = sharing_ioctl(&destination, GPU_RESOURCE_IMPORT, &imported);
	assert(error == 0 && imported.resource_id == 0U);
	assert(imported.image.device_id == exported.image.device_id);
	assert(imported.image.device_id != identity.device_id);
	close_file(&source);
	error = filedesc_close(process.fd, exported.fd);
	assert(error == 0 && shared_allocations == 1U);
	assert(scanout_memory->pages[0] == 0x100000U);
	error = drv_gpu_unregister(source_device);
	assert(error == EBUSY);

	/* Explicit shared fence dependencies guard real native callback admission and selection completion. */
	scanout_fences(&destination, imported.handle, &process);

	/* Core must release the source handle after native resource_destroy finishes reading borrowed pages. */
	error = destroy_handle(&destination, imported.handle);
	assert(error == 0 && shared_allocations == 0U);
	assert(native_imports == 2U && native_destroys == 2U);
	close_file(&destination);
	error = drv_gpu_unregister(source_device);
	assert(error == 0);
	error = drv_gpu_unregister(display_device);
	assert(error == 0);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U && credential_references == 0U && held_spinlocks == 0U);
	puts("PASS display-only GPU: native query/constraints, checked foreign backing, rejected DMA/cache mismatch, copyout rollback, source exit, final pin ordering and display wait/signal fences");
	return 0;
}

/* The retained allocation owns this immutable physical-page array until its final capability release. */
static int
scanout_backing(
	void *opaque,
	void *object,
	struct drv_gpu_scanout_backing *backing)
{
	struct sharing_memory *memory;

	(void)opaque;
	/* Exporting no physical backing is an explicit unsupported sharing contract. */
	if (backing_fault == 3U)
		return EOPNOTSUPP;

	/* This borrowed array belongs to the actual allocation pinned by the GPU core. */
	memory = object;
	scanout_memory = memory;
	memory->pages[0] = backing_fault == 1U ? 0x100000000ULL : 0x100000U;
	backing->pages = memory->pages;
	backing->page_count = 1U;
	backing->page_bytes = 4096U;
	backing->bytes = 4096U;
	backing->flags = DRV_GPU_BACKING_COHERENT | DRV_GPU_BACKING_CONTIGUOUS;

	/* Device-mapped storage cannot satisfy this native peer's coherent-RAM requirement. */
	if (backing_fault == 2U)
		backing->flags |= DRV_GPU_BACKING_DEVICE;

	/* Succeeded: the source capability owns this borrowed physical backing description. */
	return 0;
}

/* A destination validates real page addresses, cache attributes and image geometry before owning an alias. */
static int
scanout_import(
	void *opaque,
	void *session,
	const struct gpu_image_descriptor *image,
	const struct drv_gpu_scanout_backing *backing,
	void **result)
{
	struct scanout_alias *alias;
	struct test_session *owner;

	owner = session;
	assert(owner->backend == opaque && held_spinlocks == 0U);
	assert(backing->pages == scanout_memory->pages && backing->page_bytes == 4096U);
	assert(image->format == GPU_PIXEL_RGBA8888 && image->stride == 128U && image->offset == 0U);
	assert(image->width == 32U && image->height == 32U && image->allocation_bytes == 4096U);

	/* The display refuses inaccessible addresses and incompatible cache attributes before allocation. */
	if (backing->pages[0] > UINT32_MAX - 4095U || backing->flags != (DRV_GPU_BACKING_COHERENT | DRV_GPU_BACKING_CONTIGUOUS))
		return EOPNOTSUPP;

	/* A native alias borrows pages; it never takes a hidden source-memory reference. */
	alias = kern_calloc(1U, sizeof(*alias));
	assert(alias != NULL);

	/* Native identity ties this borrowed array to the destination session only. */
	alias->pages = backing->pages;
	alias->session = owner;

	/* Resource counts keep the destination backend registered until its alias is destroyed. */
	owner->live++;
	owner->backend->live++;
	native_imports++;
	*result = alias;

	/* Succeeded: the destination owns an alias whose lifetime is pinned by the GPU core. */
	return 0;
}

/* The only source lifetime comes from the GPU core's retained handle, including output-copy rollback. */
static void
scanout_destroy(
	void *opaque,
	void *session,
	void *object)
{
	struct scanout_alias *alias;
	struct test_session *owner;

	owner = session;
	alias = object;
	assert(owner->backend == opaque && alias->session == owner && held_spinlocks == 0U);
	assert(shared_allocations == 1U && alias->pages[0] == 0x100000U);

	/* Retirement must precede the core's final source handle put and physical page free. */
	owner->live--;
	owner->backend->live--;
	native_destroys++;
	kern_free(alias);

	/* Succeeded: native alias retirement observed live pages before the final source put. */
	return;
}

/* Reports display-only roles without claiming renderer commands or allocations. */
static int
scanout_device(
	void *opaque,
	void *session,
	struct gpu_device_info *request)
{
	(void)opaque;
	(void)session;
	request->roles = GPU_DEVICE_DISPLAY;
	request->companion_id = companion;

	/* Succeeded: native discovery can associate this display with the actual source renderer. */
	return 0;
}

/* Publishes actual native DMA, packed format and coherent backing requirements. */
static int
scanout_constraints(
	void *opaque,
	void *session,
	struct gpu_scanout_constraints *request)
{
	(void)opaque;
	(void)session;
	assert(request->display_id == 1U && request->generation == 1U);
	request->flags = GPU_SCANOUT_SHARED | GPU_SCANOUT_FOREIGN;
	request->formats = GPU_DISPLAY_FORMAT_RGBA8888;
	request->stride_alignment = 128U;
	request->offset_alignment = 4096U;
	request->placement = GPU_PLACEMENT_DMA32 | GPU_PLACEMENT_CONTIGUOUS | GPU_PLACEMENT_COHERENT;
	request->max_dma_address = UINT32_MAX;

	/* Succeeded: callers can trial foreign scanout against the concrete native limits. */
	return 0;
}

/* Enumerates one native output without fabricating a rendering device. */
static int
scanout_query(
	void *opaque,
	void *session,
	struct gpu_display_info *request)
{
	(void)opaque;
	(void)session;
	request->count = 1U;

	/* Succeeded: the native peer has exactly one output. */
	return 0;
}

/* Rejects mode operations outside this finite import and ownership fixture. */
static int
scanout_mode(
	void *opaque,
	void *session,
	struct gpu_display_mode *request)
{
	(void)opaque;
	(void)session;
	(void)request;

	/* This fixture does not claim mode validation or enumeration support. */
	return EOPNOTSUPP;
}

/* Publishes the one independent native display lease used by this peer. */
static int
scanout_claim(
	void *opaque,
	void *session,
	struct gpu_display_claim *request)
{
	(void)opaque;
	(void)session;
	request->lease = 1U;

	/* Succeeded: this finite request owns the native lease. */
	return 0;
}

/* Retires the finite native lease without retaining extra source references. */
static int
scanout_release(
	void *opaque,
	void *session,
	const struct gpu_display_release *request)
{
	(void)opaque;
	(void)session;
	(void)request;

	/* Succeeded: no native lease ownership remains in this peer. */
	return 0;
}

/* Reads pinned source pages only after the actual kernel has satisfied the producer dependency. */
static int
scanout_present(
	void *opaque,
	void *session,
	void *object,
	struct gpu_display_present *request)
{
	struct scanout_alias *alias;

	(void)opaque;
	alias = object;
	assert(alias->session == session && alias->pages[0] == 0x100000U);
	assert(request->flags == (GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB));
	native_presents++;
	request->sequence = native_presents;

	/* Succeeded: native selection observed the live imported allocation. */
	return 0;
}

/* Rejects unrelated wait operations which this finite peer does not model. */
static int
scanout_wait(
	void *opaque,
	void *session,
	struct gpu_display_wait *request)
{
	(void)opaque;
	(void)session;
	(void)request;

	/* No independent native wait behavior is simulated by this peer. */
	return EOPNOTSUPP;
}

/* Native display completion and producer completion are separate typed, reference-bearing payloads. */
static void
scanout_fences(
	struct test_file *display,
	uint64_t handle,
	struct process *process)
{
	struct gpu_fence_create wait;
	struct gpu_fence_create signal;
	struct gpu_fence_state state;
	struct gpu_fence_bind bind;
	struct gpu_display_present_sync request;
	int error;

	memset(&wait, 0, sizeof(wait));
	wait.version = GPU_ABI_VERSION;
	wait.size = sizeof(wait);
	wait.fd = -1;
	wait.signaled = 1U;
	error = sharing_ioctl(display, GPU_FENCE_CREATE, &wait);
	assert(error == 0 && wait.generation == 1U);
	memset(&signal, 0, sizeof(signal));
	signal.version = GPU_ABI_VERSION;
	signal.size = sizeof(signal);
	signal.fd = -1;
	error = sharing_ioctl(display, GPU_FENCE_CREATE, &signal);
	assert(error == 0);
	memset(&request, 0, sizeof(request));
	request.present.version = GPU_ABI_VERSION;
	request.present.size = sizeof(request.present);
	request.present.handle = handle;
	request.present.lease = 1U;
	request.present.width = 32U;
	request.present.height = 32U;
	request.present.stride = 128U;
	request.present.format = GPU_PIXEL_RGBA8888;
	request.present.refresh_millihz = 60000U;
	request.present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	request.present.generation = 1U;
	request.wait_fd = wait.fd;
	request.wait_generation = wait.generation;
	request.signal_fd = signal.fd;
	request.signal_generation = signal.generation;
	error = sharing_ioctl(display, GPU_DISPLAY_PRESENT_SYNC, &request);
	assert(error == 0 && native_presents == 1U && request.present.sequence == 1U);
	memset(&state, 0, sizeof(state));
	state.version = GPU_ABI_VERSION;
	state.size = sizeof(state);
	state.fd = signal.fd;
	error = sharing_ioctl(display, GPU_FENCE_QUERY, &state);
	assert(error == 0 && state.state == GPU_FENCE_SIGNALED && state.error == 0);

	/* An error prerequisite must not enter the native callback or signal a false successful frame. */
	memset(&state, 0, sizeof(state));
	state.version = GPU_ABI_VERSION;
	state.size = sizeof(state);
	state.fd = wait.fd;
	state.generation = wait.generation;
	error = sharing_ioctl(display, GPU_FENCE_RESET, &state);
	assert(error == 0 && state.generation == 2U);
	memset(&bind, 0, sizeof(bind));
	bind.version = GPU_ABI_VERSION;
	bind.size = sizeof(bind);
	bind.fd = wait.fd;
	bind.generation = state.generation;
	error = sharing_ioctl(display, GPU_FENCE_BIND, &bind);
	assert(error == 0);
	state.state = 0U;
	state.error = EIO;
	error = sharing_ioctl(display, GPU_FENCE_SIGNAL, &state);
	assert(error == 0);
	request.present.sequence = 0U;
	request.wait_generation = 2U;
	request.signal_fd = -1;
	request.signal_generation = 0U;
	error = sharing_ioctl(display, GPU_DISPLAY_PRESENT_SYNC, &request);
	assert(error == EIO && native_presents == 1U);
	error = filedesc_close(process->fd, wait.fd);
	assert(error == 0);
	error = filedesc_close(process->fd, signal.fd);
	assert(error == 0);

	/* Succeeded: distinct producer and display payloads retire without false successful selection. */
	return;
}
