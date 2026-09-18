# WS031 libvulkan の設計確認と、i915 executor の実装範囲（2026-09-18、読み取りのみ）

前回の調査メモ（`survey-libvulkan-path.md`）の言い方を訂正します。「Venus wire protocol の client」という表現は誤解を招きました。コードと README で確認した設計は次のとおりです。

## 1. 設計の確認

| 確認点 | コード上の根拠 |
|---|---|
| アプリには標準 Vulkan だけを見せる | README:「アプリは標準 Vulkan API を使います。zedBSD の GPU ioctl、resource ID、Venus wire をアプリへ公開しません」。`userland/base/vkdemo/` は標準 API だけで書かれ、別 OS の Vulkan 実装でもビルドできる |
| 公開 API | Vulkan 1.0 の core 137 command ＋ `VK_KHR_surface`／`display`／`swapchain`／`display_swapchain` の 18 command。ELF checker は 155 exports を照合（README）。dispatch 表には追加の拡張 entry も載っている（計 169） |
| ICD／loader ではない | `/lib/libvulkan.so` 単体で、layer も ICD 探索も無い。instance／device／queue、動的 handle、allocator callback、WSI（direct display、FIFO 順序）を userland で実装 |
| Venus から借りているのは番号 | `opcodes.h` 冒頭:「Numeric declarations selected from virglrenderer 1.1.0 Venus protocol … Maintained protocol IDs, no renderer implementation is included」。`LICENSE-PROTOCOL` に元の MIT 表示を保持。README:「実装は独立して記述し、Mesa、loader、virglrenderer の C 実装を移入していません」 |
| backend は GPU node ごとに選ぶ | `instance.c` が `/dev/gpu*` を走査し、capability set が合わない node は飛ばして他を探す（`instance.c:636-662`）。現在動いている backend は QEMU の virtio-vga-gl で、そこでは host の virglrenderer が同じ番号の stream を実行する。i915 では **kernel 内の executor**（`src/drivers/gpu/i915/vk/`）が同じ stream を解釈する設計 |
| kernel との境界 | 普通の GPU session／resource／map／submit／display の ioctl（`include/uapi/gpu*.h`、GPU core `src/drivers/gpu/gpu.c`）。権限、範囲、file と mapping の寿命は kernel 側が独立に検査（README と gpu.c:2540-2571, 4584-4628） |
| shader | アプリの SPIR-V をそのまま native 側へ送る（README、`resources-generated.inc:265`）。i915 では kernel executor が SPIR-V → EU 命令を生成する（`vk/spirv.c`、`vk/compile.c`、`vk/eu.c`） |
| CPU fallback | 無い。README:「CPU copy を coherent mapping の代用にしません」 |

つまり、**標準 Vulkan を実装する userland ライブラリ＋「番号だけ Venus と共通の」内部 stream＋GPU ごとの実行側**、という構成です。i915 で必要なのは、この内部 stream を実行する側（executor）と、GPU core の ops 表への接続です。

## 2. 「executor」の数と現状

i915 の executor は 1 個の device backend で、内部は opcode の範囲で 6 個の module に分かれています（`vk/cmd.c:362` `i915_vk_route`）。

| module | 担当 opcode | 役割 | 現状 |
|---|---|---|---|
| `cmd.c`（builtin） | 0–17, 19–20, 137–, 178–180 | instance／device／queue、version、reply stream の transport | transport 3 個（137、178、179）のみ実装。残りは「payload を読まずに受理」で、以後 stream がずれる |
| `res.c` | 21–34, 50–58, 70–79 | memory、buffer、image、view、sampler、descriptor | 8 個（allocate／free、bind×2、buffer／image の create／destroy） |
| `pipe.c` | 59–69, 80–84 | shader module、pipeline、layout、framebuffer、render pass | 4 個（shader module、graphics pipeline の create／destroy） |
| `cmdbuf.c` | 18, 85–136 | command pool／buffer、全 vkCmd*、queue submit | 12 個（pool／buffer 管理 7、submit、bind pipeline、bind vertex、draw、end render pass） |
| `sync.c` | 35–49 | fence、semaphore、event、query | 4 個（fence のみ） |
| `wsi.c`／`display.c` | （wire 外。GPU_DISPLAY_* ioctl 経由） | swapchain、present、flip | dispatch は EINVAL、flip は no-op、mode は 1920×1080 固定の stub |
| `spirv.c`／`compile.c`／`eu.c` | — | SPIR-V → EU 命令 | 少数の opcode（FAdd/FSub/FMul/Dot/Compose/Extract/Sample/ExtInst）のみ |

