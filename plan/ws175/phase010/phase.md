<!-- awesome-plan project=zedbsd record=ws175-p010 -->
# ws175-p010: T1 の QEMU で Notes の PDF の編集の AAT 5 本

Parent: [WS175](../ws.md)
Status: planned（2026-10-06 P2: 依頼の準備（補助・試料・シナリオの active 化）を済ませ、T1 の依頼を Q1 に送った。結果待ち）
Disposition: normal
Queue: Q1 の順（2026-10-06「その後 p010 の T1」）
依存: [p008](../phase008/phase.md)（画像の段 cleared、文字の段は host PASS で判定待ち）・p004・p005（cleared）

## 範囲（design.md §10・§11.2）

T1 の QEMU（Venus）で AAT の 5 シナリオを 1 回の QEMU の起動で流す: `apps.notes.pdf-edit-image`（T1-253 で確かめられなかった resize の画面を含む）・
`apps.notes.pdf-insert-image`・`apps.notes.pdf-edit-text`・`apps.notes.pdf-insert-text-font`（ws079-p017 の IME の text box を兼ねる）・
`apps.notes.pdf-edit-multipage`。FAIL の直しは 1 回分。p009（Save Clean Copy）は第 2 段に回す（2026-10-06 Q1）。

## 準備（2026-10-06 P2）

- **シナリオを active に**: runner（`run-aat.sh`）は active しか選ばない（T1-253 は draft で 0 本だった）。5 本の status を active にし、準備の節を
  補助の動きに合わせた（`tests/scenarios/apps/notes/pdf-*.md`）。
- **補助**（新規 `plan/tools/aat/scenarios/helpers_notes_edit.py`、Q1 の指示「helper を書いて active にするか」）: 各シナリオの手順を AAT の命令で行い、
  log の行で確かめ、撮影し、保存した PDF を host に取って `qpdf --check`・`pdftotext`・`pdffonts`・`pdfimages -list`・`pdftoppm` で読む。見た目は
  needs-person（撮影を Q1 が見る）。
  - 試料: host で `plan/ws175/tests/make-edit-samples.py` が新しく書く `edit-basic.pdf`（3 頁の US Letter。1 頁は DejaVu Sans を段落の字だけに subset
    した TrueType の 4 行と JPEG、2 頁は Helvetica の 1 行、3 頁は `/Rotate 90` の 2 行）と、PIL の `picture.png`（alpha つき）・`landscape.jpg`。
    target の `/tmp/aat-work/notes-<id>-<時刻>/` に置き kei の持ち物にする（scenario ごとに別の folder: 前の run の journal を拾わない。file chooser は
    名前の頭の字と Enter で選ぶ）。image には何も足さない（試験の image の規則）。
  - 座標: Notes の `NOTES LAYOUT`（頁の位置と scale）・`NOTES BUTTONS`（toolbar の button）・窓の位置、試料の配置から。
  - IME: `apps.terminal.japanese-history` と同じ（Settings で method 1、Alt+Space で ja、`nihongo`・Space・Enter）。終わりに元の method に戻す。
- **Notes**: `NOTES BUTTONS` を幅ごとに 1 回ではなく、button の並び（道具ごとに変わる）が変わる度に出す（補助が Select・Text の時の button を引けるように）。
- **libpdf の直し**（試料で見つけた）: 元の font で書けるかの判定（[L5]）が、cmap に無い字の glyph 0（.notdef、輪郭が有る）を「書ける」と見ていた。
  subset の font に無い大文字が豆腐で書かれるところだった。`struct pdf_glyph` に `missing`（glyph 0）を足し、`editor_encode` は missing を拒む
  （置き換えの font になる）。
- host 試験: `host-notes-edit.c` に edit-basic.pdf の 5 項目（頁の物の数、1 行目の文字と編集可、「… lazy fox」は元の font のまま、ZEBRA は置き換え、
  3 頁の行は編集可）。

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-notes-edit.sh <scratch>` | notes-edit 55/55・picture 12/12 ×3、qpdf ok、PASS（直しの前は ZEBRA の項目が FAIL） |
| `sh plan/ws175/tests/run-host-edit-scan.sh <scratch>` | PASS |
| `sh plan/tools/aat/tests/run-host.sh` | aat-host: PASS |
| `python3 plan/tools/aat/check-scenarios.py`・`helpers_notes_edit.py --list` | PASS（91）、5 本 |
| build: zedBSD の libpdf.so・`bin/notes`（-Werror）、keiland-linux の `bin/notes` | warning 0 |
| style-check（notes の *.c、libpdf の font.c・editor.c） | 0 |

## T1 への依頼（Q1 経由）

AAT の image（`plan/tools/aat/config-amd64-aat.mk`、main の最新）を QEMU（Venus）で、1 回の起動で:

    plan/tools/aat/run-aat.sh qemu OUT 'apps.notes.pdf-*'

見る所: 各シナリオの verdict（`OUT/verdicts.tsv`）・record・撮影・`OUT/logs/<id>.log`・`OUT/errors.txt`。特に pdf-edit-image の resize の行
（`NOTES EDIT resize … sx=S sy=S`、S > 1）と resized の撮影、pdf-insert-text-font の composing（IME の preedit と候補の位置）の撮影。
needs-person は撮影を Q1 が見る。FAIL は直さずに log と撮影を返す。

## 未実施・残り

- QEMU の結果（T1）。補助は host の自己試験と `--list` までで、実の target では走らせていない（座標の計算・chooser の頭の字・IME の手順は T1 の
  run で初めて確かめる）。

## 積み残し（準正常系・異常系、WS177 へ）

- 無し（この Phase は試験の準備。Notes の文字の UI の積み残しは [p008](../phase008/phase.md) の節）。
