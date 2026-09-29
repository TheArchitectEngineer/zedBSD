<!-- awesome-plan project=zedbsd record=ws095-design -->

# WS095 の設計: IME（Wayland の標準の方法、まず日本語）

2026-09-29 ws095-p001。目標は [ws.md](ws.md) のユーザーの要望（原文）。この文書は設計だけで、source は変えない（他のエージェントが
wayland・terminal・browser・textedit を変えている最中のため）。人間の判断が要る点は §14 に既定と一緒に挙げる。

## 1. 範囲

- 範囲: system の既定の IME を 1 つ（program 1 つ）入れる。IME は複数の「入力の言語」（input source）を持ち、切り替えられる。最初の言語は
  **直接入力（英数）** と **日本語**。日本語は ローマ字 → ひらがな → 変換（名詞＋助詞・動詞＋送り仮名 程度の簡単な分割）→ 候補 → 確定。
  辞書は REmacs の辞書（SKK の形式）を土台にする。
- Wayland の標準の方法: app は `zwp_text_input_v3`（wayland-protocols の text-input-unstable-v3）、IME は `zwp_input_method_v2`
  （input-method-unstable-v2。wayland-protocols ではなく wlroots・KWin・fcitx5・squeekboard が使う事実上の標準）と
  `zwp_virtual_keyboard_v1`（virtual-keyboard-unstable-v1、同じく wlroots の系統）。compositor（zdesktop、`userland/desktop/wayland`）が両方を仲介する。
- 範囲外（後の WS・判断待ち）: 日本語以外の言語（仕組みだけ複数言語の前提で作る）、学習の高度化（頻度の統計）、単語の登録の UI（§7.5）、
  X11 の app（XIM。`keiland-x11` の下の zterm・zgears は対象外）、JIS の配列の選択（§10.3）、予測の変換・かな入力（親指・JIS かな）、
  絵文字・記号の palette。

## 2. 全体の構成

```
 app（Terminal・Text Editor・Browser…）            IME（/usr/libexec/keiland-ime、1 つ）
   zwp_text_input_v3  ──┐                  ┌── zwp_input_method_v2
                        │   zdesktop       │     ├ keyboard_grab_v2（key を受ける）
   compositor の自前の   ├── (仲介・位置・  ──┤     ├ input_popup_surface_v2（候補の窓）
   text field（検索など）┘    focus・key)    └── zwp_virtual_keyboard_v1（IME が使わない key を戻す）
                                               keiland_ime_status_v1（表示の状態、私的な拡張）
```

- zdesktop は session の開始（`main.c` の初期化の後）に IME を `zwl_spawn()`（`home.c`）で起こし、その pid を覚える。IME が死んだら
  入力は素通しに戻り（key は focus の client へ）、5 秒後に 1 回だけ起こし直す（続けて死ぬなら諦めて log に残す）。
- IME の側の protocol の global（`zwp_input_method_manager_v2`・`zwp_virtual_keyboard_manager_v1`・`keiland_ime_status_manager_v1`）は、
  zdesktop が起こした IME の client（接続の SO_PEERCRED の pid が覚えた pid と同じ）にだけ registry で見せる。他の client が bind すれば protocol error。
  key の盗み見と偽の key の注入を、system の IME 以外に許さないため。

## 3. protocol（client の library と compositor の両方）

text-input-unstable-v3・input-method-unstable-v2・virtual-keyboard-unstable-v1 の XML を pin し（build の host の wayland-protocols の package、
wlroots の `protocol/` の revision と SHA-256 を `include/libc/wayland/API-PROVENANCE.md` に記録）、今までと同じく **手で書いた記述**にする
（scanner の出力は入れない）。

| 置き場所 | 内容 |
| --- | --- |
| `include/libc/wayland/text-input-unstable-v3-client-protocol.h`、`userland/desktop/libwayland/text-input-protocol.c` | app 用: manager・text_input の request と event |
| `include/libc/wayland/input-method-unstable-v2-client-protocol.h`、`userland/desktop/libwayland/input-method-protocol.c` | IME 用: manager・input_method・popup_surface・keyboard_grab |
| `include/libc/wayland/virtual-keyboard-unstable-v1-client-protocol.h`、`userland/desktop/libwayland/virtual-keyboard-protocol.c` | IME 用 |
| `userland/desktop/libwayland/keiland-ime-status-protocol.c`、`zed-ime-status-v1-client-protocol.h` | 私的な拡張（§8） |
| `userland/desktop/wayland/text-input.c`（新規） | compositor: text_input_v3 の状態（enable・surrounding・content_type・cursor_rectangle・commit の serial） |
| `userland/desktop/wayland/input-method.c`（新規） | compositor: input_method_v2 の仲介、keyboard grab、popup の配置、virtual keyboard、IME の起動と監視 |

