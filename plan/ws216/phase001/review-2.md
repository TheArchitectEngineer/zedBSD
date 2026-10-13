<!-- awesome-plan project=zedbsd record=ws216-p001-review-2 -->

# ws216-p001 設計 2 の敵対的 review（review 2）

- 対象: [phase.md](phase.md) 版 2（2026-10-13 P3）と [ws.md](../ws.md) の「ユーザーの決定」
- 照合した tree: main `2653078a0`。設計 2 の調べの tree `a9c627d6b` から `userland/desktop/notes/`・`userland/base/libpdf/`・`include/libc/pdf.h`・`src/drivers/gpu/bcm2711/` に差分は無い（`git diff --stat a9c627d6b HEAD` が空）
- review の担当: design-reviewer（2026-10-13）。source と設計文書は変えていない。build・試験・QEMU は走らせていない（読むだけ）。`.internal/` は読んでいない
- 重大度: review-1 と同じ（致命的／重大／中／軽微）

## 要約

review-1 の 28 件は、主張の大筋は source で裏が取れた（§1）。致命的・重大の新しい指摘は無い。新しく入れた所に中 13 件・軽微 18 件。p002 は、p002 に効く中 4 件（M-6・M-7・M-8・M-9）を p002 の phase.md に入れることを条件に始めてよい（§5）。

| # | 重大度 | 指摘 | 効く段 |
| --- | --- | --- | --- |
| M-1 | 中 | 帯の画面外の描画は、帯ごとに頁の全部の stroke を CPU で組み直し GPU で 4 px/pt で塗る。Pi で頁全体の塗りは描き直しの約 10 倍（数秒の見込み）で、§4.5 の 100〜300 ms は CPU の flood だけの数 | p005・p009 |
| M-2 | 中 | 膨張・flood・膨らませ戻しが帯の境をまたぐ時の扱いが無い。遅延で帯を読むと、隣の帯の境の画素を知らないまま flood が進み、帯の境で漏れ・段差が出る。試験も 1 枚の raster だけ | p005 |
| M-3 | 中 | 「0.1 pt 線の下へ潜らせる」は 4 px/pt の 2 値の mask では表せず（0.4 px）、境の閾値（26/255）で AA の fringe の外側約 0.75 px まで境になるので、塗りと線の間に約 0.1〜0.2 pt の白い隙間が残る | p005 |
| M-4 | 中 | 帯の幅が 4096 px を超える頁（A3・Tabloid の横長、R = 4 で 4764 px）で、Pi の image の上限（4096）に当たる。断りは `ENOTSUP` 系で、半分にする経路（OOM だけ）に入らず塗れない | p005・p009 |
| M-5 | 中 | 部分消しの切り口は `radius + width/2`（`document.c:741`）。水彩の縁 2 は芯の最大 1.47 倍、筆は 1.15 倍なので、切れ端の丸い cap が消しゴムの円の中へ入り、円の中に薄い水彩が残る（10 pt の消しゴム、28 pt の水彩で縁 2 が中心から約 3.4 pt まで残る）。p003 の試験は円の外しか見ない | p003 |
| M-6 | 中 | 粒の tiling pattern は「色ごとに 1 つ」で Matrix は「頁の左上を原点」。頁の左上は頁の高さと（他の PDF の上の頁では）`shown_to_user()` の行列（crop box・回転、`update.c:330-341`）に依る。writer の resources は文書で共有（`writer.c:1140`）なので、頁の大きさ・向きの違う文書で粒が画面とずれる | p002・p003 |
| M-7 | 中 | reader の tiling pattern の「cell を一度だけ描いて持つ」は、display list が倍率を持たない（倍率は `pdf_display_list_rasterize()` の時）ことと合わない。どの解像度で cell を描くかが決まっていない（一般の PDF の cell の中身は文字・shading・入れ子の pattern もある） | p002 |
| M-8 | 中 | 鉛筆の区間の「重なりも隙間も無い（面積の和 = 全体）」は、自分と重なる stroke（ループ、鋭い角の直後の切り口）では成り立たない。1 つの polygon なら nonzero で一度だけ塗られる重なりが、区間に分けると二度塗られる。受け入れの試験はこの形で落ちるか、形を選んで通すことになる | p002 |
| M-9 | 中 | 範囲の輪郭の API で、(a) 返す「実際の境の index」が点の index か中心線の頂点の index か、(b) 区間の polygon のどの辺が継ぎ目か（`seam_*` の fringe なしに要る）が決まっていない | p002・p004 |
| M-10 | 中 | 「道具の定数の版」は `TOOL` だけに持たせ、`struct notes_stroke` の field と journal の stroke の record（`notes_encode_stroke()` は道具を種類・色・幅で直に書く、`encode.c:628-650`）に持つことが書かれていない。journal 3 の形を今決めるので後から足しにくい | p003 |
| M-11 | 中 | D7 (c) の判定の偏り: 線は円（半径 10 pt）で当たり、塗りは中心で当たる。塗りの上で線の近く（中心が線から少し外れた所）に下ろすと「塗りも消す操作」になり、背景の塗り（D12 の頁全体の塗りを含む）が線と一緒に消える | p005 |
| M-12 | 中 | Notes と libpdf は Linux・FreeBSD でも build する（`Makefile.linux`・`Makefile.freebsd` が source の一覧を持つ）。新しい file（粒・塗り・shader）を 3 つの Makefile に足すことと、その build の確かめが §8 に無い（L6 は amd64 と rpi4 だけ） | p002〜p006 |
| M-13 | 中 | Save Clean Copy（`clean.c`、ws175-p009）が新しい形（tiling pattern・SMask・`/BM`・Flate の content）を保つことが試験に無い。clean copy は content が名指す resources だけを残し、pattern の中の名前を追うので、pattern に自分の `/Resources` を持たせないと落ちうる | p002・p003 |
| L-1〜L-18 | 軽微 | §6 |

