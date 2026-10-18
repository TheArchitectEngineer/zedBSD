<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-45 / realm-aware prototype-chain consumers

Status: finished（i01 chain consumers scoped partial cleared）
Owner: このchat単独。Approval: [自走指示](autonomy.md)を次のexact finite partialへ適用。q44 terminal cfd15d78e/source19b9b9ed6/history SHA2569810719d4e6f7726dbd3d1457cbcbe7d13fc2f8abf643189b9e8670034ba1cd0確認。
Timebox: 最大90分、2026-10-18 22:11:21→23:41:21 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-45-i01 | [p258](phase258/phase.md) chain consumers partial | object_is_prototype_of/object_lookup_half/vm_ordinary_has_instanceのprototype直接参照をrealm GetPrototypeOf、lookupのheap GetOwnをrealm GetOwnへ接続。ToObject/ToKey順・nearest property停止・trap/descriptor/prototype順・null/primitive/abrupt identity保持。managed realm/constructor/boxed target/current walk/fresh key/prototypeをcallback/GC中保持。old552 prefix保持＋新genuine controls/実GC/errno、full changed C/host warning0/q02 exact2 source target warning0/focused/Acid/元WebIDL保持。 | cleared | q42 GetPrototypeOf/q33 GetOwn、q44 callers証拠、native VM call保持契約 |

Generation bounds: js/builtin_object.cの上記2functionsとhelper宣言配置、vm/access.cのvm_ordinary_has_instanceだけ。host-proxy-read.c/proxy-read.jsに必要なcontrols/GC対照、WS074 records。public ABI2/native_operations定義/comment、WPT/runner/manifest/期待値、global Proxy/Reflect/callable・nested factory bounds、他VM functions/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: actual internal methodsを順次呼び、戻りprototypeを次回script前にcurrent-rootへ移す。lookupはown descriptor発見時dataならundefined/accessorなら指定halfを返しprototypeを読まない。isPrototypeOfはargument primitiveならreceiver ToObject無しfalse。OrdinaryHasInstanceはconstructor callable→value object→constructor prototype Get/validation→各GetPrototypeOf→SameValue。BoundFunction特殊経路は既存別未達で本partialでは修正しない。root登録は失敗数を追跡し単一forward cleanupで全exit退役。
Finite investigation: 90分内、各native/oracle process30秒、focused300秒。無限に同じprototypeを返すtrapのlanguage algorithmは非停止し得るので新しいidentity-cycle shortcutや任意chain長制限を追加しない。有限回同じobjectを返してから終わるtrapを検証する。general VM interruption/resource policyは本partial外。
Standards: Guardrail/C全文/browser-component全文直接適用、簡約なし。ANSI declarations first/semantic paragraphs/multiline fn、normal helper宣言は全table後、table initializer必要分のみ前。clang-format19 ColumnLimit0後headers/compound clauses復元、全変更function manualとchecker（許可単一forward cleanup以外0）。host serialized終了確認、q02 targetliteral -oだけ変更、versions取得、raw全member read-back。
Verification: old552(native552/Chrome550既知huge2)保持、新条件genuine対照。q36負zero classifierfilter/q43 huge-length2外部差を維持、all pure equalityと呼ばない。全1508はq44最新全callback/completion同値証拠を再利用し、本pair全census/images/crashは未実測と明記（new gainsを主張しない）。公式1482/3805・held0・未attempt275保持。元WebIDL4+intentionalnegative、validator require-exposed2+negative保持。必要なfocusedのみ実行。
Explicit residual: vm_instanceof outer dispatch/bound functions/callableProxy、for-in/spread/JSON/ordinary Set exotic prototype/other invariant realm化/nested/call/construct/global/immutableProto/typed backing/p100/full targetは未達。whole p258/WSはclearしない。共有投影pending/no agents/push/削除/make check/QEMU。

