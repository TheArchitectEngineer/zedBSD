<!-- awesome-plan project=zedbsd record=ws074-p258 -->

# ws074-p258: native Proxy internal methods and lifetime

Status: uncleared（q32 shared descriptor conversion partial cleared、whole Proxy未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-32-i01（q30/q31-i01 cleared履歴保持）
Origin: p189/q29 original WebIDL2 parseがProxy未定義で停止。

Objective / own criteria: native managed Proxyを実object internal methodsへ接続し、constructor/revocableと各internal operationのtrap/fallback/invariant/receiver/revocation/callability/GCを正しく実行する。original WebIDL2 actual parse/validatorに必要なget/set/array behaviorを通常APIで提供し、independent controls/原WPTを通す。部分read stageだけでwholeをclearしない。
Design: vm_native_operationsはscript-free heap interfaceのまま保持。Proxyにはmanaged target/handler recordを持たせ、scriptを実行可能なrealm-aware access/internal methodからtrapを呼ぶ。opaque JS constructor公開前に必要なoperationを段階的に整える。各段階は自走承認に基づく新finite Queueで正確に選定する。
Current scope: [q32 exact Queue](../queue.md)のVM共有To/From/CompletePropertyDescriptorと既存Object helper接続。q30/q31のread/Has/native lifetimeをverified outputとして保持し、未選定Proxy descriptor internal methods/write/keys/prototype/extent/IsArray/call/construct/global公開をwhole受入へ残す。
Standards: Guardrail/C全文/browser component全文、簡約版無し。一次 https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-get-p-receiver 。既存p100がWS全source near-final conformanceを所有し、本partialはnew/changed scopeの全文適合を検証。
Verification/bounds/affected components: q30 snapshotの通り。private object kind/cell/newborn/trace/revokeとactual receiver/trap/invariant/GCをnative/Chromeで確認し、ordinary whole/callback PASSを守る。target single-source compileのみとfull target runtimeを区別。未達はunclearedとresume。

Event ws074-p258-q30-selected-20261018: q29 verified delegationの後、正規WebIDL2がProxyを要求することを原source/Chrome actual4/4・native1/4で確認。既存heap native hooksはuser script禁止なので、その契約を保つrealm-aware Get foundationを選定。partial source/scope/90分/gatesを実装前に保存。global Proxy/whole parserを先取り公開・認定しない。共有Master/next/Queue/Project/GitHubの投影pending。

## q30 partial result / resume

Event ws074-p258-q30-partial-cleared-20261018: final source d9e9aa941/frozen pairでmanaged native Proxy read foundationを実装。realm-aware vm_get_receiverがactual receiverをprototype/Proxy fallbackへ保持し、通常vm_getはbaseをreceiverとする既存動作を保つ。managed recordのtarget/handler trace、idempotent revoke、GetMethod/handler this/3引数・string/symbol key、fixed data/accessor invariantとSameValue、trap/getter中revoke後の元target保持を実装。noscriptのvm_native_operations定義はq29と全bytes同一、script trapはaccess層だけで実行する。
同じ32 control sourceがnative/Chrome genuine Proxy/Reflectで全件一致。nativeは保守的C stack保持を無効にし、real threshold collection前にcaller rootsを落としたfactory入力/managed realm保持、foreign trap後のscript-free descriptor GC中のsole returned objectとmanaged realm生存、処理後の実realm回収を確認。Proxyだけのrootでtarget/handlerを保持しrevoke後は両者回収、最後のProxy root解放後はProxy自体を回収。循環native fallbackはRangeErrorで有界停止し、全normal/abrupt終了後property_depth=0。global Proxyは未公開のまま、private factoryのcallable/Proxy target拒否も実検証。
final pairでDOM41/41、interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19、generator delegation10を通過。host final warning0・q02 exact object/access/realm3source target compile warning0、C全文new/changed scopeは許可forward cleanup45以外0。初期fixtureのprivate header include漏れと誤ったAPI名vm_to_property_keyはactual vm_to_keyへ修正、元build error logsを保持し、最終warning0と区別する。
全1508 harnessを238.2秒、同じsettle5000/process20で実測し、q29に対するwhole判定・全callback object・completion objectが完全同一。494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN、旧PASS後退/新whole gain/差0。原WebIDL2のProxy未定義はglobal未公開なので解消したとしない。
Acid3 score/inventory100・pixel0、Acid2既存差873を再確認。static844/scripted105/strict crash39はq28の原証拠を再利用し、新pairでの全capture/strict equalityは未測定。source deltaはVM5 C/headerだけ、render/runner/WPT/manifest/font不変とsource/pair hashesを照合。確認済1482/3805（38.948752%）・held0を維持、未attempt275を分母へ保持。
i01 selected read foundation partialだけcleared、whole p258はwrite/descriptor/ownkeys/prototype/extent/has/delete/IsArray/call/construct・global constructor/revocable/actual WebIDL2未達でuncleared。次finite Queueで必要なinternal methodを選定して継続する。whole p189/p255/p256/p100/full target link/runtime/WS100%未達。全native/oracle/build/Acid/census rawをarchive全member sequential read-back済み。共有Master/Queue/next/cache/GitHub投影pending、push無し。

