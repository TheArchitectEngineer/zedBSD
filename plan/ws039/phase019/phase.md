# ws039-p019 desktop の描画の速さ（app の window を開く animation が 1 fps 未満）

- 親: [WS039](../ws.md)、状態: planned（P5 が p007 の後に）
- 起点（2026-10-16 夜 ユーザー）:「appの起動時のウィンドウが開くアニメーションが1fpsも出ていないですので、高速化は必要です。」
- 範囲（案）: RX 5600 XT の desktop で、window の開閉の animation・App Home の animation の 1 frame の時間の内訳を測り（compositor の KWL の frame の記録、executor の run の時間、GPU の待ち・fence、CPU の窓を通る写し、shader の compile の cache の有無）、遅い所を直す。目標は i915（5330）と同じく animation が滑らかに出る事。
- 実機: 10.0.10.3（P3 と時間を分ける）。
