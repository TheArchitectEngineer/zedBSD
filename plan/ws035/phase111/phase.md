<!-- awesome-plan project=zedbsd record=ws035p111 -->

# ws035-p111: terminal の文字の選択の残り（語・行単位の drag、Shift+click）

Phase ID: `ws035-p111`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 3。[p093](../phase093/phase.md)・[p100](../phase100/phase.md) の残り）

## 範囲

割り当て: Wayland の terminal（`userland/desktop/terminal`）の文字の選択。反転表示、drag での選択、double click の語、triple click の行、
CLIPBOARD と PRIMARY への copy、中 button の PRIMARY の paste。実装を調べると、これらは p093（範囲・語・行・drag・Ctrl+Shift+C）と
p100（PRIMARY・中 button）で既に動いていた。この Phase では p093 の残りのうち主な経路にあたる 2 つを足した。X11 の zterm は
retro の program なので触れない。

## 実装（2026-09-28）

- **語・行単位の drag**（`userland/desktop/terminal/main.c`）: double・triple click の press の後も button を押している間は
  選択中（`main_unit` が 2 なら語、3 なら行）。最初の語・行（`main_unit_from`・`main_unit_to`）を保ち、pointer の下の語・行の
  遠い端まで範囲を伸ばす（pointer が前にあれば後ろ向き）。語の境界は既存の `main_word_character` を使う `main_unit_bounds()` に
  まとめた。伸ばした選択は release で `ZTERM SELECT how=words|lines` を出し、PRIMARY にする。
- **Shift+click**: pointer の event に wl_keyboard の modifiers を持たせ（`terminal.h`・`window.c`）、Shift の press は最後の
  選択の始点（anchor）から click した cell までを選ぶ。範囲の中の click で選択を消したときは、その cell を anchor にする。
- PRIMARY の文は 4096 byte の途中の buffer を通さずに `main_primary`（画面全体の大きさ）へ直接取る（長い選択が切れない）。
- 試験の道具: `plan/ws035/tests/qmp-pointer.py` に `shift-down`・`shift-up`（QMP の key の event）。

## 検証（amd64、Venus、lean な files の image `build/p110-files`、2026-09-28）

- `plan/ws035/tests/zdesktop-p111.sh`（新）PASS:
  1. "gamma" を double click して "delta" まで drag: `how=word from=6,0 to=15,0 bytes=10` の後に `how=words from=6,0 to=21,0
     bytes=16`（`words.png`）。
  2. 0 行目を triple click して 1 行目へ drag: `how=lines from=0,0 to=89,1`（`lines.png`）。
  3. "one" を click、Shift で "two" の "o" を click: `how=drag from=0,1 to=6,1 bytes=7`、`PRIMARY set bytes=7`（`shift.png`）。
     Ctrl+Shift+C で `COPY bytes=7`（CLIPBOARD）。
  4. 中 click で `PASTE primary bytes=7`、Ctrl+Shift+V で `PASTE bytes=7`。prompt に "one twoone two"（`pasted.png`）。
  1 回目は 3. が FAIL だった: 1 行目の click が 2. の範囲の中だったので、範囲を消すだけで anchor が 2. のまま（0,0）だった。
  範囲を消す click の cell を anchor にするよう直して PASS。
- 回帰 PASS: zdesktop-p093（語・行・drag・範囲の drag out）、zdesktop-p100（2 つの terminal の間の PRIMARY と中 click）。
- build warning 0（terminal）。`plan/tools/style-check.py userland/desktop/terminal/main.c` 0。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p111-20260928-{words,lines,shift,pasted}.png`。
- 未実施: 実機。

## 残り

- scroll と範囲の追従（出力で画面が流れても範囲は画面の位置のまま）、drag で窓の端を越えたときの自動の scroll。
- 選択を消したとき PRIMARY を空にしない（p100 の残りと同じ。X と同じく最後の選択が残る）。
