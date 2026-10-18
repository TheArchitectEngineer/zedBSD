<!-- awesome-plan project=zedbsd record=ws074-p248 -->

# ws074-p248: Grid animation / transitionのnative timingとtrack interpolation

Status: planning
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定
Origin: [p246 q11](../phase246/evidence/20261018-q11/README.md)

## Objective・current evidence

CSS/WA animation・transitionをnativeに適用し、Grid trackのmin/max/fit/intrinsic/list matchingとdiscrete/interpolated/composited値を仕様に従わせる。q11はwhole248維持だが24 callbackの以前のfalse PASSを露出。animate/Animationはundefined、@keyframes/transition timelineもnative実装が無い。期待値は新serializerでmax-contentを保持する一方actual underlyingは変化しない。

## Planning / dependencies

p246のverified scalar/computed outputを前提にし、Phase whole clearanceには依存しない。CSS parser/rules、native timing clock/checkpoint、DOM Animation state/GC trace、per-window ownership、cancel/seek/pause、frame invalidationを実コードから設計する。public browser ABIを変える提案や重大なtiming policyは実装前に既存authorityへ提示する。test helper/runnerで値を補正せず、通常native pathを実装する。

completionには固定animation12 IDを含む全Grid/影響領域でのwhole/callback、独立時刻/合成/GC/lifetimeの検証、host/target warning0・C full standards・Acid非悪化が必要。fit-contentやnamesの表現の未達と別に切り分ける。p246新attemptは実装のneeded outputが実際に検証された後だけ実行する。Guardrail/C/browser standard適用、p100はWS最終関門。

Event ws074-p248-planned-from-q11-20261018: q11の24既存PASS lossを隠さず保持し、full native feature設計へ分離。p246/q11はunclearedのまま、ここへの記録だけでclearしない。共有投影pending。
