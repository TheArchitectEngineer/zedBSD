/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Vulkan Video H.264 decode on the Raspberry Pi 4 (see vulkan-video.h).
 *
 * The wire is libvulkan's (userland/desktop/libvulkan/video.c), the same
 * the i915 executor reads (i915/render/video.c); the readers here follow
 * that file.  What differs is the decoder: the VideoCore firmware's, which
 * is stateful.  A decode command becomes one access unit for it -- the
 * sequence and picture parameter sets rebuilt from their StdVideo fields
 * (the SPS with VUI that asks for no reordering), then the application's
 * slices -- and the frame that comes back is the decode's output.  The
 * DPB pictures are kept only in form: the firmware keeps its own
 * references, and the output picture (which is also the setup slot's,
 * DPB_AND_OUTPUT_COINCIDE) receives every decoded frame.
 *
 * A decode runs in the native job like a CPU copy, a step at a time: the
 * first step sends the unit, and each step looks for the frame with the
 * controller's window open (ioctls go on) for a few milliseconds and else
 * ends, so the other sessions' work runs while the VPU decodes.  The frame
 * comes into the decoder's cached memory; it is copied into the output
 * image with the firmware's footroom error undone, and stays there as the
 * image's cached copy, which the image's readback reads instead of the
 * image's uncached memory.  A bitstream the firmware cannot take, or a
 * frame that does not come, skips the picture (the image keeps what it
 * had) and marks the status query ERROR; it does not lose the submission.
 */

#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/bcm2711/vulkan-video.h"
#include "drivers/gpu/bcm2711/vulkan-command.h"
#include "drivers/gpu/bcm2711/vulkan-input.h"
#include "drivers/gpu/bcm2711/vulkan-resource.h"
#include "drivers/gpu/bcm2711/vulkan-memory.h"
#include "drivers/gpu/bcm2711/v3d-memory.h"
#include "drivers/gpu/bcm2711/buffer.h"
#include "drivers/gpu/bcm2711/render-device.h"
#include "drivers/gpu/bcm2711/render-worker.h"
#include "drivers/platform/rpi4/rpi4-h264.h"
#include "drivers/platform/rpi4/rpi4-mmal.h"
#include "drivers/platform/rpi4/rpi4-mmal-wire.h"
#include "drivers/platform/rpi4/rpi4-vchiq.h"

/*
 * The profiles offered.  When the firmware is found to output in display
 * order despite the rewritten VUI (design §5, R1), only Baseline, which
 * never reorders, is offered.
 */
#define VIDEO_BASELINE_ONLY		0

/* The H.264 decode limits the executor reports and holds sessions to. */
#define VIDEO_MAX_DPB_SLOTS		17U
#define VIDEO_MAX_REFERENCES		16U
#define VIDEO_MAX_WIDTH			1920U
#define VIDEO_MAX_HEIGHT		1088U
#define VIDEO_MB			16U
#define VIDEO_MAX_SLOTS_RECORDED	32U

/* The H.264 header the decoder was written against: VK_STD_vulkan_video_codec_h264_decode 1.0.0. */
#define VIDEO_STD_NAME			"VK_STD_vulkan_video_codec_h264_decode"
#define VIDEO_STD_VERSION		((1U << 22) | (0U << 12) | 0U)

/* The H.264 profiles, and the level reported: STD_VIDEO_H264_LEVEL_IDC_4_2. */
#define VIDEO_PROFILE_BASELINE		66U
#define VIDEO_PROFILE_MAIN		77U
#define VIDEO_PROFILE_HIGH		100U
#define VIDEO_LEVEL_4_2			12U

/* The parameter set id ranges. */
#define VIDEO_SPS_IDS			32U
#define VIDEO_PPS_IDS			256U

/* The deepest chain a structure may carry. */
#define VIDEO_MAX_CHAIN			8U

/* The firmware decoder's input staging: the largest access unit taken. */
#define VIDEO_INPUT_BYTES		(4U * 1024U * 1024U)

/* How long a decode waits for its frame, over all its steps. */
#define VIDEO_FRAME_MS			500U

/* How long one step of a decode looks for the frame before the other sessions take their turns. */
#define VIDEO_POLL_MS			2U

/* The timeouts in a row after which a session stops waiting. */
#define VIDEO_MAX_TIMEOUTS		3U

/* The push picture's time stamp: decodes count from 1, so no decode has it (see send_push). */
#define VIDEO_PUSH_PTS			0

/* The lines a session writes about pushes and about late frames, at most. */
#define VIDEO_PUSH_LINES		4U
#define VIDEO_LATE_LINES		8U

/* How often the cost line is written while decodes run, and how many a boot writes at most. */
#define VIDEO_STATS_MS			2000U
#define VIDEO_STATS_REPORTS		64U

/* The session memory binding reported: one page the decoder never uses. */
#define VIDEO_BIND_BYTES		4096U

/* The most queries of a result status pool. */
#define VIDEO_QUERY_MAX			64U

/* The structure types the video wire carries (Vulkan 1.3 registry values). */
#define VIDEO_TYPE_PROFILE			1000023000U
#define VIDEO_TYPE_CAPABILITIES			1000023001U
#define VIDEO_TYPE_PICTURE_RESOURCE		1000023002U
#define VIDEO_TYPE_MEMORY_REQUIREMENTS		1000023003U
#define VIDEO_TYPE_BIND_MEMORY			1000023004U
#define VIDEO_TYPE_SESSION_CREATE		1000023005U
#define VIDEO_TYPE_PARAMETERS_CREATE		1000023006U
#define VIDEO_TYPE_PARAMETERS_UPDATE		1000023007U
#define VIDEO_TYPE_BEGIN			1000023008U
#define VIDEO_TYPE_END				1000023009U
#define VIDEO_TYPE_CONTROL			1000023010U
#define VIDEO_TYPE_REFERENCE_SLOT		1000023011U
#define VIDEO_TYPE_PROFILE_LIST			1000023013U
#define VIDEO_TYPE_FORMAT_INFO			1000023014U
#define VIDEO_TYPE_FORMAT_PROPERTIES		1000023015U
#define VIDEO_TYPE_DECODE			1000024000U
#define VIDEO_TYPE_DECODE_CAPABILITIES		1000024001U
#define VIDEO_TYPE_DECODE_USAGE			1000024002U
#define VIDEO_TYPE_H264_CAPABILITIES		1000040000U
#define VIDEO_TYPE_H264_PICTURE			1000040001U
#define VIDEO_TYPE_H264_PROFILE			1000040003U
#define VIDEO_TYPE_H264_PARAMETERS_CREATE	1000040004U
#define VIDEO_TYPE_H264_PARAMETERS_ADD		1000040005U
#define VIDEO_TYPE_H264_DPB_SLOT		1000040006U

/* The flags and enumerants of the video API answered or checked. */
#define VIDEO_OP_DECODE_H264			0x1U
#define VIDEO_CHROMA_420			0x2U
#define VIDEO_DEPTH_8				0x1U
#define VIDEO_LAYOUT_PROGRESSIVE		0U
#define VIDEO_SEPARATE_REFERENCE_IMAGES		0x2U
#define VIDEO_DPB_AND_OUTPUT_COINCIDE		0x1U
#define VIDEO_CONTROL_RESET			0x1U
#define VIDEO_PICTURE_IDR			0x4U
#define VIDEO_PICTURE_REFERENCE			0x10U

/* The query type of a result status pool, and its results. */
#define VIDEO_QUERY_RESULT_STATUS		1000023000U
#define VIDEO_STATUS_COMPLETE			1
#define VIDEO_STATUS_ERROR			(-1)
#define VIDEO_QUERY_RESULT_64			0x1U
#define VIDEO_QUERY_RESULT_AVAILABILITY		0x4U
#define VIDEO_QUERY_REPLY_MAX			4096U

/* The video results the executor replies (the pinned header's values). */
#define VIDEO_ERROR_IMAGE_USAGE			(-1000023000)
#define VIDEO_ERROR_LAYOUT			(-1000023001)
#define VIDEO_ERROR_OPERATION			(-1000023002)
#define VIDEO_ERROR_FORMAT			(-1000023003)
#define VIDEO_ERROR_CODEC			(-1000023004)
#define VIDEO_ERROR_STD_VERSION			(-1000023005)

/* One picture parameter set a parameters object holds; the sets of the object are a list. */
struct video_pps {
	struct video_pps *next;
	struct drv_rpi4_h264_pps fields;
};

/*
 * One VkVideoSessionParametersKHR: the H.264 sets of one session.
 *
 * Published from its creation to its destruction; the recorded begins that
 * name it retain it.  The update sequence orders the updates.
 */
struct video_parameters {
	struct bcm2711_vulkan_input_owner owner;
	uint32_t max_sps;
	uint32_t max_pps;
	uint32_t update_sequence;
	struct drv_rpi4_h264_sps *sps[VIDEO_SPS_IDS];
	struct video_pps *pps;
};

/*
 * One VkVideoSessionKHR and the firmware decoder behind it.
 *
 * Published from its creation; the recorded begins retain it, so a decode
 * always finds its decoder.  The last sets sent, the time stamps and the
 * decode in flight move only in the native job, which runs one decode at a
 * time: inflight is the decode whose unit the firmware has and whose frame
 * has not come (its later steps find it there), until deadline.  unit is
 * the scratch a parameter set is written into (kept off the kernel stack).
 */
struct video_session {
	struct bcm2711_vulkan_input_owner owner;
	uint32_t profile_idc;
	uint32_t width;
	uint32_t height;
	uint32_t max_dpb_slots;
	uint32_t max_references;
	struct drv_rpi4_mmal_decoder *decoder;
	bool broken;
	unsigned timeouts;
	bool started;
	int64_t sequence;
	const struct video_command *inflight;
	int64_t pts;
	uint64_t started_at;
	uint64_t sent_at;
	uint64_t deadline;
	uint32_t steps;
	unsigned push_lines;
	unsigned late_lines;
	uint8_t sent_sps[DRV_RPI4_H264_SPS_MAX + 2048U];
	size_t sent_sps_bytes;
	uint8_t sent_pps[DRV_RPI4_H264_SPS_MAX + 2048U];
	size_t sent_pps_bytes;
	uint8_t unit[DRV_RPI4_H264_SPS_MAX + 2048U];
};

/*
 * One result status query pool.
 *
 * Published from its creation; the recorded query commands retain it.
 * The statuses and availabilities change only in the native job.
 */
struct video_pool {
	struct bcm2711_vulkan_input_owner owner;
	uint32_t count;
	int32_t status[VIDEO_QUERY_MAX];
	uint8_t available[VIDEO_QUERY_MAX];
};

/* A picture resource as recorded. */
struct video_resource {
	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t layer;
	uint64_t view;
};

/*
 * One recorded video or query command.
 *
 * It starts with the common record, which the command buffer's walks read.
 * It retains every object it names until its command buffer is reset or
 * its last submission retires.  A decode and a begin query borrow the
 * begin of their coding scope, and a decode the begin query open around
 * it (status); both live in the same command buffer, which retains the
 * pool.
 */
struct video_command {
	struct bcm2711_vulkan_record record;
	struct bcm2711_vulkan_object *session;
	struct bcm2711_vulkan_object *parameters;
	struct bcm2711_vulkan_object *buffer;
	struct bcm2711_vulkan_object *view;
	struct bcm2711_vulkan_object *pool;
	const struct video_command *begin;
	const struct video_command *status;
	uint32_t flags;
	uint64_t offset;
	uint64_t range;
	struct video_resource destination;
	bool has_picture;
	uint32_t picture_flags;
	uint32_t sps_id;
	uint32_t pps_id;
	uint32_t frame_num;
	int32_t poc;
	uint32_t query;
	uint32_t query_count;
};

/*
 * What the chained records of one structure carried.  It lives on the
 * stack of one command's decoding; the add record's sets are the
 * parameters object's to take, or freed with the chain.
 */
struct video_chain {
	bool has_h264_profile;
	uint32_t profile_idc;
	uint32_t picture_layout;
	bool has_profile_list;
	uint32_t profile_count;
	bool profiles_supported;
	int32_t profile_error;
	bool has_h264_create;
	uint32_t max_sps;
	uint32_t max_pps;
	struct drv_rpi4_h264_sps *new_sps[VIDEO_SPS_IDS];
	uint32_t new_sps_count;
	struct video_pps *new_pps;
	int add_error;
	bool has_picture;
	uint32_t picture_flags;
	uint32_t sps_id;
	uint32_t pps_id;
	uint32_t frame_num;
	int32_t poc;
};

/*
 * The decoder's cached copy of the last frame placed in an image: where
 * its planes are in the decoder's bounce memory (valid until the decoder's
 * next receive or flush, which forget it first), their pitch, and the part
 * of the image the frame covered (bytes a row, luma rows).  A readback of
 * that image reads here, cheaply, instead of the image's uncached memory.
 * owner is the session that placed it, NULL when there is none.  Written
 * by the native job and read by it and by a session's release, all under
 * the controller mutex; relayout fills it in the window before owner is
 * set.
 */
struct video_shadow {
	const struct video_session *owner;
	const struct bcm2711_vulkan_resource *image;
	const uint8_t *luma;
	const uint8_t *chroma;
	uint32_t pitch;
	uint32_t width;
	uint32_t rows;
};

/*
 * What the decodes and readbacks of the last few seconds cost, for the
 * line written every VIDEO_STATS_MS: counts, and sums and the largest of
 * the times in ticks (send: from a decode's start to its unit sent; frame:
 * from then to its frame received, which is the VPU's decode, the bulk
 * and the cache invalidation; place: the corrected copy into the image;
 * readback: an NV12 image's copy to a buffer, from the cached copy or the
 * image's uncached memory).  Changed under the controller mutex.
 */
struct video_stats {
	uint64_t window_start;
	uint32_t reports;
	uint32_t decodes;
	uint32_t skipped;
	uint32_t pushes;
	uint32_t steps;
	uint64_t send_ticks;
	uint64_t frame_ticks;
	uint64_t frame_max;
	uint64_t place_ticks;
	uint32_t cached_readbacks;
	uint64_t cached_ticks;
	uint32_t uncached_readbacks;
	uint64_t uncached_ticks;
};

