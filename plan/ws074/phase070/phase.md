<!-- awesome-plan project=zedbsd record=ws074p070 -->

# ws074-p070: @font-face（WOFF、libz-compat）と Amazon Ember

Phase ID: `ws074-p070`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p068

## 範囲（amazon-goal.md §4 の 8）

`@font-face` の web font を読み（WOFF 1.0 は libz-compat で展開、TrueType はそのまま）、`font-family` で使う。Amazon Ember
（WOFF2 と WOFF を出している。WOFF2 は Brotli が要るので WOFF を使う。中身は TrueType の glyf）。

## 設計と実装

- CSS（`css/parser.c`・`css.h`・`internal.h`）: `@font-face` の block（`@media` の中でも）の descriptor を読む: `font-family`（文字列か
  語の列の atom）、`font-weight`（数・normal・bold、範囲の 2 値）、`font-style`（italic・oblique は italic）、`src`（comma の列の
  `url()`（引用なし・あり）と `format()`、`local()` は捨てる、4 つまで）。family か source の無い face は捨てる。sheet が
  `css_font_face` の配列を持ち、`css_sheet_font_face_count`・`css_sheet_font_face` で見せる。`css_sheet_resolve_urls` は source も解決。
- text（`text/font.c`・`text.h`・`text/woff.c` 新）: 実行中に足す web の face（`TEXT_FACE_WEB` から 24 まで）と family の表
  （family は呼ぶ側の atom を同一性で比べる key、weight の範囲、italic、face）。`text_add_face`（sfnt の bytes を受け取って
  `truetype_open`）、`text_select_family`（CSS の font matching の要約: 指定の style の face を優先、weight は範囲の中、太字の
  weight なら重い方から、他は軽い方から。太字の weight に軽い face しか無ければ擬似の bold）。`text_font_file`: `wOFF` を sfnt に
  展開（header・table の record・4 byte 揃え、圧縮の table は `uncompress`、32 MB まで、大きさの矛盾は EINVAL）、TrueType は複写、
  WOFF2・CFF（OTTO）は ENOTSUP。
- layout（`layout/box.c`）: `layout_font_of` は style の family の列を前から見て、web font のある最初の family の face を使う（無ければ
  今まで通り）。face に無い glyph は今まで通り fallback の face から描く。
- page（`page/fonts.c` 新、`page.h`・`page.c`・`sheets.c`・`script.c`）: 使われる sheet ごとに（`sheets_add_loaded`）その
  `@font-face` の face を、読める最初の source（format が woff・truetype・opentype、format が無ければ .woff・.ttf・.otf）で一度だけ
  取得する（family・weight・style・location が同じなら再取得しない）。http・https は loader で非同期、他は即座に読む。届いたら
  sfnt にして次の layout の前に text system に足す（`page_fonts_install`）。`fonts_generation` が layout をやり直させる（style の
  再計算はしない）。取得・展開・open に失敗した face は failed で、その family は style の次の family に落ちる。
- 試験: `tests/images/fonts.html`（新）と `make-test-images.py` の追加（JetBrains Mono（OFL）を deflate した WOFF・格納の WOFF・TTF と
  license を `build/ws074-images/` に作る。font は git に入れない）、`tests/run-font-tests.py`（新）: 8 行の face（sans、WOFF、bold の
  face、TrueType、無い font の fallback、2 番目の family、範囲の italic、日本語を含む行）。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan）: golden 56/56（不変）、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、
  host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、run-loader-tests 11/11、
  run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8（plain と ASan）。ASan の `--render` で Amazon の 3 つの capture に報告なし。
- 壊れた font（途中で切れた WOFF、deflate の data を壊した WOFF、`wOF2` の署名、table の数を 65535 にした WOFF）: ASan で全て拒否され
  sans に落ちる（報告なし）。
- Chromium との比較: Chromium は `file://` の page の web font を CORS で拒む（origin が不透明）ので、比較は local の http server で行った。
  `live-compare.py http://127.0.0.1:…/fonts.html`（800x330）: 画素 94.95%（両方が JetBrains Mono の web font で描き、行の位置は一致。
  違いは glyph の縁）。
- Amazon（2026-09-28 23:52 の capture、local の http server、1280x900）: トップ 画素 34.16%・ink 24.28%、検索 68.83%・ink 20.06%
  （p062 の後と同じ）。日本の Amazon の本文は `font-family: Arial` か Hiragino・Meiryo で、Amazon Ember を名指す部品は少ない
  （トップで Ember の face で描く文字の run は 4 つ）ので、一致率はほとんど動かない。`--render` は host で検索 1.46 s、トップ 0.60 s。
- guest（QEMU の Venus、zdesktop 1280x800）: `/usr/share/browser-images/fonts.html` を窓で開き、各行が host と同じ face で描かれる
  （ERROR なし、写真）。guest の font は file の path（同期の読み込み）。loader の非同期の path は host の `--render` の http で通った
  （上の比較）。live の Amazon はこの Phase では取得していない。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p070-20260929-fonts-http.png`（私たち | Chromium | 違い）、`p070-20260929-amazon-top-http.png`・`…-search-http.png`。
- guest（QEMU）: `p070-20260929-guest-fonts.png`。

## 未実施・残り

- 実機は未実施。
- WOFF2（Brotli の展開と glyf の変換）: Amazon Ember JP・Modern Display・Modern Text は WOFF2 だけ。CFF の outline（OTTO）。
- family の名前の大文字・小文字の無視（今は atom の同一性）、`unicode-range`、`font-display`、`font-stretch`、synthetic italic（斜体の
  合成）、web の face の数の上限 24、page が変わっても face は page の寿命の間残る。
- 壊れた glyf を持つ正しい形の font に対する libtruetype の頑健さ（fuzz）は未確認。