## 1. review-1 の指摘への対応（§11 の主張を source で確かめた）

| # | §11 の主張 | 確かめた結果 | 残り |
| --- | --- | --- | --- |
| C1 | Flate は major 3 だけ、reader は decode して hash（64 MiB） | 解決。`reader.c:511-575` は今 `check_unfiltered()`（`reader.c:3697-3717`）で `ENOTSUP`。hash を記録するのは `PLACE_NEW` の頁だけ（`writer.c:771-776` は KEEP・OVERLAY・EDIT に `ENOENT`）なので、Flate の影響は Notes 自身の頁に限られる。今の Notes は 3.0 を拒む（`encode.c:539`）ので「他の PDF」として開き、content の Flate は表示の経路（`content.c:840` `pdf_filter_decode()`）で読める | L-3（64 MiB と writer の対称）、L-2（§1.4 の事実の抜け） |
| C2 | 遅延の pipeline と代わり、BGRA8・mip 1・別 texture、WS141 への依頼 | 解決。`bcm2711/vulkan-pipeline-state.c:213-247` の blend、`vulkan-resource.c:256-262`（mip 1、RGBA8/BGRA8 だけ）、`vulkan-query.c:386-391`、`vulkan-input.c:484-495`（REPEAT 可）、`vulkan-input.c:370-374`（LINEAR 可）を確かめた。WS141 に p019（案、`plan/ws141/ws.md:83`）が立っている。pipeline の作成の失敗は `vulkan-pipeline-build.c:114` で作成の時に返る（描画の時ではない）ので、代わりへの切り替えは成り立つ | なし（Pi の stencil の image は p017 の範囲。`vulkan-resource.c:23` の image の usage に DEPTH_STENCIL が無い） |
| S1 | tiling pattern と下塗り、reader に pattern | 方針は解決。古い reader の振る舞い（pattern を飛ばして下塗りだけ）は source で正しい: `content.c:3333-3352` の `set_pattern()` は PatternType 2 以外で `fill_usable = 0`、`content.c:2385` で塗らない（黒くは塗らない）。次の `rg` で色は戻る | M-6・M-7・M-13 |
| S2 | 焼き込んだ値、切れ端は写す、円の外の形は同じ | 円の外は解決（`outline.c` の補間は前後の点だけ、`keep_samples()` の併合は 0.001 pt 未満で、1/64 pt の格子では同じ点だけ）。切り口は `stroke_split()` が点の列の直線の上に補間の点を入れる（`document.c:1642-1660`）ので「円 + 幅の半分 + 点 2 つ分の外」で正しい | M-5（円の中に残る）、L-8（焼き込む時点） |
| S3 | 4 MiB の塊、三角形の境で分ける、断られたら半分 | 解決。draw は `first`・`count` で頂点を指し（`render.c:2160-2230`）、vertex buffer の bind は 1 回（`render.c:2094-2095`）なので、塊ごとの bind と分割は局所的な変更で済む。「1 つの render pass の中」は不正確（頁の絵の pass と window の pass の 2 つ、`render.c:2097-2147`）だが、1 つの polygon の draw は 1 つの pass に収まるので結論は変わらない（L-17） | M-1（帯も同じ塊を使うと頂点 × 帯の数） |
| S4 | D7 (c)、輪郭の内側の判定 | 方針は解決 | M-11 |
| S5 | 決まった倍率の帯、遅延、断られたら半分、潜らせは pt | 倍率と大きさの上限は解決（A4 で R = 4、2380×3368、帯 2380×440×4 = 3.99 MiB を計算で確かめた） | M-1〜M-4 |
| M1 | 鍵 (ca, BM)、`q/Q` で状態、Normal に戻す | 解決。今の `current_alpha` は頁の始めで 1.0（`writer.c:928`）、`set_fill_color()` は同じ alpha で `gs` を省く（`writer.c:228`）。inserted の文字は `q … rg … Q` で（`editor.c` `editor_draw_text()`）、`gs` は出さないので writer の状態とずれない | なし |
| M2 | gentle な頂点で切る、面積の試験 | 一部。gentle は中心線の頂点の性質（`outline.c:124-130` `struct pdf_outline_join`）で、点の index ではない | M-8・M-9 |
| M3 | 係数の上限 2.0、±8 ms の窓 | 解決。水彩の縁 2 の最大 1.36 × 1.0 × 1.08 = 1.47、筆 1.15 で 2.0 に収まる | なし |
| M4 | 整数の hash、3 段、RGBA8 | 解決 | L-5（格子の数の矛盾）、L-6（倍率の小さい時） |
| M5 | pattern、継ぎ目を数える | 解決 | なし |
| M6 | DRAW 48、帯、`NOTES_BUTTONS` 48 と log | 解決。action の番号 48〜52 は空き（`app.h:75-122`、色 20〜24、幅 30〜32）。`ui_add_button()` の黙った捨ては `ui.c:1110-1112` | なし |
| M7 | 島・穴の下限、8192 点、notice | 解決 | なし |
| M8 | `TOOL` の鍵、`major >= 2`、journal 2 → 3 | 一部。journal の作り直しは今の code がすでにそうする: 復旧の後は `notes_journal_create()`（`main.c:999`）が閉じた journal を作り、最初の変更で `journal_start()` が header と snapshot から新しく書く（`journal.c` `journal_ready()`）。§5.4 の文は正しいが新しい作業ではない | M-10 |
| M9 | 古い Notes の保存の振る舞い | 解決（source と一致）。decode に失敗した file は `decoded = 0` で `open_as_base()`（`save.c:348`）、`NOTES_OPENED_FOREIGN` | なし |
| M10 | 依存の表 | 解決 | L-15（ws.md の p009 は WS141 p019 と名指す） |
| M11 | double tap を止める | 解決 | なし |
| L1 | §1 の行番号 | 解決。抜き取りで確かめた行（`notes.h:41-42`、`document.c:205,419,525,617,703,1368,1665,1696`、`render.c:187,726,1113,1441,1554,1600,1690,1756`、`geometry.c:174,380`、`main.c:105,1839,2492,2715`、`encode.c:48,50,327-330,539,590-592`、`journal.c:53,570,768`、`save.c:629,1128`、`reader.c:511,3697`、`internal.h:148`、`display.c:195`、`app.h:28,36,50,56`）は全部合う | L-2 |
| L2〜L10 | 式・usage・format・stage・build・notice・見え方・exports・時刻 | 解決。ただし L10 は journal の encoder（`notes_encode_stroke()` は `TIME` と `start_ms` を常に書く、`encode.c:639-643`）にも要る（L-10） | L-10 |

