<!-- awesome-plan project=zedbsd record=ws074p046 -->

# ws074-p046: 組み込み 1b（Array・String・JSON）

Phase ID: `ws074-p046`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p026（cleared）

## 範囲（正常系のワンパス、design.md §12.3）

ws074-p026 から分けた（2026-09-28）。

- Array（構築子・isArray・of・from（array-like だけ）、prototype の変更系・非変更系・反復系の method、ES2023 の複製系
  toSorted・toReversed・toSpliced・with、安定な sort、穴の扱い）。
- String（構築子・fromCharCode・fromCodePoint・raw 以外の正規表現の要らない method、wrapper の object の添字と長さ、
  Unicode の大文字・小文字の変換（UCD 16.0.0 から build の時に表を生成、final sigma の規則））。
- JSON（parse と reviver、stringify と replacer（関数・配列）・indent・toJSON・循環の検出、孤立した surrogate の escape）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0。
2. 自前の JS の試験が Chromium と同じ（host plain・ASan、guest）。test262 の数を記録。
3. 前の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- 実装:
  - `js/builtin_array.c`・`js/builtin_string.c`・`js/builtin_json.c`（新）。`js/builtin.c` の導入の順に array・string・json。
  - `base/unicode.h`・`base/unicode.c`（新）: `wb_case_map`（SpecialCasing の完全な写像、無ければ UnicodeData の単純な写像）、
    `wb_case_is_cased`・`wb_case_is_ignorable`（final sigma の判定）。
  - `tools/gen-unicode-case.py`（新）: UnicodeData.txt・SpecialCasing.txt（16.0.0、Makefile に URL と SHA-256）から
    `$(BUILD)/zdesktop-browser-gen/unicode-case.c` を build の時に生成（user の決定: 仕様由来の表は commit しない）。
    `plan/ws074/tests/fetch-distfiles.sh`・`host-build.sh`・`guest-build.sh` も対応。
- 試験:
  - `plan/ws074/tests/js/collections.js`（46 行）を加えて自前の JS の試験は 7 件、全部 Chromium 153 と同じ出力（host plain・ASan）。
  - host-base 2038・host-heap 31・host-object 98・host-interp 45・host-number 71・host-link 22（plain・ASan）、golden 12/12。
- test262（`test262-7ab7fafa`）: **14255/47792（29.8%）、ES5 の範囲 6786/8087（83.9%）**（p026 は 9327 と 4822）。
  built-ins の失敗: Array 672/3082、String 470/1223、JSON 57/165。多い理由: Symbol が無い（219）、let/const・arrow・async・
  正規表現の構文（後の Phase）、`value is not a function`（iterator の method・正規表現の method 等の未実装）、
  `Invalid array length`（40、下の follow-up）。
- ASan（UBSan 込み）: 自前の試験と JS の試験で報告なし。
- amd64 の build: warning 0。style-check: `js/builtin_array.c`・`builtin_string.c`・`builtin_json.c`・`base/unicode.c` 0。
- guest・boot test: 下の「guest と boot」。実機: 未実施。
- commit: `a38a78ce`・`eb43ead9`・`1b2b8608`（code）、`82903fb1`・`2afc5c06`・`9c14805e`（style）、`070eae16`（試験）、
  この記録の commit。

## guest と boot

- guest（QEMU、plain）: test262 の built-ins/Array・String・JSON の 2888 件（5743 回）が host と全部同じ結果（ok 3871）。
  host-object 98・host-interp 45・host-number 71 も通過。JS の試験 7/7（operators の 1 行は BUG-078 の既知の差のまま）。
  大文字・小文字の生成した表も guest で同じ結果。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p046-20260928-boot-login.png`）。

## 後回し（follow-up）

- `String.prototype.normalize` は形の検査だけで写像は恒等（正規化の表が要る）。
- `Array.from` は array-like だけ（iterable は Symbol・iterator の Phase、p028）。species（Symbol.species）無し。
  `Array.prototype` の keys・values・entries・`[Symbol.iterator]`、String の `[Symbol.iterator]` 無し（p028）。
- 長さが 2^32-1 を越える array-like（ToLength の 2^53-1）を method が扱わない（`Invalid array length` の 40 件）。
- 正規表現を使う String の method（match・matchAll・replace・replaceAll・search・split の RegExp）: p027。
- `localeCompare` は code unit の比較（Intl は範囲外）。
