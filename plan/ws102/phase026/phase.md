<!-- awesome-plan project=zedbsd record=ws102-p026 -->
# ws102-p026: UAT 2026-10-13 の画面 keyboard の指摘（flick の日本語・IME・配置）

Parent: [WS102](../ws.md)
Status: in-progress（T1-563 FAIL → attempt 2（P1）で試験の誤りを直した。T1-565 で bugs.phone-ime-flick だけ FAIL → attempt 3（P2、2026-10-13）で判定の型を直した。T1 の再試験待ち: 下の「attempt 3」）
Disposition: normal
Queue: Q1 の依頼（2026-10-13、P2: attempt 1、P1: attempt 2、P2: attempt 3）

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

## attempt 2（P1、2026-10-13: T1-563 の FAIL の調べと直し）

T1-563（QEMU、2 回とも同じ）: p026 の中心（あい→会い の 1 回の commit、よむ→読む、Shift、number の全角）・qwerty・qwerty-ime は ok。
tools・history・emoji・p026 の 次のApp が `tool …: no place MISSING`、send の `KWL OSK preedit text=あいうえおかが`・
`reading commit text=あいうえおかが reason=enter`・`ime-probe: text` が MISSING。AAT 4 本は IME の image に `aat-input`・
`keiland-shot` が無く未実施。証拠: t1 の worktree の `build/review/t1-563/osk-run2.txt`・`osk/`（読んだだけ）。

### 原因（どちらも試験の誤り。実装は変えていない）

| 症状 | 根拠 | 原因 |
| --- | --- | --- |
| `tool 次のApp: no place`（tools・history・emoji も） | T1 が host に取った `osk/osk-now.txt` に `KWL OSK trect label=次のApp x=1124 y=86 width=150 height=50` など 21 個の道具の行が全てある。同じ回の `place_middle` でも ASCII の型（`KWL OSK crect slot=0 `・`KWL OSK wrect `）は位置が取れ、tap が効いた（`candidate commit sent=1`・`width toggle on=1` が ok） | `place_middle`（osk-guest.sh）は型を guest の shell の `grep -a '…'` に渡していた。UTF-8 の label（前のApp・→・行頭…）の型は guest の shell を通すと何にも合わない（`expect_text` の注記と同じ: UTF-8 は host で比べる）。p026 で道具の位置を log から取る形にした時に、この経路を通った |
| send の `preedit text=あいうえおかが` MISSING | `osk/ime-probe.log`: `PROBE DONE … preedit=あいうえおか` → `preedit=あいうえおが`（濁点の key が か を が に置き換え）→ `commit=あいうえおが`、`PROBE TEXT text=あいうえおが`、`PROBE DELETE` 無し。`keyboard.c` の `keyboard_voice`（読みの最後のかなを濁音に置き換える、設計の「濁点の key も読みの最後の字を置き換える」） | 期待値が p026 の前の挙動のまま: 前は か を commit した後に 3 byte を消して が を commit したので、commit の列が「あいうえおかが」だった。preedit では か が が に置き換わるので、正しい text は「あいうえおが」。ime-probe の preedit の経路は動いている（実装の bug ではない） |

### 直し（`plan/ws102/tests/osk-guest.sh` だけ）

- `place_middle`: guest では ASCII の `grep -a 'KWL OSK'` で keyboard の行を取り（`OUTDIR/place-now.txt`）、型は host の `grep -aF`（固定の文字列）で探す。呼び出し側（`tool_label`・crect・wrect・hrect）は変えない。T1-563 の `osk-now.txt` に当てて、次のApp 1199,111・行頭 1004,299・選択 1004,372・→ 1238,226・コピー 1017,445・前のApp 1043,111・貼り付け 1225,445・履歴 1160,164・絵文字 1238,164 が取れることを host で確かめた。
- send: 期待を「あいうえおが」に（`KWL OSK preedit text=あいうえおが`・`KWL OSK reading commit text=あいうえおが reason=enter`・`PROBE TEXT text=あいうえおが`）、先頭の注記も。
- 他の guest の grep に UTF-8 の型が無いことを確かめた（`grep -nP` で expect_log・expect_count・count・guest の grep を探して 0。`emoji_middle` は ASCII）。

### AAT の image

AAT 4 本（`bugs.osk-flick-terminal`・`bugs.phone-ime-flick`・`bugs.osk-qwerty-ime`・`desktop.osk.prediction`）の文書の準備は全て「AAT の image」で、session（App Home からの起動）・Phone・`keiland-settings`・`aat-input`・`keiland-shot` を使う。AAT の標準の image（`plan/tools/aat/config-amd64-aat.mk` → `plan/ws159/tests/config-amd64-uat.mk` → `config/current-uat.mk` → `config/release/config-amd64-beta2.mk` → `config/ci/config-amd64.mk`）には `keiland-ime`・`ime-dict-ja`・`ime-dict-skk`・terminal・textedit・phone が入っており、外す行も無い。よって AAT 4 本は **AAT の標準の image**（`plan/tools/aat/build-image.sh BUILD`）で流す。IME の image（`build-ime-image.sh`）は osk-guest.sh 用のまま（ime-probe が要る）。image の作り方は変えない。

### 確かめた事（host のみ。QEMU は T1 の再試験）

