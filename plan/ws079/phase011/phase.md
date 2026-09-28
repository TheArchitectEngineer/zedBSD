<!-- awesome-plan project=zedbsd record=ws079-p011 -->

# ws079-p011: Notes の仕上げ（PDF の中の名前を Kei に、Kei の glass の toolbar、pen の hover、描画の cache、部分の消しゴム）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、Kei desktop subagent。QEMU の Venus guest と host の証拠だけ。実機（i915・USB の pen）は未実施。clearance は main の判断で覆してよい）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28、2026-10-17 のデモに向けて）。Awesome Plan の Queue の item ではない
Resume point: 下の「残り」
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. PDF の中に見える名前を Kei に（main の判断、[design-input-notes.md](../design-input-notes.md) の末尾）: `/Producer (Kei Notes)`、
   添付 `kei-notes.bin`・`application/x-kei-notes`。互換は要らない。libpdf は writer の producer の文字列だけに触れる（p006 が reader と content を編集中）。
2. Notes の toolbar と外回りを Kei の見た目に（[kei-identity-design.md](../../ws035/kei-identity-design.md)）: files と titlebar の glass の調子
   （すりガラスの半透明、slate の文字、選んだ道具は Kei の青）、page の後ろの柔らかい背景、すっきりした全画面、pen の近接中の cursor か hover の点。
3. 速さ: 確定した stroke を page の画像に焼き（design-input-notes §5.3）、毎 frame は描いている stroke だけを描く。Venus で多数（例 500）の
   stroke の frame の時間を前後で測る。
4. 時間があれば部分の消しゴム（消しゴムが横切った所で stroke を切る）。

試験: `notes-p005.sh`・`notes-pen.sh`・`notes-gesture.sh`・`run-notes-host.sh` を再び通す。画面は `build/ws035-shots/ws079-p011-20260928-*.png`。

## 1. 名前

| file | 変更 |
| --- | --- |
| `userland/base/libpdf/writer.c` | libpdf の writer は producer を呼び手から受けず固定の文字列を書くので、その既定を変えた。**main へ: 触れたのは 2 行だけ** — 1519 行（`write_information()`）`/Producer (zedBSD Notes)` → `/Producer (Kei Notes)`、1602 行（`write_attachment_objects()`）添付の説明 `/Desc (zedBSD Notes edit data)` → `/Desc (Kei Notes edit data)`（どちらも PDF の中で見える文字列。reader・content・API は不変） |
| `userland/desktop/notes/notes.h` | `NOTES_ATTACHMENT_NAME` `"kei-notes.bin"`、`NOTES_ATTACHMENT_TYPE` `"application/x-kei-notes"` |
| `plan/ws079/design-pdf.md` | §1 の `/Info`、§2 の添付の名前・subtype・説明、他の viewer の説明 |
| `plan/ws079/tests/run-notes-host.sh`・`notes-p005.sh` | `qpdf --show-attachment=kei-notes.bin` |

libpdf の試験（`host-pdf-writer.c`・`host-pdf-reader.c`・`run-pdf-writer.sh`）は添付の名前を呼び手として自分で渡しているだけなので変えていない
（p006 が reader の試験を編集中）。Notes の shader の source の先頭の注釈（`// zedBSD Notes: ...`）は画面にも PDF にも出ないので残した。

## 2. Kei の見た目（toolbar・背景・全画面・pen の印）

