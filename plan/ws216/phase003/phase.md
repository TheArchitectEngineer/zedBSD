<!-- awesome-plan project=zedbsd record=ws216-p003 -->

# ws216-p003: Notes の model と形式（道具の種類、形の列、焼き込み、編集 data 3.0、journal 3、部分消し、PDF の書き出し）

Parent: [WS216](../ws.md)
Status: in-progress（2026-10-13 P1: 実装と host の受け入れ B1〜B10 は済み、Q1 の review・merge の待ち。QEMU・実機は p007）
Phase disposition: normal
Queue: Q1 の依頼（2026-10-13、P1）。承認は ws.md の「ユーザーの決定」（D1〜D12）と review-2 の判定（p003 の前に M-5・M-10・M-12 をこの phase.md で解決）

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §2・§3.1〜§3.3・§3.5・§5.2〜§5.4 と §8 の p003 の行。review は [review-1](../phase001/review-1.md)・[review-2](../phase001/review-2.md)。libpdf の API は [p002](../phase002/phase.md)。
- 触る file: `userland/desktop/notes/`（`notes.h`・新しい `shape.c`・`document.c`・`encode.c`・`journal.c`・`save.c`、`main.c` は journal の復旧の 1 か所だけ、3 つの Makefile）、`plan/ws216/`。既存の host 試験の source の一覧（`plan/ws079/tests/run-notes-host.sh`・`plan/ws175/tests/run-host-notes-edit.sh`）に新しい file を足す（link の追従だけ）。
- 触らない: 画面（`render.c`・`geometry.c`・shader、p004）、塗りつぶしを作る所と塗りの消しゴム（p005）、UI（`ui.c`・道具の選択・key、p006）、libpdf（p002 で済み）、HAL・toolchain。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU は p007 で T1。

## 1. 道具の種類と定数の版（M-10）

- `NOTES_TOOL_BRUSH 2`・`NOTES_TOOL_PENCIL 3`・`NOTES_TOOL_WATERCOLOR 4`・`NOTES_TOOL_FILL 5`。
- `struct notes_stroke` に `constants`（道具の定数の版）を足す。ペン・蛍光ペンは 0、新しい 4 つは `NOTES_TOOL_CONSTANTS 1`。`notes_stroke_create()` が種類から決める。この版の定数: 鉛筆の濃さの表（`0.30 + 0.70 q/7`）と粒の下限 `g_min 0.35`、粒の tile の作り方、水彩の 3 つの alpha（0.22・0.10・0.06）、libpdf の style（下限 0・指数 1・上限 2.0・半径の下限 0.05 pt）。焼き込んだ値（係数・段）を形に直す所の数は全部この版に属し、式（速さ・入り抜き・揺らぎ）は属さない（焼き込むので）。
- 編集 data（`TOOL` の「曲線の id」の byte）と journal 3 の stroke の record（種類の次の 1 byte）の両方に持つ。`tools_find` の鍵は (種類, 色, 幅, 定数の版)。
- 知らない定数の版（種類に対して今の版でない値）: 編集 data は `ENOTSUP` で decode を断り、file は「他の PDF」として開く（`notes_open_pdf()` は decode の失敗を全部そう扱う）。journal は復旧せず set aside する（`notes_journal_recover()` が `ENOTSUP` を返し、`main.c` が `notes_journal_set_aside()` して `NOTES RECOVER failed reason=version` を出す）。
- 1.1・2.0 の file の種類 2〜5 の stroke は拒む（L-13、`EINVAL` → 他の PDF）。1.1・2.0 の「曲線の id」は今どおり読み捨てる（今と同じに読む）。3.0 で種類 6 以上は拒む。

## 2. 焼き込む値と式（p001 §3.1〜§3.3、L-8）

- 焼き込むのは `notes_document_add_stroke()` の中（最後の sample を足した後、頁に入れる前。`app_end_contact()` は最後の sample を足してから呼ぶ）。指の取り消し（`app_abort_contact()`）は add_stroke を呼ばないので焼き込まない。journal の再生・decode・部分消しの切れ端は焼き込んだ値を持って来るので計算し直さない。
- 焼き込む前（描いている途中）の形は同じ式でその場で計算する（今の終わりを終わりとして）。
- 係数は u16（0..65535 が 0..2.0）、鉛筆の段は u8（0..7）。

