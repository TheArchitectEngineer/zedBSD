<!-- awesome-plan project=zedbsd record=ws093-design -->

# WS093 の設計: Files から app の起動（file の種類と app の対応）

2026-09-29 ws093-p001。目標は [ws.md](ws.md)（ユーザー「ファイラーからアプリの起動（画像、テキストをダブルクリック）」、デモに必須。
main の指示: まず画像と text の double click の起動、menu と上書きはその後）。

## 1. 今の Files（WS071 の到達点、調査の結果）

WS071 は開く仕組みをすでに持つ。この WS は仕組みを作り直さず、既定の対応と、上書きを画面から選ぶ操作を足す。

| 部分 | 所在 | 今の振る舞い |
| --- | --- | --- |
| 種類 | `files/mime.c` の `fm_mime_guess`（拡張子）・`fm_mime_sniff`（中身） | png・jpeg・gif・ppm などは `image/*`、txt・md・csv は `text/*`、`html`・`htm` は `text/html`、json・xml・code は `text/x-*`・`application/json` 等、pdf は `application/pdf` |
| 対応の表 | `files/apps.c` の `fm_apps_for` | 1 行 `PATTERNS<TAB>NAME<TAB>COMMAND` の 2 つの一覧（利用者の `$XDG_CONFIG_HOME/keiland/open-with`、無ければ `~/.config/keiland/open-with`、system の `/etc/keiland/open-with`）と、組み込みの表（`apps_builtins`）を順に並べ、最初が既定。一覧は開くたびに読む（編集はすぐ効く）。組み込みの行は `needs` の program が `/bin`・`/usr/bin`・`/usr/local/bin` に無ければ出さない |
| 組み込みの表 | 同上 | PDF → PDF Viewer、`image/*` → Quick Look、text → Terminal（less）・Remacs・Terminal（ed）、その他 → Terminal（less） |
| 起動 | `fm_apps_launch` | `%f` に shell 用に quote した path を入れ、fork した子が孫を起こして抜ける（孫は Files の子でない）。`LAUNCH`・`OPEN path=… app=…` の log、状態の pill「Opening "x" with Y」、最近の file に記録 |
| double click・Enter | `ui.c`（folder はその tab で開く、file は `fm_open_entry(app, index, 0)` = 既定） | spec §14 のとおり |
| touch | `touch.c` | tap は左 button の click になる。2 回の tap は double click になるので、double tap は double click と同じ |
| 「このアプリで開く」 | File menu と context menu の **Open With**（`menu.c`・`ui-context.c`・`ui-menu.c`）、情報の card の opener の pill（`ui-info.c`） | 対応の表の全ての way を並べ、選んだ way で開く |

したがって、画像と text の double click が今は Quick Look と terminal の less になるのは、組み込みの表の既定のため。

## 2. 既定の対応（p002）

組み込みの表（`apps.c` の `apps_builtins`、Files の中の system の既定）に行を足し、既定の順を変える。`needs` があるので、program の無い
image では従来の way が既定のまま（host の試験・lean な image でも振る舞いが変わらない）。

| 種類（PATTERNS） | 既定（1 番目） | 続く way |
| --- | --- | --- |
| `application/pdf` | PDF Viewer（`/bin/pdfviewer %f`、needs pdfviewer） | 従来どおり |
| `image/png,image/jpeg,image/gif` | **Image Viewer**（`/bin/imageview %f`、needs imageview） | Quick Look |
| その他の `image/*`（bmp・webp・svg・ppm・pgm。Image Viewer は magic で png・jpeg・gif しか読まない） | Quick Look | — |
| `text/html` | **Browser**（`/bin/browser %f`、needs browser） | Text Editor、Terminal（less）… |
| text（`APPS_TEXT_TYPES`: `text/*`・json・xml・shellscript・javascript） | **Text Editor**（`/bin/textedit %f`、needs textedit） | Terminal（less）、Remacs、Terminal（ed） |
| その他 | Terminal（less） | — |

