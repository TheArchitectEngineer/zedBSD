<!-- awesome-plan project=zedbsd record=ws102-p026 -->
# ws102-p026: UAT 2026-10-13 の画面 keyboard の指摘（flick の日本語・IME・配置）

Parent: [WS102](../ws.md)
Status: in-progress（実装済み・T1 の試験待ち。P2、2026-10-13。試験の依頼は Q1 へ送った: 下の「T1 に頼む試験」）
Disposition: normal
Queue: Q1 の依頼（2026-10-13、P2）

## 由来（ユーザー、2026-10-13 UAT、原文のまま）

バグ
・フリック入力のパネルをTermnalで使うと、「あい」まで入力して「愛」に変換しても、先に「あい」が確定入力されていて、二重入力になっています。
・フリック入力で送り仮名つきの変換ができていないです。
・フリックパネルの "a/A" のボタンが動作していないように見えます。
・フリックパネルのひらがな入力の時、IMEもONになるようにしてください。アルファベットのときはIME OFFにします。数字の時はONもOFFも可能で、ONなら全角、OFFなら半角にします。

要望
・オンスクリーンのフルキーボードで、Enterの位置が違和感があります。通常のキーボードでのEnterの位置にReturnとしておいてください。
・オンスクリーンのフルキーボードで、Delの位置が違和感があります。通常のキーボードでのBackspaceの位置にBackspaceとしておいてください。
・オンスクリーンのフリック入力キーボードで、Delの名前に違和感があります。削除にします。
・フリックパネルのひらがな入力の時、Delは削除にします。
・フリックパネルの「編集」「候補」といった並びのボタンが小さくて押しづらいので、縦方向を大きく、2倍にします。編集操作ボタンも2倍にします。前のApp、も縦方向1.5倍。その隣のDelボタンは、次のAppに変更します。次のAppは順繰りにアプリを変えられます。

## 原因（source を読んで確かめた）

| 指摘 | 原因 |
| --- | --- |
| 二重確定 | flick の かな は 1 字ずつ text-input の commit で送られ（`keyboard_send` → `keyboard_send_commit`）、候補の tap は読みの byte 数を `delete_surrounding_text(before=6)` で消してから語を commit する（`keyboard_candidate_release`）。Terminal の `window_text_delete`（`userland/desktop/terminal/window.c`）は**最後の 1 回の commit**（`last_commit`）より長い削除を無視する（shell の行は Terminal が知らないため）。「あ」「い」の 2 回の commit の後の before=6 は「い」の 3 byte より長いので無視され、「あい愛」になる。compositor 側の確定が原因で、Terminal の削除の扱いはその前提に合っていない |
| 送り仮名 | 候補の tab は IME の予測（`kl_ime_status_v1.predict` → `ja_predict_keyboard`）だけで、予測の索引は**送り仮名の無い見出しだけ**（`ja_predict_index`、「Only readings without okurigana are predicted」）。flick には変換（`ja_segment` の分割と活用）の経路が無い |
| a/A | `keyboard_case` は**直前に送った英字**の大文字・小文字を替える（iPhone の型）。英字を打つ前・別の操作の後に押すと何もせず（`KWL OSK case none`）、key の表示も変わらないので、動いていないように見える |
| IME の ON/OFF | flick の面と IME の言語が結ばれていない。かなは IME を通らない commit で送られ、IME の表示（あ／A）は利用者の切り替えのまま |

## 設計

### かなの組み立て（二重確定と送り仮名）