| 道具 | 列 | 式（数は p007 で調える初期値） |
| --- | --- | --- |
| 筆 | F 1 列 | F = P × V × T。P = max(0.05, p)。V = clamp(1.15 − 0.35 v / v_ref, 0.55, 1.15)、v は ±8 ms の窓の弧長 / 時間（pt/ms、同じ時刻だけなら 0）、`v_ref = 0.6 pt/ms`。T = min(入り, 抜き)、入りは弧長 `min(3w, 0.25L)`、抜きは `min(4w, 0.3L)` で smoothstep に 0.08 → 1（L = 0 なら 1） |
| 鉛筆 | F 1 列と段 | F = 0.6 + 0.4 p^0.7。生の段 q = round(7 p^0.8)。段の変化は、同じ新しい段が弧長 2 pt 以上続いた時だけ採る（短い所は前の段のまま、終わりの短い変化は捨てる） |
| 水彩 | C・E1・E2 の 3 列 | C = 0.85 + 0.15 p、E1 = 1.18 C (1 + 0.08 n1(s))、E2 = 1.36 C (1 + 0.08 n2(s))。n は弧長の 1 次元の value noise（格子の間隔 w、値 −1..1、smoothstep の補間）、seed は stroke の id と縁の番号（整数の hash） |

## 3. 形の列（`struct notes_shape`、p001 §2）

- `notes_stroke_shapes(stroke)` が作り、点か焼き込んだ値が変わるまで stroke に持つ（`shapes`・`shape_count`）。各 shape は polygon（角の列、pt）、輪郭の数と輪郭ごとの角の数（1 つの時は NULL）、alpha（色の alpha に掛ける）、paint（SOLID・GRAIN）、blend（NORMAL・MULTIPLY）、継ぎ目の辺の index（`seam_start`・`seam_end`、無ければ `PDF_OUTLINE_NO_SEAM`、p002 §1.2 の約束）。
- ペン・蛍光ペン: 1 つ（`pdf_outline_stroke()`、今と bit 同一）。筆: 1 つ（`pdf_outline_stroke_style()`）。鉛筆: 段の変わる点を cut にした `pdf_outline_stroke_ranges()` の区間ごとに 1 つ（GRAIN、alpha は区間の段の濃さ `range.start` の段）。水彩: C・E1・E2 の順に 3 つ（MULTIPLY、0.22・0.10・0.06）。塗り: 1 つ（複数の輪郭、点の列そのまま）。
- `outline`（今の画面と当たりの早い判定が使う 1 つの polygon）は p004 まで残す: ペン・蛍光ペン・筆は shape の polygon、鉛筆は区間に切らない全体の輪郭、水彩は E2、塗りは最初の輪郭。`bounds` は全部の shape の角の箱。
- 粒の tile（L-12、L-5）: `notes_grain_tile()`（shape.c）。256×256 の 8 bit、整数だけの value noise（格子の間隔 4 texel、1 tile に 64 格子、64 で wrap）と整数の contrast の表。`notes_grain_level()` で 128・64 の縮小（2×2 の整数の平均）も作る（p004 が使う）。PDF は level 0 を `pdf_writer_add_grain()` で 1 度、cell 48 pt。

## 4. 編集 data 3.0（p001 §5.4、D9）

- 版の選び方: 新しい種類の stroke が一つでもあれば 3.0、なければ編集ありで 2.0、どちらもなければ 1.1（今と同じ bytes）。3.0 は 2.0 の上位で、`IMAG`・`EDIT` は `major >= 2` で読む。新しい Notes は 1・2・3 を読む。
- stroke の flags（3.0）: `FACTORS 0x04`（点の後に列の数 k の u8 と点ごとに k 個の u16）、`STEPS 0x08`（その後に点ごとに u8）、`CONTOURS 0x10`（点の数の後に輪郭の数 u16 と輪郭ごとの点の数 u32、点はその後）。塗りは `TIME` を立てない（L-10、文書と journal の両方）。
- 種類ごとに要る flags（無い・余計は `EINVAL`）: 筆・鉛筆 FACTORS（k = 1）、鉛筆 STEPS、水彩 FACTORS（k = 3）、塗り CONTOURS（輪郭 1 個以上、各 3 点以上、和が点の数）。段は 0..7。焼き込んでいない新しい種類の stroke の encode は `EINVAL`（document に入る時に必ず焼き込むので起きない）。

## 5. journal 3（M8・M-10・L-9）

- header の版は「表せる一番低い版」: 文書に新しい種類の stroke が無ければ 2、あれば 3。版 2 の journal が開いている時に新しい種類の stroke の record を足すなら、今の文書の snapshot で版 3 の journal を書き直して（一時の名前から rename）から record を足す。
- 版 3 の stroke の record: 番号、種類、**定数の版（u8）**、色、幅、flags、開始、点の数、[輪郭]、点、[係数]、[段]。版 2 の record は今のまま。
- 復旧は版 2 と 3 を読む。知らない定数の版は §1 のとおり set aside。

