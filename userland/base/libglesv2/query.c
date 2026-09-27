/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Query and sync objects of zedBSD's OpenGL ES 3.0 (WS068 p027).
 *
 * An occlusion query (GL_ANY_SAMPLES_PASSED and its conservative form)
 * may stay active across render passes and frames, but a Vulkan query
 * lives inside one render pass.  So each run of draws inside one pass
 * while the query is active is a segment of its own: a slot of the
 * context's query pool, begun before the run's first draw and ended
 * before the pass ends (gles_queries_suspend, which every place that ends
 * a pass calls first).  The query's result is whether any segment saw a
 * sample pass.  A slot is reset by the upload queue when it is taken.
 *
 * A fence sync is the frame being recorded when it was made: it is
 * signalled once that frame is done (state->frame has moved past it);
 * waiting on it submits the frame so far and waits for it.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

/* How many slots the context's query pool has. */
#define QUERY_SLOTS		1024U

/*
 * The context's query objects' shared state: the pool, which slots are
 * free, the active occlusion query and transform feedback query, the
 * segment open in a pass, and the fence syncs made.  It lives in
 * gles_state.queries and is made at the first query.
 */
struct gles_queries {
	VkQueryPool pool;
	unsigned char used[QUERY_SLOTS];
	struct gles_query *occlusion;
	struct gles_query *feedback;

	/* The segment open (NULL: none): the query, its slot and the command buffer it was begun in. */
	struct gles_query *open;
	uint32_t open_slot;
	VkCommandBuffer open_command;

	/* The fence syncs made and not deleted. */
	struct gles_sync *syncs;
};

static struct gles_queries *query_state(struct zegl_context *context, struct gles_state *state);
static struct gles_query *query_named(struct zegl_context *context, struct gles_state *state, GLuint name);
static struct gles_query **query_active(struct gles_queries *queries, GLenum target);
static int query_slot_take(struct gles_state *state, struct gles_queries *queries, uint32_t *slot);
static void query_slots_free(struct gles_queries *queries, struct gles_query *query);
static int query_finish(struct zegl_context *context, struct gles_state *state, uint64_t frame);
static struct gles_sync *query_sync(struct gles_state *state, GLsync sync);
static int query_signalled(struct zegl_context *context, struct gles_state *state, const struct gles_sync *fence);

/*
 * Opens a segment of the active occlusion query in the pass a draw is
 * about to record into, unless one is open.  Called before each draw.
 */
void
gles_queries_draw(
	struct gles_state *state,
	const struct gles_target *target)
{
	struct gles_queries *queries;
	struct gles_query *query;
	uint32_t *slots;
	uint64_t *frames;
	uint32_t slot;
	int status;

	/* An active occlusion query without a segment open. */
	queries = state->queries;
	if (queries == NULL || queries->occlusion == NULL || queries->open != NULL)
		return;
	query = queries->occlusion;

	/* Room in its list of slots. */
	if (query->slot_count == query->slot_capacity) {
		slots = realloc(query->slots, (query->slot_capacity * 2U + 4U) * sizeof(*slots));
		if (slots == NULL)
			return;
		query->slots = slots;
		frames = realloc(query->frames, (query->slot_capacity * 2U + 4U) * sizeof(*frames));
		if (frames == NULL)
			return;
		query->frames = frames;
		query->slot_capacity = query->slot_capacity * 2U + 4U;
	}

	/* A reset slot. */
	status = query_slot_take(state, queries, &slot);
	if (status != 0)
		return;

	/* Begun in the pass, and kept with the frame that records it. */
	vkCmdBeginQuery(target->command, queries->pool, slot, 0U);
	query->slots[query->slot_count] = slot;
	query->frames[query->slot_count] = state->frame;
	query->slot_count++;
	queries->open = query;
	queries->open_slot = slot;
	queries->open_command = target->command;
}

/*
 * Ends the open segment of an occlusion query, if there is one, before
 * the pass it is in ends (every place that ends a pass calls it first).
 */
void
gles_queries_suspend(
	struct gles_state *state)
{
	struct gles_queries *queries;

	/* A segment open. */
	queries = state->queries;
	if (queries == NULL || queries->open == NULL)
		return;

	/* Ended in the command buffer it was begun in. */
	vkCmdEndQuery(queries->open_command, queries->pool, queries->open_slot);
	queries->open = NULL;
}

/*
 * Makes sure a frame is done, submitting and waiting for the frame being
 * recorded when it is that frame (glReadPixels's way).  Returns 0, or -1
 * with the error recorded.
 */
