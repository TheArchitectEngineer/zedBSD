/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The other half of the reference test: the sizes and field offsets of
 * the packets and records as amaterrace-gpu's include/ghci_cmd.h lays
 * them out.  This file is compiled only by the host test, against a copy
 * of that header written to a new directory at test time; nothing of the
 * header enters the driver.  The order of the values is the order of
 * ref_names in ghci-ref-host-test.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "ghci_cmd.h"

/* The number of values ref_layout reports. */
#define REF_VALUES			56U

uint32_t ref_layout(uint32_t *values);

/*
 * Reports the reference header's sizes and offsets, in bytes.
 */
uint32_t
ref_layout(
	uint32_t *values)
{
	uint32_t count;

	/* The command packets, in bytes. */
	count = 0U;
	values[count++] = sizeof(GhciCmdBindPipeline);
	values[count++] = sizeof(GhciCmdBindIndexBuffer);
	values[count++] = sizeof(GhciCmdBindDescriptorTable);
	values[count++] = sizeof(GhciCmdSetViewport);
	values[count++] = sizeof(GhciCmdSetScissor);
	values[count++] = sizeof(GhciCmdSetDynamicState);
	values[count++] = sizeof(GhciCmdDraw);
	values[count++] = sizeof(GhciCmdDrawIndexed);
	values[count++] = sizeof(GhciCmdDrawIndirect);
	values[count++] = sizeof(GhciCmdDrawIndirectCount);
	values[count++] = sizeof(GhciCmdDrawMeshTasks);
	values[count++] = sizeof(GhciCmdDispatch);
	values[count++] = sizeof(GhciCmdBarrier);
	values[count++] = sizeof(GhciCmdFence);
	values[count++] = sizeof(GhciCmdQuery);
	values[count++] = sizeof(GhciCmdVmMap);
	values[count++] = sizeof(GhciCmdVmUnmap);
	values[count++] = sizeof(GhciCmdSessionOpen);
	values[count++] = sizeof(GhciCmdSessionClose);
	values[count++] = sizeof(GhciCmdBindSession);
	values[count++] = sizeof(GhciCmdBeginRenderPass);
	values[count++] = sizeof(GhciCmdEndRenderPass);
	values[count++] = sizeof(GhciCmdWaitFence);
	values[count++] = sizeof(GhciCmdWriteTimestamp);
	values[count++] = sizeof(GhciCmdCopyBuffer);
	values[count++] = sizeof(GhciCmdCopyImage);
	values[count++] = sizeof(GhciCmdBlit);
	values[count++] = sizeof(GhciCmdFillBuffer);
	values[count++] = sizeof(GhciCmdResolveImage);
	values[count++] = sizeof(GhciCmdCopyBufferToImage);
	values[count++] = sizeof(GhciCmdCopyImageToBuffer);
	values[count++] = sizeof(GhciCmdFlip);

	/* The pipeline state. */
	values[count++] = sizeof(GhciPipelineState);
	values[count++] = offsetof(GhciPipelineState, flags);
	values[count++] = offsetof(GhciPipelineState, blend_state);
	values[count++] = offsetof(GhciPipelineState, shader_resource_limits);
	values[count++] = offsetof(GhciPipelineState, compute_lds_and_flags);
	values[count++] = offsetof(GhciPipelineState, attributes);

	/* The texture, sampler and buffer descriptors. */
	values[count++] = sizeof(GhciTextureDescriptor);
	values[count++] = offsetof(GhciTextureDescriptor, format);
	values[count++] = offsetof(GhciTextureDescriptor, row_pitch_bytes);
	values[count++] = offsetof(GhciTextureDescriptor, sample_count);
	values[count++] = offsetof(GhciTextureDescriptor, flags);
	values[count++] = offsetof(GhciTextureDescriptor, component_swizzle);
	values[count++] = sizeof(GhciSamplerDescriptor);
	values[count++] = offsetof(GhciSamplerDescriptor, lod_bias_fx8);
	values[count++] = offsetof(GhciSamplerDescriptor, unnormalized_coords);
	values[count++] = sizeof(GhciBufferDescriptor);
	values[count++] = offsetof(GhciBufferDescriptor, stride_bytes);
	values[count++] = offsetof(GhciBufferDescriptor, flags);

	/* The render pass description. */
	values[count++] = sizeof(GhciRenderPassDesc);
	values[count++] = offsetof(GhciRenderPassDesc, depth_stencil);
	values[count++] = offsetof(GhciRenderPassDesc, resolve);
	values[count++] = offsetof(GhciRenderPassDesc, layers);
	values[count++] = offsetof(GhciRenderPassDesc, clear_color_table_va);
	values[count++] = offsetof(GhciColorAttachmentDesc, ops);

	/* Succeeded: the number of values. */
	return count;
}
