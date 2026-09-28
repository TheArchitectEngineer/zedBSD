<!-- awesome-plan project=zedbsd record=ws074p027 -->

# ws074-p027: RegExp の engine と String の regex の method

Phase ID: `ws074-p027`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p026（cleared）
由来: amazon-goal.md §4 の 12「ws074-p027・p065 の一部」の RegExp の部分。p037 の後、「時間があれば次の amazon-goal.md の Phase」
（main の指示）として行った。同じ行の Date・Promise 等（p065 の一部）はこの Phase に入れていない。

## 範囲

design.md §12.3 の「自前の backtracking の engine」: 正規表現の literal（parse の時の早期の誤り）、`RegExp`（構築子と関数の呼び出し、
`exec`・`test`・`toString`・`compile`（Annex B）、`flags`・`source`・各 flag の getter、`lastIndex`）、String.prototype の `match`・
`replace`・`replaceAll`・`search`・`split`（文字列の場合も、今まで無かった）。flag は d・g・i・m・s・u・y。
この pass に無いもの: v flag（SyntaxError）、Unicode の property escape（`\p`・`\P` は u で SyntaxError）、modifier（`(?i:)`）、
重複した group 名（ES2025）、Symbol（未実装）による `@@match` 等の差し替えと `Symbol.species`、`matchAll`（iterator が要る）、
`RegExp.$1` 等の legacy の static property、`RegExp.escape`。

## 設計と実装

- engine（`js/regexp.c`・`regexp.h`、新）: pattern を node の木に parse（ECMAScript の文法。u の無い時は Annex B: 裸の `]`・`{`・`}`、
  legacy の八進、identity escape、文字の無い `\c`、量化できる lookahead、class の `[\d-z]`）、木を 32 bit の語の program に compile、
  backtracking の matcher が C の stack でなく自前の stack で走らせる。choice point と、capture・loop の counter・loop の開始位置の
  変更の記録（undo log）を同じ stack に積み、失敗で戻る時に元に戻す。lookaround は body を barrier の上で走らせ、成功で body の
  choice を切る（atomic）。lookbehind の body は後ろ向きに compile（逆順の連接、後ろ向きの文字・後方参照、capture の開始と終わりの
  入れ替え）。常に文字を消費し capture を含まない body の `?`・`*`・`+` は単純な choice、他は counter の loop（各回で中の capture を
  undefined に戻し、最小に達した後の空の回は失敗）。大文字小文字の無視は Canonicalize（u 無し: 1 単位の大文字、ASCII の外から中へは
  写さない。u: simple mapping からの simple case folding）。`\w` は u+i で ſ・K（Kelvin）も。名前付きの group と `\k<name>`、
  `\u{...}`・surrogate の対（u）。最初の文字が決まる pattern はその単位を先に探す。1 回の match は 4 億 step・3200 万 entry で打ち切り
  （RangeError）。
- 組み込み（`js/builtin_regexp.c`、新）: RegExp の object は kind `VM_KIND_REGEXP` の普通の object で、internal が状態の cell
  （pattern と flags の文字列、compile した program。cell の finalize で program を解放）。仕様の RegExpBuiltinExec（結果の配列、
  `index`・`input`・`groups`・d の `indices`）、RegExpExec（`exec` の property を呼ぶ）、`@@match`・`@@replace`（GetSubstitution の
  `$$`・`$&`・`` $` ``・`$'`・`$n`・`$nn`・`$<name>`）・`@@search`・`@@split`（y を足した splitter）の手順を Get・Set で行う。途中の
  list は heap の配列に置く（collector が見る）。`js_regexp_is`・`js_regexp_create` 等を `builtin.h` で String に渡す。
- String（`js/builtin_string.c`）: `match`・`replace`・`replaceAll`・`search`・`split`。RegExp の時は上の手順、文字列の時は仕様の
  文字列の手順（`replace` の関数の引数、`split` の limit と空の区切り）。
