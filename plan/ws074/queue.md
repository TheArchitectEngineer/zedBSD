<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-32 / shared VM property-descriptor conversion

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこのfinite partial scopeへ適用。q31 terminal8159bc81d/history SHA2568f564a69ab70b8be58b27153d5e8ab7930b52e2841d0101d96da5a90c306c62f read-back。q31 exact history/原archiveは保存済み、standing Queueをこのcycleへ再利用。
Timebox: 最大90分、19:57:15 JST→21:27:15 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-32-i01 | [p258](phase258/phase.md) shared descriptor helpers partial | VM-owned ToPropertyDescriptor/FromPropertyDescriptor/CompletePropertyDescriptorを既存access層へ提供し、JS Objectの既存to/from helperを同じVM処理へ接続。ToのHas/Get6順・get即時callability・set即時・mixed最後と既存error strings、Fromの各Has fieldだけを標準順にordinary data propertyへ出力・未指定enum/configも勝手に足さない、NULL descriptor→undefined、Completeのdata/accessor判定と未指定fields/false flags/undefined defaultsを実装。native realm/source/input payload/descriptor values/未公開made objectをtracerで保持しflags無しfieldの未初期化値を読まない。shared native/Chrome genuine Proxy define trapでpartial capture/normal complete snapshotを独立対照し、old68 source prefix・public invalid getter順・fresh value/getter GC、native C pressure/managed realm/output回収を確認。host warning0・target exact access/builtin_object2 source warning0・new/changed full C、DOM41/CSSOM/VM/factory/producer/observer/error/Acidのfocused gatesとraw/hashを保存 | in-progress | q31 actual Has/Get・正規ToPropertyDescriptor順・GC、TC39 To/From/Complete、VMはJS moduleへ依存しない既存layer |

Dependency graph: verified q31 Has/Get → VM descriptor conversion/complete → JS Object to/from integration → focused native/Chrome/GC/gates → 後続Proxy getOwn/Define/Set（後続未選定）。
Generation bounds: 既存engine vm/access.c/vm.h、js/builtin_object.cのobject_to_descriptor/object_from_descriptorと移管で不要になるobject_descriptor_field（削除はcode要素だけ）・対応するown forward declarations、tests/proxy-read.js/host-proxy-read.c（descriptor roundtrip/complete bridge・pressure/GC追従）、WS074記録だけ。VMはjs_builtin_*やJS header/APIへ依存せず、realm/core object APIで処理する。new module/public ABI2/Wayland/native_operations/runner/WPT/manifest/既存期待値/SDK/sysroot/toolchain/共有Board/cacheを変更しない。
Semantics bounds: 既存Object.getOwnPropertyDescriptor等はcomplete descriptorを受けて従来fields/順/値/prototypeを維持。新partial From/Completeはprivate VM bridgeから実対照し、global Proxy/descriptor trap/Set/Reflect等はまだ公開・実装しない。full p258/IDL parse/全Test262は未達を維持。
Standards: Guardrail/C全文/browser component全文を直接適用、簡約版無し。helper/type purpose/forward decl/table order、new/changed全文manual/format19/checker・raw許可cleanup明記。targetはq02 exact2既存commandの-oのみ変更。host buildは単独で完了確認、final sourceからimmutable pair/control inputを固定。
Verification bounds: この共有化/partial helperはfocused semantic/GC/gatesを実測し、全1508はq31原結果を再利用して新pair全件同値と主張しない。static/scripted/strictもq28原証拠を再利用、fixed denominator3805/未attempt275/held0を保持しPASS増分は加算しない。focused後退が具体化した場合だけ必要な追加gateを選び、未達/90分でunclearedとresume。push/削除操作/make check/QEMU/full target/p100無し。
Upcoming Work Outlook: shared conversions → realm-aware Proxy getOwn/Define → Set/keys/prototype/extent/IsArray/callability/global constructor/revocable → actual original WebIDL2再評価、native/layout/API/未attempt275/target/p100。

History before this cycle: [q31](history/ws074-q20261018-31.md)、[Past Log](history/index.md)、[older standing projection archive](queue-archive-through-q30.md)。
