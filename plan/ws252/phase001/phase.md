<!-- awesome-plan project=zedbsd record=ws252-p001 -->

# ws252-p001: 調べと設計

- Parent: [WS252](../ws.md)
- Status: cleared（2026-10-19、P1、q1265）。設計の版 3.1（短い確認 [design-review-3.md](../design-review-3.md) の Minor 1〜9 に対応、Blocker・Major 無し）。
- Disposition: normal
- Queue: q1262（P1、版 1）、q1263 / q1263-i01（P1、版 2）、q1264 / q1264-i01（P1、版 3）、q1265 / q1265-i01（P1、版 3.1、cleared）

## 範囲と受け入れ条件

今の Gen12 の GT（TGL・ADL-P）の code との差、engine・firmware・WA、attach の判断の材料、実機無しの試験の道、Phase の分割と LW、ユーザーの判断の点を [design.md](../design.md) に。design-reviewer を通す。code は書かない（2026-10-19 ユーザー「設計まで」）。

## 実施

- 読んだ物（版 1〜3.1）: `src/drivers/gpu/i915/` の `i915.c`（`identifiers[]` 80-89、`i915_attach` 258・270-284、`i915_suspend` 387-）・`device.c`・`device-info.c/.h`・`submit.c`・`firmware.c`・`workarounds.c`・`ggtt.c`・`tlb.c`・`engine.c`・`defaults.c`・`irq.c`・`compiler/eu.c`・`compiler/compile.c`・`render/`（batch・blit・state・draw・compute）・`intel/engine-table.inc`・`intel/mocs-table.inc`・`intel/display-tables-xd.h`（1035-1085）、`tests/` の eu-test・draw-fixture・draw-test、`config/kernel-options.list`・`Makefile`（239・628）・`src/drivers/gpu/amdrdna/control.c`、`userland/firmware/i915/Makefile`、WS245 の design.md §7 #18・#20・#21・#30 と ws.md（p014・p045）、WS229 の `phase001/phase.md`（42・228-234）と `tests/gen12-golden/run.sh`、`plan/known-bugs.md`（BUG-335）、`plan/master.md` の Tools（1176・1177）。
- Linux 6.8.12（版 3.1 はレビュー 3 が scratchpad に展開した tree で `intel_ggtt.c`・`intel_workarounds.c`・`intel_gt.c`・`intel_engine_cs.c`・`intel_reset.c`・`intel_renderstate.c`・`intel_gtt.c` の該当行）の `i915_pci.c`・`gt/intel_workarounds.c`・`gt/intel_mocs.c`・`gt/uc/intel_uc.c`・`gt/uc/intel_uc_fw.c`・`gt/intel_engine_cs.c` を `build/tmp/ws252-linux/` に展開して読んだ（design.md に code は写していない）。

## 結果（要点、版 3.1）

- RKL・ADL-S/RPL-S は Gen12 の今の道に表の行（engine の mask、Wa_1607297627、ID は `identifiers[]` と `device.c` の 2 つ。xd の表は既に持つ）を足すだけ。xd の表との統合は WS245 と合わせる（Q1）。DMA の幅は差が無い。GuC/HuC は不要（execlists）。
- ICL は Gen11 の第 3 の版。版を見る所を grep で全部分けた（design.md §6.2）。見積もりは WS229 の GT 系 8 LW と比べて出し直し、RKL・ADL-S だけで約 12 LW、ICL の分は約 15〜17 LW（p004〜p006 の 14 ＋ p007 の ICL 分 1〜3）、全部で約 27〜29 LW。
- 実機は無い（13400F は iGPU 無し）。試験は既存の `plan/ws229/tests/gen12-golden/` を直して使い、新しい platform の期待は Linux から抜き出す小さな script（p002・p004）。ICL の compiler・state は Master の Tools の `eu-gen9-host.sh`・`ws229/tests/host/run.sh` を `--gen=icl`・gen110.xml に広げる。どれも今の tree で動くかは未確認。
- D1=A は「既定の image では付けない、試験の config.mk の build option で ID を足す」（前例 `CONFIG_AMDRDNA_CONTROL`、共有の Makefile の調整は Q1）。p007 の依存は群ごとの D1 の表（design.md §5）。
- 見つけた事: Linux の Gen12 の MOCS との差（TGL・RKL の表、entry 63）は WS252 の外、[BUG-335](../../bugs/BUG-335.md)。
- ユーザーの判断: D2 → D1-R →（D2=全部の時だけ）D1-I → D3 → D5 の順（design.md §8）。D6 は決定済み、D4 は WS の外へ。

## 未実施・残り

- design-reviewer は Q1 が流した（2026-10-19、[design-review-1.md](../design-review-1.md): Blocker 1・Major 6・Minor 8）。q1263 で版 2 を書いた（対応は design.md 末尾の表）。再レビューを通るまで cleared にしない。
- 版 2 で未確認の事: Linux の位置のうち M4 の一部（design.md §6.2 で `*` の無い物）、v6.19 と 6.8.12 の PCI ID の差、WS229 の実績の LW、hardware が MOCS の index 0・1・63 を使うか、GT の失敗の時の画面、RKL の fuse の vcs2。
- 再レビュー（[design-review-2.md](../design-review-2.md): Major 4・Minor 7）に q1264 で版 3 を書いた。短い確認（[design-review-3.md](../design-review-3.md)、Blocker・Major 無し、Minor 9）に q1265 で版 3.1 を書き、cleared。版 3.1 の未確認は design.md §9（ICL の割込みの系統、v6.19 の ID の差、gen12-golden・Mesa の道具の build・PASS、WS229 の実績の LW、実機の振る舞い、レビュー 3 の値のまま開いていない Linux の数行）。
- `build/tmp/ws252-linux/` は q1263 の時点で既に無い。レビューが展開した scratchpad の `lx/` の削除は Q1 に依頼。