Dependency graph: q44 verified consumers + q42 GetPrototypeOf + q33 GetOwn → i01。外部nodesは今回の実装許可ではない。
Outlook（非承認）: ordinary Set exotic prototype routing、for-in、other invariant realm化→nested、call/construct/global constructor/revocable。

## Outcome

q45 chain consumers scoped partial cleared。selection WIP827443d9a/source WIPdbcb42104。object_is_prototype_of/object_lookup_half/vm_ordinary_has_instanceの3関数だけをactual GetPrototypeOf/lookup GetOwnへ接続。nearest data shadowing/アクセサ未実行/ToObject→ToKey順/primitive-before-receiver/null/SameValue/例外identityを保持。fresh current link/boxed target/key/constructor prototype/managed direct callerをcallback中にroot、単一forward cleanupで退役。normal lookup helper宣言は全table後、initializer必要wrapper例外維持。native_operations定義/comment/ABI2/Wayland/global Proxy/Reflect/factory callable・nested拒否/WPT・runner・期待値/共有Boards/cache/SDK/toolchain/sysroot不変。
old552 prefix同一、new33 conditions native/genuine Chrome全一致。frozen q44 precodeは564/585で新21条件FAIL（21別bug/公式gainとは主張しない）。最終native585/Chrome583、差はq43既知huge2のみ、q36 negative-zero classifier filter継承で全pure genuine equalityではない。finite repeated identity→後続prototypeを検証し任意cycle shortcut/chain長limitを導入しない。general native nontermination/resource policyは別未達。
既存4 consumer GC/error modesにactual __lookupGetter__/__lookupSetter__のnative GetOwn negative EACCES+GC 2 modes追加、vm_call経由managed caller/input保持と実回収。direct OrdinaryHasInstanceはembedding rootsも外側VMcallも無し、bare managed caller/native callable constructor/valueをconstructor-side pure GetOwn callback実GC中保持し、EACCES伝播後3participant全実回収。新JS controlsでfresh intermediate Proxy/Symbol/keyと後続trap collectも検証。旧forced-GC版とのdifferentialは未測定でold GC failure修復とは主張しない。
full changed6 C functions manual＋clang-format19/checked styleは許可forward cleanup77・other0。host warning0、q02 literal builtin_object/access2 source -oだけ変更target warning0、versions実取得。focused proxy/delegation10/interp45/factory56/DOM41/CSSOM12+12/marks7/observer10/ErrorEvent9/ErrorReporting19全0。Acid初回sandbox socket PermissionError保持、same frozen binaryでAcidだけescalated再実行exit0・Acid3 score/inventory100/pixel0、Acid2既存873。元WebIDL4positive＋intentionalnegative source/callback q38同一、original validator require-exposed2と意図的FAIL callback native/Chrome同一。グローバルProxy非公開。
固定1508最新全実測はq44（q37全whole/callback/completion同一、494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN）。q45pair全census未測定、new公式gainを主張しない。static844/scripted105/crash39最新q28証拠再利用、newpair全image/crash capture未測定。確認済公式1482/3805（38.948752%）、held0・未attempt275維持。
whole p258/WS/p100/full target/100%未達。outer vm_instanceof/BoundFunction/callableProxy、for-in/spread/JSON、ordinary Set exotic prototype、other invariant realm化/nested/call/construct/global constructor/revocable、array IsArray callers/immutable prototype/typed backing等残件維持。共有Master/Queue/cache/GitHub投影pending Q1、push無し。
Raw archive87 members SHA2568313f6c311a137fd6c4bcf5d2072d4d7f048a5f37e8e8134f89dd1c8e002a4e5全member read-back。runtime binaryはhash付きbuild保持・archive非同梱。q44 harness1676 member原archive SHA2567ba8f2ca5aa17ef5235bed2ac29e49034a1b2bcf2e1b153e03e9b6bb85ffde94を参照、q45実測と混同しない。

[exact history](history/ws074-q20261018-45.md)。