## 2. 中

### M-1. 帯の描画の時間が見積に無い（Pi で数秒の見込み）

- 根拠: 頂点は CPU が画素の座標で作る（`geometry.c:174-300` は `view->x + x × view->scale`、`shaders/draw.vert` は push constant の大きさで割るだけ）。§3.4 の 3「push constant の原点と倍率を替える」では帯は描けず、帯ごとに `notes_view` を替えて頁の frame を組み直す必要がある。4 px/pt の帯の画素は A4 で約 8 Mpx、Pi の全体表示の頁の絵（約 1.2 px/pt、約 0.7 Mpx、推測）の約 11 倍。§4.3 は Pi の描き直しを 0.3〜0.6 s と見積もる。
- 起こる状況（推測、未測定）: 囲まれていない所（D12）や大きな所の塗りは 8 本の帯を全部描き、wash の多い頁で 3〜6 s、小さな所でも 1〜2 本で 0.4〜1.5 s。CPU の geometry も帯の数だけ組む。§4.5 の「100〜300 ms」は flood だけの数で、p009 で初めて分かる。
- 直し方: §3.4 の 3 を「帯ごとに view（x = 0、y = −帯の上、scale = R）で頁の frame を作る。帯と bounds の交わらない stroke は入れない」に直す。§4.5 に GPU の描画の見積（帯の画素の数 × 描き直しの単価）を足す。p009 の手当ての候補に「遅い device では R を 2 px/pt に下げる」「tap の点の周りの矩形から広げる」を書く。

### M-2. 帯の境をまたぐ膨張と flood の扱い

- 根拠: §3.4 の 2（tap の帯から描き、flood が帯の端に届いたら隣を描く）と 5（境を 1.5 pt 膨らませ、flood、1.6 pt 膨らませ戻す）。
- 起こる状況: 帯の端から 6 px 以内の行の膨張は隣の帯の境の画素に依る。隣の帯を読む前にその行を flood すると、隣の帯の線で閉じるはずの隙間から漏れる、または隣の帯を読んでから膨張が変わり、flood 済みの画素が境になる。膨らませ戻しと marching squares も帯をまたぐ。p005 の host 試験（`host-fill.c`）は 1 枚の raster なので見つからない。
- 直し方: 「膨張の半径 + 膨らませ戻しの半径の行は、隣の帯を読むまで確定しない（flood はそこで止めて待つ）」か「帯は膨張の半径だけ重ねて描く」を §3.4 に書く。p005 の試験に「同じ raster を 1 枚と帯に分けた時に領域が bit で同じ」を足す（帯の高さ 64 px・440 px で、帯の境に 1 pt の隙間の円を置く）。

