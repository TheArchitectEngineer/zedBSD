# S1 agent reports (condensed)

## dma (done)
- files: dma.h, dma.c (i915-cc ok)
- API: drv_i915_dma_init(dma, ops, context, trace), drv_i915_dma_device_ops(), set_info, address_bits, max_segment, is_coherent, map_sg, map_sgtable, unmap_sg, map_page, unmap_page, pin, unpin, sync_for_device/cpu, live_mappings; types i915_cpu_phys_t/i915_dma_addr_t/i915_gpu_vaddr_t with static inline drv_i915_cpu_phys()/dma_addr()/gpu_vaddr() and *_raw(), drv_i915_dma_mapping_failed(), I915_DMA_MAPPING_ERROR.
- renames: struct osdep_dma_device -> i915_dma; field pinned->pin_count, segs->segments, addr->address, priv->context.
- only probe.c used it in production (init, set_info, backend) + osdep_dma_addr() in probe/gt_mem.
- NOTE: zedBSD errno numbering differs from Linux (EINVAL=3, ENOMEM=4, EBUSY=17, EOPNOTSUPP=21): trace/log values change.
- XXX kept: real ops have address_bits/max_segment/coherent = 0 (probe reads drv_dma_device_* directly); sync ops NULL still record trace.
- integration: probe's static dma -> struct i915_dma member of device.

## pci / runtime-pm / firmware (done)
- files: pci.[ch], runtime-pm.[ch], firmware.[ch] (i915-cc ok); blobs moved by me to data/firmware/{adlp-dmc,tgl-dmc,vbt-dell-latitude-5320,vbt-dell-latitude-5330}.c (symbols drv_i915_firmware_*; + adlp_dmc_checksum).
- pci: struct i915_pci_context {pci, msi_irq(-1 init by caller), msi_source[17]}; drv_i915_pci_init(pci, ops, context, trace); drv_i915_pci_device_ops(); read8/16/32, write8/16/32, find_capability, bar_kind, set_power_state, enable_device, disable_device, is_enabled, restore, set_bus_master, setup_msi (0/ENODEV/EIO), teardown_msi, msi_enabled. field saved_command kept?
- rpm: drv_i915_rpm_init_early(rpm, ops, context, trace), enable, is_enabled, get_noresume, get_sync, resume_and_get, put, usage, active; drv_i915_rpm_device_ops(), drv_i915_rpm_pci_probe_ops() (context = struct i915_pci *).
- firmware: struct i915_firmware {data,size}; drv_i915_firmware_request(fw,name) 0/ENOENT; release; set_override (test hook -> move to tests in S5).
- XXX: MSI alloc failure now EIO (old -1). Callers checking -ENODEV/-ENOENT must check positive.

## sync / workqueue (done)
- sync.h: drv_i915_time_base_ok(), time_base_faulted(), udelay(us) 0/EIO, wait_reg(mmio, reg, mask, value, fast_us, slow_ms, *last) 0/ETIMEDOUT/EIO; struct i915_completion; completion_init(c,name), complete, reinit_completion, wait_for_completion(c, deadline) 1 done/0 timeout.
- workqueue.h: work_init(w, fn, ctx), workqueue_create(q,name)/destroy, queue_work, cancel_work, cancel_work_sync(q,w,deadline), flush_work, work_pending; timer_queue_create(t, wq, name)/destroy, delayed_work_init, delayed_queue(t,d,ms), delayed_cancel, delayed_cancel_sync, delayed_pending. Field renames fn/ctx->function/context, wq->queue/workqueue, tq->timers; counters armed_count, fired_count, cancelled_armed, cancelled_pending, work.ran_count kept.
- test-only (S5): osdep/sync model, timer_calc, wait_test_set/reset_fault, kcomplete_all, kdelayed_flush, run_order.
- XXX: full queue drops silently; cancel_sync deadline returns early; time-base fault is driver-global.

