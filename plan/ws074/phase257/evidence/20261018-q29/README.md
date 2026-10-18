# q29: native synchronous generator delegation

source9da3cd223/final pairでnative同期yield*を通常compiler/VMへ実装。actual iterator/cached next、next初回undefinedの1引数、throw/return動的method取得、missing throwのnormal closeと例外優先順位、done/value順、done falseのvalue非観測とoriginal result同一性、normal/return/finallyを確認。managed recordとexplicit rootsでGC中のoperand/method/iterator/result/sentを保持し、ordinary yield/awaitは従来経路を維持。
独立Chrome/native38条件が全件一致、保守的stack無効のnative GC10、DOM41/41（既存promise/asyncを含む）、VM interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19を通過。host warning0・q02 exact5source target compile warning0、C全文new/changed scopeは許可forward cleanup25以外0。full target link/runtime/p100は未達。
固定1508を237.1秒、同じsettle5000/process20で実測し494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN。旧whole PASSと全PASS callbackの名前/数を保持、whole gain0。差はevent-timing/storage-accessのoriginal IDL setup2件のmessageだけで、yield*拒否/WebIDL2 undefinedからProxy未定義へ進んだ。原WebIDL2 bundleをloadしparse APIが実在することはnative/Chrome両方PASS、actual parse/validator/invalid IDL対照はChrome4/4・native1/4でProxyがblock。negative after-parserはChromeが意図assertまで到達、nativeはその前のProxy errorでありnegative-after-parser成立と主張しない。
Acid3 score/inventory100・pixel0、Acid2既存差873を再確認。static844/scripted105/strict crash39はq28の原証拠を再利用し、新pairでの全pixel/capture/crash再測定や同一性を主張しない。render sourceとrunner/manifest/WPT/fontは不変、native VM scopeだけ変更したことをsource差分/hashで保存。確認済み1482/3805（38.948752%）・held0を保持、未attempt275は分母に残す。
i01 scoped partial cleared、p257 own synchronous delegation criteriaをverifiedとしてcleared。async generator/for-await/全Test262適合は本Phase範囲外で未実装、Proxy/正規WebIDL2 parse完了はp189の次候補。whole p189/p255/p256/p100/target/WS100%は未達。全raw/元source/stdio/binary/hash/checkpointsをarchive sequential read-back済み。shared Master/Queue/next/cache/GitHub投影pending、push無し。

## Evidence

- summary.json / harness-outcome-audit.json: all1508、旧PASS/callback保持・差2件。
- final-protocol.json / generator-delegation.html/.expected: independent native/Chrome38。
- webidl-oracle-proof.json: original parser/validatorのactual残差を保持。
- native-runs.json / source-proof.json / engine-source-delta.json: immutable pair・全native実測・source境界。
- style-review.json / style-generated.log / target-commands-5.json: C全文、許可goto25・他0、target compileのみ。
- harness-all-raw.tar.gz: 1,676 members、原generated Git IDL/blob/providerを含む。
- native-oracle-and-build-raw.tar.gz: 114 members、official harness/WebIDL2原本、全native/Chrome stdio・build・Acid assetsを含む。

Primary algorithm: https://tc39.es/ecma262/multipage/ecmascript-language-functions-and-classes.html#sec-generator-function-definitions-runtime-semantics-evaluation 。HTML/JSの固定sourceを改変せずnative compilerからchecked bytecodeを生成。

Archive preparation correction: 初回はabsolute generated asset pathのrelative_toで失敗、fileを消さずpath.resolveへ揃えて再作成。verified archiveは全memberを順次read-backしhash照合、製品/runnerの変更ではない。host buildは全session終了を確認し逐次実行、final source/binaryをfreezeした後に測定した。