### M-3. 0.1 pt の潜らせが表せず、塗りと線の間に隙間が残る

- 根拠: 1 画素 0.25 pt（§3.4 の 1）。fringe は各辺の外側 1 px の帯で、濃さは `1 − smoothstep(0, 1, d)`（`shaders/fill.frag`、`geometry.c` `GEOMETRY_FRINGE 1.0`）。境は「seed との差が 26/255 を超える画素」（§3.4 の 4）。
- 起こる状況: 黒のペンの fringe は d < 約 0.75 px で差が閾値を超えるので、境は線の polygon の外側約 0.19 pt まで広がる。flood の領域はそこで止まり、0.1 pt（0.4 px、2 値の mask では 0 か 1 px）膨らませても線の縁に届かない。拡大と PDF の高い倍率で、塗りと線の間に細い白い線が出る。
- 直し方: 膨らませ戻しを「閾値で境になる fringe の幅（約 0.75 px ÷ R pt）+ 0.1 pt」とし、marching squares の後の輪郭を vector として外へずらす（pt で、画素の量子化に依らない）。最も細いペン（0.225 pt）でも塗りの縁が線の向こう側へ出ないことを試験で確かめる（線の両側を別の色で塗った時に、塗りが線の外へ出ないこと）。

### M-4. 幅の広い頁で帯の幅が 4096 px を超える

- 根拠: §3.4 の 1 `R = min(4, sqrt(16 Mi / 面積))`。Pi は image の各辺 4096 まで（`bcm2711/vulkan-resource.c:249-252`）、Notes の頁の絵も 4096 まで（`app.h:56`）。A3 横長（1191×842 pt、面積 1.0 M pt²）で R = 4、幅 4764 px。Tabloid・Letter の横長も同じ。
- 起こる状況: Pi で帯の image の作成が断られる。§3.4 の 2 は `VK_ERROR_OUT_OF_DEVICE_MEMORY` の時だけ帯を半分にするので、塗りが失敗する（notice の文言も無い）。他の PDF の上で起こりやすい。
- 直し方: `R = min(4, 4096 / 頁の幅, sqrt(16 Mi / 面積))`（または帯を横にも分ける）。p005 の試験に横長の A3 の頁を足す。

### M-5. 部分消しで円の中に水彩（と筆）の縁が残る

- 根拠: `notes_document_erase_parts_at()` は `stroke_split(…, radius + stroke->width / 2.0f, …)`（`document.c:741`）で切り、切れ端の端は丸い cap（§3.1）。`stroke_touches()` も `reach = radius + width / 2`（`document.c:1389`）。§3.5 は「当たりの幅は筆・鉛筆は係数の最大、水彩は芯」とするが、切る reach は今のまま。
- 起こる状況: 水彩 28 pt・消しゴム 10 pt: 切り口は中心から 24 pt、縁 2 の cap の半径は最大 1.47 × 14 = 20.6 pt なので、cap が中心から約 3.4 pt まで入り、円の中に alpha 0.06・0.10 の Multiply の縁が残る（消えたように見えない）。筆（F 最大 1.15）も cap が円の中へ約 0.9 pt 入る（12 pt の筆）。p003 の試験（円の外の角が同じ）では見つからない。
- 直し方: stroke ごとの「見える半幅」（新しい道具は焼き込んだ係数の最大 × w/2、水彩は縁 2 の最大）を `stroke_touches()` と `stroke_split()` の両方の reach に使う。p003 の試験に「部分消しの後、全部の shape の polygon が半径 radius − 0.01 pt の円と交わらない」を足す。

### M-6. 粒の pattern の Matrix は頁ごとに要る

- 根拠: §5.2「色ごとに 1 つの tiling pattern、Matrix は頁の既定の座標で 48 pt の大きさと頁の左上を原点とする向き」。pattern の Matrix は pattern の空間を頁の既定の座標へ写し、content の `cm` に依らない。Notes の座標は、新しい頁では `1 0 0 -1 0 h cm`（`writer.c:150` の前後、h は頁の高さ）、他の PDF の上の頁では `shown_to_user(&box, matrix)`（`update.c:330-341`、crop box の原点と回転）で写される。writer の resources は文書で 1 つ（`pdf_writer_write_resources()`、`writer.c:1140-1172`、全部の頁が同じ dictionary）。
- 起こる状況: 頁の高さ・crop box・`/Rotate` が違う頁の混ざる文書（他の PDF の上は普通にある）で、1 つの pattern では粒が画面の頁の点とずれ（回転の頁では向きも違う）。p003 の試験（A4 の 1 頁）では見つからない。名前も base の resources とぶつからないよう `name_prefix` を付ける必要がある（ExtGState と同じ）。
- 直し方: pattern の鍵を (色, 頁の行列) にし、Matrix = 頁の行列 × 48 pt の拡大とする。名前に `name_prefix` を付ける。p003 の試験に「高さの違う 2 頁」と「`/Rotate 90` の他の PDF の上の頁」の粒の向きを足す。

