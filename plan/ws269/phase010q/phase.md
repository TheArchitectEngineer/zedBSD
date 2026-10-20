<!-- awesome-plan project=zedbsd record=ws269-p010q -->

# ws269-p010q: 実機の前の点検（cache・MMU・SLC・順・時間切れの後）

- ID: ws269-p010q（親 [ws269](../ws.md)）
- Status: in-progress（2026-10-21: clear の条件をそろえた。板 96 の vkmatrix の結果の後に Q1 が判定する。下の「clearance の記録」）
- Disposition: normal
- Queue: q1395 / q1395-i01、q1398 / q1398-i01、q1401 / q1401-i01（2026-10-20 P1）

## 結論の要約（2026-10-21）

- **TA の lockup の原因**: 我々の VDM の control stream の末尾に、Mesa の terminate の PPP state の block が無かった。この block は PPP_STATE0/1 で 3 語の state（header は pres_terminate、TERMINATE0 は clip の right・bottom、TERMINATE1 は 0）を指し、その後に terminate が来る。この block が無いと VDM は terminate を完了しない。最小の stream で通り（板 77）、全 stream に入れて三角形・job が通った（q1484、板 78）。
- **userland の不通の原因**:
  - kernel の queue family が COMPUTE だけを公開していた。
  - vkimage が API 1.1 を求めていた（q1485、板 81）。
  - libc の fscanf が 1 行しか読まなかった（BUG-344、gpu-selftest は回避。q1489、板 83）。
- **compute の原因**: PDS program の差。Mesa の形（local id の unified store への DOUTW、task issue の cc bit、base workgroup の add）なら 128 workgroup で 4096/4096（板 87、P2 q1496 で既定に取り込み中）。
- **除外した項目**（括弧は板の番号か Queue）:
  - PC set・register range（47・55・57、q1449〜q1459）。
  - FW の context switch の flags・profile（59・66、q1463）。
  - static switch state を 0 にする（49、q1450）。
  - border colour table（61、q1468）、resume size を 0 にする（62）、single-core の flag（63・76）。
  - PM_META_PROTECT（q1471）、region header の heap（67、q1472）。
  - 594 MHz（73、q1478）、起動前の EVENT_CLEAR（70、q1477）。
  - HWRT の record の全 field（q1480、一致）、起動列・config（q1475、一致）、geometry の register 値（q1482、IMG の capture と一致）。
- **残り**: fence kernel の lockup（K0 の shared の確保と sd_type の差が候補）、mesa-form の probe の整理、compute-check の診断の行の整理、BUG-344、Linux の walk（この kernel では /dev/mem・/proc/kcore が使えず、kernel module が要る。q1495）。

## clearance の記録（2026-10-21、N1、判定は Q1）

- clear の条件（板）:
  - TA・3D：render check の三角形 3156 pixel、job check の色と depth（板 78 以降）。
  - compute：vkcompute の 128 workgroup 4096/4096、3 kernel の stream（板 91・93）。
  - transfer：transfer check（clear・copy）。
  - present：vkdemo の cube（HDMI で目視）。
  - `gpu-selftest` 13 ok / 0 fail / 0 skip、context reset 0（板 93・94）。
  - vkcoherency PASS（buffer 1000 round・image 200 round、stale 0、板 95）。
  - vkmatrix（Keiland の範囲の 13 case）：**板 96 の結果待ち**。
- 残件の行き先:
  - fence kernel の lockup：解決（K0、q1501・q1502）。
  - 手書き kernel の退行：解決（fence を戻した、P2 の q1503）。
  - 診断の整理：済（q1493）。
  - BUG-344（libc の fscanf）：libc の stdio の WS へ移す（WS269 は依存しない）。
  - Linux の walk（IMG の stack の control stream）：Future Work へ（kernel module が要る。再検討は IMG との差が再び要る不具合が出た時）。
  - border colour table の cache：SLC bypass にした（q1509）。
- 次は [proposal-next-phases.md](../proposal-next-phases.md) の N2〜N6。


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

## q1427-i01（P1、2026-10-20）: geometry（dm 2）の reset lockup の切り分け

Q1 の直し（main d851a8783・1512e79cd）の後の板（vf2-dmesg25）:
- compute の probe と transfer（clear・copy）は通った。
- render check は、geometry の kick の後に context reset（context 0x4、reason 1、dm 2）、`ta: fail(reset lockup)` の後に `ta: ok`・`3d: ok`。fence には達するが、colour は 0。job check も同じ。

直し（診断、`render-check.c`）:
- 三角形の render の前に、VDM の stream を end（`POWERVR_VDM_TERMINATE`）だけにした render を 1 回流す（`check_empty`）。行 `render check: empty stream: finished/FAILED (e), context kept/reset` を出す。reset か失敗なら FW の dump（kick の直前の印からの trace の全部）を出し、三角形は流さない。
- 三角形の kick の直前にも trace の印を付ける。fence に達しても context が reset されていれば dump を出す。行は `fence reached (…), context kept/reset`。
- 読み方:
  - 空の stream でも reset するなら、geometry の command・HWRT・free list・context の record・SR/PT の program の側。
  - 空は通って三角形で reset するなら、VDM の stream・PPP・vertex の program の側。
  - trace は CSW・BIF・HWR の group を含む（log type 0x4637）。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1429-i01（P1、2026-10-20）: geometry の lockup の続き — trace の読みと record の dump

板（vf2-dmesg26）: 空の stream でも geometry の context が reset された（stream の外が原因）。

trace（mainline の `pvr_rogue_fwif_sf.h` で読んだ）:
1. Kick TA の前: KCCB slot 44 の kick、cCCB Woff 200 DM 2 FWCtx 0xc0030000、Woff 616 DM 3 FWCtx 0xc0030098、fragment の UFO PR-Check（[0xc0055000] ≥ 1 を待つ）。
2. Ready-to-run DM 2。dust の電源 0 → 1、HW Request On が完了。
3. BIF: Activate MemCtx 0xc002b000 DM 2、PC 0x4060e000、PC set 5（range 6）、Grab reg set 5。
4. `700b1077` は「Kick TA: FWCtx 0xc0030000 @ 0, RTD 0xc0062000, First kick 1, Last kick 1, CSW resume 0 …」。11 個の引数は FWCtx・CCB の offset・RTD（HWRT data の FW の address）・first・last・CSW resume・PID・prio・frame・ext・int で、register の値ではない。
5. その後、「DM2 has taken N ticks」「Fast CRC / Full Signature Check」「USC slots used by DM 0」で recovery。
6. FWCCB に free list の grow の要求は来ていない（write 2 は reset の通知など）。

TA は始まったが何も進まない。USC は使われず、MMU の fault も grow も無い。

照らした事:
- 我々の geometry の command は、mainline の `rogue_fwif_cmd_geom` の regs（vdm_ctrl_stream_base 0、border colour 8、ppp_ctrl 28、te_psg 32、resume_task0_size 40、pds_ctrl 48、view_idx 52）に対し、stream・PPP_CTRL（wclamp と fixed point）・TE_PSG（complete on terminate と region の stride）・resume size（PDS_STATE0 の state update と common size）・flags（first と last）を入れている。Mesa の `pvr_geom_state_stream_init` と同じ組。border colour table・pds_ctrl・view_idx は 0（Mesa も view_idx は 0、pds_ctrl は stream に無い）。BRN 52942 の forcenewstate は未確認。
- free list は物理の page 番号（`powervr_free_list_entry`）で、mainline の `pvr_free_list_insert_pages_locked` と同じ形。record の current_dev_addr は先頭（max = current の時、mainline の式と一致）。

直し（診断）: render check の空の stream・三角形で context が reset された時に、次を出す。
- `powervr_target_dump`（`render-target.c`）: target の buffer（macrotile・region header・vertex heap・tail pointer・local の free list と物理の先頭）の device の address、HWRT の data と common の record の全部の word、local の free list の record。
- 空の stream の失敗では、geometry の command の 28 word も出す。
- 次の結果で、mainline の `hwrt_data_init_fw_structure`・`hwrtdata_common_init` と field ごとに照らす。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1431-i01（P1、2026-10-20）: HWRT の record の読み（vf2-dmesg28）と context の dump

Q1 の 3 つの問いへの答え（mainline の `pvr_rogue_fwif_check.h` の offset で読んだ）。

(1) geometry command の先頭 16 byte は cmd_shared（frame_num・hwrt_data_fw_addr など）。render check が出したのは kick の前の雛形で、`powervr_session_kick_render` が CCB に写す時に RTD を入れる（trace の Kick TA の RTD 0xc0062000 と一致）。regs は 16 から:
- 16 vdm_ctrl_stream_base = 0x8000013000
- 44 ppp_ctrl = 0x202
- 48 te_psg = 0x20000
- 56 resume_task0_size = 0x20010000（state update と common 1）
- 80 flags = 3

te_psg の region stride は 0。Mesa も layers 1 では stride = header の大きさ（0x80）/ 4096 = 0 なので同じ。

(2) HWRT data（半分ずつ 0x180、`rogue_fwif_hwrtdata`）:

| offset | field | 値 |
| --- | --- | --- |
| 0 | pm_mlist | 0x800000a000 |
| 0xa4 | common | 0xc0062300（record の +0x300） |
| 0xac | state | 9（HWR、reset の後の値） |
| 0xb0 | freelists | local 0xc0061000、global 0xc0060000、3 つ目 0 |
| 0xc8 | vheap | 0x800000f000 |
| 0xd0〜 | rta_ctl | max_rts 1 |
| 0xf0 | tail | 0x8000011000 |
| 0xf8 | macrotile array | 0 |
| 0x100 | region header | 0x800000d000（2 つ目は +0x80） |
| 0x108 | rtc | 0 |
| 0x114 | geom_caches_need_zeroing | 1 |

- macrotile array の 0: Mesa の `rogue_get_macrotile_array_size` は、SIMPLE_INTERNAL_PARAMETER_FORMAT の core（BXE）では 0 で、address は無効（0）。正しい。
- rtc の 0: layers 1 では Mesa も無効。正しい。
- region header: BRN 63142 が無い core（DDK の `rgxcore_km_36.50.54.182.h` は BRN 63553・71317 だけ）では、Mesa は general の heap に置く。正しい。
- common（+0x300、`rogue_fwif_hwrtdata_common`）:
  - screen_pixel_max 0x003f003f
  - multi_sample_ctl・flipped 0x88
  - tpc_stride 0・tpc_size 0x80（layers 1 で Mesa と同じ）
  - te_screen 0x3003
  - mtile_stride 4
  - te_mtile1・2 0x80000
  - isp_merge は 0（ROGUEXE で tile 16 の時、Mesa も入れない）
  - rgn_header_size 0x10
  - isp_mtile_size 0x20002

  見た範囲で Mesa の式と食い違いは無い。

(3) local free list（`rogue_fwif_freelist`）:
- list 0x8000008000、top 0x8000008000
- stack_top 0x1f、max 0x20、grow 0、current 0x20、allocated 0、id 1

mainline の `free_list_create_fw_structure` の式（current = 初めの数 − ready、stack_top = current − 1）と一致（Q1 の読みの「current 0x1f」は stack_top）。global の record は今回の dump に無い（target が持たない）。

直し（診断）: 空の stream の失敗の時に、次を出す。
- render context の record の全部（680 byte: geometry と fragment の common の part と switch の state）
- geometry の state（160 byte）
- geometry の CCB の control と先頭 256 byte（header と command の実物）

次はこれを mainline の `rogue_fwif_fwrendercontext`・`rogue_fwif_geom_registers_caswitch`（vdm_context_state_base 0、resume 8、ta_context_state_base 16、task の word 24〜）と照らす。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1433-i01（P1、2026-10-20）: render context・geometry state・CCB の読み（vf2-dmesg30）

mainline の offset（`rogue_fwif_fwrendercontext` = geometry の common 0〜0x97、fragment の common 0x98〜0x12f、static の state 0x130〜（368 byte、`rogue_fwif_geom_registers_caswitch`））で読んだ。