- `sh -n plan/ws102/tests/osk-guest.sh` OK。`sh plan/ws102/tests/host-keyboard.sh` PASS、`sh plan/ws102/tests/host-predict.sh` PASS。
- source（C）は変えていないので build は attempt 1（P2）のまま（amd64・rpi4 の wayland、warning 0）。

### T1 への再依頼

1. image は attempt 1 と同じ（`plan/ws095/tests/build-ime-image.sh BUILD`、同じ BUILD を BIN）。`GUEST_RUNTIME=... BIN=BUILD plan/ws102/tests/osk-guest.sh OUTDIR install p026 send tools history emoji` → 最後の行 `osk-guest: PASS`（qwerty・qwerty-ime は T1-563 で ok、変えていないので省いてよい）。PNG は tools-pasted・history-pasted・emoji-sent を目視。
2. AAT の image（`plan/tools/aat/build-image.sh BUILD`）を QEMU で起動し、`plan/tools/aat/run-aat.sh qemu OUTDIR bugs.osk-flick-terminal bugs.phone-ime-flick bugs.osk-qwerty-ime desktop.osk.prediction` → 4 本 pass（`desktop.osk.prediction` は needs-person の撮影を目視）。

## attempt 3（P2、2026-10-13: T1-565 の bugs.phone-ime-flick の FAIL）

T1-565（QEMU、AAT の標準の image）: osk-guest と AAT の `bugs.osk-flick-terminal`・`bugs.osk-qwerty-ime`・`desktop.osk.prediction` は PASS。
`bugs.phone-ime-flick` だけ step 7 が `None; Japanese kept True`（retry も同じ）で、"BUG-203 reproduced" の FAIL。
同じ step の PNG（`ime-sent`）には Phone の message に 日本 が送られ "Delivered"・"Echo: 日本" が写っている。
証拠: t1 の worktree の `build/review/t1-565/aat3/`（records・logs・png、読んだだけ）。

### 原因（判定の側。p026 の preedit の経路とも、Phone の確定の経路とも関係しない）

- `aat3/logs/bugs.phone-ime-flick.log` の送信の行は `PHONE SEND contact=0 channel=0 length=6 paired=0 error=0`（日本 の 6 byte）。続いて
  `KWL PHONE send … text=6 backend=loopback`・`PHONE STATUS request=2 state=2 failed=0`・`PHONE RECEIVED contact=0 channel=0 length=12 error=0`。IME の日本語は欄に入り、送られている（BUG-203 は起きていない。file の非 ASCII の判定 `Japanese kept True` とも合う）。
- helper の型は `PHONE SEND contact=0 channel=0 length=\d+ error=0` で、length と error の間の `paired=` を許さない。
  `paired=` は ws197-p004（commit 778c1e377、2026-10-10）が `userland/desktop/phone/main.c` の `ph_log("SEND contact=%ld channel=%u length=%zu paired=%d error=%d", …)` に足した欄で、AAT の型が追従していなかった。
- 同じ型は step 3 の `length=3 error=0`（が の送信）と、`apps.phone.message`（`plan/tools/aat/scenarios/helpers_phone.py` の `length=14 error=0`）にもあり、同じ理由で必ず外れる。

### 直し（試験だけ。source は変えていない）

- `plan/tools/aat/scenarios/helpers_bugs.py`: phone-ime-flick の 2 つの型を `length=\d+ (?:\S+ )*error=0`・`length=3 (?:\S+ )*error=0` に（間の欄は問わず、error=0 は要る）。
- `plan/tools/aat/scenarios/helpers_phone.py`: apps.phone.message の型を `length=14 (?:\S+ )*error=0` に。
- `tests/scenarios/bugs/phone-ime-flick.md`・`tests/scenarios/apps/phone/message.md`: 正解の行を同じ形に（間の欄は問わない）。
- BUG-203・BUG-204 の試験の意図（IME の日本語が Phone の欄に入り送られる、flick のかなが届き閉じると確定して送られる）は変えていない。

### 確かめた事（host のみ）

- 新しい型を Python の `re` で当てた: `… length=6 paired=0 error=0`・`… length=6 error=0` に合い、`error=5`・`channel=mms` には合わない。`length=3` の型は `length=3 paired=1 error=0` だけ、`length=14` の型は `length=14 paired=0 error=0` だけに合う。
- `py_compile`（helpers_bugs.py・helpers_phone.py）OK、`plan/tools/aat/check-scenarios.py` PASS（153）、`git diff --check` OK。
- step 2・3（flick の が）は T1-565 では step 7 で止まったため未実行（QEMU の確認は未実施）。

### T1 への再依頼

AAT の標準の image（`plan/tools/aat/build-image.sh BUILD`）を QEMU で起動し、`plan/tools/aat/run-aat.sh qemu OUTDIR bugs.phone-ime-flick`（できれば同じ回に `apps.phone.message`）。合格は両方 pass（phone-ime-flick は step 1〜3 の全部: `PHONE SEND … length=N … error=0` と非 ASCII の file、`KWL OSK preedit text=が`・refused 無し、`KWL OSK reading commit text=が reason=close`・`PHONE SEND … length=3 … error=0`）。PNG は ime-sent・flick を目視。