[Full evidence](evidence/20261018-q30/README.md)。次scopeの開始前に最新source/required methods/原WebIDL2とexact Queueを再確認する。global公開は後続readinessの別scope、whole acceptanceをread-only出力だけでclearしない。

Event ws074-p258-q31-internal-design-selected-20261018: Setのfallbackにはdescriptor処理が必要で、さらにToPropertyDescriptorはHasPropertyを使うため、未選定の内部順をHas→descriptor→Setへ具体化した。これはこのPhase内の外部契約不変の設計具体化で、q30承認scope/historyを変更しない。既存Object.definePropertyではinvalid getterの後にsetter getterを読む差をprecode native/Chromeで実証（native get,set + setterのthrow、Chrome getだけ + TypeError）。[q31 Queue exact scope](../queue.md)でHasと即時getter検証だけをmax90分選定し、actual native/core pathから対処する。write/descriptor internal methods/global公開は後続scopeに残し、whole p258はまだ達成しない。共有投影pending。

Event ws074-p258-q31-own-helper-declaration-review-20261018: final C全文§2 reviewで変更対象object_to_descriptorの既存forward宣言がconstant tablesより前にあることを確認。q31の同関数scope内で全file-scope tables後の通常blockへ移す。宣言と追加separator以外の全bytesと宣言multisetを保存し、host/target exact objectのdebug除去全bytes同一を証明して、immutable3f9 pairの測定を意味不変のfinal sourceへ対応付ける。approval criteria/timebox/API不変、whole p258/p100未達。

## q31 partial result / resume

Event ws074-p258-q31-partial-cleared-20261018: native HasPropertyをrealm-aware VM pathへ追加し、通常inとprototype上Proxyを接続。actual target/string-or-symbol key2引数・handler this、GetMethod/null fallback/ToBoolean、非configurable own/nonextensible ownをfalseで隠さないinvariant、trap/getter中revoke後の元target/handler保持、GCとbounded property depthを実装。script-free heap hooksは不変。ToPropertyDescriptorのgetterは取得直後に検証し、setterのHas/Getを観測する前にinvalid getterのTypeErrorを返す。enum/config/value/writable/get/set順と元payload/input rootを保つ。
old read32 control source prefixをbyte同一に保ち、Has/descriptorを加えた同じ68条件がnative/Chrome genuine Proxy/Reflectで全件一致。nativeはC stackの保守的保持を無効にし、元Getに加えてHasでもscript-free descriptor callback内にmanaged realmを実GC、両方でowner生存・処理後の実回収を確認。fresh value/getterを後続descriptor getterのGCから保持、revoke/retention/ctor threshold pressure/sole returned objectとprivate target bounds・global Proxy非公開を維持。fixtureはfrozen control input pathをCLIで指定でき、pair内へ同じJS bytesを保存した。
公開Object.definePropertyのprecodeはq30 nativeがget,setを観測してsetterのthrow、Chromeがgetだけを観測してTypeError。q31 nativeも同じ元HTMLでgetだけ/TypeErrorへ一致し、この実bugを修正した。原HTML/旧native/Chrome/final nativeの全stdio・command/hashを保存。全1508 harnessは同じsettle5000/process20で237.4秒、q30 whole判定・全callback/completion objectが完全同一。494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN、旧PASS後退/whole gain/差0。
final measured pairのsourceは3f9f3921d、規約follow-up sourceは2a0aa616d。変更対象object_to_descriptorの既存forward宣言を全tables後へ移し、宣言とseparator以外の全bytesを保持。host/target exact commandでfinal sourceを再compile warning0、両方のdebug除去object全bytesがbefore/after一致した。元frozen source-proof・preserved sourceとfinal-source-proofを分け、この等価性で既存測定をfinal semantic sourceへ対応付ける。literalに再link済みのpairと偽らず、不要なfull corpus再測定を増やさない。
DOM41/41、interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19、generator delegation10、host warning0/target access・builtin_object exact2 source warning0を確認。C全文new/changed scopeは許可forward cleanup55以外0、own modified helper宣言も正規位置。未変更legacy全体の規約はp100に残す。Acid3 score/inventory100・pixel0、Acid2既存差873維持。static844/scripted105/strict39はq28原証拠を再利用、新pair全capture/strict equalityは未測定。確認済1482/3805（38.948752%）・held0/未attempt275を保持。
i01 Has/descriptor prerequisite partialだけcleared。whole p258はrealm-aware getOwn/Define、FromPropertyDescriptor partial field、Set/keys/prototype/extent/IsArray/call/construct・global constructor/revocable/actual original WebIDL2未達でuncleared。元q30 read partialを維持し次finite scopeへ継続、whole p189/p255/p256/p100/full target/WS100%未達。raw/archiveを全member sequential read-back済み。共有Master/Queue/next/cache/GitHub投影pending、push無し。

