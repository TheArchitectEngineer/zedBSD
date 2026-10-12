<!-- awesome-plan project=zedbsd record=ws214 -->

# WS214: 窓をディスプレイの重ね合わせ（HVS の plane）に載せる（Pi 4）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-12 Q1 作成、ユーザーの承認）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: P2（p001 の調べと設計から）
Resume point: p003a は実装・host 試験・build 済み（main 4438fc682、[phase003a/phase.md](phase003a/phase.md)）、clear は Pi（焼き直しの後、受け入れ 3）と T1（i915・Venus）の後。p003b（UAPI の承認済み 2026-10-12、4.2 のとおり）を P2 が実装中。
<!-- awesome-plan-current:end -->

## 由来

2026-10-12 ユーザー:「画面更新の分割って、もっと高度化できそうですか？無理ですか？」→ Q1 の案 1〜4（1 damage を複数の四角のまま描く、2 窓を HVS の plane に載せる、3 CPU の記録・decode の使い回し、4 app の present の部分化）にユーザー「ぜんぶやりましょう。」。この WS は案 2。1 は WS211、3 は WS211、4 は WS210 p013。

## 目標

Pi 4（bcm2711）で、窓の移動（drag）・dock と undock の拡大縮小の間、GPU が窓の中身を描き直さない。HVS（ディスプレイの合成器）の plane に窓の絵を載せ、位置・大きさだけを変える。GPU は窓の中身が変わった時だけ動く。

## 範囲（p001 で決める）

- HVS の plane の数・拡大縮小・alpha・format・dlist の更新の制約（bcm2711 の display driver、src/drivers/gpu/bcm2711/ の display-*.c）。
- kernel・libvulkan の口（今の cursor の plane と同じく VK_KHR_display の plane か、private の口か）。HAL・UAPI の変更が要るなら差分ごとの承認。
- compositor（userland/desktop/wayland/）がどの窓を plane に載せるか（glass・影・角の丸み・blur の扱い、重なり、damage との両立、WS213 の描画の thread・cursor の thread との関係）。
- i915・Venus・Linux・FreeBSD では今どおりの合成（plane が無い時の fallback）。
- 試験: host、T1（回帰）、Pi（drag・dock の fps、GPU の時間）。

## 完了の条件（仮）

Pi 4 で窓の drag と dock・undock のアニメが 60fps（窓の前後の入れ替えは範囲外、2026-10-12 ユーザー）（実機の log とユーザーの確認）、その間の GPU の時間が窓の中身の変化分だけ。i915・Venus で回帰が無い（T1）。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験）→ design-reviewer | planning（第 3 版、review-2 を反映、Q1 の確かめの前） | — |
| p003a | kernel（core と bcm2711、担当 1 人、WS141 p016 B-k2 を含む、**承認不要**）: display の op の gate 外し（table_lock・pin・`adoption_wait`）、状態から作る list と 3 slot の差し替え、primary も coalesce（settle・EBUSY をやめる）、IRQ の採用の記録と waitq、既存の op（PRESENT・PRESENT_SYNC・CURSOR_*）を状態の変更に、借りの thread の文脈の解放、lease の失効、copy の経路、console の省略、underrun の計数。UAPI と WAIT は今のまま | 実装と host・build 済み（4438fc682）、Pi・T1 待ち | —（承認不要） |
| p003b | kernel（p003a と同じ担当、**UAPI の承認の後**）: UAPI（COMMIT・OVERLAY_INFO・HAS_OVERLAYS、WAIT を眠る形に、番号を controller の 1 本に）、overlay（unscaled・scaled・LBM）、帯域の予算と ENOSPC、値の出典の表と GPL の監査、Pi の帯域の実測（`hvs-load`） | in-progress（P2、UAPI の承認済み 2026-10-12） | p003a、UAPI の承認（ユーザー、p001 の 9 の 1） |
| p004 | libvulkan: overlay の plane、plane ごとの present、切り替えの保留（`min_primary`、置き換え、期限と単独の flush）と primary との 1 commit、隠す約束、位置だけの fast path、借りと acquire、失敗した保留は primary ごと捨てる | 案（未投入） | p003b、WS141 p016 B-u の Pi の受け入れ |
| p005 | compositor: drag の間の窓の plane と上の帯（bar）の plane（焼き、入る・戻る、C への持ち主の移し、fallback） | 案（未投入） | p004、WS213 p004 の clear、ユーザーの判断（glass の blur・中身の停止） |
| p006 | compositor: dock・undock・pull back の HVS の拡大縮小 | 案（未投入） | p005、p003b の帯域の実測 |
| p002 | 規約の全文の見直し（最後に行う）と、新しい HVS の code の GPL との類似の監査 | planned | p003a〜p006 |
