# q30: managed native Proxy read foundation

final source d9e9aa941/frozen pairでmanaged native Proxy read foundationを実装。realm-aware vm_get_receiverがactual receiverをprototype/Proxy fallbackへ保持し、通常vm_getはbaseをreceiverとする既存動作を保つ。managed recordのtarget/handler trace、idempotent revoke、GetMethod/handler this/3引数・string/symbol key、fixed data/accessor invariantとSameValue、trap/getter中revoke後の元target保持を実装。noscriptのvm_native_operations定義はq29と全bytes同一、script trapはaccess層だけで実行する。
同じ32 control sourceがnative/Chrome genuine Proxy/Reflectで全件一致。nativeは保守的C stack保持を無効にし、real threshold collection前にcaller rootsを落としたfactory入力/managed realm保持、foreign trap後のscript-free descriptor GC中のsole returned objectとmanaged realm生存、処理後の実realm回収を確認。Proxyだけのrootでtarget/handlerを保持しrevoke後は両者回収、最後のProxy root解放後はProxy自体を回収。循環native fallbackはRangeErrorで有界停止し、全normal/abrupt終了後property_depth=0。global Proxyは未公開のまま、private factoryのcallable/Proxy target拒否も実検証。
final pairでDOM41/41、interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19、generator delegation10を通過。host final warning0・q02 exact object/access/realm3source target compile warning0、C全文new/changed scopeは許可forward cleanup45以外0。初期fixtureのprivate header include漏れと誤ったAPI名vm_to_property_keyはactual vm_to_keyへ修正、元build error logsを保持し、最終warning0と区別する。
全1508 harnessを238.2秒、同じsettle5000/process20で実測し、q29に対するwhole判定・全callback object・completion objectが完全同一。494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN、旧PASS後退/新whole gain/差0。原WebIDL2のProxy未定義はglobal未公開なので解消したとしない。
Acid3 score/inventory100・pixel0、Acid2既存差873を再確認。static844/scripted105/strict crash39はq28の原証拠を再利用し、新pairでの全capture/strict equalityは未測定。source deltaはVM5 C/headerだけ、render/runner/WPT/manifest/font不変とsource/pair hashesを照合。確認済1482/3805（38.948752%）・held0を維持、未attempt275を分母へ保持。
i01 selected read foundation partialだけcleared、whole p258はwrite/descriptor/ownkeys/prototype/extent/has/delete/IsArray/call/construct・global constructor/revocable/actual WebIDL2未達でuncleared。次finite Queueで必要なinternal methodを選定して継続する。whole p189/p255/p256/p100/full target link/runtime/WS100%未達。全native/oracle/build/Acid/census rawをarchive全member sequential read-back済み。共有Master/Queue/next/cache/GitHub投影pending、push無し。

## Evidence / limits

- summary.json / harness-outcome-audit.json: 全1508・q29 outcome/callback/completion完全同値、各ID/checkpoint/raw保存。
- final-protocol.json / proxy-read.js: shared original bytesの32 native/Chromium対照。Chromiumはgenuine Proxy.revocable/Reflect.get、collector helperだけinert。GC ownership自体はnative C fixtureで別に実検証。
- host-proxy-read.c / native-runs.json: actual descriptor callback collection、sole output/managed realm/retention/revocation/reclamationとglobal非公開・private target limitsを検証。
- source-proof.json / engine-source-delta.json / source-commit.txt: immutable pairとVM5 sourceだけの変更。
- style-review.json / style-generated.log: C全文の変更範囲、許可cleanup45・他0。legacy全sourceはp100に残す。
- target-commands-3.json: q02 exact compiler/flagsの-oのみ変更、full target未実施。
- harness-all-raw.tar.gz / native-oracle-and-build-raw.tar.gz: summary.json記載hash/member数を全順次read-back、原JS/stdio/bin/Chrome command/旧fixture errorsを含む。

Primary: https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-get-p-receiver 。これはunexposed private VM stageでありglobal Proxy実装完了/IDL parse成功を主張しない。
