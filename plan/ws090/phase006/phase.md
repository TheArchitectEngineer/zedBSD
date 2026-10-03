<!-- awesome-plan project=zedbsd record=ws090-p006 -->

# ws090-p006: file chooser を libkeiui へ、Text Editor の dialog・chip

Status: cleared（2026-09-30、subagent の worktree `wt/ws090`、main 16c08ea9 を merge した上）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-30「ws090-p006（file chooser を libkeiui へ移す、Text Editor の dialog・chip）」）
依存: p004（cleared）、p005（cleared）

## 範囲と受け入れ

- design.md §7: `keiland_file_chooser_*` を `kui_file_chooser_*` として libkeiui に移す。中身は部品と `kui_window`（shm）で作り直す。
  libkeiland から取り除き、KEILAND_VERSION を main の最新の次にする（main の指示: 今は 15 → **16**）。
- BUG-112 の回避（xdg_wm_base の binding を持ち続ける）を外し、回避なしで chooser の開閉を繰り返しても切られないことを確かめる（main の指示）。
- Text Editor の chooser・dialog・chip を部品に替える。
- 受け入れ: 移した host-chooser の試験、QEMU で Open・Save As・上書き・取り消し。

## 実装

### libkeiui（`KUI_VERSION` 5）

- `keiui.h`: `kui_file_chooser_open`・`kui_file_chooser_destroy`、`struct kui_file_chooser_options`・`_listener`・`struct kui_file_filter`、
  `KUI_FILE_CHOOSER_OPEN/SAVE/CHOSEN/CANCELLED/FILTERS_MAX`（keiland の API と同じ形と意味）。`KUI_HIT_TOUCHED`、`KUI_LIST_TOUCHED`。
- `chooser-model.c`（libkeiland の model を移した）: folder・places・filter・recent・hidden・Save の検査と上書きの質問・path（Ctrl+L）。
  選択と scroll は `kui_list`、名前と path は `kui_field`。pointer・key・hit・layout の処理は view と部品へ移り、model から消した。
- `chooser-view.c`（新規）: 1 frame を部品で描き、報告で model を動かす。sidebar（`kui_sidebar_item`）、上へ、location（click で path の field）、
  列の見出し、`kui_list` の行（icon・名前・大きさ・日時）、名前の `kui_field`、filter の pill、Cancel と主の `kui_button`（押せない時は無効）、
  拒否の message、上書きの質問は `kui_dialog`。部品の取らない key: Esc・Enter・Backspace・Alt+Up・Ctrl+H・Ctrl+L・文字で頭文字の選択、矢印。
  指の tap で folder に入る（`KUI_LIST_TOUCHED`）。見た目は今の chooser（ws092-p003）と同じ配置と色。
- `chooser.c`（新規）: `kui_window` を app の接続の上に作る（`keiui_window_open_shared`）。入力は窓が `wl_display.sync` で起こす
  （`keiui_window_set_notify`）、描画は frame callback で間引く、glass の 2 枚の panel、答えは窓を消した後の sync で伝える。
- `window.c`・`window.h`・`present-shm.c`: 共有の接続の窓（自分の queue で global を探して app の default queue に移す、親と最小の大きさ、
  接続は閉じない）、起こす仕組み（queue に積んだ時・configure・buffer が返った時）。**自分の xdg_wm_base を bind して窓を閉じる時に destroy
  する**（BUG-112 の回避を外した）。
- `ui.c`: key の順を守る: 部品は要らない key に当たるとその後の key を取らず、部品の取らなかった一番古い key だけが app の
  `KUI_EVENT_KEY` になり、残りは次の frame を待つ（QEMU で「d・Enter」の Enter が d の選択の前に処理されて消える誤りを見つけて直した）。
  focus の無い時の key も、待つ key があれば順に並ぶ。指の tap の click に `KUI_HIT_TOUCHED`。
- 試験: `plan/tools/keiland/host-chooser.*` を `plan/tools/keiui/host-chooser.*` に移し、`kui_ui` を通した key・click・tap で書き直した（85 件）。

### libkeiland（`KEILAND_VERSION` 16）

- `chooser.c`・`chooser-model.c`・`chooser-draw.c`・`chooser.h`・`paint.c`・`paint-text.c`・`paint.h` を消した。`keiland.h` の file chooser の節を
  「libkeiui へ移った」の注記に替え、版の列に 15（音量、前から抜けていた）と 16 を足した。`exports.map`・`Makefile`。
- libkeiland の link の `libtruetype.so` は残した（vmunix.mk を変えない最小の変更。NEEDED は残る）。

### Text Editor

- `main.c`: `kui_file_chooser_*` を使う。dialog は `kui_dialog`（`main_overlay`、窓の card の上、Enter が主・Esc が最後）、message は `kui_chip`。
  dialog の間は pointer と key を `kui_ui` に渡し（`main_dialog_event`）、指の frame は dialog が記録する。UI の font を libkeiui の text としても開く。
