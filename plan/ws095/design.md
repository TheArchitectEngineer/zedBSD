<!-- awesome-plan project=zedbsd record=ws095-design -->

# WS095 の設計: IME（Wayland の標準の方法、まず日本語）

2026-09-29 ws095-p001。目標は [ws.md](ws.md) のユーザーの要望（原文）。この文書は設計だけで、source は変えない（他のエージェントが
wayland・terminal・browser・textedit を変えている最中のため）。人間の判断が要る点は §14 に既定と一緒に挙げる。
2026-09-29 に design-reviewer の敵対的なレビュー（指摘 45 件、うち高 9）を受け、source で確かめて反映した（反映の記録は
[phase001/phase.md](phase001/phase.md) の「レビューの反映」）。本文の「(R: A1)」等はレビューの指摘の番号。

## 1. 範囲

- 範囲: system の既定の IME を 1 つ（program 1 つ）入れる。IME は複数の「入力の言語」（input source）を持ち、切り替えられる。最初の言語は
  **直接入力（英数）** と **日本語**。日本語は ローマ字 → ひらがな → 変換（名詞＋助詞・動詞＋送り仮名 程度の簡単な分割）→ 候補 → 確定。
  辞書は REmacs の辞書（SKK の形式）を土台にする。
- Wayland の標準の方法: app は `zwp_text_input_v3`（wayland-protocols の text-input-unstable-v3）、IME は `zwp_input_method_v2`
  （input-method-unstable-v2。wayland-protocols ではなく wlroots・KWin・fcitx5・squeekboard が使う事実上の標準）と
  `zwp_virtual_keyboard_v1`（virtual-keyboard-unstable-v1、同じく wlroots の系統）。compositor（zdesktop、`userland/desktop/wayland`）が両方を仲介する。
- 範囲外（後の WS・判断待ち）: 日本語以外の言語（仕組みだけ複数言語の前提で作る）、学習の高度化（頻度の統計）、単語の登録の UI（§7.5）、
  X11 の app（XIM。`keiland-x11` の下の zterm・zgears は対象外）、JIS の配列の選択（§10.3）、予測の変換・かな入力（親指・JIS かな）、
  絵文字・記号の palette、Browser の DOM の composition の event（§11）、**App Home の検索**（`home_query[48]` が ASCII 前提で keyword も英語、
  `home.c:569-573` の BackSpace が 1 byte ずつ消す。IME の価値が低いので最初の版では Home の表示中は IME を止める。R: E4）。

## 2. 全体の構成

```
 app（Terminal・Text Editor・Browser…）            IME（/usr/libexec/keiland-ime、1 つ）
   zwp_text_input_v3  ──┐                  ┌── zwp_input_method_v2
                        │   zdesktop       │     ├ keyboard_grab_v2（key を受ける）
   compositor の自前の   ├── (仲介・位置・  ──┤     ├ input_popup_surface_v2（候補の窓）
   text field（検索など）┘    focus・key)    └── zwp_virtual_keyboard_v1（IME が使わない key を戻す）
                                               keiland_ime_status_v1（表示の状態、私的な拡張）
```

### 2.1 IME の起動と信頼（R: A5・A6・H3）

- zdesktop は session の開始（`main.c` の初期化の後）に IME を起こす。`zwl_spawn()`（`home.c:763-803`）は `/bin/sh -c` で fd 3〜1023 を閉じるので
  使わず、IME 専用の spawn（`input-method.c`）を作る: `socketpair(AF_UNIX, SOCK_STREAM)` の片方を zdesktop の client として登録し、もう片方を
  IME の子に渡して環境変数 `WAYLAND_SOCKET=<fd>` で知らせる（libwayland の client は対応済み、`libwayland/client.c:42-54`）。直接 `execve` し、sh を挟まない。
- その接続の `zwl_client` に「system の IME」の印を付ける（`zwl.h` の `zwl_client` に 1 つ field を足す）。pid の照合（SO_PEERCRED）は使わない
  （sh の exec の有無・pid の再利用に依存しないため）。
- IME の側の protocol の global（`zwp_input_method_manager_v2`・`zwp_virtual_keyboard_manager_v1`・`keiland_ime_status_manager_v1`）は、印の付いた
  client にだけ registry で見せる。`globals[]`（`protocol.c:41-58`）は全 client 共通の静的な表なので、`registry_events`（349-）と `bind_global`（481-）に
  client ごとの filter を 1 つ足す。他の client が名前を当てて bind したら protocol error（`unauthorized` は global を隠すので実際には使われない、R: H4）。
- 死亡の検出は **接続の切断**（client の破棄の処理の中）で行う。子の回収は `zwl_home_tick` の `waitpid(-1)`（glass の時しか呼ばれない、
  `shell.c:1545`）に頼らず、IME の spawn の側で SIGCHLD か main loop の tick で回収する（p004 で既存の回収との二重の `waitpid` を避ける形を確かめる）。
- 再起動は時間窓で制限する: 60 秒の間に 3 回まで起こし直し、超えたら諦めて indicator を出さず（§8）、zdesktop の log に 1 行残す。
- IME が死んだ・ハングした時の後始末は §4.5。

## 3. protocol（client の library と compositor の両方）

text-input-unstable-v3・input-method-unstable-v2・virtual-keyboard-unstable-v1 の XML を pin し（入手元・revision・SHA-256 を
`include/libc/wayland/API-PROVENANCE.md` に記録）、今までと同じく **手で書いた記述**にする（scanner の出力は入れない。XML も tree には入れない）。

### 3.1 入手元と pin（2026-09-29 に確かめた）

| XML | 入手元（pin） | SHA-256 | license | 使う版 |
| --- | --- | --- | --- | --- |
| text-input-unstable-v3.xml | build の host の Debian の package wayland-protocols 1.44-1（`/usr/share/wayland-protocols/unstable/text-input/`）。既存の primary-selection・tablet と同じ入手元 | `49048087a67011a8840bca889cd2b0ba374382be1ed54ec98adf7837fdca1982` | HPND 型（Intel・Red Hat・Purism の permission notice。MIT 系、再配布と改変の自由） | interface の version 1 |
| input-method-unstable-v2.xml | wlroots の git の tag `0.19.2`（commit `a047c2a33ff7724a476892cc4fe5dcb803607ef5`）の `protocol/input-method-unstable-v2.xml` | `99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703` | MIT（Høgsberg・Intel・Collabora・Red Hat・Purism） | version 1 |
| virtual-keyboard-unstable-v1.xml | 同じ wlroots `0.19.2` の `protocol/virtual-keyboard-unstable-v1.xml` | `7ad7870003ecd592cae47dc19d277a609b7f18fd7b7be012623cf3225a7294f5` | MIT（Høgsberg・Intel・Collabora・Purism） | version 1 |