| 点 | 決めたこと | 理由 |
| --- | --- | --- |
| toolbar | 上の中央に浮かぶ **すりガラスの card**（`ui.c`）: 白の veil（alpha 210/255）、上の内側の白い rim、薄い slate の縁、下へ落ちる柔らかい slate（`0x1f3a66`）の影、角の半径 18 px。文字は slate（`0x1e2632`、ページ番号は `0x6b7585`、使えない button は `0xa3abb8`）、選んだ道具と太さは **淡い Kei の青の pill（`0x2f7cf6` alpha 52）に青の文字・点**、選んだ色は青の輪、group の間に細い区切り線。値は files（`files.h` の FM_COLOR_*）と compositor の glass（`panels.c`）に合わせた | files の sidebar の選択（淡い青の地に青の文字）と titlebar の glass の card に揃える。最初は選んだ道具を青で塗りつぶしたが、files と調子が違うので淡い pill にした |
| card の描き方 | CPU で straight alpha の B8G8R8A8 に描く（card の外は透明、card は半透明）。GPU は toolbar の画像を straight alpha で背景に重ねる。card の幅は 1 回目の layout で測り（`ui->drawing = 0`）、中央に置いて 2 回目で描く。幅は status に依らない（button の位置が動かない。試験は `NOTES BUTTONS` を幅ごとに 1 回だけ読む） | 画像 1 枚のまま、背景が card を透けて見える |
| status | card の右（入らなければ左）に小さな glass の pill。どちらにも入らない長い status は出さない | card の幅を status で変えない |
| toolbar の帯 | 高さ 52 → 68 px（card は y 8〜60、残りは影） | 浮いた card と影の余白 |
| 背景（desk） | 窓全体に Kei のぼかした風景に似せた wash: 上の淡い空色 `#d9e6f5` → 58% の高さの霞 `#eef3f6` → 下の淡い若葉 `#dfecd6`（2 枚の gradient）。page の影は page の周り 12 px の 1 px の輪（4 本の細い帯）で alpha 44 から外へ 2 乗で消え、3 px 下へ落とす | kei-identity-design の「明るくぼかした風景、淡い水色と若葉の緑」。影を page 大の矩形の重ねにすると Venus（lavapipe）で page 全面を 10 回塗るので、輪の帯だけを描く |
| 全画面 | title も menu も無い全画面は、背景・浮いた card・page だけ | すっきりした全画面 |
| pen の印 | tablet の tool が窓に近づいたら（proximity_in）`zwp_tablet_tool_v2_set_cursor(NULL)` で compositor の cursor を隠し、Notes が自分で印を描く（`tablet.c` が近接中の移動を `NOTES_INPUT_HOVER`、離れたことを `NOTES_INPUT_LEAVE` にする）。page の上では **pen・蛍光ペンはその色と太さの点（白い halo 付き）**、**消しゴム（道具か pen の消しゴムの端）は消す範囲の輪**（半径 10 pt、薄い slate の塗りと輪）。toolbar の上では小さな slate の点。書いている間は印を出さない（インクが位置を示す）。消している間は輪を出す。mouse（pointer）は今までどおり compositor の矢印 | tablet の近接の間の位置と道具の大きさを見せる。`NOTES HOVER source=N x= y=` と `NOTES HOVER gone` を試験用に出す |

## 3. 確定した stroke の page の画像（cache）

- `render.c`: page の画像（swapchain と同じ format、`COLOR_ATTACHMENT | SAMPLED` の optimal image、page の大きさの整数 pixel）と、
  それに描く 2 つの pass（clear から始める pass と、前の画像に足す `LOAD` の pass。どちらも最後に `SHADER_READ_ONLY_OPTIMAL`、stencil は窓の
  stencil buffer を共有）を持つ。pass の attachment の format は窓の pass と同じなので、同じ 5 つの pipeline がそのまま使える。
  descriptor set は 2 つ（toolbar と page）。`notes_renderer_page()` が大きさの違うときだけ画像を作り直し、`page_serial` を増やす。
  `notes_renderer_draw(frame, page_frame, page_clear)` は page frame を先に page の画像へ描き、続けて窓の pass を描く（頂点は同じ buffer に page frame、frame の順）。
- `notes.h`・`document.c`: `document->reshaped`（上に stroke を足す以外の変更 — 下への挿入、stroke の除去、page の挿入・除去 — で増える）。
- `main.c`（`app_page_frame()`）: 画像が今の page・今の `page_serial`・今の `reshaped`・今の scale で描かれ、stroke が減っていなければ、
  **増えた stroke だけ**を LOAD の pass で足す。それ以外（page の移動、undo・消しゴム、大きさの変更）は白い page から全部を描き直す。
  毎 frame は、背景・影・page の画像（texture の矩形 1 つ）・描いている stroke・pen の印・toolbar だけを描く。
  `NOTES PICTURE full strokes=0..N` と `NOTES PICTURE add strokes=A..B` を試験用に出す。