### 数で見た実装範囲（`tools/vk_opcode_survey.py` で自動集計）

| 区分 | wire opcode 数 | libvulkan が発行 | i915 executor に handler あり | vkdemo が使う | うち handler 無し |
|---|---|---|---|---|---|
| instance／device／queue | 21 | 11 | 1 | 13 | 12 |
| memory／bind | 14 | 10 | 4 | 10 | 6 |
| fence／semaphore／event／query | 15 | 14 | 4 | 6 | 3 |
| buffer／image／view | 9 | 9 | 4 | 6 | 2 |
| shader／pipeline／layout | 11 | 11 | 4 | 6 | 2 |
| sampler／descriptor | 10 | 10 | 0 | 8 | 8 |
| framebuffer／render pass | 5 | 5 | 0 | 4 | 4 |
| command pool／buffer | 8 | 8 | 7 | 6 | 0 |
| vkCmd* 記録 | 44 | 44 | 4 | 10 | 6 |
| version／transport | 8 | 8 | 3 | 0 | 0 |
| **合計** | **145** | **130** | **31** | **69** | **43** |

- 定義されている内部 opcode は 145 個、libvulkan が実際に発行するのは 130 個、i915 executor に handler があるのは 31 個です。
- 既存の標準アプリ `vkdemo`（画面表示と GPU readback を照合する受入用アプリ）が使うのは 69 個で、そのうち **43 個に handler がありません**。
- vkdemo はほかに WSI／display の 16 関数を使います。これは wire ではなく `GPU_DISPLAY_*`／scanout の ioctl を通り、i915 の ops 表では present／display／scanout が現在 NULL です。

vkdemo に必要で handler が無い 43 個:

- instance／device（12）: vkCreateInstance、vkDestroyInstance、vkEnumeratePhysicalDevices、vkGetPhysicalDeviceFormatProperties、vkGetPhysicalDeviceProperties、vkGetPhysicalDeviceQueueFamilyProperties、vkGetPhysicalDeviceMemoryProperties、vkCreateDevice、vkDestroyDevice、vkGetDeviceQueue、vkQueueWaitIdle、vkDeviceWaitIdle
- memory（6）: vkMapMemory、vkUnmapMemory、vkFlushMappedMemoryRanges、vkInvalidateMappedMemoryRanges、vkGetBufferMemoryRequirements、vkGetImageMemoryRequirements
- sync（3）: vkWaitForFences、vkCreateSemaphore、vkDestroySemaphore
- view（2）: vkCreateImageView、vkDestroyImageView
- layout（2）: vkCreatePipelineLayout、vkDestroyPipelineLayout
- sampler／descriptor（8）: vkCreateSampler、vkDestroySampler、vkCreateDescriptorSetLayout、vkDestroyDescriptorSetLayout、vkCreateDescriptorPool、vkDestroyDescriptorPool、vkAllocateDescriptorSets、vkUpdateDescriptorSets
- framebuffer／render pass（4）: vkCreateFramebuffer、vkDestroyFramebuffer、vkCreateRenderPass、vkDestroyRenderPass
- 記録（6）: vkCmdBindDescriptorSets、vkCmdCopyBufferToImage、vkCmdCopyImageToBuffer、vkCmdPipelineBarrier、vkCmdPushConstants、vkCmdBeginRenderPass

