<!-- awesome-plan project=zedbsd record=ws269-p010q -->

# ws269-p010q: 実機の前の点検（cache・MMU・SLC・順・時間切れの後）

- ID: ws269-p010q（親 [ws269](../ws.md)）
- Status: in-progress（q1395-i01 で読みの点検と 1 つの直し。実機は未）
- Disposition: normal
- Queue: q1395 / q1395-i01（2026-10-20 P1）

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
