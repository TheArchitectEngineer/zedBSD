<!-- awesome-plan project=zedbsd record=ws269-p010q -->

# ws269-p010q: 実機の前の点検（cache・MMU・SLC・順・時間切れの後）

- ID: ws269-p010q（親 [ws269](../ws.md)）
- Status: in-progress（q1395-i01 で読みの点検と 1 つの直し。実機は未）
- Disposition: normal
- Queue: q1395 / q1395-i01、q1398 / q1398-i01、q1401 / q1401-i01（2026-10-20 P1）

## 範囲

実機で最初に出やすい誤りを、全部の kick の道（compute（vk-queue）・render-job・transfer-job・render check・transfer check・job check）について、mainline（Linux v7.3 `drivers/gpu/drm/imagination/pvr_*.c`）と Mesa 26.2.5 の winsys に照らして読む。

## 点検の表（2026-10-20 P1、読みによる）

| 点 | mainline・Mesa | zedBSD の今 | 判定 |
| --- | --- | --- | --- |
| CPU が書く program・command・set・vertex・stream の buffer が GPU から見えるか | Mesa は CPU_ACCESS の bo を書き、mainline は bo を write-combine の map で | `powervr_buffer_create` の buffer は uncached の kernel の view（`buffer->view`）だけで書く。render-job・transfer-job・render-context・render check は全部 view 経由（`put_words`・`view_put32`） | 足りている |
| FW の CCB・context・timeline の object | mainline は FW の object を uncached に | `powervr_object_create(…, POWERVR_MIPS_UNCACHED)`、読み書きは `powervr_object_get32/put32` | 足りている |
| GPU が書いて CPU が読む readback・fence・timeline | Mesa は CPU_ACCESS の map を読む | readback は uncached の view、timeline は uncached の FW の object。CPU の cache に古い line が残らない（cached の alias で読まない。compute check だけが cached の alias を読み、最後に invalidate している） | 足りている |
| GPU の SLC | mainline は SLCFLUSHINVAL（KCCB 105）を定義だけで送らない、Mesa は CPU が読む bo を SLC の bypass に（winsys の flag） | CPU・display が触る buffer は全部 `mappable=1` で map し、`powervr_gpu_buffer_flags` が SLC bypass（PM の free list の page だけ SLC あり、CPU は触らない） | 足りている |
| page table の更新の後の MMU cache の flush | `pvr_mmu_flush_request_all`・`pvr_mmu_flush_exec`（MMU_CACHE の KCCB、全部の cache、sync の word を待つ）を map と unmap の後 | `powervr_resource_make`・`powervr_session_program_map`・unmap の後に `powervr_command_mmu_flush`（MMU_CACHE 102、flags 0x1f、sync の word） | 足りている |
| kick の前の書き込みの順 | `pvr_cccb_send_kccb_kick` の前の `wmb()` | `send_command` の前の `kern_io_barrier()`（読み書きの両方の順）。CCB の write offset は kick の data（`client_woff_update`）で FW に渡す、mainline と同じ | 足りている |
| 時間切れの後の次の kick | mainline は faulty な context の job を FW に出さない（`pvr_queue` の faulty の entity） | **不足**: `powervr_session_kick_render`・`powervr_session_kick_transfer`・`powervr_session_kick_compute` が device の quarantine・session の stopping・lost・isolated・abandoned を見ずに kick していた（empty job の `powervr_render_submit` だけが見ていた） | **直した**（下） |
| 時間切れの後の memory | mainline は job の bo を fence まで hold | render-job・transfer-job・render check・transfer check・job check は時間切れで session を abandoned にし、memory を返さない（core に残す）。vk の session も abandoned で以後の submit は EIO | 足りている |
| FW の reset 通知（105）の後 | session の context を lost に | `handle_notice` が compute・render・transfer の identity で session を lost に、段の行 | 足りている（今回の直しで lost の session の kick も止まる） |

## 直し（q1395-i01）

- `render-queue.c`: `kick_refused`（queue の lock の下で `render->faulted`・`session->stopping`・`lost`・`isolated`、それに `session->abandoned`）を足し、render・transfer・compute の kick の頭で EIO を返す。

## 確かめ（2026-10-20）

| 確かめ | 結果 |
| --- | --- |
| VF2・riscv64-virt・amd64 の vmunix | rc=0、warning 0 |
| 試験の image の config（render check・transfer check・job check） | rc=0 |
| p009 run.sh | 175 passed, 0 failed |
| p010 run.sh | 全部 0 failed |

## 未確認（実機で見る点）

- uncached の view で書いた program を GPU の USC・PDS の cache が古いまま持つか（同じ place の program を作り直す時。今は job ごとに新しい buffer を map するので place は変わり得るが同じになることもある）。mainline も PDS・USC の code の cache を明示に invalidate しない（推測: FW が kick ごとに）。
- SR の H32/L32 の順（p010n）。

## 後半（q1398-i01）: vkstage・vkdemo の frame の host 試験

p009 の executor の host 試験（`tests/p009/frame-host.c` の `frame_scene`・`replies_scene`、`vk-host-test.c`）に、vkstage の draw・sample・depth と vkdemo（texture の upload、depth、push constant の時間 0.0 と 1.0 の 2 frame）の frame を足した。`userland/tests/vkstage/main.c`・`userland/tests/vkdemo/renderer.c` の呼ぶ順（buffer・image・pass・pipeline・pool・fence・upload・draw・copy out・barrier、submit の後の fence と command buffer（vkdemo は pool）の reset）を、libvulkan の encode（`pipeline.c` の static viewport・depth state、`commands.c` の render pass の clear、barrier、copy）のとおりに組んだ。vkdemo に uniform buffer は無い（set は combined image sampler だけ）。kick の stand-in で job の draw の数・頂点数・colour の load/store と clear・depth の有無と load/store/clear 値・set・push の中身・viewport を見る。

- 見つけた誤り: pipeline の static な viewport・scissor（vkstage・vkdemo）が使われず、draw の viewport が 0 のままだった（`render-job.c` は command buffer の set した値だけを見ていた）。`render-job.c` の `draw_place` で、pipeline が dynamic にしていない時は pipeline の値を使うように直した。p010 の job-host-test に、static な pipeline で draw 側を空にした job の確かめを足した（既存の job は dynamic にした）。
- 見つからなかった物: job の数・順（upload の T、R、copy out の T）、depth の ZLS の入力（CLEAR、DONT_CARE、1.0）、set、vkdemo の push の更新は期待どおり。

| 確かめ（2026-10-20） | 結果 |
| --- | --- |
| p009 run.sh | 509 passed, 2 failed（2 つは Keiland の frame の古い期待で、P2 の q1397 で直す物） |
| p010 run.sh | 全部 0 failed（job-host-test 19/0） |
| VF2 の vmunix | rc=0、warning 0 |

## q1399-i01（P2、2026-10-20）: P1 の job が P2 の関数を約束どおりに使っているか

P1 の render-job.c・transfer-job.c・render-check.c を読み、P2 の関数（render-program・render-load-program・transfer-program・render-draw・render-depth・data-program）を約束どおりに呼んでいるかを点検した。見た観点は次のとおり。
- 引数の単位（pixel・tile・byte）
- address の基準（heap の base からの offset か、device の address か）
- temps と shared の数が PDS の task word に渡っているか
- PBE の word の順と EOT の即値
- clip と extent
- 継続の job の load
- ZLS の stride

