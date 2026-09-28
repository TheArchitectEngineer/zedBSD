<!-- awesome-plan project=zedbsd record=ws079-p014 -->

# ws079-p014: Notes で他の PDF に書き込む（背景と増分の更新）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、Kei desktop subagent。host と QEMU の Venus guest の証拠だけ。実機は未実施。clearance は main の判断で覆してよい）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: 下の「残り」
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. 自前の編集 data の無い PDF（今は ENOENT）と、content の hash が合わない page（今は ESTALE）を、libpdf で描いた背景にして上に線を足す。
   page の箱と回転に合わせた scale、p011 の page の画像の cache を保つ（背景は cache の画像の一部）。
2. 保存は元の file に足す増分の更新: 触った page の `/Contents` を配列（元＋新しい overlay）に、resources に overlay の ExtGState を足す。
   Kei Notes の編集 data は添付で、背景の page を hash で記録し、page ごとの stroke を持つ。元の bytes は変えない。開き直すと stroke は編集でき、
   PDF Viewer と poppler（pdftoppm）では元の上に線が見える。暗号化・署名つきは明確な message で拒む。writer に増分の API を足す（追加だけ）、host 試験、qpdf --check。
3. PDF Viewer の Annotate in Notes が stage ① の PDF すべてで動く。

## 設計（design-pdf.md §3 の具体化）

| 点 | 決めたこと | 理由 |
| --- | --- | --- |
| 増分の単位 | Notes は他の PDF を **base**（元の bytes）として持ち、保存のたびに「base の bytes そのまま＋1 つの revision」を書く。前回の Notes の revision は積まずに置き換える | autosave（5 秒）で revision が積もると file が際限なく大きくなり、libpdf の reader の /Prev の上限（32）にも届く。base は他の program の file そのものなので、元の bytes は常に先頭に残る |
| base の記録 | 編集 data（ZNOT 1.1）の `BASE` chunk に base の長さと SHA-256、`SRC ` chunk に page ごとの由来（元の page に上書き `OVER`・元の page を置き換え `REPLACE`）と base の page 番号。minor の増加なので 1.0 の Notes は飛ばす | design-pdf §2.1 の版の規則 |
| 開き直し | file の先頭 `BASE` の長さが記録の hash と一致し、file の最新の revision の /Prev が base の startxref を指す（＝Notes の revision だけが足されている）なら、base を取り戻して stroke を編集できる。他の program が後から revision を足していたら、その file 全体を新しい base にし、`OVER` の page の stroke は背景に焼き込まれ（編集不可、design の「古い stroke を黙って編集可能にはしない」）、Notes が作った page（hash が一致）は `REPLACE` で編集可能のまま | 他の program の変更を捨てない |
| 他で変わった Notes の file | 変わった page は背景（`OVER`、stroke なし）、hash の一致する page は stroke を保ち `REPLACE`（保存でその page の content を置き換える）。file 全体が base になり増分で保存 | design §3 の page ごとの規則 |
| overlay の形 | page の `/Contents` = [`q` だけの新しい stream, 元の stream…, 新しい content]。新しい content は `Q` で元の content が残した graphics state を戻してから、表示の座標（crop box の左上・y 下向き・回転済み）→ page の座標の行列で描く。stream の無い page は `q`/`Q` なし。resources は page の実効の resources（継承を含む）の写しに `ExtGState`（と `XObject`）を足し、名前は `KeiGS0…`（既にあれば EEXIST で拒む） | 元の content が cm を残しても線がずれない（試験の page は末尾に `2 0 0 2 0 0 cm` と `3 0 0 3 0 0 cm` を残す） |
| 足した page | 前の元の page の親 node の Kids のその後ろに入れ、祖先の /Count を増やす（node を同じ番号で書き直す）。/MediaBox・/CropBox・/Rotate 0 を自前で持つ（親の継承を受けない） | page tree の入れ子に対応 |
| 添付 | catalog を同じ番号で書き直し、`/Names /EmbeddedFiles` は元の name tree（Kids を含む）の entry から同名（kei-notes.bin）を除いて名前順に平らにし自分の spec を足す。`/AF` も同名を除いて足す。/Info は `/ModDate` を更新 | 元の添付（試験では readme.txt）を保つ |
| trailer | `/Size`・`/Root`・`/Info`（元のまま）・`/Prev`（base の startxref）・`/ID [元の第 1 要素 <revision の hash>]`。xref は classic で、書いた object だけ（`0 1` の free head ＋連続の subsection） | classic xref の base（段階 ①）に classic の section を足す |
| 拒否 | 暗号化: reader は ENOTSUP で開かないので `pdf_document_encrypted()`（最新の trailer か xref stream の dict の /Encrypt）で見分け、Notes は EACCES。署名: `pdf_document_signed()`（`/AcroForm /SigFlags` の bit 1 か `/Perms`）で EPERM、writer も `PDF_ESIGNED`。Notes は toolbar の下に「The PDF is encrypted; Notes cannot write on it. Started a new note」等を 10 秒出し、新しい note を始める | design §3 |
| 背景の描画 | 背景は **CPU の raster を texture に**: `pdf_page_render(base, source)` の display list を page を表示する間保ち、page の画像の大きさ・scale で `pdf_display_list_rasterize()`（白の上）→ renderer の linear の背景の image（`NOTES_TEXTURE_BACKGROUND`）へ行を写す。page の画像を最初から描くときだけ白の矩形の後にこの texture を描き、stroke を上に足す（p011 の cache の「追加」はそのまま） | Vulkan で display list を直接描くのは path の塗り・clip・画像の実装が要り大きい。page の移動・scale の変更のときだけ描き直す |
| 通知の場所 | 長い status は card の横に入らないので、toolbar の画像を 44 px 下へ伸ばし（`NOTES_TOOLBAR_IMAGE_HEIGHT`、帯の高さと layout は不変）、card の下の中央に pill で出す。無いときは透明 | 以前は入らない status は出なかった（拒否の message が見えなかった） |

