<!-- awesome-plan project=zedbsd record=ws079-p005 -->

# ws079-p005: Notes v1（筆圧の線・蛍光ペン・消しゴム・page・undo・PDF の保存と再編集・自動保存と journal）

<!-- awesome-plan-current:start -->
Status: cleared（v1 の一通り。2026-09-28、Kei desktop subagent。QEMU の Venus guest と host の証拠だけ。実機（i915・USB の pen）は未実施）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（Kei desktop subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: 下の「残り」（edge case と後の Phase の候補）
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

`userland/desktop/notes`（`/bin/notes`、画面の名前「Notes」、app_id `notes`）の v1 を一通り通す:
fullscreen にできる窓（`--fullscreen`）、A4 の page と page の移動・追加、pointer（後で tablet）からの stroke、
`pdf_outline_stroke()` の輪郭を Vulkan で anti-alias して描く、ペン（色・太さ）・蛍光ペン・消しゴム（stroke 単位）・undo・redo、
libpdf の writer で保存（ベクタの輪郭と ZNOT の編集 data）、一定時間の操作の無いときの自動保存、crash からの journal での復元、
自分の PDF を開く、標準の shortcut、Desktop の menu group と App Home への登録。途中で main から p004（reader・平滑化した輪郭）と
p003（tablet の protocol）の merge が来たので、開く経路と pen の入力もつないだ。

設計: [design-input-notes.md](../design-input-notes.md) §5、[design-pdf.md](../design-pdf.md) §1〜§3。ユーザーの回答（ws.md）:
保存 button ＋自動保存＋journal での復元（D6）、標準の shortcut（D8）。

## 決めたこと（設計の既定から変えた点・選んだ点）