| 物 | 値 | 照らした結果 |
| --- | --- | --- |
| geometry の common | ccbctl c004c000、ccb c004d000、context_state c0031000（+0x18）、priority 0、dm 2（+0x3c）、deadline 0x7530 = 30000 ms（+0x68、mainline の MAX_DEADLINE_MS と同じ形）、memory c002b000（+0x88）、id 4（+0x8c） | 一致 |
| fragment の common | ccbctl c0056000、ccb c0057000、state c004b000、dm 3（+0xd4）、deadline 0x7530、memory c002b000、id 4 | 一致 |
| static の state | vdm_context_state_base 0x8000001010（+0x130）、resume 0（+0x138）、ta_context_state_base 0x8000001070（+0x140）、buffer 0・1 の store・resume の task の word（+0x148〜） | 形は一致 |
| geometry state（`rogue_fwif_geom_ctx_state`） | vdm_call_stack_pointer 0、vdm_call_stack_pointer_init 0x8000001000 | mainline の `reg_state_init` と同じ（init だけを入れる） |
| VDM の call stack | 0x8000001000（8 byte、Mesa の `PVR_VDM_CALLSTACK_MAX_DEPTH` 1 × 8）。VDM の state は +0x10、TA の state は +0x70 | 重なりは無い。`render-context.c` の static assert で大きさを確かめている |
| CCB の header | 0x2abc80c9。201 \| magic \| 0x8000 | bit 15 は mainline の `ROGUE_CCB_TYPE_TASK`。GEOM の型（`ROGUE_FWIF_CCB_CMD_TYPE_GEOM` = 201 \| magic \| TASK）そのもの。size 0x70 = 112（`rogue_fwif_cmd_geom` の SIZE_CHECK 112） |
| CCB の command | frame_num 0、hwrt c0062000、regs（stream 0x8000013000、ppp 0x202、te_psg 0x20000、resume 0x20010000）、flags 3、partial_render_geom_frag_fence = { c0055000, 0 } | fence の値 0 は未確認（mainline では fragment の PR の fence の値） |
| CCB の続き | UPDATE（2abc00d5、8 byte）で c0055000 = 1 | 一致 |
| CCB の control | write・read・dep が 0xc8、wrap 0x7fff | 0xc8 = 40 + 112 + 40 + 8 = 200 で、kick した書き込みの後ろ。FW が全部読んだ後の値で、write offset は進んでいた |
| deadline 以外の +0xc0 の 3、+0x100 台の 1 | fragment の dm 3、FW が書く run の field | 意味は一致 |

結論: context・CCB・geometry state・HWRT（q1431）は、どれも mainline・Mesa と形が一致した。空の stream の TA の停止の原因は見つからない。FW の trace は Kick TA の後に event が無い。前に読んだ host の register は、job の最中でも 0 だった。

次の候補（未実施、判断が要る）:
- partial_render_geom_frag_fence の値を、mainline のように fragment の PR の fence の値（kick の後の値）にする。
- dummy_rgnhdr_init_geom_regs（command の 96）を Mesa のように設定する（BRN の要否を確かめる）。
- domibel の stack で同じ空の stream を流して比べる（その環境は手元に無い）。

source の変更は無し。

## q1434-i01（P1、2026-10-20）: Q1 の 2 つの疑いの照合

- (1) VDM の stream の terminate: Mesa（25.0.7 の `csbgen/rogue_vdm.xml`）の VDMCTRL の block_type は 3 bit（29〜31）で、STREAM_TERMINATE は 6（`<struct name="STREAM_TERMINATE">` の block_type 29..31、context の bit 0）。こちらは 3 つとも `6 << 29` = 0xC0000000 で正しい。
  - `render-kick-layout.h` の `POWERVR_VDM_TERMINATE`（render check の空の stream と `render-job.c`）
  - `render-program.c` の `VDM_TERMINATE << VDM_BLOCK_SHIFT`（三角形の stream）
  - P2 の `render-draw.c` も BLOCK_SHIFT 29 で、PPP 0・PDS 1・VDM 2・INDEX 3 の値は一致。terminate は render-job.c の共通の値を使う。
  - 外れ。
- (2) partial render の fence: mainline の `pvr_queue.c`（master の 718〜727 行）は、geometry の job の `partial_render_geom_frag_fence` を、**その queue（geometry）の timeline の UFO** の address と `done_fence->seqno - 1` にする。こちらの `render-kick-layout.c`（190〜204 行）も、geometry の queue の timeline と `value - 1` で、最初の kick では 0。一致しており、外れ。
- source の変更は無し（直す物が無い）。新しい vmunix は無い。

## q1436-i01（P1、2026-10-20）: geometry だけが使う物の照合

- (1) `geom_caches_need_zeroing`: mainline の `pvr_hwrt.c`（286 行）は common の `geom_caches_need_zeroing` を false にし、data の側（+0x114）は入れない（0 のまま）。こちらの `render-target-layout.c` も data の `clear_geometry_caches` を書かない（empty の record から始めて名前の無い field は 0）、common の側も 0。板の dump の +0x114 = 1 と state 9（HWR）は、reset の後に FW が書いた値。host が 1 にしているのではないので、「0 にして kick する」切り分けは今の形がすでにそれ（kick の時は 0）。
- (2) VDM sync の固定の program: Mesa 25.0.7 の `pvr_winsys_helper.c` `pvr_setup_static_vdm_sync` の USC は 8 byte の `44 A0 80 05 00 00 00 FF`。こちらの `fixed-area.c` の `sync_shader` を host で encode した byte（`build/q1436/sync`）は `44 A0 80 05 00 00 00 FF` で、1 byte ずつ一致。PDS は DOUTU の task の word（USC の offset、temps 0、sample rate INSTANCE 0）と、constant 0 からの task の出力・END の 1 word（0xf4000002）。compute の kick の program と同じ encoder の道で、Mesa の `pvr_pds_kick_usc` の形（data の 64 bit の task の word、code の DOUT・END）と一致。
- (3) global free list の record の dump を足した（`powervr_session_global_list_dump`、render check の失敗・reset の時）。
- 確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1440-i01（P1、2026-10-20）: FW の trace の group を全部にする、reset の後の「ok」の行を止める

- global free list の record（板）: stack_top 0xfff、max 0x1000、current 0x1000、ready 0、id 0。mainline の形と一致。
- FW の trace の log type を 0x7fff にした。MAIN 0x2 から MISC 0x4000 までの全部の group（MTS・CLEANUP・CSW・BIF・PM・RTD・SPM・POW・HWR・HWP・RPM・DMA）で、DEBUG（0x80000000）は除く。空の stream の kick から reset までの PM の free list の扱い、RTD の HWRT の処理、TA の開始・終了の event を見る。
- `ta: fail(reset lockup)` の後に `ta: ok`・`3d: ok` が出ていた理由: `powervr_render_finish`（`render-queue.c`）は timeline に達すると、free list の印だけを見て TA・3D の段を ok にしていた。FW が context を reset した render の timeline は、reset が信号を出したので達する。session が lost（reset の通知の後）の時は ok を出さず、EIO で返すようにした。render check の空の stream は、これで「FAILED (5)」と dump になる。
- 確かめ: 試験の image の config の vmunix は rc 0、warning 0。p010 は 0 failed。実機は未。

## q1442-i01（P1、2026-10-20）: 全部の group の trace の読み（vf2-dmesg39）

空の stream の kick の流れ（mainline の `pvr_rogue_fwif_sf.h`）:
1. KCCB slot 27 の kick、cCCB Woff 200 DM 2 FWCtx 0xc0030000、fragment（DM 3）は PR-Check（[0xc0055000] ≥ 1）で待つ。
2. Ready-to-run DM 2、client command header（CCB 0xc004d000）。
3. PM: 「PM running primary config (Core 0)」。
4. POW: dust 0 → 1、HW Request On 完了。BIF: Activate MemCtx 0xc002b000、PC 0x40622000、PC set 0。
5. RTD:
   - Load Freelist 0xc0061000（local）: 32 page、FL-addr 0x8000008000、stacktop 31。
   - Load Freelist 0xc0060000（global）: 4096 page、FL-addr 0x8000003000。
   - CONTEXT_PB_BASE 0x3、TA RTData 0xc0062000 loaded on HW context 0。
   - TA Buffers: VHeap 0x800000f000、TPC 0x8000011000。
   - TE Region headers base 0x800000d000（RGNHDR Init 1）。
6. PM:
   - 「Is TA 1, finished 0 on HW 0 (HWRTData 0xc0062000)」
   - UFL-TA-Base 0x8000003000（SP 4095）、FL-TA-Base 0x8000008000（SP 31）
   - ALIST0 SP 0、MLIST0 SP 0（VCE・TE・ALIST の cat base 0）
7. Kick TA（RTD 0xc0062000、first 1、last 1、CSW resume 0）。
8. その後、TA の完了の event（TA finished・PM の event・ISR の geometry の終わり）は 1 つも無い。HWR の検査の後、「Discarded cmd on DM3 (reason=1) HWRTData 0xc0062000 (st 9)」と recovery。

読み:
- FW は free list・HWRT・region header・PM を全部読んで設定した。値は host の record と一致する（free list の address・page 数・stack top、vheap、TPC、region header）。
- TA の hardware は kick の後、完了を出さない。FW は TA の終わり（geometry の ISR）を待っている。
- PM の cat base（VCE・TE・ALIST）が 0 なのは、最初の TA では mainline でも同じ（FW と PM が埋める）。
- 省かれた 796 word（kick と HWR の間）に PM・TE の event があるかを見るため、dump の全部出しの上限を 512 から 2048 word に上げた（今回の 1180 word は全部出る）。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1443-i01（P1、2026-10-20）: kick の後の trace の全部（vf2-dmesg40）と次の切り分け

Kick TA の後の全部の event（mainline の `pvr_rogue_fwif_sf.h`）:
- POW の確認、MTS の Bg・Irq の task の繰り返し、fragment の cCCB の Woff の更新だけ。PM・TE・RTD・SPM の event は 1 つも無い。
- HWR が 4 回「DM2 has taken N ticks」と調べ、USC slot 0、signature の register はどれも Old・Curr とも 0。
  - 0x5000 USC_UVS0_CHECKSUM
  - 0x5020 PPP_SIGNATURE
  - 0x5028 TE_SIGNATURE
  - 0x5030 VCE_CHECKSUM
  - 0x5060 PDS_DOUTM_STM_SIGNATURE
  - 0x4600（DDK に名前が無い）
  - 0x40E0 だけは 0x10000 で変わらない（DDK に名前が無い）
- 最後に「DM2 timed out」。

読み: FW は TA の割り込みを待つだけ。VDM・PPP・TE・VCE・USC は何も処理していない（どの signature も 0）。VDM が stream の最初の word を処理する前に止まっている見込み。

(a) について: mainline の sf の表で、TA の kick の時に FW が register の値（VDM_CTRL_STREAM_BASE・TE・PM）を出す event は見当たらない。値を出すのは HWR の signature の検査（上の行）だけ。

直し（診断、`render-check.c`）:
- (b) 空の kick の 100 ms 後に、geometry の register を続けて読んで出す。TIMER（最初と最後）、EVENT_STATUS、VDM_CONTEXT_STORE_STATUS（0x430）、0x40E0、0x4600、PPP・TE の signature、VCE の checksum、BIF の FAULT_BANK0 の MMU・REQ の status。
- (c) 最初の render の stream を、三角形の PPP state の block（2 word）の後に end を置く形にした（`POWERVR_RENDER_CHECK_STATE_FIRST`、既定 1、0 で前の end だけ）。行は `render check: state-and-end stream: …`。

確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1445-i01（P1、2026-10-20）: memory context の activate の比較

FW の trace の BIF の行（mainline の sf の表）:

| 板 | DM | 前の set | PC | 割り当て | 結果 |
| --- | --- | --- | --- | --- | --- |
| dmesg25・26（compute check の session） | 4（CDM） | 0 | 0x405df000・0x4060e000 | PC set 1 = register range [2 - 2] | Compute finished |
| dmesg40・42（render check の session） | 2（TA） | -1（新規） | 0x40601000・0x4060e000 | PC set 0 = register range [1 - 1] | lockup（何も処理せず） |
| dmesg40・42（reset の後） | 3（3D） | 0 | 同じ | PC set 0 = [1 - 1] | 直後に Deactivate（reset で捨てられた command の後で、実際に描いたかは不明） |

読み:
- 動いた data master（CDM）は catalogue の register の range 2 を、止まった TA は range 1 を使っている。range 1 で何かが動いた証拠は今のところ無い（3D は TA の reset の後に捨てられている）。
- FW は catalogue の register を自分で選んで書く（MIPS の FW、host は書かない）。host の読みでは、BIF_CAT_BASE0〜7 も MMU_CBASE_MAPPING も 0 に見えるので、どの register に何が入ったかは host から確かめられない。
- 仮説（未確認）: catalogue の register の 1 番（set 0）がこの SoC では使えない。または、その set の catalogue の写像が TA の要求（VDM の fetch）に届いていない。

