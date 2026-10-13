<!-- awesome-plan project=zedbsd record=ws229-p002 -->
# ws229-p002: i915 の GT の起動を Gen9・Gen9.5 に

Status: in-progress（2026-10-14 P2: 実装と host の確かめは済み。Gen12 の回帰（5330、T1）と Gen9 の実機（p005）は未実施）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q977（P2、branch agent/p2-ws215、worktree /home/awe/zedBSD-worktrees/p2）
依存: [p001](../phase001/phase.md)（設計、review-1 と Q1 の判断、ユーザーの決定 D1〜D3・U1〜U3）

## 範囲

p001 §5 の p002 の行と review-1 の p002 の項目（F1、M1、M3、M4、M7（U2）、M8（U1）、M10、中 1・2・3・6）。所有は §5 の p002 の列に、Q1 の追記（request.c・verify-workarounds.c・defaults.c・display/display.c の Gen9 の分岐・include/kern/boot.h と src/kern/boot.c の key・worker.c）を足した物。Gen12 の振る舞いを変えないことが最優先（U3）。

## 実装（commit、base d1e743394）

| commit | 内容 |
| --- | --- |
| 4bc6381c0 | 版の選択の口: `struct i915_gt` の `graphics_ver`、device.c の定数 12 の置き換え |
| fddc62621 | Gen9 の PCI ID 86（SKL 25・KBL 22・CFL 22・CML 17、`intel/pci-ids.h`）、platform と revision、Gen9 の forcewake の表（`intel/forcewake-ranges-gen9.inc`、MEDIA_GEN9 domain）、SSEU（FUSE2・EU_DISABLE(s)）、clock（CTC の shift）、engine の表（`intel/engine-table-gen9.inc`: RCS0・BCS0・VCS0 0x12000・VECS0 0x1a000、VCS1 は使わない）、Gen6 の reset domain、Gen8 の fault register、N0-lite（display/takeover.c）と display absent、PXP を Gen9 で作らない |
| 37fb3cd20 | reset: Gen9 は SFC lock 無し。contracts の mmio（Gen9 の domain の表）と reset（Gen9 の VCS0）の検査 |
| dd901c95f | 割込み: GEN8_MASTER_IRQ と GT の 4 bank（irq.c）、Gen9 の display の割込みを 0x44200 に書かずに mask（device.c の interim） |
| 4ef67c728 | context: gen9 の offsets の表（`intel/lrc-offsets.h`）、slot（MI_MODE 0x54・BB_OFFSET 0x64・INDIRECT 0x1a/0x1c）、CS_MMIO と BB_END bit 0 を出さない、SAVE_INHIBIT・RS_CTX の masked disable、Gen8 の RPCS（S_CNT 17:15、EU min/max）、render の gen9 indirect ctx の wa batch（coherent L3 flush、SLM clear、chicken 3 個）。engine info に `graphics_ver` |
| ad543ffe9 | submit: ELSP 4 dword（上位から、port 1 から）、CSB 6・write index 0x1f、gen8_csb_parse、GFX_RUN_LIST_ENABLE、RING_IMR。engine の Gen11+ の stop の WA を外す |
| 0d7d54468 | workarounds: Gen9 の GT・engine・ctx・whitelist（SKL/KBL/CFL/CML、stepping は revision）、engine 毎の MOCS（skl の表、`intel/mocs-table-gen9.inc`、resume で record_defaults の前）、PAT 0x40e0/0x40e4、gtt の WA（LRA・64K IPS・GTT cache）、RC6・PG は sanitize の 0 のまま有効にしない、RPS は RPe の固定要求（RP1 は RP_STATE_CAP 15:8） |
| f2cc7466f | request.c: gen8 の flush（rcs: VF invalidate の前の null PC、xcs: MI_FLUSH_DW）と breadcrumb（4 dword の MI_SEMAPHORE_WAIT）、AUX invalidate と preparser を出さない。verify-workarounds: Gen8 の MCR range |
| 937f8c714 | TLB: GEN8 の RTCR・M1TCR・VTCR・BTCR と 3 domain。GGTT の更新ごとに GFX_FLSH_CNTL_GEN6 |
| f0ee3f3b5 | product → {graphics_ver, display_ver, platform} の明示の表（表に無い product は起動を拒む）、PCI match に Gen9、attach は `i915.gen9=on` の時だけ Gen9 を bind（既定 off、`I915_GEN9_DEFAULT_ON` の 1 行で切替）、Gen9 は suspend を EBUSY で拒む（U2）、N0-lite に eDP transcoder（0x7f008）と VT-d（translation・PMR・読めない unit で GT を起こさない、EIO）、PSR・SDE の割込みの silence、worker は Gen9 で video engine を選ばない |
| ac646d47d | boot の key `i915.gen9=on\|off`（include/kern/boot.h・src/kern/boot.c）。attach の時点では未 parse なので i915 は loader の line を直に読む |
| f806ca238 | defaults.c: Gen9 の render の null render state（`intel/null-state-gen9.inc`、gen9_renderstate.c の 960 dword、reloc 4）を record の request で secure に流す |
| 910d6d81e・eb7057703 | `plan/ws229/tests/gen12-golden/`（下記）、contracts の pte に mmio.c を link（GGTT flush の参照） |

