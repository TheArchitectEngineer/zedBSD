<!-- awesome-plan project=zedbsd record=ws068p007 -->

# ws068-p007: 規約の全文との照合と回帰（最後）

Phase ID: `ws068-p007`
Parent: [WS068](../ws.md)
Status: planning（2026-10-01 に phase.md を作った。範囲は ws.md の表の行のまま。保留の p034〜p037 を対象に含めるかは main の判断）
Phase disposition: normal
Queue: なし

## 範囲（ws.md の表から）

WS068 の全ての変更の規約の全文（[plan/coding-style.md](../../coding-style.md)）との照合と回帰。code を作る WS の必須の Phase（AGENTS.md「コード」、Awesome Plan §6）。
違反の修正だけで、意味を変えない。HAL・toolchain は照合の対象外（WS068 は触っていない）。

## 依存

全 Phase（p009・p004・提案の p038 の後）。保留の p037・p034〜p036 を WS の受け入れから外すかの判断（[guide.md](../guide.md) 3.2 の提案 B）。

## 手順（2026-10-01 追記）

1. 対象の file:
   ```
   mkdir -p build/ws068-p007
   ls userland/desktop/libegl/*.c userland/desktop/libegl/*.h userland/desktop/libglesv2/*.c userland/desktop/libglesv2/*.h \
      userland/desktop/libglesv2/glsl/*.c userland/desktop/libglesv2/glsl/*.h userland/desktop/libwayland-egl/*.c \
      userland/desktop/egltest/*.c userland/desktop/egltest/*.h userland/retro/libGL/*.c userland/retro/libGL/*.h userland/retro/glxtest/*.c \
      2>/dev/null > build/ws068-p007/files.txt
   wc -l build/ws068-p007/files.txt
   ```
   （libGL・glxtest は WS069 と共有。WS068 の Phase（p013・p031・p033）が変えた部分を照合する。`grep -n "WS068\|ws068" FILE` で印を拾う）
2. 機械の検査:
   ```
   xargs python3 plan/tools/style-check.py --summary < build/ws068-p007/files.txt > build/ws068-p007/style.txt 2>&1; tail -3 build/ws068-p007/style.txt
   git diff --check
   ```
   WS101 が入れた行（`ws101` の印）は WS101 の p013 の対象。重ねて直さない。
3. 全文の review: coding-style.md §14（Review checklist）の項目を表にし、機械で見られない規則（§5・§6・§10・§12）を読む。
4. 回帰（[guide.md](../guide.md) 5.1〜5.4 の順。image の build は 1 つずつ、実機は lock の script だけ）:
   ```
   sh plan/ws068/tests/glsl-host/run.sh build/ws068-p007/glsl-host
   sh plan/ws068/tests/spirv-host/run.sh build/ws068-p007/spirv-host
   sh plan/ws101/tests/glsl/run.sh build/ws068-p007/ws101-glsl
   sh plan/ws101/tests/gles/run.sh build/ws068-p007/ws101-gles
   plan/ws068/tests/build-glsl-image.sh build/ws068-p007-glsl > build/ws068-p007/img.log 2>&1; echo "exit=$?"
   ```
   Venus: guide.md 5.3 の起動と for の行（egl-p008〜glx-p033）と x11-p004・x11-p005（`GUEST_RUNTIME=$PWD/build/ws068-p007-run`）。
   実機: `BUILD=build/ws068-p007-gles plan/ws101/tests/hw/gles-hw.sh build/ws068-p007/gles-hw`、p038 があれば `es-hw.sh`。
   boot test: `OUTPUT=build/ws068-p007/boot plan/tools/boot-test.sh build/ws068-p007-glsl/hdd-image.img`。
5. 結果を「確認」に、command・結果・未実施を分けて書く。

## 完了の条件

- style-check の WS068 の分の違反 0（例外は理由と出典付き）、`git diff --check` 0、§14 の review の表がある。
- host の 4 本が PASS、Venus の全 egl・glx・x11 の試験が exit 0 で `log: … MISSING` 無し、gles-hw.sh が PASS（lock が取れなければ「未実施」で uncleared）、boot test が PASS。
- 直した source があれば、その領域の試験を直した後に再実行して PASS。

## 確認

未実施。