- かなの面で打つひらがな（と「ー」）は、確定せずに **OSK 自身の preedit** にする: 読み（`keyboard.reading`）が変わるたびに `kwl_text_input_deliver(input, 読み, 末尾, 末尾, NULL, 0, 0)`。app は IME の preedit と同じに下線つきで表示する（Terminal の `window_text_preedit`、libkeiland の `text_done`）。log `KWL OSK preedit text=…`。
- 候補の tap は **preedit を語で置き換える commit**（`deliver(input, NULL, …, 語, 0, 0)`）。削除を使わないので、Terminal を含めどの app でも二重にならない。学習は今までどおり（読みのある予測の語だけ）。
- 確定（log `KWL OSK reading commit text=… reason=…`）: 改行の key（読みがある時は改行を送らず確定だけ）、ひらがなでない文字（、。？！… 記号・英数・絵文字・履歴の貼り付け・手書きの候補）の前、道具（編集の面の操作・前の/次の App）の前、面の切り替え、panel を閉じる時、物理の key の押下（`kwl_seat_key` の先頭、IME が自分の preedit で上書きする前に）、focus の移る時（`kwl_ime_focus`、IME の preedit の確定と同じ所）。
- 変換: 空白の key は読みがある時「変換」: 候補の 1 番目を preedit に出し、押すたびに次の候補（候補の tab で青く示す）。改行で確定、削除で読みに戻る、次のかなで確定して新しい読み。候補が無い時は何もしない（log `KWL OSK convert none`）。
- 削除の key: 変換中は読みに戻る、読みがあれば最後の 1 字を消して preedit を出し直す、読みが無ければ今どおり Backspace の key。濁点の key も読みの最後の字を置き換える（読みが無い時だけ今の commit の置き換え）。
- 秘密の欄（password・PIN・hidden）は今どおり 1 字ずつ commit（preedit にしない）。text-input の無い app は今どおり断る（`refused reason=no-text-input`）。field の text-input が消えた・無効になった時は読みを捨てる（送り先が無い）。
- 送り仮名: IME の keyboard 向けの予測（`ja-engine.c` の `engine_predict`）の先頭に、**読み全体の変換**を足す: 読みを unit にして `ja_segment_split`・`ja_segment_candidates`（物理の keyboard の Space の変換と同じ分割・活用・送り仮名）。1 文節ならその候補（読みと同じひらがなは除く）、複数の文節なら各文節の 1 番目をつないだ文。変換の候補は最大 6 個を先に、残りを予測で埋め（12 個まで）、同じ語は 1 つにまとめる（予測の読みがあればそれを付けて学習できるように）。変換だけの語は読みを空で返し、OSK は学習しない（文全体を user 辞書に入れないため）。protocol（version 2）は変えない。

### IME の ON/OFF と面

- flick の panel が開いている間、面に IME の言語を合わせる: かなの面 → 日本語（`kwl_ime_select_composing`: 最後に使った日本語系の言語、無ければ `ja`）、英字の面 → `direct`。panel を開く時と面が替わる時（面の key、欄の種類による切り替え q893）に 1 回だけ選ぶ（毎 frame 選ばない）。log `KWL OSK ime face=… select=…`。
- 数字の面は選ばない（前の状態のまま）。band（title の帯）に切り替えの button「全角」「半角」（今の状態を表示）を出し、tap で ON/OFF。ON の時、ASCII の印字可能な文字（数字・記号）を全角（U+FF01〜）で commit。text-input の無い app へは半角の key に落とす。空白は半角のまま。

### a/A（英字の面）

- QWERTY の Shift と同じ型: 1 回で次の 1 字だけ大文字、400 ms 以内の 2 回で lock、もう 1 回で off。on の間は英字の key の表示を大文字（ABC・DEF…）に、a/A の key を淡い青（lock は青）にする。log `KWL OSK flick-shift state=N`。

### 配置

- QWERTY（letters・symbols・email・url の面）: 1 列の幅を 46（1/4 key 単位、今は 40）にし、数字の列の右端に **Backspace**（6）、中段（a〜l）の右端に **Return**（10、symbols は 6）を置く。下段は左右に Shift（9）、空白の列の Enter・Del は除き空白を広げる。数字の pad（number の面）の Del は位置を保ち名前を Backspace に。
- flick の面の Del → **削除**（かな・英字・数字の 3 面）。
- 道具: 1 行目「前のApp」「次のApp」（Del をやめ、高さ 44 → 66）、tab の行（編集・候補・履歴・絵文字）30 → 60、編集の操作の 4 行 44 → 88。panel の高さに収まらない画面（1280x800 では 396 px の所に 514 px が要る）では、3 つの高さを同じ比で縮めて収める（1920x1080 は 2 倍のまま）。候補・履歴・絵文字の面は tab の行の下から。
- 次のApp: 表示中の desktop の見えている窓（最小化・desktop の icon を除く）のうち、**重なりの一番下の窓を前に出す**（`kwl_focus_next`、edit.c）。押すたびに一番下が前に来るので、全ての窓を順繰りに巡る。log `KWL FOCUS next surface=… app=…`。
- 試験のために道具の位置を log に出す（`KWL OSK trect label=… x= y= width= height=`、flick の panel を開いた時）。