int
gles_frame_wait(
	struct zegl_context *context,
	struct gles_state *state,
	uint64_t frame)
{
	int status;

	/* The frame done. */
	status = query_finish(context, state, frame);
	if (status != 0)
		return -1;

	/* Succeeded: it is done. */
	return 0;
}

/*
 * Counts primitives a draw wrote into transform feedback buffers for the
 * active GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN query, if there is one.
 */
void
gles_query_primitives(
	struct gles_state *state,
	GLuint primitives)
{
	/* The active query of the target. */
	if (state->queries == NULL || state->queries->feedback == NULL)
		return;

	/* Its count. */
	state->queries->feedback->primitives += primitives;
}

/*
 * Frees a context's queries, fence syncs and query pool (nothing may
 * still run).
 */
void
gles_queries_release(
	struct gles_state *state)
{
	struct gles_queries *queries;
	struct gles_query *query;
	struct gles_sync *sync;
	GLuint name;

	/* Each query object. */
	for (name = 1U; name < state->query_objects.capacity; name++) {
		query = state->query_objects.objects[name];
		if (query == NULL)
			continue;

		/* Its slots, then the object. */
		free(query->slots);
		free(query->frames);
		free(query);
	}

	/* The namespace. */
	free(state->query_objects.objects);
	memset(&state->query_objects, 0, sizeof(state->query_objects));

	/* The fence syncs and the pool. */
	queries = state->queries;
	if (queries == NULL)
		return;
	while (queries->syncs != NULL) {
		sync = queries->syncs;
		queries->syncs = sync->next;
		free(sync);
	}

	/* The pool, then the state. */
	if (queries->pool != VK_NULL_HANDLE)
		vkDestroyQueryPool(state->device, queries->pool, NULL);
	free(queries);
	state->queries = NULL;
}

/*
 * Makes names for queries; each becomes a query at its first
 * glBeginQuery.
 */
GL_APICALL void GL_APIENTRY
glGenQueries(
	GLsizei n,
	GLuint *ids)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_query *query;
	GLsizei index;
	int status;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name gets its object now, so the next name is another. */
	for (index = 0; index < n; index++) {
		query = calloc(1U, sizeof(*query));
		if (query == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* Under the first free name. */
		query->name = gles_names_free(&state->query_objects);
		status = gles_names_add(&state->query_objects, query->name, query);
		if (status != 0) {
			free(query);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name goes back to the application. */
		ids[index] = query->name;
	}
}

/*
 * Deletes queries (an active one ends first).
 */
GL_APICALL void GL_APIENTRY
glDeleteQueries(
	GLsizei n,
	const GLuint *ids)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_query *query;
	GLsizei index;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name that is a query. */
	queries = state->queries;
	for (index = 0; index < n; index++) {
		query = gles_names_get(&state->query_objects, ids[index]);
		if (query == NULL)
			continue;

		/* No longer active or open. */
		if (queries != NULL && queries->open == query)
			gles_queries_suspend(state);
		if (queries != NULL && queries->occlusion == query)
			queries->occlusion = NULL;
		if (queries != NULL && queries->feedback == query)
			queries->feedback = NULL;

		/* Its slots, the name and the object go. */
		if (queries != NULL)
			query_slots_free(queries, query);
		gles_names_remove(&state->query_objects, ids[index]);
		free(query->slots);
		free(query->frames);
		free(query);
	}
}

/*
 * Reports whether a name is a query (one begun at least once).
 */
GL_APICALL GLboolean GL_APIENTRY
glIsQuery(
	GLuint id)
{
	struct gles_state *state;
	struct gles_query *query;

	/* A context with its state. */
	state = gles_state(gles_context());
	if (state == NULL)
		return GL_FALSE;

	/* A name whose object has a target. */
	query = gles_names_get(&state->query_objects, id);
	if (query == NULL || query->target == 0U)
		return GL_FALSE;

	/* Succeeded: it is one. */
	return GL_TRUE;
}

/*
 * Starts a query of a target: an occlusion query counts from here, a
 * transform feedback one the primitives written from here.
 */
GL_APICALL void GL_APIENTRY
glBeginQuery(
	GLenum target,
	GLuint id)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_query *query;
	struct gles_query **active;

	/* A context with the queries' state, and a target. */
	context = gles_context();
	state = gles_state(context);
	queries = query_state(context, state);
	if (queries == NULL)
		return;
	active = query_active(queries, target);
	if (active == NULL) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A query name, not active, of this target or none yet, and no query of the target active. */
	query = query_named(context, state, id);
	if (query == NULL)
		return;
	if (*active != NULL ||
	    query == queries->occlusion ||
	    query == queries->feedback ||
	    (query->target != 0U && query->target != target)) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* One occlusion query at a time, of either form. */
	if (queries->occlusion != NULL && target != GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Started afresh. */
	query_slots_free(queries, query);
	query->target = target;
	query->primitives = 0U;
	query->ended = 0;
	*active = query;
}