- VM: `VM_OP_NEW_REGEXP`（dst、pattern と flags の定数。realm の `VM_INTRINSIC_REGEXP` を new.target にして構築）、
  `VM_INTRINSIC_REGEXP_PROTOTYPE`・`VM_INTRINSIC_REGEXP`。compiler は `JS_NODE_REGEXP` をこの命令に、parser は literal の flags と
  pattern をその場で compile して、誤りを SyntaxError（早期の誤り）にする。
- `userland/desktop/libbrowser/Makefile` に `js/builtin_regexp.c`・`js/regexp.c`。

## 試験

- `plan/ws074/tests/js/regexp.js`（新）と `regexp.expected`（Chromium の出力、`run-js-tests.py --reference`。他の expected は不変）:
  literal・flag・exec の結果・g と y の lastIndex・split・replace（関数と `$` の形）・replaceAll・search・名前付き group・lookbehind・
  後方参照・量化の capture の reset・u の surrogate・s・m・d の indices・Annex B の形・誤りの種類。

## 確認（host は Debian の cc。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変えた 12 の file）: 新しい指摘 0（vm.h の 3 件と bytecode.h の
  1 件は前から）。
- test262（`test262-7ab7fafa`、worktree の `build/ws074-suites` に取得）: **14255 → 15762 / 47792**、ES5 の範囲 **6786 → 7592 /
  8087**。`built-ins/RegExp` 912/1958（ES5 504/504）、`language/literals/regexp` 242/248、`built-ins/String/prototype` 882/1184
  （ES5 630/631）。新しく落ちた 1 件（`String/prototype/replace/tostring-this-throws-symbol.js`）は、前は `replace` が無いための
  TypeError で偶然通っていたもの（`Symbol()` が未定義）。RegExp の残りの多くは property escape（441）・v flag（114）・modifier（70）・
  Symbol・let/const・arrow・for-of・class・Reflect が要る試験。engine の残り: `ΐ` と `ΐ` の full case folding、u の無い
  group 名の ID_Start の判定（非 ASCII を全部通す）。
- ASan（UBSan 込み）: test262 の RegExp（1966）・String.prototype（1185）・regexp の literal（248）で報告 0、pass の数は plain と同じ。
  Amazon の 3 つの capture（script のある top-local を含む）の `--render` で報告 0。
- 回帰（plain と ASan）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、
  host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-js-tests 8/8（regexp を追加）、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、host-relayout 161/161。
- Amazon（2026-09-28 23:52 の capture、host）: script のある top-local の Uncaught のうち「not supported yet: regular expressions」
  6 → **0**。残りの主なもの: `Date is not defined` 24（最初に落ちる script はこれ）、`P is not defined` 37（AUI の P を定義する
  script が Date 等で止まる）、let/const 3・arrow 2・template 1。`--render` は top-local 0.77 s、search-local 1.05 s（script 無しの
  capture は 0.62 s・0.91 s で p037 と同じ）。
- guest（QEMU、worktree の image）: `plan/ws074/tests/js/*.js` を guest の `/bin/browser --js` で走らせ、`run-js-tests.py --outputs`
  で 8/8（regexp を含む）。live の `https://www.amazon.co.jp/`（取得 1 回）: 30 s でトップが前と同じに出る（写真）。guest の log は
  console の行を除くので、guest での Uncaught の数は未確認。
- `plan/tools/boot-test.sh`: 未実施（guest の image は起動し、上の試験が通った）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- guest（QEMU）: `p027-20260929-guest-amazon-top-30s.png`。

## 未実施・残り

- 実機は未実施。boot-test は未実施。
- v flag、Unicode の property escape（design.md §7 の表が要る）、modifier、重複した group 名、`matchAll`（iterator）、Symbol による
  `@@match` 等と `Symbol.species`（p028 の Symbol の後）、`RegExp.$1` 等の legacy の property、`RegExp.escape`、full case folding。
- Amazon の script を先へ進めるには `Date`（最初の blocker）、let/const・arrow・template（p028）が要る。amazon-goal.md の 12 の残り
  （Date・Promise 等、p065 の一部）は未着手。
