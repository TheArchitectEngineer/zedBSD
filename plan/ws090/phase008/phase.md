<!-- awesome-plan project=zedbsd record=ws090-p008 -->

# ws090-p008: PDF Viewer・Image Viewer を libkeiui へ

Status: cleared（2026-09-30、サブエージェント P7、worktree `ws090-kui`、main を merge した上。QEMU の Venus、実機は未実施）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: なし（2026-09-30 午後 Q1 の割り当て。ユーザーが「libkeiui への移行」を選んだ。サブエージェント P7、worktree `ws090-kui`（branch `wt/ws090-kui`））
依存: p006（cleared）

## 範囲と受け入れ

- PDF Viewer・Image Viewer の窓の土台（`kui_window`）、入力（scroll・touch の経路）、file chooser（design.md §7: `kui_file_chooser`）を libkeiui に替える。
  見た目と機能は変えない。移したことで keyboard の inset（KUI_VERSION 7）などの共通の機能が効くようにする（Q1 の依頼）。
- 確かめ: 各 app の既存の試験が前後で PASS、libkeiui の host 試験、WS099 の C9、boot test、画面（`build/ws090-shots/`）。
- Q1 の許可（2026-09-30）: chooser の置き換えに合わせて `plan/ws079/tests/host-pdfviewer.c`・`plan/ws079/tests/run-pdfviewer-host.sh`・
  `plan/tools/imageview/run-host.sh` を直す（確かめる中身は弱めない、窓の移行と別の commit）。`platform/amd64/vmunix.mk` の 2 つの link の規則に
  libkeiui を足す。`plan/tools/files/config-amd64-files.mk` は package の依存の閉包で入らないときだけ変える（入ったので変えていない）。

## 実装

### 窓の移行（commit 2656dd7d）

- **PDF Viewer**: `window.c`（1145 行）・`present.c`（1212 行）・`shaders.h`・`shaders/` を消し、`kui_window`（`KUI_PRESENT_VULKAN`）に替えた。
  `main.c` が窓の event を viewer の入力（`pv_event`）に直す（pointer・wheel（縦だけ、今までどおり）・key（repeat の印を含む）・menu と titlebar の
  action は `kui_window_post` で key と同じ queue に順に積む）。指は `kui_window` の touch の event をそのまま `touch.c` に渡す（時刻の変換は libkeiui
  が同じ式で行うので `touch_time`・`pv_touch_clock` を消した）。`window.h` の `struct pv_window` は `kui_window` を持つだけ。menu・titlebar は accessor。
- **Image Viewer**: `window.c`（1222 行）を消し `kui_window`（`KUI_PRESENT_NONE`）に替えた。**画像の texture の層を持つ自前の present（`present.c`）は
  残し**、`kui_window` の surface に描く（design.md §5 の「Image Viewer の画像の層は移す Phase で決める」の判断: libkeiui の present に画像の層を
  足すと Image Viewer だけのために 500 行ほどの Vulkan を library に入れることになるので、今は app に残す。他の app が画像の層を要るようになったら移す）。
  context menu は `kui_window_seat`・`kui_window_press_serial`、glass は `kui_window_surface`。
- **libkeiui（KUI_VERSION 10）**: Image Viewer の全画面のために `kui_window_set_fullscreen`・`kui_window_fullscreen`（configure の states に
  FULLSCREEN があるか）を足した（`window.c`・`window.h`・`keiui.h`、最小の差分）。main の KUI_VERSION は 9 だった（WS102 が 8・9 を使った）。
- **keyboard の inset**: `kui_window` を使うので zdesktop の `keiland_keyboard_inset_v1` を受ける。PDF Viewer は文字の view を持たない（既定の
  caret の中央寄せは何もしない）ので、`kui_window_on_keyboard_inset` で inset を聞き、password の card を keyboard の残す部分の中央に置く
  （`pv_app` の `keyboard_right`・`keyboard_bottom`、`pv_password_layout`）。Image Viewer は文字を入れる所が無いので既定のまま。
- build: 2 つの Makefile の依存に `desktop/libkeiui`、`platform/amd64/vmunix.mk` の pdfviewer・imageview の link に `libkeiui.so`。
- 大きさ（chooser の置き換えの後）: `/bin/pdfviewer` 109 KB → 71 KB、`/bin/imageview` 120 KB → 103 KB。

