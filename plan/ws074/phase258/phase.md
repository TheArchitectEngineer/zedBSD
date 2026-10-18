<!-- awesome-plan project=zedbsd record=ws074-p258 -->

# ws074-p258: native Proxy internal methods and lifetime

Status: in-progress（q30 read foundation partial）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-30-i01
Origin: p189/q29 original WebIDL2 parseがProxy未定義で停止。

Objective / own criteria: native managed Proxyを実object internal methodsへ接続し、constructor/revocableと各internal operationのtrap/fallback/invariant/receiver/revocation/callability/GCを正しく実行する。original WebIDL2 actual parse/validatorに必要なget/set/array behaviorを通常APIで提供し、independent controls/原WPTを通す。部分read stageだけでwholeをclearしない。
Design: vm_native_operationsはscript-free heap interfaceのまま保持。Proxyにはmanaged target/handler recordを持たせ、scriptを実行可能なrealm-aware access/internal methodからtrapを呼ぶ。opaque JS constructor公開前に必要なoperationを段階的に整える。各段階は自走承認に基づく新finite Queueで正確に選定する。
Current scope: [q30 exact Queue](../queue.md)のnoncallable/nonProxy target・read-only foundation/private VM factoryだけ。未選定write/descriptor/ownkeys/prototype/IsArray/function/call/constructとglobal公開を承認と混同しない。全Phase完了基準から消さない。
Standards: Guardrail/C全文/browser component全文、簡約版無し。一次 https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-get-p-receiver 。既存p100がWS全source near-final conformanceを所有し、本partialはnew/changed scopeの全文適合を検証。
Verification/bounds/affected components: q30 snapshotの通り。private object kind/cell/newborn/trace/revokeとactual receiver/trap/invariant/GCをnative/Chromeで確認し、ordinary whole/callback PASSを守る。target single-source compileのみとfull target runtimeを区別。未達はunclearedとresume。

Event ws074-p258-q30-selected-20261018: q29 verified delegationの後、正規WebIDL2がProxyを要求することを原source/Chrome actual4/4・native1/4で確認。既存heap native hooksはuser script禁止なので、その契約を保つrealm-aware Get foundationを選定。partial source/scope/90分/gatesを実装前に保存。global Proxy/whole parserを先取り公開・認定しない。共有Master/next/Queue/Project/GitHubの投影pending。
