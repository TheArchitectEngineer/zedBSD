<!-- awesome-plan project=zedbsd record=ws103-p005 -->

# ws103-p005: WSI が present ごとに新しい fence を送る

- Parent: [WS103](../ws.md)
- Status: in-progress
- Disposition: normal
- Queue: q512-i01
- Design: [design.md](../design.md) §2.4（WSI の側）、§3 の p005

## 範囲

`userland/desktop/libvulkan/wsi-swapchain.c`（必要なら同じ library の fence の file）だけ。

- Wayland の target を含む present の job は、compositor へ送る共有の fence を present ごとに新しく作る（slot の fence を再利用しない）。VK_KHR_display だけの job は今どおり再利用。
- 新しい fence の作成の直後の host の `vkResetFences` を省く。
- present ごとの VkFence と `vkGetFenceFdKHR` の fd の 2 つを、全ての道（作成の後・submit の前の失敗、device の喪失の待ちの失敗、`present_drain` による teardown）で閉じる。
- protocol・compositor・kernel は変えない（compositor は p006 まで今の世代の照合のまま）。

## 完了の基準

1. build（warning 0）。
2. Wayland の present ごとに別の kernel の fence が送られ、前の fd の fence は次の present の後も reset されない（guest の試験）。
3. 失敗の道で fd と VkFence が漏れない（code の見直し。試せる道は試す）。
4. QEMU の Venus: 窓の app（C1・C2、acquire-fence の試験、forge-guest の wltest）、compositor の frame の間隔が変わらない。
5. boot test、5330 の passthrough の smoke（i915 には fence が無いので道は通らない。回帰の確かめ）。
6. 規約の全文。

## 記録

（実行中）
