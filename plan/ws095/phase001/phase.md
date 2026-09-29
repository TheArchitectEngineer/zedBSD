<!-- awesome-plan project=zedbsd record=ws095-p001 -->

# ws095-p001: IME の設計

Status: in-progress（2026-09-29、wrap up で中断。設計の文書は一通り書いた、main・ユーザーの確認待ち）
Disposition: normal
Parent: [WS095](../ws.md)
Queue: main が割り当て（2026-09-29、worktree `.claude/worktrees/ws095-ime`、branch `wt/ws095`）。Queue の ID は main が記録する

## 範囲と受け入れ

compositor（`userland/desktop/wayland`）の `zwp_input_method_v2`・`zwp_text_input_v3` の仲介、IME の program（候補の窓は input-method の popup surface）、
ローマ字→かな→変換、辞書（REmacs の SKK の辞書、image への入れ方）、app の側（Terminal・Text Editor・Browser の text field・Settings の検索）、
切り替えの key を設計する。source は変えない（他の WS が wayland・terminal・browser・textedit を変えているため）。人間の判断が要る点は既定を選んで挙げる。

## 結果（どこまで書いたか）

[design.md](../design.md) を §1〜§14 まで全て書いた（2026-09-29）:
§1 範囲、§2 全体の構成（IME は zdesktop が起こし、IME 用の global は その pid の client だけに見せる）、§3 protocol（text-input-v3・input-method-v2・
virtual-keyboard-v1 を手書きの記述で、私的な `keiland_ime_status_v1`）、§4 compositor の仲介（`zwl_seat_key()` の shortcut の後に grab への分岐、
virtual keyboard は grab を通さず focus へ、popup を cursor の矩形の下に、zdesktop の自前の field の「内部の text input」）、§5 IME の program の file の構成、
§6 言語の engine の interface、§7 日本語の engine（ローマ字の表は REmacs の `skk.noct` と同じ、動的計画法の分割、活用の語尾の表、利用者の辞書）、
§8 indicator と切り替えの私的な拡張、§9 辞書の置き場所（`SKK-JISYO.X` を pin した REmacs の source から image へ。補いの辞書 `SKK-JISYO.kei`）、
§10 切り替えの key、§11 app の側の表、§12 試験、§13 Phase の案（p002〜p009）、§14 判断が要る点 D1〜D9。

調べて確かめた事実（設計の根拠）:
- zdesktop の protocol は手書き（`protocol.c` の `globals[]`、名前 13〜15・20〜 が空き）、libwayland の client の記述も手書き（`primary-selection-protocol.c` の型）。
- key は `seat.c` の `zwl_seat_key()` が zdesktop の shortcut の後に focus の client の `wl_keyboard` へ送る。keymap は US だけ（`keymap.c`）。
  app は自分の `keys.c` で evdev の code を文字にする。Super+Space は未使用（Super+Tab・Super+L だけ）。
- USB HID の Japanese の key は kernel で evdev に写る（`usb-hid.c`: 変換・無変換・LANG1〜5）。PS/2 には無い。JIS の 半角/全角 は USB では KEY_GRAVE。
- 辞書: `build/sources/remacs/dict/SKK-JISYO.X`（16,412 見出し、387 KB、UTF-8、okuri-ari 2,296）は `SKK-JISYO.remacs` を全て含む。REmacs の revision
  `1a724393053e18c4e1f502ecc5ca8ce07d99287a`。欠けの例: `わたし`→私、`にほん`→日本、`ありがとう` が無い。動詞の活用の種類は REmacs の
  `tools/skkdict/src/*.tsv` にある。
- WS092 の Text Editor は `te_edit_insert_text`・`app->preedit`・`te_layout_cursor_rect` を IME のために用意する設計（ws092 design §12）。
- Settings・Files の検索は zdesktop の titlebar の field（`titlebar-shell.c`）、App Home の検索は `home.c`。

## 残り（引き継ぎ）

1. design.md の見直し（設計の敵対的な review。AGENTS の運用では design-reviewer の agent がある）と、§14 の判断をユーザーに挙げる（main 経由）。
2. ws.md の Phase の表に p002〜p009 を design §13 から写す（この commit で ws.md に表を足した。状態は planning）。
3. 決めかけの判断（既定を選んだが確認が要る。design §14）:
   - D1 辞書の入れ方: 既定は「pin した REmacs の source から build で取り、tree に入れない」。辞書の header の license の行（GPL）と image の許可の文の書き方。
   - D2 変換の操作: 既定は Space で全体を変換（MS-IME 型）、SKK 型の大文字の起点にしない。
   - D3 補いの辞書 `SKK-JISYO.kei` の量と範囲（ユーザーの「足りない分は要相談」）。
   - D7 JIS の配列（半角/全角 = KEY_GRAVE）は別の WS を提案。デモの機械（5330）の keyboard の配列の確認が要る。
   - D5 直接入力でも key を IME に通す（既定）か、zdesktop が飛ばすか。
   - D4（単語の登録なし）・D6（切り替えで確定）・D8（`/usr/libexec/keiland-ime`）・D9（wl_shm の候補の窓、glass なし）。
4. 未確認: input-method-unstable-v2・virtual-keyboard-unstable-v1 の XML の入手元と pin（wlroots の `protocol/`、build の host に package があるか）。
   text-input-unstable-v3 は build の host の wayland-protocols 1.44 の package にある見込み（未確認）。

## Resume point

design.md は全節あり。次の agent は上の「残り」の 1（review と判断の提示）から。source の変更は p002 から（engine は Wayland 無しで host の試験付き）。
