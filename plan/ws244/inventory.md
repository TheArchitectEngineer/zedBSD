# WS244 棚卸し（2026-10-18 P1 が最新の tree で更新、基点 main afa9dc1e2）

旧版（2026-10-15 P7、5a352c47c）の file の群・関数ごとの C/S/O の表は、WS245 で display 12・13 を xd-*.c に書き直し、旧 display（ddi.c・dp.c・pipe.c・vbt.c など）を外した後は古いので置き換えた（旧版は git の履歴）。
数え方: 先頭の 40 行と本文の `Linux|Mesa|brw_|derived|port of|excerpt|Copyright|Permission is hereby` の行から、`Value(s) referenced:`・`Procedure referenced:` の block と zedBSD の表示を除いた行（「Type-C port of」などの英語の誤検出は手で外した）。生成の補助は `python3 -I plan/ws244/tests/inventory.py src/drivers/gpu/i915`。
分類: **D** = データ（値・表・bit。block ごとの引用で残せる）、**C** = 写した code・comment・構造（書き直す）、**N** = 名前だけ Linux・Mesa の symbol（改名で済む）、**O** = 自前（言及・管理情報の掃除だけ）。

## 1. 済み（この WS の対象外か、対応済み）

| 範囲 | 状態 |
| --- | --- |
| display 12・13（display/xd-*.c・*.h、intel/display-regs-xd*.h・display-tables-xd.h・display-dpcd-xd.h・xd-vbt-layout.h・xd-opregion-layout.h） | WS245 で自前に書き直し済み。名前は `I915_XD_*`、値は block ごとの `Values referenced:`。先頭に出典なし。**手本**。残りは xd-dp.h の「Linux errno」「as Linux counts them」など 8 行の言及（O、p007 で掃除） |
| Gen9 の display（display/gen9-*.c） | WS258（e91b944a4）で先頭の宣言を外し、block ごとの referenced に直し済み。gen9-mode.h・gen9-outputs.h に言及が 1〜2 行（O、p007） |
| render/ の大半、display の自前の file（present・scanout・bootfb・capture・head・output・resident など） | 出典の言及なし（O） |

## 2. intel/ の上流の抜粋（23 file）— D + N（+ 先頭の MIT 表示と SHA-256 の一覧）

先頭に Intel・Tungsten Graphics などの Copyright と MIT の許諾、「Modified for zedBSD: excerpted, mechanically rewritten … Values are unchanged. Sources (SHA-256 …)」。中身は `#define` と表で、**値はデータ（D）**、**macro の名前は Linux・Mesa のまま（N）**。生成の script は退役済み（plan/ws031/handover/tools/retired/gen-inc.py）なので、手で直す。

