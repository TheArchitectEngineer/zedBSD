<!-- awesome-plan project=zedbsd record=ws079-p006 -->

# ws079-p006: libpdf の読み込み ① と PDF Viewer v1

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-28、v1 の一通り。QEMU（Venus）と host。実機は未実施）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（v1 の範囲は済んだ）。残りは下の「残りと移管」
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

1. libpdf の段階 ①: content stream を display list に解釈する（q/Q、cm、m/l/c/v/y/h/re、f/f*/B/b と nonzero・even-odd、S（stroker）、
   rg/RG/g/G、ExtGState の ca/CA と BM Multiply、Do の画像（DCTDecode は libjpeg-compat、生の RGB、SMask）、FlateDecode は libz-compat）。
   display list は fill（path・色・alpha・blend）、画像、clip の push/pop。Wayland・Vulkan に依らず、上限つき。host で pdftoppm と比べ、ASan/UBSan と破壊の loop。
2. PDF Viewer v1（`userland/desktop/pdfviewer`、`/bin/pdfviewer`、画面の名前「PDF Viewer」）: argv・file chooser・Files の Open With で開く、Vulkan で表示
   （v1 は CPU の raster を texture に）、縦の連続 scroll と page 単位（swipe・PgUp/PgDn・矢印）、fit width・fit page と Ctrl+plus/minus、
   「Annotate in Notes」で `/bin/notes <file>`、標準の shortcut（Ctrl+O・Ctrl+W・Ctrl+plus/minus/0・Home/End）、Desktop の menu の group・App Home・Files の登録。
3. Venus の guest で 3 page の Notes 形式の PDF、scroll mode、page mode の swipe、zoom を試し、画面を `build/ws035-shots/ws079-p006-20260928-*.png` に。

## 行ったこと

### libpdf（`userland/base/libpdf`）

- 公開の API の追加（`include/libc/pdf.h`・`exports.map`、既存の関数は変えていない。動的 symbol 31 → 34）:
  `enum pdf_blend_mode`・`enum pdf_item_type`・`enum pdf_path_verb`、`PDF_DISPLAY_SKIPPED/DAMAGED/LIMITED`、`struct pdf_display_item`・
  `struct pdf_display_list`、`pdf_page_render()`・`pdf_display_list_destroy()`・`pdf_display_list_rasterize()`。pdf.h は `<stdint.h>` を読む。
- display list の座標は page の見える空間（pt、crop box の左上が原点、y 下向き、`/Rotate` 済み）。画像は RGBA8（非 premultiplied、1 行目が上）と
  単位正方形から page への行列。fill は path（move・line・cubic・close）と規則・色・alpha・blend。stroke は stroker で fill の輪郭になる（app は塗りだけ）。
- 新しい file:
  - `content.c`: content の解釈。graphics state の stack（q の深さ 64、超えた q は数えるだけで対の Q が戻す）、cm、w/J/j/M/d、gs（ca・CA・LW・LC・LJ・ML・D・
    BM は Normal/Compatible/Multiply、他の BM と SMask は SKIPPED）、path（m l c v y h re。h の後の l/c は閉じた subpath の始点から新しい subpath）、
    f/F/f*/S/s/B/B*/b/b*/n、W/W*（次の塗りの演算子で clip の push、Q で pop）、g/G/rg/RG/k/K/cs/CS/sc/SC/scn/SCN（DeviceGray/RGB/CMYK、CalGray/CalRGB、
    ICCBased は N。Pattern などは SKIPPED）、Do（Image と Form。Form は Matrix・BBox の clip・自分の Resources、入れ子 12 まで）、BI…ID…EI は飛ばす（SKIPPED）、
    text の表示と sh は SKIPPED。演算子は page あたり 16,777,216 まで、operand は 64 まで、clip の入れ子 64 まで。token の誤りは DAMAGED、上限は LIMITED で、
    それまでの item は残す。`/Contents` の配列は decode して改行で連結。Resources は page tree から継承（reader.c に Resources の継承を足した）。
  - `filter.c`: FlateDecode（libz-compat の inflate、壊れた・途中で切れた stream は decode できた分を使う）と PNG（10〜15）・TIFF（2、8 bit）の predictor、
    ASCIIHexDecode。DCTDecode は画像の側に渡す。他の filter は ENOTSUP。出力は 256 MiB まで。
  - `image.c`: Image XObject → RGBA。JPEG（libjpeg-compat、`jpeg_mem_src`、CMYK は Adobe の反転を考慮）、1/2/4/8/16 bit の Gray・RGB・CMYK・Cal*・ICCBased・
    Indexed、`/Decode`、`/SMask`（大きさが違えば最近傍で合わせる。SMask の SMask は読まない）、`/ImageMask`（fill の色で塗る）。1 辺 16384、1 list の画素 64 Mi まで。
  - `stroke.c`: stroker。path を平坦化（Wang の上界、許容誤差は page の 0.05 pt を CTM で user 空間へ戻したもの）し、線分の矩形・join（round は扇、miter は
    miter limit 内、それ以外は bevel）・cap（round・square・butt）・長さ 0 の subpath の点・dash（相位つき、奇数個は繰り返し、1 stroke あたり 1,048,576 entry まで）
    を同じ向きの小さな多角形にし、nonzero の一回の fill にする（重なりが二重にならない）。線幅 0 は page の 0.25 pt。
  - `display.c`: display list の builder（item・verb・point・画像の配列、上限つき。item 1,048,576、point 8,388,608）。
  - `raster.c`: display list の CPU rasterizer（`pdf_display_list_rasterize`、premultiplied の 0xAARRGGBB、page の点 p は p*scale+offset の pixel）。
    1 pixel の行を 5 本の副走査線で走査し、各副走査線の span を規則（nonzero・even-odd）どおりに求めて横は正確な被覆で足す。clip は target 全体の被覆の mask
    （入れ子 16 まで適用、超えた分は数えるだけ）。画像は双線形、縮小は 1 pixel あたり最大 4x4 の平均。Normal と Multiply を premultiplied の整数で合成。
