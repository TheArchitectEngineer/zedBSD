<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-45 / realm-aware prototype-chain consumers

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)を次のexact finite partialへ適用。q44 terminal cfd15d78e/source19b9b9ed6/history SHA2569810719d4e6f7726dbd3d1457cbcbe7d13fc2f8abf643189b9e8670034ba1cd0確認。
Timebox: 最大90分、2026-10-18 22:11:21→23:41:21 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-45-i01 | [p258](phase258/phase.md) chain consumers partial | object_is_prototype_of/object_lookup_half/vm_ordinary_has_instanceのprototype直接参照をrealm GetPrototypeOf、lookupのheap GetOwnをrealm GetOwnへ接続。ToObject/ToKey順・nearest property停止・trap/descriptor/prototype順・null/primitive/abrupt identity保持。managed realm/constructor/boxed target/current walk/fresh key/prototypeをcallback/GC中保持。old552 prefix保持＋新genuine controls/実GC/errno、full changed C/host warning0/q02 exact2 source target warning0/focused/Acid/元WebIDL保持。 | in-progress | q42 GetPrototypeOf/q33 GetOwn、q44 callers証拠、native VM call保持契約 |

Generation bounds: js/builtin_object.cの上記2functionsとhelper宣言配置、vm/access.cのvm_ordinary_has_instanceだけ。host-proxy-read.c/proxy-read.jsに必要なcontrols/GC対照、WS074 records。public ABI2/native_operations定義/comment、WPT/runner/manifest/期待値、global Proxy/Reflect/callable・nested factory bounds、他VM functions/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: actual internal methodsを順次呼び、戻りprototypeを次回script前にcurrent-rootへ移す。lookupはown descriptor発見時dataならundefined/accessorなら指定halfを返しprototypeを読まない。isPrototypeOfはargument primitiveならreceiver ToObject無しfalse。OrdinaryHasInstanceはconstructor callable→value object→constructor prototype Get/validation→各GetPrototypeOf→SameValue。BoundFunction特殊経路は既存別未達で本partialでは修正しない。root登録は失敗数を追跡し単一forward cleanupで全exit退役。
Finite investigation: 90分内、各native/oracle process30秒、focused300秒。無限に同じprototypeを返すtrapのlanguage algorithmは非停止し得るので新しいidentity-cycle shortcutや任意chain長制限を追加しない。有限回同じobjectを返してから終わるtrapを検証する。general VM interruption/resource policyは本partial外。
Standards: Guardrail/C全文/browser-component全文直接適用、簡約なし。ANSI declarations first/semantic paragraphs/multiline fn、normal helper宣言は全table後、table initializer必要分のみ前。clang-format19 ColumnLimit0後headers/compound clauses復元、全変更function manualとchecker（許可単一forward cleanup以外0）。host serialized終了確認、q02 targetliteral -oだけ変更、versions取得、raw全member read-back。
Verification: old552(native552/Chrome550既知huge2)保持、新条件genuine対照。q36負zero classifierfilter/q43 huge-length2外部差を維持、all pure equalityと呼ばない。全1508はq44最新全callback/completion同値証拠を再利用し、本pair全census/images/crashは未実測と明記（new gainsを主張しない）。公式1482/3805・held0・未attempt275保持。元WebIDL4+intentionalnegative、validator require-exposed2+negative保持。必要なfocusedのみ実行。
Explicit residual: vm_instanceof outer dispatch/bound functions/callableProxy、for-in/spread/JSON/ordinary Set exotic prototype/other invariant realm化/nested/call/construct/global/immutableProto/typed backing/p100/full targetは未達。whole p258/WSはclearしない。共有投影pending/no agents/push/削除/make check/QEMU。

Dependency graph: q44 verified consumers + q42 GetPrototypeOf + q33 GetOwn → i01。外部nodesは今回の実装許可ではない。
Outlook（非承認）: ordinary Set exotic prototype routing、for-in、other invariant realm化→nested、call/construct/global constructor/revocable。
