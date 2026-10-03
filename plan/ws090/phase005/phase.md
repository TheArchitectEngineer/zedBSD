<!-- awesome-plan project=zedbsd record=ws090-p005 -->

# ws090-p005: 部品と見本の program（`/bin/kuidemo`）

Status: cleared（2026-09-30、subagent の worktree `wt/ws090`、main e44f9931 を merge した上）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-30「ws090-p005（部品と見本の program `/bin/kuidemo`）」）
依存: p003（cleared）、p004（cleared）

## 範囲と受け入れ

- libkeiui の部品: button・switch・slider・field・list・sidebar・panel・card・row・header・dialog・chip・progress と keyboard の focus
  （design.md §4）。見た目は Settings・Files の今の見た目（副次の文字の色 `0x56606f` を含む、main の指示）。
- 見本の program `/bin/kuidemo`: **試験の image だけ**（`plan/ws035/tests/config-amd64-zdesktop.mk`）。既定の image（`config/ci/config-amd64.mk`）には入れない。
- 受け入れ: host の試験、QEMU（Venus）で pointer・key・touch の操作、Text Editor の回帰、boot test、規約。

## 実装

### libkeiui（`KUI_VERSION` 4）

- `include/libc/keiui.h`: `struct kui_style`（canvas・text・theme・glass）、`struct kui_field`、`struct kui_list`、`KUI_BUTTON_*`・`KUI_FIELD_*`・
  `KUI_LIST_*`、`KUI_HIT_FOCUSED`、`KUI_EVENT_KEY`（`kui_event` に `code`・`modifiers`）、theme に Settings の値（card・card_edge・row_separator・
  control・control_edge・track・good・bad・control_height・switch の大きさ）。
- `ui.c`: focus（`kui_ui_key`・`kui_ui_set_focus`・`kui_ui_clear_focus`・`kui_ui_has_focus`・`kui_ui_pointer`）、Tab・Shift+Tab は描いた順、
  **key は押された時の focus の部品に宛てる**、押した部品が keyboard を取らなければ下の同じ id の部品（list の行 → list）、`KEIUI_MODAL`（dialog の
  後の部品だけを Tab が回る）、`KEIUI_DRAGGABLE`（slider の drag。放した frame にも held）、frame の終わりに止まった pointer の hover を求め直す、
  focus の枠は Tab で動かした時だけ。内部の API は `internal.h`（`keiui_ui_widget`・`keiui_ui_take_key`・`keiui_ui_take_activate`・`keiui_ui_focused`・
  `keiui_ui_focus_ring`・`keiui_ui_now`・`keiui_button`、`KEIUI_ANY`）。決めたことは design.md §4.1 に記録した。
- `widgets.c`: button（主・危険・無効、32 px、角 8）、switch（44×24、指のために ±4 の余白）、slider（step、矢印・Page・Home・End）。
- `field.c`: 1 行の field（US の配列の文字、Left・Right と Shift、Home・End、Backspace・Delete、Ctrl+A、Enter で submit、Esc で cancel、click・tap で
  caret、double click で全選択、secret は •）。
- `list.c`: list（28 px の行、角 7、focus の時は accent、scroll は `kui_scroll`、矢印・Page・Home・End・Enter、double click で activate）、
  sidebar の section と場所（Files の見た目）。
- `cards.c`: panel（glass の上は veil、無ければ Files の不透明）、card（glass の上は Settings の白い veil、白い panel の上は薄い灰と縁）、row、header、
  dialog（veil、392 px の card、Enter で主、Esc で最後）、chip、progress（割合と、長さの分からない仕事の動く帯）。

### `/bin/kuidemo`（`userland/desktop/kuidemo/main.c`、1 file）

- 3 つの頁: Controls（button 4 種、switch 2、slider と値、field と secret の field、progress）、List（100 行、選択と activate の表示）、
  Dialogs（dialog、chip、switch で動く progress）。glass（`keiland_glass` の 2 枚の panel）、無い時は theme の地。
- 部品の答えを `KUIDEMO` の行で stderr に出す（QEMU の試験は guest の log を SSH で読む）。Ctrl+Q・close・`--timeout-s` で終わる。
- build: `userland/desktop/kuidemo/Makefile`、`platform/amd64/vmunix.mk` の link の規則（Text Editor と同じ形の最小の追加）と basic の
  command の除外の列に `kuidemo`、`plan/ws035/tests/config-amd64-zdesktop.mk` に `kuidemo`（既定の image には無し）。

## 試験

### host（`plan/ws090/tests/host-widgets.sh`、新規）

94/94。全部品の頁を frame ごとに描き、pointer・key・指を与えて答え・focus・app に残る key を確かめる。button（click・無効・Enter・Space・
Ctrl+S は app）、switch、slider（click・drag・frame の間の最後の動きと release・key・Ctrl+矢印は app・1 frame の複数の key）、Tab の順（無効の
button を飛ばし list は 1 つの止まり所、逆回り）、field（文字・Shift+Home・置き換え・Backspace・Delete・Ctrl+A・Enter・Esc・Ctrl+S は app・
**frame の間の「文字・Tab・文字」の宛先**・double click・click の caret）、list（click・矢印・Page Down・End で滑って最後の行・Home・Enter・
double click・wheel）、touch（tap の button・switch・行で list が focus、指の slider の drag）、dialog（focus、Enter・Esc、Tab は dialog の中だけ、
Space、button の click、veil の下は押されない、閉じた後）、見た目（`text_secondary` が `0x56606f`、主の button が accent、field が白、Tab の時だけ
focus の枠）。gallery: `build/ws090-shots/host-widgets-page.png`・`host-widgets-dialog.png`。