使う版: text_input_v3 v1、input_method_v2 v1、virtual_keyboard_v1 v1。`zwl_kind` に
`ZWL_TEXT_INPUT_MANAGER`・`ZWL_TEXT_INPUT`・`ZWL_INPUT_METHOD_MANAGER`・`ZWL_INPUT_METHOD`・`ZWL_INPUT_POPUP`・`ZWL_KEYBOARD_GRAB`・
`ZWL_VIRTUAL_KEYBOARD_MANAGER`・`ZWL_VIRTUAL_KEYBOARD`・`ZWL_IME_STATUS` を足し、`protocol.c` の `globals[]` に空いている名前（13〜15、20〜）で登録する。

## 4. compositor（zdesktop）の仲介

### 4.1 text-input の状態

- 1 つの seat に 1 つの「今の text input」。keyboard の focus の surface の client が持つ `zwp_text_input_v3` に `enter(surface)` を送り、
  focus が外れたら `leave`。client が `enable` → `commit` した時だけ「有効」。
- 有効な text input があると、IME に `activate` と `surrounding_text`・`content_type`・`text_change_cause` を送り `done`。無効・leave で `deactivate`・`done`。
- IME の `commit_string`・`set_preedit_string`・`delete_surrounding_text`・`commit(serial)` を、今の text input の client にそのまま
  `commit_string`・`preedit_string`・`delete_surrounding_text`・`done(serial)` として渡す（text-input-v3 の「done で一度に適用」の規則）。
- content の目的が `password`（`zwp_text_input_v3.content_purpose.password`）の field は IME を通さない（常に直接入力、preedit を出さない）。
  greeter・lock の画面の password（zdesktop の自前）も同じ。

### 4.2 key の流れ

`zwl_seat_key()`（`seat.c`）の順を保ち、zdesktop の shortcut（Super+L・App Home・menu・titlebar・glass・Wiseview…）の **後**、focus の client へ
送る **前** に分岐を 1 つ足す:

1. 言語の切り替えの key（§10）は zdesktop が先に取り、IME に `keiland_ime_status` で「次の言語」を指示する（どの app でも同じに効く）。
2. IME が keyboard grab を持ち、今の text input が有効（または zdesktop の自前の field が IME を使う状態、§4.4）なら、key・modifiers・keymap を
   **grab へ**送る（focus の client へは送らない）。
3. IME は使わない key（直接入力の時の全ての key、変換中でない時の矢印・Ctrl の組み合わせ等）を `zwp_virtual_keyboard_v1.key`・`modifiers` で戻す。
   zdesktop は virtual keyboard の key を **grab を通さずに** focus の client へ送る（zdesktop の shortcut も通さない。二重の処理を防ぐ）。
   virtual keyboard の keymap は zdesktop の keymap（`keymap.c`）と同じものを使わせ、IME の keymap の差し替えは受けない。
4. key の repeat は今どおり client の側（`wl_keyboard.repeat_info`）。grab にも repeat_info を送り、IME は変換中の BackSpace・矢印を自分で repeat する。

直接入力の言語の時も key は IME を一度通る（IME が素通しで戻す）。遅延は 1 往復の socket で、zdesktop・IME とも同じ機械の中なので小さいと見込む
（p004 で key の往復の時間を計る。目安 1 ms 未満）。直接入力の時は zdesktop が grab を飛ばす最適化を後で検討できる（§14 D5）。

### 4.3 候補の窓（input popup surface）

- IME は `get_input_popup_surface(wl_surface)` で候補の窓を作る。zdesktop は今の text input の `set_cursor_rectangle`（surface の座標）を
  画面の座標に直し、popup を **cursor の矩形の下の左端**に置く（画面の下に出れば上に、右に出れば左へずらす）。IME に
  `text_input_rectangle` で cursor の矩形（popup の座標系）を送る。