- 確かめた事実: build の host の wayland-protocols 1.44 には input-method の v2 と virtual-keyboard が **無い**（`unstable/input-method/` は
  v1 だけ）。wlroots の XML は gitlab.freedesktop.org の raw（上の commit）から取得して hash を取った（design-reviewer も同じ hash を確かめた）。
  記述は手書きなので build は XML を要らず、API-PROVENANCE.md の記録（URL・commit・hash）で足りる。
- text-input-v3 の version: `build/distfiles/wayland-protocols-1.49.tar.xz`（別の package が取得済み、SHA-256 `ec4c8f74…b14`）の版は
  `zwp_text_input_v3` **version 2**（`action`・`language`・`preedit_hint` の event、`set_available_actions`・`show_input_panel`、
  content_hint の `preedit_shown` 等、そして「text_input.commit の値は次の `wl_surface.commit` で適用」の二段の適用）を持つ。1.44 は version 1 で、
  1.49 の中の v1 の部分は 1.44 と同じ意味。zdesktop は manager を **version 1 で広告**し、v2 の二段の適用（surface の commit との同期）は
  この WS では入れない。`preedit_hint`（注目の文節を示せる）は v2 の利点なので、§7.2 の cursor の範囲で示す方法が足りなければ後で v2 に上げる
  （Future Work の候補）。
- 同じ 1.49 の `experimental/xx-input-method/xx-input-method-v2.xml`（`xx_input_method_v1` version 4、keyboard grab が無く popup の positioner を持つ）と
  `xx-text-input-v3.xml` は、input-method-v2 を wayland-protocols に入れる途中の実験の版で「後方互換の無い変更が予想される、opt-in なしに出すのは
  勧めない」と書かれている。既存の IME（fcitx5・squeekboard 等）と wlroots・KWin が実装する `zwp_input_method_v2` を選び、xx は使わない。

### 3.2 protocol の意味で守る点（XML から確かめた。R: B1・B4）

- **serial は 2 種類ある**: text-input-v3 の `done(serial)` は「その `zwp_text_input_v3` から来た `commit` の request の数」（XML: "The compositor must
  count the number of commit requests coming from each zwp_text_input_v3 object and use the count as the serial in done events"）。input_method_v2 の
  `commit(serial)` は「IME に送った `done` の event の数」。zdesktop は **両方を別々に数え**、app への `done` には text input の commit の数を使う
  （IME の serial をそのまま渡さない）。
- IME の `commit(serial)` が done の数と合わない時も、XML は "must proceed as normal, except it should not change the current state of the
  zwp_input_method_v2 object" なので、commit_string・preedit・delete_surrounding は **app へ渡す**（捨てると速く打った時に文字が欠ける）。
- `set_preedit_string`・`commit_string` の文字列は 4000 byte 以内。IME は長い確定を 4000 byte 未満の code point の境界で分けて複数回 commit する。
  surrounding_text も 4000 byte を超える時は app（helper）が cursor の周りを code point の境界で切る。
- 1 つの seat に 2 つ目の input_method が作られたら、それには `unavailable` だけを送る。
- text input: enter は focus の surface の client の **全ての** text_input に送る。有効（enable → commit）にできるのは seat に 1 つだけで、2 つ目の
  enable は無視する。leave・disable の後の状態は無効（app の helper は leave で preedit を消す、XML "should reset any preedit"）。
- virtual keyboard は **`keymap` の request の前に `key`・`modifiers` を送ると `no_keymap` の protocol error**。IME は grab の `keymap` の event で受けた
  fd をそのまま virtual keyboard の `keymap` で送る（§4.2）。

### 3.3 file

| 置き場所 | 内容 |
| --- | --- |
| `include/libc/wayland/text-input-unstable-v3-client-protocol.h`、`userland/desktop/libwayland/text-input-protocol.c` | app 用: manager・text_input の request と event |
| `include/libc/wayland/input-method-unstable-v2-client-protocol.h`、`userland/desktop/libwayland/input-method-protocol.c` | IME 用: manager・input_method・popup_surface・keyboard_grab |
| `include/libc/wayland/virtual-keyboard-unstable-v1-client-protocol.h`、`userland/desktop/libwayland/virtual-keyboard-protocol.c` | IME 用 |
| `include/libc/wayland/keiland-ime-status-v1-client-protocol.h`、`userland/desktop/libwayland/keiland-ime-status-protocol.c` | 私的な拡張（§8） |
| `userland/desktop/wayland/text-input.c`（新規） | compositor: text_input_v3 の状態（enable・surrounding・content_type・cursor_rectangle・commit の数） |
| `userland/desktop/wayland/input-method.c`（新規） | compositor: input_method_v2 の仲介、keyboard grab、key の宛先の記録、popup の配置、virtual keyboard、IME の起動・監視・watchdog |

使う版: text_input_v3 v1、input_method_v2 v1、virtual_keyboard_v1 v1。`zwl_kind` に
`ZWL_TEXT_INPUT_MANAGER`・`ZWL_TEXT_INPUT`・`ZWL_INPUT_METHOD_MANAGER`・`ZWL_INPUT_METHOD`・`ZWL_INPUT_POPUP`・`ZWL_KEYBOARD_GRAB`・
`ZWL_VIRTUAL_KEYBOARD_MANAGER`・`ZWL_VIRTUAL_KEYBOARD`・`ZWL_IME_STATUS` を足し、`protocol.c` の `globals[]` に空いている名前（13〜15、20〜）で登録する。
WS035 等が並行して global を足すので、名前の番号は merge の時に main が調整する（R: A6・F4）。

## 4. compositor（zdesktop）の仲介

### 4.1 text-input の状態

- 1 つの seat に 1 つの「今の text input」。keyboard の focus の surface の client が持つ `zwp_text_input_v3` に `enter(surface)` を送り、
  focus が外れたら `leave`。client が `enable` → `commit` した時だけ「有効」。
- 有効な text input があると、IME に `activate` と `surrounding_text`・`content_type`・`text_change_cause` を送り `done`。無効・leave で `deactivate`・`done`。
- IME の `commit_string`・`set_preedit_string`・`delete_surrounding_text`・`commit` を、今の text input の client に
  `commit_string`・`preedit_string`・`delete_surrounding_text`・`done(その text input の commit の数)` として渡す（§3.2）。
- **IME を通さない field**（R: B2）: content purpose が `password`（8）・`pin`（9）、または content hint に `hidden_text`（0x40）か
  `sensitive_data`（0x80）がある field。zdesktop はこれらを IME に activate せず（key は直接 client へ、preedit を出さない）、IME の学習にも届かない。
  greeter・lock の画面の password（zdesktop の自前）も同じ。
