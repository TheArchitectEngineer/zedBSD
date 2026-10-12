<!-- awesome-plan project=zedbsd record=ws214 -->

# WS214: 窓をディスプレイの重ね合わせ（HVS の plane）に載せる（Pi 4）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの承認）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P2（p001 の調べと設計から）
Resume point: p001 の設計の第 1 版（[phase001/phase.md](phase001/phase.md)、2026-10-12 P2）を design-reviewer に掛ける。未決（UAPI の追加の承認、glass の blur・中身を操作の間止める見た目、undock の拡大、前後の入れ替えを範囲から外す件、kernel の段の担当と WS141 p016 B-k2 との順序）は p001 の 9 節。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「画面更新の分割って、もっと高度化できそうですか？無理ですか？」→ Q1 の案 1〜4（1 damage を複数の四角のまま描く、2 窓を HVS の plane に載せる、3 CPU の記録・decode の使い回し、4 app の present の部分化）にユーザー「ぜんぶやりましょう。」。この WS は案 2。1 は WS211、3 は WS211、4 は WS210 p013。

## 目標

Pi 4（bcm2711）で、窓の移動（drag）・dock と undock の拡大縮小・窓の前後の入れ替えの間、GPU が窓の中身を描き直さない。HVS（ディスプレイの合成器）の plane に窓の絵を載せ、位置・大きさ・順だけを変える。GPU は窓の中身が変わった時だけ動く。

## 範囲（p001 で決める）

- HVS の plane の数・拡大縮小・alpha・format・dlist の更新の制約（bcm2711 の display driver、src/drivers/gpu/bcm2711/ の display-*.c）。
- kernel・libvulkan の口（今の cursor の plane と同じく VK_KHR_display の plane か、private の口か）。HAL・UAPI の変更が要るなら差分ごとの承認。
- compositor（userland/desktop/wayland/）がどの窓を plane に載せるか（glass・影・角の丸み・blur の扱い、重なり、damage との両立、WS213 の描画の thread・cursor の thread との関係）。
- i915・Venus・Linux・FreeBSD では今どおりの合成（plane が無い時の fallback）。
- 試験: host、T1（回帰）、Pi（drag・dock の fps、GPU の時間）。

## 完了の条件（仮）

Pi 4 で窓の drag と dock・undock のアニメが 60fps（実機の log とユーザーの確認）、その間の GPU の時間が窓の中身の変化分だけ。i915・Venus で回帰が無い（T1）。

## Phase

| Phase | 内容 | Status |
| --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験）→ design-reviewer | planning（第 1 版、review の前） |
| p003 | kernel（bcm2711）: 状態から作る display list、overlay（unscaled・scaled・LBM）、hold、帯域・clock の予算、console の省略、overlay の commit の lock、UAPI（承認の後） | 案（p001 の 7 節、未投入） |
| p004 | libvulkan: VK_KHR_display の overlay の plane、複数の swapchain の 1 回の present を 1 つの commit に、位置だけの fast path | 案（未投入） |
| p005 | compositor: drag の間の窓の plane（焼き、入る・戻る、C の follow、fallback） | 案（未投入） |
| p006 | compositor: 上の帯の plane と dock・undock・pull back の HVS の拡大 | 案（未投入） |
| p002 | 規約の全文の見直し（最後に行う）と、新しい HVS の code の GPL との類似の監査 | planned |