- popup は全ての窓の上（menu と同じ層）に描き、focus・pointer の入力を取らない（候補の click は後で。最初は key だけ）。glass の窓の効果は付けない。

### 4.4 zdesktop の自前の text field

App Home の検索（`home.c`）、titlebar の検索の field と breadcrumb（`titlebar-shell.c`。Settings・Files の検索はここ）は zdesktop が描く field。
IME の側からは普通の text input と同じに見えるよう、zdesktop の中に「内部の text input」を 1 つ置く:
field が keyboard を取った時に IME へ `activate`（surrounding は field の文字列、cursor の矩形は field の中の cursor）、IME の commit・preedit を
field の文字列と preedit の表示に適用する。field ごとの描画に「下線付きの preedit」を足す（`titlebar-shell.c`・`home.c` の field の描画）。

## 5. IME の program（`/usr/libexec/keiland-ime`）

- 置き場所 `userland/desktop/ime/`（新規の package `ime`、MENU は desktop、既定で選ぶ）。C、coding-style の全文。Wayland と描画の部分と、
  言語の engine（Wayland を知らない）を分ける:

| file | 役割 |
| --- | --- |
| `main.c` | 接続、globals、event の loop、設定の読み込み、`KEI-IME READY/DONE/FAILED` の log |
| `method.c` | input_method_v2 の状態機械（activate・deactivate・surrounding・done の serial）、keyboard grab、virtual keyboard での戻し |
| `keys.c` | evdev の code → 文字（US の配列と Shift。Terminal の `keys.c` と同じ表）、modifier の追跡 |
| `engine.h` | 言語の engine の interface（§6） |
| `engine-direct.c` | 直接入力（全ての key を戻す） |
| `ja-engine.c` | 日本語の engine: 状態（入力中・変換中）、key の解釈 |
| `ja-romaji.c` | ローマ字 → かな（§7.1） |
| `ja-dict.c` | 辞書の読み込み・引き（§7.3、§9） |
| `ja-segment.c` | 文節の分割と候補（§7.2） |
| `ja-user.c` | 利用者の辞書（選んだ候補の順の学習、§7.4） |
| `popup.c` | 候補の窓の描画（wl_shm、libtruetype と `keiland-fallback.ttf`（Droid Sans Fallback）の日本語の glyph） |
| `status.c` | `keiland_ime_status_v1`（今の言語の表示、切り替えの指示） |

- engine は Wayland を知らない純粋な C にし、host（Linux）で試験できるようにする（§12）。
- 設定: `/etc/kei/ime.conf`（system の既定: 有効な言語の順、切り替えの key）と `~/.config/kei/ime.conf`（利用者の上書き）。最初の版は
  言語の順 `direct ja` と、起動の時の言語 `direct` だけ。

## 6. 言語の engine の interface（複数言語の前提）

```c
struct ime_engine_ops {
	const char *id;            /* "direct"、"ja" */
	const char *label;         /* 表示の短い名前: "A"、"あ" */
	int (*key)(struct ime_engine *engine, const struct ime_key *key, struct ime_output *out);
	void (*reset)(struct ime_engine *engine, struct ime_output *out);   /* focus の変化・切り替え: 未確定を確定か破棄 */
	void (*surrounding)(struct ime_engine *engine, const char *text, uint32_t cursor, uint32_t anchor);
};
```

`ime_output` は「確定する文字列」「preedit と cursor・下線の範囲」「候補の一覧と選んだ番号」「key を戻すか」を持つ。`method.c` がこれを
input_method_v2 の request（commit_string・set_preedit_string・delete_surrounding_text・commit）と popup の描画に変える。
言語を足す時は engine を 1 つ足し、`ime.conf` の順に入れるだけにする。

## 7. 日本語の engine

### 7.1 ローマ字 → かな

- REmacs の `editor/skk.noct` の `skkInitRomaji()` の表（訓令式・ヘボン式、拗音、`xa`・`la` の小書き、`xtu`）と同じ規則を C の表にする。
  `nn` → ん、子音の後の `n` → ん、同じ子音の重ね → っ（`skkComposeChar()` と同じ）。加えて `-` → ー、`,` → 、、`.` → 。、`[` → 「、`]` → 」、
  `/` → ・（MS-IME・Google 日本語入力の既定と同じ）。