- **focus の変化と preedit**（R: B3、§14 D10）: XML では leave の後 app は preedit を捨てる。打ちかけの文字を失わないよう、zdesktop は今の text input
  から focus が外れる時、IME が最後に送った preedit が空でなければ、その文字列を旧い text input へ `commit_string` ＋ `done` として送ってから
  `leave` を送り、IME には `deactivate` を送る（IME は deactivate で engine を `reset` し、未確定を捨てる。二重の確定にならない）。
  mouse で cursor を動かした時（`text_change_cause` = other の surrounding が来た時）は、IME が今の preedit を確定してから新しい位置で続ける。

### 4.2 key の流れ（R: A1・A2・A3・A10、§14 D5）

`zwl_seat_key()`（`seat.c:830-880`）の今の順は、DnD の Esc → lock（`zwl_greeter_key`）→ Super+L → greeter → glass の時の
`zwl_home_key`（Home の表示中は全ての key を取る、`home.c:538-548`）→ `zwl_network_key` → `zwl_menu_grab_key` → `zwl_titlebar_key`（field に
keyboard がある時 Alt・Super と一部の Ctrl 以外を取る、`titlebar-shell.c:455-535`）→ `zwl_glass_key` → `zwl_menu_key`（F10・item の shortcut、
`menu-shell.c:702-750`）→ `zwl_titlebar_tab_key` → focus の client。これに次の分岐を足す:

1. **言語の切り替えの key**（§10）は、lock と greeter の判定の直後、`zwl_home_key` より前で zdesktop が取り、IME に `keiland_ime_status` で指示する
   （Home の表示中も効く）。
2. **IME へ送るかの判定**: 次の全てを満たす時だけ key を grab へ送る。IME が grab を持ち、今の text input（または §4.4 の内部の field）が有効で、
   IME を通さない field（§4.1）でなく、**今の言語が直接入力でない**（zdesktop は status の `language` event で今の言語を知っている）。
   直接入力の時は grab を飛ばす（D5 の既定を変えた。IME のハング・遅延が直接入力の文字に影響しないため、R: A4・G2）。
3. **IME へ送る key の位置**: 自前の field（titlebar の検索）が keyboard を持つ時は `zwl_titlebar_key` の文字入力・編集の処理の **前** で grab へ送る
   （そうしないと field が key を取り IME に届かない）。それ以外の text input では:
   - IME が「変換中」（preedit が空でない、status の `composing(1)` の event で知らせる）の間は、`zwl_menu_key`・`zwl_titlebar_tab_key` の
     shortcut より **前** で grab へ送る（変換中の F10・Ctrl+Z が menu や確定済みの文に効かないように。R: A2）。
   - 変換中でない時は今の順（shortcut の後、client の前）で grab へ送る。
   - zdesktop の shortcut（Super+L・Super+Tab 等の Super の組み合わせ）は常に grab より前（変えない）。
4. IME は使わない key（変換中でない時の矢印・Ctrl の組み合わせ・Enter 等）を `zwp_virtual_keyboard_v1.key`・`modifiers` で戻す。
   zdesktop は virtual keyboard の key を **grab を通さずに** focus の client へ送る。変換中でない時に戻った key には menu・tab の shortcut を
   適用する（3 で飛ばした shortcut をここで効かせる。IME が戻さなかった key には適用しない）。
5. **key の宛先の記録**（R: A3）: zdesktop は押下中の key ごとに「press を送った宛先（grab・client）」を持ち、release を同じ宛先へ送る
   （途中で grab が付いた・外れた・言語が変わった時も）。virtual keyboard で press した key も記録し、virtual keyboard の破棄・IME の死・focus の変化・
   grab の終了の時に、その client へ release と modifiers（物理の値）を合成して送る（押されたままの key と client の repeat が残らないように）。
6. **modifiers**: grab の間、focus の client へ送る modifiers は virtual keyboard の `modifiers` の値だけにし（物理の modifiers と混ざって順が狂わない）、
   grab を外す時に物理の値を再送する。virtual keyboard の modifiers で `server->modifiers`（shortcut の判定に使う物理の状態）を書き換えない。
7. virtual keyboard の `keymap` の request（§3.2 のとおり必須）は受けて fd を **すぐ close** し（使わない fd が接続の終わりまで残らないように、
   `wire.c:365-371`・`objects.c:508`）、focus の client への `wl_keyboard.keymap` の再送はせず zdesktop の keymap（`keymap.c`）を使い続ける。
8. key の repeat は今どおり client の側（`wl_keyboard.repeat_info`、25/s・400 ms、`seat.c:85-86`）。grab にも repeat_info を送り、IME は変換中の
   BackSpace・矢印を自分で repeat する。IME は `deactivate` と grab の終わりで全ての repeat を止める。

### 4.3 候補の窓（input popup surface）（R: A7・A9）

- IME は `get_input_popup_surface(wl_surface)` で候補の窓を作る。zdesktop は今の text input の `set_cursor_rectangle`（surface の座標）を
  画面の座標に直し、popup を **cursor の矩形の下の左端**に置く（画面の下に出れば上に、右に出れば左へずらす）。cursor の矩形が無い時は
  text input の surface の左下に置く。IME に `text_input_rectangle` で cursor の矩形（popup の座標系）を送る。
- popup は input_method が active の間だけ見せる（XML: "visible if and only if the input method is in the active state"）。全ての窓の上（menu と同じ層）に
  描き、focus・pointer の入力を取らず、hit test から外す（候補の click は後で。最初は key だけ）。glass の窓の効果は付けない。
- 合成: plain の経路（`compose.c:1483-1495` の `zwl_popup_draw` の並び）と glass の経路（`zwl_glass_draw`、fence の保持 `compose.c:235-276`）の両方に
  IME の popup を入れ、damage と frame callback を扱う。全画面の direct scanout（`display.c:398-414`）は popup が見えている間は使わない。
- lock・greeter・App Home・Wiseview の間は IME を `deactivate` し popup を隠す（lock の画面に候補が出ない、lock 中に IME の repeat が commit しない）。

### 4.4 zdesktop の自前の text field（R: A8）

titlebar の検索の field と breadcrumb（`titlebar-shell.c`。Settings・Files の検索はここ）は zdesktop が描く field（App Home は §1 のとおり範囲外）。
IME の側からは普通の text input と同じに見えるよう、zdesktop の中に「内部の text input」を 1 つ置く:
field が keyboard を取った時、client の text input が有効でも IME の activate を **内部の field に移す**（client の text input には preedit の確定の
扱い（§4.1）をしてから、IME へ deactivate → activate(field)）。field が終わったら client の text input を再び activate する。
IME の commit・preedit を field の文字列と preedit の表示に適用し、field の描画に「下線付きの preedit」を足す。field の文字列の編集は UTF-8 の
code point の単位にする（BackSpace が 1 byte ずつ消さないことを p008 で確かめる）。

### 4.5 IME の失敗の時（R: A3・A4）

