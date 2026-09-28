<!-- awesome-plan project=zedbsd record=ws079-p015 -->

# ws079-p015: p007・p008 の残り（CCITTFax の画像、password の入力、thumbnail）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、PDF/Notes subagent。host と QEMU の Venus guest の証拠と 4 platform の build。実機は未実施。clearance は main の判断で覆してよい）
Disposition: normal
Parent: [WS079](../ws.md)
Queue: main の指示（PDF/Notes subagent、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（残りは下の「未実施と制限」）
<!-- awesome-plan-current:end -->

## 範囲（main の指示、2026-09-28）

p007・p008 の「残り」の 3 つ:

1. CCITTFax（Group 3・Group 4）の画像の decode（libpdf の filter）。
2. 暗号化された PDF の password の入力（PDF Viewer の dialog、libpdf の `PDF_EPASSWORD` を使う）。
3. PDF Viewer の thumbnail（page の一覧）。

規則: libpdf の公開 API は追加だけ。修正してよいのは WS079 の source（libpdf・`include/libc/pdf.h`・pdfviewer・notes）と
`plan/ws079/`。libkeiland・compositor・toolchain・HAL・kernel は変えない。試験の PDF は自分で生成し、git に入れない。

## 受け入れ条件（実装の前に書いた）

### A. CCITTFax

- A1. `CCITTFaxDecode`（inline image の略名 `CCF` も）を decode する: `/K` < 0（Group 4）・= 0（Group 3 の 1 次元）・> 0（Group 3 の 1 次元と
  2 次元の混在）、`/Columns`・`/Rows`・`/EncodedByteAlign`・`/EndOfLine`・`/EndOfBlock`・`/BlackIs1`・`/DamagedRowsBeforeError`。
  出力は 1 bit の行（既定で 0 が黒）。壊れた行はそこで止め、それまでの行を残す（`/Rows` があれば残りは白で埋める）。
- A2. host: ghostscript の `CCITTFaxEncode`（独立の実装）で符号化した画像（上の parameter の組）を libpdf が **元の bitmap と bit 単位で同じ**に
  戻す。pdftoppm と許容差（画像）で合う。
- A3. /usr/share/doc の実文書の SKIPPED の page（p008 の後 3: gnus-logo と txirefcard・txirefcard-a4 の各 1 page、どれも CCITTFax）が 0 になる。
  gnus-logo は pdftoppm と比べる。
- A4. CCITT の試験の PDF の破壊の loop（ASan・UBSan）で落ちず、sanitizer の報告なし。

### B. password

- B1. libpdf の API の追加: `pdf_document_open_password(path, password, &document)`・`pdf_document_open_memory_password(data, size, password, &document)`。
  標準 security handler の R2〜R6（RC4 40・128、AES-128、AES-256 の R5・R6）で **user password と owner password の両方**を受ける。違う password は
  `PDF_EPASSWORD`。既存の関数の振る舞いは不変（空の password）。
- B2. host: qpdf で user password と owner password を付けた各 revision の copy を、どちらの password でも開け、平文の文書と同じ画素。違う password は拒む。
- B3. PDF Viewer: user password の要る文書を開くと password の dialog（入力は伏せ字）。違う password は dialog の中で誤りを示して入力を消し、
  正しい password で開く。Esc・Cancel で閉じる。password を log に出さない。host の試験（frame の画像）と QEMU の guest の screenshot。

### C. thumbnail

- C1. PDF Viewer に page の thumbnail の一覧（左の sidebar）: View の「Page Thumbnails」（checkbox）、titlebar の sidebar の control、F9 で出し入れ。
  thumbnail は暇なときに 1 つずつ描き（描けるまでは白の枠と番号）、今の page を強調し、click でその page へ、wheel と drag で一覧を scroll、
  page が変わると一覧が追う。memory は上限つき（見えている範囲の周りだけ保つ）。
- C2. host の試験（frame の画像、click で page が変わる）と QEMU の guest の screenshot。

### D. 共通