| 点 | 決定 | 理由 |
| --- | --- | --- |
| 自動保存（D6 の回答） | 最後の変更から **5 秒**（`NOTES_AUTOSAVE_IDLE_MS`）操作が無く、stroke を描いている最中でないときに保存。明示の保存（Ctrl+S・toolbar・menu）と、閉じるときの保存も行う | 手書きの区切り（書いて手を止める）で保存され、描画の途中の遅延を避ける |
| 新しいノートの置き場 | `~/Documents/Notes/note-YYYYMMDD-HHMMSS.pdf`（初回の保存で folder を作る）。file を指定したときはその path | 自動保存には常に path が要る。file の選択の dialog がまだ無い |
| journal（D6 の回答） | `$XDG_DATA_HOME/keiland/notes/`（無ければ `~/.local/share/keiland/notes/`）に、文書の絶対 path の FNV-1a 64 bit の名前 `<16 桁>.journal`。保存の後の最初の変更で「header（文書の path）＋文書全体の snapshot（ZNOT）＋その変更」を一時名に書いて rename、以後の変更は append ＋ fsync。保存で削除。record は type・長さ・本体・FNV-1a の checksum で、復元は checksum の合う record まで | snapshot を持つので PDF の reader に依らず復元できる（p004 の reader の前に書いた）。途中で切れた最後の record は捨てる |
| 復元の起動 | `notes FILE` はその file の journal があれば復元（file より新しい）。`notes`（file なし）は最新の journal を復元（gesture の `/bin/notes --fullscreen` もこれ）。復元した文書は変更ありとして 5 秒後に自動保存される | kill・crash の後、次に開いたときに戻る |
| Ctrl+N | **新しい page**（今の page の後ろに足して表示）。File の menu の「New Page」 | 1 process 1 文書（design §5.4）。notebook では page を足すのが頻繁な操作。新しいノートは App Home・gesture・`notes` で起動 |
| Ctrl+O | 「Opening from Notes is not available yet」を出す（file の選択の dialog が無い）。`notes FILE.pdf` と PDF Viewer（p006）の「書き込む」は開ける | 残り |
| その他の key | Ctrl+S 保存、Ctrl+Z undo、Ctrl+Shift+Z と Ctrl+Y redo、Ctrl+W 閉じる（保存してから）、Page Up/Down で page、F11 全画面の切り替え、Esc 全画面を解く、P ペン・M 蛍光ペン・E 消しゴム | 標準（D8）。System Menu の shortcut は compositor が実行し、menu に無い key と menu の無いときは Notes が処理 |
| 蛍光ペン | 半透明（alpha 0x66）の 5 色（黄・青・桃・緑・橙）、太さ 10・16・24 pt、筆圧に依らない一定の幅（`pdf_outline_stroke()` に pressure 1 を渡す）。PDF は `ca` の ExtGState | design §6 は範囲外としていたが main の指示で v1 に入れた |
| ペンの色と太さ | 黒・青・赤・緑・橙、太さ 1.5・3・6 pt（既定 3）。太さの曲線は `pdf_outline_stroke()` の `0.15 + 0.85 p^0.7` | libpdf の輪郭が唯一の形の元（ws.md の main の判断） |
| 消しゴム | stroke 単位（円の半径 10 pt が stroke の点列に stroke の幅の半分まで近づけば消す）。1 回のドラッグで消したものは 1 つの undo | 部分の消しゴムは残り |
| pointer の筆圧 | 0.5（`NOTES_POINTER_PRESSURE`） | main の指示 |
| pen（p003 の merge 後） | `zwp_tablet_manager_v2` を bind し tablet seat の tool を聞く。frame ごとに DOWN・MOTION・UP の入力にし、筆圧 0..65535 を 0..1、傾き（度）を 1/100 度で点に保つ。eraser の tool（RUBBER）と、pen の第 1 の barrel button（BTN_STYLUS）を押している間は消しゴム（D5）。tablet を bind したので compositor は pen を tablet として送る。manager の無い compositor では pen は pointer として来る | p003 の protocol に合わせた |
| 描画 | Vulkan の stencil で輪郭を nonzero の規則で塗る（fan で winding を数える → 外側 1 px の fringe を距離で alpha を落として描く → bounding box で内側を塗って stencil を 0 に戻す）。MSAA を使わない anti-alias。半透明の stroke も 1 回だけ塗られ自己交差で濃くならない | design §5.3 の「MSAA に頼らない」を、輪郭の多角形（p004 の平滑化で内側の辺が角の点を通る）に合わせて満たす |
| toolbar | CPU で描いた BGRA の帯（高さ 52 px、libtruetype の `keiland.ttf`）を linear の VkImage で重ねる。glass は使わない | v1 の簡単な形。fullscreen の Keiland の glass は後 |
| 開く（p004 の merge 後） | `pdf_document_open` → `pdf_document_find_attachment_type(zedbsd-notes.bin, application/x-zedbsd-notes)` → ZNOT を decode。各 page の content の SHA-256（保存時に `pdf_writer_get_page_content_hash()` で ZNOT の PAGE に記録）を `pdf_document_page_content_hash()` と比べ、違えば ESTALE で拒む。編集 data の無い PDF は ENOENT。開けない PDF は上書きせず新しいノートを始める | design-pdf §3。他の PDF を背景にする形は p006 以降 |

## 実装（commit は `WIP`、この worktree）

