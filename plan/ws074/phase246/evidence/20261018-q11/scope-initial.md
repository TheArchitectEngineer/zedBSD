<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-11 / intrinsic Grid track breadth

Status: active（2026-10-18 12:26 JST）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)を次の有限scopeへ適用。
Timebox: 最大90分（13:56 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-11-i01 | [p246](phase246/phase.md) first horizontal partial | private maximum ruleをparse/cascadeで保持、computed fallbackのintrinsic/minmaxを正しくserialize。horizontal plain min-content columnの非置換single-span contributionとintrinsic track非stretchを実sizingへ反映。独立Chromium比較、q10 frozen baselineと公式Grid502/全callbackで後退0、static529の旧227 PASS維持、DOM/CSSOM、host/target warning0、C全文/format、Acid3差0/Acid2非増加。line names/fit-content/general span/row/vertical sizingは残り、公開ABI/WPT/Master不変 | in-progress | q09 minimum rule、q10 frozen output、既存intrinsic測定 |

Dependency graph: q09 minimum rule + q10 verified output → p246 first partial。
Upcoming Work Outlook: general intrinsic/span/row sizing、line names/child/vertical copy、runner coverage。

