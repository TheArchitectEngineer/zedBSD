<!-- awesome-plan project=zedbsd record=ws252-p004 -->

# ws252-p004: ICL の GT の core と WA の list

Status: in-progress（2026-10-26 q1599 P4: 実装・host 試験・build・5330 の回帰（Gen12 の不変）済み。ICL の実機は無く（D5）未確認。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §6.2 の「直す（p004、GT の core）」（D2「実機ができたら全部」、2026-10-19 ユーザー）

## やった事（e68361995）

- product.c: ICL の行（graphics 11・display 11、RCS・BCS・VCS0・VCS2・VECS0、`I915_PLATFORM_GEN11`）、intel/pci-ids.h に `I915_PCI_ICL_IDS`（Linux v6.8.12 `INTEL_ICL_11_IDS`）。attach は p006。
- device-info.c: 第 3 の世代の記述 `i915_info_gen11`（Gen12 の engine と reset の domain、Gen8 の fault register 0x4094/0x4b10/0x4b14）と `i915_info_sseu_gen11`（1 slice × 8 subslice × 8 EU、0x913c は無効な subslice の fuse、Linux `gen11_sseu_info_init`）。世代の記述に fuse の読み方の関数を持たせた。`I915_SSEU_MAX_SUBSLICES` 6 → 8。
- mmio.c と `intel/forcewake-ranges-gen11.inc`: Gen11 の forcewake の表（Linux `__gen11_fw_ranges`）、device.c が版 11 でこれを選ぶ。
- context.c と intel/lrc-offsets.h: Gen11 の layout（render は `gen11_rcs_offsets`、他は gen9 の表、engine 相対の LRI、Gen9 の slot、indirect の既定 0x1A、context control は Gen12 と同じ、WA の batch 無し）、Gen11 の RPCS（Linux `intel_sseu_make_rpcs`: 4 を超える subslice は 2 slice として要求、EU は最大）。engine に `sseu_subslice_mask`。
- request.c: Gen11 の render の flush（flush と invalidate の 2 つの PIPE_CONTROL）と breadcrumb（tile cache を含む flush）、他の engine は Gen9 の emitter（Linux `gen11_emit_flush_rcs`・`gen11_emit_fini_breadcrumb_rcs`）。
- submit.c: Gen11 の execlists layout（ELSQ・Gen11 の CSB の位置と数・descriptor の engine の field、CSB は Gen8 の解き方）。
- tlb.c: Gen11 は Gen8 の register の組で instance の bit（VCS2 は M1TCR の bit 2）、domain は Gen12 と同じ、OA 無し。
- reset.c: HEVC の SFC の WA（Wa_14010733141）は版 12 だけ。gt-power.c: VDN の HCP/MFX の power gate は版 12 だけ（Linux `gen11_rc6_enable`）。verify-workarounds.c: 版 12 未満は Gen8 の multicast の範囲。device.c: PXP は版 12 未満で作らない。
- workarounds.c と intel/workarounds.h・intel/mocs-table-gen11.inc: ICL の GT・render engine・render context・whitelist（render・video の HuC 3 つ・他の engine の timestamp）の rule、engine ごとの MOCS（ICL の表、uc 0・unused 1、VCS2 は 0x10000）、PAT（`GEN10_PAT_INDEX` に 1 つずつ）、GTT の WA（L3 LRA と GTT cache）。`drv_i915_wa_build_engine` は版の代わりに GT の情報を受ける（p002）。

## 確かめた事（2026-10-26）

- `sh plan/ws252/tests/gen12-golden/run.sh`: Gen12 の 543 行が 684a5243f と一致、`icl smoke: 292 register writes, 30 steps rc=0, 0 steps failed`、`linux check: PASS` — ICL の GT の list 9 件・rcs0 の engine 11 件・context 8 件・whitelist（rcs0 4・bcs0 1・vcs0 4・vcs2 4・vecs0 1）が Linux v6.8.12 の `icl_gt_workarounds_init`・`general_render_compute_wa_init`・`rcs_engine_wa_init`（版 11 と版 9 以上の block）・`icl_ctx_workarounds_init`・`icl_whitelist_build` から機械で計算した register・clr・set と一致（`check-linux.py` の `check_icl`）。
- build（既定・execution・vke2）warning 0、`run-xd-host-tests.sh` all passed。
- 5330（素の起動、kernel e68361995、2026-10-26）: PASS（survey lit、i21 の基準と steady の差 0、書き込みの digest 同じ）。証拠 `plan/ws245/tests/evidence/p4-ws252-p004-e68361995.md`。

## 制限・未確認

- ICL の実機は無い: forcewake の ack、fuse の値、MCR の steering の先、WA の read-back、RC6、reset、CSB の実際の event、GT の初期化の全部が未確認（p007）。
- MOCS の ICL の表の値は Linux の表と目で照合した（機械の照合は無い）。