- D1. build: amd64 の pdfviewer・notes・libpdf.so・libtruetype.so が warning 0。libpdf を変えるので pcat・pc98・rpi4 の libpdf.so も warning 0。
- D2. 規約: 新しい code と変えた関数は `plan/tools/style-check.py` の違反 0、coding-style の全文に沿う。
- D3. 回帰: `run-pdf-text.sh`（破壊の回数は減らしてよい）・`run-pdf-render.sh`・`run-pdf-reader.sh`・`run-pdfviewer-host.sh`・`run-notes-host.sh`・
  `run-pdf-update.sh`・`run-pdf-writer.sh`。

## 決めたこと

| 点 | 決めたこと | 理由 |
| --- | --- | --- |
| CCITT の decoder の置き場 | 新しい `userland/base/libpdf/ccitt.c`（内部の `pdf_ccitt_decode()` と `struct pdf_ccitt_parameters`、`internal.h`）。filter.c は parameter を読んで呼ぶだけ | filter.c は 1,300 行あり、CCITT は表と 2 次元の mode で 1,000 行を超える。単独で host の試験（bit 単位の比較）ができる |
| 符号の表 | T.4 の表を「0 と 1 の文字列と run」の形でそのまま書き、decode のたびに 13 bit（run）と 7 bit（mode）の索引の表に展開 | 表が仕様書と一字ずつ照合でき、索引は peek 1 回で引ける |
| 行と行の間 | poppler・pdf.js と同じ慣習: 12 bit が全部 0 のあいだ fill を飛ばす（`EndOfLine` なら次の EOL まで）、EOL を読む、EOL が無くて `EncodedByteAlign` なら byte に揃える、K > 0 なら tag を 1 bit、byte 揃えで EOL の無い block は 2 つの EOL で終わる、EOL の直後の EOL で block の終わり。先頭の EOL は「EOL がある」合図 | 実際の PDF を作る側（libtiff・ghostscript・scanner）の揺れを吸収する実績のある読み方 |
| K > 0 で EOL の無い data | tag を読む（poppler・pdf.js と同じ）。ghostscript の `CCITTFaxEncode` はこの組で tag を書かない（K 行ごとに 1 次元）が、試験から外した | T.4 の 2 次元の符号化は EOL と tag を伴う。実際の混在の data は EOL つき |
| 壊れた行 | `EndOfLine` が無ければそこで止める。有れば `DamagedRowsBeforeError` の数まで、その行を途中まで出して次の EOL から続ける。`/Rows` が無ければ画像の `/Height`（inline image の `/H`）を行の数にし、足りない行は白 | image.c は足りない標本を 0（黒）で読むので、白で埋めないと途中で切れた scan が黒くなる |
| password の API | `pdf_document_open_password()`・`pdf_document_open_memory_password()` を追加（`pdf_document_open()`・`_memory()` は NULL で呼ぶ形に）。password は開く間だけ document が指し、開いた後は持たない（鍵だけを security handler が持つ） | 公開 API は追加だけ。password を memory に残さない |
| owner password | R2〜R4: user として試し、だめなら algorithm 7（owner password の MD5 で /O を RC4 で 1 回か 20 回解く）で user password を得てもう一度。R5・R6: /U と /UE、だめなら /O と /OE（hash に /U の 48 byte を user data として混ぜる）。R6 の hash（ISO 32000-2 algorithm 2.B）に password と user data を入れた。R5・R6 の password は 127 byte まで | PDF 1.7 と ISO 32000-2 の手順 |
| password の card | 文書が無い状態で窓の中央に card（錠の印、「This document is protected」、file 名、伏せ字の field と caret、誤りの赤い文、Cancel と Open（既定、青））。card は全ての key と click を取る。文字は US 配列の表（Files と同じ、zdesktop は keymap を送らない）。password は log に出さず、試すたび・閉じるたびに buffer を 0 で消す | Keiland の他の card（chooser）と同じ描き方 |
| thumbnail の sidebar | 窓の左 168 px（窓が 408 px より狭いと出さない）。page の区画は 186 px の同じ高さ（112x146 の箱に page の形を合わせ、下に番号）。frame はまだ描けていない thumbnail を白で描き、`pv_app_prefetch()` が見えている区画とその先 2 つを 1 つずつ描く。一覧は今の page が変わるたびにその区画を見せる（それ以外は利用者の scroll を保つ）。thumbnail のために解釈した display list は、その page が表示されていなければ捨てる。thumbnail は合計 32 MiB を超えると見えている範囲の ±16 区画の外を捨てる | 76 page の文書でも最初の frame が止まらない。長い文書で全 page の list を持たない |
| 出し入れ | View の「Page Thumbnails」（checkbox）、titlebar の `KEILAND_CONTROL_SIDEBAR` の control（左端）、F9（Evince の side pane と同じ key）。page の側は sidebar の右の canvas（frame の pixel の上の部分 canvas）に描くので、既存の配置の code は変えずに済む | libkeiland の共通部分は変えない（SIDEBAR の control の種類は既にある） |