## 実装

| file | 内容 |
| --- | --- |
| `include/libc/pdf.h`・`userland/base/libpdf/exports.map` | 追加だけ（動的 symbol 34 → 40）: `PDF_ESIGNED`、`enum pdf_page_use`、`pdf_writer_create_update()`・`pdf_writer_keep_page()`・`pdf_writer_begin_page_over()`、`pdf_document_get_revision()`・`pdf_document_signed()`・`pdf_document_encrypted()`。既存の関数の意味は不変（`pdf_writer_get_page_content_hash()` は update の KEEP/OVERLAY の page で ENOENT を返すだけ） |
| `userland/base/libpdf/update.c`（新） | update の配置の検査、object の番号付け、page tree の挿入の計画と node の書き直し、page の上書き・置き換え・追加、resources の合成、catalog・name tree・AF、/Info、revision の xref と trailer、reader の object の書き出し（参照は参照のまま、文字列は hex、名前は #xx） |
| `userland/base/libpdf/writer.h`（新）・`writer.c` | writer の型を private header へ移し、output の helper を内部の非 static（`pdf_buffer_*`・`pdf_writer_write_*`）に。page の追加を `pdf_writer_add_page()` に切り出し、resource の名前の接頭辞（update は `Kei`）、`first_alpha_object`、save は update のとき `update_layout` の hook を使う（writer.c は reader に依存しない: `run-pdf-writer.sh` の writer だけの link を保つ） |
| `userland/base/libpdf/reader.c`・`internal.h` | page の参照（`reference`）の記録、最新の section と /Prev の offset、update 用の accessor（`pdf_reader_size`・`pdf_reader_roots`・`pdf_reader_next_number`・`pdf_reader_page_reference`）、上の 3 つの公開関数 |
| `userland/base/libpdf/Makefile` | `update.c` を足した |
| `userland/desktop/notes/notes.h`・`document.c`・`encode.c` | page の `origin`・`source`、`NOTES_BACKGROUND_PDF`、document の `base`・`base_size`・`base_hash`（free で閉じる）、ZNOT 1.1 の `BASE`・`SRC ` |
| `userland/desktop/notes/save.c` | 全体の保存（今まで）と update の保存、開く（自分の notebook・base の取り戻し・他の PDF を base に）、journal の復元の後の `notes_attach_base()`、暗号化・署名の拒否 |
| `userland/desktop/notes/main.c`・`render.c`・`app.h` | 背景の display list と raster と texture（`notes_renderer_background()`、toolbar と共通の `render_linear()`）、`NOTES OPEN ... kind=`・`NOTES BACKGROUND` の log、開いたときの notice（10 秒） |
| `userland/desktop/notes/ui.c` | card の横に入らない status を card の下の notice に |

