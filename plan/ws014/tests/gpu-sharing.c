/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises real GPU export/import with real generic handle and descriptor ownership.
 * The backend models allocation references; Venus protocol ownership is tested separately.
 */

#define main gpu_framework_unused_main
#include "gpu-framework.c"
#undef main

#include <kern/process.h>
#include <kern/filedesc.h>
#include <kern/handle.h>
#include <kern/fd-object.h>
#include <uapi/gpu-allocation.h>

/* One counted allocation remains independent from all source and receiver sessions. */
struct sharing_memory {
	uint64_t pages[1];
	unsigned references;
	struct gpu_image_descriptor image;
};

/* One backend alias contributes both session accounting and an allocation reference. */
struct sharing_alias {
	struct test_session *session;
	struct sharing_memory *memory;
};

/* Allocation counts independently reveal premature destruction and leaked exported references. */
static unsigned shared_allocations;

void gpu_test_set_process(struct process *process);
static int sharing_blob(void *opaque, void *session, const struct gpu_blob_create *request, void **result, uint32_t *identifier);
static void sharing_destroy(void *opaque, void *session, void *object);
static int sharing_export(void *opaque, void *session, void *object, const struct gpu_image_descriptor *image, void **result);
static void sharing_release(void *opaque, void *object);
static int sharing_import(void *opaque, void *session, void *object, void **result, uint32_t *identifier);
static void sharing_drop(struct sharing_memory *memory);
static void export_prepare(struct gpu_resource_export *request, uint64_t handle);
static int sharing_ioctl(struct test_file *file, unsigned long command, void *request);
static void sharing_allocation_test(struct test_file *source, struct test_file *receiver, struct test_file *foreign, struct process *process);

/*
 * Verifies capability transfer, copyout rollback, device isolation and final cleanup.
 */