/*
 * Ends the active query of a target.
 */
GL_APICALL void GL_APIENTRY
glEndQuery(
	GLenum target)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_query **active;

	/* A context with the queries' state, and a target. */
	context = gles_context();
	state = gles_state(context);
	queries = query_state(context, state);
	if (queries == NULL)
		return;
	active = query_active(queries, target);
	if (active == NULL) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A query of the target active. */
	if (*active == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Its open segment ends; it is no longer active. */
	if (queries->open == *active)
		gles_queries_suspend(state);
	(*active)->ended = 1;
	*active = NULL;
}

/*
 * Reports the active query of a target (GL_CURRENT_QUERY).
 */
GL_APICALL void GL_APIENTRY
glGetQueryiv(
	GLenum target,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_query **active;

	/* A context with the queries' state, a target and the one parameter. */
	context = gles_context();
	state = gles_state(context);
	queries = query_state(context, state);
	if (queries == NULL)
		return;
	active = query_active(queries, target);
	if (active == NULL || pname != GL_CURRENT_QUERY) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Succeeded: its name, 0 for none. */
	*params = 0;
	if (*active != NULL)
		*params = (GLint)(*active)->name;
}

/*
 * Reports an ended query's result, or whether it is available (both are:
 * the frames that recorded it are submitted and waited for first).
 */
GL_APICALL void GL_APIENTRY
glGetQueryObjectuiv(
	GLuint id,
	GLenum pname,
	GLuint *params)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_query *query;
	uint64_t latest;
	uint32_t result;
	unsigned index;
	VkResult status;
	int finished;

	/* A context with the queries' state, and a parameter. */
	context = gles_context();
	state = gles_state(context);
	queries = query_state(context, state);
	if (queries == NULL)
		return;
	if (pname != GL_QUERY_RESULT && pname != GL_QUERY_RESULT_AVAILABLE) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A query begun and not active. */
	query = gles_names_get(&state->query_objects, id);
	if (query == NULL ||
	    query->target == 0U ||
	    query == queries->occlusion ||
	    query == queries->feedback) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The frames that recorded its segments, done (the one being recorded is submitted and waited for). */
	latest = 0U;
	for (index = 0U; index < query->slot_count; index++) {
		if (query->frames[index] > latest)
			latest = query->frames[index];
	}

	/* The latest of them submitted and waited for when it is the one being recorded. */
	finished = query_finish(context, state, latest);
	if (finished != 0)
		return;

	/* Available: the frames are done. */
	if (pname == GL_QUERY_RESULT_AVAILABLE) {
		*params = GL_TRUE;
		return;
	}

	/* A transform feedback query's count. */
	if (query->target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
		*params = query->primitives;
		return;
	}

	/* An occlusion query: whether any segment saw a sample pass. */
	*params = GL_FALSE;
	for (index = 0U; index < query->slot_count; index++) {
		result = 0U;
		status = vkGetQueryPoolResults(state->device, queries->pool, query->slots[index], 1U, sizeof(result), &result,
					       sizeof(result), VK_QUERY_RESULT_WAIT_BIT);
		if (status == VK_SUCCESS && result != 0U)
			*params = GL_TRUE;
	}
}

/*
 * Makes a fence sync: signalled once the commands so far are done.
 */
GL_APICALL GLsync GL_APIENTRY
glFenceSync(
	GLenum condition,
	GLbitfield flags)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_queries *queries;
	struct gles_sync *sync;

	/* A context with the queries' state, the one condition and no flags. */
	context = gles_context();
	state = gles_state(context);
	queries = query_state(context, state);
	if (queries == NULL)
		return NULL;
	if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
		gles_error(context, GL_INVALID_ENUM);
		return NULL;
	}

	/* No flags. */
	if (flags != 0U) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* The object, for the frame being recorded. */
	sync = calloc(1U, sizeof(*sync));
	if (sync == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* The frame it waits for, in the context's list. */
	sync->frame = state->frame;
	sync->next = queries->syncs;
	queries->syncs = sync;

	/* Succeeded: the sync. */
	return (GLsync)sync;
}

