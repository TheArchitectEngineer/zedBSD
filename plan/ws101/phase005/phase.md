<!-- awesome-plan project=zedbsd record=ws101p005 -->

# ws101-p005: 実機の bring-up と kernel の試験の場面 `vkcs`

Phase ID: `ws101-p005`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。5330 の passthrough で vkcs 9/9 PASS、実行器の回帰 vkx・vke1・vke2・vkc PASS）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲（[design.md](../design.md) §2.6・§5.1、§6 の p005）

p004 の dispatch の batch を実機で動かす。kernel の試験の build の場面 `vkcs` を作り、eu-test と同じ 1 group・1 thread・1 channel から
段階的に広げる（ONE → ADD → ID → ODD → PUSH → ATOMIC-SSBO → MIXED → MANYOPS → SPILL）。main の指示（2026-09-30）で、最初に p004 の
draw の側の変更（storage を持つ draw の `transfer_pending`、range の word）が graphics を壊していないことを実行器の回帰で確かめる。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/tests/render/compute.c`（新） | 場面 `vkcs`。thread が node を待ち、自分の session を開き、buffer（1 MiB の storage の 64 KiB の region）・shader module・layout・set・target・pass（clear と load）・framebuffer・MIXED の graphics pipeline を直接作って公開する。compute pipeline は wire の vkCreateComputePipelines（66）で作る。set は wire の vkUpdateDescriptorSets（storage、dynamic storage、uniform）、記録は wire（BindPipeline・BindDescriptorSets の compute の bind point と dynamic offset・PushConstants・Dispatch 110・CopyImageToBuffer・Draw・render pass）、submit。期待値は整数で C が計算する。state は heap（試験の kernel の大きさの上限のため static にしない） |
| `src/drivers/gpu/i915/tests/render/compute-shaders/`（新） | GLSL（one・add・ids・odd・push・atomic・mixed・mixed.vert・mixed.frag・inc、regenerate.py が作る spill.comp）と SPIR-V、`regenerate.py`（glslc → spirv-val → `tests/fixtures/compute-shaders-gen.inc`。spill だけ `-O`: -O0 の 29 KiB を 12 KiB にする。それでも 2 KiB を spill する） |
| `src/drivers/gpu/i915/tests/fixtures/compute-shaders-gen.inc`（新、生成） | SPIR-V の配列と spill の定数 |
| `src/drivers/gpu/i915/tests/execution/runner.c`・`tests/render/scenarios.h` | 場面の表に `vkcs`（追加だけ） |
| `src/drivers/gpu/i915/tests/render/README.md` | suite の表と生成の手順に vkcs |
| `platform/amd64/vmunix.mk` | 試験の source の一覧に `tests/render/compute.c` |
| `src/drivers/gpu/i915/render/draw.c` | scratch の確保の log に compute の部分（大きさ・id 数・offset） |
| `plan/ws101/tests/hw/run-hw.sh`（新） | 場面の image を lock の外で build し、`test-hw.sh` で順に走らせる。この Phase で走らせた `build/ws101-p005/run-hw.sh` を一般化したもの（この形では未実行） |

## 確認（実機 = 5330 の QEMU passthrough。`plan/ws075/tests/test-hw.sh`、`flock /tmp/i915-hw.lock` の下、image は lock の外で先に build）

| 確認 | 結果 |
| --- | --- |
| 実行器の回帰（p004 の変更を含む tree）: `test-hw.sh vkx`・`vke2`・`vkc` | vkx PASS（9/9）、vke2 PASS（17/17）、vkc PASS（9/9）。image は p004 の main の取り込み後の tree（vkcs の登録の前）。`build/ws101-p005/hw-vkx`・`hw-vke2`・`hw-vkc` |
| `test-hw.sh vke1` | 1 回目は BUILD FAILED（回帰の実行中に runner.c への登録を書いたため、lock の中の make が未完成の tree を拾った。GPU は使っていない）。p005 の tree で再実行して PASS（6/6）。`hw-vke1-r1` |
| `test-hw.sh vkcs`（r1） | verdict PASS（9 of 9）。ONE・ADD・ID・ODD・PUSH・ATOMIC-SSBO・MIXED・MANYOPS・SPILL。`hw-vkcs-r1` |
| `test-hw.sh vkcs`（r2、最終の tree。draw.c の log の変更を含む） | verdict PASS（9 of 9）。scratch の log: `compute 2048 for 768 ids at +0x1000`（1576960 bytes = 0x1000 + 2048 × 768）。`hw-vkcs-r2` |
| compute pipeline の出来（r1 の log） | ONE 384 bytes 1 thread right mask 0x1、ADD 8 thread、ID 3 thread、ODD 2 thread right mask 0x7f、PUSH cross-thread 6、ATOMIC 2816 bytes、MIXED 3696 bytes、INC 736 bytes、SPILL 36352 bytes r95 scratch 2048 |
| `plan/ws101/tests/host/run.sh` | PASS（p002〜p004 の host の試験。draw.c の log の変更の後） |
| host の compile（`compute-dump` と Mesa の brw_disasm/brw_asm で vkcs の 9 kernel） | 全 kernel が compile、disassembler が全命令を受け、組み直しが同じ bytes。SPILL は scratch 2048（-O）、ほかは 0 |
| vkcs の場面の kernel の flag での compile | warning 0（`-Wall -Wextra -Werror`、`-Wframe-larger-than=8192`） |
| `git diff --check` | OK |

各 step の判定: ONE は word 0 と後ろの 63 word の sentinel。ADD は n = 1000 の和と、n から 1088 までの sentinel。ID・ODD は全 invocation の
13 word の記録（local ID・index・group ID・group の数・global ID）、最後の記録の後の 256 word の sentinel、invocation の数え上げ（288・180。
ODD の 2 thread 目の切った channel が走ると数が合わない）。PUSH は push constants・std140 の uniform block・dynamic offset 256 bytes の
storage を混ぜた式。ATOMIC-SSBO は histogram・和・符号付き/無しの min/max・and/or/xor・exchange（7）・compare-exchange（1）・counter（n）
と、counter の old の値が 0〜n−1 を 1 度ずつ。MIXED は draw → copy → dispatch → draw の 1 つの command buffer で、copy の全画素、
dispatch の全 word（+1）、2 つ目の draw の左半分（dispatch が書いた頂点と色）と右半分。MANYOPS は 200 の dispatch（push constants を
毎回変える）の漸化式の 256 word と後ろの sentinel（slot の 128 を越える）。SPILL は 256 group × 64 の全 word（2048 thread、GPU が
同時に走らせる数より多い）と、kernel が spill すること。

## 決めたこと・分かったこと

- A64 の atomic は uncached の MOCS のままで正しく動いた（design §2.3 の懸念。write-back への切り替えは要らない）。
- 試験の kernel は `AMD64_KERNEL_MAX_BYTES`（16 MiB、text + data + bss）の近く: vkcs を足した後で 16,770,544 bytes、残り 6,672 bytes。
  vkcs の state（wire の 32 KiB を含む）は heap に置き、spill の SPIR-V は `-O` にして収めた。**p006 の step（SHARED・REDUCE など）を
  足す余地はほぼ無い**。大きな bss は `kernel_heap_storage`（4 MiB）・`overlay_inodes`（3.1 MiB）・`states`（2 MiB）など kernel 全体の
  もので、WS101 の範囲の外。p006 の前に main の判断が要る（試験の build の分割、別の bss の縮小、上限の変更など）。
- 実機の run の image は lock の外で先に build する（`VKLOOP_BUILD_ONLY=1`）。lock の中の make は source の変更を拾うので、run の間は
  tree を触らない（1 回目の vke1 の失敗の原因）。

## 未実施・残り

- p006（SLM・barrier・fence・SLM の atomic、IDD の SLM・barrier の field）、p007（indirect・0 の group・LENGTH・MANY）。
- 変異の試験（わざと誤った kernel・batch で vkcs が FAIL すること）は実機では行っていない（1 回の run に 5 分ほどかかり、lock は共有）。
  判定は全 word の完全一致なので、host の試験（p004 の genxml の変異の試験）と合わせて検出力はあると判断した。
- boot test（`plan/tools/boot-test.sh`）は未実施（試験の build の image で実機の回帰を通した）。
- QEMU（Venus）での確認は範囲外。
