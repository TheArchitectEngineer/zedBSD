<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-47 / nested noncallable Proxy targets and realm invariant observations

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q46 terminal3c94e5897/sourced38a8b622/history47fb91059af5a85dd06a3cb72309fffbbea2bb7cc276bb33070b799160097f97確認。
Timebox: 最大90分、2026-10-18 22:28:58→23:58:58 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-47-i01 | [p258](phase258/phase.md) nested noncallable target partial | Get/Has post-trap target heap GetOwnをrealm GetOwnへ、Has/GetOwn/Define actual extentをrealm IsExtensibleへ接続。GetOwn undefinedはmissing/fixedを先判定し不要extentを呼ばない。GetOwn/Define current flagged descriptor snapshotをextent script前からtrace。private factoryのnested target拒否だけ解除しcallable拒否/global非公開を保持。old622 source prefix保持、新nested trap/fallback/順序/invariant/receiver/revocation/GC/depthと既存全internal methodsのactual native/genuine controls。full changed C/host warning0/q02 exact2 Ctarget warning0/focused/Acid/元WebIDL保持 | in-progress | q30〜q46 realm operations/private managed Proxy・all object Set・ownKeys/prototype/extent/IsArray/descriptor snapshots |

Generation bounds: vm/access.c access_proxy_get/access_proxy_has/access_proxy_get_own/access_proxy_defineだけ、vm/object.c vm_proxy_create validationだけ。必要private tests host-proxy-read.c/proxy-read.js（mainの旧nested拒否をexplicit positiveへ変更、新native snapshot-GC fixture/weak observers/必要private helpers）。vm.h/native_operations定義/comment/ABI2/Wayland/他VM functions/WPT/runner/manifest/期待値/SDK/toolchain/sysroot/共有Boards/cache変更無し。factoryのcallable target拒否は維持しcallability/construct/global Proxy/Reflectは今回含まない。
Design: Get/GetOwn/Has/Defineの各primary algorithmのactual targetGetOwn→条件付きactual IsExtensible→descriptor conversion/compatibility順を守る。Has falseはmissing/fixed判定後だけextent。GetOwn missing出力＋missingtargetはextent無しreturn、fixedtargetはextent無しthrow、configtargetだけextent。GetOwn nonundefined/Define trueはcurrent payload tracerを新しいextent callbackより前に登録し、absent descriptorはhas0初期化でpayload未読。post-trap targetのrevocation/GC/例外identityとold snapshotsを保持。既存Set/Delete/GetProto/SetProto/extent/ownKeys/IsArrayのnested実対照も足すがこれらproduct sourceを変更しない。native fallback budget256とcleanup後depth0を保持。
Native GC criterion: inner target own descriptorから得たfixed object payloadが、inner IsExtensible trapによるinner revokeとtarget ownership退役の後、outer descriptor value getterのactual GCを越えて生存する。weak native observerで元actualtarget実回収/payload生存を同時検証しpayloadだけを返してSameValue適合、output-only root後に残graph回収→output root解放でpayload実回収。native VM trapsはvm_call経由のnative functionで実行しscript-free native_operationsへJSを入れない。nested GetPrototypeOf fresh resultもpost-target extent callback actualGCを実対照で通す。
Standards: Guardrail/C全文/browser-component全文直接適用（簡約無し）。ANSI declarations first/意味paragraph/multiline/public-static順/normal prototypes全table後・table initializer必要分だけ前。clang-format19 ColumnLimit0後必要形復元、全変更functions manual＋checker許可single forward cleanup以外0。host serialized、q02 literal access/object2 C compile -oだけ変更、versions取得、raw全member読戻し。
Verification bounds: old622native622/Chrome620既知huge2とq36filter資格を維持しpure all equalityを主張しない、新shared条件はgenuine実対照・native weak-only fixtureは別立て。各protocol30秒/focused300秒。公式global Proxy無しのprivate factory/internal invariant範囲なので最新全1508 q46（256.7秒、q44全whole/callback/completion同一）証拠を再利用しnewpair全census未実測を明記・new official gain主張無し。images/crash q28再利用・newpair全capture未測定。確認済1482/3805・held0・未attempt275維持。
Explicit residual: primitive Set/outer vm_instanceof/BoundFunction、for-in/spread/JSON、call/construct/global Proxy/revocable/Reflect、array IsArray consumers/upper-index storage/immutable prototype/typed backing/p100/full target/whole p258/WS未達。no agents/push/削除/make check/QEMU。共有投影pending。
Dependency graph: verified q30〜q46 realm methods → i01。external nodesは今回membership外。
Outlook（非承認）: for-in/primitive Set、callable/construct/newtarget realm→global constructor/revocable、original WPT再測定。