- 入力中の preedit は「確定したかな＋未確定のローマ字」（例 `かんじy`）。Shift を押した大文字は、その文字から英字のまま（SKK と違い、
  大文字は変換の起点にしない。§14 D2）。

### 7.2 変換: 文節の分割

入力された ひらがな の列（1 回の Space で変換する範囲）を、簡単な規則で文節に分ける。形態素解析はしない（ユーザーの指示「非常に簡単でよく、
名詞＋助詞、動詞＋送り仮名、くらいの分解でよい」）。

- 語の種類:
  - **名詞**（体言）: 辞書の okuri-nasi の見出し（例 `にほんご` → 日本語）。
  - **動詞・形容詞**＋送り仮名: 辞書の okuri-ari の見出し（語幹＋送りの子音、例 `かk` → 書）。語幹の後の最初のかなの子音が見出しの子音と
    合えば、その後に **活用の語尾の表**（§7.2.1）の最も長い一致を送り仮名としてつなげる（例 `かきます` → 書＋きます、`たべた` → 食（`たb`）＋べた）。
  - **助詞・助動詞の一部**: 固定の表（は・が・を・に・へ・と・で・も・の・や・から・まで・より・ね・よ・か・な・って・です・でした・ます・だ・でしょう）。
    かなのまま。名詞・動詞の後にだけ付く。
  - **不明**: 辞書に無いかなの並び（そのままのひらがなの文節、候補にカタカナ）。
- 分割: 左から動的計画法で、費用が最小の並びを選ぶ。費用 = 文節の数（少ない方）→ 不明の文字の数（少ない方）→ 辞書の語の長さ（長い方）。
  各文節の候補は、辞書の順（SKK の `/` の順）に「語＋送り仮名＋付いた助詞」、最後に ひらがな・カタカナ。
- 利用者の操作: Space・↓ で次の候補（3 つ目から候補の窓）、↑ で前、← → で注目の文節の移動、Shift+← → で注目の文節の境界の伸縮
  （伸縮した文節はその長さで引き直す）、Enter で全て確定、Esc で変換の前（かな）に戻す、BackSpace で最後の文字を消す（変換中なら変換をやめて preedit に戻す）、
  F6・F7・F8（ひらがな・カタカナ・半角カタカナ、Windows と同じ）、F9・F10（全角・半角の英字）。
- 文節ごとの確定の順（左から）は preedit の中で下線の太さで見せる（text-input-v3 の preedit は 1 本の下線と cursor の範囲しか持たないので、
  注目の文節を **cursor の begin・end の範囲**で示す。app の側は その範囲を濃く描く、§11）。

#### 7.2.1 活用の語尾の表

okuri の子音ごとではなく、「送りの最初のかな」から始まる語尾の短い表を持つ（ja-segment.c の定数、数十の項）: `る・た・て・ない・ます・ました・ません・
ましょう・れば・よう・られる・させる・たい・たく・た・だ・ん・く・き・け・こ・か・い・かった・くて・ければ・さ・そう` 等を、五段・一段・カ変・サ変・
形容詞の語尾から作る。REmacs の辞書の元（`tools/skkdict/src/00-verbs-godan.tsv`・`01-verbs-ichidan.tsv`・`02-adjectives.tsv`）が動詞の活用の種類を
持つので、表は それらの語尾の生成（`tools/skkdict/expand.py`）に合わせる。音便（書いた・読んだ・行った）は SKK の見出しの形（`かi`・`よn`・`いt`）が
辞書にあればそのまま引け、無ければ不明になる（p003 で主な動詞を確かめ、足りなければ補いの辞書、§9.2）。

### 7.3 辞書の引き

- 起動の時に辞書を読み、見出し（UTF-8 の読み、okuri-ari は末尾の ASCII の子音）→ 候補の列の表を作る（okuri-ari・okuri-nasi を分ける）。
  `SKK-JISYO.X` は 16,412 見出し・387 KB で、全体を memory に置いてよい（hash の表、数 MB 以内）。
- 引きは「読みの完全一致」と「前方一致の最長」（分割の動的計画法の中で、位置 i から始まる全ての見出しを列挙するため、読みの先頭の文字で分けた表）。
- 候補の注釈（SKK の `;` の後）は捨てる。