- `app.c`・`textedit.h`: dialog の layout・hover・key・press を消し、`te_app_dialog_words`（題・文・button の label）と `te_app_dialog_choose` を出した。
- `draw.c`: dialog・button・message の描画を消した（Ln/Col の status の chip は今のまま）。

## 試験

### host

- `plan/tools/keiui/host-chooser.sh` **85/85**（Open: 並び・hidden・filter・places・矢印・Enter・Backspace・頭文字・上へ・Alt+Up・click・double click・
  遅い 2 回の click・folder の tap・double tap・Esc・Recent・「z と Enter が 1 frame に来ても z の後に Enter」・誤った options。Save: 名前の選択・上書き
  の質問の Cancel（click）・Esc・Replace（click）・Enter で置き換え・folder の名前・`/` の拒否・Backspace で message が消える・書けない folder・
  filter の pill・Cancel。path: Ctrl+L・`~/Desktop`・location の click・相対の path・Esc で閉じて次の Esc で取り消し・Save の path）。
  絵は worktree の `build/keiui-shots/host-chooser-*.png`（今の chooser の `build/keiland-shots/` と並べて見た目がほぼ同じ）。
- 回帰: `host-widgets` 94/94、`host-input` 63/63、`host-draw` 13/13、`plan/tools/textedit/host-core.sh` 34/34。

### QEMU（Venus、main の `build/main-pen/hdd-image.img` の複写、この worktree の wayland・libkeiland・libkeiui・libtruetype・libwayland-client・libvulkan・textedit・kuidemo）

判定は画面（VNC）、Text Editor の log、SSH で読んだ file。画面は worktree の `build/ws090-shots/p6-*.png`。

| 確かめ | 結果 |
| --- | --- |
| Open | PASS: Ctrl+O で chooser（glass、`p6-open.png`）。d・Enter で Documents へ（最初は Enter が消えた → key の順を直した）、Down Down・Enter で `note.txt` を開いた（log の `CHOSEN`・`OPEN`、`p6-open-docs.png`）。double click でも開いた |
| Save As と上書き | PASS: Ctrl+Shift+S、名前を `keep` に打ち（拡張子の前まで選択）Enter → 「Replace "keep.txt"?」（`p6-replace.png`）、Esc で保つ、Enter・Enter で置き換え → file の中身が `hello` `edited`（SSH）、chip「Saved」（`p6-saved.png`） |
| 取り消し | PASS: Esc・Cancel の button で `CHOSEN result=1` |
| **BUG-112 の回避なし** | PASS: Ctrl+O・Esc を 10 回、Ctrl+Shift+S・Cancel を 3 回（13 回とも `CHOSEN result=1`）、zdesktop の log に `ERROR` 0、Text Editor は生きていて文字を打てた |
| touch | PASS: chooser の Documents を 1 回 tap で入り、`note.txt` を double tap で開いた |
| Text Editor の dialog | PASS: 変更の後の Ctrl+W で「Save changes to "keep.txt"?」（`kui_dialog`、`p6-unsaved.png`）、Cancel の click → choose=2、Esc → choose=2、Save の click → 保存して閉じた |
| kuidemo の回帰 | PASS: field に ` OS`、Tab で password に `ab`、Shift+Tab で戻って `!` → `Kei OS!`（順が保たれた） |

- 見た目の違い（今の chooser と比べて）: Cancel・Open の button の文字が部品の太字 14 px（前は 13 px）、上書きの質問の Replace が accent（前は赤）。
  `kui_dialog` に危険の色の指定が無いため。
- 注入の pointer が窓の出る前から同じ所にあると、最初の click が届かない（QEMU の注入の手順の問題。pointer を動かしてから click すると届く）。

### build・規約・boot（`build/ws090/p006-final.sh`、出力 `build/ws090/p006-final.out`）

- build（`-Werror`）: `libkeiui.so`・`libkeiland.so`・`/bin/textedit`・`/bin/kuidemo` exit 0、warning 0。
- 規約: `style-check.py`（libkeiui・libkeiland・textedit・kuidemo・試験）違反 0、`git diff --check` 0。
- boot test: `build-ssh-image.sh build/amd64`（exit 0、log の warning に関係の file 0）→ `boot-test.sh` **PASS**（`build/ws090/boot-p006/login.png`）。

## 未実施・残り

- 実機（未実施）。
- main への依頼: `plan/master.md` の Tools 節（271 行）の「共有の file chooser の試験（tools/keiland）」を `plan/tools/keiui/host-chooser.sh`（libkeiui の
  `kui_file_chooser_*`、85 件、絵は `build/keiui-shots/`）に替える。`plan/ws092/ws.md` の試験の path も同じ（WS092 は完了の WS）。
- `plan/bugs/BUG-112.md` の「libkeiland の回避は残したまま」の行は、この Phase で外した（main の Bug の記録の更新）。
- 次: p007（Settings、WS089 の完了の後）、p008（PDF Viewer・Image Viewer の chooser を `kui_file_chooser` に）。
