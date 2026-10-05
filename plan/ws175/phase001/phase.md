<!-- awesome-plan project=zedbsd record=ws175-p001 -->
# ws175-p001: 設計 — Notes の PDF の編集

Parent: [WS175](../ws.md)
Status: in-progress（2026-10-06 P2 が設計を書き design-reviewer が review した。**review の反映は未了**（Q1 のラップアップの指示で中断）。下の「review」の表が design.md への未反映の修正の正本。clearance は反映・Q1 の判定・ユーザーの判断 D1〜D7 の後）
Disposition: normal

由来・目標は [WS175](../ws.md)。設計の正本は [design.md](design.md)。product の code は書かない。

## 調査の結果（要点、詳細は design.md §1）

- **複数頁は対応済み**（他の PDF の各 page が OVER の page、`<`・`3 / 12`・`>`・PageUp/PageDown・`+ Page`）。
- 保存は既に**増分の更新**（base の bytes＋保存ごとに作り直す Notes の revision）。page の置き方は keep・overlay・replace・new。
- libpdf の writer に**文字・font の書き出しが無い**。**deflate が無い**（stream は無圧縮）。update の resource の merge は ExtGState と XObject だけ。
- libpdf に**文字の抽出（ToUnicode）が無い**（ws128-p004 も planning でこれ待ち）。interpreter は Type0 Identity-H/V まで読む。
- font: Inter（variable、OFL、RFN なし）、JetBrains Mono（OFL）、Droid Sans Fallback（Apache-2.0、fsType 8 Editable）。serif は無い。

## 設計の要点

- 書き戻しは既定で増分の更新（編集した page の `/Contents`・`/Resources` だけ新しい版）、消した物を残さない「Save Clean Copy」は選択肢（D1）。
- libpdf の page editor（新しい公開 API）: 走査で top level の物（文字の行・画像・form）の byte の範囲と graphics state を記録し、編集した物だけを
  置き換えた content を作る。文字の block は「各 show の前に明示の Tm」に正規化してから書き換える。画像は `q M cm … Do Q` で置き直す。
- 文字の書き換えは元の font に glyph が全て有ればそのまま、無ければ system の font（Type0/CIDFontType2/Identity-H、詰めた subset、ToUnicode）に。
- 画像は PNG・JPEG（JPEG と alpha 無しの PNG は bytes のまま）。libz-compat に deflate を足す（D4）。
- Notes の model は page ごとの物の状態の表、undo は状態の前後、ZNOT major 2（EDIT・IMAG）、画像の bytes は XObject の私的な key で読み戻す。
- UI: Select・Text・Image の道具、枠と handle、操作の帯、その場の文字の編集の box（IME）、font の picker。
- AAT の draft のシナリオ 5 本を `tests/scenarios/apps/notes/pdf-*.md` に置いた（`check-scenarios.py` PASS）。

## Phase の分け方（案）

p002 走査と抽出 2.5 LW → p003 editor と書き戻し 2.5 → p004 font の埋め込み 2.5 → p005 deflate と画像の取り込み 1 →
p006 Notes の model 2 → p007 Notes の UI 3 → p008（D1）Clean Copy 1.5 → p009 T1 の QEMU と AAT 1 → p010 規約 0.5。
合計 15 LW（p008 込み 16.5 LW）。WS の概算 6 LW より大きい（design.md §10）。**review の後の見直し（未反映）**: p003 を画像（2）と文字の書き換え（1.5）に分け、p007 の UI に入力の配管（+0.5）、p008 Clean Copy を 2.5 に → 約 17.5 LW（Clean Copy 込み 20 LW）。画像を先に出す場合（D6）は約 10 LW。

## ユーザーの判断（推奨）