- frame の時間: `main.c` が frame ごとに geometry を作る時間（build）と描く時間（submit・present・GPU の完了の待ち、draw）を測り、
  contact の終わりに `NOTES FRAMES count= strokes= build_us= draw_us= frame_us= longest_us=` を出す（試験と計測用の log の行）。

### 計測（2026-09-28、QEMU の Venus guest、同じ guest・同じ image で binary だけを入れ替え）

命令: `NOTES_BINARY=<binary> plan/ws079/tests/notes-perf.sh OUT 500 5`（host で `notes-many.c` が page 1 に 500 本の手書き風の輪の stroke
（各 40 点）を持つ Notes の PDF を作り、guest の `/bin/notes --fullscreen` で開き、QMP の pointer で 60 移動の波を 5 本描く）。
前 = `a09e91a3` の Notes（毎 frame 全 stroke を描く、計測の行だけ足したもの。`build/p011/notes-base`）、後 = この Phase の最終（`build/p011/notes-final`）。
guest で `cksum /bin/notes` を確かめてから計測した。

| 条件 | frame の平均（build + draw） | build（CPU の geometry） | draw（submit〜GPU 完了） | 最長 | 1 本（約 1.3 s）の間の frame 数 |
| --- | --- | --- | --- | --- | --- |
| 前、500 本（2 回 × 5 本） | 187〜210 ms | 39〜47 ms | 147〜167 ms | 196〜307 ms | 7〜8 |
| 後、500 本（2 回 × 5 本） | 108〜118 ms | 1.2〜2.0 ms | 106〜116 ms | 120〜130 ms | 13〜14 |
| 前、空の page（3 本） | 110〜115 ms | 1.2〜2.3 ms | 109〜113 ms | 123〜129 ms | 13 |
| 後、空の page（3 本） | 107〜115 ms | 1.8〜2.2 ms | 105〜112 ms | 113〜129 ms | 13〜14 |

読み: 500 本の page でも、後は空の page と同じ frame の時間になった（build は 20〜30 分の 1、frame は約 45% 減）。
この環境の約 107 ms の下限は Venus（host の lavapipe による CPU の描画、virtio の往復、present）の 1 frame の費用で、Notes の描く量に依らない
（空の page で前後とも同じ）。i915 の実機の frame の時間は未計測。stroke を消したとき（部分の消しゴム・undo）は page の画像を全部描き直すので、
その 1 frame は前の 1 frame と同程度かかる（未計測）。

画面: `ws079-p011-20260928-perf-500-before.png`（前）、`ws079-p011-20260928-perf-500.png`（後）。

## 4. 部分の消しゴム

| 点 | 決めたこと | 理由 |
| --- | --- | --- |
| 操作 | 消しゴムを選んでいる時にもう一度 Eraser（toolbar・Tool の menu・E の key）を選ぶと、**stroke 単位** ⇄ **部分**を切り替える。部分のとき toolbar は「Part Eraser」（button の幅は長い方の label に合わせて固定）。既定は stroke 単位。pen の消しゴムの端も選んだ mode で消す | design §5.2 の「もう一度押すと stroke と部分を切り替え」。既定を stroke 単位に保ち、p005・pen の試験の期待（`removed=1`）を変えない。menu の item を足すと `notes-home.sh` の items=18 が変わるので足していない |
| 切り方 | `notes_document_erase_parts_at()`: 円（半径 10 pt）に触れた stroke の path を、円を stroke の幅の半分だけ広げた円で切る。各 segment と円の交わり（2 次方程式の 2 解を [0,1] に切る）を除き、残りを path の順に piece にする。円を横切る所では piece は交点で終わり・始まる（x・y は 1/64 pt の格子、筆圧・傾き・時刻は補間）。2 点に満たない piece は捨てる。各 piece は元と同じ道具・色・太さの新しい stroke（番号は残した piece にだけ順に振る）で、start_ms は最初の点の時刻、点の時刻はそこから（ZNOT が保つ形） | design §5.1 の「円の内の点で stroke を切り、外の区間を新しい id の stroke にする（境界は線分と円の交点を補間して足す）」 |
| 重なり | piece は元の stroke の場所に順に入れる（上下の順を保つ） | 蛍光ペンなどの重なりが変わらない |
| undo | 1 回のドラッグの切断を 1 つの undo の entry（`NOTES_UNDO_ERASE_PARTS`）にまとめる。entry は行った primitive（stroke の除去と piece の挿入）を順に持ち、undo は逆順に戻し、redo は順に行う。同じドラッグで piece をさらに切るのもそのまま記録される。所有: entry が立っている間は除いた stroke、戻した後は入れた piece を持つ | 既存の primitive（journal に記録される）の組み合わせだけで表し、journal の復元と ZNOT は変えずに済む |

