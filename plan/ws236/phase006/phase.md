<!-- awesome-plan project=zedbsd record=ws236-p006 -->

# ws236-p006: 実機の確かめと直し

Parent: [WS236](../ws.md)
Status: planned（T1 の実機の結果（p003 の probe と p005 の依頼）を受けて始める）
Phase disposition: normal

## 範囲

- p003 の probe（R1）の判定: 合えば Main・High のまま、外れれば `src/drivers/gpu/bcm2711/vulkan-video.c` の `VIDEO_BASELINE_ONLY` を 1 にしてベータ2 は Baseline だけ（design §5）。
- p005 の T1 の依頼の結果（vkvideo-probe の hash、Video Player の `backend=vulkan-video`・画・CPU、seek）で見つかった不具合の直し。
- 長時間（耐久）は別枠でユーザーに確かめてから。