#ifndef GPU_SHARING_ENTRY
#define GPU_SHARING_ENTRY main
#endif
int
GPU_SHARING_ENTRY(void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_share_ops sharing;
	struct drv_gpu_device *device;
	struct drv_gpu_device *other_device;
	struct test_backend backend;
	struct test_backend other_backend;
	struct test_file source;
	struct test_file receiver;
	struct test_file foreign;
	struct process producer;
	struct process consumer;
	struct gpu_blob_create create;
	struct gpu_resource_export exported;
	struct gpu_resource_import imported;
	struct fd_object message;
	struct kernel_handle *reference;
	uint64_t imported_handle;
	unsigned before;
	unsigned flags;
	int incoming;
	int error;

	/* Real descriptor tables provide independent process namespaces. */
	memset(&producer, 0, sizeof(producer));
	memset(&consumer, 0, sizeof(consumer));
	producer.fd = filedesc_create(&producer);
	assert(producer.fd != NULL);
	consumer.fd = filedesc_create(&consumer);
	assert(consumer.fd != NULL);
	gpu_test_set_process(&producer);

	/* The backend owns a complete dynamic sharing interface with no display or mapping substitute. */
	memset(&backend, 0, sizeof(backend));
	memset(&other_backend, 0, sizeof(other_backend));
	memset(&operations, 0, sizeof(operations));
	memset(&sharing, 0, sizeof(sharing));
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_BLOB | GPU_CAP_SHARE | GPU_CAP_ALLOCATION_SHARE;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.blob_create = sharing_blob;
	operations.resource_destroy = sharing_destroy;
	sharing.export_resource = sharing_export;
	sharing.release = sharing_release;
	sharing.import_resource = sharing_import;
	operations.share = &sharing;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == 0);
	error = drv_gpu_register(&operations, &other_backend, &other_device);
	assert(error == 0);
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&receiver, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&foreign, "gpu1", O_RDWR);
	assert(error == 0);

	/* Allocation-only capabilities exercise the same ownership core without image geometry. */
	sharing_allocation_test(&source, &receiver, &foreign, &producer);

	/* Create one source allocation through the actual GPU ioctl dispatcher. */
	memset(&create, 0, sizeof(create));
	create.version = GPU_ABI_VERSION;
	create.size = sizeof(create);
	create.bytes = 4096U;
	create.blob_id = 99U;
	create.flags = GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE;
	error = sharing_ioctl(&source, GPU_BLOB_CREATE, &create);
	assert(error == 0);
	assert(shared_allocations == 1U);

	/* Oversized metadata must be rejected before backend export takes any reference. */
	export_prepare(&exported, create.handle);
	exported.image.stride = 256U;
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == EINVAL);

	/* Export output-copy failure must release its handle and uncommitted descriptor slot. */
	before = allocations;
	export_prepare(&exported, create.handle);
	reject_copyout = 1U;
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == EFAULT);
	assert(allocations == before);
	assert(producer.fd->entries[0].state == FILEDESC_SLOT_FREE);

	/* Wrapper allocation failure also unwinds the backend capability and device hold. */
	export_prepare(&exported, create.handle);
	reject_allocation = 2U;
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == ENOMEM);
	assert(allocations == before);

	/* A successful export installs one typed capability with descriptor-local close-on-exec. */
	export_prepare(&exported, create.handle);
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == 0);
	assert(exported.fd == 0);
	assert(exported.image.device_id != 0U);
	error = filedesc_get_flags(producer.fd, exported.fd, &flags);
	assert(error == 0);
	assert(flags == FILEDESC_CLOEXEC);
	reference = handle_fd_get(exported.fd, KERNEL_HANDLE_DRIVER);
	assert(reference != NULL);
	handle_put(reference);

	/* A queued reference survives sender close before installation below the 32-slot process limit. */
	error = filedesc_get_object_ref(producer.fd, exported.fd, &message);
	assert(error == 0);
	error = filedesc_close(producer.fd, exported.fd);
	assert(error == 0);
	close_file(&source);
	filedesc_destroy(producer.fd);
	producer.fd = NULL;
	assert(shared_allocations == 1U);
	error = filedesc_install_object_from(consumer.fd, &message, FILEDESC_CLOEXEC, 17, &incoming);
	assert(error == 0);
	assert(incoming == 17);
	fd_object_clear(&message);
	gpu_test_set_process(&consumer);

	/* A capability cannot cross GPU instances even when both use the same backend operations. */
	memset(&imported, 0, sizeof(imported));
	imported.version = GPU_ABI_VERSION;
	imported.size = sizeof(imported);
	imported.fd = incoming;
	error = sharing_ioctl(&foreign, GPU_RESOURCE_IMPORT, &imported);
	assert(error == EXDEV);

	/* Receiver output-copy failure destroys only its unpublished attachment. */
	before = allocations;
	reject_copyout = 1U;
	error = sharing_ioctl(&receiver, GPU_RESOURCE_IMPORT, &imported);
	assert(error == EFAULT);
	assert(allocations == before);
	assert(backend.live == 0U);

	/* The receiver imports after the producing open and process table are both gone. */
	error = sharing_ioctl(&receiver, GPU_RESOURCE_IMPORT, &imported);
	assert(error == 0);
	assert(imported.handle != create.handle);
	assert(imported.resource_id == 17U);
	assert(imported.image.device_id == exported.image.device_id);
	imported_handle = imported.handle;

	/* The imported alias survives closure of the last transferable capability fd. */
	error = filedesc_close(consumer.fd, incoming);
	assert(error == 0);
	assert(shared_allocations == 1U);
	error = destroy_handle(&receiver, imported_handle);
	assert(error == 0);
	assert(shared_allocations == 0U);

	/* Final resource and device cleanup leaves no descriptor, cdev or callback ownership. */
	close_file(&receiver);
	close_file(&foreign);

	/* A capability alone keeps its registered backend alive after the last GPU open closes. */
	error = open_file(&source, "gpu0", O_RDWR);
	assert(error == 0);
	create.handle = 0U;
	create.resource_id = 0U;
	error = sharing_ioctl(&source, GPU_BLOB_CREATE, &create);
	assert(error == 0);
	export_prepare(&exported, create.handle);
	error = sharing_ioctl(&source, GPU_RESOURCE_EXPORT, &exported);
	assert(error == 0);
	close_file(&source);
	error = drv_gpu_unregister(device);
	assert(error == EBUSY);
	assert(shared_allocations == 1U);
	error = filedesc_close(consumer.fd, exported.fd);
	assert(error == 0);
	assert(shared_allocations == 0U);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	error = drv_gpu_unregister(other_device);
	assert(error == 0);
	filedesc_destroy(consumer.fd);
	consumer.fd = NULL;
	gpu_test_set_process(NULL);
	assert(allocations == 0U);
	assert(held_spinlocks == 0U);
	assert(credential_references == 0U);
	puts("GPU capability fd: real handle/table transfer, export/import copyout rollback, allocation failure, immutable metadata, GPU isolation, producer exit, capability-only withdrawal barrier and final release PASS");

	/* Succeeded: no whole GPU session was exported or kept alive by the capability. */
	return 0;
}