static int video_capabilities(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static void reply_capabilities(struct i915_wire_writer *reply, const uint32_t *types, uint32_t count);
static int video_format_properties(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int video_family_properties(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int session_create(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int session_requirements(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int session_bind(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int parameters_create(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int parameters_update(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int destroy_object(struct bcm2711_vulkan_session *session, enum i915_vk_object_kind kind, struct i915_wire_reader *reader);
static int release_session(struct bcm2711_vulkan_session *session, void *payload);
static int release_parameters(struct bcm2711_vulkan_session *session, void *payload);
static int release_pool(struct bcm2711_vulkan_session *session, void *payload);
static int record_command(struct bcm2711_vulkan_session *session, uint32_t opcode, struct i915_wire_reader *reader);
static int decode_command(struct bcm2711_vulkan_session *session, uint32_t opcode, struct i915_wire_reader *reader, struct video_command *command);
static int retain_command(struct bcm2711_vulkan_session *session, struct video_command *command, struct bcm2711_vulkan_object *device, uint64_t session_id, uint64_t parameters_id, uint64_t buffer_id, uint64_t view_id, uint64_t pool_id);
static int release_command(struct bcm2711_vulkan_command_node *node);
static const struct video_command *open_scope(const struct bcm2711_vulkan_command_buffer *command);
static const struct video_command *open_query(const struct bcm2711_vulkan_command_buffer *command);
static void read_chain(struct i915_wire_reader *reader, struct video_chain *chain, uint32_t depth);
static void read_fields(struct i915_wire_reader *reader, uint32_t type, struct video_chain *chain, uint32_t depth);
static int32_t read_profile(struct i915_wire_reader *reader, uint32_t *profile_idc);
static int32_t profile_result(uint32_t operation, uint32_t chroma, uint32_t luma, uint32_t chroma_depth, const struct video_chain *chain);
static void read_resource(struct i915_wire_reader *reader, struct video_resource *resource);
static void read_slot(struct i915_wire_reader *reader, struct video_resource *resource);
static void read_slots(struct i915_wire_reader *reader);
static void read_add(struct i915_wire_reader *reader, struct video_chain *chain);
static void read_scaling(struct i915_wire_reader *reader, struct drv_rpi4_h264_scaling *scaling);
static void read_extension(struct i915_wire_reader *reader, char *name, uint32_t *version);
static void chain_free(struct video_chain *chain);
static int parameters_take(struct video_parameters *parameters, struct video_chain *chain, bool replace);
static int parameters_copy(struct video_parameters *destination, const struct video_parameters *source);
static void parameters_free(struct video_parameters *parameters);
static const struct video_pps *find_pps(const struct video_parameters *parameters, uint32_t sps_id, uint32_t pps_id);
static int pool_create(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int pool_results(struct bcm2711_vulkan_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int record_query(struct bcm2711_vulkan_session *session, uint32_t opcode, struct i915_wire_reader *reader);
static int run_decode(struct bcm2711_render_device *controller, const struct video_command *command);
static int begin_decode(struct video_session *video, const struct video_parameters *parameters, const struct video_command *command);
static int send_push(struct video_session *video, const struct video_parameters *parameters, const struct video_command *command);
static int wait_frame(struct video_session *video, struct drv_rpi4_mmal_frame *frame, const uint8_t **pixels);
static uint32_t unused_pps_id(const struct video_parameters *parameters);
static int build_unit(struct video_session *video, const struct video_parameters *parameters, const struct video_command *command, uint8_t *staging, uint32_t capacity, uint32_t *bytes);
static int append_set(uint8_t *staging, uint32_t capacity, uint32_t *used, const uint8_t *unit, size_t bytes, uint8_t *sent, size_t *sent_bytes, bool force);
static int image_target(const struct video_command *command, struct bcm2711_vulkan_resource **image, uint8_t **cpu);
static void relayout(const struct drv_rpi4_mmal_frame *frame, const uint8_t *pixels, const struct bcm2711_vulkan_resource *image, uint8_t *cpu);
static void forget_shadow(const struct video_session *owner);
static void note_decode(const struct video_session *video, uint64_t received, uint64_t now);
static void report_stats(uint64_t now);
static uint64_t average_us(uint64_t ticks, uint32_t count);
static void set_status(const struct video_command *command, int32_t status);
static void skip(struct video_session *video, const struct video_command *command, const char *reason, int error);

/*
 * Whether any session has been promised video, for the log line said once.
 */
static bool video_announced;

/* The cached copy of the last frame placed (see struct video_shadow); under the controller mutex. */
static struct video_shadow video_shadow;

/* The costs of the current window (see struct video_stats); under the controller mutex. */
static struct video_stats video_stats;

/*
 * Reports whether video decode can be offered now.
 */
bool
bcm2711_vulkan_video_available(void)
{
	bool connected;

	/* The firmware's link must be up; the decoder itself is made with each session. */
	connected = drv_rpi4_vchiq_connected();
	if (!connected)
		return false;

	/* Succeeded: the capset may promise video decode. */
	return true;
}

/*
 * Reports whether a session promised video decode in its capset.
 */
bool
bcm2711_vulkan_video_session(
	const struct bcm2711_vulkan_session *session)
{
	/* Latched once by the capset (design §14, M2). */
	return session->video;
}

/*
 * Records that a session's capset promised video decode.
 *
 * The promise is kept for the session's life: its queue family keeps the
 * decode flag and its video commands are taken even if the link breaks
 * later (decodes then fail one by one).
 */
void
bcm2711_vulkan_video_promise(
	struct bcm2711_vulkan_session *session)
{
	/* Latches the promise, and says so once a boot. */
	session->video = true;
	if (!video_announced) {
		video_announced = true;
		kern_logf("v3d: vk: capset declares H.264 video decode (VideoCore firmware decoder)\n");
	}
}

/*
 * Routes the video commands.
 *
 * A session whose capset did not promise video decode refuses them all,
 * which fails the stream (libvulkan never sends them there).
 */
int
bcm2711_vulkan_video_dispatch(
	struct bcm2711_vulkan_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Only the video range belongs here. */
	*handled = 0;
	if (opcode < GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES || opcode > GPU_OP_CMD_DECODE_VIDEO)
		return 0;
	*handled = 1;

	/* A session without the promise takes none of them. */
	if (!session->video)
		return ENOTSUP;

	/* Runs the command the opcode names. */
	(void)requested;
	switch (opcode) {
	case GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES:
		error = video_capabilities(reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_FORMAT_PROPERTIES:
		error = video_format_properties(reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_VIDEO_PROPERTIES:
		error = video_family_properties(reader, reply);
		break;
	case GPU_OP_CREATE_VIDEO_SESSION:
		error = session_create(session, reader, reply);
		break;
	case GPU_OP_DESTROY_VIDEO_SESSION:
		error = destroy_object(session, I915_VK_OBJ_VIDEO_SESSION, reader);
		break;
	case GPU_OP_GET_VIDEO_SESSION_MEMORY_REQUIREMENTS:
		error = session_requirements(session, reader, reply);
		break;
	case GPU_OP_BIND_VIDEO_SESSION_MEMORY:
		error = session_bind(session, reader, reply);
		break;
	case GPU_OP_CREATE_VIDEO_SESSION_PARAMETERS:
		error = parameters_create(session, reader, reply);
		break;
	case GPU_OP_UPDATE_VIDEO_SESSION_PARAMETERS:
		error = parameters_update(session, reader, reply);
		break;
	case GPU_OP_DESTROY_VIDEO_SESSION_PARAMETERS:
		error = destroy_object(session, I915_VK_OBJ_VIDEO_SESSION_PARAMETERS, reader);
		break;
	default:
		/* The four recorded commands. */
		error = record_command(session, opcode, reader);
		break;
	}

	/* Reports a command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran or was recorded. */
	return 0;
}

/*
 * Routes the query pool commands: a result status pool on a session that
 * promised video, and its recorded begin, end and reset.
 */
int
bcm2711_vulkan_video_query_dispatch(
	struct bcm2711_vulkan_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Only the pool commands belong here. */
	*handled = 0;
	if (opcode != GPU_OP_CREATE_QUERY_POOL &&
	    opcode != GPU_OP_DESTROY_QUERY_POOL &&
	    opcode != GPU_OP_GET_QUERY_POOL_RESULTS &&
	    opcode != GPU_OP_CMD_BEGIN_QUERY &&
	    opcode != GPU_OP_CMD_END_QUERY &&
	    opcode != GPU_OP_CMD_RESET_QUERY_POOL)
		return 0;
	*handled = 1;

	/* Without video there are no query pools, as before. */
	if (!session->video)
		return ENOTSUP;

	/* Runs or records the command. */
	(void)requested;
	if (opcode == GPU_OP_CREATE_QUERY_POOL) {
		error = pool_create(session, reader, reply);
	} else if (opcode == GPU_OP_DESTROY_QUERY_POOL) {
		error = destroy_object(session, I915_VK_OBJ_QUERY_POOL, reader);
	} else if (opcode == GPU_OP_GET_QUERY_POOL_RESULTS) {
		error = pool_results(session, reader, reply);
	} else {
		error = record_query(session, opcode, reader);
	}

	/* Reports a command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran or was recorded. */
	return 0;
}

/*
 * Reports whether a recorded opcode is one this file executes.
 */
bool
bcm2711_vulkan_video_event(
	uint32_t opcode)
{
	/* The four video commands and the three query commands. */
	if (opcode >= GPU_OP_CMD_BEGIN_VIDEO_CODING && opcode <= GPU_OP_CMD_DECODE_VIDEO)
		return true;
	if (opcode == GPU_OP_CMD_BEGIN_QUERY || opcode == GPU_OP_CMD_END_QUERY || opcode == GPU_OP_CMD_RESET_QUERY_POOL)
		return true;

	/* Succeeded: not one of them. */
	return false;
}

/*
 * Executes one recorded video or query event in the native job.
 *
 * A begin binds nothing (the decode finds its scope's begin itself); a
 * control with RESET drops the decoder's pictures; a decode decodes; an
 * end does nothing.  A query reset makes its queries unavailable; a begin
 * query names the status the next decodes of its scope write; an end
 * query makes it available.
 */
int
bcm2711_vulkan_video_run(
	struct bcm2711_render_device *controller,
	const struct bcm2711_vulkan_record *record)
{
	const struct video_command *command;
	struct video_session *video;
	struct video_pool *pool;
	uint32_t index;
	int error;
	int closed;

	/* The record is a video command's. */
	command = (const struct video_command *)record;
	error = 0;

	/* Each event has its own effect. */
	switch (record->opcode) {
	case GPU_OP_CMD_CONTROL_VIDEO_CODING:
		/* A reset drops every picture and reference, and sends the sets again. */
		video = command->begin->session->payload;
		if ((command->flags & VIDEO_CONTROL_RESET) != 0 && !video->broken) {
			/* The flush gives the buffer of the cached copy back: the copy is forgotten first. */
			forget_shadow(NULL);
			video->inflight = NULL;
			bcm2711_render_window_open(controller);
			error = drv_rpi4_mmal_decoder_flush(video->decoder);
			closed = bcm2711_render_window_close(controller);
			video->sent_sps_bytes = 0;
			video->sent_pps_bytes = 0;
			video->started = false;
			if (error != 0) {
				skip(video, NULL, "flush failed", error);
				error = 0;
			}

			/* Reports a window that did not close. */
			if (closed != 0)
				error = closed;
		}

		break;
	case GPU_OP_CMD_DECODE_VIDEO:
		error = run_decode(controller, command);
		break;
	case GPU_OP_CMD_RESET_QUERY_POOL:
		/* Every query of the range becomes unavailable. */
		pool = command->pool->payload;
		for (index = command->query; index < command->query + command->query_count; index++) {
			pool->available[index] = 0;
			pool->status[index] = 0;
		}

		break;
	case GPU_OP_CMD_BEGIN_QUERY:
		/* The decodes inside write this query's status; until one does it is complete. */
		pool = command->pool->payload;
		pool->status[command->query] = VIDEO_STATUS_COMPLETE;
		break;
	case GPU_OP_CMD_END_QUERY:
		/* The query is available. */
		pool = command->pool->payload;
		pool->available[command->query] = 1;
		break;
	default:
		/* A begin or an end of a coding scope. */
		break;
	}

	/* Reports an event that could not run. */
	if (error != 0)
		return error;

	/* Succeeded: the event ran. */
	return 0;
}

/*
 * Lays out an NV12 image: rows of the width rounded up to 32 bytes, the
 * luma plane over the height rounded up to 16 rows, then the interleaved
 * chroma plane of half as many rows.  This is the layout the firmware's
 * decoder writes, so a frame lands in the image as it comes.
 */
void
bcm2711_vulkan_video_nv12_layout(
	uint32_t width,
	uint32_t height,
	uint32_t *pitch,
	uint64_t *chroma,
	uint64_t *bytes)
{
	uint32_t rows;

	/* The pitch, the rounded height, the chroma plane after the luma rows. */
	*pitch = (width + 31U) & ~31U;
	rows = (height + 15U) & ~15U;
	*chroma = (uint64_t)*pitch * rows;
	*bytes = *chroma + (uint64_t)*pitch * (rows / 2U);
}

/*
 * Copies rows of a decoded NV12 image to a buffer from the image's cached
 * copy, when it has one.
 *
 * offset is the first row's byte offset in the image's layout (in either
 * plane), bytes the bytes a row and rows the rows; destination's rows are
 * destination_pitch apart.  The copy comes from the frame last placed in
 * the image while it is still in the decoder's memory, corrected as the
 * image's own bytes were, and only when every row lies in the part the
 * frame covered.  Returns false, copying nothing, otherwise; the caller
 * then copies from the image.  The caller holds the controller mutex.
 */
bool
bcm2711_vulkan_video_readback(
	const struct bcm2711_vulkan_resource *image,
	uint64_t offset,
	uint64_t bytes,
	uint32_t rows,
	uint8_t *destination,
	uint64_t destination_pitch)
{
	const uint8_t *plane;
	uint64_t chroma;
	uint64_t total;
	uint64_t base;
	uint64_t row;
	uint64_t column;
	uint32_t pitch;
	uint32_t plane_rows;
	uint32_t index;

	/* Only the image of the last frame, while its copy lasts. */
	if (video_shadow.owner == NULL || video_shadow.image != image)
		return false;

	/* The plane the rows are in, its start, and the rows of it the frame covered. */
	bcm2711_vulkan_video_nv12_layout(image->width, image->height, &pitch, &chroma, &total);
	if (offset >= chroma) {
		base = chroma;
		plane = video_shadow.chroma;
		plane_rows = video_shadow.rows / 2U;
	} else {
		base = 0;
		plane = video_shadow.luma;
		plane_rows = video_shadow.rows;
	}

	/* The first row and column in the plane; every row must lie in the covered part. */
	row = (offset - base) / pitch;
	column = (offset - base) % pitch;
	if (bytes > video_shadow.width || column > video_shadow.width - bytes)
		return false;
	if (rows > plane_rows || row > plane_rows - rows)
		return false;

	/* Each row from the copy, corrected. */
	for (index = 0; index < rows; index++) {
		drv_rpi4_mmal_copy_samples(destination + (size_t)index * destination_pitch,
					   plane + (size_t)(row + index) * video_shadow.pitch + column,
					   (size_t)bytes);
	}

	/* Succeeded: the rows came from the cached copy. */
	return true;
}

/*
 * Adds the time of an NV12 image's readback to the cost line: from the
 * cached copy or from the image's uncached memory.
 */
void
bcm2711_vulkan_video_readback_done(
	bool cached,
	uint64_t ticks)
{
	/* The readback's kind and time. */
	if (cached) {
		video_stats.cached_readbacks++;
		video_stats.cached_ticks += ticks;
	} else {
		video_stats.uncached_readbacks++;
		video_stats.uncached_ticks += ticks;
	}
}

/*
 * vkGetPhysicalDeviceVideoCapabilitiesKHR: [physical][present]{profile}
 * [present][shape: type, link...] -> [result][present]{capabilities}.
 */
static int
video_capabilities(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint32_t types[VIDEO_MAX_CHAIN];
	uint32_t count;
	uint32_t idc;
	uint64_t present;
	int32_t result;

	/* Skips the physical device and reads the profile, which decides the result. */
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	result = VIDEO_ERROR_OPERATION;
	if (present != 0U)
		result = read_profile(reader, &idc);

	/* Reads the shape of the output: the capabilities record and what is chained to it. */
	count = 0U;
	present = drv_i915_wire_read_u64(reader);
	while (present != 0U && reader->error == 0) {
		if (count >= VIDEO_MAX_CHAIN) {
			reader->error = 1;
			break;
		}

		/* The type of the next structure in the chain. */
		types[count] = drv_i915_wire_read_u32(reader);
		count++;
		present = drv_i915_wire_read_u64(reader);
	}

	/* Refuses a malformed chain. */
	if (reader->error != 0)
		return EINVAL;
	if (count == 0U || types[0] != VIDEO_TYPE_CAPABILITIES)
		return EINVAL;

	/* A profile the decoder does not take replies its reason only. */
	if (result != 0) {
		drv_i915_wire_reply_u32(reply, (uint32_t)result);
		return 0;
	}

	/* Replies the result and the records in the shape's order. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	reply_capabilities(reply, types, count);

	/* Succeeded: the capabilities are reported. */
	return 0;
}

/* Replies one output record of the capabilities' shape and, nested before its fields, the rest. */
static void
reply_capabilities(
	struct i915_wire_writer *reply,
	const uint32_t *types,
	uint32_t count)
{
	char name[VK_MAX_EXTENSION_NAME_SIZE];

	/* The record's type, then the link to the rest of the shape. */
	drv_i915_wire_reply_u32(reply, types[0]);
	if (count > 1U) {
		drv_i915_wire_reply_u64(reply, 1U);
		reply_capabilities(reply, types + 1, count - 1U);
	} else {
		drv_i915_wire_reply_u64(reply, 0U);
	}

	/* The record's fields. */
	switch (types[0]) {
	case VIDEO_TYPE_CAPABILITIES:
		/* Separate reference images; offsets of 32 bytes, any size; 16x16 to 1920x1088; the slot limits. */
		drv_i915_wire_reply_u32(reply, VIDEO_SEPARATE_REFERENCE_IMAGES);
		drv_i915_wire_reply_u64(reply, 32U);
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u32(reply, VIDEO_MB);
		drv_i915_wire_reply_u32(reply, VIDEO_MB);
		drv_i915_wire_reply_u32(reply, VIDEO_MB);
		drv_i915_wire_reply_u32(reply, VIDEO_MB);
		drv_i915_wire_reply_u32(reply, VIDEO_MAX_WIDTH);
		drv_i915_wire_reply_u32(reply, VIDEO_MAX_HEIGHT);
		drv_i915_wire_reply_u32(reply, VIDEO_MAX_DPB_SLOTS);
		drv_i915_wire_reply_u32(reply, VIDEO_MAX_REFERENCES);

		/* The H.264 header name, padded to its array, and version. */
		kern_memset(name, 0, sizeof(name));
		kern_memcpy(name, VIDEO_STD_NAME, sizeof(VIDEO_STD_NAME));
		drv_i915_wire_reply_u64(reply, VK_MAX_EXTENSION_NAME_SIZE);
		drv_i915_wire_reply_bytes(reply, name, sizeof(name));
		drv_i915_wire_reply_u32(reply, VIDEO_STD_VERSION);
		break;
	case VIDEO_TYPE_DECODE_CAPABILITIES:
		/* The output picture is the reference picture. */
		drv_i915_wire_reply_u32(reply, VIDEO_DPB_AND_OUTPUT_COINCIDE);
		break;
	case VIDEO_TYPE_H264_CAPABILITIES:
		/* Level 4.2 (1080p60) and no field offset (progressive only). */
		drv_i915_wire_reply_u32(reply, VIDEO_LEVEL_4_2);
		drv_i915_wire_reply_u32(reply, 0U);
		drv_i915_wire_reply_u32(reply, 0U);
		break;
	default:
		/* A record the executor does not know cannot be answered. */
		reply->error = 1;
		break;
	}
}

/*
 * vkGetPhysicalDeviceVideoFormatPropertiesKHR: [physical][present]{format
 * info and its profile list}[present][capacity][array] -> [result][present]
 * [count][array]{format}.
 */
static int
video_format_properties(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct video_chain chain;
	uint64_t present;
	uint32_t type;
	uint32_t usage;
	uint32_t video_usage;
	int32_t result;

	/* Reads the format info: its type, its chain (the profile list) and the usage, then the capacity. */
	kern_memset(&chain, 0, sizeof(chain));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	if (present != 1U || type != VIDEO_TYPE_FORMAT_INFO)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	usage = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	chain_free(&chain);
	if (reader->error != 0)
		return EINVAL;

	/* The profiles must all be H.264 decode; usages include picture readback. */
	result = 0;
	video_usage = BCM2711_VIDEO_USAGE_DECODE_DST | BCM2711_VIDEO_USAGE_DECODE_DPB | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (!chain.has_profile_list || chain.profile_count == 0U) {
		result = VIDEO_ERROR_OPERATION;
	} else if (!chain.profiles_supported) {
		result = chain.profile_error;
	} else if ((usage & ~video_usage) != 0U) {
		result = VIDEO_ERROR_IMAGE_USAGE;
	}

	/* Reports a format the decoder does not offer. */
	if (result != 0) {
		drv_i915_wire_reply_u32(reply, (uint32_t)result);
		return 0;
	}

	/* Replies the one format: NV12, identity swizzle, 2D, optimal (linear rows), decode output, reference and readback. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u32(reply, 1U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u32(reply, VIDEO_TYPE_FORMAT_PROPERTIES);
	drv_i915_wire_reply_u64(reply, 0U);
	drv_i915_wire_reply_u32(reply, BCM2711_VIDEO_FORMAT_NV12);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u32(reply, VK_IMAGE_TYPE_2D);
	drv_i915_wire_reply_u32(reply, VK_IMAGE_TILING_OPTIMAL);
	drv_i915_wire_reply_u32(reply, video_usage);

	/* Succeeded: the format is reported. */
	return 0;
}

/*
 * The codec operations of each queue family: [physical][count] ->
 * [present][count]{operations}.  The one family decodes H.264.
 */
static int
video_family_properties(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint32_t count;

	/* Skips the physical device and reads the family count, which is the one family. */
	(void)drv_i915_wire_read_u64(reader);
	count = drv_i915_wire_read_u32(reader);
	if (reader->error != 0 || count != 1U)
		return EINVAL;

	/* Replies H.264 decode for family 0. */
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u32(reply, 1U);
	drv_i915_wire_reply_u32(reply, VIDEO_OP_DECODE_H264);

	/* Succeeded: the family's codec operations are reported. */
	return 0;
}

/*
 * vkCreateVideoSessionKHR: [device][present]{create info}[allocator][present]
 * [identity] -> [result][present][identity].
 *
 * The firmware's decoder is made here, so a session that cannot decode
 * fails at once (design §14, M8).
 */
static int
session_create(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct video_session *video;
	struct video_chain chain;
	struct bcm2711_vulkan_object *device;
	struct bcm2711_vulkan_object *object;
	char name[VK_MAX_EXTENSION_NAME_SIZE];
	uint64_t device_id;
	uint64_t present;
	uint64_t identity;
	uint32_t type;
	uint32_t family;
	uint32_t flags;
	uint32_t picture_format;
	uint32_t reference_format;
	uint32_t width;
	uint32_t height;
	uint32_t slots;
	uint32_t references;
	uint32_t version;
	uint32_t profile_idc;
	int32_t result;
	bool available;
	int different;
	int error;
	int released;

	/* Reads the create info: [type][chain][family][flags][profile][formats and limits][header]. */
	kern_memset(&chain, 0, sizeof(chain));
	kern_memset(name, 0, sizeof(name));
	device_id = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	if (present != 1U || type != VIDEO_TYPE_SESSION_CREATE)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	family = drv_i915_wire_read_u32(reader);
	flags = drv_i915_wire_read_u32(reader);
	result = VIDEO_ERROR_OPERATION;
	profile_idc = 0U;
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		result = read_profile(reader, &profile_idc);
	picture_format = drv_i915_wire_read_u32(reader);
	width = drv_i915_wire_read_u32(reader);
	height = drv_i915_wire_read_u32(reader);
	reference_format = drv_i915_wire_read_u32(reader);
	slots = drv_i915_wire_read_u32(reader);
	references = drv_i915_wire_read_u32(reader);
	version = 0U;
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		read_extension(reader, name, &version);

	/* Skips pAllocator and the output's present word, and reads the identity. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	chain_free(&chain);
	if (reader->error != 0 || identity == 0)
		return EINVAL;
	device = bcm2711_vulkan_object_find(session, I915_VK_OBJ_DEVICE, device_id);
	if (device == NULL)
		return EINVAL;

	/* Checks the session against what the decoder takes; the first reason is the result. */
	different = kern_strcmp(name, VIDEO_STD_NAME);
	if (result != 0) {
		/* The profile's own reason. */
	} else if (family != 0U) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	} else if (flags != 0U) {
		result = VK_ERROR_FEATURE_NOT_PRESENT;
	} else if (picture_format != BCM2711_VIDEO_FORMAT_NV12 || reference_format != BCM2711_VIDEO_FORMAT_NV12) {
		result = VIDEO_ERROR_FORMAT;
	} else if (width == 0U || height == 0U || width > VIDEO_MAX_WIDTH || height > VIDEO_MAX_HEIGHT) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	} else if (slots > VIDEO_MAX_DPB_SLOTS || references > VIDEO_MAX_REFERENCES) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	} else if (different != 0 || version > VIDEO_STD_VERSION) {
		result = VIDEO_ERROR_STD_VERSION;
	}

	/* Reports a profile or header version the decoder does not take. */
	if (result != 0) {
		drv_i915_wire_reply_u32(reply, (uint32_t)result);
		return 0;
	}

	/* Allocates the session with its limits. */
	video = kern_calloc(1U, sizeof(*video));
	if (video == NULL) {
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
		return 0;
	}

	/* The session's profile and limits. */
	video->profile_idc = profile_idc;
	video->width = width;
	video->height = height;
	video->max_dpb_slots = slots;
	video->max_references = references;

	/* The firmware's decoder of the session's largest picture; none means no session. */
	error = ENODEV;
	available = bcm2711_vulkan_video_available();
	if (available)
		error = drv_rpi4_mmal_decoder_create(width, height, VIDEO_INPUT_BYTES, &video->decoder);
	if (error != 0) {
		kern_free(video);
		kern_logf("v3d: video: no firmware decoder for a %ux%u session (%d)\n", (unsigned)width, (unsigned)height, error);
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
		return 0;
	}

	/* The session keeps its device. */
	error = bcm2711_vulkan_object_retain(device);
	if (error != 0) {
		drv_rpi4_mmal_decoder_destroy(video->decoder);
		kern_free(video);
		return error;
	}

	/* The session keeps its device. */
	video->owner.device = device;

	/* Publishes the session under its identity. */
	error = bcm2711_vulkan_object_publish(session, I915_VK_OBJ_VIDEO_SESSION, identity, video, release_session, &object);
	if (error != 0) {
		released = release_session(session, video);
		if (released != 0)
			return released;
		if (error != ENOMEM)
			return error;
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
		return 0;
	}

	/* Replies VK_SUCCESS and the identity. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the session exists. */
	return 0;
}

/*
 * vkGetVideoSessionMemoryRequirementsKHR: [device][session][present][capacity]
 * [array] -> [result][present][count][array]{binding}.
 *
 * The decoder's memory is the firmware's; one page is asked for so the
 * application's bind follows its ordinary path.
 */
static int
session_requirements(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct bcm2711_vulkan_object *object;
	uint64_t identity;
	uint32_t capacity;

	/* Skips the device, reads the session and the capacity. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	capacity = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An unknown session, or no room for the one binding, is refused. */
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION, identity);
	if (object == NULL || capacity < 1U) {
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
		return 0;
	}

	/* One binding: its type, no chain, index 0, one page, page alignment, memory type 0. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u32(reply, 1U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u32(reply, VIDEO_TYPE_MEMORY_REQUIREMENTS);
	drv_i915_wire_reply_u64(reply, 0U);
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, VIDEO_BIND_BYTES);
	drv_i915_wire_reply_u64(reply, VIDEO_BIND_BYTES);
	drv_i915_wire_reply_u32(reply, 1U);

	/* Succeeded: the binding is reported. */
	return 0;
}

/*
 * vkBindVideoSessionMemoryKHR: [device][session][count][array]{bind} -> [result].
 *
 * The binding is checked and not kept: nothing reads the page.
 */
static int
session_bind(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct bcm2711_vulkan_object *object;
	struct bcm2711_vulkan_object *bound;
	struct video_chain chain;
	uint64_t identity;
	uint64_t count;
	uint64_t item;
	uint64_t memory;
	uint32_t type;
	uint32_t index;
	int32_t result;

	/* Skips the device, reads the session and the binding count. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Reads every binding: [type][chain][index][memory][offset][size]; only binding 0 of real memory is taken. */
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION, identity);
	result = 0;
	if (object == NULL)
		result = VK_ERROR_INITIALIZATION_FAILED;
	for (item = 0U; item < count && reader->error == 0; item++) {
		kern_memset(&chain, 0, sizeof(chain));
		type = drv_i915_wire_read_u32(reader);
		if (type != VIDEO_TYPE_BIND_MEMORY)
			reader->error = 1;
		read_chain(reader, &chain, 0U);
		chain_free(&chain);
		index = drv_i915_wire_read_u32(reader);
		memory = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u64(reader);
		bound = bcm2711_vulkan_object_find(session, I915_VK_OBJ_MEMORY, memory);
		if (index != 0U || bound == NULL)
			result = VK_ERROR_INITIALIZATION_FAILED;
	}

	/* Refuses a malformed request. */
	if (reader->error != 0)
		return EINVAL;

	/* Replies the result. */
	drv_i915_wire_reply_u32(reply, (uint32_t)result);

	/* Succeeded: the command was decoded; the reply carries its result. */
	return 0;
}

/*
 * vkCreateVideoSessionParametersKHR: [device][present]{create info: chain
 * (H.264 create and add), flags, template, session}[allocator][present]
 * [identity] -> [result][present][identity].
 */
static int
parameters_create(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct video_parameters *parameters;
	struct video_parameters *source;
	struct bcm2711_vulkan_object *device;
	struct bcm2711_vulkan_object *owner;
	struct bcm2711_vulkan_object *template_object;
	struct bcm2711_vulkan_object *object;
	struct video_chain chain;
	uint64_t device_id;
	uint64_t present;
	uint64_t identity;
	uint64_t template_identity;
	uint64_t session_identity;
	uint32_t type;
	int32_t result;
	int error;
	int released;

	/* Reads the create info: [type][chain][flags][template][session], then the identity. */
	kern_memset(&chain, 0, sizeof(chain));
	device_id = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	if (present != 1U || type != VIDEO_TYPE_PARAMETERS_CREATE)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	(void)drv_i915_wire_read_u32(reader);
	template_identity = drv_i915_wire_read_u64(reader);
	session_identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	device = bcm2711_vulkan_object_find(session, I915_VK_OBJ_DEVICE, device_id);
	if (reader->error != 0 || device == NULL || identity == 0) {
		chain_free(&chain);
		return EINVAL;
	}

	/* The session must exist and the H.264 create record be given; a template must be of the same session. */
	result = 0;
	source = NULL;
	template_object = NULL;
	owner = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION, session_identity);
	if (template_identity != 0U)
		template_object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION_PARAMETERS, template_identity);
	if (template_object != NULL)
		source = template_object->payload;
	if (owner == NULL) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	} else if (!chain.has_h264_create || chain.add_error != 0) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	} else if (template_identity != 0U && (source == NULL || source->owner.parent != owner)) {
		result = VK_ERROR_INITIALIZATION_FAILED;
	}

	/* Reports a request the object cannot serve. */
	if (result != 0) {
		chain_free(&chain);
		drv_i915_wire_reply_u32(reply, (uint32_t)result);
		return 0;
	}

	/* Allocates the object, which keeps its device and its session. */
	parameters = kern_calloc(1U, sizeof(*parameters));
	if (parameters == NULL) {
		chain_free(&chain);
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
		return 0;
	}

	/* Retains the device and the owner the object lives under. */
	error = bcm2711_vulkan_object_retain(device);
	if (error == 0) {
		parameters->owner.device = device;
		error = bcm2711_vulkan_object_retain(owner);
		if (error == 0)
			parameters->owner.parent = owner;
	}

	/* The capacity the application asked for. */
	parameters->max_sps = chain.max_sps;
	parameters->max_pps = chain.max_pps;

	/* Copies the template's sets, then takes the new ones, which replace sets of the same key. */
	if (error == 0 && source != NULL)
		error = parameters_copy(parameters, source);
	if (error == 0)
		error = parameters_take(parameters, &chain, true);
	chain_free(&chain);

	/* Publishes the object under its identity. */
	if (error == 0)
		error = bcm2711_vulkan_object_publish(session, I915_VK_OBJ_VIDEO_SESSION_PARAMETERS, identity, parameters, release_parameters, &object);
	if (error != 0) {
		released = release_parameters(session, parameters);
		if (released != 0)
			return released;
		if (error == EEXIST || error == ECANCELED)
			return error;
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		if (error == EINVAL)
			result = VK_ERROR_INITIALIZATION_FAILED;
		drv_i915_wire_reply_u32(reply, (uint32_t)result);
		return 0;
	}

	/* Replies VK_SUCCESS and the identity. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the object exists. */
	return 0;
}

/*
 * vkUpdateVideoSessionParametersKHR: [device][parameters][present]{update
 * info: chain (add), sequence count} -> [result].  The update adds its
 * sets whole or not at all.
 */
static int
parameters_update(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct bcm2711_vulkan_object *object;
	struct video_parameters *parameters;
	struct video_chain chain;
	uint64_t identity;
	uint64_t present;
	uint32_t type;
	uint32_t sequence;
	int32_t result;
	int error;

	/* Reads the object and the update info: [type][chain][sequence count]. */
	kern_memset(&chain, 0, sizeof(chain));
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	if (present != 1U || type != VIDEO_TYPE_PARAMETERS_UPDATE)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	sequence = drv_i915_wire_read_u32(reader);
	if (reader->error != 0) {
		chain_free(&chain);
		return EINVAL;
	}

	/* The object must exist and the update be the next one; a malformed set refuses it. */
	result = 0;
	parameters = NULL;
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION_PARAMETERS, identity);
	if (object != NULL)
		parameters = object->payload;
	if (parameters == NULL || sequence != parameters->update_sequence + 1U || chain.add_error != 0)
		result = VK_ERROR_INITIALIZATION_FAILED;

	/* Adds the sets; a key already held refuses the whole update. */
	if (result == 0) {
		error = parameters_take(parameters, &chain, false);
		if (error == ENOMEM) {
			result = VK_ERROR_OUT_OF_HOST_MEMORY;
		} else if (error != 0) {
			result = VK_ERROR_INITIALIZATION_FAILED;
		}
	}

	/* Records the update's sequence number when it is taken. */
	chain_free(&chain);
	if (result == 0)
		parameters->update_sequence = sequence;

	/* Replies the result. */
	drv_i915_wire_reply_u32(reply, (uint32_t)result);

	/* Succeeded: the command was decoded; the reply carries its result. */
	return 0;
}

/*
 * vkDestroyVideoSessionKHR, vkDestroyVideoSessionParametersKHR and
 * vkDestroyQueryPool: [device][object][allocator], no reply body.  The
 * registry lets go of the object; recorded commands that name it keep it.
 */
static int
destroy_object(
	struct bcm2711_vulkan_session *session,
	enum i915_vk_object_kind kind,
	struct i915_wire_reader *reader)
{
	struct bcm2711_vulkan_object *object;
	uint64_t identity;
	int error;

	/* Skips the device, reads the object and skips pAllocator. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A null or unknown object is nothing. */
	object = bcm2711_vulkan_object_find(session, kind, identity);
	if (identity == 0 || object == NULL)
		return 0;
	error = bcm2711_vulkan_object_remove(session, kind, identity);
	if (error != 0)
		return error;

	/* Succeeded: the identity is gone. */
	return 0;
}

/* Frees a session after its last owner: the firmware decoder, then the device edge. */
static int
release_session(
	struct bcm2711_vulkan_session *session,
	void *payload)
{
	struct video_session *video;
	int error;

	/* The cached copy in the decoder's memory and the decoder go first; its memory stays if a bulk may still move it. */
	(void)session;
	video = payload;
	forget_shadow(video);
	if (video->decoder != NULL)
		drv_rpi4_mmal_decoder_destroy(video->decoder);
	error = 0;
	if (video->owner.device != NULL)
		error = bcm2711_vulkan_object_release(video->owner.device);
	kern_free(video);
	if (error != 0)
		return error;

	/* Succeeded: the session is gone. */
	return 0;
}

/* Frees a parameters object after its last owner, with its sets and its edges. */
static int
release_parameters(
	struct bcm2711_vulkan_session *session,
	void *payload)
{
	struct video_parameters *parameters;
	int error;
	int released;

	/* The sets, then the session and device edges. */
	(void)session;
	parameters = payload;
	parameters_free(parameters);
	error = 0;
	if (parameters->owner.parent != NULL)
		error = bcm2711_vulkan_object_release(parameters->owner.parent);
	if (parameters->owner.device != NULL) {
		released = bcm2711_vulkan_object_release(parameters->owner.device);
		if (error == 0)
			error = released;
	}

	/* Frees the object. */
	kern_free(parameters);
	if (error != 0)
		return error;

	/* Succeeded: the object is gone. */
	return 0;
}

/* Frees a query pool after its last owner. */
static int
release_pool(
	struct bcm2711_vulkan_session *session,
	void *payload)
{
	struct video_pool *pool;
	int error;

	/* The device edge, then the pool. */
	(void)session;
	pool = payload;
	error = 0;
	if (pool->owner.device != NULL)
		error = bcm2711_vulkan_object_release(pool->owner.device);
	kern_free(pool);
	if (error != 0)
		return error;

	/* Succeeded: the pool is gone. */
	return 0;
}

/*
 * Records one of the four video commands into its command buffer.
 *
 * The command is decoded whole, then the objects it names are retained;
 * a use the executor cannot run marks the recording failed (reported at
 * vkEndCommandBuffer) instead of failing the stream.
 */
static int
record_command(
	struct bcm2711_vulkan_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	struct bcm2711_vulkan_object *object;
	struct bcm2711_vulkan_command_buffer *command;
	struct video_command *video;
	uint64_t identity;
	int error;
	int released;

	/* The recording primary. */
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_COMMAND_BUFFER, identity);
	if (object == NULL)
		return EINVAL;
	command = object->payload;
	if (command->pending != 0 || command->state != BCM2711_VULKAN_COMMAND_RECORDING)
		return EBUSY;

	/* The node, with the common record first. */
	video = kern_calloc(1U, sizeof(*video));
	if (video == NULL)
		return ENOMEM;
	video->record.opcode = opcode;
	video->record.node.release = release_command;

	/* Decodes the whole command and retains what it names. */
	error = decode_command(session, opcode, reader, video);
	if (error == EINVAL && reader->error != 0) {
		(void)release_command(&video->record.node);
		return EINVAL;
	}

	/* A command buffer that failed recording takes nothing more. */
	if (command->recording_error != 0) {
		released = release_command(&video->record.node);
		if (released != 0)
			return released;
		return 0;
	}

	/* A decode, a control and an end belong to an open coding scope; a begin opens one. */
	if (error == 0 && opcode != GPU_OP_CMD_BEGIN_VIDEO_CODING) {
		video->begin = open_scope(command);
		if (video->begin == NULL)
			error = EINVAL;
	}

	/* A decode writes its status into the begin query open around it, if any. */
	if (error == 0 && opcode == GPU_OP_CMD_DECODE_VIDEO)
		video->status = open_query(command);
	if (error == 0 && command->render_open)
		error = ENOTSUP;
	if (error == 0 && sizeof(*video) > BCM2711_VULKAN_RECORD_BYTES - command->recorded_bytes)
		error = ENOMEM;
	if (error != 0) {
		command->recording_error = error;
		released = release_command(&video->record.node);
		if (released != 0)
			return released;
		return 0;
	}

	/* Appends the node to the primary's ordered list. */
	if (command->last == NULL) {
		command->first = &video->record.node;
	} else {
		command->last->next = &video->record.node;
	}

	/* The record is the command buffer's last. */
	command->last = &video->record.node;
	command->recorded_bytes += sizeof(*video);

	/* Succeeded: the command is recorded. */
	return 0;
}

/*
 * Decodes one video command's structure and retains the objects it names.
 * Returns EINVAL with the reader's error set for malformed framing, another
 * error for a use the executor refuses.
 */
static int
decode_command(
	struct bcm2711_vulkan_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader,
	struct video_command *command)
{
	struct video_chain chain;
	struct video_resource setup;
	uint64_t present;
	uint64_t session_id;
	uint64_t parameters_id;
	uint64_t buffer_id;
	uint32_t type;
	int error;

	/* Reads the structure's presence, its type and its chain. */
	kern_memset(&chain, 0, sizeof(chain));
	present = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	if (present != 1U)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	session_id = 0;
	parameters_id = 0;
	buffer_id = 0;

	/* Reads the fields of the command's structure. */
	switch (opcode) {
	case GPU_OP_CMD_BEGIN_VIDEO_CODING:
		/* [flags][session][parameters][count][array]{slot}. */
		if (type != VIDEO_TYPE_BEGIN)
			reader->error = 1;
		command->flags = drv_i915_wire_read_u32(reader);
		session_id = drv_i915_wire_read_u64(reader);
		parameters_id = drv_i915_wire_read_u64(reader);
		read_slots(reader);
		break;
	case GPU_OP_CMD_END_VIDEO_CODING:
		/* [flags]. */
		if (type != VIDEO_TYPE_END)
			reader->error = 1;
		command->flags = drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_CONTROL_VIDEO_CODING:
		/* [flags]. */
		if (type != VIDEO_TYPE_CONTROL)
			reader->error = 1;
		command->flags = drv_i915_wire_read_u32(reader);
		break;
	default:
		/* vkCmdDecodeVideoKHR: [flags][buffer][offset][range][resource][present]{setup}[count][array]{slot}. */
		if (type != VIDEO_TYPE_DECODE)
			reader->error = 1;
		command->flags = drv_i915_wire_read_u32(reader);
		buffer_id = drv_i915_wire_read_u64(reader);
		command->offset = drv_i915_wire_read_u64(reader);
		command->range = drv_i915_wire_read_u64(reader);
		read_resource(reader, &command->destination);
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U)
			read_slot(reader, &setup);
		read_slots(reader);
		command->has_picture = chain.has_picture;
		command->picture_flags = chain.picture_flags;
		command->sps_id = chain.sps_id;
		command->pps_id = chain.pps_id;
		command->frame_num = chain.frame_num;
		command->poc = chain.poc;
		break;
	}

	/* Refuses a malformed record. */
	chain_free(&chain);
	if (reader->error != 0)
		return EINVAL;

	/* Retains the session and parameters of a begin, the bitstream and output of a decode. */
	error = retain_command(session, command, NULL, session_id, parameters_id, buffer_id, command->destination.view, 0);
	if (error != 0)
		return error;

	/* A decode needs its H.264 picture. */
	if (opcode == GPU_OP_CMD_DECODE_VIDEO && !command->has_picture)
		return EINVAL;

	/* Succeeded: the command is decoded and its objects retained. */
	return 0;
}

