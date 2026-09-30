<!-- awesome-plan project=zedbsd record=ws103-p003 -->

# ws103-p003: libvulkan の dedicated の import と記述の照合

- Parent: [WS103](../ws.md)
- Status: in-progress
- Disposition: normal
- Queue: q510-i01
- Design: [design.md](../design.md) §2.3（libvulkan の変更 1〜5）、§3 の p003

## 範囲

`userland/desktop/libvulkan/` だけ。

1. device の拡張 `VK_KHR_get_memory_requirements2`（1.0 の問い合わせを local に包む）と `VK_KHR_dedicated_allocation`（`VkMemoryDedicatedRequirementsKHR` を返し、
   dedicated の情報は wire に載せず local で消費する）。拡張の追加に伴う `dispatch-table.inc`・`exports.map`・`api-commands.tsv`・`instance.c` の `available[]`。
2. `memory_import_image_fd` で、`VkMemoryDedicatedAllocateInfo`（image が非 NULL）があるとき、kernel が返した記述とその image を照らす:
   2D・mip 1・layer 1・sample 1・linear、extent、format、`vkGetImageSubresourceLayout` の offset と rowPitch、`allocationSize` が image の要求の大きさと等しいこと、
   kernel の記述の `tiling` が `GPU_IMAGE_LINEAR`、`usage` が `VK_IMAGE_USAGE_SAMPLED_BIT` を含むこと。合わなければ alias を捨てて `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
3. dedicated の情報の無い image の capability の import は拒む。
4. 照合した memory に照合した image を記録し、`resource_bind` で別の image か offset 0 以外の bind を拒む。

範囲の外: compositor（p004）、fence（p005）、HAL、toolchain。

## 完了の基準

1. amd64 の build が warning 0。
2. host の試験（新規、`plan/ws103/tests/`）: 偽の記述を拒む（幅・高さ・形式・stride・offset・大きさ・tiling・usage のそれぞれ）、正しい物は通る、dedicated の無い import を拒む、
   別の image の bind を拒む。host で走らせる方法が無ければ guest の試験にし、その旨を書く。
3. 今の compositor（dedicated を使わない）が壊れないこと: p004 までの間、compositor は dedicated を使わないので、3 の「拒む」を入れると今の compositor の import が失敗する。
   **p004 と同時に入れるか、p003 では拒むを入れず p004 で入れる**かを実装の前に決め、ここに書く。
4. QEMU の Venus で app の起動（今の compositor）、boot test。
5. 規約の全文。

## 記録

（実行中）
