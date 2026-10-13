<!-- awesome-plan project=zedbsd record=ws074-p181 -->

# ws074-p181: HTML `type` 属性 selector と hidden input の行高

Status: cleared（q1004-i01、2026-10-13 23:02 JST。HTML `type` 属性 selector 修正）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172/p179/p180 cleared、[q1003](../phase180/evidence/20261013-q1003/report.json) の同条件 layout/paint と差画像。
Queue: [q1004-i01](../queue.md) cleared。2026-10-13、このチャットでユーザーが下記最大90分の exact scope に「q1004 の範囲を承認する」と回答。

## 観測と設計の前提

Acid3 `test.html` は `document.write()` で `<input type=HIDDEN>` を form 内に生成する。UA stylesheet は `input[type=hidden] { display: none; }` と小文字値を使う。q1003 の test layout ではその input が `replaced 0×0` として form の高さ24の line に入り、後続の本文は y=373.50。reference の本文は y=350。[HTML Standard の selector case-sensitivity](https://html.spec.whatwg.org/multipage/semantics-other.html#case-sensitivity-of-selectors) は HTML 文書中の HTML 要素の `type` 属性値を ASCII 大文字小文字を区別せず照合する。CSS の明示的 `s` flag は大文字小文字を区別し、`i` flag は区別しない。[Selectors Level 4](https://drafts.csswg.org/selectors/#attribute-case)。現行 `css/cascade.c` は `simple->case_insensitive` が真の時だけ小文字化し、現行 `css/parser.c` は flag 無しと `s` を同じ値で保存する。`type=HIDDEN` が UA rule と一致しない直接原因候補。

## 目的・完了条件

HTML 文書の HTML 要素の `type` 属性セレクタが、flag 無しでは値の ASCII 大文字小文字を区別しないようにする。明示的 `s` は区別し、`i` は区別しない。XML/SVG、他属性の既存の比較は維持する。最小ページで `type=HIDDEN` の input が DOM/form collection には残り、layout/paint には出ず、後続段落を押し下げないことを確認する。

- 焦点を絞った selector/hidden-input host 試験、host build warning0、変更 C と browser component の全文規約確認。
- 固定 WPT・同一 runner/font の Acid3 score100/100、inventory100/100、例外0を維持し、差 **42921/480000** を増やさない。Acid2差 **1865/120000** を増やさない。form line と本文 y の前後を測る。改善しない、または他 module の大きな設計変更が必要なら uncleared。
- p100 の全画面差0は別条件。QEMU/別OS、main 現行 HEAD 全体の再buildは実施時に記録。

## q1004-i01 exact scope 案（最大90分）

`css/parser.c`・`css/internal.h`・`css/cascade.c` の属性 selector flag 表現と HTML `type` 値比較を最小ページで切り分ける。原因が確認できた場合のみ、flag 無しの HTML `type` 値を ASCII case-insensitive、明示的 `s`/`i` を各規則どおりにし、XML/SVG/他属性を変えない一般的な最小修正を行う。DOM/form collection は保持。host 試験と warning0 build、固定 Acid3/Acid2 再計測を保存する。WPT 本文・参照画像・runner 比較方式は変更しない。

原因が異なる、範囲外の仕様実装が必要、差が増える、または90分に達した場合は attempt を uncleared として原因・証拠・再開条件を残す。HTML Standard が列挙する `type` 以外の属性値の包括対応は今回の Queue に含めず、必要なら別の Phase で扱う。実装にはユーザーの exact-scope 承認と Queue active 化が必要。

2026-10-13 q1003 後: iframe `FAIL` 2 item を除いても本文 y=373.50 は不変。`type=HIDDEN` の UA selector 不一致と form line 24px を新しい原因候補として分離。p180 cleared と p100 全画面条件は変えない。

## q1004-i01 結果（2026-10-13 22:58〜23:02 JST）

**cleared**。`css/internal.h`・`css/parser.c` で属性 selector の flag 無しと明示的 `s` を区別し、`css/cascade.c` で HTML 文書の HTML 要素の `type` 値を flag 無しなら ASCII 大文字小文字を区別せず比較する。`s` は区別、`i` は区別しない。XML/SVG と他属性の既存比較は維持。`type` の `=`, `~=`, `|=`, `^=`, `$=`, `*=` を最小 fixture で確認した。

- 新規 `host-attribute-type-case.c` は修正前 FAIL、修正後 PASS。`type=HIDDEN` の input は DOM に残り、UA `display:none` により layout から消える。通常の HTML type・明示的 `s`/`i`・他属性・SVG の selector を独立の印で確認。Acid3 の form collection を含む100項目も pass。
- `sh plan/ws074/tests/host-build.sh plain` は warning/error0。`host-css-rule-model` 44/44、`host-iframe-fallback` pass、`git diff --check` pass。`clang-format` は環境に無く、変更 C と browser component の全文規約を手動確認。QEMU/別OSと main 最新 HEAD 全体の再buildは未実施。
- 固定 WPT の最終 `run-acid-tests.py --out build/ws074-acid-q1004-final`: Acid3 score100/100、inventory100/100、例外0、全画面差 **18596/480000**（91.0581%→96.1258%一致、差42921→18596で24325減）。Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1004/report.json)、[Acid3並列画像](evidence/20261013-q1004/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1004/acid2-side.png)、前後 layout/paint を保存。
- Acid3 test の form line は24→0、後続本文 y=373.50→349.50。reference 本文 y=350 との0.5差と他領域の残差は残る。p100 の全画面差0、Acid2 の現行 exact は未達。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main の作業 tree へ投影。GitHub Issue/Project publication と push は設定どおり保留。`master.md` は変更しない。
