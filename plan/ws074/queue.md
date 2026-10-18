<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-39 / Proxy-aware IsArray

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q38 terminal82c2a3450/history SHA2568b91f52da39c0bc54aa3568ff52a003f0c48e01ce45a32e7fc6fbaca32741730 read-back。
Timebox: 最大90分、2026-10-18 21:17→22:47 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-39-i01 | [p258](phase258/phase.md) IsArray partial | vm_is_arrayを追加しpublic Array.isArrayを接続。primitive/ordinary/typedはno coercion/no trapで正規true/false、Proxyはtargetへ反復しrevokedはTypeError。managed realm/inputはerror allocationのactual GCまで保持し終了後実回収。既存323条件source prefix保持・追加native/genuine Chrome対照・private factory bounds不変・full changed C/host warning0/q02 exact access+builtin_array target warning0/focused gates/Acid。q38 unchanged originalWebIDL2 positive/negativeもfinal pairで再確認 | in-progress | q38 IsArray実差、q37 private Proxy ownership/operations |

Generation bounds: vm/access.c（public vm_is_arrayだけ、必要own declarations/trace共有）、vm/vm.h（宣言だけ）、js/builtin_array.c（public array_is_array wrapperだけ、他concat/flatten等internal array_is_array_value consumersは今回変更しない）、tests/proxy-read.js/host-proxy-read.c（追加対照/GC、既存条件は保持）、WS074 records。global product Proxy/Reflect/nested/callable policy/native_operations ABI/comment/public ABI2/Wayland/other array algorithms/runner/WPT/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: primitive/ordinary fast pathは無allocation、Proxy frameは既存read tracerでmanaged realmとoriginal graphを保持。immutable target chainをnative反復しscript/trap/prototype/propertyを呼ばない。targetは生成時既存objectのみで自己cycle形成不可。revokedはtarget/handler null確認後正規TypeError、errorからreturnまでroot保持。公開wrapperはstatusを保持しboolean出力。public/internal consumersの残件を明示。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI宣言first・meaningful paragraphs・fn header multiline・normal prototype aftertables、table initializer必要宣言だけ例外。clang-format19 ColumnLimit0後signature/3+clauseを確認。full changed fn manual＋checker raw許可forward cleanup/他0。host serialized終了確認、target q02 exact commandの-oだけ変更。native callback contract不変。
Verification bounds: focused gatesだけ、all1508はq37、static/scripted/crashはq28を明示再利用。普通の公開IsArray挙動と既存323を保持しunexposed Proxy動作だけ是正。公式gain加算0、確認済1482/3805・held0・未attempt275維持。final source/pair/input hash固定・raw archive全member read-back。90分未達はuncleared/resume、追加試験は具体的riskだけ。push/削除/make check/QEMU/agents無し。
Dependency graph: native Proxy immutable graph → IsArray/public wrapper → GC/controls/focused/originalWebIDL2 → remaining keys/delete/prototype/extent/call/construct/global/revocable（未選定）。
Upcoming Work Outlook: remaining native internal methods → global publication/full original WPT → 未attempt275/API/layout/p100/full target。whole p258/WS100%は未達のまま。