### 7.4 利用者の辞書（学習）

- `~/.config/kei/ime/ja-user.dict`（SKK の形式）に、確定した候補を先頭に置いた見出しを書く（SKK と同じ「最後に選んだものが先」）。
  書き込みは確定のたびに一時 file と rename。system の辞書より先に引く。
- 単語の登録（辞書に無い読みの登録の UI）は最初の版では作らない（§14 D4）。

### 7.5 範囲の外（最初の版）

予測変換、かな入力、全角英数の変換の既定、学習の頻度、文節の区切りの学習。

## 8. 言語の表示と切り替えの指示（私的な拡張 `keiland_ime_status_v1`）

- IME → zdesktop: `language(id, label)`（今の言語。label は "A"・"あ"）と `languages(list)`（切り替えの順）。
- zdesktop → IME: `select(id)`・`next()`（切り替えの key・indicator の click）。
- zdesktop は top bar の右の status（network・battery の並び）に indicator（"A"／"あ"、既定の font、glass の chip）を出し、click で次の言語。
  text input が有効でない時は薄く出す。IME が居ない時は出さない。
- 名前は Kei の規則どおり画面に Keiland を出さない（indicator の tooltip・Settings の表示は「Input: Japanese」「Input: English」）。

## 9. 辞書の置き場所と image への入れ方

### 9.1 system の辞書

- 元: REmacs の source の `dict/SKK-JISYO.X`（`SKK-JISYO.remacs` を全て含む。確かめた: remacs の見出しは全て X にある）。remacs の package が
  取得する同じ git の revision（今 `1a72439`）を pin し、IME の package の build で `build/sources/remacs/dict/SKK-JISYO.X` から image の
  `/usr/share/kei/ime/ja/SKK-JISYO.X` に入れる（`ZEDBSD_USERLAND_PACKAGE` の DATA）。remacs の package を選ばない image でも、IME の package が
  同じ取得の規則（`userland/download.mk`）で source を取る。
- license: 辞書の header は「remacs と同じ license（GPL）」だが、ユーザーが著作権者で Kei の IME に使うことを許可（2026-09-29、ws.md）。
  image の `/usr/share/kei/ime/ja/LICENSE` に その許可の文（著作権者による Kei での利用の許可）を置く（§14 D1）。
- 形式はそのまま（SKK、UTF-8）。起動の時に読むので build の変換は要らない。

### 9.2 足りない分（補いの辞書）

REmacs の辞書は作者の文章の語彙が中心で、日常の基本の語に欠けがある（確かめた例: `わたし` → 私 が無い（`わたし /渡し/渡/` だけ）、
`にほん` → 日本 が無い（`にほんご` 等の複合語だけ）、`ありがとう` が無い）。代名詞・基本の名詞・挨拶・助数詞・音便の動詞の見出しを集めた
**補いの辞書** `SKK-JISYO.kei`（数百〜千語、この project で書き下ろす original の work、zlib）を作り、X の前に引く。語の選び方と量はユーザーと相談
（ユーザーの指示「足りない分は要相談」、§14 D3）。p003 で「日常の文 100 文を変換して、1 回目の候補が正しい割合」を計り、欠けの一覧を出して相談する。

## 10. 切り替えの key

### 10.1 既定

| key | 動作 | 理由 |
| --- | --- | --- |
| **Super+Space** | 次の言語（direct → ja → direct） | macOS の Ctrl+Space、Windows の Win+Space、GNOME の Super+Space と同じ系統。zdesktop は Super+Space を使っていない（Super+Tab・Super+L だけ） |
| **半角/全角**（JIS の keyboard） | direct ⇄ ja の切り替え | Windows の日本語の既定 |
| **変換**（Henkan、evdev 92） | ja にする | Windows 10 以降の IME の設定「無変換・変換で オフ・オン」 |
| **無変換**（Muhenkan、evdev 94） | direct にする | 同上 |
| **かな**（LANG1、evdev 122）・**英数**（LANG2、evdev 123） | ja・direct にする | Mac の JIS の keyboard と同じ |

- 切り替えの key は zdesktop が取る（§4.2）ので、どの app でも同じに効き、IME が変換中でも効く（切り替えの時は未確定の文字を確定する。§14 D6）。
- Ctrl+Space は使わない（Emacs の mark、Terminal の NUL と衝突するため）。

