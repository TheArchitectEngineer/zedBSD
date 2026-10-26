<!-- awesome-plan project=zedbsd record=ws291-p002a -->

# ws291-p002a amdrdna の executor に depth・index の draw・mip の texture を足す（mview が動くまで）

- 親: [WS291](../ws.md)。
- 状態: planned（2026-10-26 23:47 P2 新設、Q1 の Queue q1600）。
- 出典: 2026-10-26 ユーザー「機能を足してから比較（推し）」（Q1 経由、3D の比較の進め方の a/b/c の a）。
- 依存: p002 の Linux の基準（済み）。後続: p002 の zedBSD の計測。

## 範囲

src/drivers/gpu/amdrdna/vk（kernel の中の Vulkan の executor）に、mview が既定の scene で動くのに要る物を足す:

1. depth の attachment: D32_SFLOAT（と D24_UNORM_S8_UINT か D32_SFLOAT_S8_UINT のどれか 1 つ以上）の format の feature、depth の image（GFX10 の DB が読める layout）、render pass の depth の attachment（clear・load・store）、DB の register（Z_INFO・DEPTH_SIZE・READ/WRITE_BASE・DEPTH_VIEW・DEPTH_CONTROL・DEPTH_CLEAR）、pipeline の depth state（test・write・compare）の反映。
2. index の draw: vkCmdBindIndexBuffer（UINT32、UINT16）と vkCmdDrawIndexed（INDEX_TYPE・INDEX_BASE・DRAW_INDEX_2）。
3. mip の texture: mipLevels > 1 の image（level ごとの offset、copy・blit の level、image view の level の範囲、sampler の LOD）。

## 受け入れ

- host 試験（変えた所の短い物）と amd64 の vmunix・userland の build が warning 0。
- 実機（10.0.10.3）で mview の既定の scene（`--content=game --present=mailbox`、copies 1 と 1500）が動き、keiland-shot の PNG で Linux と同じ見た目（深度の前後が正しい、texture が出る）。`KWL SCANOUT direct=1`。
- 前後の数値（mview の SPIN・STAGES）を p002 に記録。

## 記録

- 2026-10-26 23:47 P2: 新設。