- **watchdog**: grab に送った key に対して IME から何の応答（virtual keyboard の key・commit・status）も 500 ms 無ければ、zdesktop は grab を外して
  素通しに戻り（以後の key は直接 client へ）、log に 1 行残す。IME が応答を再開したら grab を戻す。ping は toplevel 用しか無く（`zwl.h:484-486`）
  IME は toplevel を持たないので、この watchdog が要る。
- **IME の死**: 接続の切断で、押下中の key の release を合成し（§4.2 の 5）、今の text input の client に空の preedit と `done` を送り
  （app に preedit が残らないように）、popup を消し、§2.1 の制限の中で起こし直す。
- zdesktop・IME の log に key・preedit・確定した文字列を出さない（R: H2）。

## 5. IME の program（`/usr/libexec/keiland-ime`）

- 置き場所 `userland/desktop/ime/`（新規の package `ime`、MENU は desktop、既定で選ぶ）。C、coding-style の全文。Wayland と描画の部分と、
  言語の engine（Wayland を知らない）を分ける:

| file | 役割 |
| --- | --- |
| `main.c` | 接続（`WAYLAND_SOCKET`）、globals、event の loop、設定の読み込み |
| `method.c` | input_method_v2 の状態機械（activate・deactivate・surrounding・done の数）、keyboard grab、virtual keyboard での戻し、repeat |
| `keys.c` | evdev の code → 文字（US の配列と Shift。Terminal の `keys.c` と同じ表）、modifier の追跡 |
| `engine.h` | 言語の engine の interface（§6） |
| `engine-direct.c` | 直接入力（全ての key を戻す。zdesktop が grab を飛ばすので通常は使われないが、状態の整合のために持つ） |
| `ja-engine.c` | 日本語の engine: 状態（入力中・変換中）、key の解釈 |
| `ja-romaji.c` | ローマ字 → かな（§7.1） |
| `ja-dict.c` | 辞書の読み込み・引き（§7.3、§9） |
| `ja-segment.c` | 文節の分割と候補（§7.2） |
| `ja-inflect.c` | 活用の規則（送りの子音からの活用形・音便の生成、§7.2.1） |
| `ja-user.c` | 利用者の辞書（選んだ候補の順の学習、§7.4） |
| `popup.c` | 候補の窓の描画（wl_shm、libtruetype と `keiland-fallback.ttf`（Droid Sans Fallback）の日本語の glyph） |
| `status.c` | `keiland_ime_status_v1`（今の言語・変換中の表示、切り替えの指示） |

- engine は Wayland を知らない純粋な C にし、host（Linux）で試験できるようにする（§12）。
- 設定: `/etc/kei/ime.conf`（system の既定: 有効な言語の順、切り替えの key）と `~/.config/kei/ime.conf`（利用者の上書き）。最初の版は
  言語の順 `direct ja` と、起動の時の言語 `direct` だけ。
- 言語の状態は **system 全体で 1 つ**（窓ごとに持たない。macOS・Windows の既定と同じ。§14 D12）。

## 6. 言語の engine の interface（複数言語の前提）

```c
struct ime_engine_ops {
	const char *id;            /* "direct"、"ja" */
	const char *label;         /* 表示の短い名前: "A"、"あ" */
	int (*key)(struct ime_engine *engine, const struct ime_key *key, struct ime_output *out);
	void (*reset)(struct ime_engine *engine, struct ime_output *out);   /* 未確定を確定（out に出す）か破棄（out を空に） */
	void (*surrounding)(struct ime_engine *engine, const char *text, uint32_t cursor, uint32_t anchor);
};
```

`ime_output` は「確定する文字列」「preedit と cursor・下線の範囲」「候補の一覧と選んだ番号」「key を戻すか」「変換中か」を持つ。`method.c` がこれを
input_method_v2 の request（commit_string・set_preedit_string・delete_surrounding_text・commit）と popup の描画と status の `composing` に変える。
`reset` は言語の切り替え（確定、D6）と deactivate（破棄。§4.1 で zdesktop が既に確定を送っている）で使い分ける。
言語を足す時は engine を 1 つ足し、`ime.conf` の順に入れるだけにする。

## 7. 日本語の engine

### 7.1 ローマ字 → かな（R: C5）

- REmacs の `editor/skk.noct` の `skkInitRomaji()` の表（`skk.noct:110-155`、訓令式・ヘボン式、拗音、`xa`・`la` の小書き、`xtu`）を土台に C の表にし、
  REmacs の表に無い一般の綴りを足す: `ca・ci・cu・ce・co`、`qa・qi・qe・qo`、`she・je・che`、`thi・dhi`、`tsa`、`ltu`、`lya・lyu・lyo`、`xwa`、
  `kwa`、`tch`（`matcha` → まっちゃ）、`n'`（`kan'i` → かんい）。
- `nn` → ん、子音の後の `n` → ん、同じ子音の重ね → っ（`skkComposeChar()` と同じ）。`nn` 規則のため `konnichiha` は こんいちは になる（SKK・
  MS-IME と同じ。`konnnichiha` と打つ）。
- **どの規則にも合わない文字は捨てない**（`skkComposeChar` は黙って捨てる、`skk.noct:183-186`）。合わない英字は preedit に英字のまま残す。
- 記号: `-` → ー、`,` → 、、`.` → 。、`[` → 「、`]` → 」、`/` → ・（MS-IME・Google 日本語入力の既定と同じ）。
- 数字・その他の記号・空白（§14 D11）: 数字と上に無い記号は **半角のまま** preedit に入る（F9 で全角）。preedit が空の時の Space は半角の空白を
  戻す（Terminal・code の入力を考えた既定）。
- Shift を押した大文字: その文字から、次の変換・確定まで英字のまま preedit に入る（MS-IME の一時的な英字）。SKK と違い大文字は変換の起点に
  しない（§14 D2）。
- 入力中の preedit は「確定したかな＋未確定のローマ字」（例 `かんじy`）。

### 7.2 変換: 文節の分割（R: C1・C3・C4）

入力された ひらがな の列（1 回の Space で変換する範囲）を、簡単な規則で文節に分ける。形態素解析はしない（ユーザーの指示「非常に簡単でよく、
名詞＋助詞、動詞＋送り仮名、くらいの分解でよい」）。

- 語の種類:
  - **名詞**（体言）: 辞書の okuri-nasi の見出し（例 `にほんご` → 日本語）。
  - **動詞・形容詞**＋送り仮名: 辞書の okuri-ari の見出し（語幹＋送りの子音、例 `かk` → 書）と、§7.2.1 の活用の規則で作った送り仮名。
  - **助詞・助動詞**: 固定の表。単独の助詞（は・が・を・に・へ・と・で・も・の・や・か・ね・よ・な・ば・から・まで・より・けど・ので・のに・だけ・
    しか・って・たら・ても・など）、その連接（には・では・へは・への・とは・までに・からは・にも・でも・とも・のは・のが）、助動詞（です・でした・
    でしょう・だ・だった・ます・ました・ません・ませんでした・ましょう）。かなのまま。名詞・動詞・形容詞・**不明**の文節の後に付く
    （「ぱそこんを」→ パソコン＋を）。
  - **不明**: 辞書に無いかなの並び。**連続する最長の並びを 1 つの不明の文節**にする（1 文字ずつに分けない）。候補は ひらがな → カタカナの順。
