<!-- awesome-plan project=zedbsd record=ws252-p001 -->

# ws252-p001: 調べと設計

- Parent: [WS252](../ws.md)
- Status: in-progress（2026-10-19 02:58、P1、q1262）。設計の版 1 は書いた。design-reviewer は未通過。
- Disposition: normal
- Queue: q1262（P1、2026-10-19、Q1 の指示）

## 範囲と受け入れ条件

今の Gen12 の GT（TGL・ADL-P）の code との差、engine・firmware・WA、attach の判断の材料、実機無しの試験の道、Phase の分割と LW、ユーザーの判断の点を [design.md](../design.md) に。design-reviewer を通す。code は書かない（2026-10-19 ユーザー「設計まで」）。

## 実施

- 読んだ物: `src/drivers/gpu/i915/` の `device.c`・`device-info.c/.h`・`submit.c`・`firmware.c`・`workarounds.c`・`compiler/eu.c`・`render/batch.c`・`render/blit.c`・`intel/engine-table.inc`・`intel/mocs-table.inc`、WS245 design.md の §7 #18・#20・#21・#30。
- Linux 6.8.12 の `i915_pci.c`・`gt/intel_workarounds.c`・`gt/intel_mocs.c`・`gt/uc/intel_uc.c`・`gt/uc/intel_uc_fw.c`・`gt/intel_engine_cs.c` を `build/tmp/ws252-linux/` に展開して読んだ（design.md に code は写していない）。

## 結果（要点）

- RKL・ADL-S/RPL-S は Gen12 の今の道に表の行（engine の mask、MOCS の選び、Wa_1607297627、DMA の幅、ID）を足すだけ。GuC/HuC は不要（execlists）。
- ICL は Gen11 の compiler・genxml・MOCS・WA の第 3 の版で大きい（約 23 LW）。
- 実機は無い（13400F は iGPU 無し）。試験は host の表の比較と TGL・ADL-P の列の不変。
- 見つけた事: Linux は TGL・RKL に `tgl_mocs_table` を使うが zedBSD の TGL は `gen12_mocs_table` の値（design.md §2.1、Q1 へ）。
- ユーザーの判断: D1〜D5（design.md §8）。

## 未実施・残り

- design-reviewer は Q1 が流した（2026-10-19、[design-review-1.md](../design-review-1.md): Blocker 1・Major 6・Minor 8）。P1 が q1263 で改訂 2 を書く。通るまで cleared にしない。
- `build/tmp/ws252-linux/` の展開物の削除は Q1 に依頼。