## 実装

| file | 内容 |
| --- | --- |
| `userland/base/libpdf/ccitt.c`（新） | CCITT fax の decoder（上の表）。T.4 の表 2・3・4、1 次元の行、2 次元の行（pass・horizontal・vertical、b1・b2 の探索）、行の間、壊れた行、出力の行の確保（decode の上限 256 MiB） |
| `userland/base/libpdf/filter.c` | `CCITTFaxDecode`・`CCF` の種類、`read_ccitt()`（parameter と `/Height` の代わり）・`read_flag()`、`apply_filter()` が stream を受ける |
| `userland/base/libpdf/internal.h` | `struct pdf_ccitt_parameters`・`pdf_ccitt_decode()`、`pdf_crypt_open()` に password |
| `userland/base/libpdf/crypt.c` | user・owner password（上の表）: `read_legacy_key()`・`read_aes256_key()`・`pad_password()`・`legacy_owner_to_user()`・`aes256_key()`・`aes256_hash()`、`revision6_hash()` に password と user data。鍵と中間の値を使った後に 0 で消す |
| `userland/base/libpdf/reader.c` | 2 つの公開の関数、`open_owned()` と document が開く間の password |
| `include/libc/pdf.h`・`exports.map` | 2 つの宣言と export（追加だけ）、`PDF_EPASSWORD` の注釈 |
| `userland/base/libpdf/Makefile` | ccitt.c |
| `userland/desktop/pdfviewer/viewer.h` | F9・Tab・KP Enter、`PV_ACTION_THUMBNAILS`、page の thumbnail、app の sidebar と card の状態、配置の定数、新しい関数 |
| `userland/desktop/pdfviewer/document.c` | `pv_document_open()` に password、`pv_document_thumbnail()`・`pv_document_trim_thumbnails()`、解釈を `interpret()` にまとめた |
| `userland/desktop/pdfviewer/view.c` | `open_document()`（card を出す）、sidebar の入力（`sidebar_takes()`・`sidebar_event()`・`thumbnail_at()`）と配置（`pv_app_sidebar_width()`・`pv_thumbnail_range()`・`pv_thumbnail_place()`・`relayout()`・`clamp_sidebar()`・`reveal_thumbnail()`）、card（`pv_password_layout()`・`password_event()`・`password_key()`・`submit_password()`・`cancel_password()`・`forget_password()`・`key_character()`）、tick で一覧が page を追う、prefetch が thumbnail を先に |
| `userland/desktop/pdfviewer/draw.c` | sidebar（`draw_sidebar()`・`draw_thumbnail()`）、page の部分 canvas、card（`draw_password()`・`draw_button()`） |
| `userland/desktop/pdfviewer/menu.c`・`titlebar.c`・`window.h`・`main.c` | View の「Page Thumbnails」、titlebar の sidebar の control、`struct pv_state` の thumbnails |

