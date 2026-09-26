<!-- awesome-plan project=zedbsd record=ws048p004 -->

# ws048-p004: 非 coherent な DMA（uncached の対応付け）

Phase ID: `ws048-p004`
Parent: [WS048](../ws.md)
Status: planned（**HAL の承認待ち**）
Queue: —
HAL の承認: **要る**。hal.h に `hal_pmem_map_uncached`・`hal_pmem_unmap_uncached` を足す（design.md §6、差分 [proposed/hal-pmem-uncached.diff](../proposed/hal-pmem-uncached.diff)）
依存: p001

## 範囲（承認の後）

- hal.h の 2 関数と各 port の実装（arm64 は MAIR の Attr3 = Normal non-cacheable と uncached の窓。他は `HAL_ERR_UNSUPPORTED`）。
- `kern_pmem_map_uncached`・`kern_pmem_unmap_uncached`（`src/kern/pmem.c`）。
- `src/drivers/generic/dma.c`: 非 coherent な device の `alloc_coherent`・`free_coherent`・`drv_dma_map`・vector。

## 受け入れ条件

1. 承認（ユーザーの差分ごとの事前承認）。
2. host 試験: 非 coherent な device で uncached の番地を返し、`drv_dma_map`・vector が動く。coherent な device の振る舞いは変わらない。
3. QEMU raspi4b の kernel の自己試験か gdbstub で、uncached の窓に書いた値が direct map から（cache を invalidate した後）見えること。
4. rpi4 と amd64 の `make -j16` が warning 0、`BOOT_MODE=raspi4b plan/tools/boot-test.sh` と amd64 の `boot-test.sh` で login prompt。