## 試験（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| libpdf の update（host） | `sh plan/ws079/tests/run-pdf-update.sh`（gcc、`-std=c89 -pedantic -Wall -Wextra -Werror`、plain・ASan・UBSan） | ok。手作りの「他の PDF」（入れ子の page tree、継承の resources、`/Rotate 90`＋CropBox で 2 stream の page、stream の無い page、Kids のある name tree の添付、/Info、/ID、末尾に改行なし、graphics state を残す content）と qpdf が Flate で書き直した版の両方に: page 1 に上書き・後ろに page を追加・page 2（回転）と page 3（content なし）に上書き・末尾に page を追加・添付 → 読み戻して元の bytes が先頭のまま、5 page、箱と回転、/Prev、追加の page の hash、添付（元の readme.txt も残る）、/ID の第 1 要素、ModDate。qpdf `--check` 誤り・警告なし。libpdf と pdftoppm の 5 page が許容内（平均差 0.04〜1.14、差 64 超 0〜0.16%）。pdftoppm の画素で、線を描いた所だけが変わり他は元のまま（回転した page も左上に黒の箱）。2 回目の revision（全 page keep、添付の置き換え）で kei-notes.bin は 1 つ、画は同じ。拒否: 署名 `PDF_ESIGNED`、暗号化は open が ENOTSUP で `pdf_document_encrypted()`=1、順序違い・二重・範囲外・全 page を並べない save は EINVAL、名前の衝突 EEXIST、Kids に直接書かれた page は ENOTSUP |
| Notes の model と保存（host） | `sh plan/ws079/tests/run-notes-host.sh`（plain・ASan・UBSan、`update.c` を足した） | ok（3 変種）。今まで（自分の notebook の往復）に加え: 他で変わった notebook → CHANGED（page 1 背景・page 2 は stroke を保ち REPLACE）、線を足して保存→ ANNOTATED で同じ notebook。他の PDF → FOREIGN、線と追加の page、journal の復元と `notes_attach_base`、保存（元の bytes が先頭）、開き直し ANNOTATED で同じ、**2 回目の保存が同じ大きさ（revision の置き換え）**、第三者の revision の後は CHANGED（page の線は焼き込み、追加の page は REPLACE で線を保つ）、page の線を全部消して保存すると page は元のまま。署名 EPERM・暗号化 EACCES。保存した 3 file は qpdf 誤りなし・kei-notes.bin 1 つ・pdftoppm |
| libpdf の回帰（host） | `run-pdf-writer.sh`・`run-pdf-reader.sh`・`run-pdf-render.sh 500`・`run-pdfviewer-host.sh` | 4 つとも ok |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-notes.mk BUILD=build/amd64 …/bin/notes …/dynamic/libpdf.so …/bin/pdfviewer`、`config/ci/config-{pcat,pc98,rpi4}.mk` の `BUILD=build/p014-<platform>` で `dynamic/libpdf.so` | すべて exit 0・warning 0（rpi4 は新しい build dir の初回に sysroot の順序で `errno.h` が無く失敗し、変更なしの再実行で通った。p004・p006 と同じ既知の問題）。4 platform とも `pdf_` の動的 symbol 40 |
| 規約の機械的な確認 | `plan/tools/style-check.py`（変えた C の file、HEAD と比較） | update.c・save.c・main.c ほか新しい違反 0（writer.c 2・reader.c 7・pdf.h 4 は HEAD からの既存） |
| QEMU（Venus）の guest | main の `build/ws035-sq/hdd-image.img`（15:49）の copy を KVM・Venus で起動し（`GUEST_RUNTIME=build/p014/run`）、この worktree の notes・libpdf.so・pdfviewer を SSH で入れ替え、`plan/ws079/tests/notes-p014.sh OUT PREFIX install annotate check viewer reopen refuse` | **PASS**（下）。zdesktop の log に ERROR 0 |
| QEMU: p005 の回帰 | `plan/ws079/tests/notes-p005.sh`（`NOTES OPEN` の行に `kind=notes` が入ったので期待の正規表現を直した） | PASS（筆圧なしの pointer の線・undo・消しゴム・page・保存・自動保存・kill の後の journal の復元・開き直し。ZNOT は 1.1） |

guest の段（QEMU、1280x800、zdesktop --glass）:
- annotate: PDF Viewer で foreign.pdf（qpdf が書き直した手作りの PDF、3 page）→ Ctrl+E → `NOTES OPEN pages=3 strokes=0 kind=foreign`・`NOTES BACKGROUND source=0`、F11 で全画面、page 1 に黒の波・黄の蛍光ペン・青の波、PgDn で回転した page 2（`NOTES BACKGROUND source=1`、縦長 280x360 で正しく表示）に波、Ctrl+N で page を足して波、Ctrl+S（`NOTES SAVE ... pages=4 strokes=5`）、Ctrl+W。
- check（host）: 保存した file の先頭 2,794 byte が元と同じ（全体 27,310 byte）、qpdf 誤りなし、4 page、添付 kei-notes.bin と元の readme.txt、pdftoppm と libpdf の 4 page が許容内。
- viewer: PDF Viewer で 4 page、元の上に線（`viewer-after.png`）。
- reopen: 再び Annotate → `kind=annotated strokes=5`、消しゴムで黒の波が消え（`NOTES ERASE page=0 removed=1`）、Ctrl+Z で戻り、Ctrl+S が **1 回目と同じ 27,310 byte**（revision の置き換え）、元の bytes はそのまま、qpdf 誤りなし。
- refuse: 署名の PDF を PDF Viewer から Annotate → `NOTES OPEN failed error=47`（EPERM）と notice、暗号化の PDF は `error=25`（EACCES）と notice。

画面（QEMU の Venus、`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `ws079-p014-20260928-{viewer-before,notes-window,notes-page1,notes-page2,notes-added,viewer-after,viewer-after-scroll,notes-reopened,notes-erased,refuse-signed,refuse-encrypted}.png`、保存した PDF の pdftoppm の 4 page を並べた `ws079-p014-20260928-pdftoppm.png`。