| 点 | 呼ぶ側（P1） | 約束（P2） | 判定 |
| --- | --- | --- | --- |
| **render の CLEAR の値** | render-job.c `job_background`: `powervr_render_clear_shared(color->clear[0])` | clear_shared は「1 つの pack した word」を shared 0 に入れる。ところが render-pass.c は `clear[]` に VkClearColorValue の float の bit をそのまま入れていた | **食い違い（P2 の側の誤り、直した）**。CLEAR の pass の背景が、赤の float の bit（1.0 なら 0x3f800000）を色として書いていた。render-pass.c が `clear[]` を format の clear の word（`powervr_format_clear_words`、U8888 は 1 word に pack）にするよう直した。render-job.c は変えずに正しくなる |
| PBE と EOT | `powervr_render_pbe_words(&surface, &job->area, state, pbe)` → `powervr_render_eot_usc(state)` | state の 2 word を EOT の即値に。area は pixel | 合う |
| event・kick の task | `powervr_render_event_data(usc_base + USC_EOT, temps)`、`powervr_render_kick_build(usc_base + …, temps)` | USC の heap の base からの offset と、その program の temps | 合う（`usc_place - POWERVR_USC_HEAP_BASE`） |
| render の LOAD・継続の job | `powervr_render_load_usc(format)`・`load_words(&color->view)`・`load_shared(general の device address)`、shared 8 | words は device address、shared は 8 | 合う。継続の job は `load_op = LOAD` で同じ道を通る（p009 の keiland の試験で load・store を確認） |
| background の shared の数 | LOAD は 8、CLEAR は 1、draw の vertex の shared の方が大きければそれ | background_words の shared_registers | 合う |
| draw の PDS | vertex の task は `powervr_pds_task_word(vertex_usc, temps)`、fragment は `kick_build(fragment_usc, temps)`。shared の program は `data_resolve`・`data_shared_build`、その data の words を ppp・vdm に | offset は heap の base から | 合う |
| coefficient の program | data を `DRAW_PDS_COEFFICIENT` に、code を data の後（16 byte 単位）に置き、ppp には data の offset を渡す | ppp の `coefficient_offset` は data の offset（code は data の後） | 合う |
| viewport・scissor | `powervr_draw_viewport_words`・`powervr_draw_scissor_words(viewport, scissor, table, region_clip)`。pipeline の static な物は q1398 で直った | pixel、scissor の表の index は draw の番号 | 合う |
| ZLS | `powervr_render_zls_words(&job->depth)` の control・base・background・stride を fragment の setup に | stride は layer の byte | 合う |
| **vertex の DMA の words** | render-job.c `job_draw`: `dmas[].words = input->components`（shader の入力の成分の数） | data_vertex_build は words をそのまま DMA する | **注意（P1 の file）**。attribute の format の word の数が shader の入力より少ないとき（例: `vec4` の入力に R32G32）、buffer の先まで読む。Vulkan の既定の (0,0,0,1) にもならない。words は min(format の byte / 4, components) にし、足りない成分は既定値にする。Keiland・vkdemo の組（vec2←R32G32、vec4←R32G32B32A32、vec3←R32G32B32）では起きない |
| **R8G8B8A8_UNORM の vertex** | DMA は raw の word | 表は R8G8B8A8_UNORM に VERTEX_BUFFER を出す | **注意（P2 の表、または P1 の DMA）**。正規化の変換が無いので、byte が 1 word に入ったまま shader に届く。userland は使っていない。変換を足すまで表から外す案がある（Q1 の判断） |
| **transfer の blit の odd stride** | transfer-job.c は blit を行ごとに分けない（`kind != BLIT` の時だけ needs_rows） | `powervr_transfer_regs_build` は kind によらず needs_rows なら EINVAL | **食い違い（P1 の file）**。odd の行の長さの linear の destination への複数行の blit は、いつも EINVAL。blit も行ごとに分ける（source の行の対応は拡大縮小があるので 1 行の blit に area を写す）か、regs_build の拒否を blit で何とするか決める。今は shared の道（切り替え off）でしか起きない |
| render-check.c | `CHECK_CLEAR 0xff0000ff` などを pack 済みの word で | clear_shared は pack 済み | 合う |
| sRGB の render の CLEAR | clear_usc は shared 0 の 1 word を出力 0 へ | sRGB の pack は F16×4 の 2 word（PBE の source が F16） | **残り（P2）**。sRGB の render target の CLEAR は 2 word を要るが、clear の program は 1 word。job_check は 4 byte の colour を通すので、sRGB の pass は今は誤った色になる。Keiland・vkdemo は使わない |

### 直したもの（P2）

- `image-state.{c,h}`: `powervr_format_clear_words` を新しく作った。transfer-program.c の clear の pack（half・unorm8 の変換を含む）をここへ移した。`powervr_transfer_clear_colour` はこれを呼ぶ。
- `render-pass.{c,h}`: render pass の begin と ClearAttachments の colour の clear を、format の clear の word にした。
- 試験:
  - `plan/ws269/tests/p010/pass-host-test.c`: clear の期待を pack した word にした（1.0,0,0,1 → 0xff0000ff、緑 0.5 → 0x00008000）。
  - `plan/ws269/tests/p009/vk-host-test.c`（P1 の q1398 の frame の試験）: clear の青を、pack した word の byte で見るようにした（1.0 → 255、0x3e20a0a1 → 40）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| p009（切り替え 0・1） | `sh plan/ws269/tests/p009/run.sh build/p013/p009-0 0`（と `1`） | どちらも 511 passed、0 failed |