- 依存: libpdf.so の NEEDED は libz-compat.so・libjpeg-compat.so・libc.so（4 platform の `vmunix.mk` の link 規則、package の require）。
- 性能の発見: zedBSD の libc の `qsort` は byte 単位の swap の挿入 sort（O(n²)）。rasterizer は path ごとに数百の辺を並べるので、guest で 360 本の stroke の page の
  raster が 351 ms かかった。rasterizer の中を merge sort（辺・副走査線の交点、交点 24 以下は挿入）にして 11 ms になった（guest、466x658）。

### PDF Viewer（`userland/desktop/pdfviewer`）

- 構成: Wayland・Vulkan に依らない核（`view.c` の layout と navigation と入力、`draw.c` の frame、`document.c` の page の cache、`chooser.c`、`canvas.c`、
  `text.c`）と、`window.c`（xdg-shell の toplevel、pointer・keyboard、key の repeat。files の window.c から dnd を除いて作った）、`present.c`（files の
  present.c と shader を名前だけ変えて使う。CPU で描いた frame を linear の host-visible image に書き、1 枚の quad で swapchain に写す）、`menu.c`
  （System Menu: File・View・Go）、`titlebar.c`（zdesktop の CONTROLS の titlebar）、`main.c`。
- 描画の方式（v1）: **CPU の raster を Vulkan の texture で表示**。page は libpdf で一度 display list にし、表示の scale で raster にして cache する
  （合計 192 MiB、最近使っていない page から捨てる。1 辺 8192 まで）。何もしていない間に隣の page を先に raster にする（swipe の途中で隣が見える）。
- scroll mode: page を縦に 1 列（gap 16 px）、fit width は一番広い page を窓の幅に、fit page は一番大きい page を窓に。wheel・drag・PgUp/PgDn/Space・
  上下の矢印・Home/End。page の方が広ければ左右の矢印で横へ。
- page mode: 1 page を各 page の fit で中央に。横の drag で page が指に付いて動き、幅の 18% を超えるか速さ 0.6 px/ms を超えて離すと次・前の page が
  滑って入る（220 ms、ease-out）、そうでなければ戻る。隣の page は中心どうし「両方の幅の半分と gap 2 つ」離れて並ぶ（swipe の途中で見える）。端では抵抗。
  PgUp/PgDn・左右の矢印・Home/End・wheel（page の端で 90 px ためると次・前）。page が窓より高ければ縦に drag・wheel。
- zoom: Ctrl+plus（`=` の key と keypad の +）・Ctrl+minus・Ctrl+wheel で 1.25 倍ずつ（0.1〜6）、中央の点を保つ。Ctrl+0 は mode の fit に戻す。
  menu と titlebar の Fit Width・Fit Page。
- その他の key: Ctrl+O（chooser）、Ctrl+W（document を閉じる、何も無ければ窓を閉じる）、Ctrl+Q、Ctrl+E（Annotate in Notes）。zdesktop の menu が
  shortcut を先に取る（Ctrl+O・Ctrl+W・Ctrl+0・Ctrl+E は menu 経由で届いた。Ctrl+= は menu の `+` と合わず窓の key で届いた）。