出典は全部 Linux 6.8.12 の MIT の file（値だけを転記、各 header に file と sha256）。

## 確かめ（2026-10-14 P2、host だけ）

| コマンド | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws229-p002 build/ws229-p002/vmunix` | rc 0、warning 0、amd64 vmunix check PASS |
| `make … BUILD=build/ws229-p002-ktest I915_TESTS=y I915_TEST_SET=execution build/ws229-p002-ktest/vmunix` | rc 0、warning 0（ktest の source が通る） |
| `TMPDIR=build/tmp sh src/drivers/gpu/i915/tests/contracts/run.sh` | PASS（mmio dma pci rpm pte sync rps memory forget reset、ordinary + ASan/UBSan） |
| `sh plan/ws229/tests/gen12-golden/run.sh` | **gen12 golden PASS: 543 行が d1e743394 と同一**（Gen12 の gt_init_mmio・表・init_hw・engine の resume・RC6/RPS・irq の reset と postinstall の全 register 書き込みの順、WA/ctx/whitelist の表、5 engine の context の image と wa page、request の ring、ELSQ の書き込み）。同じ program の Gen9 版（SKL GT2）: 202 書き込み、全 step rc 0。出力を読んで LRI の header（CS_MMIO 無し）、BB_END、INDIRECT_CTX と offset 0x26、RPCS 0x80000088、wa batch、ELSP の順、RING_IMR、flush と breadcrumb の dword が設計どおりであることを確かめた |
| `sh plan/ws229/tests/run-host-boot-gen9.sh` | 10 checks PASS（on・off、unknown にならない、他の語を拒む） |
| `sh plan/ws083/tests/run-host-boot-video.sh` | 10 checks PASS（既存、boot.c の変更の回帰） |

## 未実施

- Gen12 の実機の回帰（5330、T1）: U3 で必須。依頼は Q1 経由。
- Gen9 の実機（p005）: 6・7・8 世代の laptop で `i915.gen9=on`。
- ktest-gt の Gen9 の register model: ktest-gt は device の起動の後に kernel の中で走るので host では走らない（review M6）。代わりに host の golden の Gen9 版で同じ点（ELSP、CSB、image の slot、MOCS、forcewake）を見た。

## 残課題と制限

- RC6・RPS: Gen9 は RC6・PG off、RPS は RPe 固定（package の C-state・S0ix に入れない）。Linux の Gen9 の RC6（context 破壊の WA）は p005 の判断。
- WaForGAMHang（KBL < C0 の DC flush の組）と skl_tune_iz_hashing（7 EU の subslice が 1 つの part）は入れていない。VCS1（GT3/GT4 の 2 本目の video engine）は使わない。
- Gen9 の display は absent（GOP の picture のまま）。suspend は拒む。
- `I915_GEN9_DEFAULT_ON`（i915.c）は p005 の実機 PASS の後に 1 にする（U1）。
- render の state・compute・EU の符号化は p003・p004。