## 判断（Q1 に示す。止まらずに選んだ案）

1. 「数字の時はONもOFFも可能」: 数字の面の band に 全角/半角 の button を置く（system bar の言語の chip・Alt+Space でも切り替わる）。数字の面に入る時は IME を変えない。
2. 「次のApp の順繰り」: 窓の重なりの一番下を前へ（N 個の窓を N 回で一巡）。同じ app の複数の窓も 1 つずつ数える。最小化の窓は含めない（前のApp と同じ範囲）。
3. 「前のApp の隣の Del」: 道具の 1 行目の Del（`TOOL_DELETE`）を次のApp に替える。削除は 4 列目の key（削除）で行う。
4. 「Delの名前 → 削除」: flick の 3 面の Del を全て「削除」に。QWERTY の pad の Del は Backspace に。
5. 「2倍」が収まらない 1280x800・小さい画面では、比を保って縮める（1280x800 で約 1.6 倍）。
6. a/A は Shift の型にした（直前の字を替える型をやめた）。
7. 変換（空白）: 予測と変換の候補の巡回。IME の popup は使わない（OSK の候補の tab が一覧）。
8. IME の言語の選択は、per-app の言語の記憶（ws095-p016）にもそのまま残る（かなで打った app は次も日本語）。

## 変える path

- `userland/desktop/wayland/keyboard.c`・`keyboard-layout.c`・`keyboard.h`（OSK）
- `userland/desktop/wayland/seat.c`（物理の key で読みを確定、1 呼び出し）
- `userland/desktop/wayland/input-method.c`・`ime.h`（言語の選択の口、最後の日本語系の言語、focus の変化・text-input の消滅を OSK に知らせる）
- `userland/desktop/wayland/edit.c`・`kwl.h`（`kwl_focus_next`、OSK の口の宣言）
- `userland/desktop/ime/ja-engine.c`（予測の先頭に変換）
- 試験: `plan/ws102/tests/`（osk-guest.sh・qwerty-plan.py・host-keyboard.c・新しい host-predict）、`plan/ws177/tests/host-osk-field.c`（pad の名前）、`plan/tools/aat/scenarios/helpers_bugs.py`・`tests/scenarios/bugs/phone-ime-flick.md`（preedit の log）

## 確認

- build（warning 0）: amd64 と rpi4 の wayland・keiland-ime、keiland-linux の host build。
- host: `plan/ws102/tests/host-keyboard.sh`（layout）、`plan/ws102/tests/host-predict.sh`（変換の先頭: よむ → 読む、かいしゃ → 会社）、`plan/ws177/tests/host-osk-field.sh`。
- QEMU（T1）・実機: 下の「q?? の結果」に書く。

## 結果（P2、2026-10-13）

実装は上の設計のとおり。設計から足した点: IME が無い（`kwl_ime_on` が -1）時は、読みがあっても空白の key は読みを確定してから空白を送る（候補が来ないので変換できない）。IME がある時に候補がまだ来ていなければ `KWL OSK convert none`。

### 変えた path

