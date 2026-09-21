# S2/S3 agent reports (condensed)

## S3b render objects (done)
- render/gfx.h (struct i915_gfx_X; GFX_MAX_*/GFX_PUSH_BYTES -> I915_GFX_*; enum i915_gfx_op_kind I915_GFX_OP_*), memory.c (+ drv_i915_render_blob_attach/detach, gfx_memory_cpu/va), image.c, descriptor.c, pipeline.c (object part), render-pass.c, sync.c (semaphore), objects.c (drv_i915_gfx_obj_dispatch, i915_gfx_destroy_plain), reply.c (drv_i915_gfx_result/create_tail/create_reply(void)).
- XXX: live memory list is a file-scope static in memory.c (move into i915_render_device at integration); pipeline create partial failure leaks unpublished pipelines (old behaviour widened XXX).

## S2 compiler (done, verified)
- compiler/{ir.h, compiler.h, spirv.c, compile.c, eu.h, eu.c}, data/eu-encoding-gen12.inc. i915-cc ok; host fixtures "spirv lower eu compile" PASS (normal+ASan/UBSan); gentool PASS; kernels md5 identical to old; old-vs-new output identical over 420k lines incl 40k mutated SPIR-V.
- API: drv_i915_shader_parse(words, count, stage, &ir, diag|NULL), shader_ir_free, shader_compile(ir, &bin) [vk arg dropped], shader_binary_free; eu.h 16 drv_i915_eu_*.
- renames: i915_shader_ir(_inst/_io/_uniform), enum i915_shader_ir_op, enum i915_shader_stage, i915_compile_diagnostic, i915_shader_binary, i915_eu_buf/reg, I915_IR_*, I915_STAGE_*, I915_EU_*.
- tests updated: i915-vk-{gentool-eu,eudump,eu-test,compile-test,spirv-test,lower-test}.c include compiler/*.c.
- open: eu-encoding inc lacks full MIT notice text (Mesa) — decide.

## S3a render entry/transport (done)
- render/{render.h, internal.h, vulkan.c, dispatch.c/.h, transport.c/.h, codec.c/.h, object.c/.h, instance.c/.h}, data/vulkan-codec.inc (generator gen_vk_server_codec.py updated; output identical modulo names).
- ops wired: i915.c attach/detach render, session.c open/close, resource.c blob attach/detach, command.c executor path; XXX lines removed.
- OLD MODULES: only vk/sync.c fences (opcodes 35-38 create/destroy/reset/status) are reached by vkdemo (offscreen + display). Others unreachable/no effect. dispatch refuses routes to unported modules (XXX, ENOTSUP, poison).
  -> MUST PORT fence part of old vk/sync.c into render/sync.c + dispatch route before vkdemo works; S3c submit looks up fence objects.
- host fixture cmd rewritten & PASS; resdispatch/res/pipe/cmdbuf/sync fixtures need rewrite against new files (S5).
- XXX: opcode 180 recursion unbounded (old).
- object table still per render device (A05 later).

## S3c recording/draw (done)
- render/{command,state,draw,blit,batch,math}.c/.h, pipeline-prepare.c, heap.h, data/i915-3dstate-gen12.inc. byte-identical state/batch vs old in 12 cases.
- test-only left: reference kernels (vkref-generated.inc), gfx_census, I915_VK_GFX_DUMP. Oracle: implement weak drv_i915_gfx_draw_checkpoint(target, draw) printing `vkdump %u %u %s` rows for draw<=1; vkloop-hw.sh oracle must link it (S5).
- XXX: rect kernel cache / discard op file-scope statics; MAX_VS_THREADS in heap.h (device-info later).

## Integration (me)
- render/fence.c/.h: ported fences 35-38 from old vk/sync.c (latch only; arm/wait were only used by unported wsi/cmdbuf). dispatch FENCE route -> drv_i915_render_fence_dispatch; command.c calls drv_i915_fence_signal directly.
- HW: vkdemo offscreen 14 frames, frame1 7523debe…05ff = E-127/E-128.
