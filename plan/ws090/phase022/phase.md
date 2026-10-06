# ws090-p022: 全ての文字の入力で IME を受け付ける、自前の text box を libkeiland の部品へ

Status: in-progress（q816、P1。1〜5・7 は実装と host の確認まで、6 は WS131 p025 の後。T1 の結果待ち）
WS: [WS090](../ws.md)
Related: [WS095](../../ws095/ws.md)（IME）

## 出典

2026-10-06 ユーザー:「テキスト入力のあるすべてのアプリで、IMEを受け付けることをチェックしてください。また、テキストボックスはlibkeilandのUI要素で実現していると理解していますが、逸脱して自前で用意しているKeilandアプリがあれば教えてください。特に理由がなければlibkeilandのUIパーツを使ってほしいです。」
Q1 が source を調べた（読むだけ、2026-10-06）。IME を受けるのは libkeiland の `kl_field`（ただし app が `kl_ui_text`・`kl_ui_text_wanted`・`kl_window_text_input` を自分で結線した時だけ、`kl_app` は結線しない）と、自前で扱う Text Editor の本文・Terminal、compositor の title bar の欄（検索・breadcrumb）。

## 直す所（秘密の欄は IME 無しが正しいので除く）

1. `kl_app` が `kl_field` の IME（text の event の配達、`kl_ui_text_wanted` で text input の on・off と caret）を自動で結線する。→ file chooser（場所・Save As の名前、全 app）、Text Editor の置換の dialog が直る。
2. 複数行の text の部品を libkeiland に足し（IME・caret・選択）、Mailer の本文と Calendar の日の memo を置き換える（今は ASCII だけ・IME 無し）。
3. Files の改名の欄（自前の `fm_field`）を `kl_field` に。
4. Settings の `se_field`（自前、ASCII だけ）を `kl_field` に（秘密の欄は `kl_field` の秘密の mode で IME 無し）。管理の新しい利用者の名前・氏名で日本語を打てるように。
5. Terminal の検索の欄の preedit と候補の位置を検索の欄に。
6. Browser の web の form（input・textarea）に text input を結線（libbrowser の shell）。
7. App Home の検索で IME を受ける（今は compositor が意図して外している。日本語の app 名・翻訳の後に要る）。
- 秘密の欄（Wi-Fi の鍵・password・PIN・PDF の password・greeter・lock）は IME 無しのまま。
- 試験: host の試験と、T1 で各欄に日本語を打つ guest の試験（ime-probe 系）。

## q816（P1、2026-10-06）

### 1. libkeiland の結線（KL_VERSION 47）

- `kl_app` は `kl_ui` を持たないので自動にはできない。代わりに 2 つの呼び出しで結線を 1 行ずつにした: `kl_ui_window_input(ui, event)`（窓の pointer・button・wheel・key・touch・text input の `KL_WINDOW_TEXT_*` を部品へ、部品の input なら 1）と `kl_ui_window_text(ui, window)`（frame の後、focus の部品が text を取る間は text input を on にして caret を伝え、他は off。秘密の欄は取らない）。`ui/text-input.c`。
- file chooser（libkeiland、全 app）: event を `kl_ui_window_input` に、frame の後に `kl_ui_window_text`。場所と Save As の名前の欄が IME を受ける。
- Text Editor の置換の dialog: dialog の間は `KL_WINDOW_TEXT_*` を dialog の `kl_ui` へ、frame の後の caret は dialog の欄の物（`main_text_caret`）。本文の text input は dialog が無い時だけ。

### 2. 複数行の部品 `kl_text_area`（`libkeiland/ui/text-area.c`、新規）