/* Finds and retains each object a recorded command names; zero identities name nothing. */
static int
retain_command(
	struct bcm2711_vulkan_session *session,
	struct video_command *command,
	struct bcm2711_vulkan_object *device,
	uint64_t session_id,
	uint64_t parameters_id,
	uint64_t buffer_id,
	uint64_t view_id,
	uint64_t pool_id)
{
	struct bcm2711_vulkan_object *object;
	int error;

	/* Each identity, if given, must name an object of its kind; the node keeps it. */
	(void)device;
	if (session_id != 0) {
		object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION, session_id);
		if (object == NULL)
			return EINVAL;
		error = bcm2711_vulkan_object_retain(object);
		if (error != 0)
			return error;
		command->session = object;
	}

	/* The parameters object the scope uses. */
	if (parameters_id != 0) {
		object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_VIDEO_SESSION_PARAMETERS, parameters_id);
		if (object == NULL)
			return EINVAL;
		error = bcm2711_vulkan_object_retain(object);
		if (error != 0)
			return error;
		command->parameters = object;
	}

	/* The bitstream buffer of a decode. */
	if (buffer_id != 0) {
		object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_BUFFER, buffer_id);
		if (object == NULL)
			return EINVAL;
		error = bcm2711_vulkan_object_retain(object);
		if (error != 0)
			return error;
		command->buffer = object;
	}

	/* The output picture's view of a decode. */
	if (view_id != 0) {
		object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE_VIEW, view_id);
		if (object == NULL)
			return EINVAL;
		error = bcm2711_vulkan_object_retain(object);
		if (error != 0)
			return error;
		command->view = object;
	}

	/* The query pool of a query record. */
	if (pool_id != 0) {
		object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_QUERY_POOL, pool_id);
		if (object == NULL)
			return EINVAL;
		error = bcm2711_vulkan_object_retain(object);
		if (error != 0)
			return error;
		command->pool = object;
	}

	/* A begin needs a session; the parameters may come later (a reset-only scope). */
	if (command->record.opcode == GPU_OP_CMD_BEGIN_VIDEO_CODING && command->session == NULL)
		return EINVAL;

	/* Succeeded: every named object is retained. */
	return 0;
}