[Full evidence](evidence/20261018-q31/README.md)。次は必要なdescriptor internal methodsを新finite Queueで具体化し、partial FromPropertyDescriptorのpresenceとshared conversionの層/GCを確認する。global APIと全Proxy受入は未達に保つ。

Event ws074-p258-q32-selected-20261018: Proxy GetOwn/Defineから標準descriptor処理を共有するため、q31でverifiedのTo順をVM-owned abstract operationへ移し、Fromのpartial field presenceとCompleteのdefaultsを提供する同一Phase内設計を具体化。既存Objectのcomplete descriptor出力を維持しprivate partialはnative/genuine Chromeで確認する。これはVM→JS依存を増やさず既存layerのVM側へ言語処理を集約する。exact source/criteria/max90分とfocused gatesはq32 Queue、全1508再実行・global公開を先取りしない。共有投影pending。

## q32 partial result / resume

Event ws074-p258-q32-partial-cleared-20261018: VM-owned ToPropertyDescriptor/FromPropertyDescriptor/CompletePropertyDescriptorを既存access層へ追加し、JS Objectの既存to/fromを薄いwrapperで共有した。ToのHas/Get6順とget即時callability/元errorを保持。Fromはpresent fieldsだけをvalue/writable/get/set/enumerable/configurable順にordinary data propertiesへ出し、NULL→undefined。Completeはvalid partialのdata/accessor/defaultsを無script/無allocationで補う。VMからJS moduleへの依存無し、native_operationsは全bytes不変。
同じ93条件がnativeとChromium genuine Proxy define trap/通常Object APIで全件一致。old68 source prefixはbyte同一。partial snapshotとcomplete snapshotを独立対照し、native Cでは保守的C stack保持を無効にしてfresh value/getterの両modeにreal threshold GCを発生させ、caller setup roots無しでmanaged realmとpayloadの生存を確認。flags無しpayload2 cellはそのGCで実回収され、出力が元payloadとdefault data属性の1 fieldだけを持つこと、output root解放後にrealm/payload/outputが全て実回収されることを確認。q30/q31のGet/Has/revoke/sole result/実realm回収対照も維持した。
source WIP786856b04、最終sourceを再buildしたimmutable pair/source hash317件を照合。host warning0、q02 exact access/builtin_object2 source target compile warning0。full new/changed Cのraw checker許可forward shared cleanup50・他0、通常helper宣言は全tables後、public fn headerはmultiline。DOM41、interpreter45/factory56、CSSOM12+12、marks7/observer10/ErrorEvent9/ErrorReporting19/delegation10を通過し、公開Object.definePropertyのinvalid getter順は元Chromeのgetだけ/TypeErrorと一致。Acid3 score/inventory100・pixel0、Acid2既存差873。初回Acidはsandbox localhost socket不足でPermissionError、原logを保存して同じcommandを通常network権限で再実行exit0、意味上の失敗と区別。
今回はexact承認どおりfocused gatesだけ。全1508はq31、static/scripted/strict crashはq28の原証拠を再利用し、q32 pair全件同値/新whole PASS増分を主張しない。確認済1482/3805（38.948752%）・held0・未attempt275を保持。archive55 membersを順次全read-back、sha2563eda8931e35dce0f676f860cc7d6b1bfaefdb37731e7620ccb38a2f970fa7e4c。
i01 shared descriptor conversion partialのみcleared、whole p258はrealm-aware Proxy getOwn/Define/Set/keys/prototype/extent/IsArray/call/construct/global constructor/revocable/原WebIDL2未達でuncleared。full target/whole-source p100/WS100%未達、共有Master/Queue/next/cache/GitHub投影pending、push無し。

[証拠](evidence/20261018-q32/README.md)。次finite Queueでrealm-aware getOwn/Defineとordinary no-script outputを検証してからSetへ接続する。global Proxyは非公開を維持。
