<!-- awesome-plan project=zedbsd record=ws244-p005 -->

# ws244-p005 GT の submit: context・request・submit・engine の書き直し

Status: cleared（2026-10-19 Q1 判定: context・request・submit の書き直し（前の版との比べ context 559・request 292・submit 724 行一致）、engine・defaults・worker の言及の掃除、build 4 組 warning 0（P1 の報告）。ac058991c・0f4a846f2、merge 7ebce4341。log の文言に残る Linux の関数名は p009 で直す（Q1 の判断））（旧: in-progress（2026-10-19 q1260 P1。範囲は済み、cleared は Q1））

- 親: [WS244](../ws.md)。範囲: context.c・context.h・request.c・request.h・submit.c・submit.h・engine.c・engine.h と、p004 から移した defaults.c・defaults.h・worker.c の言及。動作は変えない。

## 実行の記録（2026-10-19）

### 1. context.c・context.h（logical ring context の image）

- 前の版は「The reference functions this file stands for are __lrc_alloc_state() … lrc_reset()」で、関数の分け方と名前が Linux の lrc_init_state・__lrc_init_regs・init_common_regs・init_ppgtt_regs・__reset_stop_ring・lrc_setup_indirect_ctx・gen12_emit_timestamp_wa・gen12_emit_cmd_buf_wa・gen12_emit_restore_scratch・setup_predicate_disable_wa・gen9_init_indirectctx_bb に 1 対 1 で、世代の違いは関数ごとの `gen9 ?` の分岐だった。
- 書き直し: 世代ごとの layout の記録（`struct i915_lrc_layout`: register の表、engine 相対の符号化、MI_MODE・batch offset・INDIRECT_CTX の slot、既定の offset、control の bit、power clock の関数、workaround の batch の関数）を 1 つ選んで使う形にした。register state は 1 つの関数（`i915_lrc_lay_regs`）で表・control・timestamp・batch offset・PDP0・MI_MODE を書く。Gen12 の INDIRECT_CTX は「保存値から GPR0 へ load」「GPR0 から register へ copy」の 2 つの部品で timestamp・CMD_BUF_CCTL・GPR0 を組み立てる。表の符号の解き方は `Procedure referenced: Linux v6.19 …/intel_lrc.c (set_offsets)`、slot の値は `Values referenced: … (lrc_ring_*)`（v6.19 の値と照合済み）。
- 公開の名前: `drv_i915_lrc_init_state` → `drv_i915_lrc_build_image`、`drv_i915_lrc_update_regs` → `drv_i915_lrc_set_ring`、`drv_i915_lrc_reset` → `drv_i915_lrc_scrub`。struct の field `lrc_reg_state` → `regs`、`lrc_desc` → `desc`（`lrca` は PRM の語なので残した）。呼び手（defaults.c・migrate.c・pxp.c・submit.c・worker.c・engine.c・tests/execution の eu-test・fhd-render・ktest-gt・draw-test）を追従。
- 確かめ: `plan/ws244/tests/context-equiv/run.sh <前の context.c> <前の context.h> <今の context.c> <今の context.h> <新しい出力の dir>`。Gen12・Gen9 の 5 engine（render・copy・video 2・enhancement）× 3 つ（default state なし、あり、scrub と ring の大きさ違い）で、image の全 page の digest と register page・batch page の全 dword、ring・descriptor・batch の記録が前の版と一致、alloc の失敗 4 つと不正な引数も一致（`context-equiv: PASS (559 lines)`）。

### 2. request.c・request.h（ring に request を書く）

- 前の版は intel_ring_begin・intel_ring_advance・gen12_emit_flush_rcs/xcs・gen8_emit_flush_rcs/xcs・gen8_emit_fini_breadcrumb_*・gen12_emit_preempt_busywait・gen8_emit_wa_tail と 1 対 1 で、request.h は「re-derived from the reference (gen8_engine_cs.c …) in the order the reference emits it」だった。
- 書き直し: engine の種類ごとの emitter の記録（`struct i915_request_emitter`: flush、seqno の書き込み、tail、breadcrumb の dword 数）を 4 つ（Gen12 render・Gen12 その他・Gen9 render・Gen9 その他）持ち、request の手順はその記録だけを使う。seqno の書き込みは PIPE_CONTROL の qword と MI_FLUSH_DW の store の 2 部品、tail は PREEMPT の semaphore（Gen12 は token の 5 dword、Gen9 は 4 dword と NOOP）と request の終わり（tail・wa_tail の記録）の部品から組む。
- 公開の名前: `drv_i915_ring_begin` → `drv_i915_ring_reserve`、`drv_i915_ring_advance` → `drv_i915_ring_commit`、`drv_i915_gen12_emit_aux_table_inv` → `drv_i915_emit_aux_inv`、`drv_i915_emit_ctx_wa` → `drv_i915_emit_context_wa`、`drv_i915_request_create` → `drv_i915_request_open`、`drv_i915_request_add` → `drv_i915_request_close`。request.h の「software request queue は this file」の誤りも直した（request-queue.h）。
- 確かめ: `plan/ws244/tests/request-equiv/run.sh <前の request.c> <前の request.h> <今の request.c> <今の request.h> <新しい出力の dir>`。Gen12・Gen9 の 5 engine で open（invalidate）・context の workaround（masked・全 bit・読み書き）・close・completed の 3 値、mmio なしの workaround、失敗の後の close、不正な slot 2 つ、奇数の reserve、大きすぎる reserve、wrap、AUX の emitter で、ring の全 dword と request の記録・register の読みが一致（`request-equiv: PASS (292 lines)`）。

