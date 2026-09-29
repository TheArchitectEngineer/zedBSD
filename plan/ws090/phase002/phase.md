<!-- awesome-plan project=zedbsd record=ws090-p002 -->

# ws090-p002: libkeiui の骨組みと描画の層

Status: cleared（2026-09-29、subagent の worktree `wt/ws090`、main cdde9518 を merge した上）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-29「ws090-p002（骨組みと描画の層、Settings の書き換えを除く）」）
依存: p001（cleared）

## 範囲と受け入れ

- 新しい共有 library `libkeiui`（design.md §2・§3）の骨組み: header `include/libc/keiui.h`（`KUI_VERSION` 1）、`userland/desktop/libkeiui/`、
  exports.map（`kui_*` だけ）、package の Makefile、`platform/amd64/vmunix.mk` の `libkeiui.so` の link の規則。
- 描画の層: canvas・text・icons（Files の icon と Settings の線の絵）・theme（Files の値）。
- **Settings の書き換えは p007 へ**（2026-09-29 main: WS089 が Settings の source を触っている間は変えない）。
- 受け入れ: `libkeiui.so` の build（`-Werror`）warning 0、host の試験で Files・Settings の実装と同じ絵（byte で一致）、規約の checker 0。

## 実装

- `canvas.c`・`text.c`・`icons.c` は Files の同名の file を `fm_`→`kui_`・`FM_`→`KUI_` に置き換えて移した。`icons-line.c` は Settings の `glyphs.c`
  （`se_glyph_draw`）を移し、内部の `keiui_icon_line_draw`（export しない、`internal.h`）にした。
- icon は 1 つの `enum kui_icon`: Files の 23 の icon（`KUI_ICON_HOME`〜`KUI_ICON_DOWN`）の後に Settings の 25 の線の絵を **Settings の順のまま**
  続けた（`KUI_ICON_TILES`〜`KUI_ICON_DISCLOSURE`）。名前がぶつかる 2 つは改名（Settings の GRID→`KUI_ICON_TILES`（枠の 4 枚、Files の GRID は塗りの
  4 枚で絵が違う）、CHEVRON→`KUI_ICON_DISCLOSURE`）。`kui_icon_draw` は `KUI_ICON_TILES` 以降を線の絵の pen に回す。p007 で Settings を移す時は
  `SE_GLYPH_x` → `KUI_ICON_TILES + x` の 1 対 1。
- `theme.c`: `struct kui_theme`（地・card・glass の veil・文字 3 段・icon・accent・選択・hover・区切り・folder・danger、角の半径、行の高さ、文字の大きさ）と
  `kui_theme_default()`（Files の `FM_COLOR_*` の値）。`version.c`: `kui_version()`。
- 規約: 移した file のうち 2 か所を意味を変えずに直した（`canvas.c` の `canvas_lerp_pixel` の最後の `return result;` → `blended` と成功の注釈（§11）、
  `text.c` の glyph の key の `(bold != 0)` → if で作る `weight`（§6））。直した後も絵は Files と byte で一致（下）。
- libkeiland の内部の `paint.c`・`paint-text.c`（file chooser）は p006 で chooser と一緒に libkeiui へ移す（今は触らない）。

## 試験

- build: `build/ws090-p002-build.sh`（worktree、git の外）= `make -j64 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/dynamic/libkeiui.so`
  → exit 0、warning 0。export は `kui_*` の 37 個だけ（`nm -D`）、内部の `keiui_icon_line_draw` は出ない。NEEDED は `libtruetype.so`・`libc.so`。
- host: `sh plan/ws090/tests/host-draw.sh` → **13/13**。同じ場面（地の gradient・影・角丸・角丸の gradient と縁・円・環（0.7 周）・星の多角形・線・
  太字と省略と日本語（fallback）と折り返しの文字・Files の 23 の icon・Settings の 25 の線の絵・folder・帯と label の file・tag・拡大した画像）を
  Files と Settings の実装（`fm_`・`se_`）と libkeiui（`kui_`）で描き、**違う pixel は 0**。theme の値が Files の `FM_COLOR_*` と同じこと、
  `kui_color_mix` が同じこと、版。絵: `build/ws090-shots/host-draw-kui.png`・`host-draw-fm.png`（worktree）。
- 規約: `python3 plan/tools/style-check.py userland/desktop/libkeiui/*.c plan/ws090/tests/host-draw.c` → 違反 0。
- 未実施: QEMU（まだどの program も libkeiui を使わず、image にも入れていない）、boot test（image の中身が変わらない。この worktree には
  ssh の image の build の成果物が無く、package の build から始まるので省いた。libkeiui を image に入れる最初の Phase（p004）で行う）。

## 残り・main への依頼

- image への登録（`config/ci/config-amd64.mk`・`plan/ws035/tests/config-amd64-zdesktop.mk` の `ZEDBSD_USER_PROGRAMS` に `libkeiui`）は、使う最初の
  program ができる p004 で行う（この worktree の範囲の外の file なので、その時に main の許可を得る）。
- `plan/ws090/tests/host-draw.sh` は WS090 の完了の時に `plan/tools/keiui/` へ移す候補。
