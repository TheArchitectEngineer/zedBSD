<!-- awesome-plan project=zedbsd record=ws035p122 -->

# ws035-p122: Notes の toolbar の間隔と、窓の title の区切りを揃える

Phase ID: `ws035-p122`
Parent: [WS035](../ws.md)（Notes・PDF Viewer は [WS079](../../ws079/ws.md) の app。見た目の小さな修正だけをこの WS で行う）
Status: cleared（2026-09-29、サブエージェント。QEMU の Venus と host。実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「Notes の toolbar」、demo critical）

## 範囲（依頼）

[p116](../phase116/phase.md) の main への一覧の 1 と 2:

1. Notes の toolbar: 「Marker」と「Eraser」の間だけ間隔が広い。
2. 窓の title の区切りが app で違う: PDF Viewer は「quilt.pdf - PDF Viewer」、Notes は「Notes — note-….pdf」。

## 原因と設計

1. Eraser の button は、mode の切り替え（stroke 単位 ⇄ 部分、[ws079-p011](../../ws079/phase011/phase.md)）で後ろの button が動かないように、
   長い方の label「Part Eraser」の幅を取り、「Eraser」を中央に置いていた。そのため「Eraser」の左に（幅の差の半分）の余白が出て、
   Marker との間だけ広く見えた。
   - 直し方: Eraser の label は他の tool と同じく button の左から 12 px、選んだときの pill は label に合わせる。幅の差（slack）は tool の group の
     後ろに回し、区切り線は「label の後ろ + (UI_GAP + slack)」の中央に置く。後ろの button の位置は mode に依らず前と同じ
     （p011 の「幅は status・mode に依らない」、`NOTES BUTTONS` を幅ごとに 1 回読む試験の前提を保つ）。
   - 「Part Eraser」の間は slack が 0 で、区切り線は他の group と同じ間隔。
2. 区切りは「文書の名前 — app の名前」（em dash）に揃える。文書の名前を先にするのは、title が狭い title bar で切れても文書が分かるため
   （PDF Viewer の title bar は「quilt.pdf — PDF Vie…」のように後ろが切れる）。Notes は `note-….pdf — Notes`、PDF Viewer は
   `quilt.pdf — PDF Viewer`。文書が無い間の title は今のまま（「Notes」「PDF Viewer」）。title を期待値に持つ試験は無い（grep で確認）。

## 実装（2026-09-29）

- `userland/desktop/notes/ui.c`: `ui_eraser_button()` の label を左寄せ・pill を label の幅に、`ui_eraser_slack()`（新: 使わない幅）、
  `ui_layout()` の tool の group の後ろの区切り線と間隔。
- `userland/desktop/notes/main.c`: `app_set_title()` を `"%s — Notes"`。
- `userland/desktop/pdfviewer/main.c`: `main_opened()` の title を `"%s — PDF Viewer"`（注釈の旧名 2 つも p121 として直した）。
- `plan/ws035/tests/p122-notes-eraser.sh`（新）: demo の image で compositor（1280x800）と `notes --fullscreen` を起こし、Pen・Eraser（E）・
  Part Eraser（E をもう一度、`NOTES TOOL 3 parts=1`）の 3 枚を撮る。

## 検証（2026-09-29、QEMU の Venus。実機は未実施）

- build warning 0、`style-check.py` notes/ui.c・notes/main.c・pdfviewer/main.c 違反 0。
- host: `plan/ws079/tests/run-notes-host.sh` ok（qpdf、Producer、`kei-notes.bin -> 9,0`）。
- QEMU: `p122-notes-eraser.sh` PASS（`NOTES TOOL 3 parts=0`・`parts=1`）。3 枚を並べて見て、Pen・Marker・Eraser の間隔が同じ、色の点から後ろの
  button は 3 つの状態で同じ位置、「Part Eraser」の pill は label に合う。
  画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p122-20260929-notes-{pen,eraser,parts}.png`。
- demo の通し（p121 と同じ run）: 06 の Notes の toolbar（1920x1280）、07 の PDF Viewer の title「quilt.pdf — PDF Vie…」
  （`p121-20260929-demo-06-notes.png`・`…-07-pdfviewer.png`）。
- 未実施: pen の guest 試験（`notes-p011.sh`・`notes-pen.sh`。demo の image に `/bin/peninject`・`/dev/input-inject` が無い。pen の image は WS079 p009 の残り）、実機。

## 残り

- 「Eraser」の間は tool の group の後ろの間隔が他の group より広い（slack の分、約 36 px を区切り線の両側に分ける）。気になるなら
  mode の表示を label 以外（小さな印など）にして button の幅を揃える案がある（WS079 の判断）。