/*
 * Reports whether a handle is a fence sync.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsSync(
	GLsync sync)
{
	struct gles_state *state;
	struct gles_sync *fence;

	/* A context with its state. */
	state = gles_state(gles_context());
	if (state == NULL)
		return GL_FALSE;

	/* One of the context's. */
	fence = query_sync(state, sync);
	if (fence == NULL)
		return GL_FALSE;

	/* Succeeded: it is one. */
	return GL_TRUE;
}

/*
 * Deletes a fence sync (0 is ignored).
 */
GL_APICALL void GL_APIENTRY
glDeleteSync(
	GLsync sync)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sync **link;
	struct gles_sync *fence;

	/* A context with its state; 0 is nothing. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || sync == NULL)
		return;

	/* One of the context's. */
	fence = query_sync(state, sync);
	if (fence == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Out of the list, and freed. */
	for (link = &state->queries->syncs; *link != NULL; link = &(*link)->next) {
		if (*link == fence) {
			*link = fence->next;
			break;
		}
	}

	/* Freed. */
	free(fence);
}

/*
 * Waits for a fence sync: signalled already, signalled within the time
 * (the commands so far are submitted and waited for), or not (a time of
 * 0 does not wait).
 */
GL_APICALL GLenum GL_APIENTRY
glClientWaitSync(
	GLsync sync,
	GLbitfield flags,
	GLuint64 timeout)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sync *fence;
	int signalled;
	int finished;

	/* A context with its state, a sync and flags it knows. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return GL_WAIT_FAILED;
	fence = query_sync(state, sync);
	if (fence == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return GL_WAIT_FAILED;
	}

	/* Flags it knows. */
	if ((flags & ~(GLbitfield)GL_SYNC_FLUSH_COMMANDS_BIT) != 0U) {
		gles_error(context, GL_INVALID_VALUE);
		return GL_WAIT_FAILED;
	}

	/* Signalled already. */
	signalled = query_signalled(context, state, fence);
	if (signalled)
		return GL_ALREADY_SIGNALED;

	/* No time to wait. */
	if (timeout == 0U)
		return GL_TIMEOUT_EXPIRED;

	/* The frame so far submitted and waited for. */
	finished = query_finish(context, state, fence->frame);
	if (finished != 0)
		return GL_WAIT_FAILED;

	/* Succeeded: signalled now. */
	return GL_CONDITION_SATISFIED;
}

/*
 * Makes the server wait for a fence sync: its commands run in order on
 * one queue already, so nothing is done.
 */
GL_APICALL void GL_APIENTRY
glWaitSync(
	GLsync sync,
	GLbitfield flags,
	GLuint64 timeout)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sync *fence;

	/* A context with its state, a sync, no flags and no time. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	fence = query_sync(state, sync);
	if (fence == NULL || flags != 0U || timeout != GL_TIMEOUT_IGNORED)
		gles_error(context, GL_INVALID_VALUE);
}

/*
 * Reports a property of a fence sync.
 */
GL_APICALL void GL_APIENTRY
glGetSynciv(
	GLsync sync,
	GLenum pname,
	GLsizei bufSize,
	GLsizei *length,
	GLint *values)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sync *fence;
	GLint value;
	int signalled;

	/* A context with its state, and a sync. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	fence = query_sync(state, sync);
	if (fence == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The property. */
	switch (pname) {
	case GL_OBJECT_TYPE:
		value = GL_SYNC_FENCE;
		break;
	case GL_SYNC_STATUS:
		signalled = query_signalled(context, state, fence);
		value = GL_UNSIGNALED;
		if (signalled)
			value = GL_SIGNALED;
		break;
	case GL_SYNC_CONDITION:
		value = GL_SYNC_GPU_COMMANDS_COMPLETE;
		break;
	case GL_SYNC_FLAGS:
		value = 0;
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Succeeded: the one value, when there is room. */
	if (length != NULL)
		*length = 0;
	if (bufSize < 1)
		return;
	values[0] = value;
	if (length != NULL)
		*length = 1;
}

/* Returns the context's queries' state, making it (and the query pool) at the first use; NULL with the error recorded. */
static struct gles_queries *
query_state(
	struct zegl_context *context,
	struct gles_state *state)
{
	VkQueryPoolCreateInfo create;
	struct gles_queries *queries;
	VkResult result;

	/* A context with its state, whose queries' state exists. */
	if (state == NULL)
		return NULL;
	if (state->queries != NULL)
		return state->queries;