### file chooser の置き換え（commit 43beae3a・03109c93）

- 両方の app の中の chooser（`chooser.c`、view の key・click・wheel の処理、`draw_chooser`、layout と定数）を消し、`kui_file_chooser` にした。
  view は「chooser を求める」だけ（`choosing`／`chooser_open` と始めの folder `chooser_folder`、log `CHOOSER open folder=`）、`main.c` がそれを見て
  chooser の窓を開き（filter: PDF Viewer は「PDF Documents」（pdf）と「All Files」、Image Viewer は「Images」（png jpg jpeg jpe gif）と「All Files」、
  viewer の font）、答えを `pv_app_chosen`／`iv_app_chosen`（NULL は取り消し、log `CHOOSER chose path=`・`CHOOSER cancelled`）に渡す。
  他に `chooser.c` を使う所が無いことを grep で確かめた。
- 試験の変更（Q1 の許可の範囲）:
  - `plan/ws079/tests/host-pdfviewer.c`: 「Ctrl+O で chooser（`app.choosing`）」「Esc で閉じる」の 2 件を、「Ctrl+O で文書の folder の chooser を求める」
    「取り消しで文書が残る」「選んだ file が開く（3 頁、path が一致）」の 3 件に替えた（確かめる中身は強めた）。frame `12-chooser` は残した。
  - `plan/ws079/tests/run-pdfviewer-host.sh`: compile の列から `chooser.c` を除き、冒頭の注記を直した。
  - `plan/tools/imageview/run-host.sh`: compile の列から `chooser.c` を除き、注記を足した（`host-imageview.c` は chooser を試していない）。

## 試験

### host

| 試験 | 前 | 後 |
| --- | --- | --- |
| `plan/ws079/tests/run-pdf-render.sh 300` | ok | —（libpdf は変えていない） |
| `plan/ws079/tests/run-pdfviewer-host.sh`（plain・ASan） | ok（51 件 ×2） | ok（52 件 ×2）。frame は `12-chooser` 以外すべて byte で同じ（`12-chooser` は app の中の chooser が無くなった） |
| `plan/tools/imageview/run-host.sh` | PASS（20） | PASS（20） |
| libkeiui: `host-input` 63/63、`host-draw` 13/13、`host-widgets` 94/94、`plan/tools/keiui/host-chooser.sh` 85/85、`plan/tools/textedit/host-core.sh` 34/34、`plan/ws102/tests/host-inset.sh` 10/10 | — | PASS |

### QEMU（Venus、この worktree で build した image）

- 環境の注記: この worktree には `build/ws035-sq-venus`（strict-queue の virglrenderer）が無く、最初は Venus の device が作れなかった
  （`ZWL VULKAN_ERROR operation=device result=-3`、main の main-pen の image でも同じ）。main の `build/ws035-sq-venus` への symlink を置いて直した。
- **demo-s8-s9.sh**（`plan/ws079/tests/config-amd64-demo.mk` の image。libkeiui は **package の依存の閉包で入った**（config は変えず、rootfs の
  `lib/libkeiui.so` が build した物と一致。closure に無い `libbrowser.so` は入らない）):
  - 前（同じ image に移行前の `/bin/pdfviewer` を入れた `demo-base.img`）: 1 回目は S9 b の指の turn 9/10・double tap が MISSING で FAIL、
    2 回目 **PASS**（scroll 142 ms・page frame 134 ms・turn 458 ms）。
  - 後（`demo-new.img`）: **PASS**（scroll 136 ms・page frame 129 ms・turn 437 ms）。画面 `build/ws090-shots/p8-demo-new/`。前後の `s9-first.png` は
    窓の中が同じ（違いは system bar の時計だけ）。
