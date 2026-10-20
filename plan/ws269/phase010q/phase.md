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
