<!-- awesome-plan project=zedbsd record=ws102-p022 -->

# ws102-p022: 色付きの絵文字 その 2（keyboard の絵文字の面）

Status: in-progress（2026-10-05 P1 generation17、q736。実装・host 試験・build は済み。QEMU は T1、結果まで cleared にしない）
Disposition: normal
Parent: [WS102](../ws.md)
Level: L3
Queue: q736（2026-10-05 ベータ2 の P1 の列、ユーザー「ベータ2の実装をすべて、P1,P2にスケジューリング可能にします。」）
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
   - tap の release: `keyboard_send_commit(server, text, 0U)` と log `ZWL OSK emoji commit sent=<0|1> text=<文字>`。
   - 各 rect を log `ZWL OSK erect category=N index=I x= y= width= height=`（試験が座標を読む。QWERTY の `ZWL OSK qrect` と同じ形）。
4. 試験の手順 `emoji` を `plan/ws102/tests/osk-guest.sh` に足す（`history` の手順、`osk-guest.sh:757` の形）:
   - ime-probe を `--log=/tmp/ime-probe.log` で起こす → flick を開く（`swipe 1272 792 1130 650`）→ 「絵文字」の tab → 種類 0 の index 0 を tap →
     ime-probe の log に `PROBE TEXT text=<表の 0 番目>`、zdesktop の log に `ZWL OSK emoji commit sent=1 text=…`。
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

## 結果（2026-10-05 P1 generation17、q736）

2026-10-03 の試験の方針（細かい修正ごとに回帰を回さない、QEMU は T1）に従い、手順 6 の回帰の組（osk-guest の全手順・large・C9・WS079-p010）は流さない。
QEMU は T1 に依頼する手順に絞る（下）。

| 項目 | 結果 |
| --- | --- |
| 表（手順 1） | `keyboard-layout.c` の `layout_emoji`（4 種類 × 20、`顔`・`手と人`・`物`・`記号`）。どれも既定で絵文字として出る単独の code point（異体字の選択子・ZWJ の列は無し）。`keyboard.h` に `ZWL_EMOJI_*` と `zwl_emoji_count`・`zwl_emoji`・`zwl_emoji_category_name` |
| host 試験（手順 2） | `host-keyboard.c` の `check_emoji`（4×20、1 文字の UTF-8、範囲の外は無し、重複無し）: `host-keyboard: PASS`。`host-emoji.c` の `emoji_missing`（80 個の全部に Noto Color Emoji 2.047 の色の glyph）: `host-emoji: PASS`（`host-emoji.sh` は `keyboard-layout.c` も compile する）。`host-emoji.c` が古い名前 `keiland_color_glyph` を呼んで compile できなかったのを `kl_color_glyph` に直した（p019 の後の改名で古くなっていた） |
| 面（手順 3） | `KEYBOARD_FACE_EMOJI`。「絵文字」の tab を有効に（`keyboard_tool_enabled`）、tab で face と `ZWL OSK tool face=emoji category=N`。`keyboard_emoji_rect`・`_at`・`_release`・`_log`・`keyboard_draw_emoji`: 上に種類の tab の 1 行、下に 5×4 の格子（cell の高さが 52 px 以上なら 36 px、未満なら 24 px の絵文字）。tap で `keyboard_send_commit` と `ZWL OSK emoji commit sent=0|1 text=…`。送った後は voice の key の置き換えの対象を無しにする（絵文字の bytes を消さないように）。場所の log `ZWL OSK etab category=N …`・`ZWL OSK erect category=N index=I x= y= width= height=` |
| 試験の手順（手順 4） | `osk-guest.sh` の `emoji`: 絵文字の tab（1238,141）→ 種類 0 の 0 番目と種類 1 の 3 番目を ime-probe に（`PROBE TEXT text=😀🙌`）→ wltest は断る（`sent=0`・`refused reason=no-text-input`）→ Text Editor に 👍 を打って保存し、file の bytes が `f09f918d`。場所は log の rect から求める。`emoji.png`・`emoji-sent.png` |
| style（手順 5） | `keyboard.c`・`keyboard-layout.c`・`keyboard.h`・`host-emoji.c`: 指摘 0。`host-keyboard.c` は新しい `check_emoji` に指摘 0（既存の行の指摘は p014 の全文規約で） |
| build | zedBSD `bin/wayland` と Linux `bin/wayland`: warning 0。`keiland-os-boundary`: PASS |

T1 への依頼（Q1 経由）: inset の image（`sh plan/ws102/tests/build-inset-image.sh BUILD` と ime-probe）、`pen-guest.sh start`、
`BIN=BUILD sh plan/ws102/tests/osk-guest.sh OUT install start pointer flick tools history emoji`（`osk-guest: PASS`、`emoji.png`・`emoji-sent.png` を見る）。
1920x1080 の `large`、C9・WS079-p010・boot test は今回は依頼しない（keyboard の面の追加だけで、開閉・配置・他の gesture を変えていない）。

未実施: QEMU（T1 の依頼の後）、実機（5330・Windows の QEMU）。COLRv1 に替えるかの判断（格子の大きさでの CBDT の縮小の見え方）は T1 の `emoji.png` を見てから。
