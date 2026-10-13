<!-- awesome-plan project=zedbsd record=ws074-p187 -->

# ws074-p187: 負の padding 宣言と Acid3 第6色箱

Status: cleared（q1010-i01、2026-10-13 23:38 JST）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172〜p186 cleared、[q1009](../phase186/evidence/20261013-q1009/report.json) の差画像・style/paint。
Queue: [q1010-i01](../../history/queue-q1010.md) cleared。ユーザーの最大90分 Queue 自動反復指示に従い、下記 exact scope を記録して選定。

## 観測と目的

q1009 差1701画素のうち900画素は x593〜603/y137〜218、紫の第6箱の右端10px。Acid3 CSS は `#bucket6 {padding-right:-10px}` を指定するが、負の padding は CSS で無効。現行 computed style は padding-right=-10px、参照では先行宣言の0pxが保たれるため紫の箱が10px広い。`css/values.c` の padding 長さ解析は負値を受理する。

## q1010-i01 exact scope / clearance（最大90分）

padding longhand、4辺 shorthand、inline/block shorthand の直接負値・負 percentage を、宣言単位で EINVAL とし、先行の有効宣言を保つ最小修正。margin の負値は保持し、calc() の range 処理、他の寸法 property は変更しない。局所 host fixture で直接負値と同じ stylesheet の正値、margin、shorthand 全体 fallback を確認。warning0 build、変更 C/browser component 全文規約、固定 Acid3/Acid2 再計測。WPT/runner/font/参照画像は変えない。

Acid3 score100/100、inventory100/100、例外0、差 **1701/480000** 非増加、Acid2差 **1865/120000** 非増加が条件。範囲外変更、差増加または90分到達時は uncleared、証拠と再開条件を記録。p100全画面差0は別条件。

## q1010-i01 結果（2026-10-13 23:35〜23:38 JST）

**cleared**。`css/values.c` の padding longhand 値を非負に限定した。4辺/inline/block shorthand は各 component の解析が EINVAL を返すと宣言全体が無効になり、先行の有効値が残る。margin の負値と正の padding を維持。calc() の range 処理は変更していない。

- `run-negative-padding-tests.py` は修正前 FAIL、修正後 PASS。右辺・4辺・inline/block の負値を無効化し、先行値、負 margin、正の padding/percentage を確認。`host-css-rule-model` 44/44、host build warning/error0、`git diff --check` PASS。`clang-format` は環境に無く、変更 C と browser component 全文規約を手動確認。QEMU/別OS、main 最新 HEAD 全体の再buildは未実施。
- 固定 WPT `run-acid-tests.py --out build/ws074-acid-q1010-final`: Acid3 score100/100・inventory100/100・例外0、差 **801/480000**（99.833125%一致、q1009の1701から900減）。Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1010/report.json)、[Acid3並列画像](evidence/20261013-q1010/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1010/acid2-side.png)、前後 style/paint を保存。紫箱右端は参照と一致。
- 残る801画素は左上の `#linktest` 文字に限定。p100全画面差0とWS074受入は未達。次 Queue で :visited と履歴状態/描画 privacy を検討する。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