| p010 | `sh plan/ws269/tests/p010/run.sh build/p013/p010` | 全部 0 failed |
| VF2 の kernel | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p013/vf2 vmunix -j16` | rc 0、warning 0 |

- 未実施: 実機（CLEAR の pass の色は実機で初めて確かめられる）。
- P1 へ: 上の表の「vertex の DMA の words」と「transfer の blit の odd stride」の 2 つ。
- 片付け（Q1 に依頼）: `build/p013/`。

## 後半（q1401-i01）: P2 の q1399 が返した 2 件

- `render-job.c`: vertex の属性の DMA を shader の入力の成分の数ではなく min(format の word 数, 成分の数) にした。32 bit の channel の format（R32〜R32G32B32A32 の UINT・SINT・SFLOAT）で足りない成分は、draw の slot の既定（float は (0,0,0,1.0)、整数は (0,0,0,1)）から stride 0 の DMA で埋める。packed の format（8・16 bit の channel）は word のまま読み（shader が解く）、既定は足さない。table に無い format は従来どおり成分の数。Mesa 25.0.7 の `pvr_pipeline.c` は size_in_dwords を format の bit / 32 とし（成分の数と等しいと assert）、既定の扱いは Vulkan の Vertex Input Extraction に従った（Mesa 26.2.5 の該当の扱いは手元に無く未確認）。
- `transfer-job.c`: blit も奇数の stride の linear の dst では行ごとの job にした。各行の shared words には矩形全体の対応（`&request->area`）を渡すので、どの行も一度に描いた時と同じ所を sample する。
- 試験: p010 job-host-test に、2 float の属性を 3 成分の入力へ流す job（既定が slot に書かれる）と、63 px の linear の dst への 3 行の blit（transfer の kick 3 回）を足した。

| 確かめ（2026-10-20） | 結果 |
| --- | --- |
| p010 run.sh | 全部 0 failed（job-host-test 24/0） |
| p009 run.sh | 511 passed, 0 failed |
| VF2 の vmunix | rc=0、warning 0 |

## q1400-i01（P2、2026-10-20）: Q1 の決定の 2 つ

1. `image-state.c`: R8G8B8A8_UNORM の VERTEX_BUFFER を外した（vertex の fetch が byte を正規化しないため。変換を作るまで）。p009 の「RGBA8 は vertex の format」の期待を「でない」に変えた。
2. sRGB の render pass の CLEAR の用意（P2 の側）:
   - `render-program.{c,h}`:
     - `powervr_render_clear_usc_words(words)` を足した（1 か 2 の word で、shared 0・1 を pixel output 0・1 へ。最初の group に overlap check、最後の group に end）。
     - `powervr_render_clear_shared_words(colour, words)` を足した（2 word は偶数の shared への 64 bit の 1 回の write）。
     - 前の `powervr_render_clear_usc`・`powervr_render_clear_shared` は 1 word の場合として残した。
   - `image-state.{c,h}`: `powervr_format_clear_word_count(format)` を足した。1 は RGBA8 など、2 は sRGB・R16G16B16A16_SFLOAT・R32G32_SFLOAT、4 は R32G32B32A32_SFLOAT、0 は depth など pack しない format。
   - 試験:
     - render-host-test: 2 word の clear の program の bytes。0 と 3 の word は拒む。2 word の write の data。
     - image-host-test: word の数。

**P1 の file に要る残り（これで sRGB の CLEAR が直る）**: render-job.c の `job_background` の CLEAR の枝を次のようにする。
- `words = powervr_format_clear_word_count(color->surface.format)` を求める。
- `powervr_render_clear_usc_words(words, …)` と `powervr_render_clear_shared_words(color->clear, words, …)` を呼ぶ。
- `*shared_registers = words` にする。
- sRGB の PBE の source は F16 で、pixel の出力が 2 register なので、fragment の command の pixel output の幅が 2 register になっていることも確かめる。
- LOAD（render-load）の sRGB は今も U8888 だけ（ENOTSUP）。

| 確認 | 結果 |
| --- | --- |
| p010 run.sh | 全部 0 failed（render-program 29、image 63 など） |
| p009（切り替え 0・1） | どちらも 511 passed、0 failed |
| VF2 の vmunix（build/p014/vf2） | rc 0、warning 0 |

- 片付け（Q1 に依頼）: `build/p014/`。

## q1401-i01 の追加（P1、2026-10-20）: sRGB 等の多 word の CLEAR

- `render-job.c` の `job_background` の CLEAR（と DONT_CARE）の枝を `powervr_format_clear_word_count`・`powervr_render_clear_usc_words`・`powervr_render_clear_shared_words` に変え、`*shared_registers` を word の数にした。0 word（depth など）と 2 を越える word（R32G32B32A32_SFLOAT の 4）は ENOTSUP（fragment の command の pixel output が 1 pixel 2 register なので）。
- pixel output の幅: `render-command.c` は USC_PIXEL_OUTPUT_CTRL を常に 2 register（BXE-4-32 の最少）で出すので、sRGB の F16 の 2 word は入る。直しは要らなかった。
- 試験: job-host-test に sRGB の CLEAR の job（走る）と 4 word の format の拒否を足した（26/0）。p009 511/0、VF2 の vmunix warning 0。

## q1403-i01（P2、2026-10-20）: 相互点検 2、P1 の file の wire の decode と libvulkan の encode

P1 の file の decode を、libvulkan（`userland/desktop/libvulkan`）の encode と field ごとに照らした。見たのは field の順、大きさ（u32・u64）、pointer の有無の word、count の 2 つの形（u32 の count と u64 の payload の数）、padding、sType の chain。

| 命令 | P1 の decode | libvulkan の encode | 判定 |
| --- | --- | --- | --- |
| vkQueueSubmit | vk-queue.c `submit`・`read_submit`・`read_identities`: [queue][u32 count][u64 count]、submit ごとに [sType][u64 pNext][u32 wait][u64 n]{u64}[u64 n]{u32 stage}[u32 cmd][u64 n]{u64}[u32 signal][u64 n]{u64}、最後に [u64 fence] | queue.c `queue_attempt`・`queue_write_submit`・`queue_write_waits`（stage は has_stages の時だけ、同じ native_count）・`queue_write_signals`・`queue_write_handle`（fence、null は 0） | 合う。software で完了した wait は libvulkan が配列から抜くが、u32 と u64 の count は同じ native_count で、decode はどちらも受ける |
| vkCreateFence・vkCreateSemaphore | `create_sync`: [device][u64 1][sType][u64 pNext][u32 flags] と create の tail | sync.c `sync_create`: 同じ。pNext は常に 0（export fence などの chain は libvulkan の中で済む） | 合う |
| vkResetFences | [device][u32 count][u64 count]{u64} | sync.c:129 同じ | 合う |
| vkGetFenceStatus | [device][fence] | sync.c:903 同じ | 合う |
| vkCmdClearColorImage | vk-transfer.c: [image][u32 layout][u64 present][u32 tag][u64 4]{4×u32}[u32 count][u64 count]{range} | commands-generated.inc `vkCmdClearColorImage` と commands.c `command_encode_color`（tag 2、u64 4、4 word） | 合う（pColor が NULL のときは present 0 で色の word が無いが、Vulkan では NULL にならない） |
| vkCmdCopyBufferToImage | [buffer][image][u32 layout][u32 count][u64 count]{VkBufferImageCopy} | 同じ順 | 合う |
| vkCmdCopyImageToBuffer | [image][u32 layout][buffer][u32][u64]{VkBufferImageCopy} | 同じ順 | 合う |
| vkCmdCopyImage | [src][u32 layout][dst][u32 layout][u32][u64]{VkImageCopy} | 同じ順 | 合う |
| vkCmdBlitImage | [src][layout][dst][layout][u32][u64]{VkImageBlit}[u32 filter] | 同じ順（filter が最後） | 合う |
| vkBindImageMemory | vk-image.c `bind_image`: [device][image][memory][u64 offset] → [result] | resources.c `resource_bind`: 同じ。BindImageMemory2 は libvulkan が 1 つずつこれにする | 合う |
| vkGetImageMemoryRequirements | `image_requirements`: [device][image][u64 present] → [u64 1][VkMemoryRequirements] | `resource_requirements`: 同じ。…2KHR と dedicated の答えは libvulkan の中 | 合う |
| vkGetImageSubresourceLayout | `subresource_layout`: [device][image][u64 1][VkImageSubresource][u64 1] → [u64 1][VkSubresourceLayout] | resources.c:252: 同じ。reply は 52 byte（opcode 4、present 8、layout 40） | 合う |
| vkQueueWaitIdle・vkDeviceWaitIdle | vk-device.c（P2） | libvulkan は送らない（自分の中で済む） | 対応の要なし |

- 中の struct（VkImageSubresourceRange・VkBufferImageCopy・VkImageCopy・VkImageBlit・VkImageSubresource・VkSubresourceLayout・VkMemoryRequirements）は、生成された codec の組（libvulkan の codec.c と i915 の vulkan-codec.inc）で読む。p009 の frame の試験は libvulkan の encoder（lib-wire・lib-codec）で frame を書き、executor で読む。この試験で ClearColorImage・CopyBufferToImage・CopyImageToBuffer・bind・requirements・subresource layout・submit・fence が通っている（511 passed）。
- **食い違いは見つからなかった。** 直しは無い。
- 動きの注意（wire ではない、P1 の file、記録だけ）: `submit` は semaphore の wait と signal を読むだけで使わない。submit は同期で終わるので、今の順の保証はこれで足りる。fence は error のときに立たず、libvulkan は submit の result（DEVICE_LOST など）で知る。

- 試験と build: コードの変更が無いので、新しくは流していない（main の f7aa25f5b の上で p009 511 passed は Q1 が確認済み）。

## q1402-i01（P1、2026-10-20）: 相互点検 1、P2 の file の wire の decode と libvulkan の encode

libvulkan（`userland/desktop/libvulkan`、この tree での場所）の encode と、powervr の decode を field ごとに読んで照らした。照らした物は、順・大きさ・presence の word・count の二重の形（u32 と u64）・padding・sType と pNext。p009 の host 試験は libvulkan の encoder で組んだ frame をこの decoder に通しているので、そこを通る道は出力でも確かめてある（Keiland の frame と、vkstage・vkdemo の frame。後者は q1398 で足した）。

| 道 | libvulkan の encode | powervr の decode | 照らした結果 |
| --- | --- | --- | --- |
| vkCreateGraphicsPipelines（`vk-draw.c` `decode_info`） | `pipeline.c` `pipeline_encode_graphics`: sType, u64 0, flags, u32+u64 の stage の数、stage。各 state は presence の後（tessellation は stage が無い時 0、viewport・multisample は rasterize の時、depth は subpass に depth がある時、blend は colour がある時、raster は常に 1）。後ろに dynamic, layout, pass, subpass, base, index | 同じ順で 9 つの presence を読む | 一致。試験で通る: static・dynamic の viewport、depth の有無、blend。specialization の map entry は ENOTSUP（意図どおり） |
| vkCmdDraw・vkCmdDrawIndexed | 4・5 個の u32 | 同じ。firstInstance≠0 は ENOTSUP。indexed は firstIndex を values[2]、vertexOffset を first に | 一致 |
| vkCmdBindVertexBuffers・vkCmdBindIndexBuffer | first、u32+u64 の数と handle、u64 の数と offset。index は buffer, u64 offset, u32 type | 同じ | 一致 |
| vkCmdSetViewport・vkCmdSetScissor・vkCmdPushConstants | first、u32+u64 の数と record。push は layout, stages, offset, size, u64 size, byte（4 byte に pad） | 同じ（`i915_vkc_read_bytes` も pad を読み飛ばす） | 一致 |
| vkCmdBindDescriptorSets（`vk-command.c` `record_sets`） | point, layout, firstSet, u32+u64 の数と set, u32+u64 の dynamic offset の数と u32 | 同じ。dynamic offset は ENOTSUP | 一致 |
| vkCreateRenderPass・vkCreateFramebuffer（`vk-pass.c`） | device, presence, 生成の codec の record, tail | 同じ codec（i915 の生成の decoder） | 一致。試験で通る: 1・2 attachment、dependency 1・2 |
| vkCmdBeginRenderPass | presence, sType, u64 0, pass, fb, rect, u32+u64 の clear の数。colour は tag 0, u32 2, u64 4, 4 word。depth は tag 1, float, stencil。後に contents | `read_clear` は同じ形（colour の長さ 4 を確かめる） | 一致（depth の clear 1.0 は q1398 の試験で job まで届く） |
| vkCmdClearAttachments | aspect, colour index（depth は 0）, tag と値。後に u32+u64 の rect | 同じ | 一致 |
| vkAllocateMemory（`vk-memory.c`） | device, presence, sType, u64 chain（0/1）。chain は [type][u64 0][u32 value] で、export は handle の種類（DMA_BUF 0x200 か OPAQUE 0x1）、import は `GPU_OP_RECORD_IMPORT_RESOURCE` と blob。後に size（host visible は page に切り上げ）, type, tail | 同じ。export は `POWERVR_VK_EXTERNAL_HANDLES` 0x201 の中だけ | 一致。dedicated の chain は libvulkan が送らない |
| vkUpdateDescriptorSets（`vk-layout.c` `update_sets`・`update_write`） | device, u32+u64 の write の数、write（sType, u64 0, set, binding, element, count, type）。後に u64 の image の数と {sampler, view, layout}、u64 の buffer の数と `VkDescriptorBufferInfo`、u64 の texel view の数と handle。最後に u32+u64 の copy | 同じ。copy は読んでから ENOTSUP。VK_WHOLE_SIZE は buffer の終わりまで | 一致 |
| instance・device の問い合わせ（`vk-device.c`） | — | — | p009 の setup の frame（全部の reply の検査）で通る範囲だけ。field ごとの読みは未実施 |

sample の段の道（通しで読んだ）:

- write の `set_image` は view の 2 word と layer の大きさ・pack の情報を image の部分（offset 0）に、sampler の 2 word を sampler の部分（`COMBINED_SAMPLER_OFFSET`）に書く。
- compiler（`shader-spirv-instruction.c` の OpImageSampleImplicitLod）は、set・binding の IMAGE_STATE と SAMPLER_STATE を、それぞれ `POWERVR_SHADER_STATE_WORDS` の request にする。
- `data-program.c` `resolve_descriptor` は同じ `powervr_set_descriptor_offset` で place を求め、set の address に足して DOUTD で shared に読む。
- render-job.c は set の copy を slot の後ろに置き、その address と layout を state に渡す。job-host-test で set の copy の位置を確かめている。
- 3 つが同じ offset の関数を使うので食い違いは無い。array の element は compiler が 0 しか出さない（array の sampler は未対応）。

食い違い・未対応（直しは要らないが記録）:

| 項目 | 内容 | 扱い |
| --- | --- | --- |
| libvulkan に有って decode に無い command | vkCmdDrawIndirect・vkCmdDrawIndexedIndirect・vkCmdSetBlendConstants・vkCmdSetDepthBias（`commands-generated.inc` には有る） | 記録の時点で拒まれる（ENOTSUP/EINVAL）。Keiland・vkstage・vkdemo は使わない。要るなら別の Phase |
| descriptor の copy | 読んでから ENOTSUP | 同上 |

結論: P2 の file に直しの要る食い違いは見つからなかった。P1 の file の直しも無い（source の変更なし）。

## q1404-i01（P2、2026-10-20、急ぎ）: 起動の gpu-selftest が firmware を待たない

- 症状（UAT、VF2 の実機）: `stages: firmware=- render-node=- …` と `kernel: fail(firmware=-)` が出て、ほかの段は全部 skip になった。各行が 2 回出る。
- 原因の見込み（Q1）: powervr は firmware を root が live になった後の段で載せる。gpu-selftest の oneshot はそれを待たずに走る。
- 直し（`userland/tests/gpu-selftest/main.c`）:
  - kernel の段の前の `wait_attach` を足した。stages の firmware が `-` の間、1 秒ごとに読み直す。止まるのは次のどれか:
    - firmware が `-` 以外になる。
    - `hw.gpu.attaching` が一度 0 より大きくなった後に 0 に戻る。
    - 120 秒が過ぎる（`SELFTEST_ATTACH_SECONDS`）。
  - 待ったら `gpu-selftest: waited N s for the GPU to attach` を 1 行出す。上限を越えて firmware がまだ `-` なら `kernel: fail(attach timeout, firmware=-)`。
  - attaching は「一度 0 より大きく見えた後の 0」だけを終わりとみなす。late start が始まる前の 0 で、すぐ終わらないようにするため。
- 2 回の行: `-c` は stdout と `/dev/console` の両方に書いていた。boot の service（init）の stdout が console なので、同じ行が 2 回出た。`-c` でも stdout が console そのもの（同じ char の device、または同じ file）なら、console へは書かないようにした（`stdout_is_console`）。実機の init の stdout が別の名前の device（rdev が違う）なら、まだ 2 回出る（実機で確かめる）。
- 試験: `plan/ws269/tests/p010/selftest-host.sh` に `late` を足した（最初は `firmware=-`、2.5 秒後に ok → `waited 3 s` の後、13 ok）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host | `sh plan/ws269/tests/p010/selftest-host.sh build/p015` | 0 failed（late を含む） |
| VF2 の試験の image | `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p015/img disk-image -j16` | rc 0、warning 0 |

- 未実施:
  - 120 秒の上限を越える場合の host 試験（時間がかかるので）。
  - 実機。
- 片付け（Q1 に依頼）: `build/p015/`。

## q1405-i01（P1、2026-10-20、急ぎ）: VF2 の firmware の起動が止まる件の観測の行

- `firmware-boot.c`: 後段の各段（file、image、processor、memory、interrupt、start、boot、health check、render node）の始まりと終わりに `powervr: firmware: <段> start`、`done (N ms)` か `failed (E) after N ms` を 1 行ずつ出す。start の中は cache、MIPS wrapper、processor の reset 解除の前に 1 行ずつ出す。
- 待ちは元から全部に上限がある（boot・stop の register・command の return slot）。時間切れの時に、何を待っていたかと register・slot の値の行を足した。
- 後段の間は別の thread が 1 秒ごとに見て、終わらない段を 5 秒ごとに `still in <段> after N s` と出す（段が register の読みで戻らない時もほかの hart から出る）。
- `userland/base/sysctl/main.c`: `hw.gpu.powervr.stages` は可変の長さの文字列なのに、名前の分岐が無く固定の大きさで読んでいた（ENOMEM）。`fetch_value` で大きさを問い、文字列として出す分岐を足した。kernel の `sysctl_output` は oldp NULL に大きさを答えており、正しかった。
- 確かめ: VF2 の vmunix と sysctl の build、warning 0。p010 run.sh 0 failed。実機は未（ユーザーが SD で）。

## q1406-i01（P1、2026-10-20）: health check の timeout の診断と mainline との照合

実機（Q1、main 5eb7030f5）: boot は 4 ms で done、health check は return slot 0x0 のまま 1011 ms で時間切れ。

- 診断（`firmware-boot.c`）:
  - health check の時間切れに `firmware_dump` を出す。内容は次のとおり。
    - KCCB・FWCCB の control（write・read・wrap・size と FW の address）。
    - slot の command の先頭 4 word と return slot（FW の address と値）。
    - os init が名指す各 address。
    - MTS の schedule・interrupt/background assoc、soft reset、MIPS wrapper、Garten、MIPS の IRQ status。
    - host の割り込みの数、FW の interrupt_count・kccb_cmds_executed、sysdata の pow_state、started とその時刻。
    - trace の assert の path・info・line。
    - trace の pointer と最後の 48 word。
  - trace を有効にした（log type 0x207: TRACE・MAIN・MTS・POW）。
- (3) の確かめの行:
  - processor の reset 解除の直前に started の値を出す（前の残りでないことを見る）。
  - boot の done の時に started・その時刻・marker と、FW が埋める os init の hardware・firmware の identity（layout と BVNC）を出す。FW が os init を読んだかどうかもこれで分かる。
- mainline（StarFive 6.12.y 4cecf169f38e の imagination）との照合。すべて一致し、直しは無し。

| 項目 | mainline | zedBSD | 結果 |
| --- | --- | --- | --- |
| KCCB の送り（`pvr_kccb_send_cmd_reserved_powered`） | command を slot へ、return slot を NO_RESPONSE、mb、write_offset、MTS_SCHEDULE に DM_GP（0）、wmb の後 | 同じ順（`commands.c`）。値 0、0x0b00 に 32 bit | 一致 |
| ccb_ctl | write 0、read 128、wrap 132、cmd_size 136、144 byte | 同じ | 一致 |
| kccb_cmd | 88 byte、HEALTH_CHECK 115 と magic 0x2abc<<16 | 同じ | 一致 |
| osinit | 0・4・8・12・16、hwr 28、os data 36、compchecks 40（layout の版 3、bvnc 0） | 同じ | 一致 |
| sysinit | runtime 316・trace 320・sysdata 324・util 328・clock 344・started 352・marker 356 | 同じ | 一致 |
| KCCB の写像 | PVR_BO_FW_FLAGS_DEVICE_UNCACHED | `POWERVR_MIPS_UNCACHED` | 一致 |
| `pvr_fw_start` | 順と値（secure、soft reset 3 段、slc、wrapper、ACE、3 µs、release、3 µs） | 同じ | 一致 |
| boot の待ち | `firmware_started` の poll | 同じ | 一致（4 ms が本物かは上の行で見る） |

- 確かめ: VF2 の vmunix の build、warning 0。実機は Q1 が板で。

## q1407（P1、2026-10-20）: return slot が見えない件

板の dump（Q1、bdb17253e）から分かったこと:
- FW は起動している。started の前は 0、後は 1。os init の identity も埋まった。
- FW は slot 0 の health check を読んで実行した。KCCB の read は 1、kccb executed は 1、host への割り込みは 1。
- それでも return slot は 0 のまま。

trace の event id を mainline の `pvr_rogue_fwif_sf.h` の表で読んだ:
- 0x70017012: Bg Task OSid
- 0x70037013: KCCB Slot 0 の Cmd 0x2abc0073
- 0x7001700d: Bg Task complete
- 0x70017006: Kick MTS Irq task
- 0x70017014: Irq Task
- 0x7005a02e: Check Pow state
- 0x7003a015: Idle timer start
- 0x7003a002: GPU idle
- 0x7000700e: Irq Task complete

つまり command は FW の中で完了し、その後 FW は idle に入った。

mainline と照らした結果（v7.3 の `pvr_ccb.c`・`pvr_fw.c`・`pvr_rogue_fwif.h` を raw.githubusercontent から取得）:
- kccb_flags は mainline も 0。
- return slot は u32 の配列で、rtn[slot] の bit 0 が EXECUTED。
- 写像は DEVICE_UNCACHED。
- MIPS の entry の cache policy は 2。

どれも一致した。書いた先が CPU の uncached の view（JH7110 の system port の alias）から見えない理由は、まだ確定していない。候補は GPU の SLC か、CPU の L2 に残る行。同じ時に FW が書いた read offset と executed count は見えている。

直し（回避）と診断:
- `commands.c` `powervr_command_wait`: return slot が EXECUTED でなくても、os data の kccb_cmds_executed がその command の通し番号に達していれば実行済みとする。最初の 1 回だけ `slot N: return 0x.. not seen, executed count M reached; taken as executed` を出す。
  - 通し番号は `powervr_command_send` が数え、boot ごとに 0 に戻す（`kccb_sent`・`kccb_expect`）。
  - FW は KCCB を順に実行するので、数で判定できる。
  - CLEANUP の BUSY など、return の値そのものが要る command は、この道では EXECUTED だけになる。原因が分かるまでの回避。
- dump に return slot 0〜3 と return の物理 address を足した（FW が別の slot に書いていないかを見る）。
- 確かめ: VF2 の vmunix は warning 0。p010 は 0 failed。実機は未。

## q1408-i01（P1、2026-10-20）: coherency の読み、return slot、compute の memory (16)

(1) coherency の「stale」:
- `coherency.c` の round は、CPU の cached の view（direct map）を先に読み込んで clean な行にしておき、FW に書かせる。「stale」は cached の view に前の値が見えた word の数、「missing」は uncached の alias に FW の値が無かった word の数。
- 板の値は 31424 stale・0 missing。FW の書き込みは全部 DRAM（alias）に届いている。cached の view が古いのは、GPU の書き込みが CPU の L2 を snoop しないため（非 coherent、`take_memory` の設計どおり）。
- したがって「GPU の書き込みが DRAM に出ない」のではない。driver は alias だけを読むので、この stale は問題でない。
- StarFive の DDK（`drivers/gpu/drm/img/img-rogue/services/system/rogue/sf_7110/sysconfig.c`）も snoop は EMULATED（非 coherent）で、CPU 側を `sifive_l2_flush64_range` で保守する。GPU 側の SLC の特別な設定（SLC_CTRL_BYPASS・GARTEN_SLC）は BXE では入れていない（`rgxstartstop.c` の GARTEN_SLC は S7 の cache の階層だけ）。MIPS の entry の cache policy は uncached 2 で一致。

(2) return slot:
- mainline の health check は slot を待たずに送る（`pvr_power.c` の watchdog）。slot を待つのは MMU cache（`pvr_mmu.c`）と cleanup（`pvr_fw.c`）。StarFive の DDK も health check は slot 無し（`rgxfwutils.c` の `RGXScheduleCommand`）。
- 板では FW が health check を実行したのに slot は 0 で、それ以外の FW の書き込みは alias に見えている。このことから、FW は health check の return slot を書かないと見る（推測）。
- health check は FW の executed count で待つようにした（`powervr_command_wait_executed`）。他の command（MMU の flush など）は従来どおり return slot で待つ。見えない時の回避の行（`return 0x.. not seen`）はそちらで出れば本当の問題の証拠になる。

(3) compute kernel の `memory (16)`:
- zedBSD の errno で 16 は EEXIST（EBUSY は 17）。`compute-check.c` は run と stream を general heap の固定の位置（+1 MiB・+4 MiB）に map していた。session の中で同じ heap から取られた物と重なると EEXIST になる。
- run・stream の位置を session の `powervr_gpu_space_take` で取るようにし、teardown で返す。PDS・USC は固定のまま。step の名前を `memory: run`・`streams`・`data sequencer programs`・`shading cluster programs` に分け、まだ重なればどれかが行に出る。

確かめ: VF2 の vmunix は warning 0、p010 は 0 failed。実機は未。TA の reset lockup は別の件で、この直しの範囲外（次の dmesg を見て）。

## q1409-i01（P1、2026-10-20）: return slot の行き先の探索、compute の program の位置、TA の lockup の dump

板（2b996406e）: health check は 9 ms で done。slot 1（MMU の flush）の return も見えず、fallback で executed count 130 を見て通過。compute は `memory: shading cluster programs (16)`。render check は `ta: fail(reset lockup)`。

- (a) `powervr_firmware_return_probe`（`firmware-boot.c`）: fallback が初めて働いた時に一度だけ出す。
  - return slot の FW の address と heap の位置、MIPS の page table の entry とそれが指す物理の page、host の page と alias。
  - slot 0〜7 を alias と cached の view（invalidate の後）で読んだ値。
  - heap の有効な全部の page で、slot の位置の word が 1 の page（最大 8）。
  - entry の指す page と host の page が違うか、別の page に 1 があるかで、FW が書いた先が分かる。
  - executed count は FW が実行した KCCB の command の総数。`kccb_sent` は送った数（boot ごとに 0 から、最初の command が 1）。130 は、coherency の検査が送った MMU cache の command を含む数。
- (b) `compute-check.c`: PDS・USC の program を、固定の offset ではなく session の program の heap（`powervr_session_program_map`）から取るようにした。teardown で `powervr_session_program_unmap`。
- (c) render check の fence の失敗の時に `powervr_firmware_dump`（slot 無し、trace の最後の 128 word）を出す。`render-queue.c` の context reset の通知（105）で、data の 40 byte を hex で 1 行出す。
- 確かめ: VF2 の vmunix は warning 0、p010 は 0 failed。実機は未。

### q1409-i01 の追加（ユーザーの仮説、2026-10-20「CPUの窓がHALのAPIでcachedなとcoherent属性がおかしいのでは」）

- 130 の数: KCCB は 128 slot なので、slot 1 は 130 番目の command（coherency の検査が送った MMU cache の command の後で一周した物）。期待の数 130 と FW の kccb_cmds_executed 130（mainline の `rogue_fwif_osdata` の offset 568 で一致）が合っており、fallback の判定は正しい。実行の前の command を executed にした誤りではない。
- 窓: VF2 の alias は TRM の Table 2-4 の system port（DDR を 0x04_4000_0000 で、memory port の 0x00_4000_0000 から +0x4_0000_0000）。同じ窓で、FW の read offset・executed count・coherency の検査の書き込み（missing 0）は見えているので、窓の全部が cached とは考えにくい。違いは、CPU が kick の直前に書く word（return slot の NONE）だけ。
- 直し（仮説の確かめを兼ねる、`commands.c`）:
  - return slot を kick の直前に NONE にするのをやめた。答えを読んだ直後（`powervr_command_wait` と `powervr_command_wait_executed` の成功の時）に NONE に戻す。
  - send の時は、slot がまだ NONE でない時だけ NONE を書き、`kern_io_barrier` の後で読み戻す。
  - CPU の遅れた store が FW の答えを上書きしていたのなら、fallback の行と probe が出なくなる。
- HAL の実装・hal.h は変えていない。窓の leaf の属性と U74 の PMA は、この板の結果を見てから（probe の行が alias と cached の値の違いを出す）。
- 確かめ: VF2 の vmunix は warning 0、p010 は 0 failed。

## q1410-i01（P1、2026-10-20）: probe の「0xc0028000 に 1」の読み直し

- 板の probe は、heap page 0x28000（FW 0xc0028000）の slot 2 の word（offset 8）が 1 だと出した。
- runtime の record（`rogue_fwif_runtime_cfg`）は offset 8 が `active_pm_latency_persistant` で、host が 1 を書く（`fill_records`、mainline の check の 306 行と一致）。この page は runtime の record で、FW の return の書き先ではない見込みが高い（偽の一致）。os init の field の offset は mainline の check（340〜351 行）と全部一致しており、ずれは無い。
- そのため return slot の謎は残る。FW は MMU cache の command も実行する（executed count が進む）が、os init の 0xc001d000（entry 0x10142d7、物理 0x4050b000、host の page と同じ、uncached の policy 2・writable・valid）に答えが無い。
- 直し: probe の行に、page がどの record に属するか（code・data・…・runtime・trace）の名前を足した。fallback は残す（return slot が見えるまで外さない）。
- 確かめ: VF2 の vmunix は warning 0。

## q1411-i01（P1、2026-10-20）: compute の reset lockup の切り分けと return slot の marker

- 通知の読み（mainline の `rogue_fwif_fwccb_cmd_context_reset_data`: context 0・reason 4・dm 8・job 12・flags 16・pc 24・fault 32）: `00000002 00000001 00000004 00000000 …` は context 0x2、reason 1（guilty lockup）、dm 4（compute）、pc・fault は 0。`render-queue.c` の行を名前付きにした。
- compute の kernel の失敗の時に `powervr_firmware_dump`（trace の最後の 128 word を含む）を出す。
- compute の command は mainline の `rogue_fwif_cmd_compute`（regs は 8 から、cdm_ctrl_stream_base 40、cdm_context_state_base_addr 48）と word 10・12 で一致。Mesa（25.0.7 の `pvr_job_compute.c`）は、ほかに `cdm_resume_pds1`（CR_CDM_CONTEXT_PDS1: switch の load program の data の大きさ、unified・common の大きさ）、`cdm_item`、`compute_cluster` を入れる。こちらは 0 のまま。
- q1388 の compute の switch の static state（`powervr_session_compute_switch`）は、`POWERVR_COMPUTE_SWITCH_STATE`（`render-private.h`、既定 0）で外せるようにし、この build では外した。switch を入れて PDS1 を 0 のままにしているのが lockup の原因かを、これで切り分ける。
  - lockup が消えれば、switch を戻すときに `cdm_resume_pds1` を Mesa のとおりに入れる。
  - 消えなければ別の原因。dump と trace で見る。
- return slot: boot の時に全 slot に marker（`0xdead0000 | slot << 4`、executed の bit は 0）を入れ、答えを読んだ後も marker に戻す。FW がこの page の word を書けば、probe の slot の値が marker でなくなる。
- 確かめ: VF2 の vmunix は warning 0、p010 は 0 failed、p009 は 511/0。実機は未。

## q1412-i01（P1、2026-10-20）: compute の lockup の切り分け

板（54de7284b）:
- return slot の marker は全部そのまま。FW はこの page を書いていない。
- switch の state を外しても compute は guilty lockup（dm 4）。
- compute の失敗の時に dump が出なかった。

- dump が出なかった理由: 失敗が「kick は終わったが 0 word」の枝（error 0）で、dump はもう一方の枝（error ≠ 0）にしか無かった。両方の枝で出すようにした。
- 切り分けのため、compute の kernel の前に小さい kick を 2 つ流す（`compute-check.c` の `probe_kick`）。どちらも行 `compute probe: <名前>: finished/FAILED (e), context kept/reset` を出し、失敗か context の reset なら dump して check を止める。
  - 「end only」: stream が end だけ。
  - 「fence and end」: fence の kernel と end。
  - どこで lockup するかで、原因が CDM の command・context（end only）、fence の PDS program（fence）、kernel の PDS・USC の program（両方通って kernel で落ちる）のどれかに絞れる。
- heap page 0xc020a000（unnamed）: firmware の 20 の record の外。session の FW の context の record（render の module が同じ heap から取る物）の見込み。slot 2 の位置の 1 は、その record の field である可能性が高い。名前を付けるのは後で（急がない）。
- CDM の command の残りの field（cdm_resume_pds1・cdm_item・compute_cluster）は今回入れていない。switch を外した状態では resume は使われず、cdm_item・compute_cluster は Mesa でも feature（compute_morton_capable・cluster_grouping）がある時だけ。BXE-4-32 の feature の値は次に確かめる。
- 確かめ: 試験の image の config（`plan/ws269/tests/p010/config-visionfive2-selftest.mk`、compute check を含む）の vmunix（`build/q1412/st`）は warning 0。通常の VF2 の config には compute check が入らない。

## q1413-i01（P1、2026-10-20）: compute の lockup — resume の PDS1

板（31755389a）: 「end only」の stream でも context が reset された（guilty lockup、dm 4）。

trace の読み（mainline の `pvr_rogue_fwif_sf.h` の表）:
- 0x7004109f: FW FAULT。line 0x532（1330）、file 0x66775f68 0x77722e63 = "fw_h" "wr.c" = `…fw_hwr.c`、データ 0。HWR（lockup の検出）が出す行で、command の検証の失敗ではない。
- 0x70021016: UFO Updates for FWCtx 0xc002f000 @ 136。
- 0x70021017: UFO Update [0xc002e000] = 1。timeline の更新で、kick の wait が通った理由。
- 0x7005a02e・0x7002a00f・0x7000a00d・0x7003a015・0x7003a002: power の確認と idle。
- assert の line 70・path 空: FW FAULT の行の残り（assert の buffer は使われていない）と見る。未確認。

`kccb executed 131084` は 0x2000C だが、coherency の検査は 1 word ごとに MMU cache の command を 1 つ送る。16384 word × 8 round = 131072 に、ほかの 12 を足した数で、u32 の count として正しい。fallback の比べ方の誤りではない。

原因の見込み:
- FW は CDM の context を始める時に、static state の resume の program を流す（推測）。
- switch の state が 0 なら address 0 の program を、state があれば PDS1 が 0 の program を流すことになる。どちらも end だけの stream で lockup する。q1388 の state を外しても入れても lockup したことと合う。

直し:
- `render-sr-program.c` に `powervr_compute_resume_pds1` を足した。Mesa の `pvr_submit_info_stream_init` の CR_CDM_CONTEXT_PDS1 のとおり、switch の unified store、common_shared、common の大きさ（64 byte 単位）、load の data（16 byte 単位）を入れる。
- compute の command の word 15（regs の cdm_resume_pds1、offset 8+52）に、compute check と vk-queue の両方で入れる。common の大きさは、今は 0 で渡す（shared register の数を渡すのは残り）。
- switch の state を既定で戻した（`POWERVR_COMPUTE_SWITCH_STATE` 1）。
- p009 の run.sh に render-sr-program を足した。

確かめ: 試験の image の config と VF2 の vmunix は warning 0。p009 は切り替え 0・1 とも 511/0、p010 は 0 failed。実機は未。

## q1414-i01（P1、2026-10-20）: どの kick で reset か

板の全文（vf2-dmesg10）の順:
- coherency の検査
- context reset notice（context 0x2、reason 1、dm 4）
- `compute: fail(reset lockup)`
- `compute probe: end only: finished (0), context reset`

この順の読み:
- coherency の検査は data master を使わない。KCCB の MMU cache の command だけで、131072 個。
- compute-check の最初の compute の kick は probe の「end only」。その行は wait の後に出るので、wait の間に service が通知を出す。通知が probe の行より前に出るのはそのため。
- したがって、最初に reset を起こしているのは「end only」の kick そのものと読む。前の reset を引きずっているのではないので、(2) の context の作り直しは要らない。

FW の trace（2648〜）:
1. FW FAULT（`…fw_hwr.c` line 1330）
2. UFO の更新（FWCtx 0xc002f000 @ 136、[0xc002e000] = 1）
3. idle

CDM の lockup の前後の行は、MTS・POW の group しか有効でなかったので出ていない。

直し:
- probe の kick の前に `powervr: kick: compute probe <名前> sent (context kept|reset)` を出す。
- FW の trace の group を増やした（log type 0x4637: MAIN・MTS・CSW・BIF・POW・HWR・MISC）。context の switch・bus（MMU）・recovery の行で、CDM のどこで止まるかを見る。
- 確かめ: 試験の image の config の vmunix は warning 0。実機は未。

## q1415-i01（P1、2026-10-20）: kick の後の trace の全部と、CDM が読む物の probe

trace（vf2-dmesg11）の 2 つの id（mainline の表）:
- 0x7002b021: HWR「Recovery DM%u: Updated Recovery counter. New R-Flags」。DM 4、flags 0。
- 0x7001b019: HWR「Recovery DM%u: DM fully recovered」。DM 4・5・6。

compute の DM が回復させられた後の行で、lockup の手前の CSW・BIF の event は窓の外。

直し（診断）:
- probe の kick の直前に FW の trace の pointer を覚える（`powervr_firmware_trace_mark`）。dump はそこから現在までの全部を出す。512 word を越えれば先頭 256 と末尾 128 を出し、間の数を 1 行に書く。
- probe の kick の失敗の時に `probe_report` を出す。
  - stream・resume の area・PDS・USC の GPU の address と、session の page table の entry（`powervr_gpu_mmu_lookup`）、buffer の物理 page、catalogue。
  - context の record の common の field（ccb control・ccb・state・flags・priority・dm・deadline・memory・id）。
  - switch の state の 7 word。

mainline と 1 field ずつ照らした結果（すべて一致）:

| 物 | mainline（check の offset） | zedBSD |
| --- | --- | --- |
| fwcommoncontext | ccbctl 0、ccb 4、context_state 24、priority 32、dm 60、max_deadline 104、fw_mem_context 136、server id 140、152 byte | 同じ（`interface.h` の POWERVR_CONTEXT_*） |
| fwcomputecontext | common 152 + static 56 + work_est・flags・wgp・core_mask の 24 = 232 | 232 |
| compute_ctx_state | ctx_state_flags の 4 byte | 4 |
| cccb_ctl | write 0、read 4、dep 8、wrap 12、32 byte | 同じ |
| ccb_cmd_header | type・size・ext・int・work_est、40 byte | 40 |
| CDM の型 | 205 \| magic \| TASK（bit 15） | 同じ |
| UPDATE | 213 \| magic、ufo 8 byte | 同じ |
| kick_data | context 0、woff 4、wrap 8、num_cleanup 12 | 同じ |
| fwmemcontext | pc の物理 0、reg set 8、32 byte | 同じ |
| cmd_compute | common 4（align で regs は 8）、ctrl_stream_base 40、context_state_base 48、resume_pds1 60 | 同じ |

- 確かめ: 試験の image の config の vmunix（`build/q1415/st`）と VF2 の vmunix は warning 0。実機は未。

## q1416-i01（P1、2026-10-20）: 「end only」の kick の trace の読み

trace（vf2-dmesg12、mark 2392〜3778）を mainline の `pvr_rogue_fwif_sf.h` で読んだ順:
1. KCCB slot 17 の Cmd 0x2abc0065（kick）。
2. cCCB Woff update = 184、DM 4、FWCtx 0xc002f000。
3. Ready-to-run DM 4、item 0xc002f000。Client command header DM 4、client CCB 0xc0043000。
4. GPU Units init（mask 0x1）、power off の取り消し。
5. BIF: Activate MemCtx 0xc002b000 DM 4、Setup register set -1、PC address 0x405df000（catalogue と一致）、Alloc PC set 0、Grab reg set 0。
6. Kick Compute: FWCtx 0xc002f000 @ 0。
7. その後の HWR の検査:
   - 「DM4 has taken 31444 / 62782 / 94246 ticks」
   - Fast CRC Check の HWRNeeded 0 → 1
   - USC Slots used by DM 0
   - signature の register 0x40f8・0x4600 は Old・Curr とも 0
   - Recovery DM4・5・6 fully recovered

読み:
- FW は CDM を kick し、MMU の context（PC）も設定した。
- CDM は何も進めていない。USC の slot は 0、signature も 0。MMU の page fault の event は無い（EVENT_STATUS 0）。
- program を動かす前（stream を読む、または resume の段）で止まっている見込み。

PTE（`powervr_gpu_page_entry`）と mainline の `pvr_page_table_l0/l1/l2_entry_raw_set` の照合:
- l0: 0x009 は VALID（bit 0）と SLC_BYPASS_CTRL（bit 3）。READ_ONLY（1）・CC（2）・PM_SRC（4）・ENTRY_PENDING（5）は 0。address は bit 12〜39。mainline と同じ形で、bypass は mainline の BO の BYPASS_DEVICE_CACHE と同じ使い方。
- l1（PD）: valid と page size 4 KiB（0）と address。
- l2（PC）: 32 bit で (address >> 12) << 4 | valid。

どれも一致した。

その他:
- Mesa の compute の stream の execute_count は、gpu_multicore_support でも 0（Mesa main も同じ）。cdm_item・compute_cluster・tpu_tag_cdm_ctrl も 0 のまま。違いにはならない。
- 直しは見つからなかった。次の診断として、probe の kick の 100 ms 後（FW の HWR の前）に core の register を読む `probe_registers` を足した。register は EVENT_STATUS・SIDEKICK_IDLE・CDM_CONTEXT_STORE_STATUS・BIF の CAT_BASE0・MMU_ENTRY_STATUS・MMU_ENTRY・FAULT_BANK0/1 の MMU・REQ の STATUS・MMU_STATUS・READS の EXT/INT・STATUS_MMU で、offset は StarFive の DDK の `rgx_cr_defs_km.h` による。MMU の walk の pending（MMU_ENTRY）や、bus の読みの滞り（READS_*）で、CDM がどこで止まるかを見る。
- 確かめ: 試験の image の config の vmunix は warning 0。実機は未。BXE で存在しない register の読みは 0 を返す見込み。

## q1417-i01（P1、2026-10-20）: register の読みが本物かの確かめ

板（vf2-dmesg13）: kick の 100 ms 後の 14 個の register が全部 0。

確かめた事:
- BXE-4-32 の feature（StarFive の DDK `configs/rgxconfig_km_36.V.54.182.h`）: XE_MEMORY_HIERARCHY、MIPS、ROGUEXE、GPU_VIRTUALISATION（OSID 8）、PHYS_BUS_WIDTH 36、VIRTUAL_ADDRESS_SPACE_BITS 40、SLC 16 KiB・1 bank・512 bit の line、GPU_MULTICORE_SUPPORT、LAYOUT_MARS。
- XE_MEMORY_HIERARCHY でも、DDK の debug（`rgxdebug.c`）は BIF の FAULT_BANK の register を同じ offset で読む（REQ_STATUS の field の形だけが違う）。
- FW の trace は「Alloc PC set 0 as register range [1 - 1]」なので、catalogue は CAT_BASE0 ではない所に入る見込みがある。CAT_BASE0 の 0 は不自然でない。
- JH7110 の DT の gpu の node に dma-ranges は無い。GPU は CPU の物理の address をそのまま使う（bus の offset は無い）。FW の memory context の catalogue は 64 bit の物理（mainline の `fw_mem_context_init` と同じ形）。
- 前の dump で、32 bit で読んだ MIPS wrapper は 0x1cf80 なのに、64 bit で読んだ Garten wrapper・soft reset は 0 だった。64 bit の読みが bus で 0 になる疑いがある。

直し（診断）:
- probe の register を 25 個にした。CORE_ID・TIMER（最初と最後）、CDM_CONTEXT_PDS0/1・TERMINATE_PDS・CONTEXT_LOAD_PDS0、CAT_BASE1、SLC_STATUS1・SLC_IDLE・JONES_IDLE を足した。
- それぞれを 64 bit の読みと 32 bit 2 つの読みで並べて出す。CORE_ID・TIMER が 64 bit で 0、32 bit で値なら、64 bit の読みが落ちている（全部の 64 bit の読みの直しへ）。
- 確かめ: 試験の image の config の vmunix は warning 0。実機は未。

## q1418-i01（P1、2026-10-20）: catalogue の register を全部読む

板（vf2-dmesg14）:
- 読みは本物。CORE_ID 0x08470000、TIMER は進む。
- CAT_BASE0・1、CDM の context の register、SLC・Jones の idle は 0。

StarFive の DDK の `rgx_cr_defs_km.h` に、XE 用の別の offset の BIF の register は無かった。`__XE_MEM__` は field の形だけ。catalogue の register の候補は次の 3 つ:
- BIF_CAT_BASE0〜7（0x1200〜0x1238）と CAT_BASE_INDEX（0x1240）
- FWCORE_MEM_CAT_BASE0〜7（0x30F0〜）
- 新しい MMU の MMU_CBASE_MAPPING_CONTEXT・MAPPING・FAULT_STATUS（0xE140・0xE148・0xE150）

どれに FW が書くかは、読んで確かめる。

CORE_ID: 0x0018 は古い形の id の register。PBVNC_COREID_REG の core（この BXE）では、packed の BVNC が CORE_REVISION の位置 0x0020 に出る見込み。boot の BVNC の読みと比べるため、0x0020 も足した。

直し（診断）: probe の register を 38 個にした（上のすべてと前の分）。どの catalogue の register に PC（0x405df000 の形）が入っているか、MMU の fault の status が何かを見る。

確かめ: 試験の image の config の vmunix は warning 0。実機は未。

## q1419-i01（P1、2026-10-20）: FW の register の書き込みは届いているか

板（30d65e4b3）:
- 0 でないのは CORE_ID 0x08470000、0x0020 の packed BVNC 0x00240032003600b6（36.50.54.182）、TIMER だけ。
- catalogue・MMU・CDM・SLC・idle の register は全部 0。

MIPS の設定を mainline の `pvr_mips_wrapper_init`・`pvr_mips_fw_process` と照らした（すべて一致）:
- MIPS wrapper: 0xCF800000 >> 16 | micromips。読み戻しの 0x1cf80 と一致。
- remap 1・5・2・3: 入力の address と enable、出力の page と log2 の segment 12。
- boot data: stack 0、reg_base 8（GPU の register の物理）、page table 16〜（4 page）、log2 12、page 数 4。

FW の register の書き込みが全部消えているとは考えにくい。FW は host への割り込み（register の書き込み）を出せている（host interrupts 8）。

新しい疑い: 前の probe は 1 register ごとに kern_logf していたので、読みの間に時間がかかり、FW の HWR（kick から約 0xA1A27 の TIMER）をまたいでいた可能性がある。TIMER の最初と最後の差は 0xAF8E1 で、その間隔より長い。reset の後の 0 を読んでいたかもしれない。

直し（診断）: 38 個を先に続けて全部読み、後でまとめて出す。最後の TIMER と最初の差で、読みが HWR の前に収まったかが分かる。

確かめ: 試験の image の config の vmunix は warning 0。実機は未。

## q1420-i01（P1、2026-10-20）: 読みは job の最中だった、host の書き込みの読み戻し

vf2-dmesg16 で時刻を照らした。FW の trace の時刻と TIMER は桁が合い、同じ timer と見る（推測）:
- FW の trace: Activate MemCtx 0x9a8fb9、Kick Compute 0x9a8ffd、「DM4 has taken」0x9b0ae2・0x9b854b・0x9c0035、Recovery 0xa4aa3d。
- probe の読み: TIMER 0x9ce713〜0x9ce71e。

読みは kick の後、recovery の前、つまり job の最中。そこで catalogue base 0〜7・MMU・CDM の register が全部 0 だった。FW の書き込みが見えていない仮説が戻る。

mainline で FW が host の割り込みを上げる道: host は `ROGUE_CR_MIPS_WRAPPER_IRQ_STATUS` を読んで `..._IRQ_CLEAR` を書く。FW は MIPS wrapper の割り込みを立てるが、それが GPU の register bank の書き込みかは、mainline の host の code からは分からない。host interrupts 8 が届いたことは、FW の register の書き込みの証拠にならない。

直し（診断）: FW の start の段の cache の設定の後で、host の書き込みが register に届き、読み戻せるかを確かめる（`register_check`）。
- SLC の control（直前に host が書いた物）を読む。
- まだ誰も使わない catalogue base 7（0x1238）に 0x12345000 を書いて読み、0 に戻して読む。

読み戻しの結果で場合を分ける:
- 書いた値が読めれば、その register の読みは信用でき、job の最中の 0 は FW が書いていない事を意味する。
- 読めなければ、その register の帯は host から読めない（OSID の bank など）ので、0 は何も言わない。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1421-i01（P1、2026-10-20）: host の書き込みが一部の register に届かない件

板: SLC control と catalogue base 7 は、host が書いても読み戻すと 0。MIPS wrapper・CORE_ID・BVNC・TIMER は読める。

照らした事:
- 電源・clock・reset（StarFive の DDK `sf_7110/sysconfig.c` の `u0_img_gpu_enable` と `sys_gpu_runtime_resume`）と、`src/drivers/platform/visionfive2/vf2-gpu.c` の `power_up`。順は一致している: PMU の domain → bus master（NOC の GPU AXI）→ APB・RTC の clock → core の divider の rate → core・sys の clock → APB の reset → DOMA の reset。
  - 違い: DDK は `starfive_pmu_hw_event_turn_off_mask(0)` で PMU の hardware event の mask を外している。こちらは扱っていない（PMU の file は Q1・HAL の側、未確認）。
- DDK の `RGXStart`（`rgxstartstop.c`）の順は mainline と同じ。clock は「既定（自動の clock gating）」で、CLK_CTRL を書かない。MIPS では BIF の catalogue を host が書かない（FW の仕事）。

直し（診断、`register_check`）:
- 次の値を出す: register の窓の大きさ、CLK_CTRL（0x0000）と CLK_STATUS（0x0008）、soft reset。
- 次に、CLK_CTRL の全部の field を ON にしてから、同じ catalogue base 7 の書き・読みと SLC control の読みをし、CLK_CTRL を元に戻す。
- 結果で場合を分ける:
  - ON で読めるようになれば、自動の clock gating で block の clock が止まっている。FW が block を起こしていないか、起こし方の誤りを探す。
  - ON でも 0 なら、clock 以外（PMU・電源の island・register の帯の保護）を探す。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1422-i01（P1、2026-10-20）: 書き込みの幅と bus の保護

板: clock control（0x0000）に「全部 on」を 64 bit で書いても 0 に読める。

読み:
- soft reset の 0x100000000 は bit 32 の GARTEN_EN で、start の手順のとおり（processor だけを reset に置いた状態）。soft reset は 64 bit の store で書き、64 bit で読めている。64 bit の書き込みそのものは効いている。
- SLC control は `powervr_write32`（32 bit）で書いているのに 0 に読める。MIPS wrapper も 32 bit で書いて読める。幅だけでは説明が付かない。
- `kern_mmio_write64` は 1 回の 64 bit の store（kern の device-io）。mainline の `pvr_cr_write64` も `writeq` の 1 回。
- 残る候補は bus の secure の保護（SYS_BUS_SECURE、0xA100）。FW の image の feature の bit 48（mainline の `PVR_FW_HAS_FEATURE_SYS_BUS_SECURE_RESET` の位置と一致）が立つ時だけ、start の最初で 0 を書いて解いている。解けていなければ、保護された register の帯への host の書き込みは落ち、読みは 0 になる。

直し（診断、`register_check`）:
- SYS_BUS_SECURE の値を出す。
- catalogue base 7 を 32 bit 2 回（low → high、high → low）で書いて読み戻す。
- clock control も 32 bit 2 回で書いて読み戻し、元に戻す。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1423-i01（P1、2026-10-20）: PMU の hw event の mask、register の地図

板: bus secure は 0。32 bit の 2 回の書き込みでも catalogue・clock control は 0 に読める。幅でも bus secure でもない。

(1) PMU（Q1 の許可で WS256 の `vf2-pmu.c`・`vf2-gpu.c` も直した）:
- StarFive の `drivers/pmdomain/starfive/jh71xx-pmu.c` の `starfive_pmu_hw_event_turn_off_mask` は、`JH71XX_PMU_HW_EVENT_TURN_OFF`（0x08）に mask をそのまま書く。
- DDK の `sys_gpu_runtime_resume` は clock の前に 0 を、`sys_gpu_runtime_suspend` は clock を止めた後に -1 を書く。
- こちらの `drv_vf2_pmu_set_power` は、GPU の domain の切り替えのたびに GPU の bit（0x80）を on・off の mask に足していた。
- 直し:
  - `drv_vf2_pmu_event_off_mask` を足した。
  - `vf2-gpu.c` の `power_up` で、PMU の domain を入れた後・clock の前に 0 を書く。
  - `power_down` で、clock を止めた後・domain を切る前に 0xffffffff を書く。
  - DDK のとおりで、全部の domain の mask を書く。

(2) register の地図（`register_map`、start の段の register check の後）:
- 帯は 0x0000〜0x0200、0x0800〜0x0900、0x0A00〜0x0C00、0x1200〜0x1400、0x3800〜0x3900、0xA100〜0xA110、0xE100〜0xE200。
- 8 byte ごとに読み、0 でない物を 4 つずつ 1 行で、0 の数を帯ごとに出す。
- 書くのは前からの catalogue base 7 と clock control（元に戻す）だけ。

(3) DDK の `rgxpower.c`・`rgxinit.c` で、host が BXE の start の前に書く register を探した。soft reset（`rgxinit.c` の reset の道）と、IRQ・event・safety の clear だけで、power island（SIDEKICK・JONES）や CLK_CTRL を host が書く所は無かった。

確かめ: 試験の image の config と通常の VF2 の config の vmunix は、どちらも rc 0、warning 0。実機は未。

## q1424-i01（P1、2026-10-20）: 地図の読み（StarFive の DDK の `rgx_cr_defs_km.h`）

| offset | DDK の名前 | 値 |
| --- | --- | --- |
| 0x0018 | CORE_ID | 0x8470000 |
| 0x0020 | CORE_ID__PBVNC | 36.50.54.182 |
| 0x0058 | DDK に定義なし | 0xff |
| 0x0100 | SOFT_RESET | GARTEN_EN |
| 0x0160 | TIMER | 進む |
| 0x0810 | MIPS_WRAPPER_CONFIG | 0x10000 |
| 0x0818〜0x0878 | MIPS_ADDR_REMAP1〜 の CONFIG1/2 | 0x1fc00000 と 0xc の繰り返し |
| 0x0888 | MIPS_ADDR_REMAP_RANGE_DATA | 0x780 |
| 0x0890 | XPU_BROADCAST | 1 |
| 0x08C8 | MIPS_DEBUG_CONFIG | 1 |
| 0x08D0 | MIPS_EXCEPTION_STATUS | 1 |
| 0x08F8 | MARS_IDLE | 6 |
| 0x0B50 | MTS_GARTEN_WRAPPER_CONFIG | 1 |
| 0x0BC8 | MTS_SCHEDULE_ENABLE | 0xff |
| 0x38C0 | AXI_ACE_LITE_CONFIGURATION | 0x88805000 |

- DDK の定義には XE 用の別の offset の CLK_CTRL・BIF は無い（q1418 の確かめのとおり、`__XE_MEM__` は field の形だけ）。mainline の imagination の driver（upstream）には JH7110 の GPU の DT の node も無い。BXE-4-32 の JH7110 で動いた参照は、StarFive の DDK だけ。
- 注目点: host が書いた MIPS wrapper（0xCF800000 >> 16 | micromips、前は 0x1cf80 で読めた）が、今は 0x10000 で読める。remap の CONFIG2 は出力の page を落とした 0xc（log2 の 12）だけで読める。
  - FW は起動するので、書き込みは効いている。
  - これらの register は、読むと一部の field が 0 に見える（読みが制限されている）。
  - したがって、0 の読みは「書き込みが届いていない」証拠にならない。CLK_CTRL・BIF・SLC の 0 も同じ理由の可能性がある（GPU_VIRTUALISATION の OS の bank から読む host の窓の制限か）。
- 結論: register の配置の誤りの証拠は見つからなかった。host の読みで GPU の状態を確かめる道は、この core ではあてにならない。CDM の停止の原因は、FW の trace（BIF の setup・Kick Compute の後に何も無い）から別に探す必要がある。
- source の変更は無し（記録だけ）。

## q1425-i01（P1、2026-10-20）: visionfive2opengpu の照合（取得できず）と mainline の master の差分

- `https://github.com/rumorsorchid/visionfive2opengpu` は取得できなかった（git clone・raw.githubusercontent・GitHub の API がどれも 404、検索の API でも 0 件）。private か、名前が変わったか、消えた見込み。この repo の patch・DT・README の照合は未実施。
- 代わりに、mainline の master（torvalds/linux）の `drivers/gpu/drm/imagination` を StarFive の 6.12 の写しと比べた。BXE と同じ形の BXM-4-64（TH1520）の対応で入った変更:
  - `pvr_fw_startstop.c` `pvr_fw_stop`: 「LAYOUT_MARS の feature の core では、SIDEKICK と SLC は FW が電源を落としている」として、SIDEKICK_IDLE・SLC_IDLE の待ちを LAYOUT_MARS でない core だけにした。この core は LAYOUT_MARS 1（DDK の config と FW の feature）。
    - FW が SIDEKICK・SLC の電源の島を落とすなら、host の読みで SLC・BIF・clock の register が 0 になるのは、その島が off の間なら当然。q1421〜q1424 の 0 の読みと合う（推測）。
    - こちらの `stop_processor` は、MARS の時に MARS_IDLE を待つ形で、SIDEKICK・SLC の待ちは前から無い（`POWERVR_FEATURE_LAYOUT_MARS` の分岐）。
  - `pvr_mmu.c`: page table の page を、device が coherent でない時は write-combine で map するようにした。こちらは uncached の alias で書いているので同等。
  - `pvr_power.c`: TH1520 は pwrseq（SoC 固有の電源の順の driver）を使う。JH7110 は manual の道（clock・reset）に当たる。
  - `pvr_fw_mips.c`: ELF の処理の共通化と、IRQ の enable の register（MIPS_WRAPPER_IRQ_ENABLE）の扱いの整理だけ。中身の変化は無い。
- source の変更は無し（直す違いを見つけられなかった）。repo の中身（JH7110 の SoC の統合の直し）が分かれば、それを照らすのが次。