/* Releases what a recorded command retained, then the node. */
static int
release_command(
	struct bcm2711_vulkan_command_node *node)
{
	struct video_command *command;
	int error;
	int released;

	/* Each retained object, keeping the first failure. */
	command = (struct video_command *)node;
	error = 0;
	if (command->session != NULL)
		error = bcm2711_vulkan_object_release(command->session);
	if (command->parameters != NULL) {
		released = bcm2711_vulkan_object_release(command->parameters);
		if (error == 0)
			error = released;
	}

	/* The bitstream buffer. */
	if (command->buffer != NULL) {
		released = bcm2711_vulkan_object_release(command->buffer);
		if (error == 0)
			error = released;
	}

	/* The output picture's view. */
	if (command->view != NULL) {
		released = bcm2711_vulkan_object_release(command->view);
		if (error == 0)
			error = released;
	}

	/* The query pool. */
	if (command->pool != NULL) {
		released = bcm2711_vulkan_object_release(command->pool);
		if (error == 0)
			error = released;
	}

	/* Frees the node. */
	kern_free(command);
	if (error != 0)
		return error;

	/* Succeeded: the node and its edges are gone. */
	return 0;
}

/* Finds the begin of the coding scope open at the end of a recording, or NULL. */
static const struct video_command *
open_scope(
	const struct bcm2711_vulkan_command_buffer *command)
{
	const struct bcm2711_vulkan_command_node *node;
	const struct bcm2711_vulkan_record *record;
	const struct video_command *begin;

	/* The last begin, cleared by any end after it. */
	begin = NULL;
	for (node = command->first; node != NULL; node = node->next) {
		record = (const struct bcm2711_vulkan_record *)node;
		if (record->opcode == GPU_OP_CMD_BEGIN_VIDEO_CODING)
			begin = (const struct video_command *)record;
		if (record->opcode == GPU_OP_CMD_END_VIDEO_CODING)
			begin = NULL;
	}

	/* Succeeded: the open scope's begin, if any. */
	return begin;
}

