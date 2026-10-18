# 固定Interop2025 testharness全適格ID census / q03

2026-10-18。最大90分、実測895.6秒。固定binaryは9b94ebe38のp241出力。固定WPT/metadata/manifest/font・5秒settle・20秒processは[coverage](coverage.json)と各raw commandを参照。

1,455 unique IDを12 shard全件測定。selected/result/raw ID・outcome・順序・eligible SHA256を照合した。全shard exit0。**485 PASS / 910 FAIL / 16 harness_error / 20 no_completion / 24 no_output**。受信result callbackは10,995 PASS / 20,538 FAIL（未発火のsubtestを含む総数ではない）。[全結果](results.json)、[集計](summary.json)、[実行](runs.json)、各shardのreport/progress/raw gzipを保存。

比較可能な全ID snapshot q1202は481 PASS。今回4 IDがPASSへ進み、旧PASSの後退は0。4件はsummary.jsonに列挙。q235の485という歴史記載とは増分を主張せず、今回の全ID/rawを新しい基準値とする。補助aspect-ratio257は公式分母外であり加算しない。

1,783 testharness中、未対応328 IDはcoverage.jsonに理由付きで保持する。variant80、generated45、HTTPS44、worker/substitution33、driver/origin/worker126。全3,805 IDの100%は未達。reftest1,974/crash48の最新全体測定も別途必要。

共通原因: anchor/@scope群のcleanupでreplaceChildren未定義が後続caseを止めるためp242へ渡す。Grid used-track CSSOM、anchor/@scopeのCSS本体、navigation API未実装、非同期runner coverageが残る。測定は読取のみでbrowser/runner/WPT/ABIは変更していない。

q03-i01の測定部分cleared、p189 wholeはuncleared。Master/共有Queue/GitHubのprojectionはpending。ユーザーの自走指示により次の有限Queueを選定する。