/* Allocates a modeled GPU-only backing and its independently owned session alias. */
static int
sharing_blob(
	void *opaque,
	void *session,
	const struct gpu_blob_create *request,
	void **result,
	uint32_t *identifier)
{
	struct sharing_memory *memory;
	struct sharing_alias *alias;
	struct test_session *owner;

	/* The real core supplies the documented allocation request to this bounded peer. */
	(void)opaque;
	assert(request->bytes == 4096U);
	memory = kern_calloc(1U, sizeof(*memory));
	if (memory == NULL)
		return ENOMEM;

	/* A failed alias leaves no independent allocation behind. */
	alias = kern_calloc(1U, sizeof(*alias));
	if (alias == NULL) {
		kern_free(memory);
		return ENOMEM;
	}

	/* One alias now retains both the source session and allocation accounting. */
	owner = session;
	memory->references = 1U;
	alias->memory = memory;
	alias->session = owner;
	owner->live++;
	owner->backend->live++;
	shared_allocations++;
	*result = alias;
	*identifier = 17U;

	/* Succeeded: ordinary resource destruction consumes this alias. */
	return 0;
}

/* Destroys one source or receiver alias while other allocation owners remain independent. */
static void
sharing_destroy(
	void *opaque,
	void *session,
	void *object)
{
	struct sharing_alias *alias;
	struct test_session *owner;

	/* The common core must not release a resource through a foreign session. */
	(void)opaque;
	alias = object;
	owner = session;
	assert(held_spinlocks == 0U);
	assert(alias->session == owner);
	owner->live--;
	owner->backend->live--;
	sharing_drop(alias->memory);
	kern_free(alias);

	/* Succeeded: this alias no longer retains its context or allocation. */
	return;
}

/* Acquires an allocation reference without retaining the source session. */
static int
sharing_export(
	void *opaque,
	void *session,
	void *object,
	const struct gpu_image_descriptor *image,
	void **result)
{
	struct sharing_alias *alias;

	/* The kernel copied and stamped image metadata before reaching this callback. */
	(void)opaque;
	alias = object;
	assert(alias->session == session);
	if (image != NULL) {
		/* Image capabilities carry a validated scanout description; raw allocations do not. */
		assert(image->device_id != 0U);
		alias->memory->image = *image;
	}

	/* Either export retains the allocation without retaining its source namespace. */
	alias->memory->references++;
	*result = alias->memory;

	/* Succeeded: the returned capability contains no source-session pointer. */
	return 0;
}