次の切り分けの案（判断は Q1）:
- render の session で、TA の前に別の memory context の job を流し、set 0 を他に取らせた状態で TA を kick する（TA が set 1 = range 2 を使うか、それで動くかを見る）。
- 逆に、compute の probe を新しい session（set 0 = range 1 を取る）で流し、compute も range 1 で止まるかを見る。range の番号だけで決まるなら、原因は catalogue の register 1 の扱い（FW の設定、または XE の OSID の bank の対応）に絞れる。

source の変更は無し。

## q1447-i01（P1、2026-10-20）: page catalogue の set の切り分けと trace の dump の短縮

- split 1: render check の最初に transfer check（自分の session で clear と copy）を流してから、render の session を開いて TA を kick する。行 `render check: transfer before the render`。
  - transfer の session は TA の前に閉じるので、その memory context は deactivate される。FW がその後も set 0 を前の context に結び付けて残すか（TA が set 1 = range 2 を得るか）は、trace の BIF の行で見る。
- split 2: render の後に、もう一度 transfer check を新しい session で流す。行 `render check: transfer after the render`。TA が手放した set を transfer が取り、それで動くかを見る。job check はその後。
- dump の trace: 既定は、kick の印からの先頭 64 word と末尾 64 word（128 word を越えた時、間の数を 1 行）。全部を出すのは `POWERVR_RENDER_CHECK_FULL_TRACE=1` で build した時（上限 2048 word）。kernel の message の ring が dump で溢れて、前の行が消えないようにする。
- 確かめ: 試験の image の config の vmunix は rc 0、warning 0。実機は未。

## q1449 / q1449-i01（2026-10-20）

- dmesg44 の短い trace（head 64 + last 64 word、1052 word 省略）には BIF の「Alloc PC set」行も Kick TA 行も入っておらず、最初の transfer・TA・2 回目の transfer の PC set は判定できなかった（読めたのは MTS の DM=0 の kick のみ）。
- 変更: `render-check.c` の最初の transfer の session と memory を render の終わりまで閉じずに保持し（`transfer_check` の kept 引数、`transfer_close`）、TA が別の PC set を取るようにした。既定の trace の dump を all 1536 / head 256 / last 128 word に広げ、mark 以降の BIF 行が入るようにした。
- 確認: self-test config で `vmunix` を build（warning 0）。板の試験は未実施（Q1/T1）。
- 次: 板の dmesg の trace で TA の「Alloc PC set」を読む。set 1 でも TA が止まれば PC set の仮説は外れ。

## q1450 / q1450-i01（2026-10-20）

- dmesg47 の trace の判定: TA の kick の前の BIF の行は「Activate MemCtx=0xc0055000 DM=2」、「Alloc PC set 2 as register range [3-3]」、「Grab reg set 2」。transfer の session が active のままでも、TA は set 2・range 3（compute が動いた時は range 2）を得て、それでも止まった（USC slot 0、HWR は lockup）。PC set の仮説は外れ。
- (c) の切り分け: `render-private.h` に `POWERVR_RENDER_SWITCH_STATE`（既定は今 0）を足し、`render-context.c` の render context の static switch state（VDM・TA の state buffer と store・resume の task）を 0 のままにした。
- device の quarantine の直し（Q1 の追加）: 105 の context reset はもともとその session だけを lost にしていた。device を quarantine にしうる経路を 2 つ直した。
  - 監視の pass が、lost の session の未完了の command を overdue として数えないようにした（health check を走らせない）。
  - `powervr_render_stop_poll` で、FW が return slot を書かない時は kccb の executed count で cleanup の完了を判定するようにした。これで、session の close の stop が期限切れで escalate しない（escalate すると isolate、それが失敗すると fault になる）。
  - 112（FW の fault）と restart の要求は、これまでどおり device lost。
- 確認: self-test config で `vmunix` を build（warning 0）。板の試験は未実施。dmesg47 には device の quarantine を arm した行が無く、上の 2 経路のどちらだったかは未確認。

## q1453 / q1453-i01（2026-10-20）

- dmesg49 の結果: static state を 0 にしても TA は変わらず reset した。`POWERVR_RENDER_SWITCH_STATE` の既定を 1 に戻した。
- log を足した:
  - EIO で断る全部の場所に、理由（device faulted、session stopping・lost・isolated・abandoned）と context id を 1 行出す。場所は kick・reserve・commit・submit と、vk の submission（abandoned の時）。
  - quarantine を arm した時、session を lost・isolated にした時、device lost（112・restart の要求、restart の失敗）の時に 1 行ずつ出す。
  - vk の compute の kick の失敗と wait の失敗、run_frame の失敗にも 1 行ずつ出す。
- 105 の flags 0x1 は `POWERVR_RESET_FLAG_FAULT`（page fault。fault の address を報告するだけ）で、通知の context の session だけを lost にし、device は落とさない。device 全体の扱いになるのは flags 0x2（ALL）の時。
- 訂正: q1449・q1450 の時に私の確認の build を `CONFIG=` で流しており、実際には x86 の既定の config で build されていた。今回は `ZEDBSD_CONFIG=` で RISC-V の build を確認した（`build/q1453/st/vmunix`、warning 0）。q1449・q1450 の差分もこの build に入っている。
- 未実施: 板。submit を断った理由は、板の dmesg で特定する。

## q1456 / q1456-i01（2026-10-20）

- vkcompute の fault 0x8000025800 を切り分けるための log を `vk-queue.c` に足した。
  - dispatch の時: 各 buffer の binding の address の範囲と大きさ・offset・shared word、workgroup の数と workgroup id の shared word。
  - 最初と最後の kernel の USC の room と PDS の data の address。
  - run の時: stream の address と使った範囲、kernel の数、USC・PDS の使った範囲。
- 置き方の確認（机上）: 129 個の kernel（128 + fence）× 7 word × 4 = 0xe1c byte に terminate を足しても、0x1000 から始めて 0x1e20 で終わり、0x10000 の stream に収まる。
- 確認: RISC-V の self-test の build（`build/q1456/st/vmunix`）で warning 0。板は未実施（Q1）。

## q1459 / q1459-i01（2026-10-20）

- DDK（StarFive の tree）を読んで分かったこと: この core の page catalogue は `RGX_CR_BIF_CAT_BASE0..7`（0x1200〜0x1238）ではない。`RGX_CR_MMU_CBASE_MAPPING_CONTEXT`（0xE140）に context id を書き、`RGX_CR_MMU_CBASE_MAPPING`（0xE148、PC の address >> 12）に書く形である（rgxstartstop.c の RGXInitBIF の `#if defined(RGX_CR_MMU_CBASE_MAPPING)` の枝。XE_MEMORY_HIERARCHY の core）。trace の「register range [7-7]」は、この mapping context の 7 番のこと。MIPS の FW では `MMU_CONTEXT_MAPPING_FWIF` が `FWPRIV`（0）と同じなので、7 番は FW の予約ではない。
- `render-check.c` の `probe_geometry`:
  - 読む register を足した: BIF_CAT_BASE0..7・INDEX、BIF_CTRL、BIF_FAULT_BANK0 の MMU/REQ status、BIF_MMU_STATUS、BIF_STATUS_MMU、MMU_CBASE_MAPPING_CONTEXT、MMU_FAULT_STATUS・_META。
  - context 0..7 を 0xE140 に書いて 0xE148 を読む、各 mapping context の catalogue base を 1 行に出す。読んだ後で元の context を書き戻す。
  - 読む時点は、kick の 200 ms 後（100 ms から変えた）と wait の後（reset の後）。
- 確認: `build/q1459/st/vmunix`（RISC-V の self-test の build）で warning 0。板は未実施。

## q1460 / q1460-i01（2026-10-20）

- `POWERVR_RENDER_CHECK_FULL_TRACE` を `powervr-private.h` に移し、既定を 1 にした（この build 用）。1 の時の動き:
  - FW の trace の log type を 0x80007fff にする（全 group と DEBUG group）。
  - dump は mark（TA の kick の直前）から先頭の 1536 word と、末尾の 512 word（HWR まで）を出す。合わせて 2048 word 以下なら全部を出す。
  - render check は、空の stream の TA の後で終える（triangle、後の transfer、job check を行わない）。register の probe、target・global list・context の dump も行わない（dmesg の ring から押し出さないため）。
  - 通常に戻す時は 0 にする。
- host が register を読めない件の調査（DDK の StarFive の tree）: sf_7110 の sysconfig は 0x18000000 から 0x100000 の 1 つの window を ioremap して、host は直に読み書きする。MIPS の core には META の slave port のような FW 経由の読みは無い。OSID の view は MTS_SCHEDULE の間隔（0x10000）ごとで、OS0 は先頭にある。clock も DDK と同じ順で入れている（clk_axi、apb・rtc、core の分周、core・sys、apb の reset の解除）。DDK に、host の読みで FW と同じ値を得るための追加の条件（別の window・security の register・特別な clock）は見当たらなかった。この調べた範囲では「host からは読めない（原因は未特定）」と結論する。
- 確認: `build/q1460/st/vmunix`（RISC-V の self-test の build）で warning 0。板は未実施。

## q1463 / q1463-i01（2026-10-20）

- 我々と Linux の vendor の driver（dds_draw.txt）の config の比較:

  | 項目 | 我々（直す前） | Linux |
  | --- | --- | --- |
  | system flags | 0x00000010（POW_RASCALDUST のみ。SLC が小さい時だけ bit 19 DISABLE_DM_OVERLAP も立つ） | 0x00020010 |
  | OS flags（`RGXFWIF_OSDATA.ui32FwOsConfigFlags`、offset 0） | 0（書いていなかった） | 0x0000000F |
  | core clock | 396 MHz | 594 MHz |

  - 違う bit:
    - system flags の bit 17: `CTXSWITCH_PROFILE` が MEDIUM（2<<16）。
    - OS flags の bit 0〜3: `OS_CTXSWITCH_TDM_EN`・`GEOM_EN`・`3D_EN`・`CDM_EN`。
  - DDK では、OS flags は rgxsrvinit.c の GetFWConfigFlags で `ui32EnableFWContextSwitch & DM_ALL` から作られる。FW の側は binary なので、DM の扱いは読めない。GEOM の bit が 0 の時に FW が TA の kick の前の context store・resume の設定（VDM の context store の register など）を飛ばす・変える可能性はあるが、未確認。
- 直し（`firmware-boot.c`・`interface.h`）: system flags に MEDIUM の profile を足し（0x20010 になる）、OS data の config flags に 0xF を書く。起動時に値を 1 行の log に出す。
- core clock: DDK の sf_7110 の sysconfig は既定が 594 MHz（`RGX_STARFIVE_7100_CORE_CLOCK_SPEED`）で、DT に `/opp-table-0/opp-1250000000` がある時だけ 396 MHz（BIN2）にする。我々の vf2-gpu.c は 396 MHz に固定していて、その注記（「vendor の kernel はどの板も 396 で動かす」）は誤り。clock は TA の lockup とは関係が薄いと見て、変えていない。594 にするかは WS256 の判断。
- 確認: `build/q1463/st/vmunix`（RISC-V の self-test の build）で warning 0。板は未実施。

## q1467 / q1467-i01（2026-10-20）: executor の dispatch を 1 kernel にする

設計（Mesa 25.0.7 の `pvr_compute_update_kernel`・`pvr_pds_compute_shader`、`rogue_cdm.xml` に従う）:
1. kernel 語（`powervr_compute_kernel_words`。`struct powervr_compute_kernel` に `workgroups[3]` と `usc_common` を足した）:
   - KERNEL0: usc_common_size（bit 18〜26、64 byte の単位）= 係数 register 3 個 → 1。unified = 0。pds_temp_size は PDS の temp 4 個 → 1。pds_data_size は data 4 word → 1。usc_target は ANY。fence は 0。
   - KERNEL1: PDS の data の address（slot）。sd_type は 0、common_shared は 0。
   - KERNEL2: PDS の code の address。
   - KERNEL3〜5: workgroup の数 −1（x = groups−1、y = z = 0）。
   - KERNEL8: local size −1（shader の workgroup[0]、x のみ）と max_instances 0（32）。
   - 新しい field が 0 なら今までと同じ語になる（compute check は変わらない。p008 の host 試験 76/76 PASS）。
