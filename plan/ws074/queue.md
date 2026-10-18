<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-39 / Proxy-aware IsArray

Status: finished（i01 IsArray scoped partial cleared）
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q38 terminal82c2a3450/history SHA2568b91f52da39c0bc54aa3568ff52a003f0c48e01ce45a32e7fc6fbaca32741730 read-back。
Timebox: 最大90分、2026-10-18 21:11:21→22:41:21 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-39-i01 | [p258](phase258/phase.md) IsArray partial | vm_is_arrayを追加しpublic Array.isArrayを接続。primitive/ordinary/typedはno coercion/no trapで正規true/false、Proxyはtargetへ反復しrevokedはTypeError。managed realm/inputはerror allocationのactual GCまで保持し終了後実回収。既存323条件source prefix保持・追加native/genuine Chrome対照・private factory bounds不変・full changed C/host warning0/q02 exact access+builtin_array target warning0/focused gates/Acid。q38 unchanged originalWebIDL2 positive/negativeもfinal pairで再確認 | cleared | q38 IsArray実差、q37 private Proxy ownership/operations |

Generation bounds: vm/access.c（public vm_is_arrayだけ、必要own declarations/trace共有）、vm/vm.h（宣言だけ）、js/builtin_array.c（public array_is_array wrapperだけ、他concat/flatten等internal array_is_array_value consumersは今回変更しない）、tests/proxy-read.js/host-proxy-read.c（追加対照/GC、既存条件は保持）、WS074 records。global product Proxy/Reflect/nested/callable policy/native_operations ABI/comment/public ABI2/Wayland/other array algorithms/runner/WPT/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: primitive/ordinary fast pathは無allocation、Proxy frameは既存read tracerでmanaged realmとoriginal graphを保持。immutable target chainをnative反復しscript/trap/prototype/propertyを呼ばない。targetは生成時既存objectのみで自己cycle形成不可。revokedはtarget/handler null確認後正規TypeError、errorからreturnまでroot保持。公開wrapperはstatusを保持しboolean出力。public/internal consumersの残件を明示。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI宣言first・meaningful paragraphs・fn header multiline・normal prototype aftertables、table initializer必要宣言だけ例外。clang-format19 ColumnLimit0後signature/3+clauseを確認。full changed fn manual＋checker raw許可forward cleanup/他0。host serialized終了確認、target q02 exact commandの-oだけ変更。native callback contract不変。
Verification bounds: focused gatesだけ、all1508はq37、static/scripted/crashはq28を明示再利用。普通の公開IsArray挙動と既存323を保持しunexposed Proxy動作だけ是正。公式gain加算0、確認済1482/3805・held0・未attempt275維持。final source/pair/input hash固定・raw archive全member read-back。90分未達はuncleared/resume、追加試験は具体的riskだけ。push/削除/make check/QEMU/agents無し。
Dependency graph: native Proxy immutable graph → IsArray/public wrapper → GC/controls/focused/originalWebIDL2 → remaining keys/delete/prototype/extent/call/construct/global/revocable（未選定）。
Upcoming Work Outlook: remaining native internal methods → global publication/full original WPT → 未attempt275/API/layout/p100/full target。whole p258/WS100%は未達のまま。

Timing correction: selection commit3a24e9028 author timestamp21:11:21 JSTを実開始として採用。初期snapshotの21:17は将来時刻の記載誤りで、original snapshotを保持しdeadlineを22:41:21へ狭める。scope/criteria不変。

## Outcome

vm_is_arrayを追加し公開Array.isArrayを接続。primitive/ordinaryは従来どおり無allocation/no-coercionで判定し、native Proxyはimmutable target chainを反復、revokedは正規TypeErrorを返す。handler/prototype/get/GetPrototypeOf trapを一切呼ばず、managed realmと元Proxy graphを既存read tracerでエラー生成まで保持し、normal/abruptともframeを外す。concat/flatten等internal array_is_array_value consumersは選定外として完全保持、private factory callable/nested target拒否とglobal Proxy非公開を維持。
旧323 source prefix全bytes同一、追加13のIsArray genuine Chromium条件を含む336がnative/oracle対照で通過。旧canonical key分類のq36 negative-zero primitive filterを継承しており全336をpure genuine exotic一致とは主張しない。native C stack保守保持無し。bare managed realmと実revoked Proxyのsetup rootsを落とし、actual bootstrap TypeError string allocationのthreshold collectionが発生、realmと元Proxyを独立に生存確認。終了後に両者の実回収を確認した。従来Proxy5-hook/sole results/write inputs/From/BigInt parser/typed storage GC対照も保持。
source WIPc84e12f08、final source/pair/input317 hash照合。host final warning0、q02 exact access/builtin_array2 target warning0。全文/manual reviewとclang-format19、full changed Cの許可forward cleanup35・他checker0。既存DOM41、VM45/factory56、CSSOM12+12、marks7/observer10/ErrorEvent9/ErrorReporting19/delegation10を通過。Acid3 score/inventory100・pixel0、Acid2既存873を維持。q38と同じ元WebIDL2/testharness/source bytesをfinal pairで再実行、baseline Proxy undefined、private adapter positive4/4・元intentional negative assertion到達の全callbackを完全保持。これは公式WPT/global constructorの成功認定ではない。
exact finite scopeどおりfocused gatesのみ。all1508はq37、static/scripted/crashはq28を明示再利用し、q39全capture/全callback同値は未測定。公式gain加算0、確認済1482/3805（38.948752%）・held0・未attempt275維持。i01 IsArray partialだけcleared、whole p258/WS/p100/full target/100%未達。keys/delete/prototype/extent/call/construct/global/revocable、array別consumers/typed残りを保持。共有Master/Queue/cache/GitHub投影pending、push無し。
管理commentに残っていた旧Phase名を触れたsource headerから除去。選定snapshotの21:17という将来時刻の記載誤りはoriginalを保存し、selection commitの21:11:21をactual startとしdeadline22:41:21へ狭めた。scope/criteria不変。p258冒頭の旧q37投影もdated q38履歴を保持してq39へ復旧。
Raw archive68 membersを全member read-back、SHA25656690aa90d83af6200e77c119bdff5902eebd5a171de50a78128163822260c7d。凍結binaryはhash付きbuildに保持しarchive未同梱。

[exact history](history/ws074-q20261018-39.md)。