- 実行できる file（mode の x）は従来どおり「Run in Terminal」が 1 番目（`.sh` に x があれば実行が既定。x が無ければ Text Editor）。
- 引数: imageview・textedit・pdfviewer は `[FILE]` を最後の引数に取る。browser は `[URL]` に path を取る（App Home が `/bin/browser /usr/share/browser/start.html` で使う形）。
- 起動された app は Files の環境（`XDG_RUNTIME_DIR`・`WAYLAND_DISPLAY`・`HOME`）を継ぐ。
- 表示名は app の画面の名前に合わせる（「Image Viewer」「Text Editor」「Browser」「PDF Viewer」。画面に Keiland を出さない）。

受け入れ（p002）: Venus の guest の Files で、png と jpeg を double click → Image Viewer がその画像で開く（`IMAGEVIEW SHOW path=…`）、
txt を double click → Text Editor がその file で開く（`TEXTEDIT OPEN`）、html → Browser、pdf → PDF Viewer。Enter でも同じ。注入の touch の
double tap で画像と text が開く。File > Open With と context menu に新しい way が並ぶ。画面を `build/ws093-shots/` に撮る。

## 3. 利用者の上書きを画面から選ぶ（p003）

上書きの仕組み（利用者の一覧）はあるが、今は file を手で書くしかない。「Always Open With」を足し、選んだ way を利用者の一覧に書く。

- **menu**: File menu と context menu の「Open With」の隣に submenu「Always Open With」（その file の way を並べ、最後に区切りと「Use System Default」）。
  選ぶと、その種類（`fm_mime_sniff` の type ちょうど）の既定をその way にし、同じ file を今それで開く。
- **書き方**: 利用者の一覧の中の Files が書いた行（直前の行が `# set by Files` の注釈）だけを扱う: 同じ type の Files の行を消し、先頭に
  `# set by Files` と `TYPE<TAB>NAME<TAB>COMMAND` を足す。利用者が手で書いた行と注釈はそのまま残す。「Use System Default」は Files の
  その type の行を消すだけ。書き込みは同じ folder の一時 file に書いて `rename`（途中で落ちても一覧が壊れない）。folder が無ければ作る。
- 一覧の行の数の上限（開くたびに全部読むので、Files の行は type ごとに 1 行）。書けない時は状態の pill に「Couldn't change the default app.」。
- 情報の card の opener の pill は今のまま（選ぶとその way で開く）。

受け入れ（p003）: host の試験（一時の `XDG_CONFIG_HOME` で、書いた行が次の `fm_apps_for` の 1 番目になる、手書きの行が残る、Reset で戻る、
二度書いても 1 行）、guest で txt を Always Open With → Terminal（less）にすると double click が less で開き、Use System Default で Text Editor に戻る。

## 4. 範囲外

- system の一覧 `/etc/keiland/open-with` を image に入れること（組み込みの表が system の既定を担う。入れるなら main の config）。
- MIME の表そのものの追加（WebP・HEIC 等の新しい形式）。app 側の新しい形式の対応。
- Files 以外（App Home・Terminal）からの「開く」。
- libkeiland・textedit・imageview・browser の source（他の担当）。textedit が引数の path を開けない時は main に報告する。

## 5. Phase

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | §2 の既定の対応。host の試験（`fm_apps_for` の順、program の有無）、Venus の guest の double click・Enter・注入の touch の double tap、画面 | p001 |
| p003 | §3 の Always Open With と利用者の一覧への書き込み。host と guest の試験 | p002 |
| p004 | 全文の規約（`plan/coding-style.md`）と回帰（WS071 の Files の回帰 `plan/tools/files/` の該当、boot test） | p003 |

## 6. 見つけたこと（main へ）

- `plan/tools/files/host-model.c` の section 17（opening）は利用者の一覧を `$XDG_CONFIG_HOME/zdesktop/open-with` に書くが、`apps.c` は
  `keiland/open-with` を読む（名前の変更に試験が追いついていない可能性）。p002 で host の試験を流して確かめる。直すなら `plan/tools/` は main の担当。
