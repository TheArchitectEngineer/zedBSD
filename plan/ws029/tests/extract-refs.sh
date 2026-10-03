#!/bin/sh
# Extract the definitions and reference function bodies from the fetched Linux i915 sources (v6.19).
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
src="$repo/plan/ws029/temp/linux/v6.19"
out="$repo/plan/ws029/temp/refs-extract.txt"
: > "$out"
func() { # file name
    awk -v name="$2" 'BEGIN{p=0} p==0 && $0 ~ ("^[A-Za-z].*[ \t*]" name "\\(") && $0 !~ /;[ \t]*$/ {p=1} p{print} p && /^}/ {p=0; exit}' "$src/$1"
}
array() { # file name
    awk -v name="$2" 'BEGIN{p=0} p==0 && index($0, name "[] = {") {p=1} p{print} p && /^};/ {p=0; exit}' "$src/$1"
}
section() { printf '\n===== %s =====\n' "$1" >> "$out"; }
section "intel_pci_config.h (whole)"; cat "$src/intel_pci_config.h" >> "$out"
section "intel_lrc_reg.h (whole)"; cat "$src/gt/intel_lrc_reg.h" >> "$out"
section "intel_engine_regs.h (whole)"; cat "$src/gt/intel_engine_regs.h" >> "$out"
section "intel_gtt.h defines"; grep -n "define GEN8_PAGE\|define GEN12_PPGTT\|define GEN8_PTE\|define GEN8_PDE\|define GEN8_PML4\|define GEN8_3LVL\|define GEN8_PDES\|define I915_GTT_PAGE\|define GEN12_GGTT\|define GEN8_GGTT\|define PTE_\|define GEN6_PTE\|define I915_PDES\|define GEN8_PPGTT" "$src/gt/intel_gtt.h" >> "$out"
section "intel_gt_regs.h selected"; grep -n "GEN6_GDRST\|GRDOM\|FORCEWAKE\|GFX_FLSH_CNTL\|GEN11_GFX_MSTR_IRQ\|GEN11_MASTER_IRQ\|GEN11_GT_DW_IRQ\|GEN11_GT_INTR_DW\|GEN11_INTR_IDENTITY\|GEN11_IIR_REG\|GEN11_INTR_\|GEN11_RENDER_COPY_INTR\|GEN11_VCS_VECS_INTR\|GEN11_RCS0_RSVD\|GEN11_BCS_RSVD\|GEN11_GUC_SG\|GEN12_GLOBAL_MOCS\|GEN9_LNCFCMOCS\|GEN12_GT_GEOMETRY\|define GEN12_CSB\|GEN8_GT_IIR\|GT_RENDER_USER_INTERRUPT\|GT_CONTEXT_SWITCH\|GT_CS_MASTER_ERROR\|GEN11_DISPLAY_IRQ\|GEN11_GU_MISC_IRQ\|define GEN11_GT_INTR\|RENDER_RING_BASE\|BLT_RING_BASE\|GEN11_BSD_RING_BASE\|GEN11_VEBOX_RING_BASE\|MOCS" "$src/gt/intel_gt_regs.h" >> "$out"
section "i915_reg.h selected"; grep -n "define GEN6_GDRST\|GRDOM\|GFX_FLSH_CNTL\|GEN11_GFX_MSTR_IRQ\|GEN11_MASTER_IRQ\|GEN11_GT_DW\|GEN11_DISPLAY_IRQ\|GEN11_GU_MISC\|GEN11_INTR\|GEN11_IIR\|GEN11_RENDER_COPY\|GEN11_RCS0\|GEN11_BCS\|define _MMIO\|define GEN12_DSMBASE\|define GEN6_PCODE\|GEN8_GT_IIR\|GEN8_GT_IER\|GEN8_GT_IMR" "$src/i915_reg.h" | head -60 >> "$out"
section "i915_reg_defs.h _MMIO etc"; grep -n "define _MMIO\|define REG_BIT\|define REG_GENMASK\|define REG_FIELD_PREP\|define REG_FIELD_GET\|typedef.*i915_reg_t" "$src/i915_reg_defs.h" >> "$out"
section "intel_gpu_commands.h selected"; grep -n "MI_INSTR\|MI_NOOP\|MI_USER_INTERRUPT\|MI_BATCH_BUFFER_START\|MI_BATCH_BUFFER_END\|MI_BATCH_NON_SECURE\|MI_BATCH_RESOURCE\|MI_FLUSH_DW\|MI_INVALIDATE\|MI_STORE_DWORD_IMM\|MI_MEM_VIRTUAL\|MI_USE_GGTT\|MI_SEMAPHORE_WAIT\|MI_SEMAPHORE_POLL\|MI_SEMAPHORE_SAD\|MI_SEMAPHORE_GLOBAL\|MI_LOAD_REGISTER_IMM\|MI_LRI_\|MI_ARB\|MI_STORE_REGISTER_MEM\|XY_SRC_COPY_BLT\|XY_COLOR_BLT\|COLOR_BLT\|SRC_COPY_BLT\|BLT_WRITE\|BLT_DEPTH\|XY_FAST\|GEN12_.*BLT\|define PIPE_CONTROL\|PIPE_CONTROL_\|GFX_OP_PIPE_CONTROL\|MI_PREDICATE\|MI_TOPOLOGY\|MI_BATCH_PPGTT\|MI_STORE_DATA_IMM\|MI_COPY_MEM_MEM\|MI_ATOMIC" "$src/gt/intel_gpu_commands.h" >> "$out"
section "intel_lrc.c arrays and helpers"
for n in gen12_xcs_offsets gen12_rcs_offsets; do array gt/intel_lrc.c $n >> "$out"; done
grep -n "define NOP\|define LRI\|define REG(\|define REG16\|define END\|define LRC_STATE_OFFSET\|define LRC_PPHWSP\|define GEN8_LR_CONTEXT\|define GEN12_LR_CONTEXT\|LRC_PPHWSP_SZ\|LRC_STATE_PN\|CTX_R_PWR\|lrc_ring_mi_mode\|lrc_ring_bb_offset\|lrc_ring_gpr0\|lrc_ring_indirect\|lrc_ring_cmd_buf\|lrc_ring_wa_bb" "$src/gt/intel_lrc.c" | head -60 >> "$out"
for f in set_offsets lrc_ring_mi_mode lrc_ring_bb_offset lrc_ring_gpr0 lrc_ring_indirect_ptr lrc_ring_indirect_offset lrc_ring_cmd_buf_cctl init_common_regs init_ppgtt_regs lrc_init_regs lrc_reset_regs __lrc_init_regs lrc_descriptor lrc_pin lrc_update_regs lrc_ring_indirect_offset_default lrc_init_state lrc_setup_bb_per_ctx lrc_ring_indirect_ptr setup_indirect_ctx_bb lrc_init_wa_ctx; do func gt/intel_lrc.c $f >> "$out"; done
section "intel_execlists_submission.c"
for f in write_desc execlists_submit_ports csb_read gen12_csb_parse process_csb reset_csb_pointers enable_execlists execlists_reset_prepare execlists_sanitize execlists_update_context ring_set_tail? execlists_hold? intel_execlists_submission_setup; do func gt/intel_execlists_submission.c "$f" >> "$out"; done
grep -n "execlists_update_context\|RING_EXECLIST_SQ\|EL_CTRL_LOAD\|csb_write\|CSB_WRITE\|GEN11_CSB_WRITE_PTR\|RING_CONTEXT_STATUS_PTR" "$src/gt/intel_execlists_submission.c" | head -40 >> "$out"
section "intel_gt_irq.c"
for f in gen11_gt_irq_postinstall gen11_gt_irq_reset gen11_gt_irq_handler gen11_gt_bank_handler gen11_gt_identity_handler gen11_gt_engine_identity gen11_gt_intr_from_tile gen11_engine_irq_handler cs_irq_handler gen11_gt_identity_handler; do func gt/intel_gt_irq.c $f >> "$out"; done
grep -n "irq_handler\|gen11_gt_irq_handler\|intel_engine_cs_irq\|GEN11_GFX_MSTR_IRQ\|GEN11_MASTER_IRQ" "$src/gt/intel_gt_irq.c" | head -30 >> "$out"
section "intel_reset.c"
for f in gen11_reset_engines gen8_engine_reset_prepare gen8_engine_reset_cancel gen8_reset_engines gen6_hw_domain_reset gen11_lock_sfc gen11_unlock_sfc intel_gt_reset_engine __intel_gt_reset __reset_engine intel_reset_get_unlock? ; do func gt/intel_reset.c "$f" >> "$out"; done
grep -n "GEN11_GRDOM\|gen11_engine_reset_domains\|hw_domain\[" "$src/gt/intel_reset.c" | head -30 >> "$out"
section "intel_ggtt.c"
for f in gen8_get_total_gtt_size gen8_gmch_probe ggtt_probe_common gen8_ggtt_pte_encode gen8_ggtt_invalidate gen8_ggtt_insert_page gen8_ggtt_insert_entries gen8_ggtt_clear_range gen8_set_pte gen8_get_pte ggtt_init_hw gen8_ggtt_bind_get_ce? ggtt_probe_hw intel_ggtt_init_hw ggtt_set_pages? gen8_ggtt_insert_page_bind? ; do func gt/intel_ggtt.c "$f" >> "$out"; done
section "gen8_ppgtt.c"
for f in gen8_pte_encode gen8_pde_encode gen8_pdpe_encode? gen8_init_scratch gen8_ppgtt_create gen8_ppgtt_insert_pte gen8_ppgtt_insert gen8_ppgtt_alloc gen8_ppgtt_clear __gen8_ppgtt_alloc __gen8_ppgtt_clear gen8_pd_top_count gen8_ppgtt_notify_vgt? gen8_ppgtt_insert_entry gen8_pdp_for_page_index gen8_pd_range? gen8_alloc_top_pd gen8_pdp_for_page_address gen8_ppgtt_foreach? ; do func gt/gen8_ppgtt.c "$f" >> "$out"; done
grep -n "define GEN8_PD\|define gen8_pd\|define as_pd\|define gen8_pdp\|define GEN8_PTE\|define GEN8_PD_SHIFT\|define gen8_pd_top\|GEN8_PDE_IPS_64K" "$src/gt/gen8_ppgtt.c" "$src/gt/intel_gtt.h" | head -20 >> "$out"
section "intel_uncore.c forcewake"
for f in fw_domain_get fw_domain_wait_ack_set fw_domain_wait_ack_clear fw_domain_put fw_domains_get_normal fw_domain_init intel_uncore_fw_domains_init intel_uncore_forcewake_reset fw_domain_wait_ack_with_fallback fw_domains_reset fw_domain_reset; do func intel_uncore.c $f >> "$out"; done
grep -n "GRAPHICS_VER(i915) >= 11\|FORCEWAKE_GT_GEN9\|FORCEWAKE_ACK_GT_GEN9\|FORCEWAKE_RENDER_GEN9\|FORCEWAKE_ACK_RENDER_GEN9\|FORCEWAKE_MEDIA_VDBOX_GEN11\|FORCEWAKE_ACK_MEDIA_VDBOX_GEN11\|FORCEWAKE_MEDIA_VEBOX_GEN11" "$src/intel_uncore.c" | head -30 >> "$out"
section "intel_engine_cs.c engines table"
array gt/intel_engine_cs.c intel_engines >> "$out"
grep -n "define MAX_MMIO_BASES\|mmio_bases\|GRAPHICS_VER(i915) >= 11\|engine_mask\|intel_engine_setup\|RING_HWS_PGA\|GFX_RUN_LIST_ENABLE\|RING_MODE_GEN7\|RING_MI_MODE\|STOP_RING\|intel_engine_stop_cs\|intel_engine_resume\|intel_engine_init_execlists\|intel_engine_set_hwsp_writemask" "$src/gt/intel_engine_cs.c" | head -40 >> "$out"
for f in intel_engine_stop_cs intel_engine_wait_for_pending_mi_fw intel_engine_set_hwsp_writemask intel_engine_init_execlists engine_setup_common intel_engine_setup? ; do func gt/intel_engine_cs.c "$f" >> "$out"; done
section "gen8_engine_cs.c emission"
for f in gen8_emit_bb_start gen8_emit_bb_start_noarb gen12_emit_flush_xcs gen12_emit_fini_breadcrumb_xcs gen8_emit_ggtt_write gen12_emit_ggtt_write_rcs gen8_emit_fini_breadcrumb_tail gen12_emit_fini_breadcrumb_tail gen12_emit_flush_rcs gen12_emit_fini_breadcrumb_rcs gen12_emit_pipe_control gen8_emit_pipe_control gen12_emit_ggtt_write_rcs __gen8_emit_flush_dw gen8_emit_flush_xcs gen12_emit_aux_table_inv gen8_emit_init_breadcrumb gen12_emit_preempt_busywait? gen12_emit_flush_xcs; do func gt/gen8_engine_cs.c "$f" >> "$out"; done
section "intel_mocs.c gen12"
array gt/intel_mocs.c gen12_mocs_table >> "$out"
for f in init_global_mocs __init_mocs_table get_mocs_settings intel_mocs_init get_entry_control intel_set_mocs_index? mocs_register? ; do func gt/intel_mocs.c "$f" >> "$out"; done
grep -n "GEN12_GLOBAL_MOCS\|define LE_\|define L3_\|define MOCS_ENTRY\|define _L3_CACHEABILITY\|define L3_LKUP\|define LE_CoS\|define LE_SCC\|define LE_PFM\|define LE_SCF\|define LE_AOM\|define LE_TC\|define LE_CACHEABILITY\|define LE_TGT_CACHE\|define LE_LRUM\|define L3_ESC\|define L3_SCC\|define L3_GLBGO\|define I915_MOCS\|define MOCS_TABLE\|MOCS_TABLE_SIZE\|GEN12_MOCS\|gen12_mocs_table" "$src/gt/intel_mocs.c" | head -50 >> "$out"
section "i915_pci.c ADL-P"
grep -n "adl_p\|ADLP\|alderlake_p\|INTEL_ADLP" "$src/i915_pci.c" | head -20 >> "$out"
array i915_pci.c adl_p_info >> "$out" || true
awk '/static const struct intel_device_info adl_p_info/,/^};/' "$src/i915_pci.c" >> "$out"
awk '/define XE_LP_FEATURES/,/^$/' "$src/i915_pci.c" >> "$out"
awk '/define GEN12_FEATURES/,/^$/' "$src/i915_pci.c" >> "$out"
grep -n "INTEL_ADLP_IDS\|46a8\|define INTEL_ADLP\|define INTEL_ADLN\|define INTEL_RPLP" "$src/include/drm/intel/pciids.h" | head -20 >> "$out"
awk '/define INTEL_ADLP_IDS/,/^$/' "$src/include/drm/intel/pciids.h" >> "$out"
wc -l "$out"