- 分割: 左から動的計画法で、費用が最小の並びを選ぶ。費用は次の順に比べる（辞書式）:
  1. **不明の文字の数**（少ない方）。
  2. **1 文字の名詞の数**（少ない方。`い`→胃・`き`→木 等、X に 1 文字の okuri-nasi の見出しが 54 あり、仮名を吸い込むため）。
  3. **文節の数**（少ない方）。
  4. 辞書の語の長さの合計（長い方）。
  旧い費用（文節の数を先にした）では入力全体を 1 つの不明の文節にするのが常に勝つ誤りがあった（R: C1 の反例「ほんをよんだ」）。
  この費用で §12 の例文が期待どおりに分かれることは p002 の試験で確かめ、崩れる例文があれば費用を例文の集合に合わせて直す（費用の定義は
  engine の内部の技術的な選択）。
- 各文節の候補は、辞書の順（SKK の `/` の順、利用者の辞書が先）に「語＋送り仮名＋付いた助詞」、最後に ひらがな・カタカナ。
- 利用者の操作（§14 D2）: Space・↓ で次の候補（3 つ目から候補の窓）、↑ で前、← → で注目の文節の移動、Shift+← → で注目の文節の境界の伸縮
  （伸縮した文節はその長さで引き直す）、Enter で全て確定、Esc で変換の前（かな）に戻す、BackSpace で最後の文字を消す（変換中なら変換をやめて
  preedit に戻す）、F6・F7・F8（ひらがな・カタカナ・半角カタカナ、Windows と同じ）、F9・F10（全角・半角の英字）。F10 は変換中は §4.2 の 3 で
  IME に届く（menu を開かない）。
- 注目の文節は text-input-v3 の preedit の **cursor の begin・end の範囲**で示す（v1 は 1 本の下線と cursor の範囲しか持たない。app の側は その範囲を
  濃く描く、§11）。

#### 7.2.1 活用の規則（R: C2・C3）

確かめた事実: `SKK-JISYO.X` の okuri-ari の見出しは **辞書形の送り仮名からだけ** 作られている（REmacs の `tools/skkdict/expand.py` の `parse_row`:
語幹＋辞書形の送りの最初のかなの子音。い形容詞は `i` と `k` の両方、`たかi`・`たかk` → 高）。TSV の大半（約 4,100 語）は `W 読み 表記` だけで活用の
種類を持たない。そのため `かi`・`よn`・`いt`（音便）、`みm`（見ます）、`いい` は X に無い。見出しの末尾は子音だけでなく母音もある
（`かe`・`おもi`、`expand.py` の `KANA_CONS`: う→w、あいえお→a i e o、じ・ぢ→j、づ→z、先頭の っ→次のかなの子音）。

そこで IME は辞書の見出しを **活用の語幹** とみなし、送りの子音（末尾の文字）から活用形を **規則で作る**（`ja-inflect.c`）:

- 五段（送りの子音 k・g・s・t・n・b・m・r・w）: 未然（か・が・さ・た・な・ば・ま・ら・わ）・連用（き・ぎ・し・ち・に・び・み・り・い）・終止・仮定・
  意志と、**音便**: k → い（書いた・書いて）、g → い＋だ・で（泳いだ）、m・b・n → ん＋だ・で（読んだ・飛んだ・死んだ）、t・r・w → っ（持った・
  取った・買った）、s → し（話した）。例外 `いk`（行く → 行った）。
- `r` の見出しは **五段と一段の両方の仮説**を出す: 一段は語幹の直後に語尾が付く（`みr` → 見＋ない・ます・た・て・る・れば・よう・られる・させる）。
  どちらの仮説でも語尾の automaton（下）に合う方を候補にする。
- い形容詞（`k`・`i` の見出し）: く・かった・くて・ければ・い・さ・そう。
- 母音の見出し（`かe`・`あe` 等）: 送りの最初のかなの母音が見出しの末尾と合う時だけ、そのかなを含む送りとして一段の語尾をつなげる。
- **語尾は小さな automaton で連ねる**（最長一致の 1 つではなく）: 連用形 → て・た・ます・ました・ません・ませんでした・ましょう・たい・たく・
  ながら、て → いる・います・いた・ください、未然形 → ない・なかった・なくて・れる・られる・せる・させる、等。これで「たべています」は
  食べ＋て＋います で 1 文節になる。
- **固定の表**: する（し・します・した・して・しない・すれば・しよう）、くる（来ます・来た・来ない、読みの揺れ き・こ）、いい（良い・いい）、
  ある・いる・なる は辞書に無い・紛らわしい候補（`すr` → 刷・擦・磨 が先に出る等）を避けるため規則より先に引く。名詞＋する（勉強します）は
  名詞の文節の後に する の活用が付く形で 1 文節にする。
- この規則で引けない語（辞書の見出しが無い基本の動詞等）は §9.2 の補いの辞書で足す（p003 で計る）。

### 7.3 辞書の引き

- 起動の時に辞書を読み、見出し（UTF-8 の読み、okuri-ari は末尾の ASCII の文字）→ 候補の列の表を作る（okuri-ari・okuri-nasi を分ける）。
  `SKK-JISYO.X` は 16,412 見出し・387,465 byte（注釈は 0 件）で、全体を memory に置いてよい（hash の表、数 MB 以内）。
- 引きは「読みの完全一致」と「前方一致の全て」（分割の動的計画法の中で、位置 i から始まる全ての見出しを列挙するため、読みの先頭の文字で分けた表）。
- 候補の注釈（SKK の `;` の後）は捨てる。
- 辞書の file の大きさに上限（8 MB）を置き、壊れた行（形式に合わない行）は飛ばす。

### 7.4 利用者の辞書（学習）（R: H2）

- `~/.config/kei/ime/ja-user.dict`（SKK の形式、mode 0600）に、確定した候補を先頭に置いた見出しを書く（SKK と同じ「最後に選んだものが先」）。
  書き込みは確定のたびに一時 file へ書き、`fsync` してから rename（UFS の write cached の既定で crash の後に空の file にならないように）。
  system の辞書より先に引く。読み込みは大きさの上限（1 MB）を置き、壊れた行は飛ばす。
- IME を通さない field（§4.1）は IME に届かないので学習しない。加えて content hint に `sensitive_data` がある時は、IME は学習しない（二重の守り）。
- 単語の登録（辞書に無い読みの登録の UI）は最初の版では作らない（§14 D4）。

### 7.5 範囲の外（最初の版）

