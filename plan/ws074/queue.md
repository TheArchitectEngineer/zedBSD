<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-42 / Proxy prototype operations

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q41 terminaldecb248bc/source980acd47c/history SHA256011178acdc135dfb32a8de698e9a69d3622cb73240e3f51cb4df0d6088edc143 read-back。
Timebox: 最大90分、2026-10-18 21:28:39→22:58:39 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-42-i01 | [p258](phase258/phase.md) GetPrototypeOf/SetPrototypeOf partial | VM realm-aware vm_get_prototype/vm_set_prototypeとProxy2 trap/fallbackを追加し公開Object.getPrototypeOf/setPrototypeOfとAnnexB __proto__ get/setを接続。ordinary Setの既存JS helperをVMへ移しsame/nonext/cycle拒否を保存、cycle確認はProxyのnonordinary GetPrototypeOfで止める。dynamic GetMethod/null fallback、handler this/元target/任意proto、Get結果object-or-null・post-trap target extent/prototype invariant、Set false即拒否/trueはtarget nonextならactual同一proto要求、revoke/GC/error/depth。旧406 prefix＋native/genuine追加対照、managed owner/元participants/protoinputとfresh出力保持/終了後実回収、host/target exact access+builtin_object2 warning0/full changed C/focused gates/Acid/元WebIDL2保持 | in-progress | q40 IsExtensible、q41 managed frame/Proxy基盤 |

Generation bounds: vm/access.c（新public2 APIs/private Proxy2 helpers/pure ordinary Set helper/必要declarations）、vm/vm.h（宣言2だけ）、js/builtin_object.c（object_get_prototype_of/object_set_prototype_of/object_proto_get/object_proto_setを薄いwrapperへ接続、旧pure object_set_prototype helper/declをVMへ移管・除去）、tests/proxy-read.js/host-proxy-read.c（追加controls/GC）、WS074 records。other Object/Proxy methods・native_operations定義/comment/public ABI2/Wayland/global Proxy/Reflect/nested/callable factory bound/WPT/runner/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: realm frameでowner/input/requested protoをroot後depthを有界化、helperがoriginal target/handler/fresh method/Get fresh prototype resultを保持。prototype入力/出力はobject又はnull、Get trapはtarget extensibleなら任意object/null、nonextならtarget actual GetPrototypeOfとSameValue。Set trap falseはtarget確認無し、trueはtarget actual IsExtensible→nonextならactual GetPrototypeOfを要求。ordinary Setは旧same/nonext/cycle処理のVM所有移管でVM→JS依存無し。通常cyclewalkはnonordinary ProxyのGetPrototypeOfを呼ばずそこで止める。Get primitive ToObject・Set RequireObjectCoercible/prototype validation・AnnexB無効proto無視は既存順を保持。immutable-prototype exotic等の元ordinary実装の別未達は全体verifiedと主張しない。targetはまだnonProxyだけ、post-target scriptによるfresh result/root pressureはnested許可の後続scopeで再検証。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI宣言first・意味paragraph・public/static順・multiline fn・normal helper prototypes tables後/table-required例外維持。clang-format19 ColumnLimit0後fn/3+clause復元、full changed fn/checker許可forward cleanup以外0。host serialized終了確認・final source/pair/input固定、target q02 literal2 commandの-oだけ変更。
Verification bounds: focused gatesのみ、all1508 q37/static/scripted/crash q28再利用、新pair全census/capture同値は主張しない。ordinary公開挙動とProxy private controlsを保持、公式gain0・1482/3805・held0・未attempt275保持。raw全member read-back。90分未達はuncleared/resume。no agents/push/削除/make check/QEMU。
Dependency graph: q40 target actual extent + original Proxy ownership → Get/SetPrototypeOf VM/public/AnnexB → controls/GC/gates → keys/other invariants/nested/call/construct/global（未選定）。
Upcoming Work Outlook: own keys/other internal invariant realm化/nested/call/construct/global/revocable → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS100%未達を保持。