### handler 以外に直す必要があるもの

| 項目 | 内容 |
|---|---|
| capability set | executor は 156 byte の先頭に 1 を置くだけ（`vk/vk.c:160-176`、comment にも「統合時に確認」とある）。libvulkan は protocol の版、timeline 数、zedBSD の vendor suffix（strict queue、native quiescence）、`GPU_CAP_JOB`／`GPU_CAP_JOB_CAPACITY` を要求する（`context.c:141-159`、`instance.c:654-656`）。合わないと i915 node は黙って飛ばされる |
| 未実装 opcode の扱い | builtin が payload を消費せずに成功を返すため、以後の stream がずれる。未対応は「消費して明示的に失敗」にする必要がある |
| command 記録の配送 | libvulkan は大きな記録を opcode 180（vkExecuteCommandStreamsMESA）で送る。executor は未対応 |
| session の分離 | object 表が device 単位（`cmd.c:33, 137`）。bind の offset 未検査（`res.c:211, 261`） |
| shader compiler | 現在の SPIR-V subset では vkdemo の shader 全体は通らない可能性が高い（未検証）。今回の texture fixture は Mesa で事前生成した kernel なので、kernel 内 compiler の能力とは別 |
| 実行基盤 | 現在の handler は legacy driver の object／request／session に直接依存（`internal.h:399-429`）。parity 構成では attach が公開前に return し（`i915.c:271-280`）、`/dev/gpuN` が無い |

## 3. parity driver との接続で必要なもの

GPU core が backend に要求する ops 表（`include/drivers/gpu.h:119-`）と、legacy i915 が現在埋めている範囲（`i915.c:563-596`）:

| ops | legacy i915 | parity で必要な対応 |
|---|---|---|
| open／close／get_info | あり | session ＝ parity の VM＋context（RCS0）＋object 台帳を open ごとに持つ |
| resource_create／destroy、blob_create、resource_map、read／write | あり | parity の object（DMA backing）を GPU core の resource／mmap 契約へ。CPU アドレス／DMA／GPU VA の区別を維持 |
| get_capset | あり（内容が不足） | 上の capability set を正しく返す |
| command／`commands`（submit、drain）／`jobs`（reserve、commit、cancel、capacity） | あり | parity の request（execbuf 形）と完了（現在 polling）へ接続。完了通知は GPU core の completion へ |
| `recovery`（stop、fault、reset、isolate） | あり | parity の reset／wedge 処理へ |
| present、display、scanout 系 | **NULL** | LCD 実装（表示 A〜D）をこの ops の形に合わせて作る |

Intel GPU 向けの ioctl は GPU core 側に既にあり、不足しているのは「parity の device を ops 表へ登録して通常稼働させること」と「executor の中身」です。表示側に別の入口を新設する必要はありません。

## 4. 見積りの目安（作業の単位であり、回数の約束ではない）

1. **接続**：parity device の通常稼働（試験して撤収するモードは回帰用に残す）、ops 表への登録、正しい capability set、未対応 opcode の明示的失敗。ここまでで libvulkan が i915 node を受け入れ、vkCreateInstance〜vkCreateDevice が通る。
2. **オフスクリーン**：instance／device 12、memory 6、sync 3、command 記録の配送。compute か単純な graphics で buffer／image へ書いて readback。
3. **graphics pipeline**：view 2、layout 2、render pass／framebuffer 4、記録 6、SPIR-V subset の拡張。今回の RECTLIST fixture は固定 state なので、一般の pipeline state 生成は別に必要。
4. **texture**：sampler／descriptor 8、copy 系、barrier。
5. **表示**：display／scanout／present の ops と WSI 16 関数の kernel 側。LCD 受入の後。

vkdemo が実際に何を描くか（shader の中身、使う format）は未読なので、3 と 4 の深さは vkdemo を読んでから確定します。
