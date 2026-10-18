<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-29 / synchronous generator yield* delegation

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこのfinite partial scopeへ適用。q28 terminal e5572243e/history SHA256469ef149fce63f08789c17b76b6b7739d8a1c74bd4fef9c9e14e060b76916f13をread-back。最大90分、2026-10-18 20:39 JSTまで。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-29-i01 | [p257](phase257/phase.md) synchronous yield* partial | 通常compilerでsync yield*をnative managed iterator record/start/stepと既存suspension/return-finallyへlower。actual Symbol.iterator/next一回取得、next常に1引数（初回undefined）、throw/return動的GetMethodとmissing/null/callability/close precedence、iterator result Object/done/value順とdone falseのvalue非読出し・result identity維持、normal/throw/return completion、nested/finally/GCを実装。VM opcode/checker/tableとprivate resume contractを整合しraw resultをJS wrapperで再包装しない。独立Chrome/native protocol/abrupt/identity/GC controls、既存generator/async/DOMとhost warning0・変更source exact target warning0・full changed C review、固定1508 census/旧PASS callback保持とWebIDL2再実行を確認、原raw/hashを保存 | in-progress | q28 verified VM/Window/GC/source pair、既存generator・iterator protocol出力、TC39 yield* sync算法 |

Dependency graph: q28 VM/GC/Window → synchronous delegation/compiler/native controls → fixed1508/native regression/WebIDL2 prerequisite audit → outcome/archive。
Generation bounds: engine既存js/compile_expr.c/builtin_generator.c、vm/generator.c/interpreter.c/internal.h/vm.h/bytecode.h/code.c、必要な既存vm/check.c（存在するcheckerの実source）、tests/dom/generator-delegation.html/.expected、tests/host-generator-delegation.cとWS074記録だけ。既存spread iteratorのoptimized経路は変更せずgenerator-owned native iterator recordを使用。新source module/ABI2/Wayland/runner/WPT/manifest/期待値/SDK/sysroot/toolchain/共有Board/cacheを変更しない。bytecodeはcompilerから通常生成し生成後書換えをしない。private VM resume APIは実caller全件追従する。
Standards: Guardrail・C全文・browser component全文を直接適用、簡約版無し。format19/full manual/checkerのraw許可cleanup flagsを明示。q02 target dry-runの既存source exact single-source commandで-oのみ変更、host buildは逐次完了確認しfinal source pairをuniqueにfreeze。VM変更なので全1508 harnessと既存DOM/native GC/Acidを確認し、画像render source不変ならstatic/scripted/crashはq28証拠の再利用と限界を記録（具体的後退根拠が出た時だけ再測定）。
Bounds: async generator/for-await、全ECMAScript/Test262適合、WebIDL/native API全実装、未attempt275/全fixedPASS、full target link/runtime/p100/whole WSはこのpartialで達成としない。WebIDL2が次の未実装で停止する場合は原error/loader/sourceを保存し、JS補完やrunner判定変更で通さない。90分または未解消後退/GC/identity失敗はunclearedとresumeを保存。push/削除/make check/QEMU無し。
Upcoming Work Outlook: WebIDL2実行後のactual missing APIs、external/syntax errors、未attempt275、layout/API/target/p100。

