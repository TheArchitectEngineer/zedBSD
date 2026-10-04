<!-- awesome-plan project=zedbsd record=ws113-p011 -->

# ws113-p011: i915 の 2 つ目の出力（Keiland の指示での scanout）

Parent: [WS113](../ws.md)
Status: planned（2026-10-05 q702 の計画で p002 から分けた）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue: none
目安: 4〜6h（実機の比重が大きい）

## 目的

GOP の出力先とは別の接続済みの出力（初回の fixture は 5330 の eDP + HDMI、D-PORT）を、Keiland が libvulkan の経路で claim・present した時だけ、2 つ目の pipe で同時に scanout する（Guardrail の scanout の規則、[契約の確定](../phase001/contracts-beta2.md) D-GOP・D-RELEASE）。

## 範囲

- 単一の `rd`・lease・plane の状態を出力ごとに分ける（pipe・transcoder・DPLL・plane・watermark・帯域の割り当てを接続の集合で validate、他の出力が使う資源を奪わない）。2026-09-20 の診断（eDP pipe A/DPLL0 + HDMI pipe B/DPLL1、[report](../../ws031/handover/expert-reports/report-e123b-dual.md)）の手順を production の modeset に。
- `GPU_DISPLAY_CLAIM`（GOP の出力先でない display_id）で modeset の準備、最初の `PRESENT` で点灯、`RELEASE` で消灯（D-RELEASE）。GOP の出力先の RELEASE は console に戻る（今のまま）。
- 切断: その出力の lease と present を失効させ、新しい submit を拒み、scanout が読む buffer を退役するまで保持。残る出力は続ける（H07・H08）。
- mode は列挙した native の mode だけ（retiming は範囲外、偽の成功を返さない）。

## 受け入れ

実機（5330、eDP + HDMI、i915 の lock）: 小さい native の probe（`plan/ws113/tests/`）で eDP と HDMI に別々の lease で同時に present し、両方の画面に別の絵（ユーザーの目視か写真）、HDMI を抜いても eDP が続き、差し直して claim し直すと HDMI が戻る。RELEASE で HDMI が消える。host の試験（資源の割り当て・lease の寿命）、build warning 0（vmunix の kernel include check まで）、規約。

## 依存と衝突

依存: p002（inventory・規則）。衝突: WS051（DP-alt は同じ modeset の経路を使う）、WS075・WS084 の i915 の Phase。実機の時間はユーザーと（p008 とまとめてもよい）。

### D-LIMIT の反映（2026-10-05）

資源（pipe・transcoder・DPLL・帯域）が足りなくて 2 つ目（以降）を点けられない時の claim は `ENOSPC`（p002 の暫定の「一度に 1 つ」の `ENOSPC` を、本当の資源の検査に置き換える）。実機の受け入れに「3 つ目の出力（DP-alt があれば）で ENOSPC、他の出力は続く」を可能なら足す。