予測変換、かな入力、全角英数の変換の既定、学習の頻度、文節の区切りの学習。

## 8. 言語の表示と切り替えの指示（私的な拡張 `keiland_ime_status_v1`）

- IME → zdesktop: `language(id, label)`（今の言語。label は "A"・"あ"）、`languages(list)`（切り替えの順）、`composing(0|1)`（preedit が空でないか。
  §4.2 の 3 の shortcut の順の切り替えに使う）。
- zdesktop → IME: `select(id)`・`next()`（切り替えの key・indicator の click）。
- zdesktop は top bar の右の status（network・battery の並び。`shell.c:1605` の配置と `network.c`、R: A11）に indicator（"A"／"あ"、既定の font、
  glass の chip）を出し、click で次の言語。text input が有効でない時は薄く出す。IME が居ない時（起こし直しを諦めた時を含む）は出さない。
- 名前は Kei の規則どおり画面に Keiland を出さない（indicator の tooltip・Settings の表示は「Input: Japanese」「Input: English」）。

## 9. 辞書の置き場所と image への入れ方

### 9.1 system の辞書（R: G1）

- 確かめた事実: 今の remacs の package は revision を固定していない（`userland/packages/editors/remacs/Makefile:14-16`: `REMACS_GIT_REF ?= main`、
  `git clone --depth 1 --branch main` を共有の `build/sources/remacs` に）。`userland/download.mk` は lifecycle の定義だけで取得の規則を持たない。
  共有の `build/sources/remacs` は main の HEAD に動くので、それを pin とは言えない。
- 設計: IME の package は REmacs の **commit を固定した tarball** を自分で取得・検証する（Guardrail の「tarball を取得・検証」に合わせる）:
  - URL `https://github.com/awemorris/remacs/archive/1a724393053e18c4e1f502ecc5ca8ce07d99287a.tar.gz`
    （2026-09-29 に取得: 597,025 byte、SHA-256 `419d03a195e18875e4761f4d81992905d4130697b72a5fc87849f0228a2f1507`）。
  - GitHub の archive の tarball は再生成で hash が変わりうるので、中の `dict/SKK-JISYO.X` の SHA-256
    `73819384159330a0c822915d0fd211c3e21dea77f2cfd2083273ac1ffd121ab9`（今の共有の `build/sources/remacs` の X と同じ）も確かめる。
  - 取得の先は `build/distfiles/`（共有、消さない）、展開は `$(BUILD)/packages/desktop/ime/` の中。image の `/usr/share/kei/ime/ja/SKK-JISYO.X` に入れる
    （`ZEDBSD_USERLAND_PACKAGE` の DATA）。remacs の package とは独立（remacs を選ばない image でも IME は辞書を持つ）。
- license: 辞書の header は「remacs と同じ license（GPL）」だが、ユーザーが著作権者で Kei の IME に使うことを許可（2026-09-29、ws.md）。
  既定は image の `/usr/share/kei/ime/ja/LICENSE` に その許可の文（著作権者による Kei での利用の許可）を置く。header と LICENSE の文が食い違って
  見えるので、別の案として著作権者が REmacs の辞書の header を書き換える（二重 license 等）方が矛盾が無い（§14 D1）。license の監査の道具
  （`plan/tools/` の audit）での扱いは p003 で main と決める。
- 形式はそのまま（SKK、UTF-8）。起動の時に読むので build の変換は要らない。

### 9.2 足りない分（補いの辞書）

REmacs の辞書は作者の文章の語彙が中心で、日常の基本の語に欠けがある（確かめた例: `わたし` → 私 が無い（`わたし /渡し/渡/` だけ）、
`にほん` → 日本 が無い（`にほんご` 等の複合語だけ）、`ありがとう`・`いい` が無い、1 文字の名詞 `い`→胃 等が先に出る）。代名詞・基本の名詞・挨拶・
助数詞・基本の動詞・形容詞の見出しを集めた **補いの辞書** `SKK-JISYO.kei`（数百〜千語、この project で書き下ろす original の work、zlib）を作り、
X の前に引く。語の選び方と量はユーザーと相談（ユーザーの指示「足りない分は要相談」、§14 D3）。p003 で「日常の文 100 文を変換して、1 回目の候補が
正しい割合」を計り、欠けの一覧を出して相談する。

## 10. 切り替えの key

### 10.1 既定

| key | 動作 | 理由 |
| --- | --- | --- |
| **Super+Space** | 次の言語（direct → ja → direct） | macOS の Ctrl+Space、Windows の Win+Space、GNOME の Super+Space と同じ系統。zdesktop・app は Super+Space を使っていない（Super+Tab・Super+L だけ。app の menu の item は Super を持てる（`menu-shell.c:2099-2100`）が、切り替えは menu より前で取るので衝突しない） |
| **半角/全角**（JIS の keyboard） | direct ⇄ ja の切り替え | Windows の日本語の既定。ただし USB の JIS の keyboard では KEY_GRAVE で来るので §10.3 |
| **変換**（Henkan、evdev 92） | direct の時は ja にする。ja の時は IME に渡す（変換中なら次の候補・再変換） | Windows 10 以降の IME の設定「無変換・変換で オフ・オン」。変換中の 変換 は「変換」の意味を保つ（R: D3） |
| **無変換**（Muhenkan、evdev 94） | direct にする | 同上 |
| **カタカナ/ひらがな**（International2、evdev `KEY_KATAKANAHIRAGANA`） | ja にする | Windows の JIS の keyboard の key（R: D1） |
| **かな**（LANG1、evdev 122）・**英数**（LANG2、evdev 123） | ja・direct にする | Mac の JIS の keyboard と同じ |

- 切り替えの key は zdesktop が取る（§4.2 の 1）ので、どの app でも同じに効き、IME が変換中でも効く（切り替えの時は未確定の文字を確定する。§14 D6）。
- Ctrl+Space は使わない（Emacs の mark、Terminal の NUL と衝突するため）。

### 10.2 key の code

- USB HID の Japanese の key は kernel で evdev の code に写る（`src/drivers/usb/usb-hid.c:2222-2253`: International2 → KEY_KATAKANAHIRAGANA、
  International4・5 → KEY_HENKAN・KEY_MUHENKAN、LANG1・LANG2 → KEY_HANGEUL(122)・KEY_HANJA(123)、LANG5 → KEY_ZENKAKUHANKAKU(85)）。
  LANG5 は実際の JIS の keyboard ではまず送られない（半角/全角 は usage 0x35）。
- PS/2（`src/drivers/platform/pcat/ps2-8042.c:66-121`）の表は 0x44 付近までで、日本語の key（set 1 の 0x70 かな・0x79 変換・0x7b 無変換・0x73 ろ・
  0x7d ¥）の写しが無い。ノート PC の内蔵 keyboard は i8042 経由が多い。デモの機械（5330）の内蔵 keyboard の接続（PS/2 か USB か）と配列は **未確認**。
  PS/2 で JIS なら、写しを足す driver の Phase（ws095-p010、HAL ではない）を 5330 の確認の後に行う（§13）。

