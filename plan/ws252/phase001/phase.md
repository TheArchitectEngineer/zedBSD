<!-- awesome-plan project=zedbsd record=ws252-p001 -->

# ws252-p001: 調べと設計

- Parent: [WS252](../ws.md)
- Status: in-progress（2026-10-19、P1、q1263）。設計の版 2 を書いた（レビュー 1 の全部の指摘に対応）。再レビュー待ち。
- Disposition: normal
- Queue: q1262（P1、版 1）、q1263 / q1263-i01（P1、版 2）

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

- design-reviewer は Q1 が流した（2026-10-19、[design-review-1.md](../design-review-1.md): Blocker 1・Major 6・Minor 8）。q1263 で版 2 を書いた（対応は design.md 末尾の表）。再レビューを通るまで cleared にしない。
- 版 2 で未確認の事: Linux の位置のうち M4 の一部（design.md §6.2 で `*` の無い物）、v6.19 と 6.8.12 の PCI ID の差、WS229 の実績の LW、hardware が MOCS の index 0・1・63 を使うか、GT の失敗の時の画面、RKL の fuse の vcs2。
- `build/tmp/ws252-linux/` は q1263 の時点で既に無い。レビューが展開した scratchpad の `lx/` の削除は Q1 に依頼。