## 実装（commit は `WIP`、この worktree）

| file | 内容 |
| --- | --- |
| `userland/base/libpdf/writer.c` | 上の 2 行（名前） |
| `userland/desktop/notes/notes.h` | 添付の名前、`NOTES_INPUT_HOVER`・`NOTES_INPUT_LEAVE`、`document->reshaped`、`NOTES_UNDO_ERASE_PARTS` と entry の `inserted`、`notes_document_erase_parts_at()` |
| `userland/desktop/notes/document.c` | `reshaped` の数え方、部分の消しゴム（`notes_document_erase_parts_at`・`stroke_split`・`segment_inside`・`point_between`・`piece_add`・`piece_close`・`pieces_free`・`erase_entry`・`undo_hold_more`）、undo・redo・解放 |
| `userland/desktop/notes/app.h` | toolbar の帯 68 px、texture の番号（toolbar・page）、draw の `texture`、renderer の page の画像と 2 つの pass・2 つの set、`notes_renderer_page()`、`notes_frame_gradient()`、ui の `drawing`、ui state の `erase_parts`、tool の `leaving` |
| `userland/desktop/notes/render.c` | page の画像・pass・set、page frame を先に描く記録（`render_draws()` に分けた）、clear の色を淡い空色に |
| `userland/desktop/notes/geometry.c` | `notes_frame_gradient()`、texture の draw の画像の番号 |
| `userland/desktop/notes/ui.c` | glass の card（`ui_over` の straight alpha の over、角の丸い矩形・縁・影・輪・区切り線）、2 回の layout、status の pill、Eraser ⇄ Part Eraser の button |
| `userland/desktop/notes/tablet.c` | 近接中の cursor を隠す、HOVER・LEAVE の入力 |
| `userland/desktop/notes/main.c` | page の画像の cache（`app_page_frame`）、背景と影（`app_desk`）、pen の印（`app_hover`・`app_mark`・`app_circle`・`app_ring`）、部分の消しゴムの切り替えと呼び出し、frame の時間（`NOTES FRAMES`）、`NOTES PICTURE`・`NOTES HOVER` の行 |
| `plan/ws079/tests/notes-many.c`・`notes-perf.sh` | 計測（上） |
| `plan/ws079/tests/notes-p011.sh` | この Phase の guest の試験（下） |
| `plan/ws079/tests/host-notes.c` | 部分の消しゴムの host 試験（`check_erase_parts`） |
| `plan/ws079/tests/notes-pen.sh` | 6. pen の印（hover の pen と消しゴムの端）の段、`NOTES_BINARY` |

## 試験（2026-09-28、この worktree、最終の image `build/amd64/hdd-image.img`）

