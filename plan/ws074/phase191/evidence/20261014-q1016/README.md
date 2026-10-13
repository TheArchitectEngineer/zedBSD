# q1016-i01: CSS.supports の宣言判定

2026-10-14 00:11–00:14 JST。ユーザーの Interop 2025 100% までの有限Queue事前承認に基づく最大90分の部分試行。[p191 scope](../../phase.md)。browserの `CSS` global と `supports(property, value)` を追加し、インラインstyleと同じ既存宣言parserで判定する。単一引数の基本的な `(property: value)` も扱い、複合条件は現段階では `false`。WPT本体・期待は変更していない。

[host fixture](css-supports.html) は `CSS` object、`display:block` のtrue、無効値のfalse、基本的な括弧付き条件true、未対応の複合条件falseを[実測](host-output.txt)。`sh plan/ws074/tests/host-build.sh plain` warning/error0、`host-cssom` 12/12、`host-css-rule-model` 44/44、`git diff --check` PASS。`clang-format` は環境に無いため適用できず、[C全文](../../../../coding-style.md)と[browser component全文](../../../../standards/browser-component.md)を手動確認。

固定WPT textdecoration 16件を最終source buildで再計測。[report.json](report.json) と `test-01.json`–`test-16.json` を参照。**4 PASS / 12 FAIL、subtest47 PASS /132 FAIL**で q1015 と数は同じ。`text-decoration` と `text-decoration-line` computed系での `CSS is not defined` は消え、値が空という次の実装不足に進んだ。これは結果取得と原因の前進だが、PASS数改善とは数えない。WPTの複合 `supports` 条件や他のCSSOM APIはこの部分scopeでは未対応。

[Acid回帰](acid-summary.json) は固定WPT、800×600条件でAcid3 score100/100、inventory100/100、RGB差0/480000を維持。Acid2差1865/120000も前回と同じ。`master.md`・公開ABI・WPT本体は無変更。p191 wholeの全16 PASS、p189/p190 runner coverage、p173全3,805件PASS 100%は未達。