### M-7. reader の tiling pattern の cell の解像度

- 根拠: display list は倍率を持たず、倍率は `pdf_display_list_rasterize(list, …, scale, …)`（`main.c:1151` の呼び出し）で決まる。§5.2「cell の内容を pattern object ごとに一度だけ描いて持ち、…cell の画素は display list の画素の数に一度だけ数える」。
- 起こる状況: display list を作る時に cell を固定の解像度で画素にすると、拡大した PDF Viewer で一般の PDF の pattern（文字・線の cell）がぼける。raster の時に描くなら「一度だけ」は raster の呼び出しごと・倍率ごとで、cell の中身（content・入れ子の pattern・shading）を raster の中で再帰に描く仕組みが要る。どちらかで p002 の大きさが変わる。
- 直し方: p002 の設計で決めて書く。推奨: display list には cell を「部分の display list（または画像の cell なら画像そのもの）」として 1 回だけ持ち、raster の時に倍率で cell を 1 回画素にしてから敷き詰める。cell の画素の上限（例 cell の画素が 4 Mi px を超えたら飛ばす）と入れ子の深さの上限を書く。

### M-8. 鉛筆の区間の「重なりも隙間も無い」は自分と重なる stroke で成り立たない

- 根拠: `outline.c:20-29`（1 つの polygon は台形・扇の和を nonzero で塗る）。区間に分けると、区間 A の台形が区間 B の台形と重なる所（ループ、鋭い角の直後の切り口で台形が切り口の線を越える所）は別々の fill で二度塗られる。
- 起こる状況: 筆記体の鉛筆で、区間の境のまたぐ重なりが濃い点になる（画面と PDF は同じ）。§3.2・§8 p002 の受け入れ「区間の polygon の面積の和が全体の面積と許容差で一致」は、重なる stroke では面積の和が大きくなって落ちる。
- 直し方: §3.2 に「区間をまたぐ自分の重なりは二度塗られる（画面と PDF で同じ。制限）」と書き、面積の試験は自分と重ならない stroke に限る。gentle な頂点を選ぶ条件に「前後の半径 r の弧長の中に gentle でない頂点が無い」を足す（切り口の近くの重なりを避ける）。

### M-9. 範囲の輪郭の API の境の index と継ぎ目の辺

- 根拠: §5.1「libpdf は各候補を後ろの一番近い gentle な頂点へずらし、実際の境の index を返す」。gentle は中心線（spline を切った後の頂点、`outline.c` `struct pdf_outline_vertex`）の性質で、点（sample）の index ではない。§2 の `seam_start`・`seam_end` は「始めの切り口が共有の辺か」だけで、polygon のどの辺かは言わない。
- 起こる状況: Notes は返った index を点の段（`steps`）と結べない。画面は polygon のどの辺に fringe を出さないかを知らない（`notes_frame_polygon()` は全部の辺に fringe を出す、`geometry.c:240-280`）。「後ろ」が前か後かも読めない。
- 直し方: p002 の API で決めて書く: 返すのは区間ごとの polygon と「各区間の段（候補の index の段）」、継ぎ目は polygon の決まった辺（例 最後の角から最初の角への閉じる辺を始めの切り口、など）か継ぎ目の辺の index の列。「後ろ」は「弧長の増える向き」と書く。

### M-10. 道具の定数の版の持ち場

- 根拠: §5.4「`TOOL`: 曲線の id の byte を道具の定数の版に、`tools_find` の鍵は (種類, 色, 幅, 定数の版)」。`tools_find()`（`encode.c:763-800`）は stroke の field で比べるので、`struct notes_stroke`（`notes.h:135-159`）に field が要る。journal の stroke は `TOOL` を使わず、種類・色・幅・flags・開始を直に書く（`notes_encode_stroke()`、`encode.c:628-650`、journal.c の注釈「with its tool written out」）。
- 起こる状況: journal 3 の record が定数の版を持たないと、将来定数の版を上げた時に、復旧した stroke の形が保存した形と違う（または版を推測で決める）。「知らない定数の版は他の PDF として開く」の規則も journal の復旧には効かない。
- 直し方: `struct notes_stroke` に定数の版を足し、journal 3 の stroke の record に 1 byte で持つ、と §5.4 に書く。知らない定数の版の stroke を持つ journal は復旧せず set aside する（`notes_journal_set_aside()`、今の edits の不一致と同じ扱い、`main.c:919-927`）。

### M-11. D7 (c) の判定で、線の近くの消しゴムが塗りを消す