### 10.2 USB の key の code

USB HID の Japanese の key は kernel で evdev の code に写る（`src/drivers/usb/usb-hid.c`: International 4・5 → KEY_HENKAN・KEY_MUHENKAN、
LANG1・LANG2 → KEY_HANGEUL・KEY_HANJA、LANG5 → KEY_ZENKAKUHANKAKU）。PS/2（`ps2-8042.c`）には Japanese の key の写しが無い（p003 で必要なら足す、
それは kernel の driver の変更で WS095 の範囲に入れるか main と相談）。

### 10.3 JIS の keyboard の 半角/全角

USB の JIS の keyboard の 半角/全角 は HID の usage 0x35（US の `` ` `` の位置）を送り、kernel では KEY_GRAVE になる。今の zdesktop の keymap は US だけ
（`keymap.c`）で、配列の選択（US・JIS）が無い。JIS の配列を選べる設定ができるまで、KEY_GRAVE を 半角/全角 と見なすのは US の keyboard の `` ` `` を奪うので
しない。**JIS の配列の設定**（Settings・keymap・Terminal と Text Editor の `keys.c`）は別の WS にすることを提案する（§14 D7）。デモの機械（5330）の
keyboard の配列の確認が要る。

## 11. app の側（text-input-v3 の対応）

共通の helper を libkeiland に置く（`userland/desktop/libkeiland/text-input.c`、`keiland_text_input_*`）: focus の enter・leave で enable・disable、
cursor の矩形・content の種類・surrounding の送り、preedit・commit・delete_surrounding の受け取りを app の callback にまとめる。app ごとの結線:

| app | 入力の口 | preedit の描き方 | cursor の矩形 | 備考 |
| --- | --- | --- | --- | --- |
| Terminal（`terminal/window.c`） | commit を UTF-8 のまま pty へ（`terminal_window_type()` と同じ道） | cursor の cell から下線付きで重ねて描く（画面の cell は変えない） | cursor の cell | surrounding は送らない（端末は知らない）。全角の 2 cell は既存の幅の計算 |
| Text Editor（WS092 `textedit`） | `te_edit_insert_text(app, utf8, length)` | `app->preedit`（WS092 の設計 §12 で用意済み） | `te_layout_cursor_rect` | surrounding は cursor の行 |
| Browser の text field（`browser/page/input.c`・`dom/control.c`） | 注目の input・textarea の値に挿入（今の key の文字の挿入と同じ関数） | field の中に下線付き | field の cursor の位置 | `type=password` は content purpose を password に |
| zdesktop の自前の field（App Home の検索、titlebar の検索・breadcrumb。Settings・Files の検索はこれ） | §4.4 の内部の text input | field の描画に下線付き | field の cursor | 1 か所で全ての自前の field に効く |
| Files の自前の field（`files/ui-field.c`・rename） | field の文字列に挿入 | 下線付き | cursor | — |
| Notes | 対象外（pen の app、text の入力が無い） | — | — | — |

- 他のエージェントが Terminal・Browser・Text Editor・zdesktop を変えているので、app の結線は各 app の Phase に分け、その WS の担当と merge の順を
  main が調整する（p005 以降、§13）。

## 12. 試験

- **host**（Linux で build、`plan/ws095/tests/`）: ローマ字の表（全ての規則、`n`・`っ` の境界、未確定の残り）、辞書の読み込み（X の全ての行を読み、
  見出しの数 16,412 と一致）、分割（例文の表: 「わたしはにほんごをはなします」→ 私 は 日本語 を 話します（補いの辞書あり）、「きょうはいいてんきです」、
  「ほんをよんだ」、「たべたい」等、期待の分割と 1 番目の候補）、利用者の辞書の往復、engine の状態機械（key の列 → 出力の列の golden）。
  ASan・UBSan。
- **guest**（Venus の desktop の image、QEMU）: IME の起動と `KEI-IME READY`、probe の client（text-input-v3 の最小の client、`userland/base/tests/ime-probe`）で
  QMP の key の列を送り、client が受けた preedit・commit を log で確かめる（Super+Space の切り替え、変換、確定、Esc、password の field で素通し）。
  候補の窓と indicator は画面を撮って目で確かめる。key の往復の時間（直接入力の遅延）を計る。