/* Releases the allocation reference owned by a generic kernel capability. */
static void
sharing_release(
	void *opaque,
	void *object)
{
	/* Final handle release must run outside every descriptor and GPU core spinlock. */
	(void)opaque;
	assert(held_spinlocks == 0U);
	sharing_drop(object);

	/* Succeeded: terminal cleanup is independent from the current process. */
	return;
}

/* Creates one receiver alias for a capability's retained allocation. */
static int
sharing_import(
	void *opaque,
	void *session,
	void *object,
	void **result,
	uint32_t *identifier)
{
	struct sharing_alias *alias;
	struct sharing_memory *memory;
	struct test_session *owner;

	/* A destination alias can fail without consuming the source capability. */
	(void)opaque;
	alias = kern_calloc(1U, sizeof(*alias));
	if (alias == NULL)
		return ENOMEM;

	/* Receiver session accounting is separate from the allocation's existing owners. */
	memory = object;
	owner = session;
	alias->session = owner;
	alias->memory = memory;
	memory->references++;
	owner->live++;
	owner->backend->live++;
	*result = alias;
	*identifier = 17U;

	/* Succeeded: the destination now owns an ordinary independently destroyable resource. */
	return 0;
}

/* Frees modeled allocation storage only after the last independent owner retires. */
static void
sharing_drop(
	struct sharing_memory *memory)
{
	/* Every alias and exported capability contributes exactly one allocation reference. */
	assert(memory->references != 0U);
	memory->references--;
	if (memory->references != 0U)
		return;

	/* Terminal destruction is observable separately from session cleanup. */
	shared_allocations--;
	kern_free(memory);

	/* Succeeded: the final owner released the allocation exactly once. */
	return;
}

/* Verifies generic sharing without granting the image-only import protocol. */
static void
sharing_allocation_test(
	struct test_file *source,
	struct test_file *receiver,
	struct test_file *foreign,
	struct process *process)
{
	struct gpu_blob_create create;
	struct gpu_allocation_export exported;
	struct gpu_allocation_import imported;
	struct gpu_resource_import image;
	unsigned before;
	int different;
	int error;

	/* A raw buffer has no width, height, format or row pitch to invent for export. */
	memset(&create, 0, sizeof(create));
	create.version = GPU_ABI_VERSION;
	create.size = sizeof(create);
	create.bytes = 4096U;
	create.blob_id = 98U;
	create.flags = GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE;
	error = sharing_ioctl(source, GPU_BLOB_CREATE, &create);
	assert(error == 0);

	/* The opaque description is immutable per capability and independent from the GPU codec. */
	memset(&exported, 0, sizeof(exported));
	exported.version = GPU_ABI_VERSION;
	exported.size = sizeof(exported);
	exported.handle = create.handle;
	exported.flags = GPU_HANDLE_CLOEXEC;
	exported.fd = -1;
	exported.allocation.version = GPU_ABI_VERSION;
	exported.allocation.size = sizeof(exported.allocation);
	exported.allocation.allocation_bytes = create.bytes;
	exported.allocation.schema = 0x54455354U;
	exported.allocation.metadata_bytes = 3U;
	exported.allocation.metadata[0] = 0x47U;
	exported.allocation.metadata[1] = 0x50U;
	exported.allocation.metadata[2] = 0x55U;

	/* Nonzero unused metadata must fail before taking backend ownership. */
	exported.allocation.metadata[3] = 1U;
	error = sharing_ioctl(source, GPU_ALLOCATION_EXPORT, &exported);
	assert(error == EINVAL);
	exported.allocation.metadata[3] = 0U;

	/* Output failure leaves neither a live fd nor an extra retained allocation reference. */
	before = allocations;
	reject_copyout = 1U;
	error = sharing_ioctl(source, GPU_ALLOCATION_EXPORT, &exported);
	assert(error == EFAULT);
	assert(allocations == before);
	assert(process->fd->entries[0].state == FILEDESC_SLOT_FREE);