- `userland/desktop/wayland/keyboard.c`: 読みを preedit にする（`keyboard_reading_add`・`_show`・`_commit`・`_delete`、`keyboard_convert`、候補の tap は preedit を置き換える commit、`reading_commit` の照合をやめた）、面と IME（`keyboard_face_set`・`keyboard_face_ime`、数字の面の band の `keyboard_width_toggle`、全角の `keyboard_full_width`）、a/A の Shift（`keyboard_shift_press` を QWERTY と共用、`keyboard_flick_label`）、道具（`TOOL_NEXT`、`keyboard_tool_heights`・`keyboard_tools_face_area`、log `trect`・`hrect`・`wrect`）、公開の口 `kwl_keyboard_key_typed`・`kwl_keyboard_focus_leaving`・`kwl_keyboard_text_input_gone`
- `userland/desktop/wayland/keyboard-layout.c`・`keyboard.h`: QWERTY の行の幅 46、Backspace・Return・左右の Shift、空白の列から Enter を除く、pad の Del・Enter を Backspace・Return に、flick の Del を 削除 に
- `userland/desktop/wayland/input-method.c`・`ime.h`: `kwl_ime_choose`・`kwl_ime_on`、`composing_language`（最後の日本語系の言語）、`kwl_ime_focus` と `kwl_ime_text_input_gone` から OSK の口を呼ぶ
- `userland/desktop/wayland/seat.c`: `kwl_seat_key` の先頭で `kwl_keyboard_key_typed`
- `userland/desktop/wayland/edit.c`・`kwl.h`: `kwl_focus_next`
- `userland/desktop/ime/ja-engine.c`: `engine_predict` の先頭に読み全体の変換（`engine_convert_reading`・`engine_convert_units`・`engine_word_add`）
- Terminal（`userland/desktop/terminal/`）は変えていない（二重確定の原因は compositor 側の 1 字ずつの確定で、preedit にしたので Terminal の削除の扱いに頼らない）
- 試験: `plan/ws102/tests/osk-guest.sh`（send の段を preedit と a/A に、tools・history・emoji の段を log の位置に、qwerty-ime の Return、新しい段 `p026`）、`qwerty-plan.py`（Return）、`host-keyboard.c`（配置の検査、幅 46）、新しい `host-predict.c`・`host-predict.sh`、`plan/ws177/tests/host-osk-field.c`（pad の Backspace）、`plan/tools/aat/scenarios/helpers_bugs.py`（phone-ime-flick の preedit、osk-qwerty-ime の Return、新しい `bugs.osk-flick-terminal`）、`tests/scenarios/bugs/phone-ime-flick.md`・`osk-qwerty-ime.md`・新しい `osk-flick-terminal.md`

### 確かめた事（host と build のみ。QEMU・実機は未実施）

- build: amd64（`ZEDBSD_CONFIG=plan/ws102/tests/config-amd64-inset.mk BUILD=build/ws102-p026`、`bin/wayland`・`bin/keiland-ime`）、rpi4（`config/ci/config-rpi4.mk`、`build/ws102-p026-rpi4`）、keiland-linux（`KEILAND_LINUX_BUILD=build/p2-keiland-linux all`）。全て exit 0、warning 0（-Werror）。
- host: `plan/ws102/tests/host-keyboard.sh` PASS（60 項目、配置の検査を含む）、`plan/ws102/tests/host-predict.sh` PASS（よむ → 読む、かく → 書く が先頭、かいしゃ → 会社 が先頭で 1 回、きょうはてんき → 今日は天気、読みそのものは出さない、ASan・UBSan）、`plan/ws095/tests/host-engine.sh` 233 passed 0 failed、`plan/ws177/tests/host-osk-field.sh` PASS、`plan/tools/aat/check-scenarios.py` PASS（153）、`sh -n osk-guest.sh`、`git diff --check`、`plan/tools/style-check.py` の変えた行の指摘 0。
- 未実施: QEMU（T1）、実機（Pi 4 の touch panel・5330）。

### T1 に頼む試験

- image: 依頼の commit の tree で `plan/ws095/tests/build-ime-image.sh BUILD`（keiland-ime・辞書・Terminal・ime-probe 入り）。同じ BUILD を `BIN` にする。
- 流す: `GUEST_RUNTIME=... BIN=BUILD plan/ws102/tests/osk-guest.sh OUTDIR install p026 send tools history emoji qwerty qwerty-ime`。合格は最後の行 `osk-guest: PASS`。PNG: p026-flick（道具の行の高さ）・p026-candidates・p026-converted・p026-shift（a/A の淡い青と大文字の表示）・p026-number（全角）・p026-qwerty（Return・Backspace）。
- AAT（あれば同じ回に）: `bugs.osk-flick-terminal`・`bugs.phone-ime-flick`・`bugs.osk-qwerty-ime`・`desktop.osk.prediction`。

### 実機で見る点（UAT）

- Pi 4 の touch panel（指）: flick のかなが下線つきの preedit で出て、候補の tap で語だけが入る（Terminal で「あい」→「愛」が「愛」だけ）。「おくる」→ 空白 → 送る。道具の行・tab・編集の操作が押しやすい高さか（画面が低いと比で縮む）。次のApp で窓が順に前へ。数字の面の 全角/半角。
- 5330: system bar の言語の表示がかなの面であ、英字の面で A になる。物理の keyboard を打つと flick の読みが先に確定する。QWERTY の Return・Backspace の位置。