/* Finds the begin query open at the end of a recording (not ended since), or NULL. */
static const struct video_command *
open_query(
	const struct bcm2711_vulkan_command_buffer *command)
{
	const struct bcm2711_vulkan_command_node *node;
	const struct bcm2711_vulkan_record *record;
	const struct video_command *query;

	/* The last begin query, cleared by an end query of the same pool and query. */
	query = NULL;
	for (node = command->first; node != NULL; node = node->next) {
		record = (const struct bcm2711_vulkan_record *)node;
		if (record->opcode == GPU_OP_CMD_BEGIN_QUERY)
			query = (const struct video_command *)record;
		if (record->opcode == GPU_OP_CMD_END_QUERY &&
		    query != NULL &&
		    ((const struct video_command *)record)->pool == query->pool &&
		    ((const struct video_command *)record)->query == query->query)
			query = NULL;
	}

	/* Succeeded: the open begin query, if any. */
	return query;
}

/*
 * Reads a chain: an absent link ends it; a present one carries a structure
 * whose own chain comes before its fields.
 */
static void
read_chain(
	struct i915_wire_reader *reader,
	struct video_chain *chain,
	uint32_t depth)
{
	uint64_t present;
	uint32_t type;

	/* An absent link ends the chain. */
	present = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || present == 0U)
		return;

	/* Refuses a chain deeper than any the library writes. */
	if (depth >= VIDEO_MAX_CHAIN) {
		reader->error = 1;
		return;
	}

	/* The linked structure: its type, its own chain, then its fields. */
	type = drv_i915_wire_read_u32(reader);
	read_chain(reader, chain, depth + 1U);
	read_fields(reader, type, chain, depth);
}

/* Reads the fields of one chained structure into the chain's record. */
static void
read_fields(
	struct i915_wire_reader *reader,
	uint32_t type,
	struct video_chain *chain,
	uint32_t depth)
{
	struct video_chain nested;
	uint64_t present;
	uint64_t count;
	uint64_t item;
	uint32_t value;
	int32_t result;

	/* Reads each chained type the library writes; any other fails the stream. */
	switch (type) {
	case VIDEO_TYPE_H264_PROFILE:
		/* The H.264 profile and the picture layout. */
		chain->has_h264_profile = true;
		chain->profile_idc = drv_i915_wire_read_u32(reader);
		chain->picture_layout = drv_i915_wire_read_u32(reader);
		break;
	case VIDEO_TYPE_DECODE_USAGE:
		/* Usage hints, which the decoder does not use. */
		(void)drv_i915_wire_read_u32(reader);
		break;
	case VIDEO_TYPE_PROFILE_LIST:
		/* [count][array]{profile}: every profile must be H.264 decode; the first refusal is kept. */
		chain->has_profile_list = true;
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		chain->profile_count = (uint32_t)count;
		chain->profiles_supported = true;
		for (item = 0U; item < count && reader->error == 0; item++) {
			result = read_profile(reader, &value);
			if (result != 0 && chain->profiles_supported) {
				chain->profiles_supported = false;
				chain->profile_error = result;
			}
		}

		break;
	case VIDEO_TYPE_H264_PARAMETERS_CREATE:
		/* The capacity, then the add record the create carries, if any. */
		chain->has_h264_create = true;
		chain->max_sps = drv_i915_wire_read_u32(reader);
		chain->max_pps = drv_i915_wire_read_u32(reader);
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U) {
			value = drv_i915_wire_read_u32(reader);
			if (value != VIDEO_TYPE_H264_PARAMETERS_ADD)
				reader->error = 1;
			kern_memset(&nested, 0, sizeof(nested));
			read_chain(reader, &nested, depth + 1U);
			chain_free(&nested);
			read_add(reader, chain);
		}

		break;
	case VIDEO_TYPE_H264_PARAMETERS_ADD:
		/* The sets added. */
		read_add(reader, chain);
		break;
	case VIDEO_TYPE_H264_DPB_SLOT:
		/* [present][flags][FrameNum][reserved][2][PicOrderCnt x2], read and dropped. */
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U) {
			(void)drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
			count = drv_i915_wire_read_u64(reader);
			if (count != 2U)
				reader->error = 1;
			(void)drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
		}

		break;
	case VIDEO_TYPE_H264_PICTURE:
		/*
		 * [present]{picture: flags, ids, reserved x2, frame_num,
		 * idr_pic_id, [2] order counts}[slice count][array]{offset}: the
		 * push after a first picture needs its frame_num and order count;
		 * the slice offsets are not needed (the range is sent whole).
		 */
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U) {
			chain->has_picture = true;
			chain->picture_flags = drv_i915_wire_read_u32(reader);
			chain->sps_id = drv_i915_wire_read_u32(reader);
			chain->pps_id = drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
			chain->frame_num = drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
			count = drv_i915_wire_read_u64(reader);
			if (count != 2U)
				reader->error = 1;
			chain->poc = (int32_t)drv_i915_wire_read_u32(reader);
			(void)drv_i915_wire_read_u32(reader);
		}

		/* The slices' offsets, which follow. */
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		if (count > (uint64_t)(reader->size - reader->offset))
			reader->error = 1;
		for (item = 0U; item < count && reader->error == 0; item++)
			(void)drv_i915_wire_read_u32(reader);
		break;
	default:
		/* A structure the library does not write here. */
		reader->error = 1;
		break;
	}
}

/*
 * Reads a profile structure: its type, its chain and its four fields.
 * Returns 0 for a profile the decoder takes, or the standard reason another
 * is refused; the H.264 profile it names goes to profile_idc.
 */
static int32_t
read_profile(
	struct i915_wire_reader *reader,
	uint32_t *profile_idc)
{
	struct video_chain chain;
	uint32_t type;
	uint32_t operation;
	uint32_t chroma;
	uint32_t luma;
	uint32_t chroma_depth;
	int32_t result;

	/* The type, then the chain that carries the H.264 profile, then the fields. */
	kern_memset(&chain, 0, sizeof(chain));
	type = drv_i915_wire_read_u32(reader);
	if (type != VIDEO_TYPE_PROFILE)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	operation = drv_i915_wire_read_u32(reader);
	chroma = drv_i915_wire_read_u32(reader);
	luma = drv_i915_wire_read_u32(reader);
	chroma_depth = drv_i915_wire_read_u32(reader);
	chain_free(&chain);

	/* Decides the profile. */
	result = profile_result(operation, chroma, luma, chroma_depth, &chain);
	*profile_idc = chain.profile_idc;

	/* Succeeded: reports the profile's result. */
	return result;
}

/* Decides whether a profile is one the decoder takes, or why not, in the standard order. */
static int32_t
profile_result(
	uint32_t operation,
	uint32_t chroma,
	uint32_t luma,
	uint32_t chroma_depth,
	const struct video_chain *chain)
{
	/* H.264 decode is the one operation. */
	if (operation != VIDEO_OP_DECODE_H264)
		return VIDEO_ERROR_OPERATION;

	/* 8-bit 4:2:0 is the one format. */
	if (chroma != VIDEO_CHROMA_420 || luma != VIDEO_DEPTH_8 || chroma_depth != VIDEO_DEPTH_8)
		return VIDEO_ERROR_FORMAT;

	/* The H.264 record must be there and progressive. */
	if (!chain->has_h264_profile)
		return VIDEO_ERROR_CODEC;
	if (chain->picture_layout != VIDEO_LAYOUT_PROGRESSIVE)
		return VIDEO_ERROR_LAYOUT;

	/* Baseline always; Main and High unless only Baseline is offered. */
	if (chain->profile_idc == VIDEO_PROFILE_BASELINE)
		return 0;
	if (VIDEO_BASELINE_ONLY)
		return VIDEO_ERROR_CODEC;
	if (chain->profile_idc != VIDEO_PROFILE_MAIN && chain->profile_idc != VIDEO_PROFILE_HIGH)
		return VIDEO_ERROR_CODEC;

	/* Succeeded: the profile is supported. */
	return 0;
}

/* Reads a picture resource structure: [type][chain 0][x][y][width][height][layer][view]. */
static void
read_resource(
	struct i915_wire_reader *reader,
	struct video_resource *resource)
{
	struct video_chain chain;
	uint32_t type;

	/* The type and an empty chain. */
	kern_memset(&chain, 0, sizeof(chain));
	type = drv_i915_wire_read_u32(reader);
	if (type != VIDEO_TYPE_PICTURE_RESOURCE)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	chain_free(&chain);

	/* The coded area, the layer and the view. */
	resource->x = (int32_t)drv_i915_wire_read_u32(reader);
	resource->y = (int32_t)drv_i915_wire_read_u32(reader);
	resource->width = drv_i915_wire_read_u32(reader);
	resource->height = drv_i915_wire_read_u32(reader);
	resource->layer = drv_i915_wire_read_u32(reader);
	resource->view = drv_i915_wire_read_u64(reader);
}

/* Reads a reference slot structure: [type][chain: DPB slot][index][present]{resource}. */
static void
read_slot(
	struct i915_wire_reader *reader,
	struct video_resource *resource)
{
	struct video_chain chain;
	uint64_t present;
	uint32_t type;

	/* The type, the chain, the index and the picture, which the decoder does not use. */
	kern_memset(&chain, 0, sizeof(chain));
	kern_memset(resource, 0, sizeof(*resource));
	type = drv_i915_wire_read_u32(reader);
	if (type != VIDEO_TYPE_REFERENCE_SLOT)
		reader->error = 1;
	read_chain(reader, &chain, 0U);
	chain_free(&chain);
	(void)drv_i915_wire_read_u32(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		read_resource(reader, resource);
}

/* Reads an array of slots, which the decoder does not use. */
static void
read_slots(
	struct i915_wire_reader *reader)
{
	struct video_resource slot;
	uint64_t total;
	uint64_t item;