	/* Reset output fields after the injected kernel-copy failure and publish the capability. */
	exported.fd = -1;
	exported.allocation.device_id = 0U;
	error = sharing_ioctl(source, GPU_ALLOCATION_EXPORT, &exported);
	assert(error == 0);
	assert(exported.allocation.device_id != 0U);

	/* An allocation envelope cannot masquerade as a validated linear display image. */
	memset(&image, 0, sizeof(image));
	image.version = GPU_ABI_VERSION;
	image.size = sizeof(image);
	image.fd = exported.fd;
	error = sharing_ioctl(receiver, GPU_RESOURCE_IMPORT, &image);
	assert(error == EINVAL);

	/* Import accepts only the same GPU even when other devices share the same driver table. */
	memset(&imported, 0, sizeof(imported));
	imported.version = GPU_ABI_VERSION;
	imported.size = sizeof(imported);
	imported.fd = exported.fd;
	error = sharing_ioctl(foreign, GPU_ALLOCATION_IMPORT, &imported);
	assert(error == EXDEV);

	/* Receiver-supplied metadata is rejected instead of replacing the authoritative envelope. */
	imported.allocation.schema = 1U;
	error = sharing_ioctl(receiver, GPU_ALLOCATION_IMPORT, &imported);
	assert(error == EINVAL);
	imported.allocation.schema = 0U;

	/* Import output failure retires only the new alias and preserves the original capability. */
	before = allocations;
	reject_copyout = 1U;
	error = sharing_ioctl(receiver, GPU_ALLOCATION_IMPORT, &imported);
	assert(error == EFAULT);
	assert(allocations == before);

	/* A complete import returns exactly the sender's immutable bytes and a fresh resource id. */
	error = sharing_ioctl(receiver, GPU_ALLOCATION_IMPORT, &imported);
	assert(error == 0);
	assert(imported.handle != create.handle);
	different = memcmp(&imported.allocation, &exported.allocation, sizeof(imported.allocation));
	assert(different == 0);

	/* Source destruction and last capability close leave the receiver's alias alive. */
	error = destroy_handle(source, create.handle);
	assert(error == 0);
	error = filedesc_close(process->fd, exported.fd);
	assert(error == 0);
	assert(shared_allocations == 1U);

	/* The final receiver destruction retires the actual allocation exactly once. */
	error = destroy_handle(receiver, imported.handle);
	assert(error == 0);
	assert(shared_allocations == 0U);
	puts("GPU allocation fd: opaque metadata, image-protocol rejection, rollback, same-GPU identity and independent lifetime PASS");

	/* Succeeded: no test allocation or transferable fd remains live. */
	return;
}

/* Initializes one valid versioned export request without output identities. */
static void
export_prepare(
	struct gpu_resource_export *request,
	uint64_t handle)
{
	/* The public header and empty fd output precede the immutable image description. */
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->handle = handle;
	request->flags = GPU_HANDLE_CLOEXEC;
	request->fd = -1;
	request->image.version = GPU_ABI_VERSION;
	request->image.size = sizeof(request->image);
	request->image.width = 32U;
	request->image.height = 32U;
	request->image.stride = 128U;
	request->image.format = GPU_PIXEL_RGBA8888;
	request->image.allocation_bytes = 4096U;
	request->image.memory_type = 0U;
	request->image.usage = 6U;
	request->image.tiling = GPU_IMAGE_LINEAR;

	/* Succeeded: the real core must supply the fd and device identity. */
	return;
}

/* Dispatches a request through the real cdev and GPU session admission path. */
static int
sharing_ioctl(
	struct test_file *file,
	unsigned long command,
	void *request)
{
	int error;

	/* All ownership and copyout decisions belong to the production GPU dispatcher. */
	error = cdev_file_ops.ioctl(&file->file, command, (uintptr_t)request);
	if (error != 0)
		return error;

	/* Succeeded: this production request completed normally. */
	return 0;
}