- 根拠: §3.5「最初に何かに当たった時、円の中心で一番上に見えている物が塗りなら、その操作は塗りも消す」。線の当たりは円（`MAIN_ERASER_RADIUS 10.0f`、`main.c:102`）と `radius + w/2`、塗りの当たりは中心だけ。
- 起こる状況: 塗った囲みの線を消そうとして、中心が線（1.5〜6 pt）から少し内側に外れた所に下ろすと、円は線に当たり、中心は塗りの上なので「塗りも消す操作」になり、塗り全体が線と一緒に消える。D12 の頁全体の塗り（色の背景）の上では、線の近くで消しゴムを下ろすたびに背景が消えやすい。undo はできるが、(c) の「見えている所に触れたらその物を消す」の意図（線を消したい時は線だけ）とずれる。
- 直し方（推奨、D7 (c) の範囲の技術の詰め）: 最初の当たりで、円が塗り以外の線に当たっていれば「線の操作」（中心が塗りの上でも）、どの線にも当たらず中心が塗りの内側の時だけ「塗りの操作」。p005 の試験に「中心が線から 3 pt 外れた塗りの上で下ろすと、線だけが消える」を足す。

### M-12. Linux・FreeBSD の build

- 根拠: `userland/desktop/notes/Makefile.linux:3-18`・`Makefile.freebsd` と `userland/base/libpdf/Makefile.linux` は source の一覧を自分で持つ（zedBSD の `Makefile` と別）。Notes・libpdf は WS108・WS112 の Linux の package に入る（推測、package の一覧は見ていない）。
- 起こる状況: 新しい `.c`（粒の生成、塗り）と shader を zedBSD の Makefile だけに足すと、Linux・FreeBSD の build が link で落ち、§8 の確かめ（amd64・rpi4 の zedBSD だけ）では分からない。
- 直し方: §8 の build の確かめに Linux の host の build（`Makefile.linux` の有限の target）を足し、p002〜p006 の「影響する file」に 3 つの Makefile を書く。FreeBSD は build の確かめができなければ未実施として記録する。

### M-13. Save Clean Copy の試験が無い

- 根拠: `clean.c:8-36`: 頁の resources は content が名指す名前だけを残し、pattern が自分の resources を持たなければ頁の resources からその content の名前を足す。Notes は File > Save Clean Copy（`notes_save_clean_copy()`、`save.c:159-190`）で編集 data を落とした PDF を書く。
- 起こる状況: clean copy で、pattern の cell の画像・粒の SMask・`/BM` の ExtGState が落ちるか（pattern の content の Flate を clean.c が読めないと「resources を丸ごと残す」側に倒れるので落ちない見込み、推測）。落ちれば clean copy の鉛筆が消え、水彩が Normal になる。どの段の試験にも無い。
- 直し方: writer は pattern に自分の `/Resources`（cell の画像）を持たせる。p003 の試験に「3.0 の notebook の clean copy を `qpdf --check` と `pdftoppm` で描き、元と画素が近い」を足す。

## 3. D1〜D12 とユーザーの決定

- ws.md の「ユーザーの決定」と phase.md §10 は一致している（D7 (c)、D8 の major 3 だけ、D10〜D12 の中身）。
- D10〜D12 の出どころ（軽微、記録）: ユーザーの「ではあとは推奨でお願いします」の時、D10〜D12 には推奨が無かった（review-1 §5 は選択肢だけで、D10 は「ユーザーの判断」と書いた）。ws.md は「第 2 版で設計者が推奨を決め、その推奨を採った」と正しく書いている。D10 は Guardrail を緩めない側（Pi を受け入れに含める）なので問題は小さいが、D10〜D12 の中身（Pi で WS216 の完了が WS141 p017・p019 を待つこと、libpdf の reader に tiling pattern を足すこと、頁全体の塗り）は、Q1 が次の報告でユーザーに示すのがよい。
- D5（CPU の flood fill）: Guardrail「画像の処理は GPU で」の例外の扱いは D5 の推奨の文（「内容が変わった時の一度だけの用意」として扱う）ごと採られている。M-1 の時間が Pi で数秒になると「体験を損なう」の懸念に当たるので、p009 の数でユーザーに示す。
- D11: 古い reader で鉛筆が「消えない」は source で正しい（§1 の S1）。
- D12: M-4（幅の広い頁）と M-11（頁全体の塗りの上の消しゴム）が D12 の使い方に効く。

## 4. 段の分け方・依存・見積

- 依存の表（p002 → p003 → p004 → p005 → p006 → p007 → p008、p009 は p007 と WS141 p017・p019）は矛盾が無い。p004〜p006 を同じ担当にする根拠（`main.c`・`render.c`）も正しい。
- 粒の tile の生成の code は p003（PDF の SMask、`save.c`）と p004（画面の texture）の両方が使う。p003 で作る、と書く（L-12）。
- p002 の 3.5 LW は、M-7（一般の tiling pattern の cell の描き方）の選び方で増える。cell を画像だけに限る（PaintType 1 で cell が画像 1 つの pattern だけ描き、他は今どおり飛ばす）なら 3.5 に収まる見込み（推測）。限るなら §5.2 にそう書き、PDF Viewer の一般の pattern は Future Work にする。
- p009 の前提の WS141 p017 は planning（「(b) の後の候補」、`plan/ws141/ws.md:82`）、p019 は案。WS216 の completed は長く待つ。ws.md の書き方はそれを正しく言っている。

## 5. p002 を始めてよいか