2. PDS（`powervr_compute_workgroup_program`、code 6 語・data 4 語）:
   - data: [0..1] DOUTU の task 語、[2] DOUTW の制御（COMMON_STORE、address 0、ALL64、last なし）、[3] DOUTW の制御（COMMON_STORE、address 2、UPPER、last）。
   - code: `BRA IF1 neg +4`、`DOUTW temp64 0 → cf0・cf1`（CDM が temp 0・1 に入れる x・y）、`DOUTW temp64 1 の上位 → cf2`（z は temp 3）、`HALT`（ここまでが係数の更新の pass で、workgroup ごとに 1 回）、`DOUTU end`、`HALT`（instance の pass）。
3. compiler（P2、shader-backend.c）への約束: request の kind が `POWERVR_SHADER_REQUEST_WORKGROUP_ID` の時、その word k（k = 0, 1, 2 が x, y, z）を、inline の値でも shared register でもなく、**`POWERVR_USC_BANK_COEFFICIENT` の register k** からの move で読む（`compile_inline` の時も同じ）。他の request（buffer の address、NUM_WORKGROUPS）は今までどおり inline でよい。
- vk-queue.c: `POWERVR_EXECUTOR_ONE_KERNEL`（既定 0）を足した。1 の時の動き:
  - dispatch を 1 回だけ compile する。
  - `build_kernel` に workgroup の数を渡し、PDS は workgroup の program にする。
  - slot の data の 4 語を書き、kernel の usc_common に 3、PDS の temp に 4 を渡す。
  - 0 の時は今までの kernel を workgroup ごとに出す形。上の 3 の compiler の変更が main に入ったら 1 にする。
- 確認: RISC-V の self-test の build を、`POWERVR_EXECUTOR_ONE_KERNEL` を 1 にした時と 0 にした時の両方で行い、どちらも warning 0（commit は 0）。`plan/ws269/tests/p008/run.sh` は encode 94/94・compute 76/76 PASS。板は未実施。
- 未確認の仮定: DOUTW の COMMON_STORE の address が係数 register の番号であること（Mesa の barrier_coefficient・work_group_input_regs と同じ扱い）、z が temp 3 であること（Mesa の add_base_workgroup の {0,1,3}）。

## q1468 / q1468-i01（2026-10-20）

- (1) TPU の border colour table を作った:
  - 表は session ごとに 1 つ、0 で埋めた 256 KiB（64 entry × 128 format × float/int の 2 × 16 byte、Mesa の pvr_border.c の entry の形）を、render_open で general heap に置く。entry 0（透明黒）は 0 で正しい。他の entry（不透明の黒・白）は今は 0 のままで、sampler の border は未対応。
  - 置く場所: geometry の regs+8（command の +24）と fragment の regs+344（command の +360）に、address の bit 2〜39 を入れる。
  - render_close で、keep でない時に解放する。
  - 作れなかった時は log を出して 0 を入れる（今までと同じ）。
- (2) `vdm_context_resume_task0_size` の 0x20010000 は、Mesa の `pvr_geom_state_stream_init` の VDMCTRL_PDS_STATE0 と同じ値（block_type PDS_STATE_UPDATE が bit 29、usc_common_size は shared register 1 個を 64 byte の単位に切り上げて 1、bit 16）。違わないので直していない。IMG の 0 も試せるよう switch `POWERVR_GEOMETRY_RESUME_SIZE_SET`（既定 1）を足した。
- (3) SINGLE_CORE の bit 3: mainline の `ROGUE_GEOM_FLAGS_SINGLE_CORE`（multicore の firmware で他の core を待たない）を、`gpu_multicore_support` の feature がある時に立てている。switch `POWERVR_RENDER_SINGLE_CORE_FLAG`（既定 1、0 で立てない）を足した。
- 確認:
  - RISC-V の self-test の build で warning 0。
  - host 試験: p009 の executor が 511/511。p010 は全部 0 failed（job 26・command 29・kick 81 など）。p010 の job-host-test に `powervr_session_border_table` の stub を足した。
  - 板は未実施。

## q1471 / q1471-i01（2026-10-21）: PM_META_PROTECT と SLC bypass

- bit: `ROGUE_MMUCTRL_PT_DATA_PM_META_PROTECT_EN` は page table entry の bit 62（0x4000000000000000）。mainline の pvr_mmu.c は `DRM_PVR_BO_PM_FW_PROTECT` の buffer に立てる。

| buffer | mainline の kernel | Mesa 25.0.0 | 我々（前） | 我々（後） |
| --- | --- | --- | --- | --- |
| free list（entry の配列） | PM_FW_PROTECT が必須、CPU の mmap は禁止（pvr_free_list.c） | GPU_UNCACHED と PM_FW_PROTECT | mappable（SLC bypass）、protect なし | SLC bypass と PM_PROTECT、mappable なし |
| vheap と RTC | — | GPU_UNCACHED | mappable（bypass） | 同じ |
| tail pointers（TPC） | — | GPU_UNCACHED | mappable（bypass） | 同じ |
| macrotile array と mlist | — | GPU_UNCACHED（25.0.0。25.0.7 の別の file は mlist に PM_FW_PROTECT） | mappable（bypass） | 同じ |
| region headers | — | GPU_UNCACHED、**rgn_hdr_heap**（mainline の ROGUE_RGNHDR_HEAP_BASE 0xDBF0000000、256 MiB） | general heap（0x80_0000_xxxx）、bypass | 同じ（下の注を参照） |
| FW の record（firmware heap） | FW の側 | — | MIPS の uncached | 同じ |

- SLC bypass の方針の差: mainline は `DRM_PVR_BO_BYPASS_DEVICE_CACHE`（Mesa の GPU_UNCACHED）の buffer だけを bypass する。我々は mappable か shareable の resource を全部 bypass している（`powervr_gpu_buffer_flags`）。render target の buffer では、Mesa も GPU_UNCACHED なので結果は同じ。違いが出るのは vk の buffer・image（Mesa は cached）。
- 実装:
  - `gpu-mmu.h`・`gpu-mmu.c` に `POWERVR_GPU_MAP_PM_PROTECT`（bit 62）を足した。
  - `render.c` に `powervr_resource_make_free_list`（bypass と protect、mappable なし）を足した。
  - `render-target.c` の free list の entry の配列にだけ使う。kernel は従来どおり自分の uncached の view から entry を書く。
  - PM の page（`list->pages`）は GPU の VA に map しない（PM は物理で扱う）ので、変えていない。
- 注（新しい候補、未確認）: Mesa は region header を専用の heap（RGNHDR、0xDBF0000000）に置く。TE の `TE_PSGREGION_ADDR` の base は bit 6〜33（shift 6）。我々の 0x800000d000 は general heap なので、heap の制約があるかを確かめる価値がある。
- 確認: RISC-V の self-test の build で warning 0。p010 の host 試験はどれも 0 failed。板は未実施。

## q1472 / q1472-i01（2026-10-21）: HWRT の address の field の幅

csbgen（Mesa 25.0.7 の `gen_pack_header.py` の `__pvr_address`: `((addr >> shift) << start) & mask(start..end)`。register に入るのは address の bit start〜end）と `rogue_cr.xml` で調べた結果:

| register（field） | bit | 表せる address | alignment | 我々の address | 入るか |
| --- | --- | --- | --- | --- | --- |
| TE_PSGREGION_ADDR（region headers） | 6〜33 | 下位 34 bit（16 GiB の窓） | 64 | 0x800000d000（bit 39） | **入らない**（下位の 0xd000 だけ残る） |
| TE_TPC_ADDR（tail pointers） | 6〜33 | 下位 34 bit | 64 | 0x8000011000 | 入らない（ただし Mesa も general heap 0x80_0000_0000 に置くので、FW か HW が上位を補っているはず） |
| PM_MTILE_ARRAY | 4〜39 | 40 bit | 16 | 0x800000a000 | 入る |
| PM_VHEAP_TABLE | 4〜39 | 40 bit | 16 | 0x800000f000 | 入る |
| PM_MLIST0_BASE | 4〜39 | 40 bit | 16 | （macrotile と同じ buffer） | 入る |
| VDM_CTRL_STREAM_BASE | 2〜39 | 40 bit | 4 | 0x8000013000 | 入る |
| TPU_BORDER_COLOUR_TABLE | 0〜37（shift 2） | 40 bit | 4 | general heap | 入る |
| ISP_SCISSOR_BASE | 2〜39 | 40 bit | 4 | general heap | 入る |

- mainline の heap（pvr_rogue_heap_config.h）: GENERAL 0x8000000000（128 GiB）、PDSCODEDATA 0xDA00000000（4 GiB）、**RGNHDR 0xDBF0000000（256 MiB）**、USCCODE 0xE000000000（4 GiB）、FW 0xE1C0000000、TRANSFER_FRAG 0xE400000000（16 GiB）、VISTEST 0xF200000000（2 MiB）。Mesa 25.0.0 は region headers だけを rgn_hdr_heap に置き、TPC・vheap・MTA・mlist は general heap に置く。
- 直し: session に region header の窓（`region_space`、0xDBF0000000〜、256 MiB、`powervr_session_heaps_init` で初期化）を足した。render target の region headers を `powervr_resource_make_region_headers` でそこに map する。resource は自分の取った窓（`resource->space`）を覚え、unmake ではその窓に返す。TPC は Mesa と同じく general のまま。
- 確認: RISC-V の self-test の build で warning 0。p010 の host 試験はどれも 0 failed。板は未実施（dump の `target: … region headers 0xdbf…` で配置を確かめられる）。

## q1474 / q1474-i01（2026-10-21）

- (1) 1 kernel の executor から fence kernel を外した（`POWERVR_EXECUTOR_FENCE_KERNEL`、既定は `!POWERVR_EXECUTOR_ONE_KERNEL`）。stream は kernel 語と terminate だけになる。compiler はもともと store ごとに `MACHINE_WAIT`（wdf）を出している。end の前の wdf は P2 の q1473 の約束に従う。注: fence が無いので、同じ submission の dispatch の間の順序は保証されない（vkcompute は dispatch 1 回）。
- (2) shared words の作り方の比較（compute-check の `run_compiled_fill` と vk-queue の `build_shared`）:

  | word | compute-check | vk-queue（1 kernel） |
  | --- | --- | --- |
  | DESCRIPTOR の low・high | probe の area の address（`run_address + COMPILED_OFFSET`）の low・high | `powervr_vk_buffer_address` + slot->offset の low・high |
  | WORKGROUP_ID | 0 | 0（1 kernel では compiler が cf から読む） |
  | NUM_WORKGROUPS | 全部 1 | x = groups、y・z = 1 |
  | 他の request | 0 | ENOTSUP で拒否 |

  形は同じ。違うのは値（buffer の address と workgroup の数）と、workgroup の数が 1 か 128 か。fault 0x8000470010 は buffer 0x8000047000 の low word が 4 bit 左にずれた形（0x47000 → 0x470000、+0x10）で、128 workgroup の時だけ出るなら、gid の計算（cf からの wg id × 32 + lane）の値か、shared words の値のどちらかと見る。判別のため、dispatch の時に各 shared word を 1 行ずつ log する（`vk: dispatch: shared word N = 0x…`）ようにした。直す点は、板の log と生成された code を見てから決める。
- p009 の executor の試験を 1 kernel の形（kernel 1 つ、workgroup の数 128、common 3、workgroup の PDS program、fence 無し）に直した。502/502 PASS。
- 確認: RISC-V の self-test の build で warning 0。板は未実施。

## q1475 / q1475-i01（2026-10-21）: DDK 1.19 の起動列との照合

(a) `rgxstartstop.c` の RGXStart と我々の `firmware-boot.c` の start（XE、MIPS、PHYS_BUS_WIDTH 36、`rgxconfig_km_36.V.54.182.h` の feature で枝を選んだ）:

| 順 | DDK | 我々 | 差 |
| --- | --- | --- | --- |
| 1 | SYS_BUS_SECURE_RESET の feature があれば SYS_BUS_SECURE に 0 を書き、読んで fence | 同じ（`secure`） | なし |
| 2 | SOFT_RESET を ALL（S7_TOP は無い）に、読んで fence | ALL（PBE2_IN_XE の時は ALL_PBE2）、XE_TPU2 なら SOFT_RESET2 も | なし |
| 3 | ALL から RASCALDUSTS を除く | 同じ（host の dust power の hack、`DUST_POWER_BY_HOST` は 0 で無効） | なし |
| 4 | SOFT_RESET = GARTEN | 同じ | なし |
| 5 | RGXEnableClocks: 何も書かない（自動の clock gating） | 書かない（register_check は読むだけで、試験の書き込みは元に戻す） | なし |
| 6 | __RGXInitSLC: SLC_CTRL_BYPASS は 8 KB の SLC か BRN 61450 の時だけ（この core は 16 KB で該当しない）。SLC_CTRL_MISC の下位 32 bit = HASH1、PSG の hazard の bit は残し、line が 1024 bit 未満なら BYPASS_BURST_COMBINER | `init_cache` で同じ | なし |
| 7 | ECC_RAMS の feature が無いので SAFETY_EVENT_ENABLE は書かない | 書かない | なし |
| 8 | RGXInitMipsProcWrapper: WRAPPER_CONFIG（bus 36 bit > 32 なので `REGISTERS_VIRTUAL_BASE >> align`、microMIPS）、REMAP1（boot）、BRN 63553 の REMAP5、REMAP2（data）、REMAP3（code）、bus 32 bit の時だけ REMAP4、GARTEN_WRAPPER = IDLE_CTRL_META、MIPS_DEBUG_CONFIG 0 | `init_wrapper` で同じ | なし。読み返しの 0x1CF80 は DDK と同じ値 |
| 9 | AXI_ACE_LITE の feature があるので CONFIGURATION を書く（3/3/2/2/2/2/2/2。BRN 42321・68186 はこの core に無い） | 同じ値。順も wrapper の後 | なし |
| 10 | RGXInitBIF: MIPS の feature があれば何もしない（MMU_CBASE_MAPPING は META・RISC-V の時だけ） | 書かない（mainline も MIPS では bif init を飛ばす） | なし |
| 11 | 32 cycle の wait → SOFT_RESET 0 → wait | usleep → 0 → usleep | なし |

- EVENT_CLEAR・EVENT_ENABLE・MTS・CLK_CTRL を host が書く所は、DDK の起動には無い（rgxinit.c の SAFETY の clear だけで、この core に SAFETY は無い）。EVENT_STATUS の bit 4（ISP_END_MACROTILE）が立ったままなのは、host の起動列の差では説明できない。

(b) sf_7110 の sysconfig と vf2-gpu.c:
- `u0_img_gpu_enable` の順: clk_apb、clk_rtc、core の分周（rate）、clk_core、clk_sys、rst_apb の解除、rst_doma の解除。その前の runtime resume で、PMU の turn-off mask 0 と clk_axi。
- vf2-gpu.c も同じ順。違いは rate だけ（DDK は既定 594 MHz で、`opp-1250000000` がある時だけ 396 MHz。我々は 396 MHz）。syscon への特別な書き込みは DDK に無い。
- DDK は bEnableActivePM・bEnableRDPowIsland を TRUE、ActivePMLatency を 100 ms にする。我々と mainline は APM の latency 0（persistent）。

(c) firmware に渡す config（我々の firmware は mainline の ABI なので、mainline の pvr_fw.c と比べた）:
- sysinit: fault page・PDS/USC の exec base・runtime・trace・sysdata・util の cb・初期 clock・marker 1・started 0 を mainline と同じに設定している。mainline は filter・HWPerf・align_checks・firmware_perf・APM・gpio を 0 にし、coremem は MIPS では無い。我々の 0 埋めと同じ。
- runtime: clock、persistent 1、dust 数 = cluster 数、watchdog 0。mainline と同じ。
- sysdata の flags: mainline は 0（SLC が 4 KB 以上）。我々は POW_RASCALDUST に加え、q1463 で CSW の MEDIUM の profile（0x20010）。
- osdata の config: mainline は 0、我々は 0xF（q1463。DDK に合わせた）。osinit の hwr_debug_dump_limit は 0 で同じ。
- 結論: TA の完了や fence に関係し得る差は、起動列・config の中には見つからなかった。残る差は、clock の rate（396 と 594）と、mainline からわざと変えた POW_RASCALDUST・CSW の flags だけ。コードは変えていない。

## q1477 / q1477-i01（2026-10-21）

- (1) `vf2-gpu.c` に `POWERVR_VF2_CORE_594`（既定 0）を足した。1 の時は core の分周の目標を 594 MHz（PLL2 1188 MHz の 1/2、DDK の既定）にする。FW に渡す clock（sysinit・runtime）は `core->core_rate`（clock の読み返し）なので、自動で合う。
- (2) ISP_END_MACROTILE（EVENT_STATUS の bit 4）:
  - (a) trace51・trace57 は、render check の mark（KCCB の slot 0x58）からの窓で、FW の boot の直後は入っていない。窓の中では最初の `MTS Irq Task` から 0x10 が出ていて、`GPU Units init` の前にも出ている。boot の直後の最初の位置は、今ある trace では確かめられない（全 trace の build で mark を FW の起動の前に置けば取れる）。
  - (b) `firmware-boot.c` に `POWERVR_CLEAR_EVENTS_AT_START`（既定 0）を足した。1 の時は、soft reset を Garten だけにして wrapper と AXI を設定した後、processor を reset から出す前に、EVENT_STATUS を読み、EVENT_CLEAR（0x138）に 0xffffffff を書き、もう一度 EVENT_STATUS を読んで、2 行の log に出す（before / after）。DDK の起動では host は event の register を書かないので、これは実験である。
  - (c) 観察: 我々の trace では毎回 `Irq Task (EVENT_STATUS=0x10)` の後に `Irq Task complete` が来て、その間に DM の処理の行が無い。FW は bit 4 に対応する DM の仕事が無いので、clear せずに無視していると見る（Linux では 3D の後に 0x18 などが出て、FW が clear する）。TA の完了の event（TA_FINISHED は bit 5、TA_TERMINATE は bit 6）は同じ register の別の bit なので、bit 4 が立ったままでも TA が終われば 0x30・0x50 のように見える筈。我々の HWR の時の値は 0x10 のままなので、TA は終わっていない（event が隠れているのではない）。
- 確認: RISC-V の self-test の build で warning 0（switch は既定の 0）。板は未実施。

## q1478 / q1478-i01（2026-10-21）

- (1) 594 MHz が効かなかった理由: CRG の表で `clk_gpu_core`（index 45）の divider の最小を 3 にしていた（TRM の表の「3 to 7」）。root（PLL2 1188 MHz）に対する `drv_vf2_clock_set_rate` の計算は ceil(1188/594) = 2 だが、最小の 3 に切り上がって 396 MHz になっていた。vendor の kernel は 594 MHz（PLL2 / 2）で動かしている（sysconfig.h の `RGX_STARFIVE_7100_CORE_CLOCK_SPEED`、Linux の FW の log の `Core clock set to 594000000 Hz`）ので、最小を 2 にした（`vf2-crg.c`）。switch が 0（396）の時は divider 3 で変わらない。FW に渡す rate は clock の読み返しなので、594 が出る筈。
- (2) `POWERVR_FIRMWARE_BOOT_TRACE`（`powervr-private.h`、既定 0）。1 の時の動き:
  - FW の trace の log type を全 group と DEBUG にする。
  - health check の後に trace を 0 語目から最大 2048 語 dump する（`powervr: boot trace: …` の後に、従来と同じ `dump: trace` の行）。
  - compute と render の check は行わない（dmesg の ring に残すため）。
  - FW の起動前の EVENT_STATUS は 0（板 70）なので、この dump で bit 4 が立つ所（`GPU init` の前後）を見る。
- 確認: RISC-V の self-test の build で warning 0。既定（0・0）と、両方の switch を 1 にした時の両方で確かめた（commit は既定）。板は未実施。

## q1480 / q1480-i01（2026-10-21）: HWRT の record と Mesa・mainline の値

64×64、1 sample、1 layer。tile は 16 px なので 4×4 tile。simple parameter format は version 2。板の dump は vf2-dmesg51 の `target: record`（half 0 は +000、half 1 は +180、common は +300）。

| field（mainline の struct） | Mesa・mainline の計算 | 期待値 | 板の値 | 一致 |
| --- | --- | --- | --- | --- |
| common.geom_caches_need_zeroing | false | 0 | 0 | ○ |
| screen_pixel_max | (w-1)、(h-1)<<16 | 0x003f003f | 0x003f003f | ○ |
| multi_sample_ctl / flipped | 1 sample の中心（8,8）と、y を反転した 16-8 | 0x88 / 0x88 | 0x88 / 0x88 | ○ |
| tpc_stride / tpc_size | 1 layer は stride 0。size は next_pow2(8)²/4 × 8 byte | 0 / 0x80 | 0 / 0x80 | ○ |
| te_screen | round_up(4,2)-1 = 3、Y は <<12 | 0x3003 | 0x3003 | ○ |
| mtile_stride | mtile_x × mtile_y = 2×2 | 4 | 4 | ○ |
| teaa | 1 sample | 0 | 0 | ○ |
| te_mtile1 / te_mtile2 | X1 = ceil(4/8)×2 = 2 を <<18、X2・X3 は 0（format 2） | 0x80000 / 0x80000 | 0x80000 / 0x80000 | ○ |
| isp_merge_* | roguexe で 16 px の tile なので 0 | 0 | 0 | ○ |
| rgn_header_size | (2×2) × (4×4) / 4（region header の個数） | 16 | 0x10 | ○ |
| isp_mtile_size | x = 2 を <<16、y = 2 | 0x20002 | 0x20002 | ○ |
| data.pm_mlist_dev_addr | mlist の buffer | macrolist（half ごと） | 0x800000a000 / 0x800000b000 | ○ |
| cat base・stack pointer | FW が書く | 0 | 0 | ○ |
| hwrt_data_common_fw_addr | common の FW address | c007b300 | c007b300 | ○ |
| hwrt_data_flags | mainline は書かない | 0 | 0 | ○ |
| state | 初期値 0、以後は FW が書く | — | 9（FW が書いた後の値） | — |
| freelists_fw_addr[0..2] | local・global の順 | local, global, 0 | c007a000, c0079000, 0 | ○（FW の trace も type 0 → 1 の順に load） |
| vheap_table_dev_addr | vheap | 0x800000f000 | 0x800000f000 | ○ |
| rta_ctl | index 0、active 0、valid 0、partial 0、max_rts = layers | max_rts 1 | 1 | ○ |
| tail_ptrs_dev_addr | TPC | 0x8000011000 | 0x8000011000 | ○ |
| macrotile_array_dev_addr | simple internal parameter format の core では MTA の大きさは 0（`rogue_get_macrotile_array_size`） | 0 | 0 | ○ |
| rgn_header_dev_addr | region headers（q1472 から 0xdbf…） | half ごと | 0x800000d000 / 0x800000d080（板 51 の時点） | ○ |
| rtc_dev_addr | 1 layer では RTC 無し | 0 | 0 | ○ |
| geom_caches_need_zeroing（data） | 初期値 false | 0 | 1（HWR の後に FW が立てる） | — |

- free list（local は +00、global は +00）: base・current の address、stack top（pages−1）、total・max・current の pages は mainline の `free_list_fw_init` と同じ形（local は 32 page、global は 4096 page、grow 0）。
- Mesa が TA の前に行う初期化と我々の比較:
  - vheap・tails・mlist・region headers: Mesa は `pvr_bo_alloc`（kernel の GEM で 0 埋め）だけで、中身を書かない。我々も 0 埋めの buffer。region header は FW が初期化する（trace の `RGNHDR Init: 1`）。
  - macrotile array: この core では無い（大きさ 0）。
  - TPC: 0 埋めのみ（Mesa も同じ）。
  - RTC: 1 layer では無い。
- 結論: 64×64・1 sample・1 layer の HWRT の record・common・free list の全 field は、Mesa と mainline の計算と一致した。直す差は無い。コードは変えていない。

## q1482 / q1482-i01（2026-10-21）: geometry の register と最小の stream

(1) Mesa 25.0.7 の csbgen の `rogue_cr.xml`（25.0.0 と同じ定義）で decode した:

| register | field（bit） | 我々 | IMG（capture） |
| --- | --- | --- | --- |
| TE_PSG | force_protect 22、cs_size 21、enable_pwr_gate_state 20（ROGUEXE）、enable_context_state_restore 19、zonlyrender 18、**completeonterminate 17**、cache_bypass 14、forcenewstate 13、region_stride 0〜10（4096 byte の単位） | 0x20000 = completeonterminate だけ。region_stride は 0（region header は 16 個 × 8 byte で 4 KiB 未満） | 0x20000（同じ） |
| PPP_CTRL | vpt_scissor 12、flush_mode 11、bfcull_restrict_clip 10、**fixed_point_format 9**、default_point_size 8、cull の disable 7〜2、**wclampen 1**、opengl 0 | 0x202 = fixed_point_format と wclampen | 0x202（同じ） |
| TPU | mcu_pds_l0_off 8、tag_cem_64_face_packing 7、tag_enable_mmu_prefetch 6、… madd_config_dxt35_transovr 0 | 0 | 0 |
| VDM_CTRL_STREAM_BASE | addr 2〜39 | 0x8000013000 | 0x80000ea000 |
| TPU_BORDER_COLOUR_TABLE_VDM | addr 0〜37（shift 2） | 表の address（q1468） | 0x2000026c00 |

(2) Mesa の `pvr_geom_state_stream_init` と我々の geometry command の対応（`render-command.c`）:
  - vdm_ctrl_stream_base、tpu_border_colour_table、ppp_ctrl（wclampen と fixed point 1）、te_psg（completeonterminate と region_stride。forcenewstate は quirk 52942 の時だけで、この core には無い）: 同じ。
  - vdm_context_resume_task0_size: Mesa の VDMCTRL_PDS_STATE0 の usc_common_size に合わせている（q1468）。同じ。
  - view_idx 0、tpu 0: 同じ。
  - pds_coeff_free_prog: ext の stream の物で、この core では使わない。0 で同じ。
  - flags: Mesa は is_first・is_last（FIRST|LAST の 0x3）。`use_single_core` は fragment が atomic を使う時だけ立つので、通常は 0。我々は multicore の feature で bit 3 を立てていた（0xb）。**違うので直した**: `POWERVR_RENDER_SINGLE_CORE_FLAG` の既定を 0 にした（IMG の capture も flags 3）。
(3) Mesa の最小の stream: `pvr_sub_cmd_gfx_build_terminate_ctrl_stream` は `pvr_cmd_buffer_emit_ppp_state`（PPP_STATE0 に word_count 3 と address の上位 byte、PPP_STATE1 に下位の word）の後に terminate を置く。PPP の 3 語は `pvr_framebuffer_create_ppp_state` の物で、次のとおり:
  - TA_STATE_HEADER は pres_terminate（bit 25）だけ。
  - TERMINATE0 は clip_right（bit 18〜26）と clip_bottom（bit 0〜8）。どちらも 32 px の block 数 −1 で、64×64 では 1 と 1。
  - TERMINATE1 は 0。
  - `render-check.c` の `POWERVR_RENDER_CHECK_STATE_FIRST` を 2 にすると、この形になる。3 語を general の +0x180 に置き、stream は STATE0・STATE1・terminate。log の名前は `terminate-state`。既定は 1 のまま。
- 確認:
  - RISC-V の self-test の build で warning 0。既定（1）と、switch を 2 にした時の両方で確かめた。
  - p010 の host 試験は failed 0。
  - 板は未実施。

## q1483 / q1483-i01（2026-10-21）: Linux 側で control stream を読む道具

- `plan/ws269/tests/p010-linux/walk.py`: PC の物理 address・GPU の VA・byte 数を取り、/dev/mem で Rogue の page table を歩いて、各段の entry（PC は 32 bit、PD は 64 bit で page size の code を含む、PT は 64 bit で flag を含む）と PA と中身を出す。entry の形は mainline の pvr_rogue_mmu_defs.h に従い、PD の page size（4 KiB〜2 MiB）にも対応する。page をまたぐ読みは page ごとに解く。
- `capture2.py`（gdb）: capture.py と同じ TA の kick の後、次の ioctl で firmware の trace（debugfs）の最後の `PC address=` を取る。TA cmd の +16（`vdm_ctrl_stream_base`）から 256 byte を読み、先頭の block が PPP_STATE0/1 なら、その指す PPP state の語も `/tmp/ta3d-capture2.txt` に出す。
- README に手順と、CONFIG_STRICT_DEVMEM の時の代替（`/proc/kcore` の PT_LOAD を使った物理 address の変換、drgn の `prog.read(…, physical=True)`、`iomem=relaxed` の限界、devmem2・dd が同じ制限を受けること）を書いた。
- 確認: host で、偽の物理 memory の file に PC・PD・PT・page を置き、`walk.read_va` が VA 0x80000ea010 → PA 0x5010 を解き、語を正しく読むことを確かめた（scratchpad の一時の試験で、repository には入れない）。板（Debian）は未実施（Q1）。

## q1484 / q1484-i01（2026-10-21）: 全 stream の末尾に Mesa の terminate state

- 板 77 で、Mesa の最小の stream（terminate の PPP state を指す PPP_STATE0/1 → VDM terminate）で TA が完走した。我々の stream は、PPP state の block が無いか、三角形の state（pres_terminate 無し）だけを置いて terminate していた。
- 直し:
  - `render-program.c` に `powervr_render_terminate_state`（3 語: header は pres_terminate だけ、TERMINATE0 は clip の right・bottom を 32 px の block 数 −1、TERMINATE1 は 0）と `powervr_render_terminate_stream`（PPP_STATE0（word count 3 と上位 byte）・PPP_STATE1（下位の word）・terminate）を足した。Mesa の `pvr_framebuffer_create_ppp_state` と `pvr_sub_cmd_gfx_build_terminate_ctrl_stream` の形。
  - 三角形の stream（`powervr_render_triangle_vdm`、VDM の語数を 10 から 12 に）の末尾をこの 3 語にした。terminate state の address は `triangle.terminate_address`。
  - job の道（`render-job.c`。vk の graphics もここを通る）の stream の末尾もこの 3 語にした。terminate state は general の +0xb00 に置き、job の幅と高さを使う。
  - render check の最初の render（`check_empty`）は、この末尾の 3 語だけの stream にした。`POWERVR_RENDER_CHECK_STATE_FIRST` とその 0・1 の分岐は消した。log の名前は `terminate-state`。
- 我々の PPP state の header と Mesa の対比（三角形の block、19 語。header は 0x007f05c7）:

  | bit | pres_ | 我々の三角形 | Mesa の draw（`pvr_emit_ppp_state`・clear の template） | Mesa の terminate state |
  | --- | --- | --- | --- | --- |
  | 0〜2 | ispctl、ispctl_fa、ispctl_fb | ○ | ○ | — |
  | 6〜8 | pds_state_ptr0〜2 | ○ | ○ | — |
  | 10 | region_clip | ○ | ○ | — |
  | 16 | wclamp | ○ | ○ | — |
  | 17 | outselects | ○ | ○ | — |
  | 18〜20 | varying_word0〜2 | ○ | ○ | — |
  | 21 | ppp_ctrl | ○ | ○ | — |
  | 22 | stream_out_size | ○ | ○ | — |
  | 25 | terminate | — | — | ○（TERMINATE0・1 の 2 語が続く） |

  Mesa も draw の state に pres_terminate を立てない。terminate は render の最後の、terminate state の block だけが持つ。我々に欠けていたのは、この最後の block である。
- 試験:
  - p010 の render-host-test の三角形の stream の golden を新しい末尾（0x00000300・0x81234600・0xc0000000）に直し、64×64 の terminate state（0x02000000・0x00040001・0）を足した（30/30）。
  - job-host-test の stream の末尾の確認を、terminate state の block と中身の確認に直した（29/29）。
  - p010 の他の試験と p009 の executor（502/502）は全部 PASS。
- 確認: RISC-V の self-test の build で warning 0。板は未実施（Q1）。

## q1485 / q1485-i01（2026-10-21）: userland の Vulkan の device と queue

- `present-clear: graphics queue family -8` の出所: kernel の `vk-device.c` の `families()` が、queue family を 1 つだけ、`VK_QUEUE_COMPUTE_BIT` だけで返していた。stage の結果とは関係なく、graphics を一度も公開していなかった。vkstage・present は graphics の family を探して `VK_ERROR_FEATURE_NOT_PRESENT`（-8）で止まる。直し: family の flags を GRAPHICS・COMPUTE・TRANSFER にした（executor は 1 つの queue で render pass（`run_job`）・dispatch・transfer を流す）。
- `vkimage: vkCreateInstance -9` の出所: vkimage が `apiVersion` に 1.1 を求め、libvulkan の `vkCreateInstance` は 1.0 以外を `VK_ERROR_INCOMPATIBLE_DRIVER`（-9）で断る（`userland/desktop/libvulkan/instance.c`。1.0 だけを公開する方針）。device の状態や compute の fail とは関係が無い。直し: vkimage は 1.1 の機能を使わないので、`VK_API_VERSION_1_0` を求めるようにした（`userland/tests/vkimage/main.c`）。
- compute の stage の fail で device が閉じるか: 板 78 の dmesg では、compute の reset は該当の session だけを lost にし（`session lost …`・`kick refused … device faulted 0`）、device の quarantine は起きていない。device と queue の公開は stage に依存しない（stage は log だけ）。理由を出す 1 行は、両方の test がすでに `<call> <VkResult>` を出しているので、library 側には足していない。
- vkstage の draw・sample・depth の見込み: vk の graphics は `vk-queue.c` の `run_job` → `powervr_render_job_run`（render-job.c）を通る。job check も `job_compile`（vk の graphics と同じ compiler の道）→ `powervr_render_job_run` で同じ道であり、板 78 で色と depth が通った。stream の末尾の terminate state（q1484）も、この道に入っている。違いは shader の中身（sample の texture の読み、push constant）だけで、道の違いは見つからなかった。
- 確認: RISC-V の self-test の build で warning 0。p009 の executor は 502/502。板は未実施。

## q1487 / q1487-i01（2026-10-21）: present の段

- 読み合わせ:
  - gpu-selftest の present は `vkdemo --offscreen --time-ms=0 --hold=0 --output=/tmp/gpu-selftest-vkdemo.ppm` を動かす。vkdemo の exit が 0 の時だけ frame を判定する（0 でなければ理由は `offscreen: …`）。
  - vkdemo の `vkdemo_write_frame` は `P6\n320 240\n255\n` と RGB の 320×240×3 byte を書く。gpu-selftest の `fscanf("P6 %u %u %u")` と、空白を含めて合う。
  - 板の理由は `frame not a 320x240 PPM` なので、vkdemo は exit 0 だったが、file の中身が合わなかった。候補は 2 つ: 前の run の古い file（別の大きさ・空・別の形式）が残り、vkdemo が書かなかった。または、書いた中身が短い。
  - 板の出力が無いので、どちらかは特定できない。
- 直し（`userland/tests/gpu-selftest/main.c`）:
  - offscreen の run の前に frame の file を `unlink` し、古い file が判定に入らないようにした。
  - header が合わない時は、理由に file の大きさと先頭 8 byte を出す（`frame not a 320x240 PPM (N bytes, starts …)`）。
- 確認:
  - gpu-selftest と vkimage の main.c を、UAT の config と同じ clang の引数で RISC-V 向けに compile した（warning・error 0）。
  - `build/uat/vf2-gpu/config.mk`（`config-visionfive2.mk` に COMPUTE_CHECK・RENDER_CHECK と、gpu-selftest・gpu-selftest-boot・vkcompute・vkimage・vkstage・vkdemo・libvulkan・libwayland-client など）の rootfs の一覧（`make -pn` で展開）に、/bin の gpu-selftest・vkcompute・vkdemo・vkimage・vkstage、/lib の libvulkan.so、firmware が入ることを確かめた。
  - 板は未実施。

## q1489 / q1489-i01（2026-10-21）: present の header の読み

