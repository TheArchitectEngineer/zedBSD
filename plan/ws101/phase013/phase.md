<!-- awesome-plan project=zedbsd record=ws101-p013 -->

# ws101-p013: 規約の全文との照合と回帰（WS101 の全ての変更）

Phase ID: `ws101-p013`
Parent: [WS101](../ws.md)
Status: planned（2026-10-01 に phase.md を作った。範囲は ws.md の表の行のまま）
Phase disposition: normal
Queue: なし

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

未実施。