**条件つきで始めてよい。** 致命的・重大は無い。p002 の範囲（libpdf）に効く M-6（pattern の Matrix と名前を頁ごとに）、M-7（cell の描き方と上限）、M-8（面積の試験の範囲と gentle の条件）、M-9（範囲の輪郭の API の index と継ぎ目）を、p002 の phase.md の設計と受け入れに入れてから実装に入る。M-13（pattern に自分の resources）と L-3（hash の上限の対称）も p002 で書けば足りる。

p003 以降の前に直すもの: M-5・M-10・M-12（p003）、M-1〜M-4・M-11（p005、p005 の phase.md で）。

## 6. 軽微

- L-1: §3.4 の 3「push constant の原点と倍率を替える」は事実と違う。push constant は target の大きさだけで、頂点は CPU が view で画素にする（M-1）。
- L-2: §1.4「頁の content は無圧縮」の補い: 他の PDF の上の編集した頁（ws175）の content はすでに Flate（`update.c:396` `pdf_writer_pack()`、`update.c:1594-1596`）で、hash は KEEP・OVERLAY・EDIT の頁では記録しない（`writer.c:771-776`）。p002 の content の Flate は `pdf_writer_pack()`（`update.c:2485`）を使い回せる。
- L-3: reader の decode の上限 64 MiB は、既存の `PDF_FILTER_OUTPUT_MAX` 256 MiB（`internal.h:140`）と別の数。writer の側で hash を記録する頁の content がこの上限を超える時は hash を記録しない（または同じ定数を共有する）と書く。超えると自分の保存した頁が CHANGED になる。
- L-4: 帯の境の判定「各 channel 26/255 を超える」は「どれか 1 つの channel」か「全部」かを書く（どれか 1 つ、を推す）。薄い鉛筆（q = 0、α ≈ 0.105）の橙（`0xea580c`、`ui.c:127`）は B の差が約 25 で境にならない。
- L-5: 粒の noise の「格子 1 pt 相当」と「格子は 256 で wrap」が合わない（1 pt = 5.33 texel なので 1 tile に 48 格子、256 格子なら周期 256 pt）。格子の間隔を texel で書く（例 4 texel、1 tile に 64 格子で 2 の冪にして整数で wrap）。
- L-6: 頁の倍率が約 0.89 px/pt 未満（小さな window）では一番粗い段（64×64）でも 1 画素あたり 1.5 texel を超え、aliasing が出る。4 段目（32×32）を足すか制限として書く。段の切り替えで見え方が跳ぶことも書く。
- L-7: §3.4 の 5「1.5 pt（R で 6 px）」は R = 4 の時だけ。「1.5·R px」と書く。
- L-8: §2「pen を離した時に最後の計算の値を焼き込む」: `app_end_contact()` は最後の sample を足してから stroke を入れる（`main.c:2034-2038`）ので、最後の frame の値ではなく、最後の点を足した後に同じ式で計算し直して焼き込む、と書く。指の取り消し（`app_abort_contact()`）では焼き込まない。
- L-9: journal 3 も「表せる一番低い版」を選ぶ（新しい道具が無ければ版 2）と書く。今の Notes は版 3 の journal を復旧できず、file を開いた後の最初の変更で journal を上書きする（`journal_start()`）ので、古い image に戻した時に未保存の変更が消える。新しい道具の無い文書でまで起こさない。
- L-10: L10（塗りは `TIME` を立てない）は文書の encoder（`encode.c:432-437`）と journal の encoder（`encode.c:639-643` は flags と `start_ms` を常に書く）の両方に要る。
- L-11: Notes の descriptor の pool は `maxSets = NOTES_TEXTURES`（`render.c:1467-1470`）。粒の 3 つの set の分を足す、と §6.1 に書く。sampler は今 NEAREST（`render.c:1438-1439`）なので、帯の画面外の描画の背景の texture は最近傍で拡大される（§3.4 の 3 の制限の一部）。
- L-12: 粒の tile の生成の code の置き場所（p003 で Notes に作り、p004 が使う）を書く。
- L-13: 新しい Notes は 1.1・2.0 の file の種類 2〜5 の stroke を拒む（または他の PDF）と書く。今の decode は種類を検査しない（`encode.c:1010-1022`）。
- L-14: 鉛筆の区間の候補の後ろに gentle な頂点が無い時（stroke の終わりまで）、その段の変化は捨てられる、と書く。
- L-15: ws.md の p009 の依存「WS141 の Multiply の blend の依頼」は WS141 p019（案）と名指す。ws.md の p004 の「頂点の塊（16 MiB の上限）」は §4.2 の 4 MiB の塊と読み違えやすい（「16 MiB の device の上限に当たらない 4 MiB の塊」）。
- L-16: p002 の「鉛筆 200 本相当の頁」の試験に、`PDF_DISPLAY_LIMITED` の画素だけでなく、path の点（`PDF_DISPLAY_POINTS_MAX` 8 Mi）と item（`PDF_DISPLAY_ITEMS_MAX` 1 Mi、`internal.h:146-147`）の数を記録する。水彩は 1 本で fill 3 つ、鉛筆は区間ごとに 2 つで、点の数の上限に先に当たりうる（推測）。
- L-17: §4.2「全部が 1 つの command buffer の 1 つの render pass の中」は「1 つの polygon の draw は全部同じ render pass の中（頁の絵の pass か window の pass）」と直す（`render.c:2097-2147`）。
- L-18: 水彩の z の決め方（§3.4 の 8）: tap の点が縁 2（alpha 0.06）の中なら塗りは水彩の上に入り、その薄い縁を覆う。縁 2 は境にならない（差 ≤ 15）ので、塗りの領域は縁 2 の下まで広がる。見た目の制限として書くか、z の「見えている物」から alpha 0.1 未満の shape を除く。