下の QEMU の 4 つの試験は、main の p006（libpdf の display list・PDF Viewer）を merge した後に image を build し直して（`build/p011-image-merged.log`、
Notes・libpdf の warning 0）、もう一度すべて PASS した。libpdf の host 試験（`run-pdf-writer.sh`・`run-pdf-reader.sh`・`run-pdf-render.sh`）も
merge の後に ok（writer の出力の `/Info` は `<< /Producer (Kei Notes) ... >>`）。

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `plan/ws079/tests/build-notes-image.sh build/amd64`（`config-amd64-notes.mk`） | exit 0、Notes・libpdf の warning 0（openssl の外部 package の既知の warning だけ）。共有の `build/llvm` の stamp は 2026-09-27 のまま（書いていない） |
| 規約の機械的な確認 | `python3 plan/tools/style-check.py userland/desktop/notes/*.c userland/desktop/notes/*.h` | 違反 0（全文の読み直しは p009） |
| host | `sh plan/ws079/tests/run-notes-host.sh` | plain・ASan・UBSan とも `host-notes: ok`（`erase parts checked` を含む: 1 回のドラッグで 2 回切る、piece の端が円の縁（278.5・301.5 pt）、新しい番号、上下の順、undo で元の 61 点の stroke に戻る、redo、journal の復元と ZNOT の往復が同じ文書、undo の後の新しい stroke で undo された切断の piece を解放（ASan の leak なし））。`qpdf --check` 誤りなし、`kei-notes.bin -> 9,0`、Producer `Kei Notes` |
| QEMU: この Phase | `plan/ws079/tests/notes-p011.sh build/p011/final-p011 …`（1 回目は起動直後の scp が 1 つ落ちて FAIL。put と button の読み取りに再試行を足して PASS） | PASS: pen で「Kei」（太字）・青の波・黄の蛍光ペン・3 行、各 stroke で `NOTES PICTURE add`、hover で `NOTES HOVER source=1`、E を 2 回で `NOTES TOOL 3 parts=1`、波を縦に消して `NOTES ERASE page=0 cut=1 strokes=12` と `NOTES PICTURE full strokes=0..12`、Ctrl+Z で 11、Ctrl+Y で 12、Ctrl+S、host で qpdf・Producer `Kei Notes`・添付 `kei-notes.bin`（ZNOT）・pdftoppm。zdesktop の ERROR なし |
| QEMU: p005 | `notes-p005.sh`（final image） | PASS（全段。`kei-notes.bin -> 9,0`、Producer `Kei Notes`） |
| QEMU: pen | `notes-pen.sh`（final image） | PASS（筆圧・傾き・消しゴムの端・保存に加え、6. hover の pen の点 `NOTES HOVER source=1`、消しゴムの端の輪 `source=2`、離れて `gone`） |
| QEMU: gesture | `notes-gesture.sh`（final image） | PASS |
| 計測 | `notes-perf.sh`（上の表） | 上 |

画面（QEMU の Venus、`/home/awe/zedBSD-rpi4/build/ws035-shots/`）:
`ws079-p011-20260928-demo.png`（**全画面の Notes に pen の stroke、新しい見た目、pen の印**）、`…-erase-parts.png`（Part Eraser で波を切った後）、
`…-pdf.png`（保存した PDF の pdftoppm）、`…-pen-hover-pen.png`・`…-pen-hover-eraser.png`（pen の印）、`…-pen-pressure.png`・`…-pen-eraser.png`・`…-pen-pdf.png`、
`…-p005-{start,strokes,undo,erase,page2,saved,recovered,reopened,pdf-page1,pdf-page2}.png`、`…-gesture-{swipe,raised}.png`、
`…-perf-500-before.png`・`…-perf-500.png`。

未実施: 実機（i915 の HDMI、USB の pen・touch）、Venus 以外の GPU（i915 での page の画像の pass・LOAD の経路）、stroke を消した frame（全部の描き直し）の時間、
`boot-test.sh`（デスクトップの Phase は描画の試験を直接行う）。

## 残り

1. i915 の実機での frame の時間と page の画像の経路の確認（Venus だけで確かめた）。
2. 消したときの全部の描き直しを、消した stroke の外形の箱だけの描き直しにする（500 本で消しゴムを動かす間の frame を軽くする）。
3. pen の印の大きさ: 細いペン（1.5 pt）の点は小さい（半径 2 px）。実機の pen で見やすさを確かめる。
4. p005 の残りのうち、この Phase で扱っていないもの: Ctrl+O の選択、他の PDF を背景に、罫線・方眼、page の削除と並べ替え、拡大。
5. `plan/coding-style.md` の全文の読み直し（p009）。
