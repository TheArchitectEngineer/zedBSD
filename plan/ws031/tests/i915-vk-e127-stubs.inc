/*
 * WS031 E-127: the host fixtures of the older executor modules (cmd, res, pipe, cmdbuf, sync) build
 * cmd.c / i915.c without the graphics path (vk/gfx-*.c, vk/inst.c).  These stand-ins say "not mine"
 * for every opcode, so the fixtures keep exercising the modules they were written for.  The graphics
 * path itself is exercised on hardware (plan/ws031/tests/vkloop-hw.sh) and by the pixel oracle.
 */
struct i915_vk_session;
struct i915_vk_reader;
struct i915_vk_writer;
struct i915_vk_device;
struct i915_gem_object;

int i915_vk_gfx_obj_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled);
int i915_vk_gfx_rec_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled);
int i915_vk_inst_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled);
void i915_vk_gfx_session_close(struct i915_vk_session *session);
int drv_i915_vk_blob_attach(struct i915_vk_device *vk, uint64_t blob_id, struct i915_gem_object *object);
void drv_i915_vk_blob_detach(struct i915_vk_device *vk, struct i915_gem_object *object);

int
i915_vk_gfx_obj_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled)
{
	(void)session; (void)opcode; (void)reader; (void)reply;
	*handled = 0;
	return 0;
}

int
i915_vk_gfx_rec_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled)
{
	(void)session; (void)opcode; (void)reader; (void)reply;
	*handled = 0;
	return 0;
}

int
i915_vk_inst_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled)
{
	(void)session; (void)opcode; (void)reader; (void)reply;
	*handled = 0;
	return 0;
}

void
i915_vk_gfx_session_close(struct i915_vk_session *session)
{
	(void)session;
}

int
drv_i915_vk_blob_attach(struct i915_vk_device *vk, uint64_t blob_id, struct i915_gem_object *object)
{
	(void)vk; (void)blob_id; (void)object;
	return 2;	/* ENOENT: no allocation has that identity */
}

void
drv_i915_vk_blob_detach(struct i915_vk_device *vk, struct i915_gem_object *object)
{
	(void)vk; (void)object;
}
