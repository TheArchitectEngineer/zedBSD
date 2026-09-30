<!-- awesome-plan project=zedbsd record=ws090-p013 -->

# ws090-p013: kui_window の text-input-v3 の受け口と Text Editor（WS102 の D1）

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`）、main を merge した上。QEMU の Venus、実機は未実施）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て。ユーザーの判断 WS102 の D1「libkeiui に入れる」: text-input-v3 の受け口を `kui_window` に入れ、Text Editor と
今後の libkeiui の app がかなを受け取れるようにする）
依存: p004（`kui_window`）

## 範囲と受け入れ

- libkeiui の `kui_window` に text-input-unstable-v3 の client の受け口。Text Editor が compositor から送られた文字列（かな）を受け取る。
- IME の file（`ime.h`・`text-input.c`（wayland の）・`input-method.c`、`userland/desktop/ime/`）と seat.c の IME の hook は変えない（変えていない）。
  使うのは client の公開の header（`include/libc/wayland/text-input-unstable-v3-client-protocol.h`）だけ。
- 確かめ: QEMU で compositor の text-input の経路で commit された文字列が Text Editor に入る。Text Editor の回帰（host-core、打つ・保存）。
  可能なら WS102 の keyboard のかな（main に未 merge のため未実施）。

## 変更

### libkeiui（`KUI_VERSION` 6）

- `keiui.h`: `KUI_WINDOW_TEXT_COMMIT`（14）・`_PREEDIT`（15）・`_DELETE`（16）、`KUI_WINDOW_TEXT_MAX`（256）、`struct kui_window_event` に
  `text`・`begin`・`end`・`before`・`after`。`kui_window_text_input(window, enabled)`（text を編集する所で求める）、`kui_window_text_cursor(window, x, y, w, h)`。
- `text-input.c`（新）: manager を registry で bind し、seat の text input を作る。app が求め、text input が自分の surface に enter している間
  enable（content type は普通の文、caret の矩形）し、変わるたびに commit（数える）。preedit・commit・delete は done まで溜め、done で
  delete → commit → preedit の順に窓の入力の queue に積む（preedit は出た時と消えた時）。長い文字列は文字の境で切る。
- `window.c`・`window.h`: registry に manager、起動と close、`keiui_window_push`（窓の他の部分から queue に積む口）。
- `Makefile`: `text-input.c`。

### Text Editor

- `main.c`: 窓の text の入力を editor の入力（`TE_EVENT_TEXT`・`TE_EVENT_TEXT_DELETE`）に直す。preedit は caret の所に白地と accent の下線で
  重ねて描く（`main_preedit_draw`）。dialog と chooser の無い間だけ text input を求め、frame の後に caret の矩形を送る。log `TEXT input commit=`・
  `TEXT input preedit=`。
- `app.c`・`textedit.h`: `app_text`（commit は選択の所へ挿入、delete は caret の前後の byte を消す。dialog・chooser の間は受けない）、
  `te_app_caret_rect`（窓の座標の caret）。log `TEXT commit bytes= text=`。

## 試験

### QEMU（`plan/ws090/tests/textinput-p013.sh`、image は `config-amd64-textinput.mk`: WS095 の IME の image + Text Editor・libkeiui）

**PASS**（`build/ws090/textinput-run1.log`、画面と log は worktree の `build/ws090-shots/p013/`）。

| 確かめ | 結果 |
| --- | --- |
| IME が Text Editor に activate | `ZWL IME activate client=` |
| 直接の入力 | `ab` は key として入り、text input の commit は無い |
| 日本語（Alt+Space） | `kanji` → preedit `かんじ` が caret に下線付きで出る（`preedit.png`）、Space → `漢字`（`converted.png`）、Enter → Text Editor が `漢字` を挿入（`TEXT commit bytes=6 text=漢字`） |
| かな | `kana` Enter → `かな` を挿入（`committed.png`） |
| 保存 | 直接の入力に戻して `c`、Ctrl+S → file が `ab漢字かなc`（SSH で読んだ） |
| compositor | `ZWL ERROR` 0 |

- WS102 の on-screen keyboard のかな: main に keyboard.c が未 merge のため未実施（P3 の L1 の作業中）。同じ text-input の commit の経路を通るので、
  merge の後に `textinput-p013.sh` と同じ形で確かめられる。

### 回帰・build・boot（出力 `build/ws090/p013-final.out`）

- build（`-Werror`）: libkeiui・textedit・kuidemo exit 0、warning 0。
- host: `plan/tools/textedit/host-core.sh` 34/34、`host-input` 63/63、`host-draw` 13/13、`host-widgets` 94/94、`plan/tools/keiui/host-chooser.sh` 85/85。
- Text Editor の打つ・保存: 上の QEMU の試験の中（`ab`・`c` を打って Ctrl+S、file で確認）。
- 規約: `style-check.py`（text-input.c・window.c・window.h・textedit の .c・.h）違反 0、`git diff --check` 0。
- boot test: `build-ssh-image.sh build/amd64`（exit 0）→ `boot-test.sh` **PASS**（`build/ws090/boot-p013/login.png`）。

## 制限・残り

- preedit は caret の所に重ねて描く（本文の中へは入れない）。文字の大きさは UI の 13 px（本文の等幅の font と違う）。
- surrounding text（caret の前後の文）は送っていない（IME の再変換は使えない）。
- kuidemo・file chooser の field（`kui_field`）はまだ text input を求めない（受け口は `kui_window` にあるので、field の focus で求める形は後の Phase）。
- 実機は未実施。