| file | 内容 |
| --- | --- |
| `userland/desktop/notes/notes.h` | 文書 model（point・stroke・page・undo・document・入力の event・byte buffer）と、model・ZNOT・journal・PDF の API（window system に依らない。host の試験が build する） |
| `userland/desktop/notes/document.c` | page と stroke、4 つの基本の変更（stroke の挿入・番号で除去、page の挿入・除去）を journal に記録してから適用、利用者の操作（stroke の追加・消しゴムのドラッグ・page の追加）を undo の entry（最大 1000、memory だけ）に。輪郭は `pdf_outline_stroke()` の結果を cache。座標・幅・page の大きさは ZNOT と同じ 1/64 pt の格子に丸める（保存・読み戻しで同じ値） |
| `userland/desktop/notes/encode.c` | ZNOT 版 1.0 の encode・decode（DOC・TOOL・PAGE の chunk、差分の LEB128・zigzag、PAGE の content hash、知らない chunk は飛ばす、壊れた data の上限と検査）と、journal 用の stroke 単体の符号化 |
| `userland/desktop/notes/journal.c` | 上の journal |
| `userland/desktop/notes/save.c` | libpdf の writer での保存（輪郭の塗り・色と ca・page の content hash・ZNOT の添付・`/ID` と作成日の維持・一時名 → fsync → rename → folder の fsync）と、reader での開く |
| `userland/desktop/notes/geometry.c` | frame の頂点と draw の列（矩形・stencil の 3 段の多角形・toolbar の texture）、page の配置 |
| `userland/desktop/notes/render.c`・`shaders/`・`shaders.h` | Vulkan（Wayland WSI、stencil の形式の選択 D24S8→D32S8→S8→D16S8、5 つの pipeline、頂点 buffer の伸長、toolbar の linear image）。shader は `shaders/regenerate.py`（glslc・spirv-val）で `shaders.h` を作る。`gl_VertexIndex` は使わない（i915） |
| `userland/desktop/notes/window.c` | xdg toplevel（app_id `notes`、`--fullscreen` は最初の commit の前に set_fullscreen、xdg の bounds に合わせた窓の大きさ）、pointer と keyboard、入力の queue（`notes_window_input()` は tablet と共通） |
| `userland/desktop/notes/tablet.c` | 上の pen |
| `userland/desktop/notes/menu.c` | System Menu（File・Edit・Page・Tool・View、18 item、shortcut と role） |
| `userland/desktop/notes/ui.c` | toolbar（Pen・Marker・Eraser、色 5、太さ 3、Undo・Redo、< n / m >、+ Page、Save、右端の status） |
| `userland/desktop/notes/main.c` | 起動（journal の復元 → PDF を開く → 新しいノート）、main loop、自動保存、shortcut、入力、試験用の `NOTES ...` の log 行、`--timeout-s` |
| `userland/desktop/notes/Makefile` | package `notes`（Desktop の menu group、amd64、`desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype base/libpdf`） |
| `platform/amd64/vmunix.mk` | `/bin/notes` の link の規則（libvulkan・libwayland-client・libkeiland・libtruetype・libpdf・libc）と、汎用の規則からの除外 |
| `userland/desktop/wayland/home.c`・`plan/ws035/demo/apps.conf` | App Home の組み込みの一覧と demo の一覧に「Notes」（`/bin/notes`、色 e0a526） |

画面と PDF の文字列に Keiland の名前は出さない（窓の題は「Notes — <file 名>」）。code の接頭辞に ZEDBSD_ は使っていない。