## 6. 部分消し（S2、M-5）

- 切れ端は元の点の焼き込んだ値をそのまま写し、境の点の値は両隣の線形の補間（係数は u16 で丸め、段は近い側）。定数の版も写す。
- 当たりと切る reach（M-5）: `radius + 見える半幅`。見える半幅はペン・蛍光ペンは `width / 2`（今のまま）、筆・鉛筆は `width / 2 × 焼き込んだ係数の最大`、水彩は `width / 2 × E2 の最大`。切れ端の丸い cap は消しゴムの円に入らない。
- 塗りの stroke は p005 まで消しゴムの当たりに入れない（`stroke_touches()` が当たらない、と注記。D7 (c) の規則は p005）。

## 7. PDF の書き出し（p001 §5.2・§5.3）

- 各 stroke の各 shape を 1 回の fill（複数の輪郭は subpath、nonzero）。blend は `pdf_writer_set_blend_mode()`、alpha は色の alpha × shape の alpha。GRAIN は (1) 単色 `a·g_min`、(2) `pdf_writer_set_fill_grain()` の粒 `a`（cell 48 pt）の 2 回の fill。頁の終わりに blend を NORMAL に戻す。
- 3.0 の文書は `pdf_writer_set_content_flate(writer, 1)`（新しい文書と他の PDF の上の revision の両方）。1.1・2.0 は今どおり無圧縮。

## 8. 受け入れ