- 行の折り返し（空白で、無ければ文字で）と改行、Left・Right・Up・Down（横の位置を保つ）・Home・End（行の）、Shift の選択、Ctrl+A、Backspace・Delete、Enter は改行、Esc は `KL_FIELD_CANCELLED`。IME の commit（改行は保つ）・前後の削除・caret の位置の preedit の下線。click で caret、double click で全選択。caret が見えるように縦に scroll。`KL_TEXT_AREA_MAX` 8192。
- Mailer の本文（前は末尾に足すだけ・ASCII だけ・IME 無し）と Calendar の memo（同じ）を置き換えた。返信は先頭の空行に caret。

### 3・4. Files と Settings（自前の欄に IME、kl_field への移行は ws090-p009 の後）

- Files と Settings は libkeiland の部品の層で描いていない（自前の canvas・文字・入力、`fm_canvas`・`se_*`）。`kl_field` に置き換えるには canvas の移行（旧 ws090-p009、D9 で WS131 の後）が要る。それまでの間、自前の欄に IME を足した（Q1 に報告、既定の (a)）。
- Files: 名前の変更の欄（`fm_field`、grid・list・desktop）が commit・削除・preedit（caret の位置に下線）を受け、改名の間だけ text input を on にして caret を伝える（`main_text_input`）。名前に `/` と制御文字は入らない。場所と検索の欄は compositor の title bar の欄（既に IME あり）。
- Settings: 管理の新しい利用者の氏名の欄が IME を受ける（`se_users_admin_text`・`_text_wanted`）。login 名・password・管理者の password は key で打つ（秘密は IME 無し、login 名は ASCII）。`se_field` の Backspace を 1 文字（UTF-8 の全 byte）に。Wi-Fi の鍵・PIN・PDF の password などの秘密の欄は変えない。

### 5. Terminal の検索の欄

- 検索の bar が開いている間、preedit を bar の caret（"Find: " と検索の文字の後、最後の行）に描き、IME の caret の矩形もそこに（`terminal_search_bar_column`、`render_preedit`・`main_text_cursor`）。preedit は bar の上に描く。

### 6. Browser の form（未着手、WS131 p025 の後）

- browser の shell は自前の Wayland の窓（`browser/shell/window.c`）で text input が無く、libbrowser にも text の入力の口（commit・preedit・caret の矩形・編集中か）が無い。WS131 p025（shell の窓を libkeiland に）の後、libbrowser（WS074）に口を足して結線する。

### 7. App Home の検索

- compositor の自前の欄（`ime_field`）に App Home の検索を足した: Home が開いている（開いていく）間、IME は検索を serve する（surface は無く、矩形は画面の座標。候補の窓もそこ）。key はまず IME へ（`kwl_ime_home_key`、IME が返した key は `kwl_home_key`）、commit は検索の末尾、削除は末尾の前、preedit は検索の後に下線で描く。Home の開閉と検索の変化で IME に状態を伝える。検索の長さを 48→96 byte に、Backspace は 1 文字。log: `ZWL IME activate field home`。

### 確認

- build: zedBSD amd64 の libkeiland・textedit・mailer・calendar・terminal・files・settings・wayland（exit 0、warning 0）、`make keiland-linux` の gcc と clang（exit 0、warning・error 0）、`exports.py --check` OK、`keiland-os-boundary/check.sh` PASS。
- host: run-host-mailer（10 PASS、本文に "hello"）、run-host-calendar（9 PASS、memo の click を行の末尾に直した: click が caret を置くので）、host-widgets 94/94、host-chooser 85/85、run-host-phone PASS、terminal-p006（rm を除いて直に）PASS、files-render の改名に commit と compose（`build/ws090-p022/rename.png`: base日本語、語が下線）、host-account-admin 34/34、host-settings 38/38。`plan/tools/files/host-render.c` に `commit=`・`compose=` を足した。
- 未実施（T1）: 各欄に日本語を打つ guest の試験（keiland-ime を選んだ image）: file chooser の Save As の名前、Text Editor の置換、Mailer の本文、Calendar の memo、Files の改名、Settings の新しい利用者の氏名、Terminal の検索、App Home の検索。
