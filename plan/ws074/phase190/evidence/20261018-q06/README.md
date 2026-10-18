# q06 固定Interop2025 静的reftest全件census

2026-10-18。最大90分、同sessionの追加比較も同じtimebox内。固定WPT/metadata/manifest、800×600/font、render20秒、d328827f5の固定browser/library。binary/font hashはsummary.json。

全1,974 reftest中、静的候補1,561 unique IDを全件測定し、**836 PASS / 725 FAIL / 0 error / 0 timeout**。selected/result/checkpoint/全ID集合、未対応413の集合とmanifestを照合。script402、scripted reference5、variant6は未対応で分母に保持。sparseから欠けたquirks参照2件を同じ固定commitの原本から復元し、入力をテスト結果へ合わせて編集していない。

比較できる全ID snapshot q1062は662 PASS/899 FAIL。今回192 FAIL→PASS、18 PASS→FAIL、644 PASS維持、707 FAIL維持。これはq1062以降の全変更の差であり、今回のsessionの改善量とはしない。q1202で既知だった4後退も今回すべてFAILのまま。

今回のp240/p241等による後退を分けるため、current nonPASS全725件をsession開始前に保存したbrowser/libraryで比較した。まず過去PASSの18件、残る707件を別の有限runで測り、**全725件が開始前もFAIL、今回sessionのPASS→nonPASSは0**。開始前のPASS全件を再測定したという意味ではなく、現行の全FAILをcounterfactualで照合した。今回のPASS増分はこの比較だけでは数えない。

失敗群はFlex242、Grid304、writing-modes85、anchor21、zoom21、backdrop20、shape16等（重複なしdirectory集計はsummary）。次候補p243はq03のGrid CSSOMのfr/auto/none返却を実solver snapshotから補う計画。画像のFAIL本体は引き続き別の修正が必要。

crashtest48は今回未更新。q1040には48 local画像生成PASSがあるが、そのraw stderr中7件にUncaughtがある。公式のcrashtestはload/paintとtest-wait解除を確認する（https://web-platform-tests.org/writing-tests/crashtest.html）。JS例外だけを独自にFAILと決めるのではなく、旧runnerが待機条件を保証していない点をp190の残りへ記録し、現段階で全3,805達成の証拠に加えない。歴史の48 local PASSは改ざんしない。

既存reftest runnerのTemporaryDirectoryは自動cleanupを行う。userのQ1 cleanup方針へ合わせるため、次の検証を始める前にretainへ修正する必要がある。今回のreport/progressと代表20 FAILの画像はdurableに保存済み。消えたowned temp画像を証拠として参照しない。

q06の診断/測定部分cleared、p190 whole uncleared、全3,805件100%未達。browser/runner/WPT source/ABI/Masterは変更なし、コード無変更につきbuild/Acidの再実行は省略。直前q05でhost/target compile warning0、Acid3差0を検証済み。Master/共有Queue/GitHub projectionはpending。