注: guest の最終の実行は、規約の整え（空行・注釈、update.c の /ID の分岐の並べ替え、save.c の変数の初期化）の前の build の binary。整えの後は host の試験（update・notes・writer・reader）を再び通し、amd64 と 3 platform を再 build（warning 0）したが、guest では流し直していない。

## 未実施と制限

- 実機（i915・pen）: 未実施。notes-p011.sh・notes-pen.sh（peninject が要る。main の sq image には注入の device が無い）: 未実施。
- 段階 ① の libpdf は text・shading・inline image を描かないので、一般の PDF の文字は Notes の背景に出ない（PDF Viewer と同じ）。保存した file の文字は他の viewer では元のまま見える（元の content は変えない）。
- 背景は CPU の raster（page の移動・拡大の変更のときだけ）。Vulkan で display list を直接描くのは後。
- xref stream・object stream の PDF（多くの一般の PDF）は reader が ENOTSUP で開けないので Notes も書けない（段階 ②、p007）。そのときの message は「uses features Notes cannot read yet」。暗号化が xref stream の中にある file も見分ける（`pdf_document_encrypted()` が xref stream の dict を読む）。
- Kids に直接書かれた page・catalog が直接の object の file は ENOTSUP（保存の失敗として status に出る）。
- 他の program の revision の後に開くと、Notes が以前その page に描いた線は背景に焼き込まれ編集できない（design どおり）。
- 添付の reader は filter つきの埋め込み file を読まない（qpdf が圧縮した readme.txt は ENOTSUP で「在る」とだけ分かる）。
- 署名の判定は `/SigFlags` の bit 1 と `/Perms` だけ（未署名の署名 field だけの file は書ける）。

## 残り

1. 背景の Vulkan での描画（display list の塗り・clip・画像）と、大きな page の tile 化。
2. 段階 ②（p007）で xref stream の base に足すときは、revision も xref stream で書くか確かめる（今は classic の section を足す。reader がまだ読まない）。
3. page の削除・並べ替えを他の PDF でも（今の Notes に page の削除は無い）。
4. `plan/coding-style.md` の全文の読み直し（p009）。

## 再開

`sh plan/ws079/tests/run-pdf-update.sh`、`sh plan/ws079/tests/run-notes-host.sh`。guest は main の zdesktop image を
`GUEST_RUNTIME=$PWD/build/p014/run plan/ws035/tests/zdesktop-guest.sh start <image>` で起動し、
`plan/ws079/tests/notes-p014.sh OUT PREFIX install annotate check viewer reopen refuse`（`check` は run-pdf-update.sh の
`build/ws079-p014-host/` の PDF と host-pdf-render を使う）。
