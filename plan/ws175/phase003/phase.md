<!-- awesome-plan project=zedbsd record=ws175-p003 -->
# ws175-p003: libpdf の画像・図形の editor と書き戻し

Parent: [WS175](../ws.md)
Status: cleared の判定を Q1 に依頼（2026-10-06 q805 P2: p003a・p003b を実装、host 試験 PASS。blank の editor は p007 に移す、末尾）
Disposition: normal
Queue: q805
依存: [p002](../phase002/phase.md) の p002a（main に統合済み 78ecc1825）

## 分け方（2026-10-06 P2）

| 部分 | 内容 |
| --- | --- |
| p003a | 物の削除・移動と大きさ（`delete`・`place`・`reset`）、新しい content の組み立て（design.md §3.4: 全体を `q … Q`、編集した物の byte の範囲の置き換え `q M cm … Q`（M = C_rec × S × C_rec⁻¹）、空の stack の Q を落とす、BT の中の終わりに ET、開いた q の数の Q、数は有効数字 9 桁 [L4]）、preview の render（新しい content を page の resource で interpreter に）、update の `PDF_WRITER_PLACE_EDIT`（`/Contents [編集した stream, 上に描く stream]`、`pdf_writer_begin_page_edited`）、文書全体の名前の接頭辞 [M4]、読めない stream の page は編集を拒む（EPERM）。host 試験 |
| p003b | 画像の差し替えと挿入（`set_image`・`insert_image`、`struct pdf_image_source`）、preview の画像の object を文書の寿命の領域に 1 回だけ [H5][N4][N5]、全体の書き出し（Notes が作った文書）での editor（`pdf_page_editor_blank`）。p006（画像の取り込み）と合わせる |

## p003a の実装（2026-10-06 q805 P2）

- `editor.c`: 物ごとの状態（そのまま・削除・置き換え S）と `pdf_page_editor_reset`・`delete`・`place`（S は shown space の affine、NaN・1e9 以上・面積の無い物と C_rec の逆が無い物は EINVAL）・`render`（`hidden` の物を除いて）。READ_ONLY の page は EPERM、render は元のまま。`pdf_editor_content`（internal）が §3.4 の新しい content を作る: 全体を `q … Q`、削除と hidden は空白に、置き換えは `q M cm <元の bytes> Q`（M = C_rec × S × C_rec⁻¹）、空の stack の Q を落とす、BT の中の終わりに ET、開いた q の数の Q。数は小数 9 桁で末尾の 0 を落とす（[L4]）。`pdf_page_editor_object` は削除の flag（`PDF_EDIT_OBJECT_DELETED`）と置いた後の四辺形を返す。`pdf_writer_begin_page_edited` も editor.c（update.c が editor を知らないように。Notes の host build は editor.c を持たない）。
- `content.c`: `page_run` が呼び手の content も受け、`pdf_content_render`（internal）。
- `update.c`・`writer.h`・`writer.c`: `PDF_WRITER_PLACE_EDIT`（page の `edited` に新しい content、`/Contents [edited, 上に描く stream]`、resource は上に描く物と merge）。`pdf_update_begin_edited`（internal）。content hash は EDIT も ENOENT。[M4] `pdf_writer_create_update` は全 page の ExtGState・XObject・Font の名前を見て接頭辞を `Kei`、使われていれば `Kei1_`〜`Kei9_` から選ぶ（`writer->prefix_buffer`）。前に Notes が書いた `KeiGS0`・`KeiIm0` の文書の次の保存の EEXIST（潜在の bug）が直る。
- 試験: `plan/ws175/tests/host-edit-change.c`（26 項目: 削除と 10 pt の移動の preview、hidden、reset、READ_ONLY の EPERM、Kei1_、update の保存、base の bytes が先頭、開き直した page 1 の物と描画）と `run-host-edit-scan.sh`（2 つの試験、plain・ASan・UBSan、保存した file の `qpdf --check` は試料の page 3 の読めない stream だけ）→ PASS。`make-edit-samples.py` に page 5（`/KeiIm0`）。
- ws079 の試験の直し: `plan/ws079/tests/host-pdf-update.c` の「a name the page uses refused」（EEXIST）を、[M4] で別の接頭辞になり保存できることに直した → `run-pdf-update.sh` ok。
- build: zedBSD の libpdf.so（13 の editor の symbol）、keiland-linux の warning 0、host の C89 pedantic、style-check 0。

## p003b の実装（2026-10-06 q805 P2）

- `editor.c` を作り直した: 物ごとの変更（状態・S・差し替えの画像・挿入か・挿入の unit square の写像 P）を 1 つの表に、page の物の後に挿入した物を並べる。与えられた画像は editor が bytes を複写して持つ（JPEG は 1・3 成分のまま、CMYK は EINVAL [M15]。RGBA は RGB と、不透明でない画素がある時だけ alpha の mask に分ける）。
- `pdf_page_editor_set_image`（物の今の四隅に、画像の縦横比を保って中央に合わせる。content は `q M cm /Name Do Q`、M = P × C_rec⁻¹）と `pdf_page_editor_insert_image`（placement は unit square → shown space、最後の物、元の content の `Q` の後に B の下で `q M cm /Name Do Q`、M = P × B⁻¹。scan は B を `pdf_scan.base` に記録）。`object` は INSERTED の flag、差し替えた画像の大きさと四隅、hit は削除した物を除き挿入した物が上。挿入した物に key は無い（ENOENT、Notes の model が持つ、p007）。
- preview: page の resource を浅く複写した辞書に、editor の arena の画像の stream（`bytes` は editor の buffer、RGBA は SMask つき）を `ZedPreviewIm<N>` で足し、`pdf_content_render` に渡す（`page_run` は resource も受ける）。reader は画像を object で cache しないので、preview の画像の object は editor と一緒に捨ててよい（[H5] の UAF は font の cache の話で p005 で扱う。[N5] の浅い複写は満たす）。
- 書き出し: `pdf_writer_begin_page_edited` は、まだ使われている画像だけを `pdf_writer_add_image_object`（writer.c、新規の internal）で文書の画像にし、`<prefix>Im<N>` の名前で content を作る。上に描く物と同じく page の XObject に merge される。
- 試験: `plan/ws175/tests/host-edit-image.c`（17 項目: RGBA で form を差し替え 100,77.5〜130,92.5、長さ違いと CMYK の EINVAL、JPEG の挿入 50,70〜90,90、hit、preview の 4 枚、update の保存と開き直しで 4 つの物と 4 枚の描画）。`run-host-edit-scan.sh` で 3 つの試験が plain・ASan・UBSan とも PASS、保存した 2 つの file の qpdf --check は試料の page 3 だけ。pdftoppm で保存した page 1 を描き、4 枚が意図した所にあることを目で確かめた。
- 回帰: ws079 の run-pdf-render ok、run-pdf-update ok。build: zedBSD の libpdf.so と keiland-linux の warning 0、style-check 0。
- 残り（p003 の外、後の Phase）: `pdf_page_editor_blank`（Notes が作った page・REPLACE の page の上の挿入、design [N2]）は Notes の model（p007）と合わせる。PNG の IDAT の素通しと deflate は p006。