## 確認（2026-09-29、この worktree。host は gcc 14.2.0・poppler 25.03.0・qpdf 12.2.0・ghostscript 10.05.1）

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| CCITT の bit 単位の比較 | `sh plan/ws079/tests/run-pdf-ccitt.sh 100`（新）: `make-ccitt-data.py` が 4 つの bitmap（全ての run の符号と長い make-up、noise と block（全ての vertical の offset と pass）、1275x1650 の文字の page、端の行、幅 2600・203・1275・1001）を ghostscript の `CCITTFaxEncode` で 20 の組（K −1・0 に EOL・byte 揃え・EOB の有無、K 4 は EOL つき、BlackIs1 と /Rows の有無を混ぜる）に符号化、`host-pdf-ccitt exact` | **80 例とも元の bitmap と bit 単位で同じ**（plain・ASan・UBSan）。表を 2 か所壊した copy は失敗を検出（負の対照） |
| CCITT の破壊 | `host-pdf-ccitt fuzz`（ASan・UBSan、80 例 × 100: byte の変更・挿入・削除・切断と parameter の変更） | 落ちず、sanitizer の報告なし、出力は常に行の整数倍で上限内 |
| CCITT の PDF | 同じ script: `ccitt.pdf`（XObject の 20 の組、BlackIs1 と /Decode、stencil mask、inline image の `/F /CCF`）を 3 build で | 3 page とも SKIPPED なし、3 build で同じ画素。page 1（150 dpi、1:1）pdftoppm とぼかし平均 4.991・64 超 0.348%（許容 6.0・0.6%: libpdf は画像の縁を滑らかにし、poppler は bit のまま描く）、page 2 3.568・0.081%（画像の許容 4.0・1.5%）、page 3 は ghostscript と 3.996・0.011%（poppler は byte 揃えの inline の stencil mask を描かない。ghostscript と libpdf は描く） |
| 実文書 | 同じ script と `run-pdf-text.sh` の 5 | **/usr/share/doc の 9 文書の SKIPPED の page: 3 → 0**。gnus-logo p1 は pdftoppm と 0.538・0.029%、txirefcard p1 4.044・0.000%（密な文字の許容 5.0） |
| CCITT の文書の破壊 | 同じ script の `fuzzdoc`（ccitt.pdf の非圧縮の copy、ASan・UBSan 各 100+100） | 落ちず、sanitizer の報告なし（全て開けた） |
| password | `sh plan/ws079/tests/run-pdf-text.sh 20` の新しい 4c: programs.pdf を qpdf で user "secret"・owner "owner" の RC4 40（R2）・RC4 128（R3）・AES-128（R4）・AES-256 R5・R6 に。`host-pdf-render renderpassword`（新: file と memory の両方の open が同じ答えか確かめてから描く） | 5 revision × 2 password × 3 build で平文と同じ画素。"wrong" と空の password は `open error 13`（EACCES）。32 byte を超える password（R4・R6、user と owner）と UTF-8 の password（R6、`pässwörd`・`öwner`）も同じ画素 |
| 回帰（run-pdf-text の残り） | 同じ run | **run-pdf-text: ok**。比較の数値は p008 と同じ（例: programs 0.963・0.000%、debian-faq p3 2.880・0.007%）、object stream・修復・空の password の暗号化の copy は同じ画素、破壊の loop（20 回）で落ちず |
| 他の host の回帰 | `run-pdf-render.sh 100`・`run-pdf-reader.sh`・`run-pdf-writer.sh`・`run-pdf-update.sh`・`run-notes-host.sh`（7 つの script の link に ccitt.c を足した） | 5 つとも ok |
| PDF Viewer の host 試験 | `sh plan/ws079/tests/run-pdfviewer-host.sh`（notes.pdf を qpdf で user "secret"・owner "owner" の AES-256 にした password.pdf を足した） | plain と ASan+UBSan で **host-pdfviewer: ok**。新しい検査 26: F9 で sidebar（168 px、page の幅 832 px）、frame では thumbnail を描かず待つ間に描く、A4 の thumbnail 104x146、thumbnail の click で page 3、page mode で一覧が追う、sidebar の drag は選ばない、狭い窓（360 px）に sidebar は無く広げると戻る、F9 で隠す。card: 開くと card、"wrong"＋Enter は拒否（赤い文、入力は消える）、"secreT" を Backspace で直して "secret"＋Enter で開く、password は残らない、Esc で閉じる、owner password と Open の click で開く。frame: `build/ws079-p006-host/viewer-plain/14〜21-*.png` |
| build | amd64: `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws079-p015-amd64 …/bin/pdfviewer …/bin/notes …/dynamic/libpdf.so …/dynamic/libtruetype.so`、pcat・pc98・rpi4: `config/ci/config-<p>.mk`・`BUILD=build/ws079-p015-<p>`・`…/dynamic/libpdf.so` | 4 つとも exit 0・warning 0（rpi4 は新しい build dir の初回に sysroot の順序で `errno.h` が無く失敗し、変更なしの再実行で通った。p007・p014 と同じ既知の問題）。toolchain は main の `build/llvm`・`build/llvm-source` への symlink を読むだけ、sysroot は複写 |
| 規約（機械的） | `plan/tools/style-check.py` | ccitt.c・crypt.c は違反 0。新しい関数と変えた関数（filter.c・reader.c・view.c・draw.c・document.c）に新しい違反なし（既存の違反は p009 で直す） |
| QEMU の Venus guest（KVM、zdesktop --glass 1280x800） | main の `build/ws035-sq/hdd-image.img`（21:50）の copy を `build/ws079-p015-run/` に置き、この worktree の pdfviewer・notes・libpdf.so・libtruetype.so を SSH で入れ替えて `plan/ws079/tests/pdf-demo-guest.sh OUT PREFIX start install ccitt password thumbnails stop`（新しい 3 段） | **全段 ok**、zdesktop の log の ERROR 0 |