| # | 条件 |
| --- | --- |
| B1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk` の `bin/notes`、Linux の host rules の `bin/notes`）warning 0。FreeBSD は native の build ができなければ未実施と記録（M-12） |
| B2 | 3.0 の往復: 筆・鉛筆・水彩・塗り（輪郭 2 つ）・ペンの文書を encode → decode して model（点・係数・段・輪郭・定数の版・道具）が一致し、もう一度 encode した bytes が同じ |
| B3 | 古い版: base（d3a486f37）の Notes の encode.c・save.c で書いた 1.1・2.0 の file を新しい Notes が同じ model に読み、新しい Notes が書く edit data の bytes が base の Notes と同じ。1.1・2.0 の content に `/Filter` が無い。1.1・2.0 の種類 2〜5 と、知らない定数の版の 3.0 は decode を断る |
| B4 | base の Notes（1・2 だけを読む）は 3.0 の file を他の PDF として開き、ペンを足して保存できる。新しい Notes でその file を開くと新しい道具の stroke は背景（base の content）になり、足したペンは編集できる（M9 の模型） |
| B5 | journal: base の Notes が書いた版 2 の journal を新しい Notes が復旧する。新しい道具の journal は版 3 で、復旧すると同じ model。版 2 の journal に新しい道具を足すと版 3 に書き直される。知らない定数の版の journal は `ENOTSUP` |
| B6 | 部分消し: 筆・鉛筆・水彩で、切る円（半径 + 見える半幅）+ 見える半幅 + 点 2 つ分の間隔の外の輪郭の角が元と同じ（許容差 0.01 pt、両向き。角は中心線から見える半幅まで離れるので、p001 §8 の「円 + 幅の半分 + 点 2 つ分」に見える半幅をもう 1 つ足した範囲）。切れ端の全部の shape が半径 `radius − 0.01 pt` の円と交わらず、円の中心を含まない（M-5）。undo・redo で元に戻る |
| B7 | ペンの見た目の不変: ペン・蛍光ペンの shape と outline が `pdf_outline_stroke()` と bit 同一、ペンだけの文書の PDF の頁の content が base の Notes と同じ bytes |
| B8 | 3.0 の保存 → 開くで `NOTES_OPENED_NOTES`（Flate の content の hash）。`qpdf --check` を通る。clean copy（M-13）も `qpdf --check` を通り、元と画素が近い |
| B9 | 粒の向き: 鉛筆で塗った広い形の画素（pdftoppm）が、粒の tile から計算した `1 − (1 − a·g_min)(1 − a·h)` と合う（新しい頁、高さの違う頁、`/Rotate 90` の他の PDF の上の頁、M-6） |
| B10 | 既存の host 試験 `run-notes-host.sh`・`run-host-notes-edit.sh`・`run-host-clean.sh` が通る。全部の新しい試験が plain・ASan・UBSan で通る |

試験: `plan/ws216/tests/run-notes-tools.sh`（`host-notes-tools.c` と base の Notes の模型 `host-notes-old.c`）。出力は worktree の `build/ws216-p003-host/`。

## 9. 実行の記録（2026-10-13 P1、agent/p1、base main d3a486f37）

### 9.1 変えた所

| file | 内容 |
| --- | --- |
| `notes.h` | 種類 2〜5、`NOTES_TOOL_CONSTANTS`、paint・blend、係数の単位と上限、鉛筆の段、粒の tile と cell と g_min。`struct notes_shape`。`struct notes_stroke` に constants・factors・factor_columns・steps・contours・shapes。shape.c の関数、`notes_encode_stroke()`・`notes_decode_stroke()` に版の引数、`notes_document_new_tools()` |
| `shape.c`（新） | 種類の定数の版と列の数、焼き込み（`notes_stroke_bake()`）、見える半幅（`notes_stroke_reach()`）、形の列（ペン・蛍光ペンは今と同じ `pdf_outline_stroke()`、筆・水彩は `pdf_outline_stroke_style()`、鉛筆は `pdf_outline_stroke_ranges()`、塗りは輪郭の列）、筆・鉛筆・水彩の式、整数だけの粒の tile と縮小 |
| `document.c` | stroke の作成で定数の版、解放で shapes・値、`notes_stroke_outline()` は shapes と全部の shape の箱、`notes_document_add_stroke()` で焼き込み、部分消しの切れ端が値と定数の版を写す（境は補間）、当たりと切る reach は `radius + notes_stroke_reach()`（M-5）、塗りは p005 まで当たらない。**既存の不具合の直し**: 取り消した stroke の追加の entry が忘れられる時（undo の後に新しい変更）に stroke が解放されなかった（`entry->count` が 0 のまま、ASan の leak で見つけた）→ revert で count 1、apply で 0 |
| `encode.c` | 3.0 の header、`TOOL` の定数の版、flags `FACTORS`・`STEPS`・`CONTOURS`、塗りは `TIME` なし、`IMAG`・`EDIT` は `major >= 2`、種類と定数の版の検査（1.1・2.0 の種類 2〜5 は EINVAL、3.0 の知らない定数の版は ENOTSUP）、journal の stroke の版 2・3 |
| `journal.c` | 版 2・3、一番低い版、版 2 の journal に新しい道具 → 版 3 で書き直し、復旧は 2・3、知らない定数の版は ENOTSUP |
| `save.c` | shape ごとの fill（blend、alpha、複数の輪郭、粒の下塗りと pattern）、粒は文書に一度、3.0 は content の Flate |
| `main.c` | 復旧が ENOTSUP なら journal を set aside して `NOTES RECOVER failed reason=version`（1 か所） |
| 3 つの Makefile | `shape.c`（M-12） |
| `plan/ws216/tests/host-notes-tools.c`・`host-notes-old.c`・`run-notes-tools.sh`（新） | §8 の受け入れ |
| 他 WS の試験（Q1 の許可、2026-10-13）| `plan/ws079/tests/run-notes-host.sh`・`plan/ws175/tests/run-host-notes-edit.sh` の source の一覧に `shape.c` と libpdf の `paint.c` を足した（link の追従だけ） |

### 9.2 受け入れの結果（host、gcc 14.2.0、poppler 25.03.0、qpdf 12.2.0）

`sh plan/ws216/tests/run-notes-tools.sh` → `run-notes-tools: ok`（plain・ASan・UBSan で run と old、出力 `build/ws216-p003-host/`）。

| # | 結果 |
| --- | --- |
| B1 | amd64（`ZEDBSD_CONFIG=config/current-uat.mk make build/amd64/bin/notes`）・rpi4（`config/ci/config-rpi4.mk`、`build/arm64/bin/notes`）とも exit 0、-Werror で warning 0。Linux の host rules（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws216-linux build/ws216-linux/bin/notes`）exit 0・warning 0。**FreeBSD の native の build は未実施**（`Makefile.freebsd` に shape.c を足しただけ） |
| B2 | 筆・鉛筆（21 区間と 17 区間）・水彩・塗り（輪郭 2 つ）・ペンの文書: 全部焼き込まれ、encode（3.0）→ decode で model が一致、もう一度 encode した bytes が同じ。plain・ASan・UBSan の t30.bin が同じ bytes |
| B3 | base の Notes（d3a486f37 の source を `git show` で build）が書いた old11（1.1）・old20（2.0、JPEG の挿入）を新しい Notes が `NOTES_OPENED_NOTES` で開き、encode が base の bytes と同じ。両方の Notes の reencode が同じ bytes。old11・old20・ペンだけの文書を両方の Notes で保存した PDF が qpdf の qdf で日付の行を除いて同じ bytes。content の `/Filter` は 1.1・2.0 に無く 3.0（t30.pdf の 2 頁）は FlateDecode。2.0・1.1 に印した道具の文書、4.0 は EINVAL、知らない定数の版は ENOTSUP |
| B4 | base の Notes は t30.pdf（3.0）を `opened 2`（FOREIGN）で開き、ペンを足して保存できた。新しい Notes はその file を ANNOTATED で開き、頁 1 の stroke は base の Notes のペンだけ（origin OVER）、頁 2 の鉛筆は背景（M9） |
| B5 | ペンだけの journal は版 2、筆で版 3 に書き直し、鉛筆・水彩・塗り・部分消し・undo の後の復旧が同じ model。base の Notes の版 2 の journal を復旧すると base の Notes の文書と同じ bytes、続けて筆を足すと版 3 になり復旧が同じ。知らない定数の版の stroke を足した journal は ENOTSUP |
| B6 | 筆 12 pt・鉛筆 2.4 pt・水彩 28 pt × 6 seed、消しゴム 10 pt: 2 つの切れ端、域の外の角が両向きで 0.01 pt 以内、どの切れ端の shape も円（半径 − 0.01 pt）と交わらず中心を含まない（M-5）、undo・redo で戻る |
| B7 | ペン・蛍光ペン 20 本の shape が outline そのもので、`pdf_outline_stroke()` と bit 同一。ペンだけの文書の PDF が base の Notes と同じ（B3） |
| B8 | t30.pdf の保存 → 開くで `NOTES_OPENED_NOTES`（Flate の content の hash）、同じ model。t30・clean copy・grain・old11・old20・ペンの PDF が `qpdf --check` を通る。clean copy の 72 dpi の画素が元と完全に同じ（M-13）。水彩の `/BM /Multiply` |
| B9 | 鉛筆（黒、全筆圧、幅 300 pt）の 1536 dpi の切り抜き 87616 画素: 新しい頁・他の PDF の A4 の頁・`/Rotate 90` の 420×600 の頁で 100% の画素が粒の tile の式（隣の texel の範囲 ±4）に合う。上下を逆にした tile は 48%、縦横を入れ替えた tile は 50%（向きを見分けられる）。tile の FNV-1a は `2a644409`（p004・p009 の比較の元） |
| B10 | `run-notes-host.sh` ok、`run-host-notes-edit.sh` PASS、`run-host-clean.sh` PASS |