- **`plan/ws090/tests/viewers-p008.sh`**（新規）: **PASS**。PDF Viewer: password の card（`pdf-card.png`）、左下の角の swipe で QWERTY →
  `KEYBOARD inset right=0 bottom=324 reason=2`、card が keyboard の上に移る（`pdf-card-keyboard.png`）、閉じると inset 0、`secret` と Enter で開く、
  Ctrl+O で libkeiui の chooser（`pdf-chooser.png`）、`a`・Enter で `a4.pdf`（10 頁）、Ctrl+O・Esc で取り消し。Image Viewer: Ctrl+O の chooser
  （`iv-chooser.png`）、`0`・↓↓・Enter で `03-portrait.jpg`、取り消し、F で全画面（`FULLSCREEN state=1`、`kui_window_fullscreen`）、Esc で戻る。
  `ZWL ERROR` 0、FAILED 0。画面 `build/ws090-shots/p8-viewers/`。
- **Image Viewer の既存の guest 試験**（`plan/tools/imageview/imageview-guest.sh` の全 step と `touch-guest.sh`、同じ demo の image、BIN を差し替え）:
  - 前（移行前の imageview、`ws090-widgets` の build の複写）: imageview-guest 全 ok、touch-guest は 2 回とも「the view glided on after the lift」
    だけ FAIL（20 px・-43 px）。
  - 後: imageview-guest 全 ok、**touch-guest PASS**（glide 168 px）。画面の比較: empty・fit・next・zoom・portrait・alpha・pixels・broken・fullscreen は
    system bar の時計の分（約 2400 px）だけの違い、chooser は新しい chooser（`build/ws090-shots/p8-iv-base/chooser.png` と `p8-iv-new/chooser.png`）。

### C9・boot・規約

- WS099 の C9（`plan/ws099/tests/criteria.sh build/ws090/criteria.img build/ws090/criteria C9`、この worktree の criteria の image）: 1 回目は
  8/10（p052 は 14 秒で guest の準備の前に始まり画面が黒、p076 は `widened.png` の 1 枚の画素）。両方とも viewer と libkeiui を使わない
  compositor の試験で、`C9_TESTS="p052 p076"` の再実行で **2/2 PASS**（`build/ws090/criteria2/results.txt`）。
- build（`-Werror`）: `libkeiui.so`・`/bin/pdfviewer`・`/bin/imageview`・`/bin/textedit` exit 0、warning 0（最後の main の merge の後）。
- 最後の merge の後の host: run-pdfviewer-host ok、imageview run-host PASS、host-input 63/63、host-draw 13/13、host-widgets 94/94、
  host-chooser 85/85、textedit host-core 34/34、host-inset 10/10。
- main の colour emoji（KUI_VERSION 9）の後、`plan/ws090/tests/host-widgets.sh` が link で落ちていた（`keiland_color_glyph`）→ compile の列に
  `picture/color-glyph.c` と libz-compat・libpng-compat と compat の include を足した（WS090 の試験、commit dc244ce2）。`plan/tools/textedit/host-core.sh`
  の同じ誤りは Q1 が main で直した。
- 規約: `style-check.py`（pdfviewer・imageview の .c・.h、libkeiui の window.c・window.h）違反 0、`git diff --check` 0。
  `plan/ws079/tests/host-pdfviewer.c` の既存の違反（前からの test の書き方）は足した行には無い。
- boot test: `build-ssh-image.sh build/amd64`（exit 0、log の warning に libkeiui・pdfviewer・imageview は 0）→ `boot-test.sh` **PASS**
  （`build/ws090/boot-p008/login.png`）。
- QEMU の試験は merge（WS102 の編集の操作と colour emoji）の前の tree で行った。その後の merge で viewer の source は変わっていない。

## 判断と残り

- PDF Viewer・Image Viewer の touch（pinch の zoom、頁・画像の swipe、double tap の zoom）は `kui_ui` に無いので、各 app の `touch.c`（libkeiland の
  gesture と scroller）を残し、入力の経路だけ `kui_window` にした。view の scroll の model（`scroll_x`・`scroll_y`、wheel は即座に動く）も
  既存の host 試験が数値で確かめているので変えていない。`kui_scroll` に替えると wheel が glide に変わる（見た目の変更）。後の候補。
- password の card を `kui_dialog`＋`kui_field` に替えるのは後の候補（Q1 の指示）。
- chooser の見た目の違い: 前の app の中の chooser は今の画像を選んだ状態で開いたが、`kui_file_chooser` は選択なしで開く。
- 実機は未実施。