| # | 判断 | 推奨 |
| --- | --- | --- |
| D1 | 消した物の bytes が file に残る | 増分の更新＋「Save Clean Copy」（p008） |
| D2 | font の種類 | Inter・JetBrains Mono・Droid Sans Fallback の 3 つ（serif は Future Work） |
| D3 | 太字・斜体 | 出さない |
| D4 | deflate | libz-compat に足す（WS175 の p005、path の許可） |
| D5 | 文字の抽出の持ち主 | WS175 の p002。ws128-p004 は p002 に依存を付け替える |
| D6 | 出し方 | 画像の編集を先に（約 8 LW）、文字を後 |
| D7 | 既存の文字の編集の単位 | 行ごと（段落の再配置なし） |

## 実行の記録

- 2026-10-06 P2: worktree `/home/awe/zedBSD-worktrees/p2`（main c6fd9a4a へ fast-forward）。調査（notes・libpdf の reader・writer・update・content・font、
  libtruetype、fonts の table と license、libpng/libz-compat、libkeiui の text input、ws128-p004、AAT の道具）。design.md と AAT の draft 5 本を作成。
- `python3 plan/tools/aat/check-scenarios.py` → `check-scenarios: PASS`（scenarios 81）。
- design-reviewer の review: 下の「review」。

## review

2026-10-06 design-reviewer（read-only、source の行を引いて照合）: high 6・medium 16・low 11。**design.md へは未反映**（Q1 のラップアップ）。次の世代が下の修正を design.md に入れてから clearance に出す。