## 7. 確かめた点

- 計画: `AGENTS.md`、`plan/guardrail.md`（「画像の処理は GPU で」）、`plan/master.md:509,515,548-553,568`、`plan/ws216/ws.md`、`plan/ws216/phase001/phase.md`・`review-1.md`、`plan/ws141/ws.md:82-83`。
- Notes: `notes.h:35-50,110-300`、`app.h:20-140`、`render.c:180-560,1087-1118,1423-1500,1610-1628,1690-1990,2013-2230`、`geometry.c:1-500`、`shaders/draw.vert`・`fill.frag`、`main.c:100-108,895-1010,1053-1190,1829-1860,2023-2200,2257-2625,2700-2720,3308-3360`、`document.c:205-272,602-806,1368-1412,1455-1740`、`encode.c:1-130,300-470,628-1032`、`journal.c:1-120` と `journal_ready()`、`save.c:229-372,600-710,1120-1160`、`ui.c:120-145,347-380,400-420,1095-1120`、`Makefile`・`Makefile.linux` の source の一覧。
- libpdf: `outline.c:1-170,271-330`、`writer.c:100-260,740-790,915-935,1140-1200,1890-1915`、`update.c:280-440,1570-1610`、`reader.c:500-580,3690-3720,3930-3945`、`content.c:1-45,770-860,2378-2392,3295-3352`、`editor.c:1844-1900` と `editor_draw_text()` の出す演算子、`clean.c:1-80`、`internal.h:140-148`、`include/libc/pdf.h:88-100,255-265`、`exports.map`。
- driver: `bcm2711/vulkan-pipeline-state.c:1-80,200-260`、`vulkan-pipeline-build.c:95-140`、`native-bin.c:40-140`、`vulkan-native-bin.c:60-100`、`vulkan-query.c:15-25,320-335,378-395`、`vulkan-resource.c:15-30,245-262`、`vulkan-input.c:360-395,470-495`、`vulkan-buffer-copy.c`・`vulkan-draw.c` の `COPY_IMAGE_TO_BUFFER`、`i915/render/state.c:170-215`（Multiply の係数）、`i915/memory.h:87`、`i915/render/command.c:1198,2153`（`vkCmdCopyImageToBuffer`）。
- 道具: `plan/tools/aat/scenarios/helpers_notes_edit.py:84-106,518-528`、`tests/scenarios/apps/notes/` の一覧、`NOTES BUTTONS`・`NOTES TOOL` を使う scenario の一覧、`plan/ws079/tests/`・`plan/ws081/tests/` の試験の file、`config/ci/config-rpi4.mk`・`config/current-uat.mk` の存在。
- 計算: A4 の R と帯の大きさ、A3 横長の帯の幅、水彩・筆の係数の最大と部分消しの cap の入り込み、帯の画素の数と頁の絵の比。

## 8. 確かめていない点

- 実測は何もしていない（M-1 の時間、L-16 の点の数、M-3 の隙間の幅は source の式からの推測）。
- bcm2711 の BLEND_CFG の packet（`native-bin.c` は blend の有効の bit だけを出す）の既定の値と、Multiply の係数を CL に落とす作業の大きさは見ていない（WS141 p019 の範囲）。`vkCreateGraphicsPipelines` の `ENOTSUP` が libvulkan でどの `VkResult` になり、device が使えるままかは読んでいない（`vulkan-pipeline-build.c` が作成の時に返すことだけ確かめた）。
- i915 と bcm2711 の shader の compiler が、texture の sample と stencil の pipeline の組（`FRINGE_GRAIN`・`COVER_GRAIN`）を受けるかは見ていない。
- PDF の tiling pattern の `/ca` の当て方（pattern 全体に掛かるか）は仕様書と viewer で確かめていない（p002 の `pdftoppm`・Ghostscript の比較で分かる）。
- `clean.c` が Flate の pattern の content の名前を読めるか（M-13）は読んでいない。
- Notes・libpdf の Linux・FreeBSD の package の範囲（M-12）は Makefile の存在だけで、WS108・WS112 の package の一覧は見ていない。
- `ui.c` の toolbar の幅（約 1100 px）と button の数は数えていない（`NOTES_BUTTONS` 48 なら足りる見込み）。
- text box を開いている間に B・L・W・F の key が文字として box に入るか、道具を替えるか（今の P・M・E・T と同じ扱いの見込み）は読んでいない。