guest の段（QEMU、画面は `/home/awe/zedBSD-rpi4/build/ws079-shots/ws079-p015-20260929-*.png`、host の frame は同じ所の `ws079-p015-host-*.png`）:
- ccitt: gnus-logo.pdf `PAGE index=0 flags=0`（`ccitt-gnus-logo.png`）、txirefcard.pdf の拡大（`ccitt-refcard-zoom.png`、Type 3 の CCITT の glyph を含む）、
  ccitt.pdf の 3 page とも `flags=0`（`ccitt-1〜3.png`、page 1 の raster 509x658 211 ms）。注意の pill は一度も出ない（`NOTICE shown` 0 行）。
- password: password.pdf で `PASSWORD asked … wrong=0`（`password-card.png`）、"wrong"＋Enter で `wrong=1`（`password-wrong.png`）、
  "secret" の伏せ字（`password-typed.png`）、Enter で `PASSWORD accepted`・`OPEN … pages=1`（`password-opened.png`）。log に password の行は 0。
- thumbnails: debian-faq.pdf（76 page）で F9 → `THUMBNAILS shown=1 sidebar=168`、待つ間に thumbnail を描く（`thumbnails.png`）。画面の
  sidebar の色から位置を求めて 3 つ目の thumbnail を click → `THUMBNAIL chose page=2`、titlebar は「Page 3 of 76」（`thumbnails-chosen.png`）。
  End で一覧が最後の page へ（`thumbnails-end.png`、10 個の thumbnail を描いた）。

## 未実施と制限

- 実機（i915・pen・touch の LCD）: 未実施。disk image への組み込みの build: 未実施（guest は main の image の copy に SSH で入れ替えた）。
- CCITT: K > 0 で EOL の無い data は tag を読む（ghostscript の書く形は読めない。poppler・pdf.js と同じ）。uncompressed mode（T.4 の拡張）は壊れた行として扱う。
  壊れた行の「前の行で置き換える」（T.4 の推奨）はしない。JBIG2・JPX は SKIPPED のまま。
- password: 入力は US 配列の ASCII だけ（zdesktop が keymap を送らないため。Files の欄と同じ）。R5・R6 の SASLprep はしない（ASCII と、qpdf が
  そのまま UTF-8 にした password は通る）。/P の権限は見ない（表示だけ）。Notes は暗号化の文書を引き続き拒む（書き込みは無い）。
- thumbnail: 一覧は縦の 1 列（窓の幅で列を増やさない）。drag・click・wheel は QEMU では click と key だけを試した（wheel と drag は host の試験）。
  touch の指の swipe での scroll は未確認（pointer の fallback で drag と同じはず）。
- 規約: 既存の code の違反（filter.c・reader.c・pdfviewer の各 file の blank-after-brace など）は p009 で直す。

## 残り

1. p009（WS079 の全 source の全文の規約の確認と回帰）。