## device-info / reset / power(PCODE) (done)
- drv_i915_gt_init_mmio(struct i915_gt_info *gt, int graphics_ver, uint32_t platform_engine_mask, struct i915_mmio *mmio)
- drv_i915_gt_reset_all(struct spinlock *uncore_lock, struct i915_mmio *mmio, unsigned fast_us); I915_GT_RESET_ACK_US 2000U
- drv_i915_pcode_read(sb_lock, mmio, mbox, *val, *val1); drv_i915_snb_pcode_write(sb_lock, mmio, mbox, val); drv_i915_skl_pcode_request(sb_lock, mmio, mbox, request, reply_mask, reply, timeout_base_ms)
- renames: parity_gt_mmio->i915_gt_info, parity_engine->i915_engine_info, parity_sseu->i915_sseu; data/engine-table.inc
- MUST FIX at S4: display_state.c:486 tests skl_pcode_request `< 0` -> `!= 0`; cdclk diag statuses change sign.
- log: "i915: gt_reset attempt=%u passes=%u rc=%d"

## irq (GT half, done)
- struct i915_irq_dev; drv_i915_irq_install (0/EIO), irq_uninstall, synchronize_irq, irq_reset, irq_postinstall, gen11_gt_irq_reset/postinstall/handler, gen3_irq_reset/assert_iir_is_zero/irq_init (public for S4 display).
- struct i915_irq_display_ops {set_irqs_enabled, uninstall_check, reset, postinstall, handle(ctx,master_ctl), gse} via irq->display_ops/display_context; NULL in S1 (logs XXX).
- display pieces left out: old irq.c 65-158, 238-317, 397-535, 679-802, 917-1216 (list in agent report); display fields dropped (pd, pwc, pch, de_*, vbl...). sync_calls/timeouts moved into i915_irq_dev.
- INTEGRATION TODO (S1): interim display hook that writes GEN11_DISPLAY_INT_CTL=0 in reset to avoid storms (XXX, removed at S4).
- git index shows staged D of i915/irq.c from the git mv; fine (new untracked file); `git add` at integration.

## workarounds / gt-power / verify-workarounds (done; verify needs execution headers)
- workarounds.h: wa builders, gt_init_workarounds_adlp, engine_init_workarounds/ctx_wa/whitelist, wa_list_apply(wal, mmio, verify, result), engine_apply_whitelist, wa_list_dump, get_mocs_settings, mocs_init, init_l3cc_table, tgl_setup_private_ppat(mmio,&writes) [PAT to move to ppgtt.c], gt_init_tables(struct i915_gt_init*, gt, ver, sb_lock, mmio), gt_init_hw_core(gt_init, gt, mmio), engine_apply_resume_wa(gt_init, gt, index, mmio). I915_WA_ENGINES=6.
- gt-power.h: rc6_init, gen11_rc6_enable(rc6,mmio,gt), rc6_sanitize, rps_init(rps, sb_lock, mmio), rps_enable, rps_sanitize.
- verify-workarounds.h: engines_verify_workarounds(verify, engines, gt_init, gt_mem, mmio, timeout_ms), engines_verify_wa_release(verify, gt_mem); struct i915_gt_verify_wa. Needs request.h/engine.h/submit.h/memory.h names: i915_gt_request(.error), i915_gt_engines, i915_gt_engine, i915_execlists, i915_gt_object, i915_gt_context; drv_i915_ring_begin/advance, request_create/add/completed, execlists_submit/process_csb, gt_object_create/destroy, gt_ggtt_bind; I915_GEM_HWS_SEQNO_ADDR (not I915_I915_).
- data: i915-gt-workarounds.inc, i915-gt-mocs-table.inc, i915-gt-power.inc, i915-mcr-ranges.inc. MACRO CLASH with data/i915-regs.inc & i915-workarounds.inc (BLIT_CCTL, GEN12_GLOBAL_MOCS, GEN9_LNCFCMOCS, GEN8_MCR_SELECTOR, RING_CMD_CCTL, HIZ_CHICKEN) -> don't co-include; resolve at integration.
- dropped: test skip rc6 switch; unused wa_write, masked_dis, gt_init_workarounds.
- XXX: MOCS entry 63 missing vs Linux 6.8 (LNCFCMOCS31 upper half L3_3_WB vs L3_1_UC) — keep, check vs Linux dump later.