| file | 行 | macro | 使う file 数 | 出典 | 分類 |
| --- | --- | --- | --- | --- | --- |
| gt-regs.h | 719 | 249 | 70 | Linux v6.19 gt/intel_gt_regs.h・intel_engine_regs.h・i915_reg.h・intel_lrc_reg.h・intel_pci_config.h・i915_drm.h | D+N |
| commands.h | 264 | 77 | 34 | Linux gt/intel_gpu_commands.h | D+N |
| gt-power.h | 130 | 47 | 8 | Linux gt/intel_gt_regs.h（RC6・RPS）・intel_pcode | D+N |
| lrc-offsets.h | 357 | 7 | 2 | Linux gt/intel_lrc.c の reg offset 表 | D（表）+N |
| mocs.h・mocs-table.inc・mocs-table-gen9.inc | 148+108+51 | 49 | 12 | Linux gt/intel_mocs.c | D+N |
| workarounds.h | 184 | 108 | 12 | Linux gt/intel_gt_regs.h の WA の bit | D+N |
| mcr-ranges.inc | 48 | 表 | 1 | Linux gt/intel_workarounds.c mcr_ranges_gen12 | D |
| engine-table.inc・engine-table-gen9.inc | 59+58 | 表 | 1 | Linux gt/intel_engine_cs.c intel_engines[] | D（表の行の形は struct の写しの疑い、要確認） |
| forcewake-ranges.inc・forcewake-ranges-gen9.inc | 82+78 | 表 | 1 | Linux intel_uncore.c | D |
| pci-ids.h | 213 | 12 | 8 | Linux include/drm/intel/pciids.h（macro 列の形） | D+N |
| display-regs-gen9.h・display-regs-gen9-modeset.h | 233+366 | 107+218 | 24+22 | Linux i915_reg.h・display/*_regs.h | D+N |
| genxml.h・genxml-gen9.h・genxml-video.h | 883+200+309 | 358+42+113 | 18+14+2 | Mesa 25.0.7 src/intel/genxml（Intel の公開の command の名前） | D+N（名前の扱いは判断 1） |
| eu-encoding-gen12.h・eu-encoding-gen9.h | 428+249 | 191+129 | 8+4 | Mesa 25.0.7 brw_eu_defines.h・brw_inst.h | D+N（判断 1） |
| null-state-gen9.inc | 1005 | 表 | 1 | Mesa の gen9 null state の dword | D |
| bits.h | 29 | 6 | 10 | Linux の REG_BIT・REG_GENMASK の補助 | N（自前の helper にする） |

## 3. GT の core — C（流れ・comment・構造の写し）

| file | 行 | 言及（主な物） | 分類と方針 |
| --- | --- | --- | --- |
| power.c・power.h | 425+38 | 「a direct port of Linux's intel_pcode.c」 | C。PCODE の mailbox の手順（PRM の GT の mailbox）から書き直す |
| reset.c・reset.h | 919+49 | 「The control flow is Linux's」「follows __intel_gt_reset()」 | C。GDRST・RING_RESET_CTL の手順を PRM から |
| tlb.c・tlb.h | 369+62 | 「This is Linux intel_gt_invalidate_tlb_full()」 | C（流れ）+D（register）。書き直し |
| context.c・context.h | 1095+135 | 「stands for __lrc_alloc_state()」、lrc_* の名前の対応、CTX_* の index | C+N。LRC の image の配置（D）は残し、組み立ては自前に |
| request.c・request.h・submit.h・engine.h | 851+102+115+169 | 「re-derived from gen8_engine_cs.c」「follows intel_execlists_submission.c」 | C の疑い。手で確かめ、命令列は D（Procedure referenced）、関数の分け方は自前に |
| device-info.c・device-info.h | 944+216 | 「This is Linux's intel_gt_init_mmio()」、enum intel_engine_id の値・struct sseu_dev_info の subset | C+N |
| gt-power.c・gt-power.h | 1416+234 | 「Follows Linux intel_rc6_init() …」21 関数 | C。RC6・RPS の手順を PRM から、値は D |
| workarounds.c・workarounds.h・verify-workarounds.c・.h | 1528+228+797+131 | 「Linux wa_write_or()」など 30 関数、struct i915_wa | C。WA の register・bit・番号（Wa_*）は D、list の組み立て・検証は自前に |
| ggtt.c・ppgtt.c・ppgtt.h・memory.c・memory.h・irq.c・irq.h・dma.h・defaults.h・migrate.h・park.h・pxp.h・runtime-pm.*・sync.h・workqueue.h・worker.c | 小 | 1〜3 行の「as Linux …」「Linux's I915_CACHE_NONE」など | 大半 O（言及の掃除）、ggtt.c・ppgtt.c の PTE の bit は D。手で読んで C が出たら書き直し |

## 4. compiler・render — Mesa の手順の言及

| file | 行 | 非 referenced の言及 | 分類 |
| --- | --- | --- | --- |
| compiler/compile.c | 5477 | 58 | 大半「Mesa does X (brw_*.cpp)」の手順の言及。Procedure referenced: への書き換え + brw_ の symbol 名の除去。写した構造が無いか関数ごとに確認 |
| compiler/eu.c | 2750 | 45 | 同上（encode の field は eu-encoding-*.h の D） |
| compiler/compile-geometry.inc・compile-compute.inc | 882+786 | 20+12 | 同上 |
| render/state.c・heap.h・transport.*・memory.c・video-mfx.* | | 2〜7 | Mesa 25.0.7 の値の言及 → Value(s) referenced: に。Venus の protocol の名前（vk*MESA）は protocol の名前なので残す |

## 5. display の残り — O（古い comment・管理情報）

display/internal.h（14 行。「Structure-faithful port of intel_bios_init()」など、もう無い vbt.c を指す古い comment。使っているのは gen9-flip・scanout・tc-port・head・clock・backlight・vbt-shell・present・dp-ext-kern）、dp-ext-kern.c・.h、output.c、resident.c、vbt-shell.c、xd-dp.h、xd-pipe.c、xd-picture.c、xd-screen.c、xd-power.c、xd-diag.c、xd.h、xd-platform.h、gen9-mode.h、gen9-outputs.h。

## 6. license の package

userland/base/licenses/i915-driver/LICENSE は「code derived from the Linux kernel v6.8.12 … carry the MIT notice below」と 30 余りの Copyright を並べる。全部の群が済めば派生の code が無くなるので、package を外す（Makefile:372 の `i915-driver-license`）か、データの引用の説明だけにする（判断 2）。

## 判断が要る点（Q1 経由でユーザーへ）

1. **Intel の公開の名前**: genxml の command・field の名前（3DSTATE_VS、MI_BATCH_BUFFER_START など）と EU の opcode の名前は Intel の PRM に載る hardware の名前。WS039 の「AMD の公開の ISA の mnemonic は使ってよい」と同じに、PRM に載る名前は使ってよいか。使えないなら genxml・eu-encoding の 900 余りの macro と使う 40 file 余りを改名する（量が倍）。既定の案: PRM に載る command・opcode の名前は使ってよい、Linux・Mesa だけの名前（GEN6_GDRST・RING_RESET_CTL など）は `I915_*` の zedBSD の名前にする。
2. **license の package**: 書き直しの後に外すか、引用の説明だけ残すか。既定の案: 外す。
