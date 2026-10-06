<!-- awesome-plan project=zedbsd record=ws175-p009 -->
# ws175-p009: Save Clean Copy（古い版を含めない copy の保存）

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 P2 が実装。build warning 0、host 試験 PASS。QEMU は T1 の結果待ち。ラップアップで返却、再開の情報は下）
Disposition: normal
Queue: q831（第 2 段、P2 の列の 1 番目、2026-10-06 Q1 ACK「p008 の text の段の判定を待たずに進めてよい」）
依存: [p008](../phase008/phase.md)（[N17]。libpdf の部分は p008 の判定に依らない。Notes の menu は p008 の main.c・menu.c の上）

## 範囲（design.md D1・[M11]・§9 の p009 の行）

読んだ文書を今の見た目で全体に書き直す writer（catalog から辿れる object の複写・番号の付け直し・object stream の展開・古い版を落とす・
page の content が参照しない resource を落とす [M11]）と、Notes の File の menu の「Save Clean Copy…」。正常系だけ（2026-10-06 Q1 の第 2 段の規則）。
元の埋め込みの font の program は subset のまま複写するので、使われなくなった glyph は残る（D1 の制限、設計どおり）。

## 実装（2026-10-06 P2）

- **libpdf `clean.c`（新規）**: `pdf_document_save_clean(document, path, attachment, &counts)`（`include/libc/pdf.h`、`exports.map`）。
  - 番号: catalog = 1、page tree の node = 2（全 page を Kids に並べた 1 つの node に平らにする）、page = 3 から順。他は参照の出会い順に番号を付けて
    queue で書く。古い page tree の node への参照は 2 に写す。読めない・無い object への参照は null。
  - page: 自分の entry（Parent・MediaBox・CropBox・Rotate・Resources を除く）、継承した MediaBox・CropBox・Rotate を page に書く
    （`pdf_reader_page_inherited`、reader.c に追加）。
  - resource の刈り込み [M11]: XObject・Font・ExtGState・Pattern・Shading・ColorSpace・Properties の entry のうち、page の content stream の
    name の token に現れない物を落とす（inline image の data は飛ばす）。content が読めない、または名指しされた form・Type 3 font・tiling pattern が
    /Resources を持たず page の物を借りる時は、その page は全部残す。
  - stream は元の符号化の bytes のまま、/Length は直接の数。object stream と xref stream はどこからも参照されないので落ちる。
  - attachment: 指定の名（Notes は `kei-notes.bin`）の file specification を EmbeddedFiles の name tree（/Names の対ごと）と catalog の /AF から落とす
    ので、copy は他の program の PDF（編集は焼き込み済み）として開く。
  - trailer: /Size・/Root・/Info・/ID（元の permanent の ID と新しい version）。1 つの xref の表。署名・暗号化の文書は update と同じく拒む。
  - update.c: `pdf_writer_write_name`・`pdf_writer_write_real`（writer.h）を clean.c のために公開。
- **Notes**: `save.c` の `notes_save_clean_copy(from, path, &bytes, &objects, &dropped)`（`.tmp` に書き、fsync、rename、folder の fsync）。
  `main.c`: File > **Save Clean Copy…**（`NOTES_ACTION_SAVE_CLEAN` 46、chooser の目的 `MAIN_CHOOSE_CLEAN`、題「Save Clean Copy」、既定の名
  `<名>-clean.pdf`）。選んだ path が notebook 自身なら「Choose another file for the clean copy」。変更が有るか未保存なら先に保存
  （`NOTES SAVE reason=clean-copy`）。notebook は自分の path のまま。log `NOTES CLEAN-COPY bytes= objects= dropped= path=`（失敗は
  `NOTES CLEAN-COPY failed error= path=`）、status「Saved a clean copy」、recent に足す。`NOTES CHOOSER open mode=clean`。
- Makefile（zedBSD・freebsd・linux）に clean.c。Notes の host の試験 3 本（`plan/ws175/tests/run-host-notes-edit.sh`、
  `plan/ws079/tests/run-notes-host.sh`・`notes-perf.sh`）の libpdf の source の一覧に clean.c を足した（save.c が呼ぶため）。

## 確認

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws175/tests/run-host-clean.sh`（新規、plain・ASan・UBSan）: 試料（`make-clean-sample.py`、qpdf で object stream に入れた物）の page 1 の画像 ImA を editor で削除し box を描いた update＋kei-notes.bin を保存、その clean copy | 29/29 ×3、`objects=12 dropped=6`。copy に ImA の bytes・/Prev・ObjStm が無い、kei-notes.bin が無い、2 page、継承の box（200×100）と自分の box（300×150）、page 1 は ImB だけ、ImA・F9 が落ち G1・F1・ImB が残る、page 2 の link の Dest が page 1（object 3）。`qpdf --check` OK、pdftotext と pdftoppm（72 dpi）が update と一致 → PASS |
| `sh plan/ws175/tests/run-host-notes-edit.sh` | PASS（host-notes-edit 55/55、picture 12/12） |
| `sh plan/ws079/tests/run-notes-host.sh` | ok |
| zedBSD: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-b194 build/p2-b194/dynamic/libpdf.so build/p2-b194/bin/notes`（-Werror） | warning 0、`pdf_document_save_clean` が libpdf.so の export |
| `make keiland-linux` | warning 0 |
| `plan/tools/style-check.py`（clean.c・reader.c・update.c・save.c・main.c・menu.c・host-clean.c） | 0 |
| QEMU（T1）| 未実施（依頼の文面は下） |
| 規約の全文の見直し | 未実施（第 2 段の後に WS177 と一緒、Q1 の方針） |

## T1 への依頼（未投入。Q1 が台帳に足す）

ws175-p009: Notes の Save Clean Copy（QEMU、p010 の補助 `helpers_notes_edit.py` と試料 edit-basic.pdf の上）。
image: p010 と同じ config で、この commit の libpdf.so と bin/notes を含む物。手順:

1. Notes で edit-basic.pdf を開く。
2. Select で page 1 の画像を選んで Delete し、Ctrl+S を押す。`NOTES SAVE` が出ること。
3. File > Save Clean Copy… を選ぶ。`NOTES CHOOSER open mode=clean`、既定の名 `edit-basic-clean.pdf` で保存する。
4. 合格の条件:
   - `NOTES CLEAN-COPY bytes=… objects=… dropped=N`（N ≥ 1）が出る。
   - status が「Saved a clean copy」になる。
5. 作られた copy を PDF Viewer（または Notes の Open）で開く。
   - page 1 に削除した画像が無く、他は元の見た目のまま（screendump の PNG）。
   - Notes で開いた時は `NOTES OPENED` で、他の PDF として開く（edit data が無い）。

補助の関数（menu の選択と chooser の操作）がまだ無ければ、`helpers_notes_edit.py` に足すのが再開の作業。

## 積み残し（plan/ws177/backlog-p2.md に行）

- form XObject・Type 3 font・pattern の中の resource は刈り込まない（page の resource だけ）。
- attachment を落とした後の name tree の /Limits を直さない。直接（参照でない）の file specification は落とせない。
- 一度だけの注意（D1 (a) の UI）・copy を開き直す提案は無い。

## 再開の情報（2026-10-06 ラップアップ）

- commit: 返却の報告の SHA（agent/p2、`git commit -m WIP`）。merge は Q1。
- 残り:
  1. T1 の結果。
  2. FAIL なら直す。
  3. 補助の関数が要れば `helpers_notes_edit.py` に足す。
  4. Q1 の判定で cleared。