## memory / ggtt / ppgtt / tlb (done; tlb needs engine.h)
- memory.h: gt_mem_init(gm, drv_dma_device*, dma_mask, table, entries, scratch_pte, i915_mmio*), gt_mem_fini, gt_object_create/destroy/page_dma, gt_clflush; gem_create(struct i915_gem_registry*, bytes, &obj), gem_destroy(registry, obj), gem_share_put(registry,obj), gem_bind_vm(vm,obj), gem_unbind_vm, gem_read/write.
  -> device must embed `struct i915_gem_registry` (objects, object_count, quarantined_objects).
- ggtt.h: dma_in_range, ggtt_pte_encode(i915_dma_addr_t, len, mask, &pte) 1/0, gt_ggtt_bind/unbind/flush/read_pte, gt_display_window_init/bind/unbind/bind_foreign/unbind_foreign, gt_init_scratch.
- ppgtt.h: gen12_ppgtt_pte_encode, gen8_pde_encode(_cached), gt_ppgtt_create/destroy/alloc_range/foreach_pt/insert_page/insert_scratch; legacy ppgtt_create/destroy/va_alloc/insert/insert_uncached/clear.
- tlb.h: gt_invalidate_tlb_full(tlb, i915_gt_engines*, mmio, uncore_lock) — uses es->n, es->ge[i].info->class.
- dropped: gem_bind_ggtt/unbind_ggtt (retired), gt_ppgtt_walk (tests), ppgtt_pte_encode (tests), ppgtt_lookup (unused).
- INTEGRATION: verify-workarounds.c needs #include "ggtt.h"; PAT to ppgtt.c.
- logs: "i915: gt memory: ...", "i915: TLB invalidation ...".

## execution (done)
- context.h: lrc_alloc(ce, ge, vm(i915_gt_ppgtt*), gm, ring_size, sw_id), lrc_init_state, lrc_reset, lrc_update_regs(ce, head), lrc_aux_inv_reg, lrc_release(ce, gm).
- request.h: ring_begin/advance, gen12_emit_aux_table_inv, emit_ctx_wa, request_create(rq, ce, seqno, hwsp_ggtt, hwsp_cpu), request_add, request_completed.
- submit.h: execlists_init, submission_setup, enable, reset_csb_pointers, reset_prepare(ge, mmio), execlists_submit(ge, el, mmio, rq), execlists_process_csb(ge, el, mmio).
- engine.h: engine_setup_common, engine_release, stop_cs, wait_for_pending_mi_fw, engine_dump, engines_init(es, gt_info, gm, pp), engines_release(es, gm), gt_resume(es, gt_init, gt_info, mmio, uncore_lock).
- defaults.h: engines_record_defaults(d, es, gi, gm, pp, mmio, uncore_lock, timeout_ms), engines_defaults_release(d, gm).
- migrate.h: migrate_init(m, es, gm), migrate_fini(m, gm). pxp.h: pxp_init(x, es, gt_vm, gm, has_pxp), pxp_fini(x, gm).
- data/i915-execution.inc; Linux names (CTX_RING_TAIL, MI_NOOP, I915_GEM_HWS_SEQNO_ADDR) used, not I915_CTX_*.
- test-only statics needing exposure in S5: lrc_state_size, lrc_set_offsets, sseu_make_rpcs, gen12_csb_parse, defaults submit/poll/finish.
- verify-workarounds.c needs #include "ggtt.h".