### 10.3 JIS の keyboard の 半角/全角

USB の JIS の keyboard の 半角/全角 は HID の usage 0x35（US の `` ` `` の位置）を送り、kernel では KEY_GRAVE になる。今の zdesktop の keymap は US だけ
（`keymap.c`）で、配列の選択（US・JIS）が無い。JIS の配列を選べる設定ができるまで、KEY_GRAVE を 半角/全角 と見なすのは US の keyboard の `` ` `` を奪うので
しない。**JIS の配列の設定**（Settings・keymap・Terminal と Text Editor の `keys.c`）は別の WS にすることを提案する（§14 D7）。それまで JIS の keyboard では
変換・無変換・カタカナ/ひらがな・Super+Space で切り替える。

## 11. app の側（text-input-v3 の対応）

共通の helper を libkeiland に置く（`userland/desktop/libkeiland/text-input.c`、`keiland_text_input_*`）: focus の enter・leave で enable・disable、
cursor の矩形・content の種類・surrounding の送り（4000 byte の制限と code point の境界、§3.2）、preedit・commit・delete_surrounding の受け取りを
app の callback にまとめ、leave で preedit を消す。app ごとの結線:

| app | 入力の口（確かめた関数） | preedit の描き方 | cursor の矩形 | 備考 |
| --- | --- | --- | --- | --- |
| Terminal（`terminal/window.c`） | commit を UTF-8 のまま pty へ（`terminal_window_type()`、ただし 256 byte を超えた分を捨てる `terminal.h:420`・`window.c:388-399` ので分けて送る） | cursor の cell から下線付きで重ねて描く（画面の cell は変えない） | cursor の cell | **前提: 日本語の glyph**。font は `keiland-mono.ttf` の 1 face だけ（`terminal/main.c:46`）で CJK の fallback が無く、確定した文字も preedit も描けない（R: E2）。p006 の前に fallback の font が要る（§13）。**password**: sudo・su・ssh の echo off の prompt（R: H1）は、pty の termios の ECHO が off の間 content hint に `sensitive_data`・`hidden_text` を付ける（§4.1 で IME を通さなくなる）。master の側から termios を読めるかは未確認で p006 で確かめる（読めなければ判断を挙げる） |
| Text Editor（WS092 `textedit`） | `te_edit_insert_text`（WS092 の worktree で引数 4 つ、`edit.c:88-92`） | `app->preedit`（今 `char[64]`、`textedit.h:472`、日本語 21 文字まで。IME の preedit を入れるには大きさを直す） | cursor の矩形（`te_layout_cursor_rect` はまだ無い。WS092 と調整） | surrounding は cursor の行。WS092 の完了か口の確定が前提（R: E3） |
| Browser の text field | `page/form.c` の `page_edit_key`（129）→ `form_insert`（557、static、UTF-8）、caret は `page_place_caret`・`page_paint_caret`（333・450）（R: E1。`page/input.c`・`dom/control.c` には挿入の関数が無い） | field の中に下線付き | field の caret の位置 | caret は UTF-16 の index（`vm/vm.h:67-75`）なので surrounding の byte への変換が要る。`type=password` は content purpose を password に。DOM の composition の event（compositionstart・update・end、keydown の "Process"）は最初の版では出さず、確定を普通の input として入れる（範囲外、§1） |
| zdesktop の自前の field（titlebar の検索・breadcrumb。Settings・Files の検索はこれ） | §4.4 の内部の text input | field の描画に下線付き | field の cursor | 1 か所で自前の field に効く。App Home は範囲外（§1） |
| Files の自前の field（`files/ui-field.c`・rename） | field の文字列に挿入 | 下線付き | cursor | libkeiland の helper を使う（p006 の helper が前提） |
| Notes | 対象外（pen の app、text の入力が無い） | — | — | — |
| Settings の Wi-Fi の key（WS089）・PDF Viewer の password（WS079） | text input を使わないので素通し | — | — | 後で helper を使う時は password の purpose が必須（R: E5） |

- 他のエージェントが Terminal・Browser・Text Editor・zdesktop を変えているので、app の結線は各 app の Phase に分け、その WS の担当と merge の順を
  main が調整する（§13）。

## 12. 試験（R: C6・C7・F1・F2）

- **host**（Linux で build、`plan/ws095/tests/`、ASan・UBSan）:
  - ローマ字の表（全ての規則、`n`・`っ`・`n'`・`tch` の境界、合わない英字が残ること、未確定の残り）。
  - 辞書の読み込み: p002 は **試験用の小さな固定の辞書**（`plan/ws095/tests/` に置く、X の一部の行と補いの行を手で書いたもの）で engine を試す。
    X の全ての行を読んで壊れた行が 0 で、見出しの数が X の中の行の数と一致すること（固定の数 16,412 は pin した X の hash と組にして p003 で確かめる）。
  - 活用の規則: 五段の各行の音便、一段と五段の `r` の両仮説、い形容詞、する・くる・いい、語尾の automaton の連なり（ています・なかった・
    ませんでした）。
  - 分割: 例文の表（「ほんをよんだ」→ 本を｜読んだ、「たべています」→ 食べています、「きょうはいいてんきです」→ 今日は｜いい｜天気です、
    「べんきょうします」→ 勉強します、「ぱそこんを」→ パソコンを、「わたしはにほんごをはなします」→ 私は｜日本語を｜話します（固定の辞書に
    私・日本 を入れて））、期待の分割と 1 番目の候補。
  - 利用者の辞書の往復（壊れた file・大きすぎる file を飛ばす）、engine の状態機械（key の列 → 出力の列の golden）。
- **guest**（Venus の desktop の image、QEMU）: 判定は log に頼らない（guardrail）。probe の client（text-input-v3 の最小の client、
  `userland/base/tests/ime-probe`）が受けた preedit・commit・done の serial を file に書き、SSH（`plan/tools/guest/guest.sh`）で読んで確かめる。
  QMP で key の列を送る（qcode に henkan 等があるかは p004 で確かめる）。確かめる事柄:
  - protocol: done の serial が text input の commit の数と等しい、IME の serial が合わない時も文字が届く、4000 byte と UTF-8 の境界、popup が
    active の間だけ見える。
  - key の経路: Super+Space の切り替え、変換、確定、Esc、password の field で素通し、直接入力で grab を通らない、press の後に grab が付いて release が
    来る、key を押したまま IME を kill しても client の repeat が止まる、IME を STOP（ハング）させて 500 ms 後に素通しになる、変換中の focus の変化で
    preedit が確定される、変換中の lock で候補が出ない。
  - 候補の窓と indicator は画面を撮って目で確かめる（全画面の窓の上でも出ること）。直接入力の遅延が増えないこと。
