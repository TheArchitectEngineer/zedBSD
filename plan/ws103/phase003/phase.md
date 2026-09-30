<!-- awesome-plan project=zedbsd record=ws103-p003 -->

# ws103-p003: libvulkan の dedicated の import と記述の照合

- Parent: [WS103](../ws.md)
- Status: cleared（2026-09-30 夜、q510-i01）
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
3. 今の compositor（dedicated を使わない）が壊れないこと。**決定（実装の前、Q1）: 範囲の 3（dedicated の無い import を拒む）は p003 では入れず、
   compositor を dedicated に切り替える p004 で同時に入れる**（途中の壊れた状態を作らないため。p004 の範囲に移した）。
4. QEMU の Venus で app の起動（今の compositor）、boot test。
5. 規約の全文。

## 記録（2026-09-30 夜、q510-i01、メインのエージェント Q1）

### 変更（commit `619c72a9`）

- 公開の header: `include/libc/vulkan/vulkan_external.h` に `VK_KHR_get_memory_requirements2`・`VK_KHR_dedicated_allocation` の宣言と 1.1 の構造体 7 つ。
  `maintain-external.noct` の一覧を広げ、virglrenderer 1.1.0 の pinned の `vulkan_core.h` を取得し直して SHA-256（`8cb01233…`）を照らしてから生成。差は追加と最初の注釈の 1 行だけ。
- dispatch: `maintain-dispatch.noct` に 2 つの拡張の bit を足し、pinned の `vn_protocol_renderer_defines.h`（SHA-256 `ff73828c…`、同じ commit から取得）で
  `dispatch-table.inc`・`api-commands.tsv` を再生成（command 170 → 173）。`opcodes.h` は tree と一致（前回の WS035 は round trip でしか確かめていなかった）。
  `API-PROVENANCE.md` に節を追加。
- `internal.h`: device の bit `VULKAN_DEVICE_MEMORY_REQUIREMENTS2`・`VULKAN_DEVICE_DEDICATED_ALLOCATION`、`struct vulkan_memory` の `dedicated_image`、`vulkan_dedicated_check` の宣言。
- `instance.c`: device の拡張の一覧に 2 つ（全ての renderer で local に答える）。`device.c`: 有効化と、dedicated は requirements2 を要する依存。
- `resources.c`: `vkGet{Buffer,Image}MemoryRequirements2KHR`（1.0 の問い合わせ、`VkMemoryDedicatedRequirements` は prefers・requires とも偽）、
  `vkGetImageSparseMemoryRequirements2KHR`（1.0 の答えを包む）、`resource_bind` の守り（dedicated の import の memory には、その image だけを offset 0 で。他は
  `VK_ERROR_OUT_OF_DEVICE_MEMORY` で拒む。規格では利用者の誤りで、bind が返せる失敗はこれだけ）。
- `memory.c`: `vkAllocateMemory` は dedicated の情報に拡張の有効化を求める。`memory_import_image_fd` は `VkMemoryDedicatedAllocateInfo` の image があれば
  `memory_dedicated_check` で照らし、成功なら `dedicated_image` を記録。
- `dedicated.c`（新規）: `vulkan_dedicated_check`。副作用の無い照合（capability の tiling が linear・usage が SAMPLED を含む、image が 2D・linear・mip 1・layer 1・sample 1、
  大きさ、形式のバイト順（unorm と sRGB）、`allocationSize` が要求の大きさと等しい、memory type、要求の大きさが allocation の中、offset と rowPitch）。host で試せるよう分けた。
- dedicated の無い import は今のまま通す（上の決定、p004 で拒む）。

### 確かめ

| 基準 | 結果 |
| --- | --- |
| 1 build | libvulkan: warning 0。基準の image・passthrough の image: rc 0。基準の image の warning 478 は全て外部の package（openssl・openssh）と Noct の既存の 1 件で、compositor・libvulkan は 0 |
| 2 host の試験 | `sh plan/ws103/tests/run-dedicated-host.sh`: 通常と ASan/UBSan の両方で PASS（18 件: 正しい 2 件は通し、幅・高さ・形式・stride・offset・allocation の大きさ・tiling・usage・import の大きさ・memory type 2 件・image の tiling・layer・level・sample・rowPitch の 16 件を拒む）。bind の守り（wire を通る）と、dedicated の import の端から端までは、compositor が dedicated を使う p004 で確かめる |
| 3 今の compositor が壊れない | 上の決定により dedicated の無い import は不変。下の 4 で確かめた |
| 4 QEMU の Venus と boot test | `criteria.sh … C1 C2`: C1 p126 PASS、C1 c1-boot-shutdown PASS、C2 PASS（14/14）。boot test PASS（`build/ws103/p003-boot/login.png`） |
| 追加: 5330 の passthrough | libvulkan は全ての app に効くので確かめた。`c5-hw.sh build/ws103/p003-pt.img build/ws103/p003-hw 3`: PASS（34 回、最大 55 ms）、`ZWL DISPLAY … i915 (Gen12 Xe) 1920x1080 60011` |
| 5 規約 | 変更の範囲を `plan/coding-style.md` の checklist で見直した。生成物（header・dispatch の表）は道具の出力のまま |

準備（範囲の外、ユーザーの許可「消して作り直してOKです」）: 別の checkout を指す CMake の cache（Noct の build directory、7 つの package、package の cross の wrapper）を
消して作り直し、既定の `config.mk` の image の build が通るようにした。途中で見つけた toolchain の lock と libcxx の複写の問題は [BUG-126](../../bugs/BUG-126.md)。