- 原因は zedBSD の libc の `vfscanf`（`src/libc/stdio-extra.c`）。中身は `fgets` で 1 行だけ読んで `vsscanf` に渡す形なので、`fscanf(file, "P6 %u %u %u")` は 1 行目の "P6\n" しか見ず、1 を返す。PPM の header は 3 行にまたがるので、vkdemo の frame は正しい（230415 byte = 15 byte の header + 320×240×3）のに失敗していた。さらに、残りの行は読み捨てられる（stream の位置が 1 行先へ進む）。
- 直し（`userland/tests/gpu-selftest/main.c`）: fscanf を使わず、`ppm_header`・`ppm_number` で byte ごとに読むようにした。"P6"、空白・`#` の comment の行、10 進の幅・高さ・最大値を読み、最後の数の後の空白 1 byte まで読む。その後の `ftell` が画素の先頭になる。
- libc の bug（Q1 に ticket の起票を依頼。libc は私の担当の範囲外なので直していない）:
  - 症状: `fscanf`・`vfscanf`・`scanf` が、stream から 1 行（`fgets`、最大 4095 byte）しか読まない。
    - 改行をまたぐ書式（"%u %u" で数が次の行にある）が途中で止まる。
    - 1 行の中で書式が使わなかった残りは捨てられ、stream の次の読みに戻らない。
    - 4095 byte を超える行は切れる。
  - 期待: C の仕様どおり、書式の空白は改行を含む任意の空白を読み飛ばし、変換が使わなかった文字は `ungetc` で stream に戻す。
  - 再現: "P6\n320 240\n255\n" の file に `fscanf(f, "P6 %u %u %u", …)` を行うと、zedBSD では 0 か 1 を返す（glibc では 3）。
- 確認:
  - RISC-V 向けに UAT の clang の引数で `-fsyntax-only`（warning・error 0）。
  - host で `ppm_header` を取り出し、"P6\n320 240\n255\n" + 画素 1 byte の file を読んで、320・240・255 と次の byte が正しく出ることを確かめた。
  - 板は未実施。

## q1491 / q1491-i01（2026-10-21）: executor の temporaries field

- `vk-queue.c` の `build_kernel` を、`powervr_compute_temporaries_field(temps, 2, …)`（2 register 単位の自前の規則）から `powervr_compute_task_field(temps, …)`（compute の program が共有する 1 か所の規則。P2 の q1490 で単位を確定させる）に変えた。p009 の executor の試験の期待値も同じ関数に合わせた。
- Mesa の temps の単位:
  - `pvr_pds_setup_doutu`（pvr_pds.c）は、DOUTU の src0 の TEMPS（bit 35〜40、6 bit、`PVR_ROGUE_PDSINST_DOUT_FIELDS_DOUTU_TEMPS_MASK`）に、`DIV_ROUND_UP(usc_temps, 4)`（`..._SRC0_TEMPS_ALIGNSIZE` 4、`ALIGNSHIFT` 2）を書く。usc_temps は shader の `common.temps`（register の数）。最大は 63 × 4 = 252 register。
  - compute の kernel 語（`pvr_compute_update_kernel`）は、KERNEL0 に pds_temp_size（PDS の temp を 16 byte 単位）・pds_data_size・usc_unified_size（入力 register の数 × 4 を 16 byte 単位）と、usc_common_size（coefficient register、64 byte 単位）を書く。USC の temps は KERNEL0・KERNEL8 には入らず、DOUTU の field だけで渡る（KERNEL8 は local size と max_instances）。
  - Mesa の表記どおりなら単位は 4 register。板 84 は field を 4 倍にすると全 lane が正しく読んだので、実際の単位はそれより小さい（P2 が q1490 で確定）。executor はその規則を自動で受け取る。
- 併せて直した: p009 の試験の `expected` の buffer（0x400）が、main で 0x800 になった `QUEUE_USC_ROOM` より小さく、`compile_inline` が E2BIG になっていた（main の時点で 501/502 で、この直しの前から落ちていた）。0x800 にした。502/502。
- 確認: RISC-V の self-test の build で warning 0。板は未実施（P2 の直しと一緒に Q1 が流す）。

## q1493 / q1493-i01（2026-10-21）: 既定の起動の log を軽くする

- 板 85 の dmesg（全体で 1279 行・95 KB）で多かった行: compute probes の compiled fill（226）・dump（54）など P2 の compute-check の行、`dump: trace`（180）、`probe: register`（101、compute-check）、`render check: register`（52）、`map:`（約 40）。
- `POWERVR_DIAGNOSTICS`（`powervr-private.h`、既定 0）を足し、長い診断をその下に移した。

| 行 | 既定（0） | 1 |
| --- | --- | --- |
| firmware の段（`firmware: X start/done`、`render node start/done`、`still in … after N s`） | 残す | 残す |
| firmware の起動の register check と register map（`register check:`、`map:`） | 出さない | 出す |
| return slot の probe（`probe: returns …`、`slots …`） | 出さない（executed count の fallback の 1 行は残す） | 出す |
| coherency の 2 行 | 残す | 残す |
| render check の register probe（kick の 200 ms 後と wait の後） | 出さない | 出す |
| render check の失敗時の target・global list・context・geometry command の dump | 出さない | 出す |
| 失敗時の firmware の dump（header の行と trace） | 残す。trace は短く（全部で 128 word まで、超えたら先頭 32 + 末尾 64） | 従来（1536 / 256 / 128） |
| context reset notice、`session lost …`、`ta:`・`3d:` などの stage の行、`FAILED at … (error)`、`device quarantine armed`・`device lost` | 残す | 残す |
| `kick/reserve/submit refused (EIO)`（lost の session が kick ごとに繰り返す） | 出さない | 出す |
| vk の dispatch の配置（shared word、buffer、kernel の address、run の stream） | 出さない | 出す |
| vk の `dispatch of N workgroups: compiled to …`（dispatch に 1 行） | 残す | 残す |
| render check の自己試験（terminate-state → 三角形 → transfer → job check）とその結果の行 | 残す | 残す |

- 範囲外（P2 の担当）: `compute-check.c` の compute probes の行（compiled fill・dump・field・probe register で約 400 行）は、今回の dmesg の最大の量である。P2 の q1492 の後で、同じ `POWERVR_DIAGNOSTICS` の下に移すことを推める（UAT の config は COMPUTE_CHECK が有効）。
- 確認: RISC-V の self-test の build で warning 0（既定の 0 と、1 にした時の両方）。p008・p009・p010 の host 試験は failed 0。板は未実施。

## q1495 / q1495-i01（2026-10-21）: Linux の compute の kick を取る道具

- `plan/ws269/tests/p010-linux/capture-cdm.py`（gdb）:
  - `RGXKickCDM2`（bridge 129、function 5。入力 struct は packed で 108 byte、`pui8DMCmd` +40、`ui32CmdSize` +84、`ui32NumOfWorkgroups` +92、`ui32NumOfWorkitems` +96）で CDM の command を記録する。
  - 次の ioctl で、firmware の trace の最新の `PC address=` を取る。`walk.py` で、command の +40（mainline の layout の `cdm_ctrl_stream_base`。違えば環境変数 `CDM_STREAM_OFFSET`）の control stream を 256 byte 読む。
  - 各 kernel の語（KERNEL0 の present の bit で語数を数える）から、PDS の data・code を読む（PDS heap は 0xDA00000000）。
  - PDS の code の中の DOUTU（class 0xF、dst 2）の const64 から task 語を取り、USC の program を 1 KiB 読む（USC heap は 0xE000000000）。
  - 結果は `/tmp/cdm-capture.txt` に書く。
- `decode-cdm.py`: KERNEL0〜8 の全 field（Mesa の rogue_cdm.xml）と、DOUTU の exe_off・sample_rate・temps field（×4 register）・dual_phase を表にする。host で偽の capture を decode して、表示を確かめた。
- README に手順を書いた（vkcompute を Debian の gcc で `/tmp/vkcompute` に build する、/dev/mem の制限は walk.py と同じ）。
- 板（Debian）は未実施（Q1）。

## q1498 / q1498-i01（2026-10-21）: executor を Mesa の PDS の配置に

- P2 の q1496 で `powervr_compute_workgroup_program` が Mesa の bytes（data 8 語・code 10 語、code は slot の +0x20）を出すようになったのに合わせて、`vk-queue.c` の `build_kernel` を変えた:
  - `QUEUE_PDS_SLOT` を 0x40 から 0x80 にした（0x80 × 65 = 0x2080 ≤ `POWERVR_EXECUTOR_PDS_BYTES` 0x4000、static assert で確かめる）。
  - 1 kernel の時は data を 8 語全部書き、code を slot + `POWERVR_COMPUTE_PDS_GROUP_CODE`（0x20）に置いて、kernel の `code_offset` を合わせた。kernel の `usc_inputs` は `POWERVR_COMPUTE_GROUP_USC_INPUTS`（4）。
  - fence の program（slot 0、code は +0x10）は変えていない。
- p009 の executor の試験の期待（slot 0x80、code は +0x20、usc_inputs 4）を直した。502/502。
- 確認: RISC-V の self-test の build で warning 0。板は未実施（Q1 が vkcompute・gpu-selftest を流す）。

## q1501 / q1501-i01（2026-10-21）: Mesa の 3 kernel の compute stream（switch）

- Mesa の vkcompute の stream（`plan/ws269/tests/p008/mesa-pco-vkcompute-stream-bxe-4-32.txt`）は 3 kernel と terminate からなる:
  - K0 は shared register を確保する kernel（usc_common 64 byte、target ALL、sd NONE、usc_common_shared、PDS の data 32 byte、max_instances 8、1 instance）。
  - K1 は dispatch（sd_type USC）。
  - K2 は fence（sd PDS、usc_common_shared、max_instances 8）。
- 我々の fence kernel は、確保されていない shared register を解放しようとして止まっている可能性がある（K0 が無い）。
- `vk-queue.c` に `POWERVR_EXECUTOR_MESA_STREAM`（既定 0）を足した。1 の時の動き:
  - dispatch の前に K0 を置く（`build_shared_kernel`: usc_common 16 register = 64 byte、any_cluster 0 = target ALL、common_shared 1、sd NONE、max_instances 8、instances 1）。K0 の PDS は slot 0 の +0x40 に data 2 語（0）、+0x50 に code（halt 1 語）を置く。
  - dispatch の kernel は sd_type USC（`powervr_compute_kernel` に足した `shared_from_usc`。KERNEL1 の bit 2〜3 に 2 を書く）。
  - fence kernel を有効にする（`POWERVR_EXECUTOR_FENCE_KERNEL` の既定が MESA_STREAM で on になる）。
- `compute-program.c`・`.h`（P2 の file）には、`shared_from_usc` の 1 field と、PDS と USC の両方を同時に立てたら拒否する検査だけを足した。
- 確認:
  - RISC-V の self-test の build で warning 0（switch の 0 と 1 の両方）。
  - 既定の 0 で p009 は 502/502、p008 は encode 94・compute 77。
  - switch を 1 にすると、p009 の 1 kernel の形の期待 2 件が落ちる。stream が変わるためで、板で通ったら既定にし、試験を 3 kernel の形に直す。
  - 板は未実施（Q1 が `-DPOWERVR_EXECUTOR_MESA_STREAM=1` で vkcompute を流す）。
- 板で通らなかった場合: 1 submission の中の dispatch の間の順序は保証しない（fence が無い）、という制限を docs の Vulkan の queue の節と、この phase.md に書く（q1501 の (2)）。

## q1502 / q1502-i01（2026-10-21）

- (1) 板 91 で、3 kernel（K0 の shared の確保・sd USC の dispatch・fence）で 4 回の dispatch が reset 無しで通り、gpu-selftest は 13/13 になった。fence kernel の lockup の原因は、K0（shared register の確保）が無かったことで確定。
  - `POWERVR_EXECUTOR_MESA_STREAM` を既定 1 にした。**1 submission の中の dispatch は、各 dispatch の後の fence kernel で順序が守られる。**
  - p009 の executor の試験を 3 kernel の形に直した（K0 の語、dispatch の sd USC、fence の語、terminate）。504/504。
