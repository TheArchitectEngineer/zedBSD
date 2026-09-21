/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Native Vulkan executor entry: attaches to an i915 device, owns per-open
 * session state, and receives the command stream libvulkan submits through the
 * drv_gpu UAPI.  The capset it reports lets libvulkan open the device without
 * change.  Command decoding is in cmd; this module is the boundary.
 */

#include "vk.h"
#include "cmd.h"
#include "gfx.h"
#include "../parity/resident.h"

#include <kern/kmem.h>
#include <kern/klog.h>
#include <kern/device-io.h>

#include <errno.h>
#include <string.h>

/* The largest reply the executor builds for one command. */
#define I915_VK_REPLY_MAX 4096U

static void i915_vk_capset_fill(struct i915_vk_device *vk);

/* Attaches the executor to an i915 device and prepares its capset. */
int
drv_i915_vk_attach(
	struct i915_device *device,
	struct i915_vk_device **out)
{
	struct i915_vk_device *vk;
	int error;

	/* The caller receives nothing on failure. */
	*out = NULL;

	vk = kern_calloc(1U, sizeof(*vk));
	if (vk == NULL)
		return ENOMEM;

	/* The executor keeps a back reference to the hardware device it serves. */
	vk->i915 = device;

	/* The object table indexes every handle libvulkan creates. */
	error = i915_vk_object_table_create(&vk->objects);
	if (error != 0) {
		kern_free(vk);
		return error;
	}

	/* The capset lets libvulkan accept the node as a Vulkan backend. */
	i915_vk_capset_fill(vk);

	/* Succeeded: the device can accept vk sessions and commands. */
	*out = vk;
	return 0;
}

/* Releases the executor and its object table. */
void
drv_i915_vk_detach(
	struct i915_vk_device *vk)
{
	/* A device that never attached is nothing to release. */
	if (vk == NULL)
		return;

	i915_vk_object_table_destroy(vk->objects);
	kern_free(vk);
}

/* Opens a session over a WS029 drv_gpu session and its PPGTT. */
int
drv_i915_vk_open(
	struct i915_vk_device *vk,
	struct i915_session *gpu_session,
	struct i915_vk_session **out)
{
	struct i915_vk_session *session;

	/* The caller receives nothing on failure. */
	*out = NULL;

	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* The session reaches the executor and the address space it draws into. */
	session->vk = vk;
	session->gpu = gpu_session;

	/* The decode scratch of this session (vkc.h); a session without one decodes no records. */
	session->arena.base = kern_calloc(1U, I915_VK_ARENA_BYTES);
	if (session->arena.base == NULL) {
		kern_free(session);
		return ENOMEM;
	}
	session->arena.size = I915_VK_ARENA_BYTES;

	/* Succeeded: the session accepts commands. */
	*out = session;
	return 0;
}

/* Closes a session; the WS029 session lifetime is owned by the caller. */
void
drv_i915_vk_close(
	struct i915_vk_session *session)
{
	/* A session that never opened is nothing to release. */
	if (session == NULL)
		return;

	i915_vk_gfx_session_close(session);
	kern_free(session->arena.base);
	kern_free(session);
}

/* Decodes and executes one submitted command stream, writing any reply. */
int
drv_i915_vk_command(
	struct i915_vk_session *session,
	const void *wire,
	size_t bytes,
	void *reply,
	size_t *reply_bytes)
{
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	int error;

	/* The reader bounds every decode against the submitted length. */
	reader.base = wire;
	reader.size = bytes;
	reader.offset = 0U;
	reader.error = 0;

	/* The writer bounds the reply against the caller's buffer. */
	writer.base = reply;
	writer.size = reply == NULL ? 0U : *reply_bytes;
	writer.offset = 0U;
	writer.error = 0;

	/* One submission may carry several commands back to back. */
	while (reader.offset < reader.size) {
		session->arena.used = 0U;
		error = i915_vk_cmd_dispatch(session, &reader, &writer);
		if (error != 0)
			return error;
	}

	/* A reply that overflowed the caller's buffer is a protocol error. */
	if (writer.error != 0)
		return EMSGSIZE;

	/*
	 * Reply bytes were written into the shared resource the stream selected; a
	 * write barrier publishes them before the completion trailer libvulkan polls.
	 */
	kern_io_write_barrier();

	/* The caller learns how many reply bytes the commands produced. */
	if (reply_bytes != NULL)
		*reply_bytes = writer.offset;

	/* Succeeded: every command in the stream was decoded and routed. */
	return 0;
}

/* Reports the capset libvulkan reads to accept the node as a Vulkan backend. */
static void
i915_vk_capset_fill(
	struct i915_vk_device *vk)
{
	/*
	 * libvulkan requires at least 156 capset bytes.  The content is a Venus
	 * capability record; the executor reports a single supported revision and
	 * leaves the remaining fields clear until a command needs them.  The
	 * precise field layout is confirmed against libvulkan at integration.
	 */
	memset(vk->capset, 0, sizeof(vk->capset));
	vk->capset[0] = 1U;
	vk->capset_bytes = 156U;

	/*
	 * E-127 resident build: the record libvulkan accepts (context.c): wire version 1, the VK XML
	 * version it was generated from, a non-zero timeline count, and the 168-byte vendor suffix.
	 * XXX: flags 7 = OPAQUE | STRICT_QUEUE | QUIESCE is declared for the connectivity check only;
	 * the shim honours it on the happy path and has no retire / recovery behind it.
	 */
	if (PARITY_RESIDENT) {
		vk->capset[1] = 0x0040310DU;      /* VK_MAKE_VERSION(1, 3, 269) */
		vk->capset[152U / 4U] = 1U;       /* timelines: RCS0 */
		vk->capset[160U / 4U] = 0x5a424453U;
		vk->capset[164U / 4U] = 7U;
		vk->capset_bytes = 168U;
		kern_logf("i915: vk: XXX capset declares vendor flags 7 for the connectivity check (contracts not implemented beyond the happy path)\n");
	}
}

/* Maps a Vulkan result code carried on the wire to an errno. */
int
i915_vk_errno(
	int vk_result)
{
	/* VK_SUCCESS is the only non-negative result the executor forwards as ok. */
	if (vk_result == 0)
		return 0;

	/* Out-of-memory results map to ENOMEM; everything else is a generic error. */
	if (vk_result == -1 || vk_result == -2)
		return ENOMEM;

	return EINVAL;
}