### QEMU（Venus、main の `build/main-pen/hdd-image.img` の複写 `build/ws090/pen/`）

この worktree の `kuidemo`・`textedit`・`wayland` と `libkeiui`・`libkeiland`・`libtruetype`・`libwayland-client`・`libvulkan` を置き、zdesktop を
`--glass --wallpaper` で起こした。入力は QMP（`plan/ws035/tests/qmp-pointer.py`・`plan/tools/textedit/qmp-keys.py`）と `/bin/touchinject`、判定は画面
（VNC）と kuidemo・Text Editor の log（SSH）。画面は worktree の `build/ws090-shots/p5-*.png`。

| 確かめ | 結果 |
| --- | --- |
| 見た目 | PASS: glass の上の sidebar・content、Settings の card と row、button 4 種、switch、slider、field（`p5-controls.png`） |
| pointer | PASS: Save（`BUTTON save`）、Bluetooth の switch、slider の click と drag（窓の外まで → 100）、field の click |
| key | PASS: field に ` OS`（`Kei OS`）、Ctrl+S は app の `KEY code=31 modifiers=2`、Tab で password へ、`secret` で 6 文字（•）、Tab で button、Space で `BUTTON delete`（`p5-controls-used.png`） |
| list | PASS: click で選択、Down・Page Down・Enter（activate）、End で 100 行目まで滑る（`p5-list-end.png`）、Home、double click で activate、wheel と止まった pointer の下の行が光る（`p5-list-wheel.png`） |
| dialog・chip・progress | PASS: dialog（`p5-dialog.png`）、Tab で Cancel に枠（`p5-dialog-tab.png`）→ Esc で `answer=Cancel`、再び開いて Enter で `answer=Delete`、chip と動く progress（`p5-chip.png`） |
| touch | PASS: sidebar の tap で頁、switch の tap、slider の knob を指で 300 px（100 → 2）、field の tap で focus（`p5-touch-controls.png`）、list の 1 本指の drag と fling、tap で選択、double tap で activate（`p5-touch-list.png`） |
| Text Editor の回帰 | PASS: 1 行目の末尾に ` p005` を打ち Ctrl+S → file の 1 行目が `alpha beta gamma delta p005`（SSH で確認、`p5-textedit.png`）、tap で Ln 5、2 本指の scroll（`p5-textedit-touch.png`） |

- QEMU で見つけて直した誤り:
  1. frame の間に「文字・Tab・文字」が来ると、Tab が先に focus を動かし、前の文字が app に落ちた → key を押された時の focus の部品に宛てる。
  2. slider の drag の最後の動きと release が同じ frame の間に来ると、最後の位置が失われた（91 で止まった）→ 放した frame にも held と見る。
  3. wheel で list が動いた後、止まった pointer の下でない行が光ったまま → frame の終わりに hover を求め直す。
  4. 見本の program: 頁を替えた frame の sidebar の光が古い、activate の表示が次の入力まで古い → 部品が変えた時に次の frame を求める。
- 観察（未解決、影響は小）: list の double click の直後（500 ms）の画面で、状態の行が 1 frame 古かった。数秒後の画面は新しい。次の frame は
  dirty で描いているので、見せ方か VNC の取り込みの遅れの可能性（未確認）。
- 注入の touch の device は touchinject の実行ごとに作り直され、最初の tap が届かないことがある（p004 と同じ観察）。script の最初に捨てる tap を置いた。
- focus の枠を Tab の時だけにした変更（最後の変更）は host の試験で確かめ、QEMU の画面は変更の前（Tab で動かした画面だけなので見た目は同じ）。

### build・規約・boot（`build/ws090/p005-final.sh`、出力 `build/ws090/p005-final.out`）

- build（`-Werror`）: `libkeiui.so`（138 KB）・`/bin/kuidemo`（31 KB）・`/bin/textedit` exit 0、warning 0。
- host: `host-widgets` 94/94、`host-input` 63/63、`host-draw` 13/13、`plan/tools/textedit/host-core.sh` 34/34、`plan/tools/keiland/host-chooser.sh` 75/75。
- 規約: `style-check.py`（libkeiui の .c・.h、kuidemo、host-widgets）違反 0、`git diff --check` 0。`include/libc/keiui.h` は前からの 7 件
  （prototype の並びの段落の注釈の判定、p004 と同じく対象外）。
- boot test: `build-ssh-image.sh build/amd64`（exit 0、log の warning に libkeiui・textedit・libkeiland・kuidemo は 0）→ `boot-test.sh` **PASS**
  （`build/ws090/boot-p005/login.png`）。

## 未実施・残り

- 実機（未実施）。IME（WS095 の p006 の後）。secret の field の IME は対象外。
- 部品の key の宛先の変更で、Text Editor（`kui_ui` を指だけに使う）の動きは変わらない（QEMU で tap・2 本指を確認）。
- 次: p006（file chooser を libkeiui へ）。