| # | 重さ | 指摘 | design.md への修正（次の世代） |
| --- | --- | --- | --- |
| H1 | high | 正規化で `TD` を落とすと TL（leading）が変わり、後の触っていない block の `T*`・`'`・`"` の行がずれる（content.c:2410-2418） | §3.4: 落とす `tx ty TD` は `-ty TL` に置き換える。edit-hard.pdf に「block A の TD、block B の T*」 |
| H2 | high | clip の mode（Tr 4〜7）を含む block を `ET q…Q BT` で割ると clip が早く掛かり壊れる | §3.1・§3.4: Tr ≥ 4 の show を含む block の行は全部編集しない（一覧に出さない）。試験を足す |
| H3 | high | 開く時に適用できない状態を「捨てる」と、次の autosave で file から永久に消える。ordinal は grouping の heuristic に依り、libpdf の更新だけで番号が変わりうる | §6.3: 1 つでも適用できなければ file 全体を新しい base（open_as_base、編集は焼き込み）として開く。物の key は種類＋decode した content の中の最初の token の byte の位置と長さ＋指紋 |
| H4 | high | 保存で journal が消え、次の snapshot（ZNOT）に画像の bytes が無いので、保存→編集→crash の回復で保存前の画像が失われる（journal.c:8-26・578-586、notes_attach_base は base_size までしか読まない） | §6.3: journal の snapshot に、今の状態が参照する画像の bytes を入れる（JOURNAL_SIZE_MAX 512 MB に注意） |
| H5 | high | font の cache は dictionary の pointer が key で文書が閉じるまで残る（font.c:284-289）。editor の arena の object を捨てると address の再利用で別の font に当たり、face が解放済みの bytes を指す（use-after-free） | §3.5: system の font と preview の画像の object は pdf_document の寿命の領域に 1 回だけ作り、editor の間で共有。cache が参照する object を解放しない。ASan の host 試験（editor の開閉の繰り返し） |
| H6 | high | deflate で content を圧縮すると content hash が filter つきの stream を拒み（reader.c:533-557・3641-3661）、Notes の文書が FOREIGN で開く。添付は decode されず読まれる（reader.c:622-624）ので圧縮した ZNOT は読めない | §5.2・p005: 圧縮は新しい画像・font の stream と major 2 の編集した base の page の content だけ。stroke の content と ZNOT は圧縮しない |
| M1 | medium | interpreter が記録する CTM は B を含む（content.c:510、C_rec = C_user × B）ので式の C が曖昧 | §3.4: M = C_rec × S × C_rec⁻¹、Tm' = Tm × C_rec × S × C_rec⁻¹ と書く（挿入は M = P × B⁻¹ のまま）。/Rotate 0/90/180/270・crop box のずれ・scale した cm の中の Do を試験 |
| M2 | medium | 移動・大きさだけでも置き換えの型を通すと font が 1 つにまとまる | 置き方だけの時は正規化した block の中で各 show の Tm をその場で変換する（分割・再符号化なし） |
| M3 | medium | 色を RGB で出し直すと Separation・CMYK・ICC・Pattern の色が DeviceRGB になる | 既存の行の置き換えでは色・text state を出さない（font を変える時の Tf だけ）。色は挿入の物だけ |
| M4 | medium | 名前は描く時に writer 全体の番号で content に入る（writer.c:229・1533）ので page ごとに選べない。CHANGED の文書では base に既に `KeiGS0`・`KeiIm0` が在り、今でも次の保存が EEXIST で失敗する潜在の bug | §2.1: pdf_writer_create_update の時に、base のどの page（継承を含む）の ExtGState・XObject・Font にも無い文書全体の接頭辞（`Kei`、`Kei1_`…）を選ぶ。潜在の bug は Q1 に報告済み |
| M5 | medium | filter が読めない・上限を超えた stream は read_contents が飛ばす（content.c:600-668）ので、書き直すとその stream が消える | editor が自分で content を読み、SKIPPED・LIMITED・DAMAGED の stream が有れば編集の道具を出さない |
| M6 | medium | 画像の読み戻しが不足（RGBA は RGB＋SMask の別 object、私的な key を書く writer の API が無い、複数頁の同じ画像が重複） | 種類ごとの読み戻し（JPEG はそのまま、PNG_IDAT は bytes＋DecodeParms、RGBA は image.c で image と SMask を解く）、画像 id ごとに XObject 1 つを共有、file を閉じる前に bytes を複写（save.c:224-231） |
| M7 | medium | Notes の window_event が TEXT_COMMIT・PREEDIT・DELETE を渡さず（window.c ~310-345）、key の repeat を捨て（373-379）、key → 文字の変換が無い | p007 に repeat・`kl_key_character`・`kl_window_text_cursor`・surrounding の削除・clipboard を足し、見積もり +0.5 |
| M8 | medium | 指の gesture（1 本の指 = scroll、double tap = zoom、touch.h:8-16）と衝突 | Select の道具: tap で選択、選んだ物の上の long-press（300 ms）の後の drag で移動、それ以外は scroll。double tap の編集は選んだ文字の上だけ |
| M9 | medium | 語ごとに BT を出す生成器で行が分かれる。page 全体が 1 つの form の文書は何も編集できない。OCR の Tr 3 の見えない文字が hit で勝つ | 隣の top level の BT block を（CTM・font・baseline が同じで間に描画が無ければ）行にまとめる。Tr 3 の行は hit で後回し・削除だけ。form で包んだ page と scan の制限を §9 に。edit-hard.pdf に例 |
| M10 | medium | 印付きの内容（BDC の /ActualText、MCID）を扱わない。BT の中で始まった BMC の中で ET/BT を割ると入れ子の規則に反する（推測） | /ActualText は落とし、BDC/EMC は残す。BT の中で始まった印付きの内容の中の行は割らず block の ET の後に出す（その場合だけ device の色を出す、他の色空間なら内容の変更は不可）。試験を足す |
| M11 | medium | Clean Copy でも merge した resource が元の XObject を全て持ち、消した画像が残る。元の埋め込みの font の glyph も残る | p008 は新しい content が参照しない resource を落とす。D1 の文言を合わせ、見積もり 2.5 LW |
| M12 | medium | preview は font 全体（CID = 元の GID）、保存は subset（CID = 新しい GID）で、「同じ content」は言い過ぎ | 保存した file の libpdf の render と preview の render を比べる host 試験 |
| M13 | medium | autosave は UI の loop で走り（main.c:476-481）、毎回 content の組み立て・deflate・subset（4 MB の Droid）をする。開く時は全編集頁を走査 | page ごとの content・font ごとの subset を世代の番号で cache。10 頁の編集で 200 ms 未満を目安に p003・p006 で測る |
| M14 | medium | §11.1 (4) の「pdftoppm と libpdf の差 0」は別の rasterizer なので通らない。試験の欠け | pdftoppm の前後、libpdf の前後を別々に、膨らませた四辺形の外で比べる。H1・H2・M5・M10・CMYK の JPEG・crash の回復（画像）・ASan の editor の開閉・Flate の開き直しの例を足す。ToUnicode の parser と走査は ASan/UBSan と fuzz の corpus（design-pdf.md §4.3） |
| M15 | medium | CMYK の JPEG を受けると書いたが writer は成分 1・3 だけ（writer.c:530） | v1 は CMYK の JPEG を拒む（知らせる） |
| M16 | medium | p006 は p005 の画像の種類と私的な key、p004 の font の収集に依るのに並列と書いた。D6 が表に無い | p003 を画像と文字に分け、model は画像の取り込みの Phase に依存。新しい分け方: p002 走査と抽出 2.5 → p003 画像の editor と書き戻し 2 → p004 文字の書き換え 1.5 → p005 font 2.5 → p006 deflate と画像の取り込み 1.5 → p007 model 2.5 → p008 UI 3.5 → p009（D1）Clean Copy 2.5 → p010 T1 1 → p011 規約 0.5 |
| L1 | low | 確認済み: 古い Notes は major ≠ 1 を EINVAL にし（encode.c:444-449）全体を FOREIGN で開く（CHANGED ではない）。編集は失われない | §6.3 に: そのとき新しい Notes の pen も背景になる、Notes の file と知らされない。新しい decoder は major 1 を読み続ける |
| L2 | low | inline image の移動が未記述 | `q M cm BI…EI Q` で包む |
| L3 | low | content が BT の中で終わる（ET 無し） | 閉じの Q の前に ET。開いたままの q は token から数える（interpreter の上限で数えない） |
| L4 | low | `pdf_buffer_append_number` は `%.4f`（writer.c:974） | editor の行列は桁を増やす |
| L5 | low | 「描ける・幅が有る」の glyph の判定は空白・空の glyf で誤る。標準 14 font を含まない | ToUnicode の逆引き＋空白以外は空でない輪郭で判定。非埋め込みの font は常に置き換え |
| L6 | low | PNG の IDAT の素通しの parser が無い。`kl_picture_exif_orientation` の picture.c は Notes が build していない | CRC の確認・完全な inflate で大きさの確認・tRNS は除く・iCCP/gAMA は無視（色がずれる）と書く。Notes の Makefile に picture.c を足す |
| L7 | low | 挿入した物の削除の undo が z の位置を持たない。undo の上限で古い entry を捨てる時の画像の参照の数 | entry に z の位置。捨てる時に参照を減らす |
| L8 | low | /Rotate の page・回転した文字で「1 px の縦線の caret」は誤り。回転した page の挿入の Tm | caret は文字の向きに沿う線。挿入の Tm は page の回転を打ち消す |
| L9 | low | 今の `NOTES TOOL %u` は数字（main.c:963）。`NOTES OPENED … path=%s` は path で終わる（main.c:2339） | log は `NOTES TOOL N name=select` の形。新しい項目は `path=` の前に。シナリオの文言を合わせる |
| L10 | low | §11.3 の表は開き直しの後に undo できると書くが、undo の履歴は file に残らない | 表と multipage のシナリオを「開き直しの後は Reset で戻す」に合わせる |
| L11 | low | license は確認済み（fsType、RFN 無し、name ID 0/13/14、Inter の variable の table）。Droid の composite が多く remap は必須。Droid の subset は Apache-2.0 の改変物（§4(a)(b) の license と改変の notice） | name ID 13/14＋改変の注記で足りるかを D2 に添えてユーザーに示す。pdf.h の公開の struct は size の field か getter にする（ABI） |