	/* [count][array]: a count past the bytes left is malformed. */
	(void)drv_i915_wire_read_u32(reader);
	total = drv_i915_wire_read_u64(reader);
	if (total > (uint64_t)(reader->size - reader->offset))
		reader->error = 1;
	for (item = 0U; item < total && reader->error == 0; item++)
		read_slot(reader, &slot);
}

/*
 * Reads an H.264 add record's fields: [count][array]{SPS}[count][array]{PPS}
 * (libvulkan's video_encode_sps and video_encode_pps).  Each set is a new
 * allocation kept on the chain; a set the executor cannot keep marks
 * add_error.
 */
static void
read_add(
	struct i915_wire_reader *reader,
	struct video_chain *chain)
{
	struct drv_rpi4_h264_sps scratch_sps;
	struct video_pps scratch_pps;
	struct drv_rpi4_h264_sps *sps;
	struct video_pps *pps;
	uint64_t count;
	uint64_t item;
	uint64_t offsets;
	uint64_t present;
	uint32_t index;

	/* The sequence parameter sets. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (count > (uint64_t)(reader->size - reader->offset))
		reader->error = 1;
	for (item = 0U; item < count && reader->error == 0; item++) {
		/* A set past the id range, or one that cannot be allocated, is read into scratch. */
		sps = NULL;
		if (chain->new_sps_count < VIDEO_SPS_IDS)
			sps = kern_calloc(1U, sizeof(*sps));
		if (sps == NULL) {
			kern_memset(&scratch_sps, 0, sizeof(scratch_sps));
			sps = &scratch_sps;
			chain->add_error = ENOMEM;
		}

		/* The fields in declared order. */
		sps->flags = drv_i915_wire_read_u32(reader);
		sps->profile_idc = drv_i915_wire_read_u32(reader);
		sps->level = drv_i915_wire_read_u32(reader);
		sps->chroma_format_idc = drv_i915_wire_read_u32(reader);
		sps->id = drv_i915_wire_read_u32(reader);
		sps->bit_depth_luma_minus8 = drv_i915_wire_read_u32(reader);
		sps->bit_depth_chroma_minus8 = drv_i915_wire_read_u32(reader);
		sps->log2_max_frame_num_minus4 = drv_i915_wire_read_u32(reader);
		sps->pic_order_cnt_type = drv_i915_wire_read_u32(reader);
		sps->offset_for_non_ref_pic = (int32_t)drv_i915_wire_read_u32(reader);
		sps->offset_for_top_to_bottom_field = (int32_t)drv_i915_wire_read_u32(reader);
		sps->log2_max_pic_order_cnt_lsb_minus4 = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u32(reader);
		sps->max_num_ref_frames = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u32(reader);
		sps->pic_width_in_mbs_minus1 = drv_i915_wire_read_u32(reader);
		sps->pic_height_in_map_units_minus1 = drv_i915_wire_read_u32(reader);
		for (index = 0U; index < 4U; index++)
			sps->crop[index] = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u32(reader);

		/* The picture order count cycle's offsets, at most 255. */
		offsets = drv_i915_wire_read_u64(reader);
		if (offsets > 255U)
			reader->error = 1;
		sps->offset_count = (uint32_t)offsets;
		for (index = 0U; index < (uint32_t)offsets && reader->error == 0; index++)
			sps->offsets[index] = (int32_t)drv_i915_wire_read_u32(reader);

		/* The scaling lists, then the VUI, which is never sent. */
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U) {
			sps->has_scaling = 1;
			read_scaling(reader, &sps->scaling);
		}

		/* The VUI, which is never sent. */
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U)
			reader->error = 1;

		/* Keeps the set on the chain; an id outside H.264 marks the add malformed. */
		if (sps->id >= VIDEO_SPS_IDS || sps->level > 18U)
			chain->add_error = EINVAL;
		if (sps != &scratch_sps) {
			chain->new_sps[chain->new_sps_count] = sps;
			chain->new_sps_count++;
		}
	}

	/* The picture parameter sets. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (count > (uint64_t)(reader->size - reader->offset))
		reader->error = 1;
	for (item = 0U; item < count && reader->error == 0; item++) {
		/* A set that cannot be allocated is read into scratch. */
		pps = kern_calloc(1U, sizeof(*pps));
		if (pps == NULL) {
			kern_memset(&scratch_pps, 0, sizeof(scratch_pps));
			pps = &scratch_pps;
			chain->add_error = ENOMEM;
		}

		/* The fields in declared order. */
		pps->fields.flags = drv_i915_wire_read_u32(reader);
		pps->fields.sps_id = drv_i915_wire_read_u32(reader);
		pps->fields.pps_id = drv_i915_wire_read_u32(reader);
		pps->fields.num_ref_idx_l0_default_active_minus1 = drv_i915_wire_read_u32(reader);
		pps->fields.num_ref_idx_l1_default_active_minus1 = drv_i915_wire_read_u32(reader);
		pps->fields.weighted_bipred_idc = drv_i915_wire_read_u32(reader);
		pps->fields.pic_init_qp_minus26 = (int32_t)drv_i915_wire_read_u32(reader);
		pps->fields.pic_init_qs_minus26 = (int32_t)drv_i915_wire_read_u32(reader);
		pps->fields.chroma_qp_index_offset = (int32_t)drv_i915_wire_read_u32(reader);
		pps->fields.second_chroma_qp_index_offset = (int32_t)drv_i915_wire_read_u32(reader);
		present = drv_i915_wire_read_u64(reader);
		if (present != 0U) {
			pps->fields.has_scaling = 1;
			read_scaling(reader, &pps->fields.scaling);
		}

		/* Keeps the set on the chain; ids outside H.264 mark the add malformed. */
		if (pps->fields.sps_id >= VIDEO_SPS_IDS || pps->fields.pps_id >= VIDEO_PPS_IDS)
			chain->add_error = EINVAL;
		if (pps != &scratch_pps) {
			pps->next = chain->new_pps;
			chain->new_pps = pps;
		}
	}
}

/* Reads H.264 scaling lists: [present mask][default mask][96]{4x4}[384]{8x8}. */
static void
read_scaling(
	struct i915_wire_reader *reader,
	struct drv_rpi4_h264_scaling *scaling)
{
	uint64_t count;

	/* The two masks, then the lists, list after list. */
	scaling->present_mask = drv_i915_wire_read_u32(reader);
	scaling->default_mask = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (count != sizeof(scaling->list4))
		reader->error = 1;
	i915_vkc_read_bytes(reader, scaling->list4, sizeof(scaling->list4));
	count = drv_i915_wire_read_u64(reader);
	if (count != sizeof(scaling->list8))
		reader->error = 1;
	i915_vkc_read_bytes(reader, scaling->list8, sizeof(scaling->list8));
}

/* Reads an extension name and version: [256][name bytes][version]. */
static void
read_extension(
	struct i915_wire_reader *reader,
	char *name,
	uint32_t *version)
{
	uint64_t count;

	/* The name, whose array is always the full extent, then the version. */
	count = drv_i915_wire_read_u64(reader);
	if (count != VK_MAX_EXTENSION_NAME_SIZE)
		reader->error = 1;
	i915_vkc_read_bytes(reader, name, VK_MAX_EXTENSION_NAME_SIZE);
	name[VK_MAX_EXTENSION_NAME_SIZE - 1U] = '\0';
	*version = drv_i915_wire_read_u32(reader);
}

/* Frees the parameter sets a chain read and nobody took. */
static void
chain_free(
	struct video_chain *chain)
{
	struct video_pps *pps;
	uint32_t index;

	/* The sequence sets, then the picture sets. */
	for (index = 0U; index < chain->new_sps_count; index++) {
		kern_free(chain->new_sps[index]);
		chain->new_sps[index] = NULL;
	}

	/* The chain holds no sequence set any more. */
	chain->new_sps_count = 0U;
	while (chain->new_pps != NULL) {
		pps = chain->new_pps;
		chain->new_pps = pps->next;
		kern_free(pps);
	}
}

/*
 * Moves a chain's new sets into a parameters object.
 *
 * With replace, a set replaces the object's of the same key (a create);
 * without, a key already held refuses the whole add (an update) and
 * nothing is taken.  The object's capacity bounds the sets.
 */
static int
parameters_take(
	struct video_parameters *parameters,
	struct video_chain *chain,
	bool replace)
{
	struct video_pps *pps;
	struct video_pps *old;
	const struct video_pps *held;
	struct video_pps **link;
	uint32_t index;
	uint32_t id;
	uint32_t sps_count;
	uint32_t pps_count;

	/* An update must not repeat a key the object holds. */
	if (!replace) {
		for (index = 0U; index < chain->new_sps_count; index++) {
			if (parameters->sps[chain->new_sps[index]->id] != NULL)
				return EINVAL;
		}

		/* Nor a picture set key it holds. */
		for (pps = chain->new_pps; pps != NULL; pps = pps->next) {
			held = find_pps(parameters, pps->fields.sps_id, pps->fields.pps_id);
			if (held != NULL)
				return EINVAL;
		}
	}

	/* Refuses, before taking anything, sets beyond the object's capacity (new keys counted as added). */
	sps_count = 0U;
	for (index = 0U; index < VIDEO_SPS_IDS; index++) {
		if (parameters->sps[index] != NULL)
			sps_count++;
	}

	/* The new sequence sets of new keys. */
	for (index = 0U; index < chain->new_sps_count; index++) {
		if (parameters->sps[chain->new_sps[index]->id] == NULL)
			sps_count++;
	}

	/* The picture sets held, and the new ones of new keys. */
	pps_count = 0U;
	for (pps = parameters->pps; pps != NULL; pps = pps->next)
		pps_count++;
	for (pps = chain->new_pps; pps != NULL; pps = pps->next) {
		held = find_pps(parameters, pps->fields.sps_id, pps->fields.pps_id);
		if (held == NULL)
			pps_count++;
	}

	/* Refuses sets beyond the object's capacity. */
	if (sps_count > parameters->max_sps || pps_count > parameters->max_pps)
		return EINVAL;

	/* The sequence sets, each in its id's place. */
	for (index = 0U; index < chain->new_sps_count; index++) {
		id = chain->new_sps[index]->id;
		kern_free(parameters->sps[id]);
		parameters->sps[id] = chain->new_sps[index];
		chain->new_sps[index] = NULL;
	}

	/* The chain holds no sequence set any more. */
	chain->new_sps_count = 0U;

	/* The picture sets, replacing any of the same key. */
	while (chain->new_pps != NULL) {
		pps = chain->new_pps;
		chain->new_pps = pps->next;
		link = &parameters->pps;
		while (*link != NULL) {
			if ((*link)->fields.sps_id == pps->fields.sps_id && (*link)->fields.pps_id == pps->fields.pps_id) {
				old = *link;
				*link = old->next;
				kern_free(old);
				continue;
			}

			link = &(*link)->next;
		}

		/* The set joins the object's list. */
		pps->next = parameters->pps;
		parameters->pps = pps;
	}

	/* Succeeded: the sets are the object's. */
	return 0;
}

/* Copies a template's sets into a new parameters object. */
static int
parameters_copy(
	struct video_parameters *destination,
	const struct video_parameters *source)
{
	const struct video_pps *pps;
	struct video_pps *copy;
	uint32_t index;

	/* The sequence sets. */
	for (index = 0U; index < VIDEO_SPS_IDS; index++) {
		if (source->sps[index] == NULL)
			continue;
		destination->sps[index] = kern_calloc(1U, sizeof(*destination->sps[index]));
		if (destination->sps[index] == NULL)
			return ENOMEM;
		*destination->sps[index] = *source->sps[index];
	}

	/* The picture sets. */
	for (pps = source->pps; pps != NULL; pps = pps->next) {
		copy = kern_calloc(1U, sizeof(*copy));
		if (copy == NULL)
			return ENOMEM;
		copy->fields = pps->fields;
		copy->next = destination->pps;
		destination->pps = copy;
	}

	/* Succeeded: the template's sets are copied. */
	return 0;
}

/* Frees a parameters object's sets. */
static void
parameters_free(
	struct video_parameters *parameters)
{
	struct video_pps *pps;
	uint32_t index;

	/* The sequence sets, then the picture sets. */
	for (index = 0U; index < VIDEO_SPS_IDS; index++) {
		kern_free(parameters->sps[index]);
		parameters->sps[index] = NULL;
	}
	while (parameters->pps != NULL) {
		pps = parameters->pps;
		parameters->pps = pps->next;
		kern_free(pps);
	}
}

/* Finds the picture set of a key in a parameters object. */
static const struct video_pps *
find_pps(
	const struct video_parameters *parameters,
	uint32_t sps_id,
	uint32_t pps_id)
{
	const struct video_pps *pps;

	/* Walks the list for the key. */
	for (pps = parameters->pps; pps != NULL; pps = pps->next) {
		if (pps->fields.sps_id == sps_id && pps->fields.pps_id == pps_id)
			return pps;
	}

	/* Succeeded: no set of the key. */
	return NULL;
}

/*
 * vkCreateQueryPool: [device][present][type][chain 0][flags][query type]
 * [count][statistics][allocator][present][identity] -> [result][present][pool].
 * Only a result status pool of at most VIDEO_QUERY_MAX queries is made.
 */
static int
pool_create(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct bcm2711_vulkan_object *device;
	struct bcm2711_vulkan_object *object;
	struct video_pool *pool;
	uint64_t device_id;
	uint64_t identity;
	uint32_t type;
	uint32_t count;
	int error;
	int released;

	/* Decodes the type, the count and the identity. */
	device_id = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	type = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	device = bcm2711_vulkan_object_find(session, I915_VK_OBJ_DEVICE, device_id);
	if (reader->error != 0 || device == NULL || identity == 0)
		return EINVAL;

	/* Only a result status pool of a bounded size. */
	if (type != VIDEO_QUERY_RESULT_STATUS || count == 0U || count > VIDEO_QUERY_MAX) {
		kern_logf("v3d: vk: vkCreateQueryPool refused: type %u, %u queries (result status only)\n", (unsigned)type, (unsigned)count);
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT);
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u64(reply, 0U);
		return 0;
	}

	/* The pool, every query unavailable, keeping its device. */
	pool = kern_calloc(1U, sizeof(*pool));
	if (pool == NULL) {
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u64(reply, 0U);
		return 0;
	}

	/* The pool's size, and the device it lives under. */
	pool->count = count;
	error = bcm2711_vulkan_object_retain(device);
	if (error != 0) {
		kern_free(pool);
		return error;
	}

	/* The pool keeps its device. */
	pool->owner.device = device;

	/* Publishes the pool under its identity. */
	error = bcm2711_vulkan_object_publish(session, I915_VK_OBJ_QUERY_POOL, identity, pool, release_pool, &object);
	if (error != 0) {
		released = release_pool(session, pool);
		if (released != 0)
			return released;
		if (error != ENOMEM)
			return error;
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u64(reply, 0U);
		return 0;
	}

	/* Replies VK_SUCCESS and the identity. */
	drv_i915_wire_reply_u32(reply, 0U);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the pool exists. */
	return 0;
}