## 試験（2026-09-28、この worktree）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| host の model・ZNOT・journal・PDF | `sh plan/ws079/tests/run-notes-host.sh`（gcc 14.2.0、`-std=c99 -pedantic -Wall -Wextra -Werror`、plain・ASan・UBSan、libpdf の writer・reader・outline と libc の SHA-256 を host で build） | 3 変種とも `host-notes: ok`。undo・redo（stroke・消しゴムのドラッグ 1 回で 2 本・page）、ZNOT の往復の一致、別 major・途中で切れた data の拒否、「crash」した journal（snapshot＋17 record）の復元と途中で切れた record の除外、最新の journal、保存 → journal の削除 → 次の変更で snapshot から始まる journal、保存した PDF を開いて同じ文書・同じ `/ID`、page の content を 1 文字変えた PDF の拒否（ESTALE）、編集 data の無い PDF の拒否（ENOENT）。`qpdf --check` エラーなし、添付 `zedbsd-notes.bin`、`pdftoppm` の描画を目視 |
| amd64 の build | `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-notes.mk BUILD=build/amd64 build/amd64/bin/notes` と image（`plan/ws079/tests/build-notes-image.sh build/amd64`） | exit 0、Notes・libpdf・compositor の warning 0（openssl の外部 package の warning だけ）。LLVM の再 build は起きなかった（lean の構成、clang・lldb なし） |
| 規約の機械的な確認 | `python3 plan/tools/style-check.py userland/desktop/notes/*.c userland/desktop/notes/*.h` | total 0（全文の読み直しは p009） |
| QEMU（Venus）: 主な経路 | `plan/ws079/tests/notes-p005.sh`（1280x800、`--fullscreen /tmp/notes-test/test.pdf`、QMP の pointer と key） | PASS: pointer のドラッグで 2 本と蛍光ペン、Ctrl+Z・Ctrl+Y、消しゴム（E）で 1 本消えて Ctrl+Z で戻る、Ctrl+N で page 2、Page Up、Ctrl+S（pages=2 strokes=4）、host の qpdf・pdfinfo・添付・pdftoppm、1 本足して 7 秒で自動保存（strokes=5）、1 本足して直ちに SIGKILL → journal 871 byte → 再起動で `NOTES RECOVER records=2 pages=2 strokes=6`、復元後の自動保存と Ctrl+W、journal は 0 個、同じ file を開き直して `NOTES OPEN pages=2 strokes=6` → 1 本足して保存（strokes=7）。zdesktop の ERROR なし |
| QEMU: pen（tablet） | `plan/ws079/tests/notes-pen.sh`（image は `CONFIG_INPUT_TEST_INJECT=y` と peninject 入り） | PASS: `NOTES TABLET seat`、tool 0x140（pressure・tilt あり）、筆圧 0→4095→0 の傾けた stroke（`pressure=0..65535 tilt=1`、端が細く中央が太い）、一定の軽い筆圧の stroke（最大 9602 = 600/4095）、eraser の tool（0x141）で 1 本消え Ctrl+Z で戻る、保存と qpdf |
| QEMU: App Home と窓 | `plan/ws079/tests/notes-home.sh` | PASS: Home に Notes の icon、click で起動（`ZWL HOME launch name=Notes`）、窓の題「Notes — note-….pdf」と System Menu（items=18）、F10 で File の menu（New Page Ctrl+N・Open... Ctrl+O・Save Ctrl+S・Close Ctrl+W）、Ctrl+W で `~/Documents/Notes/note-….pdf` に保存 |
| QEMU: p010 のスワイプと本物の Notes | `plan/ws079/tests/notes-gesture.sh` | PASS: 右上からのスワイプで `/bin/notes --fullscreen` が起動（`NOTES START … fullscreen=1`）し描ける、窓の Notes はスワイプで全画面（`NOTES LAYOUT window=1280x800`）になり 2 つ目は起きない |

画面（QEMU の Venus、`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `ws079-p005-20260928-{start,strokes,undo,erase,page2,saved,recovered,reopened}.png`、
PDF の描画 `…-pdf-page1.png`・`…-pdf-page2.png`・`…-final-pdf-page1.png`、anti-alias の拡大 `…-antialias-zoom.png`、
pen `…-pen-{pressure,eraser,pdf}.png`、App Home `…-home-{home,window,menu}.png`、スワイプ `…-gesture-{swipe,raised}.png`。

未実施: 実機（i915 の HDMI、USB の pen・touch。機種は届いてから）、Venus 以外の GPU（i915 の stencil の経路は未確認）、
長い使用（page 数・stroke 数が多いときの描画の速さ）、`boot-test.sh`（2026-09-27 の指示でデスクトップの Phase は描画の試験を直接行う）。

## 残り（後の補強・別 Phase の候補）

1. Ctrl+O の file の選択（dialog が無い）。PDF Viewer（p006）の「書き込む」からの起動はできる（`notes FILE`）。
2. 他の program の PDF・他の program が変えた page を背景にして書き込む（design-pdf §3。p006 の display list の後）。今は拒んで新しいノートにする。
3. 部分の消しゴム（design §5.1）、罫線・方眼の背景、page の削除と並べ替え、拡大、pen の近接中の cursor（`set_cursor`）。
4. 描画の速さ: 今は毎 frame に page の全 stroke の頂点を作り直して描く。確定した stroke を page の画像に焼く（design §5.3）のは後。
5. 名前: libpdf の `/Producer (zedBSD Notes)` と添付の名前 `zedbsd-notes.bin`・`application/x-zedbsd-notes`（design-pdf §2）は
   WS078 の名前の移行（Kei）の対象になりうる。PDF の中で見える文字列。変えるなら p004・p006 と合わせて main が決める（Notes は `NOTES_ATTACHMENT_*` の 2 つの定数だけ）。
6. `plan/coding-style.md` の全文の読み直し（p009）。
