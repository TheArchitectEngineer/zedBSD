<!-- awesome-plan project=zedbsd record=ws101-p013 -->

# ws101-p013: 規約の全文との照合と回帰（WS101 の全ての変更）

Phase ID: `ws101-p013`
Parent: [WS101](../ws.md)
Status: in-progress（2026-10-05 P1 generation18、q750。Q1 の承認「Do WS101 p013 under q750」。p012・p018 はユーザーの判断まで保留のまま（Q1 が質問を記録）。p017 の 5330 の値は実機の UAT の項目）
Phase disposition: normal
Queue: q750（P1、2026-10-05）

## 範囲（ws.md の表から）

規約の全文（[plan/coding-style.md](../../coding-style.md)）との照合（WS101 の全ての変更）と回帰（host、vkx・vke1・vke2・vkc・vkcs・GLES、boot test）。
code を作る WS の必須の Phase（AGENTS.md「コード」、Awesome Plan §6）。source の意味を変えない直しだけを行う（違反の修正）。
toolchain（Noct の patch・`userland/base/noct/Makefile`・`version.mk`・`zedbsd.cmake`）は照合だけで、直しが要れば main に依頼する。

## 依存

p002〜p017 の code（p017 の扱いが決まった後の最終の source）。新規実装の期間（〜2026-10-10）の後半に置く。p012 と並行してよい（触る file が別）。

## 手順（2026-10-01 追記）