	/* The state. */
	queries = calloc(1U, sizeof(*queries));
	if (queries == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* The occlusion query pool. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	create.queryType = VK_QUERY_TYPE_OCCLUSION;
	create.queryCount = QUERY_SLOTS;
	result = vkCreateQueryPool(state->device, &create, NULL, &queries->pool);
	if (result != VK_SUCCESS) {
		free(queries);
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* Succeeded: kept for the context. */
	state->queries = queries;
	return queries;
}

/* Returns the query object of a name glGenQueries made, recording GL_INVALID_OPERATION when it is not one. */
static struct gles_query *
query_named(
	struct zegl_context *context,
	struct gles_state *state,
	GLuint name)
{
	struct gles_query *query;

	/* The name's object (0 is none). */
	query = NULL;
	if (name != 0U)
		query = gles_names_get(&state->query_objects, name);
	if (query == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the query. */
	return query;
}

/* Returns where the active query of a target is kept, NULL for a name that is not a query target. */
static struct gles_query **
query_active(
	struct gles_queries *queries,
	GLenum target)
{
	/* The occlusion targets share one; transform feedback has its own. */
	switch (target) {
	case GL_ANY_SAMPLES_PASSED:
	case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
		return &queries->occlusion;
	case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
		return &queries->feedback;
	default:
		break;
	}

	/* Not a query target. */
	return NULL;
}

/* Takes a free slot of the pool and resets it through the upload queue; nonzero when none is free or the reset failed. */
static int
query_slot_take(
	struct gles_state *state,
	struct gles_queries *queries,
	uint32_t *slot)
{
	uint32_t index;
	int status;

	/* The first free slot. */
	for (index = 0U; index < QUERY_SLOTS; index++) {
		if (!queries->used[index])
			break;
	}

	/* None free. */
	if (index == QUERY_SLOTS)
		return -1;

	/* Reset before the frame that begins it runs (the upload is submitted and waited for now). */
	status = gles_upload_begin(state);
	if (status != 0)
		return -1;
	vkCmdResetQueryPool(state->upload, queries->pool, index, 1U);
	status = gles_upload_end(state);
	if (status != 0)
		return -1;

	/* Succeeded: the slot is taken. */
	queries->used[index] = 1U;
	*slot = index;
	return 0;
}

/* Gives a query's slots back to the pool and forgets them. */
static void
query_slots_free(
	struct gles_queries *queries,
	struct gles_query *query)
{
	unsigned index;

	/* Each slot. */
	for (index = 0U; index < query->slot_count; index++)
		queries->used[query->slots[index]] = 0U;
	query->slot_count = 0U;
}

/*
 * Makes sure a frame is done: the frame being recorded is submitted and
 * waited for when it is that frame and has commands; an earlier frame is
 * done already.  Returns 0, or -1 with the error recorded.
 */
static int
query_finish(
	struct zegl_context *context,
	struct gles_state *state,
	uint64_t frame)
{
	struct zegl_surface *surface;
	EGLint error;

	/* An earlier frame is done. */
	if (frame < state->frame)
		return 0;

	/* Nothing recorded to submit. */
	surface = context->draw;
	if (surface == NULL || !surface->frame_open)
		return 0;

	/* The passes end, and what was recorded is submitted and waited for. */
	gles_queries_suspend(state);
	gles_target_close(state);
	zegl_frame_leave_pass(surface);
	surface->recorded = 1;
	error = zegl_frame_flush(surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* Everything recorded before is done: the frame's resources are free again. */
	state->frame++;
	gles_collect(state);

	/* Succeeded: the frame is done. */
	return 0;
}

/* Returns the context's fence sync a handle names, or NULL. */
static struct gles_sync *
query_sync(
	struct gles_state *state,
	GLsync sync)
{
	struct gles_sync *fence;

	/* The list of the context's. */
	if (state->queries == NULL)
		return NULL;
	for (fence = state->queries->syncs; fence != NULL; fence = fence->next) {
		if ((GLsync)fence == sync)
			return fence;
	}

	/* Not one. */
	return NULL;
}

/* Reports whether a fence sync is signalled: its frame is done, or nothing of it waits to be submitted. */
static int
query_signalled(
	struct zegl_context *context,
	struct gles_state *state,
	const struct gles_sync *fence)
{
	struct zegl_surface *surface;

	/* An earlier frame is done. */
	if (fence->frame < state->frame)
		return 1;

	/* The frame being recorded with nothing recorded is done too. */
	surface = context->draw;
	if (surface == NULL || !surface->frame_open)
		return 1;

	/* Not yet. */
	return 0;
}
