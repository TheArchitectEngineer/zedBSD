<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-41 / Proxy DeleteProperty

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q40 terminala1c187b79/source5eb0f6f11/history SHA2561047163e6869a1313acdc7b36ea440dbdfc57b0d3161879500c2ae6ea369003c read-back。
Timebox: 最大90分、2026-10-18 21:21:49→22:51:49 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-41-i01 | [p258](phase258/phase.md) DeleteProperty partial | vm_delete_property realm API/Proxy deleteProperty trapを追加し既存delete operatorのobject pathを接続。dynamic GetMethod/null fallback、handler this/元target/string-or-symbol key2引数、ToBoolean false時target読取無し、true後actual own descriptor無しならtrue/fixed own拒否/actual nonextensible existing own拒否、trap中実削除/revoke/GC/error/depth/strict/sloppy semantics。ordinary/array/TypedArray native delete policyは保存。旧372 source prefix保持＋native/genuine Chrome追加対照、managed owner/元target/handler/fresh symbol keyをpost-trap no-script get-own GCまで保持・fallback native delete GC/終了後実回収、host/target exact access1 warning0/full changed C/focused gates/Acid/元WebIDL2保持 | in-progress | q40 realm-aware IsExtensible、q33 GetOwn、q39 ownership |

Generation bounds: vm/access.c（vm_delete object dispatch、新public vm_delete_property/own private helper/declarationsだけ）、vm/vm.h（宣言だけ）、tests/proxy-read.js/host-proxy-read.c（private Boolean deleteReadProperty bridge/追加controls/実GC pure native policy観察）、WS074 records。vm_object_delete/typed policy本体・primitive/string delete・other Proxy methods/extensibility snapshots・global Proxy/Reflect/nested/callable factory bound/native_operations定義/comment/public ABI2/Wayland/WPT/runner/manifest/期待値/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: realm frameがinput/key/ownerをdepth error前にroot。Proxy helperはoriginal target/handler/key/fresh methodを明示snapshot-rootしGetMethod/revoke/normal/error全pathを解放。symbol keyはそのまま、integer internal keyはstring trap keyへ変換してroot。falseは即deleted0、trueはrealm-aware target GetOwnでpresent/fixedを確認、existing configurableだけactual target IsExtensibleを呼ぶ。snapshotのpayloadは以後読まずattributesだけ利用。native callbacksは収集可能だがuser script不可。ordinaryは既存vm_object_deleteへ委譲し結果/errnoを保持。public delete operatorだけ既存strict拒否を適用、Boolean bridgeは言語内部APIを直接使う。nested policy解除/Reflect公開は別Queue。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI declarations first・意味paragraph・public/static順・fn multiline・normal prototypes tables後/table-required例外のみ。clang-format19 ColumnLimit0後fn/3+clause復元、full changed fn review/checker許可forward cleanup以外0。host serialized終了確認・final source/pair/input固定、target q02 literal access commandの-oだけ変更。
Verification bounds: focused gatesのみ、all1508 q37/static/scripted/crash q28再利用、新pair全census/capture同値は主張しない。private global未公開・普通の公開delete semantics維持を対照。公式gain0、1482/3805・held0・未attempt275保持。raw全member read-back。90分未達はuncleared/resume。no agents/push/削除/make check/QEMU。
Dependency graph: q40 actual extensibility + q33 GetOwn → DeleteProperty/native fallback/strict operator → controls/GC/gates → remaining keys/prototype/nested/call/construct/global（未選定）。
Upcoming Work Outlook: remaining Proxy operations/other invariant realm化/nested/global公開 → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS100%未達を保持。