- (2) a4f8663f2 で `POWERVR_DIAGNOSTICS` の下へ移した、起動時の診断が core の状態に与える副作用の整理（板 92 で DIAGNOSTICS=1 の結果を待つ）:

  | 処理 | 書く register と値 | 起動後に残る状態 | 「手書き kernel が通る前提」になり得るか |
  | --- | --- | --- | --- |
  | `register_check` の最初 | `CLK_CTRL`（0x0000）を読む。host には 0 に見える | — | — |
  | catalogue base 7 | `BIF_CAT_BASE7`（0x1238）にパターンを書いて読み、0 を書く。32 bit の半分ずつの書きも同じ | 0（この core は MMU_CBASE を使い、BIF_CAT_BASE を使わない） | 低い |
  | clock の強制 on | `CLK_CTRL` に ALL_ON（0x5555…、各 field の ON）を 64 bit で書き、次に、最初に**読んだ値（0）**を書き戻す。もう一度、32 bit の半分ずつで ALL_ON を書いて 0 に戻す | 書きが届いているなら、`CLK_CTRL` は reset 値（AUTO）ではなく **0（全 field が OFF）**のまま FW の起動へ進む。FW が自分の初期化で CLK_CTRL を設定し直すかは binary で見えない | **最有力**。FW が CLK_CTRL を書き直さないなら、clock の gating の状態が DIAGNOSTICS の有無で変わる。USC の timing（P2 の madd の hazard の類）が変わり、手書き kernel の lane の結果が変わり得る |
  | bus secure | `SYS_BUS_SECURE` は読むだけ（0 にする書きは `start_processor` に元からある） | 変わらない | 無い |
  | SLC control | 読むだけ | 変わらない | 無い |
  | `register_map` | 7 つの band を読むだけ | 変わらない | 無い（読みの副作用を持つ register が band に入っていれば別） |
  | `powervr_firmware_return_probe` | FW の heap の page の CPU の cache line を invalidate して読む（GPU の register は書かない） | CPU の cache の line が落ちる。FW の heap に CPU の dirty な line があれば、それも消える | 低い。FW の object は uncached の view で書くので、dirty な line は無い筈 |

  - 訂正（2026-10-21）: 板 92（DIAGNOSTICS=1 で register_check・return probe 有り）でも手書き kernel は 6976/16384 で、CLK_CTRL の疑いは否定された。原因は板 69（bd39399ba）で compute check の stream から fence kernel を外したこと（kick の完了が store の完了より早い）。P2 の q1503 で 3 kernel の形に戻した。
  - 板 92 の後の切り分け: DIAGNOSTICS=1 で手書き kernel が戻るなら、`register_check` の clock の部分だけ（`CLK_CTRL` ← ALL_ON → 0）を独立の switch にして確かめる。戻るなら「起動時に CLK_CTRL を明示に設定する」のを正式の起動列に入れる（DDK・mainline の起動列は CLK_CTRL を書かない、q1475）。
- 確認: RISC-V の self-test の build で warning 0。p009 は 504/504。板は未実施。

## q1504 / q1504-i01（2026-10-21）

- `vk-queue.c` の shared register の確保を、P2 の q1503 の helper に寄せた: slot 0 の program は `powervr_compute_shared_program`、`build_shared_kernel` の語は `powervr_compute_shared_kernel_words`。自前の定数 `QUEUE_SHARED_COMMON`・`QUEUE_FENCE_INSTANCES` は消して helper 側（`POWERVR_COMPUTE_SHARED_COMMON`・`POWERVR_COMPUTE_FENCE_INSTANCES`）に寄せた。使わなくなった `pds-encode.h` の include も外した。
- p009 の executor の試験: K0 の期待を helper から取り、slot 0 の shared の program の確認を足した。505/505。
- 確認: RISC-V の self-test の build で warning 0。板は未実施。

## q1507 / q1507-i01（2026-10-21）: vkmatrix（Keiland の Vulkan の範囲の行列）

- `userland/tests/vkmatrix/`（vkstage と同じ作り。標準の Vulkan C、64×64 B8G8R8A8 の offscreen、CPU の期待の画と読み返しを比べる）。各 case は `vkmatrix: <case>: PASS|FAIL(...)` と合計の 1 行。引数で 1 case だけ走らせる。
- 範囲は Keiland（`userland/desktop/wayland/`）の使い方に合わせた。
  - 使う物:
    - descriptor は set 0・binding 0 の combined image sampler だけ。
    - push constant は 32 byte と 96 byte（panel）で、vertex と fragment の両 stage。
    - batch の vertex は 1 binding・stride 44（R32G32、R32G32B32A32 ×2、R32 の SFLOAT）。compose は R32G32 の stride 8。
    - sampler は NEAREST・LINEAR の CLAMP_TO_EDGE で、mip は 1。
    - blend は ONE / ONE_MINUS_SRC_ALPHA（premultiplied）。
    - viewport・scissor は dynamic。
    - depth は無し。
  - 省いた物（Keiland が使わない）: uniform・storage・dynamic の buffer、set 1、mip 2 段、repeat、R8G8B8A8・R32 UINT の vertex、depth の混在。
- case（13）:

  | case | 中身 |
  | --- | --- |
  | push4 | 4 byte の push constant、fragment、3 draw で値を変える |
  | push32 | Keiland の compose の 32 byte、vertex（offset）と fragment（色）、3 draw |
  | push128 | 128 byte（panel の 96 byte を含む大きさ）、最後の vec4 と中間の 1 語を読む、3 draw |
  | batch | batch の vertex の全 attribute |
  | nearest | 4×4 の texture、NEAREST |
  | linear | 2×2 の texture、LINEAR、clamp。期待は CPU の bilinear（tolerance 3） |
  | two-textures | 2 つの set を 1 pass の中で切り替える |
  | blend-over | premultiplied の alpha 合成 |
  | blend-add | ONE / ONE |
  | draws-32 | 1 pass に 32 draw |
  | draws-64 | 1 pass に 64 draw（16 を超える） |
  | two-passes | clear の pass の後に load の pass、1 submit |
  | scissor-viewport | scissor・viewport を draw の間で切り替える |

- 生成物と build:
  - shader（GLSL）は `shaders/`、`shaders/regenerate.py` で `shader.h`（SPIR-V の語）を生成する。build に shader の compiler は要らない。
  - `platform/common/userland-dynamic.mk` に vkmatrix の dynamic の link の規則（vkstage と同じ形、libvulkan.so）を足した。package の platform は visionfive2 だけ。
  - `plan/ws269/tests/p010/config-visionfive2-selftest.mk` に vkmatrix を足した。
- 確認:
  - host の lavapipe（`plan/ws269/tests/vkmatrix/run.sh OUTDIR`）で 13/13 PASS。
  - RISC-V の build（`bin/vkmatrix`、libvulkan.so の dynamic、warning 0）。
  - 板は未実施（Q1 が /bin に置いて流す）。

## q1510 / q1510-i01（2026-10-21）: vkmatrix の draws-32・draws-64・two-passes

- 板 96: この 3 つが `FAIL(pixel (0,0) 0x00000000, expected 0xff000080)`（clear の色も無く、全 channel が 0）。他の 10 case は PASS。
- 3 つに共通するのは、colour の LOAD の道である。
  - draws-32・draws-64: 16 draw を越える pass は `powervr_render_job_continue` で 2 つ目以降の job が LOAD。
  - two-passes: app の 2 つ目の pass が LOAD。
  - LOAD は vkstage・gpu-selftest・render check・job check のどれも使っておらず、板で通るのは初めて。(0,0) は 1 つ目の job が clear する背景なので、2 つ目の job の LOAD（`powervr_render_load_usc` の sample で背景を作る）が 0 を読んで上書きしている、と見る。
  - dmesg96b には `vk: a render pass did not run`（job の失敗）も reset も無い。job は「成功」している。
- 候補（板で切り分ける）:
  1. LOAD の image state（view の address・layout（twiddled か）・format の sample code・寸法）が、1 つ目の job の PBE が store した所と食い違う（別の address か layout を読む）。
  2. LOAD の program の座標（`SPECIAL_X/Y` を unnormalised の座標として渡す）が、範囲の外か浮動小数でない。
  3. 2 つの job の間の順序（1 つ目の store が終わる前に 2 つ目の sample が読む）。
- log を足した（`render-job.c`、`POWERVR_DIAGNOSTICS` の下）:
  - job ごとに、寸法・draw の数・colour の load_op と store_op・store の address・layout・pitch を出す。
  - LOAD の時は、view の address・layout・寸法・mapping と、image の 4 語・sampler の 2 語を出す。
- 板で流すもの: `-DPOWERVR_DIAGNOSTICS=1` の kernel で `vkmatrix two-passes` と `vkmatrix draws-32`。log の store の address と load の address・layout が合うかを見る。合っていれば 2 か 3。
- 確認: RISC-V の self-test の build で warning 0（DIAGNOSTICS の 0 と 1 の両方）。

## q1511（q1510 の続き、2026-10-21）: LOAD の切り分けの probe

- 板 97: two-passes・draws-32 とも、1 つ目の job は CLEAR + STORE で 0x8000047000 に twiddled で書き、2 つ目は同じ address・layout から LOAD している（image 語 `60008861 003f00fc 1c000001 00200001`、sampler `00000000 00022400`）。address と layout の不一致（候補 1）は外れた。
- 読みで見つけた点: LOAD の program（`powervr_render_load_usc`）は手で組んだ 6 group で、compiler の `shader_backend_space_hazards`（書いた register を次の group がすぐ読む時の間隔）を通らない。group の間に間隔が無い:
  - `MOVE_SPECIAL`（x・y）の直後に、その temp を `SAMPLE` が読む。
  - `PACK` の直後に、`MOVE` が出力へ送る。
  - P2 が見つけた hazard（直前の group が書いた値を読むと、半分の lane が古い値になる）に当たる可能性がある。
- `render-job.c` に `POWERVR_RENDER_LOAD_PROBE`（既定 0）を足し、`render-load-program.c` に `powervr_render_load_usc_probe` を足した:

  | 値 | LOAD の背景 | 結果の読み方 |
  | --- | --- | --- |
  | 0 | いつもの program | 今と同じ |
  | 1 | sample の座標を (0, 0) に固定（`MACHINE_IMMEDIATE` 0） | 全画素が 1 つ目の job の (0,0)（clear の色）になれば、sample と image の state は正しく、座標（`SPECIAL_X/Y`）が原因 |
  | 2 | いつもの program の各 group の後に wait の group | 通れば hazard が原因 |
  | 3 | sample をせず、背景を定数の magenta（clear の道） | magenta が出れば、2 つ目の job の store は動き、問題は LOAD の sample の側。0 のままなら、2 つ目の job の store か順序が原因（候補 3・4） |

- 板で流すもの: `-DPOWERVR_RENDER_LOAD_PROBE=1`（2・3 も）の kernel で `vkmatrix two-passes`。FAIL の行の pixel の値を見る。
- 確認:
  - RISC-V の self-test の build で warning 0（probe の 0〜3 の全部）。
  - p010 の host 試験は failed 0。
  - 板は未実施。

## q1511 の続き（2026-10-21）: probe の結果の読みと、次の切り分け

- 板（probe 3）の `0x3f000080` の読み: magenta の値を、私が float の bit（0x3f800000）で clear の shared word に入れたのが誤りで、clear の shared word は format の packed の語を受ける。B8G8R8A8 として読むと 0x3f800000 は B=0x00…ではなく、出た `0x3f000080` は「その raw の語が swizzle されて書かれた」値と合う。つまり **2 つ目の job の background の program は走り、store も動いている**（定数の背景は出ている）。問題は LOAD の sample が 0 を返すこと。
- LOAD の image 語と、通っている texture の image 語の比較（host で `powervr_view_words` を同じ address で計算した）:

  | view | 語 |
  | --- | --- |
  | attachment B8G8R8A8 64×64（LOAD） | 60008861 003f00fc 1c000001 00200001（板の log と同じ） |
  | texture R8G8B8A8 4×4（nearest・PASS） | 60000a61 0003000c 1c000001 00200001 |
  | texture B8G8R8A8 4×4 | 60008861 0003000c 1c000001 00200001 |
  | texture R8G8B8A8 64×64 | 60000a61 003f00fc 1c000001 00200001 |

  違いは、swizzle（B8G8R8A8 は 2,1,0,3、R8G8B8A8 は 0,1,2,3）と寸法だけ。texformat 12、twiddled、address の語は同じ形。sampler の語も transfer（板で通る）と同じ作り。
- 残る差は 2 つ:
  1. **B8G8R8A8 の sample**（vkmatrix の texture の case は全部 R8G8B8A8 だった）。
  2. **render pass が書いた image の sample**（PBE の書きと TPU の読みの間。SLC・順序・layout）。
- vkmatrix に case を 2 つ足した（host の lavapipe で 15/15 PASS、RISC-V の build OK）:
  - `sample-bgra`: B8G8R8A8 の 4×4 texture を nearest で sample。
  - `sample-rendered`: 16×16 B8G8R8A8 の image を 1 つ目の pass で clear し、barrier の後、同じ command buffer の 2 つ目の pass で sample。
- 板で: `vkmatrix sample-bgra` と `vkmatrix sample-rendered`。前者が落ちれば B8G8R8A8 の sample（format・swizzle）、後者だけが落ちれば PBE の書きと TPU の読みの間（同期・cache）が原因。
- probe 3 の値の form の誤りは、次の commit で直す（今は切り分けに使わない）。
