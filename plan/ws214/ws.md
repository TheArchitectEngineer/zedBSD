<!-- awesome-plan project=zedbsd record=ws214 -->

# WS214: 窓をディスプレイの重ね合わせ（HVS の plane）に載せる（Pi 4）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。kernel・libvulkan の段は main。2026-10-13 ユーザー「いや、これ以上詰めなくていいです。」で描画の最適化を終え、compositor の段 p005・p006 は未投入で終了）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG008
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Pi 4（bcm2711）で、窓の移動（drag）・dock と undock の拡大縮小の間、GPU が窓の中身を描き直さない。HVS（ディスプレイの合成器）の plane に窓の絵を載せ、位置・大きさだけを変える。i915・Venus・Linux・FreeBSD では今どおりの合成（fallback）。

## ユーザーの決定

- 2026-10-12（由来）:「画面更新の分割って、もっと高度化できそうですか？無理ですか？」→ Q1 の案 1〜4 に「ぜんぶやりましょう。」この WS は案 2（窓を HVS の plane に）。案 1・3 は WS211、案 4 は WS210 p013。
- 2026-10-12: 窓の前後の入れ替えは範囲の外。
- 2026-10-12（クリック）「WS214 にも広げる」: 公開の文書の無い HVS5 の scaled の plane の値も WS141 と同じ規則（値はハードウェアの事実として使い、名前・配置・comment は写さない）で使う。値ごとの出典は [hvs-values.md](hvs-values.md)（[Guardrail](../guardrail.md) の GPL の節が参照）。
- 2026-10-12: p003b の UAPI（COMMIT・OVERLAY_INFO・HAS_OVERLAYS、WAIT を眠る形に）の承認。
- 2026-10-13「いや、これ以上詰めなくていいです。」（描画の最適化の終了）

## 結果

- p003a（kernel: display の op の gate 外し、状態から作る list と 3 slot の差し替え、coalesce、IRQ の採用の記録と waitq、WS141 p016 B-k2）は main（4438fc682）。
- p003b（UAPI、overlay の unscaled・scaled・LBM、帯域の予算と ENOSPC、値の出典の表と GPL の監査）は main（114e7c68c）。
- p004（libvulkan の overlay の plane、plane ごとの present、切り替えの保留、位置だけの fast path）は main（046a376af）。
- いずれも host 試験と build まで。Pi の `hvs-load` の帯域の実測と T1（i915・Venus）の回帰は未実施のまま閉じた（2026-10-28 に commit が main の履歴にあることを `git merge-base --is-ancestor` で確かめた）。

## 制限・移管

- compositor の段（p005 drag の間の窓と bar の plane、p006 dock・undock の HVS の拡大縮小）は案のまま未投入で終了。目標の「drag・dock の間 GPU が窓を描き直さない」は compositor の側では未達。
- p002 の規約の全文と新しい HVS の code の GPL との類似の監査は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」、2026-10-27 の精査で一括の見直しに読み替え）。
- 回帰に残す試験（`vulkan-overlay-host.sh`、`display-p003a-host.sh`）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws214-p001 | 調べと設計（HVS の plane の能力と制約、口、compositor の載せ方、fallback、試験） | planning（第 3 版、review-2 を反映）のまま閉じた。実装は p003a〜p004 |
| ws214-p003a | kernel（承認不要）: gate 外し、状態から作る list、coalesce、B-k2 | in-progress のまま閉じた（main 4438fc682、host・build 済み、Pi・T1 は未実施） |
| ws214-p003b | kernel（UAPI の承認の後）: COMMIT・OVERLAY_INFO・WAIT、overlay、帯域の予算 | in-progress のまま閉じた（main 114e7c68c、host・build 済み、Pi・T1 は未実施） |
| ws214-p004 | libvulkan: overlay の plane と present | in-progress のまま閉じた（main 046a376af、host・build 済み） |
| ws214-p005 | compositor: drag の間の窓と bar の plane | 案のまま未投入で終了 |
| ws214-p006 | compositor: dock・undock の HVS の拡大縮小 | 案のまま未投入で終了 |
| ws214-p002 | 規約の全文の見直しと GPL との類似の監査 | WS の外の一括の見直しへ |

## 記録の所在

値の出典の表 [hvs-values.md](hvs-values.md) は phase003/ から WS の直下へ移す（Q1 の OK の後。license の出典の記録として残す）。他の Phase の計画・結果・試験は削除する。git の commit `fb1f6afee` の `plan/ws214/` にある。Queue の履歴は [plan/history](../history/index.md)。
