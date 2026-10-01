<!-- awesome-plan project=zedbsd record=ws102-p022 -->

# ws102-p022: 色付きの絵文字 その 2（keyboard の絵文字の面）

Status: planned（2026-10-01 手順を追記。Queue なし。WS102 の再開はユーザーが言うとき）
Disposition: normal
Parent: [WS102](../ws.md)
Level: L3
Queue: なし
依存: p019（色の絵文字の font と描画、cleared）、p016（道具の面、cleared）、p024（道具の面の face の切り替え、cleared）

## 範囲と受け入れ

design.md §2.10 の「絵文字: 種類の tab と格子、tap で送る。text-input の commit（text-input の無い app へは送らない）」。

- flick の panel の道具の面の「絵文字」の tab（今は無効: `userland/desktop/wayland/keyboard.c:3457-3459` が 0 を返す）を有効にし、絵文字の面を出す。
- 絵文字の面: 種類の tab（顔・手と人・物・記号 の 4 つ）と、種類ごとの格子（1 種類 16〜24 個の固定の表）。tap した絵文字を `keyboard_send_commit`（`keyboard.c:2177`）で
  focus の text-input に commit する。text-input の無い app へは送らず、既存の `ZWL OSK refused reason=no-text-input` を出す。
- 描画は compositor の glass の文字の描画（色の絵文字の fallback、`userland/desktop/wayland/glass.c:2082-2090`）を使う。
- 範囲外: 絵文字の検索、最近使った絵文字、肌の色の選択、ZWJ の合成の列（家族など）。表は単独の code point（と U+FE0F の付く物）だけ。IME の file（`ime.h`・`text-input.c`・`input-method.c`）は変えない。

## 手順（2026-10-01 追記）

1. 表: `userland/desktop/wayland/keyboard-layout.c` に絵文字の表（種類の名前と UTF-8 の文字列の配列）と、`keyboard.h` に参照の関数（例 `zwl_keyboard_emoji_count(category)`・`zwl_keyboard_emoji(category, index)`）を足す。
   4 種類 × 16〜24 個。どれも Noto Color Emoji v2.047 に glyph がある文字を選ぶ。
2. host の試験: `plan/ws102/tests/host-keyboard.c` に、表の全ての項目が正しい UTF-8 で空でなく、種類の中で重複が無いことを足す。
   font に glyph があることは `plan/ws102/tests/host-emoji.c` に表の全ての文字の `truetype_color_glyph` の成功を足して確かめる（host-emoji.sh は keyboard-layout.c も compile するように 1 行足す）。
3. 面: `keyboard.c` に `KEYBOARD_FACE_EMOJI`（`keyboard.c:227-228` の隣）を足す。
   - `keyboard_tool_enabled`（`:3446`）で `TOOL_TAB_EMOJI` を 1 に（`TOOL_TAB_CANDIDATES` は 0 のまま）。
   - `keyboard_tool_release`（`:3501`）の `TOOL_TAB_EMOJI` で `keyboard.tools_face = KEYBOARD_FACE_EMOJI` と log `ZWL OSK tool face=emoji category=N`。
   - 履歴の面の形（`keyboard_history_rect`・`_at`・`_release`・`keyboard_draw_history`、`:3682`・`:3707`・`:3738`・`:3757`）に倣い、`keyboard_emoji_rect`・`_at`・`_release`・`keyboard_draw_emoji` を足す。
     上に種類の tab の 1 行、下に格子（key の大きさは道具の面の key と同じ）。press と release の扱いは履歴の面（`:1543`・`:1699` の分岐）に絵文字を並べる。
   - tap の release: `keyboard_send_commit(server, text, 0U)` と log `ZWL OSK emoji commit text=<文字> sent=<0|1>`。
   - 各 rect を log `ZWL OSK erect category=N index=I x= y= width= height=`（試験が座標を読む。QWERTY の `ZWL OSK qrect` と同じ形）。
4. 試験の手順 `emoji` を `plan/ws102/tests/osk-guest.sh` に足す（`history` の手順、`osk-guest.sh:757` の形）:
   - ime-probe を `--log=/tmp/ime-probe.log` で起こす → flick を開く（`swipe 1272 792 1130 650`）→ 「絵文字」の tab → 種類 0 の index 0 を tap →
     ime-probe の log に `PROBE TEXT text=<表の 0 番目>`、zdesktop の log に `ZWL OSK emoji commit text=… sent=1`。
   - 種類 1 の tab → index 3 を tap → 2 文字目が届く。
   - wltest（text-input 無し）を前に出して tap → `ZWL OSK refused reason=no-text-input` が 1 行、`sent=0`。
   - Text Editor（libkeiui の text-input-v3、D1）で 1 個 tap → Ctrl+S → `/root/e.txt` の byte が表の文字の UTF-8（`guest.py run 'od -An -tx1 /root/e.txt'`）。
   - 画面 `emoji.png`（絵文字の面）・`emoji-sent.png`（Text Editor に色で出た絵文字）。
5. `python3 plan/tools/style-check.py userland/desktop/wayland/keyboard.c userland/desktop/wayland/keyboard-layout.c` が違反 0。
6. 回帰: [手引き](../guide.md) の「Phase の回帰の組」（build・host 3 本・osk-guest の全手順に `emoji` を加えた物・large・C9・WS079-p010・boot test）。
   command は p024 の「手順（2026-10-01 追記）」の列をそのまま使い、osk-guest の手順の最後に `emoji` を足す。

## 完了の条件

- host: `host-keyboard: PASS`（絵文字の表の検査を含む）、`host-emoji.sh` が exit 0（表の全ての文字に色の glyph）。
- guest: `osk-guest.sh … emoji` の全ての確かめが ok、全手順 `osk-guest: PASS`、1920x1080 の `large` PASS。
- 画面で絵文字の面の格子と、Text Editor の色の絵文字が読める（`emoji.png`・`emoji-sent.png` をユーザーに見せる）。
- C9 の FAIL 0（p076 だけなら BUG-125 として単独 3 回の結果を併記）、`p010: PASS`、boot test PASS。
- build の warning 0、style の違反 0。
- 実機（5330・Windows の QEMU）は範囲外（「未実施」と書く）。