/*
 * vkGetQueryPoolResults: [device][pool][first][count][dataSize][bytes]
 * [stride][flags] -> [result][bytes]{bytes}.  Each query's record is its
 * status and its availability, in words of 4 or 8 bytes, stride apart.
 */
static int
pool_results(
	struct bcm2711_vulkan_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct bcm2711_vulkan_object *object;
	struct video_pool *pool;
	uint64_t identity;
	uint64_t bytes;
	uint64_t stride;
	uint64_t record;
	uint64_t written;
	uint64_t end;
	uint64_t value;
	uint64_t available;
	uint32_t first;
	uint32_t count;
	uint32_t flags;
	uint32_t width;
	uint32_t query;
	uint32_t result;
	uint8_t data[16];
	uint8_t zeros[64];

	/* Decodes the range, the size, the stride and the flags. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	first = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	bytes = drv_i915_wire_read_u64(reader);
	stride = drv_i915_wire_read_u64(reader);
	flags = drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A record is one or two words of four or eight bytes. */
	width = 4U;
	if ((flags & VIDEO_QUERY_RESULT_64) != 0U)
		width = 8U;
	record = width;
	if ((flags & VIDEO_QUERY_RESULT_AVAILABILITY) != 0U)
		record += width;

	/* Refuses an unknown pool, a range past it, and records that do not fit the reply. */
	pool = NULL;
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_QUERY_POOL, identity);
	if (object != NULL)
		pool = object->payload;
	if (pool == NULL || first > pool->count || count > pool->count - first ||
	    bytes > VIDEO_QUERY_REPLY_MAX || stride < record || stride > bytes ||
	    (count != 0U && (uint64_t)(count - 1U) * stride + record > bytes)) {
		drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_DEVICE_LOST);
		drv_i915_wire_reply_u64(reply, 0U);
		return 0;
	}

	/* NOT_READY when any query of the range is unavailable. */
	result = 0U;
	for (query = first; query < first + count; query++) {
		if (pool->available[query] == 0)
			result = (uint32_t)VK_NOT_READY;
	}

	/* The result, then the bytes the records take. */
	drv_i915_wire_reply_u32(reply, result);
	drv_i915_wire_reply_u64(reply, bytes);

	/* Each record at its stride, then zeros to the next record and to the end. */
	written = 0U;
	kern_memset(zeros, 0, sizeof(zeros));
	for (query = 0U; query < count; query++) {
		value = (uint64_t)(int64_t)pool->status[first + query];
		available = pool->available[first + query];
		kern_memset(data, 0, sizeof(data));
		kern_memcpy(data, &value, width);
		kern_memcpy(data + width, &available, width);
		drv_i915_wire_reply_bytes(reply, data, (size_t)record);
		written += record;
		end = written - record + stride;
		if (end > bytes)
			end = bytes;
		while (written < end) {
			value = end - written;
			if (value > sizeof(zeros))
				value = sizeof(zeros);
			drv_i915_wire_reply_bytes(reply, zeros, (size_t)value);
			written += value;
		}
	}
	while (written < bytes) {
		value = bytes - written;
		if (value > sizeof(zeros))
			value = sizeof(zeros);
		drv_i915_wire_reply_bytes(reply, zeros, (size_t)value);
		written += value;
	}

	/* Succeeded: the records are written. */
	return 0;
}

/*
 * Records a query command: vkCmdBeginQuery [cmd][pool][query][flags],
 * vkCmdEndQuery [cmd][pool][query], vkCmdResetQueryPool [cmd][pool][first]
 * [count].  A begin or end of a status query belongs to a coding scope.
 */
static int
record_query(
	struct bcm2711_vulkan_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	struct bcm2711_vulkan_object *object;
	struct bcm2711_vulkan_command_buffer *command;
	struct video_command *query;
	struct video_pool *pool;
	uint64_t identity;
	uint64_t pool_id;
	int error;
	int released;

	/* The recording primary and the command's fields. */
	identity = drv_i915_wire_read_u64(reader);
	pool_id = drv_i915_wire_read_u64(reader);
	query = kern_calloc(1U, sizeof(*query));
	if (query == NULL)
		return ENOMEM;
	query->record.opcode = opcode;
	query->record.node.release = release_command;
	query->query = drv_i915_wire_read_u32(reader);
	query->query_count = 1U;
	if (opcode == GPU_OP_CMD_BEGIN_QUERY)
		(void)drv_i915_wire_read_u32(reader);
	if (opcode == GPU_OP_CMD_RESET_QUERY_POOL)
		query->query_count = drv_i915_wire_read_u32(reader);
	object = bcm2711_vulkan_object_find(session, I915_VK_OBJ_COMMAND_BUFFER, identity);
	if (reader->error != 0 || object == NULL) {
		kern_free(query);
		return EINVAL;
	}

	/* The command buffer the query is recorded into. */
	command = object->payload;
	if (command->pending != 0 || command->state != BCM2711_VULKAN_COMMAND_RECORDING) {
		kern_free(query);
		return EBUSY;
	}

	/* A command buffer that failed recording takes nothing more. */
	if (command->recording_error != 0) {
		kern_free(query);
		return 0;
	}

	/* The pool, the range within it, and the scope of a begin or end. */
	error = retain_command(session, query, NULL, 0, 0, 0, 0, pool_id);
	pool = NULL;
	if (query->pool != NULL)
		pool = query->pool->payload;
	if (error == 0 && (pool == NULL || query->query > pool->count || query->query_count > pool->count - query->query))
		error = EINVAL;
	if (error == 0 && opcode != GPU_OP_CMD_RESET_QUERY_POOL) {
		if (query->query_count != 1U || query->query >= pool->count)
			error = EINVAL;
		query->begin = open_scope(command);
		if (query->begin == NULL)
			error = EINVAL;
	}

	/* A query inside a render pass is not supported. */
	if (error == 0 && command->render_open)
		error = ENOTSUP;
	if (error == 0 && sizeof(*query) > BCM2711_VULKAN_RECORD_BYTES - command->recorded_bytes)
		error = ENOMEM;
	if (error != 0) {
		command->recording_error = error;
		released = release_command(&query->record.node);
		if (released != 0)
			return released;
		return 0;
	}

	/* Appends the node to the primary's ordered list. */
	if (command->last == NULL) {
		command->first = &query->record.node;
	} else {
		command->last->next = &query->record.node;
	}

	/* The record is the command buffer's last. */
	command->last = &query->record.node;
	command->recorded_bytes += sizeof(*query);

	/* Succeeded: the command is recorded. */
	return 0;
}

/*
 * Decodes one picture with the firmware's decoder, a step at a time.
 *
 * The first step builds the access unit (the sets when they changed or at
 * an IDR, then the bitstream range) in the decoder's staging and sends it,
 * with the push picture after the first unit of a start; every step then
 * looks for the frame for at most VIDEO_POLL_MS with the controller's
 * window open.  A step that finds none ends with EINPROGRESS, so the other
 * sessions take their turns while the VPU decodes; the next step comes
 * back here.  The frame that comes is copied, corrected, into the output
 * image and kept as the image's cached copy for its readback.
 */
static int
run_decode(
	struct bcm2711_render_device *controller,
	const struct video_command *command)
{
	struct video_session *video;
	struct video_parameters *parameters;
	struct bcm2711_vulkan_resource *image;
	struct drv_rpi4_mmal_frame frame;
	const uint8_t *pixels;
	uint8_t *cpu;
	uint64_t now;
	uint64_t received;
	bool starting;
	int begun;
	int error;
	int closed;

	/* The scope's session and parameters. */
	video = command->begin->session->payload;
	parameters = NULL;
	if (command->begin->parameters != NULL)
		parameters = command->begin->parameters->payload;
	starting = false;
	if (video->inflight != command)
		starting = true;

	/* A broken session decodes nothing more. */
	if (video->broken) {
		video->inflight = NULL;
		set_status(command, VIDEO_STATUS_ERROR);
		return 0;
	}

	/* A decode needs parameters. */
	if (parameters == NULL) {
		video->inflight = NULL;
		skip(video, command, "no parameters bound", EINVAL);
		return 0;
	}

	/* The output image and its CPU view, resolved again each step (the window keeps them while it is open). */
	error = image_target(command, &image, &cpu);
	if (error != 0) {
		video->inflight = NULL;
		skip(video, command, "output picture not usable", error);
		return 0;
	}

	/*
	 * The frame about to be received may reuse the buffer the cached copy
	 * of the last frame is in: the copy is forgotten first.
	 */
	forget_shadow(video);

	/* Without the controller mutex: the unit is built and sent on the first step, then the frame is looked for. */
	bcm2711_render_window_open(controller);
	begun = 0;
	if (starting)
		begun = begin_decode(video, parameters, command);
	error = begun;
	if (begun == 0)
		error = wait_frame(video, &frame, &pixels);

	/* A frame that came is placed in the image, corrected, while the window keeps the image's view. */
	received = 0;
	if (error == 0) {
		received = clock_ticks();
		relayout(&frame, pixels, image, cpu);
	}
	closed = bcm2711_render_window_close(controller);
	now = clock_ticks();
	video->steps++;

	/* Reports a window that did not close. */
	if (closed != 0) {
		video->inflight = NULL;
		return closed;
	}

	/*
	 * A unit that could not be built or sent, or a decoder that failed
	 * while its frame was looked for, skips the picture; the firmware may
	 * lack the sets, so they go again next time.
	 */
	if (begun != 0 ||
	    (error != 0 && error != ETIMEDOUT)) {
		video->inflight = NULL;
		video->sent_sps_bytes = 0;
		video->sent_pps_bytes = 0;
		video_stats.skipped++;
		if (begun != 0) {
			skip(video, command, "access unit not sent", error);
		} else {
			skip(video, command, "frame not received", error);
		}
		return 0;
	}

	/* No frame yet: the step ends here and the next one looks again, until the decode's deadline. */
	if (error == ETIMEDOUT && now < video->deadline)
		return EINPROGRESS;

	/* A frame that did not come in time skips the picture; too many in a row stop waiting. */
	if (error == ETIMEDOUT) {
		video->inflight = NULL;
		video->sent_sps_bytes = 0;
		video->sent_pps_bytes = 0;
		video->timeouts++;
		if (video->timeouts >= VIDEO_MAX_TIMEOUTS)
			video->broken = true;
		video_stats.skipped++;
		skip(video, command, "no frame from the decoder", ETIMEDOUT);
		return 0;
	}

	/* The decoder answered: the timeouts in a row start again, and the decode is over. */
	video->inflight = NULL;
	video->timeouts = 0;
	note_decode(video, received, now);

	/* Only NV12 is the image's format; another output is not this picture. */
	if (frame.encoding != DRV_RPI4_MMAL_FRAME_NV12) {
		forget_shadow(video);
		skip(video, command, "decoder output is not NV12", ENOTSUP);
		return 0;
	}

	/* The status of the picture. */
	if ((frame.flags & DRV_RPI4_MMAL_FRAME_CORRUPTED) != 0) {
		set_status(command, VIDEO_STATUS_ERROR);
	} else {
		set_status(command, VIDEO_STATUS_COMPLETE);
	}

	/* The image keeps its cached copy until the decoder's next receive. */
	video_shadow.owner = video;

	/* Succeeded: the picture is in the output image. */
	return 0;
}

/*
 * Builds and sends a decode's access unit, and after the first unit of a
 * start the push picture; the decode is then in flight until its deadline.
 * The caller has the window open.
 */
static int
begin_decode(
	struct video_session *video,
	const struct video_parameters *parameters,
	const struct video_command *command)
{
	uint8_t *staging;
	uint32_t capacity;
	uint32_t bytes;
	int error;

	/* The decode's time stamp: its number in the session (the push's 0 is never one). */
	video->sequence++;
	video->pts = video->sequence;
	video->started_at = clock_ticks();
	video->steps = 0;

	/* The unit, in the staging the firmware has given back. */
	error = drv_rpi4_mmal_decoder_input(video->decoder, &staging, &capacity);
	if (error != 0)
		return error;
	error = build_unit(video, parameters, command, staging, capacity, &bytes);
	if (error != 0)
		return error;

	/* Sends it with its time stamp. */
	error = drv_rpi4_mmal_decoder_send(video->decoder, bytes, video->pts, 0);
	if (error != 0)
		return error;

	/* The decode is in flight from here; a push that cannot go only leaves the first picture held. */
	video->inflight = command;
	video->sent_at = clock_ticks();
	video->deadline = video->sent_at + kern_ms_to_ticks(VIDEO_FRAME_MS);
	if (!video->started) {
		video->started = true;
		error = send_push(video, parameters, command);
		if (error != 0 && video->push_lines < VIDEO_PUSH_LINES) {
			video->push_lines++;
			kern_logf("v3d: video: decode %lld: no push picture after the first (%d); the first picture may be held\n",
				  (long long)video->pts, error);
		}
	}

	/* Succeeded: the unit is the firmware's. */
	return 0;
}

/*
 * Sends the push picture after the first unit of a start.
 *
 * The firmware holds the first picture after a start until the next
 * picture's slices come, even with the end-of-frame flag (later pictures
 * come out at their own end of frame); the parameter sets again, an access
 * unit delimiter and an empty end of frame did not give it out, and an end
 * of stream lost the references (T1-631, T1-637).  A Vulkan decode has no
 * next picture to give, so a grey non-reference picture of the push's own
 * (drv_rpi4_h264_write_push) follows the first one: it changes no
 * reference and no order count, and its frame comes with time stamp
 * VIDEO_PUSH_PTS and is dropped.  Only a reference first picture is pushed
 * (the push's frame_num follows it).
 */
static int
send_push(
	struct video_session *video,
	const struct video_parameters *parameters,
	const struct video_command *command)
{
	const struct drv_rpi4_h264_sps *sps;
	uint8_t *staging;
	uint32_t capacity;
	uint32_t pps_id;
	size_t written;
	int error;

	/* The picture's SPS (build_unit found it), and a reference picture to follow. */
	sps = parameters->sps[command->sps_id];
	if (sps == NULL)
		return ENOENT;
	if ((command->picture_flags & VIDEO_PICTURE_REFERENCE) == 0)
		return ENOTSUP;
	pps_id = unused_pps_id(parameters);

	/* The other staging area: the push in its first half, the slice's RBSP in its second. */
	error = drv_rpi4_mmal_decoder_input(video->decoder, &staging, &capacity);
	if (error != 0)
		return error;
	error = drv_rpi4_h264_write_push(sps, pps_id, command->frame_num + 1U, (uint32_t)command->poc + 1U,
					 staging + capacity / 2U, capacity / 2U, staging, capacity / 2U, &written);
	if (error != 0)
		return error;

	/* Sends it as a frame of its own. */
	error = drv_rpi4_mmal_decoder_send(video->decoder, (uint32_t)written, VIDEO_PUSH_PTS, 0);
	if (error != 0)
		return error;

	/* Succeeded: the push follows the first picture. */
	video_stats.pushes++;
	return 0;
}

