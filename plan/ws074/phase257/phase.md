<!-- awesome-plan project=zedbsd record=ws074-p257 -->

# ws074-p257: synchronous generator delegation

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-29-i01

Objective: native synchronous generator yield*をactual iterable protocolで実行し、固定WebIDL2のcompiler prerequisiteを解消する。async generators/for awaitは範囲外。
Completion criteria: sync delegationのnext/throw/return・normal/abrupt completion・result identity・finally/GCと独立Chrome/native対照、final source full-standard conformance・host/target warning0、既存PASS後退無しを実証。WebIDL2後続API不足を実装済みと混同しない。
Design: generator-owned managed iterator recordがactual iterator/cached nextを保持。start/step opcodesで普通のcompiler loweringを行い、stepはyield/normal/return outcomeを返す。delegated done falseのresultを専用suspensionから外へ同一objectで渡す。通常yield/awaitは従来wrappingを維持。return completionは既存js_emit_returnでfinallyを通る。
Prerequisites: q28 verified VM/Window/GC、p086既存generator output（元Phase範囲を変更せずyield*未対応という歴史を保持）、p189 q24 pinned WebIDL2/IDL inputs。Guardrail/C全文/browser component全文を適用。
Verification / bounds / generation scope: [current Queue exact snapshot](../queue.md)。TC39 primary https://tc39.es/ecma262/multipage/ecmascript-language-functions-and-classes.html#sec-generator-function-definitions-runtime-semantics-evaluation を参照。max90分、2026-10-18 20:39 JST、未達はuncleared。

Event ws074-p257-q29-selected-20261018: q24/28の原WebIDL2 unsupported yield*をsourceで確認。source変更前に有限scopeを選定、独立protocol/negative/GCを必要条件とする。p189はverified delegation後に再実行するだけでrunner/API補完へ範囲を広げない。whole WS/p100未達、共有Master next/registry/Project投影はQ1所有のためpending。