1. 対象の file の一覧（2026-10-01 に `grep -rl "ws101\|WS101" src include userland tools config` で拾った物。Noct の展開した tree は除く）:
   ```
   mkdir -p build/ws101-p013
   grep -rl "ws101\|WS101" src include userland tools config | grep -v '^userland/base/noct/noct/' | sort > build/ws101-p013/files.txt
   ```
   主な物: `src/drivers/gpu/i915/compiler/`（compile.c・compile-compute.inc・compiler.h・eu.c・ir.h・spirv.c・spirv-compute.inc）、
   `src/drivers/gpu/i915/render/`（batch.c・command.c・compute.c・compute.h・draw.c・gfx.h・heap.h・pipeline*.c・state.c・state.h）、
   `src/drivers/gpu/i915/intel/`（eu-encoding-gen12.h・genxml.h）、`src/drivers/gpu/i915/tests/`（execution/runner.c・render/compute.c・fixtures・compute-shaders）、
   `userland/desktop/libglesv2/`（buffer.c・compute.c・draw.c・es31.c・feedback.c・gles.c・gles.h・pixels.c・program.c・spirv.c・glsl/*）、
   `userland/desktop/libegl/egl.c`、`userland/desktop/glescompute/`、`userland/desktop/gpudemo/`、`userland/desktop/egltest/feedback.c`。
2. 機械の検査（C と header）:
   ```
   grep -E '\.(c|h|inc)$' build/ws101-p013/files.txt | xargs python3 plan/tools/style-check.py --summary > build/ws101-p013/style.txt 2>&1; tail -3 build/ws101-p013/style.txt
   git diff --check
   ```
   違反は WS101 が入れた行だけ直す（他の WS の既存の違反は記録だけ。`git log -L` か `git blame` で判断）。
3. 全文の review: coding-style.md の §14（Review checklist）の各項目を、2 の file の WS101 の部分について読む。機械で見られない規則（§5 の段落、§6 の control flow、§10 の comment、§12 の環境変数 `KEI_GLES_COMPUTE_TRACE`）を表にする。
4. 回帰（順に。image の build は 1 つずつ、実機は lock の script だけ）:
   ```
   sh plan/ws101/tests/host/run.sh
   sh plan/ws101/tests/glsl/run.sh build/ws101-p013/glsl
   sh plan/ws101/tests/gles/run.sh build/ws101-p013/gles-host
   sh plan/ws068/tests/glsl-host/run.sh build/ws101-p013/ws068-glsl-host
   make -j16 ZEDBSD_CONFIG=plan/ws101/tests/hw/g3/config.mk BUILD=build/ws101-p013-lib build/ws101-p013-lib/dynamic/libGLESv2.so build/ws101-p013-lib/dynamic/libEGL.so build/ws101-p013-lib/bin/glescompute > build/ws101-p013/lib.log 2>&1; echo "exit=$?"
   plan/ws101/tests/gles/build-image.sh build/ws101-p013-img > build/ws101-p013/img.log 2>&1; echo "exit=$?"
   IMAGE=build/ws101-p013-img/hdd-image.img SYMBOLS=build/ws101-p013-img/vmunix GUEST_RUNTIME=$PWD/build/ws101-p013-run plan/ws101/tests/gles/venus.sh build/ws101-p013/venus
   OUT=build/ws101-p013-hw plan/ws101/tests/hw/run-hw.sh r1 vkcs vkx vke1 vke2 vkc; echo "exit=$?"
   BUILD=build/ws101-p013-gles plan/ws101/tests/hw/gles-hw.sh build/ws101-p013/gles-hw
   OUTPUT=build/ws101-p013/boot plan/tools/boot-test.sh build/ws101-p013-img/hdd-image.img; echo "exit=$?"
   ```
   G3 の passthrough（`g3-hw.sh`）は [guide.md](../guide.md) 3.2 の提案 B の後に `G3_NOCT=build/demo-lcd9/bin/noct BUILD=build/ws101-p013-g3 plan/ws101/tests/hw/g3-hw.sh build/ws101-p013/g3-hw`。
5. 結果を下の「確認」に、command・結果・未実施を分けて書く。boot test の PNG（`build/ws101-p013/boot/login.png`）をユーザーに見せる。

## 完了の条件

- 1 の全 file について style-check の WS101 の分の違反 0、`git diff --check` 0、§14 の review の表がある（例外は理由と出典付き）。
- host の 4 本が PASS、lib の build が exit 0 で warning 0、venus.sh が `ws101 venus: PASS`。
- 実機（passthrough）: run-hw.sh が exit 0（vkcs 21/21、vkx 9/9、vke1 6/6、vke2 17/17、vkc 9/9）、gles-hw.sh が `gles-hw: PASS`。lock が取れず走らせられなかった物は「未実施」と書き、uncleared にする。
- boot test が `boot-test: PASS`。
- 直した source があれば、その領域の host 試験を直した後に再実行して PASS。

## 確認

### 2026-10-05 q750-i01（P1 generation18）: 途中で区切った（安全な地点）

Q1 の指示（ユーザー「GPU computeは言語側が完成しておらず、進められないんです。i915のSPIR-V lowringだけ進められますか？」の後）で、
compiler の 2 file と共有の file の WS101 の行までで区切り、残りを下に残した。Phase は in-progress のまま。

**直した物**（意味は変えない。§5 段落・§6 条件・§11 return・§8 入れ子の call）:

| file | 直し |
| --- | --- |
| `src/drivers/gpu/i915/compiler/compile-compute.inc` | 1 段落に複数の判断を分けた（operand・predicate・temporaries・fence）、`}` の後の空行と comment、条件の中の call（`i915_compile_group_threads`）を変数に、`i915_compile_storage_block` の最後を成功の return に、`i915_compile_define` の入れ子の call を分けた |
| `src/drivers/gpu/i915/compiler/spirv-compute.inc` | `return i915_spirv_refuse(...)`（25 か所）を `error = ...; return error;` に、関数の最後を成功の return に（execution mode）、guard の列を段落ごとに comment、`i915_spirv_fence` の後の `return parser->error` を失敗と成功に分けた、入れ子の `i915_spirv_integer_constant` を変数に、atomic の comment の古い記述（fence は未対応）を直した |
| `compiler/spirv.c`（lower_storage の compute の load の `continue`）、`compiler/ir.h`（`I915_IR_OP_COUNT` の comment）、`render/pipeline.c`（compute pipeline の失敗の解放）、`render/draw.c`（scratch の作り直し） | WS101 の行の空行と comment |

style-check: `compile-compute.inc`・`spirv-compute.inc` は forward-declaration（`.inc` の関数の宣言は compile.c・spirv.c の先頭にあり、全て在ることを確かめた。道具の限界）以外 0。

**確認**（host、2026-10-05）:

| command | 結果 |
| --- | --- |
| `make BUILD=build/p013-k -j16 vmunix`（既定の config.mk） | exit 0、warning 0 |
| `sh plan/ws101/tests/host/run.sh`（Mesa の道具あり） | PASS |
| `sh plan/ws101/tests/glsl/run.sh build/ws101-glsl-p013` | PASS（10 shader の往復を含む） |
| `sh plan/ws075/tests/guard/run.sh` | PASS |
| `sh plan/ws031/tests/run-vk-host-tests.sh` | PASS（cmd spirv lower res resdispatch sync eu compile pipe cmdbuf） |
| `sh plan/ws068/tests/i915-shader-check/run.sh` | exit 0 |
| `BRW_TOOLS=build/mesa-tools/... sh plan/ws031/tests/run-vk-gentool-test.sh` | PASS |

未実施: QEMU・実機の回帰（vkcs・vkx・vke1・vke2・vkc・GLES・boot test。T1 に依頼する。p013 の残りが済んでからまとめて）。

**残り**（再開の点）:

1. 全文の review と直し: `src/drivers/gpu/i915/render/compute.c`・`compute.h`、`src/drivers/gpu/i915/tests/render/compute.c`（style-check 49 件と段落）、
   `userland/desktop/libglesv2/compute.c`・`es31.c`・WS101 の印の行（buffer.c・gles.c・gles.h・program.c ほか）、`userland/desktop/libglesv2/glsl/` の WS101 の行（WS068 p007 と同時に）、
   `userland/desktop/libegl/egl.c` の WS101 の行、`userland/tests/glescompute/main.c`、compile.c の WS101 の行（operands・skip_region の atomic・predicated load）の段落の見直し。
2. 他の WS の既存の違反（記録だけ。直さない）: compile.c（liveness・keep_outputs・load_block・store_output・integer・terminate の vertex・describe）、spirv.c（convert・transpose・geometric・integer_compare・switch・pass の `offset += count`）、state.c（vertex input・viewport・blend の call-in-condition 4）、command.c:2130、draw.c:281・610・622、gfx.h:576、pipeline-prepare.c:278、runner.c:181〜198。
3. toolchain の file（`userland/base/noct/Makefile`・`version.mk`・`zedbsd.cmake`）は照合だけ（直しは main）。
4. 回帰を T1 へ（上の手順 4）。