- 実機（5330）: JIS・US の keyboard での切り替えの key。

## 13. Phase（案）

| Phase | 目的 | 主な変更 | 依存 |
| --- | --- | --- | --- |
| ws095-p001 | 設計（この文書） | plan だけ | — |
| ws095-p002 | 日本語の engine（Wayland 無し）: ローマ字・辞書・分割・候補・利用者の辞書、host の試験 | `userland/desktop/ime/` の engine の file、`plan/ws095/tests/` | p001 |
| ws095-p003 | 辞書の package（X の取得と install、LICENSE）、変換の品質の計測と補いの辞書の案（ユーザーと相談） | ime の Makefile、`SKK-JISYO.kei` の案 | p002、§14 D1・D3 |
| ws095-p004 | protocol の client の記述（text-input-v3・input-method-v2・virtual-keyboard-v1・status）と zdesktop の仲介（§4）、IME の program（直接入力の engine と日本語の engine の結線、候補の窓、indicator）、ime-probe の guest の試験 | libwayland、`wayland/text-input.c`・`input-method.c`・`seat.c`・`protocol.c`・`panels.c`、ime の Wayland の部分 | p002。zdesktop を変えている WS035 と merge の順の調整（main） |
| ws095-p005 | libkeiland の text-input の helper と Terminal の対応 | libkeiland、terminal | p004 |
| ws095-p006 | Text Editor の対応（WS092 の口） | textedit | p004・p005、WS092 の完了 |
| ws095-p007 | zdesktop の自前の field（App Home・titlebar の検索）と Files の field | wayland の home・titlebar-shell、files | p004 |
| ws095-p008 | Browser の text field | browser | p004・p005 |
| ws095-p009 | 全体の規約の適合（coding-style の全文）、guest の回帰、実機の確認 | — | p002〜p008 |

## 14. 人間の判断が要る点（既定を選んで進める）

| # | 問い | 既定（この設計が選んだもの） | 別の案 |
| --- | --- | --- | --- |
| D1 | 辞書をどう image に入れるか・license の表示 | REmacs の source（pin した revision）から build の時に `SKK-JISYO.X` を取り image に入れる。tree には取り込まない。image に著作権者の許可の文を置く | 辞書を tree（`userland/desktop/ime/dict/`）に取り込み、header の license の行をユーザーが Kei 用に書き換える |
| D2 | 変換の起点の入れ方 | Space で「今の未確定のかな全体」を変換（MS-IME・Google 日本語入力の型）。SKK の大文字の起点は使わない | SKK の型（大文字で変換の始まり・送りの始まりを示す）。REmacs と同じ操作になるが、一般の利用者には馴染みが薄い |
| D3 | 足りない語の補い | この project で書き下ろす補いの辞書 `SKK-JISYO.kei`（代名詞・基本の名詞・挨拶・音便の動詞等、数百〜千語）。語の量と範囲はユーザーと相談 | REmacs の辞書そのものに足す（ユーザーの repo）／SKK-JISYO.L 等の外部の辞書を任意で入れる（GPL、既定では入れない） |
| D4 | 単語の登録 | 最初の版では作らない（学習は候補の順だけ） | 変換で見つからない時に登録の小窓（SKK の再帰の登録） |
| D5 | 直接入力の時も key を IME に通すか | 通す（input-method-v2 の標準の形、IME が単一の場所で切り替えを管理） | 直接入力の時は zdesktop が grab を飛ばす（遅延ゼロ、IME の状態との整合の管理が増える） |
| D6 | 切り替えの時の未確定の文字 | 確定する（Windows と同じ） | 破棄する |
| D7 | JIS の keyboard の配列（半角/全角 が KEY_GRAVE、¥・ろ の key） | WS095 では扱わず、別の WS（keyboard の配列の設定）を提案。それまで 半角/全角 は LANG5・変換・無変換・Super+Space で代える | WS095 で JIS の配列の選択まで作る |
| D8 | IME の program の名前 | `/usr/libexec/keiland-ime`（内部の名前、画面には出さない） | `/bin/ime` など |
| D9 | 候補の窓の見た目 | wl_shm で Kei の見た目（白の card、角丸、選んだ行を強調、番号 1〜9 で選べる）。glass の効果は付けない | glass（zdesktop の glass の拡張を popup に広げる） |