### 3. submit.c・submit.h（execlists）

- 前の版は「follows the reference's intel_execlists_submission.c for Gen12」で、intel_execlists_submission_setup・enable_execlists・reset_csb_pointers・execlists_reset_prepare/rewind・process_csb・gen8/__gen12_csb_parse・schedule_in/out・execlists_update_context・execlists_submit_ports・csb_read の名前と説明、世代の違いは関数の中の `graphics_ver == 9` の分岐だった。
- 書き直し: 世代ごとの layout の記録（`struct i915_execlists_layout`: ELSQ か ELSP か、CSB の write pointer の dword と長さ、descriptor の engine の field、legacy mode を切る bit、engine の割り込みを開くか、promotion の判定の関数）にまとめ、bind・enable・drain がそれを使う。内部の関数名を自前に（pause・open_errors・take_tag・give_tag・prepare_context・write_port・read_csb・report）。
- 公開の名前: `drv_i915_execlists_submission_setup` → `drv_i915_execlists_bind`、`_reset_csb_pointers` → `_csb_restart`、`_reset_prepare` → `_quiesce`、`_reset_rewind` → `_abandon`、`_process_csb` → `_drain_csb`（呼び手 engine.c・defaults.c・device.c・verify-workarounds.c・worker.c・eu-test.c・ktest-gt.c）。
- 確かめ: `plan/ws244/tests/submit-equiv/run.sh <前の submit.c> <前の submit.h> <今の submit.c> <今の submit.h> <新しい出力の dir>`。Gen12・Gen9 の render・copy で bind（未 setup と setup）、enable（ESR 0 と非 0）、CSB の restart、submit・busy・promote・complete、進まない tail（強制 restore）、見えない CSB の entry（MMIO の fallback）、3 種の CSB の誤り、ring の一周、quiesce・abandon、tag なし・未 close の submit で、register の読み書き・barrier・log・状態の全部が一致（`submit-equiv: PASS (724 lines)`）。

### 4. engine.c・engine.h・defaults.c・defaults.h・worker.c の言及

- engine.c・engine.h: init_status_page・intel_engines_init・intel_gt_resume の段の名前・intel_engine_stop_cs・__gpm_wait_for_fw_complete ほかの言及を自前の説明にし、GT の resume の順は `Procedure referenced: Linux v6.19 …/intel_gt_pm.c (intel_gt_resume)` を付けた。code は変えていない。
- defaults.h: Linux の関数の順の表を自前の説明に書き直し、`Procedure referenced: Linux v6.19 …/intel_gt.c (__engines_record_defaults)`。defaults.c: null render state の表と reloc に `Values referenced: Linux v6.19 …/gen9_renderstate.c`、ほかの言及を外した。worker.c: execbuf・gen8_emit_*・waitboost・intel_context_create の言及を外した。
- log の文言（`i915: intel_engines_init: …`、`gt_sanitize: reset_engines … (continuing, as the reference does)`、defaults の失敗の段の名前 `intel_context_create` など）は前の Phase と同じく変えていない（p009 の規約の見直しで扱うか Q1 に判断を仰ぐ）。

### build（2026-10-19、1〜4 の後）

- amd64 の既定・I915_TESTS=y・I915_TEST_SET=vke2・I915_TEST_SET=execution の 4 つとも rc=0、warning 0、`amd64 vmunix check: PASS`（BUILD=build/ws244-k-default・-tests・-vke2・-execution）。

## 残り

- 範囲は済み。log の文言に残る Linux の関数名（上）の扱い。
- context-equiv・request-equiv・submit-equiv の試験は開発の確かめ用（前の版を入力に取る）。p005 が cleared になったら消してよい（path を Q1 へ）。