/*
 * Looks for the frame of the decode in flight for at most VIDEO_POLL_MS:
 * the push's frame, empty frames and late frames of earlier decodes are
 * dropped on the way.  ETIMEDOUT when none came yet.  The caller has the
 * window open.
 */
static int
wait_frame(
	struct video_session *video,
	struct drv_rpi4_mmal_frame *frame,
	const uint8_t **pixels)
{
	int error;

	/* Frames until this decode's (or none for now). */
	for (;;) {
		/* The next frame, or none within the poll. */
		error = drv_rpi4_mmal_decoder_receive(video->decoder, VIDEO_POLL_MS, frame, pixels);
		if (error != 0)
			return error;

		/* An empty frame carries no picture; the push's frame is not one of the stream's. */
		if (frame->length == 0 || frame->pts == VIDEO_PUSH_PTS)
			continue;

		/* A late frame of an earlier decode (one that timed out) is dropped, said a few times; an unknown time is this one's. */
		if (frame->pts < video->pts && frame->pts != DRV_RPI4_MMAL_FRAME_NO_TIME) {
			if (video->late_lines < VIDEO_LATE_LINES) {
				video->late_lines++;
				kern_logf("v3d: video: late frame %lld dropped at decode %lld\n", (long long)frame->pts, (long long)video->pts);
			}

			continue;
		}

		/* A frame of a later picture (the decoder reordered after all) is reported, a few times, and used. */
		if (frame->pts != video->pts &&
		    frame->pts != DRV_RPI4_MMAL_FRAME_NO_TIME &&
		    video->late_lines < VIDEO_LATE_LINES) {
			video->late_lines++;
			kern_logf("v3d: video: frame %lld came for decode %lld (the decoder reorders)\n", (long long)frame->pts, (long long)video->pts);
		}

		break;
	}

	/* Succeeded: frame is the decode's. */
	return 0;
}

/*
 * Builds one access unit: the picture's SPS (VUI asking for no
 * reordering) and PPS when they differ from those last sent or the picture
 * is an IDR, then the application's bitstream range (its slices, each with
 * a start code).
 */
static int
build_unit(
	struct video_session *video,
	const struct video_parameters *parameters,
	const struct video_command *command,
	uint8_t *staging,
	uint32_t capacity,
	uint32_t *bytes)
{
	struct bcm2711_vulkan_resource *buffer;
	struct bcm2711_v3d_view *view;
	const struct drv_rpi4_h264_sps *sps;
	const struct video_pps *pps;
	uint8_t *unit;
	void *source;
	size_t written;
	uint32_t used;
	uint32_t address;
	bool idr;
	int error;

	/* The picture's sets. */
	if (command->sps_id >= VIDEO_SPS_IDS)
		return EINVAL;
	sps = parameters->sps[command->sps_id];
	pps = find_pps(parameters, command->sps_id, command->pps_id);
	if (sps == NULL || pps == NULL)
		return ENOENT;
	idr = false;
	if ((command->picture_flags & VIDEO_PICTURE_IDR) != 0)
		idr = true;

	/* The SPS and the PPS, sent when new or at an IDR. */
	unit = video->unit;
	used = 0;
	error = drv_rpi4_h264_write_sps(sps, 1, unit, sizeof(video->unit), &written);
	if (error == 0)
		error = append_set(staging, capacity, &used, unit, written, video->sent_sps, &video->sent_sps_bytes, idr);
	if (error == 0)
		error = drv_rpi4_h264_write_pps(&pps->fields, sps, unit, sizeof(video->unit), &written);
	if (error == 0)
		error = append_set(staging, capacity, &used, unit, written, video->sent_pps, &video->sent_pps_bytes, idr);
	if (error != 0)
		return error;

	/* The bitstream range, copied from the application's buffer. */
	if (command->buffer == NULL || command->range == 0 || command->range > capacity - used)
		return ENOSPC;
	buffer = command->buffer->payload;
	error = bcm2711_vulkan_resource_backing(buffer, command->offset, command->range, &view, &address, &source);
	if (error != 0)
		return error;
	kern_memcpy(staging + used, source, (size_t)command->range);
	used += (uint32_t)command->range;

	/* Succeeded: the unit is in the staging. */
	*bytes = used;
	return 0;
}

/* Appends a parameter set to the unit when it differs from the one last sent, or when forced. */
static int
append_set(
	uint8_t *staging,
	uint32_t capacity,
	uint32_t *used,
	const uint8_t *unit,
	size_t bytes,
	uint8_t *sent,
	size_t *sent_bytes,
	bool force)
{
	bool same;
	int difference;

	/* The set is the same as the one sent before when its bytes are. */
	same = false;
	if (bytes == *sent_bytes) {
		difference = kern_memcmp(unit, sent, bytes);
		if (difference == 0)
			same = true;
	}

	/* An unchanged set is not sent again unless forced. */
	if (same && !force)
		return 0;
	if (bytes > capacity - *used)
		return ENOSPC;

	/* Appends it and remembers it. */
	kern_memcpy(staging + *used, unit, bytes);
	*used += (uint32_t)bytes;
	kern_memcpy(sent, unit, bytes);
	*sent_bytes = bytes;

	/* Succeeded: the set is in the unit. */
	return 0;
}

/* Resolves a decode's output picture to its NV12 image and CPU view. */
static int
image_target(
	const struct video_command *command,
	struct bcm2711_vulkan_resource **image,
	uint8_t **cpu)
{
	struct bcm2711_vulkan_image_view *view;
	struct bcm2711_vulkan_resource *resource;
	struct bcm2711_v3d_view *backing;
	uint64_t chroma;
	uint64_t bytes;
	uint32_t pitch;
	uint32_t address;
	void *mapped;
	int error;

	/* The view's image must be an NV12 decode output. */
	if (command->view == NULL)
		return EINVAL;
	view = command->view->payload;
	if (view->owner.parent == NULL)
		return EINVAL;
	resource = view->owner.parent->payload;
	if (resource->format != (VkFormat)BCM2711_VIDEO_FORMAT_NV12 ||
	    (resource->usage & BCM2711_VIDEO_USAGE_DECODE_DST) == 0)
		return EINVAL;

	/* Its whole bound memory, mapped for the CPU. */
	bcm2711_vulkan_video_nv12_layout(resource->width, resource->height, &pitch, &chroma, &bytes);
	error = bcm2711_vulkan_resource_backing(resource, 0, bytes, &backing, &address, &mapped);
	if (error != 0)
		return error;

	/* Succeeded: the image and where the CPU reaches it. */
	*image = resource;
	*cpu = mapped;
	return 0;
}

/*
 * Copies a frame from the decoder's cached memory into the image's layout,
 * row by row, with the firmware's footroom error undone: the luma rows,
 * then the chroma rows, each as wide as both layouts allow.  The part
 * copied becomes the image's cached copy (owner is set by the caller once
 * the decode is over).
 */
static void
relayout(
	const struct drv_rpi4_mmal_frame *frame,
	const uint8_t *pixels,
	const struct bcm2711_vulkan_resource *image,
	uint8_t *cpu)
{
	const uint8_t *luma;
	const uint8_t *chroma_plane;
	uint64_t chroma;
	uint64_t bytes;
	uint32_t pitch;
	uint32_t source_pitch;
	uint32_t source_chroma;
	uint32_t width;
	uint32_t rows;
	uint32_t row;

	/* The two layouts. */
	bcm2711_vulkan_video_nv12_layout(image->width, image->height, &pitch, &chroma, &bytes);
	source_pitch = frame->plane_pitch[0];
	source_chroma = frame->plane_offset[1];
	if (frame->planes < 2U || source_pitch == 0) {
		source_pitch = frame->width;
		source_chroma = frame->width * frame->height;
	}
	luma = pixels + frame->plane_offset[0];
	chroma_plane = pixels + source_chroma;

	/* The rows and the bytes of each row both layouts have. */
	width = image->width;
	if (width > source_pitch)
		width = source_pitch;
	rows = image->height;
	if (rows > frame->height)
		rows = frame->height;

	/* The luma rows, then the chroma rows. */
	for (row = 0; row < rows; row++)
		drv_rpi4_mmal_copy_samples(cpu + (size_t)row * pitch, luma + (size_t)row * source_pitch, width);
	for (row = 0; row < rows / 2U; row++)
		drv_rpi4_mmal_copy_samples(cpu + chroma + (size_t)row * pitch, chroma_plane + (size_t)row * source_pitch, width);

	/* The part copied, for the image's readback. */
	video_shadow.image = image;
	video_shadow.luma = luma;
	video_shadow.chroma = chroma_plane;
	video_shadow.pitch = source_pitch;
	video_shadow.width = width;
	video_shadow.rows = rows;
}

/*
 * Forgets the cached copy of the last frame: owner's only, or with NULL
 * whatever there is.
 */
static void
forget_shadow(
	const struct video_session *owner)
{
	/* Another session's copy stays. */
	if (owner != NULL && video_shadow.owner != owner)
		return;

	/* No image has a cached copy now. */
	kern_memset(&video_shadow, 0, sizeof(video_shadow));
}

/* Reports the highest PPS id no set of a parameters object uses, for the push's own PPS. */
static uint32_t
unused_pps_id(
	const struct video_parameters *parameters)
{
	const struct video_pps *pps;
	uint32_t id;
	bool used;

	/* From 255 down, the first id no set has (an object holds at most 256 sets). */
	for (id = VIDEO_PPS_IDS - 1U; id > 0; id--) {
		used = false;
		for (pps = parameters->pps; pps != NULL; pps = pps->next) {
			if (pps->fields.pps_id == id)
				used = true;
		}

		/* The id is free. */
		if (!used)
			break;
	}

	/* Succeeded: the id (0 only when every other one is taken). */
	return id;
}

/* Adds a finished decode to the cost line, and writes the line when its window is over. */
static void
note_decode(
	const struct video_session *video,
	uint64_t received,
	uint64_t now)
{
	uint64_t frame;

	/* The decode's times: to its unit sent, to its frame received, and its placing. */
	frame = received - video->sent_at;
	video_stats.decodes++;
	video_stats.steps += video->steps;
	video_stats.send_ticks += video->sent_at - video->started_at;
	video_stats.frame_ticks += frame;
	if (frame > video_stats.frame_max)
		video_stats.frame_max = frame;
	video_stats.place_ticks += now - received;

	/* The line, when the window is over. */
	report_stats(now);
}

/*
 * Writes the cost line of the window when VIDEO_STATS_MS have passed since
 * it opened (the first VIDEO_STATS_REPORTS windows of a boot), and opens
 * the next.
 */
static void
report_stats(
	uint64_t now)
{
	uint64_t elapsed;
	uint32_t reports;

	/* The first decode opens the first window. */
	if (video_stats.window_start == 0) {
		video_stats.window_start = now;
		return;
	}

	/* A window shorter than VIDEO_STATS_MS keeps counting. */
	elapsed = kern_ticks_to_ms(now - video_stats.window_start);
	if (elapsed < VIDEO_STATS_MS)
		return;

	/* The averages of the window, while the boot's lines last. */
	if (video_stats.reports < VIDEO_STATS_REPORTS) {
		kern_logf("v3d: video: %u decodes in %llu ms (%u skipped, %u pushes, %u steps); avg_us send %llu frame %llu place %llu, max_us frame %llu; readbacks %u cached avg_us %llu, %u uncached avg_us %llu\n",
			  (unsigned)video_stats.decodes,
			  (unsigned long long)elapsed,
			  (unsigned)video_stats.skipped,
			  (unsigned)video_stats.pushes,
			  (unsigned)video_stats.steps,
			  (unsigned long long)average_us(video_stats.send_ticks, video_stats.decodes),
			  (unsigned long long)average_us(video_stats.frame_ticks, video_stats.decodes),
			  (unsigned long long)average_us(video_stats.place_ticks, video_stats.decodes),
			  (unsigned long long)average_us(video_stats.frame_max, 1U),
			  (unsigned)video_stats.cached_readbacks,
			  (unsigned long long)average_us(video_stats.cached_ticks, video_stats.cached_readbacks),
			  (unsigned)video_stats.uncached_readbacks,
			  (unsigned long long)average_us(video_stats.uncached_ticks, video_stats.uncached_readbacks));
	}

	/* The next window starts empty; the count of lines written stays. */
	reports = video_stats.reports + 1U;
	kern_memset(&video_stats, 0, sizeof(video_stats));
	video_stats.reports = reports;
	video_stats.window_start = now;
}

/*
 * Reports the mean of a tick sum in microseconds.  The ticks are coarse
 * (a millisecond), but the sum of many independently phased times still
 * estimates the mean.
 */
static uint64_t
average_us(
	uint64_t ticks,
	uint32_t count)
{
	uint64_t microseconds;

	/* An empty sum has no mean. */
	if (count == 0)
		return 0;

	/* Succeeded: the mean. */
	microseconds = kern_ticks_to_rate(ticks, 1000000U);
	return microseconds / count;
}

/* Writes a decode's status into the status query open around it, if any. */
static void
set_status(
	const struct video_command *command,
	int32_t status)
{
	struct video_pool *pool;

	/* Only a decode inside a begin query has a status to write. */
	if (command == NULL || command->status == NULL)
		return;
	pool = command->status->pool->payload;
	if (command->status->query >= pool->count)
		return;
	pool->status[command->status->query] = status;
}

/* Skips a picture: logs why (a few times a session) and marks its status ERROR. */
static void
skip(
	struct video_session *video,
	const struct video_command *command,
	const char *reason,
	int error)
{
	/* The first lines of a session say why; later ones only count. */
	if (video->timeouts < VIDEO_MAX_TIMEOUTS || video->sequence < 4)
		kern_logf("v3d: video: picture %lld skipped: %s (%d)\n", (long long)video->sequence, reason, error);
	set_status(command, VIDEO_STATUS_ERROR);
}