- file chooser: folder と `.pdf` の一覧（folder が先、名前順、`..` で上へ）、上下・Enter・Backspace・Esc、click。最初は開いている file の folder か $HOME。
- 「Annotate in Notes」: `posix_spawn("/bin/notes", {"notes", <絶対 path>})`。/bin/notes が無ければ「Notes is not installed on this system.」。
- 開けない file は理由つきの message（ENOTSUP は「it uses PDF features this version does not read yet」等）。読めない要素がある page は list の flags に残る
  （v1 は表示しない。log に `PAGE ... flags=`）。窓の title は「<file> - PDF Viewer」、開いた file は recent files（`keiland_recent_add`）へ。
- titlebar: 前・次の page（BACK/FORWARD）、「Page 3 of 10」、Scroll・Pages（VIEW_LIST/VIEW_COLUMNS の組）、「−」「+」、Fit Width、Fit Page、
  Annotate in Notes。zdesktop は組に入った GENERIC と PRIMARY_ACTION を icon の四角（「···」「+」）で描くので、zoom・fit・Annotate は組に入れない GENERIC にした。
- 登録: package は `desktop` の group（menuconfig の Desktop）、amd64 の link 規則（`platform/amd64/vmunix.mk`）、App Home の built-in の一覧
  （`userland/desktop/wayland/home.c`）と demo の `plan/ws035/demo/apps.conf`、Files の Open With の built-in の先頭に `application/pdf` → PDF Viewer
  （`userland/desktop/files/apps.c`）、試験の image の構成（`plan/ws035/tests/config-amd64-zdesktop.mk`・`plan/tools/files/config-amd64-files.mk` に
  libjpeg-compat・libpdf・pdfviewer）。
- log（stderr、`PDFVIEWER ...`）: READY・OPEN・PAGE（items・flags・ms）・RASTER（大きさ・ms）・SWIPE・TURN・ZOOM・MENU・TITLEBAR・CHOOSER・ANNOTATE・DONE。

### 試験（`plan/ws079/tests/`）

- `host-pdf-render.c`・`run-pdf-render.sh`: writer で Notes 形式の 3 page（筆圧の線 12 行・半透明の蛍光ペン、色の線と JPEG・半透明の RGBA、
  360 本の密な線）と、手で組んだ operator の 3 page（fill の規則・色空間・曲線・ca/BM Multiply・text、stroke の cap/join/dash/hairline/半透明の自己交差、
  clip・SMask の画像・stencil・Form・Flate+PNG predictor の画像・Rotate 90 と CropBox。page 1 の content は Flate の stored block）を作り、libpdf と
  pdftoppm（-cropbox）で 72・150 dpi に描いて比べる（許容: 平均差 2.5 以下かつ差 64 超の pixel が 0.6% 以下）。qpdf で Flate に圧縮した copy が同じ pixel に
  なること、plain・ASan・UBSan が同じ pixel になること、破壊の loop（operator の content の変異と file の byte の変異、各 N 回）。
- `host-pdfviewer.c`・`run-pdfviewer-host.sh`: viewer の核を host で動かす（plain と ASan+UBSan）。scroll・wheel・drag・End/Home・zoom・Fit Page・
  page mode・swipe（長い・短い）・PgDn・左右・Ctrl+0・mode の切り替え・Ctrl+E・Ctrl+O/Esc・Ctrl+W の 25 項目と、各段の frame の PPM/PNG。
- `pdfviewer-guest.sh`: Venus の guest の手順（install・home・scroll・page・zoom・chooser・annotate・files・files-open・ops・real）。program の log の行
  （PDFVIEWER・ZFILES・ZWL・NOTES）を SSH で読み、画面は VNC（`zdesktop-check.py`）。console・serial の log は読まない。
- `build-pdfviewer-image.sh`: 試験の image を作る手順（今回は使っていない。下の「guest の image」）。

