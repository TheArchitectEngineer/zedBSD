<!-- awesome-plan project=zedbsd record=ws175-p008 -->
# ws175-p008: Notes の UI（画像の段: Select の道具・画像の挿入と差し替え・削除・Reset）

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 P2: 画像の段を実装、build と host 試験 PASS。main に merge 3d42efb00。画面は T1-253（AAT の pdf-edit-image・pdf-insert-image、draft）の結果で Q1 が判定）
Disposition: normal
Queue: Q1 の順（2026-10-06「画像の段の UI の p008 を先に、その後 p004・p005」、D6 (b)）
依存: [p007](../phase007/phase.md)（cleared）

## 範囲（design.md §7 の画像の部分）

道具（Select）、選択と handle、移動と大きさの drag、画像の挿入（file chooser）と差し替え、削除・Reset、key、描画（編集の在る page の背景）、
menu、log [L9]、AAT の draft の手直し。文字（Text の道具・編集の box・IME・font の picker）は p004・p005 の後。

## 実装（2026-10-06 P2）

- **道具と toolbar**（`ui.c`・`app.h`）: Pen・Marker・Eraser の後に **Select**。Select の時は色と太さの代わりに **Image**（挿入）・**Replace**・**Delete**・**Reset**
  のボタン（押せない時は淡い）。design.md §7.2 の「物の上の操作の帯」は、文字を描ける toolbar の中のボタンにした（設計からの変更、下）。
  `notes_ui_state` に can_insert・selected・can_replace・can_reset。font の読み込み（`notes_ui_open`）は触っていない（P1 の q812）。
- **menu**（`menu.c`）: Edit に Insert Image…・Replace Image…・Delete・Reset、Tool に Select（radio）。状態で有効・無効。
- **入力**（`main.c`）: Select の press は選んだ物の handle（四隅から 12 px 以内）なら大きさ、物の上なら選んで移動、何も無ければ選択を外す。
  文字の行は選ばない（p004 の後）。読めない stream の page は「This page cannot be edited…」。drag の motion は shown space の写像（移動、または対角の
  隅を固定した拡大縮小。縦横比を保つ、Shift で縦横別、最小 4 pt・最大 page の 4 倍）、release で 1 つの変更（元の物は PLACED の変換の後に写像、挿入した
  物は置き方の後に写像）。押しただけ（動かない）は何も変えない。key: Delete・Backspace で削除、矢印で 1 pt（Shift で 10 pt）移動、Esc で選択を外す
  （無ければ今までどおり fullscreen を出る）。道具・page の変更、undo・redo で選択を外す。
- **画像の file**（新規 `picture-file.c`）: JPEG は frame header の大きさと成分、APP1 の EXIF の向き（`picture/picture.c` の
  `kl_picture_exif_orientation`、[L6][N18]）、bytes はそのまま。CMYK・YCCK は ENOTSUP [M15]。PNG は 8bit Gray・RGB で alpha・palette・tRNS・interlace
  が無ければ bytes のまま（libpdf が行を素通し）、それ以外は libpng-compat で RGBA に解いて圧縮（[N3]）。一辺 16384・64 M 画素を超えると E2BIG。
- **挿入と差し替え**: file chooser（題 Insert Image・Replace Image、Images の filter）。挿入は見えている page の部分の中央に、page の幅・高さの半分と
  72 dpi の大きさ以内（向き 5〜8 は縦横を入れ替え、§5.3）、Select の道具で選んだ状態にする。差し替えは選んだ物の flags に IMAGE。
  chooser の目的（PDF・挿入・差し替え）は chooser を開いた時だけ記録する（開いている間に別の目的で押しても上書きしない）。
- **描画**: 編集の在る page（と drag 中の page）の背景は page の editor の `pdf_page_editor_render`（Notes 自身の page は blank の editor）。
  `app_look`（文書の `edit_serial` と drag の preview の数）が変わると背景と page の絵を描き直す。drag の間は 100 ms ごとに editor に仮の置き方を入れて
  描き直す（design.md §7.3 の「物だけの texture を GPU で動かす」の代わり。下）。選んだ物の青い枠（2 px）と四隅の 8 px の handle を描く。
  drag を落とした時（abort）は editor を作り直して仮の置き方を捨てる。model 側は `edit.c` の put・take で `reshaped` と `edit_serial` を増やす。
- **log**: `NOTES TOOL N name=pen|highlighter|eraser|select`（eraser は `parts=` の後）、`NOTES EDIT select page= object= kind=image|graphic inserted= clipped=`、
  `NOTES EDIT move … dx= dy=`、`resize … sx= sy=`、`replace … image=WxH`、`insert page= kind=image object= image=WxH`、`delete`、`reset`、`undo|redo page= edits=`、
  `NOTES SAVE … strokes= edits= edited_pages= bytes= path=`、開いた後の `NOTES EDITS opened edits= edited_pages= rebased=0|1`。
- **build**: Notes の 3 つの Makefile に `picture-file.c`・`picture/picture.c` と libpng-compat・libjpeg-compat・libgif-compat、`platform/amd64/vmunix.mk` の
  Notes の link の規則にも同じ 3 つ（picture.c が libjpeg と libgif を参照する）。
- **AAT の draft**: `tests/scenarios/apps/notes/pdf-edit-image.md`・`pdf-insert-image.md` を実装の操作と log に合わせた（status は draft のまま、p010 で active）。

## 設計からの変更（Q1 に報告）

- 操作の帯（物の上に浮く帯）は toolbar の Select の時のボタンにした（文字を描く仕組みが toolbar にしか無いため。機能は同じ）。
- drag の描画は「物だけの texture を GPU で変換」ではなく、100 ms ごとに page を描き直す（CPU）。大きな page では drag が重くなりうる（p010 で確かめる）。
- `NOTES OPENED … edits= rebased=` は既存の `NOTES OPEN … kind= path=` を試験が完全一致で読むため、別の行 `NOTES EDITS opened …` にした。
- [M8] 指の long-press の drag は未実装。指は今までどおり scroll（toolbar の Finger を入れると 1 本の指が pointer と同じに選択・移動する）。

## 試験と結果（host、2026-10-06）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>`（新規 `host-notes-picture.c` 12 項目を追加: EXIF の向き 6 の JPEG、RGB の PNG はそのまま、RGBA と palette の PNG は圧縮した RGBA、半透明、CMYK と text は ENOTSUP、無い file は ENOENT、各画像を挿入して page の editor が描く） | notes-edit 33/33・picture 12/12 ×3（plain・ASan・UBSan）、PASS |
| build: zedBSD の `bin/notes`（config-amd64-zdesktop、-Werror）、keiland-linux の `bin/notes` | warning 0 |
| `python3 plan/tools/style-check.py userland/desktop/notes/*.c` | 0 |
| `python3 plan/tools/aat/check-scenarios.py` | PASS |

## 未実施・残り

- 画面の確認（Select・drag・handle・挿入・差し替え・削除・Reset・undo、保存と開き直し）: 未実施。QEMU は p010 で T1（AAT の pdf-edit-image・pdf-insert-image）。
- [M8] 指の long-press、[N13] 表示中の page の前後の外の editor を捨てること（今は各 page の editor を残す）、[M13] の autosave の cache と時間。
- 文字の UI（Text の道具、編集の box、IME [M7]、font の picker、Edit Text・Font・Size のボタン）は p004・p005 の後。
