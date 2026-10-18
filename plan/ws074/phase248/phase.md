<!-- awesome-plan project=zedbsd record=ws074-p248 -->

# ws074-p248: animation / transitionのnative timingとDOM lifecycle

Status: planning
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定
Origin: [p246 q11](../phase246/evidence/20261018-q11/README.md)

## Objective・current evidence

[p224](../phase224/phase.md)が所有するGrid track補間・合成の前提として、CSS/WA animation・transitionのnative timing、DOM Animation state/GC、seek/pause/cancel、per-window lifetimeとframe invalidationを提供する。Grid固有のmin/max/fit/list matchingと補間・合成の受け入れはp224が引き続き所有する。q11はwhole248維持だが24 callbackの以前のfalse PASSを露出。animate/Animationはundefined、@keyframes/transition timelineもnative実装が無い。期待値は新serializerでmax-contentを保持する一方actual underlyingは変化しない。

## Planning / dependencies

p246のverified scalar/computed outputを前提にし、Phase whole clearanceには依存しない。CSS parser/rules、native timing clock/checkpoint、DOM Animation state/GC trace、per-window ownership、cancel/seek/pause、frame invalidationを実コードから設計する。public browser ABIを変える提案や重大なtiming policyは実装前に既存authorityへ提示する。test helper/runnerで値を補正せず、通常native pathを実装する。

completionには独立時刻/seek/pause/cancel/GC/lifetime、通常CSS/WA entry pointとframe invalidationの検証、host/target warning0・C full standards・Acid非悪化が必要。固定Grid animation12 IDの統合whole/callbackとproperty補間はp224で検証し、このPhaseはnative timing/DOM primitiveのscoped outputを渡す。fit-contentやnamesの表現の未達と別に切り分ける。p246新attemptは実装のneeded outputが実際に検証された後だけ実行する。Guardrail/C/browser standard適用、p100はWS最終関門。

Event ws074-p248-planned-from-q11-20261018: q11の24既存PASS lossを隠さず保持し、full native feature設計へ分離。p246/q11はunclearedのまま、ここへの記録だけでclearしない。共有投影pending。

Event ws074-animation-phase-ownership-reconciled-20261018: q14の待機中に既存[p224](../phase224/phase.md) q1135を再確認。新p248とGrid track goalが重複していたため、p224の既存goal/history/未達を保持し、p248をnative timing/DOM lifecycleという前提outputへ限定。p248 verified output → p224 Grid interpolation/compositionと依存を明示。p246/q11未達とq14のcomputed-only scope/criteria/approvalは不変。両Phaseは未選定であり、この設計改訂だけでは実装を開始しない。