## 確認（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| libpdf と pdftoppm | `sh plan/ws079/tests/run-pdf-render.sh 3000`（gcc 14.2.0、`-std=c89 -pedantic -Wall -Wextra -Werror`、poppler 25.03.0、qpdf 12.2.0） | exit 0。12 の比較が全て ok（Notes 形式: 平均差 0.20〜2.37、差 64 超 0.0006〜0.11%。operator: 平均差 0.23〜1.13、0.004〜0.39%）。plain・ASan・UBSan の画素が `cmp` で一致。qpdf の Flate の copy が同じ画素。qpdf `--check` は 2 file とも誤り無し |
| 破壊の loop | 同上（plain 3000 回、ASan・UBSan は各 1000 回。content の変異と file の変異） | 落ちず、ASan・UBSan・LeakSanitizer の報告なし（plain 82 s、ASan 60 s、UBSan 42 s） |
| 本物の Notes の PDF | guest の Notes で線 2 本と Marker 1 本を描いて Ctrl+S（`/root/Documents/Notes/note-...pdf`、13,972 byte、ca 0.4）→ host で libpdf と pdftoppm を 100 dpi で比較 | 平均差 0.048、差 64 超 0 pixel。qpdf `--check` 誤り無し |
| p004 の回帰 | `sh plan/ws079/tests/run-pdf-reader.sh`、`sh plan/ws079/tests/run-pdf-writer.sh` | 2 つとも ok（reader.c に Resources の継承と内部の accessor を足した後） |
| viewer の核（host） | `sh plan/ws079/tests/run-pdfviewer-host.sh` | plain・ASan+UBSan とも 25 項目 ok |
| amd64 の build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p006-amd64 …/bin/pdfviewer …/bin/notes …/bin/files …/bin/wayland …/dynamic/libpdf.so` | exit 0、warning 0。pdfviewer は NEEDED libvulkan・libwayland-client・libkeiland・libtruetype・libpdf・libc（check-dynamic-elf 通過） |
| 4 platform の libpdf.so | `config/ci/config-{pcat,pc98,rpi4}.mk`、BUILD `build/ws079-p006-{pcat,pc98,rpi4}` | 3 つとも exit 0、warning 0（rpi4 は新しい build dir の初回に sysroot の順序で `errno.h` が無く失敗、変更なしの再実行で通った。p004 と同じ既知の順序の問題）。4 platform とも `pdf_` の動的 symbol 34、NEEDED libz-compat・libjpeg-compat・libc |
| Venus の guest（QEMU） | main の `build/ws035-sq/hdd-image.img`（14:22 の zdesktop image）の copy を KVM・Venus で起動し、この worktree の pdfviewer・libpdf.so・files・wayland（と merge 後の notes）を SSH で入れ替え、`plan/ws079/tests/pdfviewer-guest.sh` | 下の各段が ok。zdesktop の log に ERROR 0 |

guest の各段（QEMU、zdesktop --glass 1280x800、画面は `build/ws035-shots/ws079-p006-20260928-*.png`）:

- home: App Home に PDF Viewer（`home.png`。1 回目は compositor の起動直後の click で開かず、単独の再試行で `ZWL HOME opened`。script の待ちを 7 秒にした）。
- scroll: notes.pdf（3 page）を開く（`PDFVIEWER OPEN path=/tmp/notes.pdf pages=3`）、wheel 6 notch、End（`scroll-1/2/end.png`）。
- page: `--mode=page`、右から左への drag を押したまま撮る（`page-swipe-middle.png`: 1 page 目が左へ動き 2 page 目が右から見える）、離して 2 page 目
  （`SWIPE ... direction=1`、`PAGE shown=1`）、PgDn で 3 page 目、左から右への swipe で戻る（`direction=-1`）。
- zoom: Ctrl+= 2 回（`ZOOM scale=0.977`→`1.221`）、Ctrl+0（`zoom.png`・`zoom-reset.png`）。
- chooser: Ctrl+O（`CHOOSER open folder=/tmp entries=4`）、Esc（`chooser.png`）。
- annotate: Notes が無い image では message（1 回目）。merge 後の Notes を入れて Ctrl+E → `ANNOTATE program=/bin/notes path=/tmp/notes.pdf pid=808`
  （Notes は起動したが、Notes v1 は自分の編集 data の無い PDF を開かず新しい notebook になる。`annotate.png`）。
- real: Notes 自身が保存した PDF を PDF Viewer で開き（`real.png`）、Ctrl+E → Notes が `NOTES START ... strokes=3 path=/tmp/real-notes.pdf` で編集を続けられる
  （`real-annotate.png`）。
- files: Files で notes.pdf を double click → `ZFILES OPEN path=/tmp/pvdir/notes.pdf app=PDF Viewer error=0`、pdfviewer の process と窓（`files-open.png`）。
- ops: operator の 3 page を page mode で（`ops-1/2/3.png`）。
- 時間（guest、KVM、466x658）: 解釈 7〜56 ms、raster 3〜11 ms（merge sort の後）。

host の比較の画像（左 libpdf、右 poppler）: `ws079-p006-20260928-host-{ops1,ops2,ops3,notes2,real-notes}-libpdf-vs-poppler.png`。viewer の核の frame は
`build/ws079-p006-host/viewer-plain/*.png`（この worktree の build）。

## guest の image

新しい worktree で disk image の全体を作ると、NoctLang の取得（network）・noct の cmake・openssl・openssh の build が要る（`make -n` で確認）。共有の
`build/packages` は rebuild の時に `rm -rf` するので symlink にできない。そのため main の最新の zdesktop image の copy を起動し、変えた binary だけを SSH で
入れ替えた。image そのものに pdfviewer が入る確認（`build-pdfviewer-image.sh`）は未実施。LLVM は symlink（`build/llvm`・`build/llvm-source`・
`toolchain/llvm/distfiles`）で読むだけにし、worktree の checkout で新しくなった LLVM の patch の mtime を main の file に合わせて、`build/llvm-source` の
作り直しを避けた（dry run で確認。共有の `build/llvm` には何も install していない）。

## 未実施と制限

- 実機（i915・pen tablet）: 未実施。
- disk image に pdfviewer を入れた build: 未実施（上）。
- CMYK: 素朴な変換（1−c)(1−k)。poppler は Adobe 風の変換で、純粋な K が濃い灰になる（比較の文書からは外した）。
- 読めない要素（text・shading・inline image・pattern・soft mask の ExtGState・他の blend mode）は描かず list の flags に残る。v1 の viewer はそれを画面に
  出さない（段階 ② で「この page の一部は表示していません」を出す）。
- stroke の dash は user 空間で切る。極端に細かい pattern は 1 stroke 1,048,576 entry で止まる（LIMITED）。
- 大きな page の zoom は raster の 1 辺 8192 px が上限（それより大きい zoom は粗くなる）。tile に分けていない。
- reader の xref の並べ替え（`reader.c` の `sort_entries`、p004）と chooser の一覧は libc の `qsort`（O(n²)）のまま。object が数万ある一般の PDF では遅い。
- 規約: 新しい code に `plan/coding-style.md` の機械的な項目（条件の中の呼び出し、`error == 0 &&` の連鎖、式で作る Boolean、条件演算子）を当てた。
  「全ての if の前の目的の comment」などの全文の見直しは p009。`present.c`・`window.c`・shader は files の実装の写し（元の書き方のまま）。

## main への連絡

1. libpdf の公開 API の追加だけ（既存の関数・struct の意味は不変）: 上の 3 関数と型。`reader.c` は内部に accessor（`pdf_reader_resolve*`・`pdf_reader_page`・
   `pdf_reader_bytes`）と page tree の Resources の継承を足した。
2. libpdf.so が libz-compat.so と libjpeg-compat.so を NEEDED に持つ（4 platform の link 規則と package の require を変えた）。libpdf を使う program の image には
   その 2 つも要る。
3. **libc の `qsort` が O(n²) の挿入 sort**（`src/libc/stdlib-extra.c`）。libpdf の rasterizer は自前の merge sort にしたが、他の利用者（reader の xref、
   chooser、glob など）にも効く。libc の側の直しは別の WS/Phase（この Phase の範囲外）。
4. Notes: `pdf_page_render()` と `pdf_display_list_rasterize()` で他の PDF の page を背景に描ける（design-pdf.md §3。Notes v1 は今それを拒んで新しい notebook に
   なる）。
5. zdesktop の titlebar: 組に入った GENERIC と PRIMARY_ACTION が label ではなく icon（「···」「+」）で描かれる。PDF Viewer は組を使わない GENERIC で避けた。
6. menu の shortcut の Ctrl+`+`（keysym 0x2b）は US 配列の Ctrl+`=` の key と合わない（terminal も同じ）。PDF Viewer は窓の key で Ctrl+= も受ける。

## 残りと移管

- 段階 ②（p007）: xref stream・object stream・壊れた xref の修復、text と font、shading、inline image、読めない要素の注意の表示、page の thumbnail。
- 性能: 解釈の cache（page の list は保つが画像の decode は page ごと）、raster の tile 化、GPU での path の塗り（design-pdf.md §4.1 の Vulkan の方式）。
- libc の `qsort`（上の 3）。

## 再開

v1 の範囲は済んだ。回帰は `sh plan/ws079/tests/run-pdf-render.sh`（fuzz の回数は引数）と `sh plan/ws079/tests/run-pdfviewer-host.sh`。guest は main の
zdesktop image の copy を `plan/ws035/tests/zdesktop-guest.sh` と同じ引数で起動し（`GUEST_RUNTIME` は自分の dir）、
`plan/ws079/tests/pdfviewer-guest.sh OUTDIR PREFIX install home scroll page zoom chooser annotate ops`。