目視: `build/ws216-p003-host/t30-page-1.png`（筆の入り抜き、鉛筆の粒、水彩の芯と縁、穴のある塗り、ペン）と `grain-foreign-page-2.png`。

### 9.3 決めたこと

- §1〜§7 のとおり。M-5（見える半幅の reach）、M-10（定数の版を stroke と journal 3 の record に、知らない版は ENOTSUP と set aside）、M-12（3 つの Makefile、Linux の build）、L-8（add_stroke で焼き込み、取り消しでは焼き込まない）、L-9（journal も一番低い版）、L-10（塗りは文書と journal の両方で TIME なし）、L-12（粒の tile は shape.c、p004 が使う）、L-13（1.1・2.0 の種類 2〜5 は拒む）、L-5（格子の間隔 4 texel と 2 texel の 2 octave、64・128 で wrap）。
- 筆の `v_ref` は 0.6 pt/ms の初期値（p007 で調える）。水彩の揺らぎの格子の間隔は幅 w（周期 約 2w）。

### 9.4 未実施・残り

- QEMU・実機（5330・Pi 4）の確かめは無い（画面は p004、QEMU は p007 で T1）。今の画面（p004 の前）は新しい道具の stroke を `outline`（筆は形、鉛筆は区間に切らない全体、水彩は縁 2、塗りは最初の輪郭）の単色で描く。道具は p006 まで UI から選べない。
- FreeBSD の native の build は未実施。
- 試験の副作用（Q1 に報告）: 最初の試行で `XDG_DATA_HOME` を相対 path で渡したため、Notes が利用者の `~/.local/share/keiland/notes/` に journal（`d6c91947e6d6d701.journal`）を書いた（直した: 試験は絶対 path にし、相対なら FAIL）。`plan/ws175/tests/run-host-notes-edit.sh` も同じ形（相対の出力先）で `~/.local/share/keiland/notes/` に `*.journal.images`・`*.journal.kept` を書く（既存の挙動）。
- `plan/ws079/tests/notes-perf.sh` は Notes の source の一覧に `shape.c`・`paint.c` が無いので link できない（Master の Tools に無く、WS079 の cleared の Phase からだけ参照）。試験の整理の基準で削除の候補として Q1 に送る。