- **実機**（5330）: JIS・US の keyboard での切り替えの key、内蔵 keyboard の接続。人の作業なので p011 の受け入れとは別に記録する（R: F3）。

## 13. Phase（案）（R: F3 で p004 を分け、依存を直した）

| Phase | 目的 | 主な変更 | 依存 |
| --- | --- | --- | --- |
| ws095-p001 | 設計（この文書） | plan だけ | — |
| ws095-p002 | 日本語の engine（Wayland 無し）: ローマ字・辞書の読み込み・活用の規則・分割・候補・利用者の辞書、試験用の固定の辞書で host の試験 | `userland/desktop/ime/` の engine の file、`plan/ws095/tests/` | p001 |
| ws095-p003 | 辞書の package（pin した tarball の取得・検証と install、LICENSE）、X での変換の品質の計測と補いの辞書の案（ユーザーと相談） | ime の Makefile、`SKK-JISYO.kei` の案 | p002、§14 D1・D3 |
| ws095-p004 | protocol の client の記述（text-input-v3・input-method-v2・virtual-keyboard-v1・status）、zdesktop の仲介（§2.1 の起動と信頼、§4.1・§4.2 の key の経路、§4.5 の watchdog）、IME の program の骨（直接入力の engine）、ime-probe と guest の試験（protocol・key の経路） | libwayland、`wayland/text-input.c`・`input-method.c`・`seat.c`・`protocol.c`・`main.c`・`zwl.h`、ime の Wayland の部分 | p002。zdesktop を変えている WS035 等と merge の順（main） |
| ws095-p005 | 候補の窓の合成（§4.3、plain・glass の両経路、全画面）、indicator と status（§8）、日本語の engine の結線、lock・Home での deactivate、guest の試験（変換・候補の窓の画面） | `wayland/compose.c`・`glass.c`・`display.c`・`damage.c`・`shell.c`・`network.c`、ime の `popup.c`・`status.c` | p003（image に辞書）・p004 |
| ws095-p006 | libkeiland の text-input の helper と Terminal の対応（password の検出を含む） | libkeiland、terminal | p004・p005、**Terminal の CJK の fallback の font**（今は無い。WS095 に入れるか別の WS かを main が決める） |
| ws095-p007 | Text Editor の対応（WS092 の口、preedit の大きさ、cursor の矩形） | textedit | p006、WS092 |
| ws095-p008 | zdesktop の自前の field（titlebar の検索、§4.4）と Files の field | wayland の titlebar-shell、files | p005・p006 |
| ws095-p009 | Browser の text field（`form.c`、UTF-16 の caret の変換） | browser | p006 |
| ws095-p010 | PS/2 の日本語の key の写し（5330 の内蔵 keyboard が PS/2 の JIS と分かった時だけ。driver の変更で HAL ではない） | `src/drivers/platform/pcat/ps2-8042.c` | 5330 の確認（人） |
| ws095-p011 | 全体の規約の適合（coding-style の全文）、guest の回帰。実機の確認は人の作業として別に記録 | — | p002〜p009（p010 は行った時だけ） |

## 14. 人間の判断が要る点（既定を選んで進める）

| # | 問い | 既定（この設計が選んだもの） | 別の案 |
| --- | --- | --- | --- |
| D1 | 辞書をどう image に入れるか・license の表示 | REmacs の commit を固定した tarball から build の時に `SKK-JISYO.X` を取り image に入れる。tree には取り込まない。image に著作権者の許可の文（LICENSE）を置く | (a) 著作権者が REmacs の辞書の header を Kei でも使える license（二重 license 等）に書き換える（header と LICENSE の食い違いが無くなる）／(b) 辞書を tree（`userland/desktop/ime/dict/`）に取り込み header を Kei 用に書き換える |
| D2 | 変換の操作 | Space で「今の未確定のかな全体」を変換（MS-IME・Google 日本語入力の型）。Shift の大文字は一時的な英字。SKK の大文字の起点は使わない | SKK の型（大文字で変換の始まり・送りの始まりを示す。REmacs と同じ操作、分割の誤りが減るが一般の利用者には馴染みが薄い） |
| D3 | 足りない語の補い | この project で書き下ろす補いの辞書 `SKK-JISYO.kei`（代名詞・基本の名詞・挨拶・助数詞・基本の動詞と形容詞、数百〜千語）。p003 で 100 文の計測の後に語の一覧を示して量を決める | REmacs の辞書そのものに足す（ユーザーの repo）／SKK-JISYO.L 等の外部の辞書を任意で入れる（GPL、既定では入れない） |
| D4 | 単語の登録 | 最初の版では作らない（学習は候補の順だけ） | 変換で見つからない時に登録の小窓（SKK の再帰の登録） |
| D5 | 直接入力の時も key を IME に通すか | **通さない**（レビューで変更: zdesktop が status で今の言語を知っているので grab を飛ばす。IME のハング・遅延が英字の入力に影響しない） | 通す（input-method-v2 の素直な形。IME が全ての key を見る） |
| D6 | 言語の切り替えの時の未確定の文字 | 確定する（Windows と同じ） | 破棄する |
| D7 | JIS の keyboard の配列（半角/全角 が KEY_GRAVE、¥・ろ の key） | WS095 では扱わず、別の WS（keyboard の配列の設定）を提案。それまで 変換・無変換・カタカナ/ひらがな・Super+Space で代える | WS095 で JIS の配列の選択まで作る |
| D8 | IME の program の名前 | `/usr/libexec/keiland-ime`（内部の名前、画面には出さない） | `/bin/ime` など |
| D9 | 候補の窓の見た目 | wl_shm で Kei の見た目（白の card、角丸、選んだ行を強調、番号 1〜9 で選べる）。glass の効果は付けない | glass（zdesktop の glass の拡張を popup に広げる） |
| D10 | focus が移る時の未確定の文字（レビューで追加） | zdesktop が旧い field に確定して送る（Windows の IME と同じ。打ちかけを失わない） | 破棄する（text-input-v3 の素直な形） |
| D11 | 日本語の時の数字・記号・空白（レビューで追加） | 数字・記号は半角のまま preedit に（F9 で全角）、preedit が空の Space は半角の空白 | 全角（MS-IME の既定に近い） |
| D12 | 言語の状態を system 全体で 1 つか窓ごとか（レビューで追加） | system 全体で 1 つ | 窓ごと（Windows の旧い既定） |
| D13 | App Home の検索を IME の対象にするか（レビューで追加） | 最初の版では対象外（ASCII・英語の keyword） | UTF-8 に直して対象にする |
| D14 | Terminal の CJK の fallback の font（レビューで見つけた隠れた依存） | p006 の前提として WS095 の中で小さく足す案を main に挙げる（Terminal の担当の WS と調整） | Terminal の WS に任せ、p006 はそれを待つ |
