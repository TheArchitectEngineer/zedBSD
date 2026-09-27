<!-- awesome-plan project=zedbsd record=ws074p024 -->

# ws074-p024: JS の lexer と parser（ES2024 の構文 → AST）、test262 の構文の試験

Phase ID: `ws074-p024`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲（正常系のワンパス、design.md §12.1）

- lexer: UTF-16 の source。ASI のための改行の印、regex literal と除算の区別（parser が式の頭で `/` を読み直す）、template
  （cooked と raw、不正な escape は tagged template では cooked が無い）、数（十進・16・8・2 進、legacy の 8 進、区切りの `_`、BigInt）、
  文字列の escape（legacy の 8 進は strict で誤り）、identifier の Unicode escape（decode した text と、escape の無い時だけの keyword）、
  script の HTML 風の comment（Annex B）、private name。
- parser: 手書きの再帰下降 → arena の AST。構文の誤りは longjmp で js_parse へ戻る。ES2024 の全体: 文・宣言、destructuring
  （arrow の引数と代入の cover grammar）、class（field・private・static block・accessor）、async・generator・async generator、
  optional chaining、`??`、論理代入、template、module（import・export・import attributes・import()・import.meta）、strict mode と
  directive、早期の誤りのうち scope 解析の要らないもの（strict の予約語・eval と arguments、重複の引数、yield・await の文脈、
  super・new.target の位置、label、break・continue の対象、`__proto__` の重複など）。
- 出力: `browser --dump=ast [--module] [--strict] FILE.js`（1 行 1 node の text）。
- 試験: test262（parse だけ）の runner と、多数の file を 1 process で読む driver。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0。
2. test262 の parse の試験（host plain・ASan、guest で同じ結果）。正例は全部 parse できる。
3. 前の試験が下がらない。boot test。

## 結果（2026-09-27）

cleared。

- 書いたもの（`userland/desktop/browser/js/`、約 6,400 行）:
  - `js.h`: node の種類（73）、flag、`struct js_node`（kind・op・flags・text・raw・number・4 つの子の list・next・位置・word）、
    `js_parse`・`js_program_release`・`js_dump`。
  - `internal.h`: lexer・parser・文脈の構造、単語の id（`enum js_word`、escape の無い keyword と decode した word を別に持つ）。
  - `lexer.c`（token）、`parser.c`（入口、ASI、node、失敗）、`parse_expr.c`（式、cover grammar、pattern への変換、関数・class・
    引数の検査）、`parse_stmt.c`（文、宣言、module の item）、`dump.c`（AST の text）。
  - `main.c`: `--dump=ast`、`--module`、`--strict`。
- 試験の道具: `plan/ws074/tests/host-parse.c`（manifest の各行 `PATH<TAB>MODE` を parse して 1 行ずつ結果）、
  `plan/ws074/tests/run-test262-parse.py`（frontmatter から mode を決め、negative の parse の試験は失敗を期待。`--record`・
  `--manifest-only`・`--results`）。除外: intl402・staging・fixture と、予定しない提案（decorators、import defer、source phase
  imports、explicit resource management）の 957 件。
- test262（`test262-7ab7fafa`）parse: **46876/47792（98.1%）**、正例 43441/43441、負例 3435/4351
  （`plan/ws074/results/test262-parse.txt`）。残る負例 916 件は parse では落とさない早期の誤り:
  - RegExp の pattern の早期の誤り（literals/regexp・property-escapes・RegExp/prototype、約 360 件）→ p027（RegExp）。
  - 字句の宣言の重複（block-scope・switch・for・function・if 等、約 200 件）→ p025 の scope 解析。
  - class の private name の規則（未宣言の `#x`、重複、`delete this.#x` 等、class の約 250 件の大半）→ p025。
- host plain・ASan（UBSan 込み）: 全 manifest で結果が同じ、sanitizer の報告なし（途中で見つけた use-after-free と
  longjmp の leak を直した）。
- guest（QEMU、plain）: test/language/expressions/ のうち 4320 file・8344 回の parse を host と比べて全部一致
  （guest の root の file system の inode が尽きたので 10704 file の一部。guest に tar が無く pax で展開）。
- 前の試験: host-base 2038・host-heap 31・host-object 98・host-interp 45（plain・ASan）、host-object・host-interp は guest でも
  全部通過。
- amd64 の build: warning 0。style-check: js/*.c の全部 0。boot test: PASS
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p024-20260927-boot-login.png`）。実機: 未実施。
- commit: `6647e5fd`（code・試験）、`ac102611`（style）と、この記録の commit。

## 後回し（follow-up）

- 上の 916 件の早期の誤り（p025 の scope 解析、p027 の RegExp）。
- 遅延の compile（関数の本体の範囲だけ残す）は起動の遅さが見えたら。
- fuzz（libFuzzer の JS parser）は最後の Phase。
