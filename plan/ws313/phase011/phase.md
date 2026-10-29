<!-- awesome-plan project=zedbsd record=ws313-p011 -->

# ws313-p011: Mali の render の node・scheduler・Vulkan の executor・CS の命令の builder

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 D）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md) §11・§13。compiler は配下 C（§12）。

## ユーザーの指示（原文、2026-10-29、Q1 経由）

「Maliのドライバは、このGPUでとりあえずデスクトップを動かせるように、シェーダーコンパイラ、コマンド投入、シェーダ実行などを、一通り実装して、UATとデバッグに備えてください。デスクトップが動かせる範囲の実装でよいですが、余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」
「実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

- 解釈: desktop（Keiland の compositor と libkeiland の描画）に要る範囲を先に。実機の試験は無いので「実装済み・実機未試験」で cleared にしない。判断が要る所は推しで進め、下に記録する。

## 実装（2026-10-29）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/mali/cs/cs-build.{h,c}` | CS の命令（NOP・MOVE48/32・WAIT・RUN_COMPUTE/IDVS/FRAGMENT・FINISH_TILING/FRAGMENT・ADD32/64・LOAD/STORE_MULTIPLE・BRANCH・SET_SB_ENTRY・SET_EXCEPTION_HANDLER・CALL・JUMP・REQ_RESOURCE・FLUSH_CACHE2・SYNC_ADD/SET/WAIT32/64・STORE_STATE・ERROR_BARRIER・HEAP_SET/OPERATION）の encode、writer、行の copy・fill の loop（front end の LOAD/STORE だけで動く。shader 不要） |
| `src/drivers/gpu/mali/mali-queue.c` | stream の一般化: open（CSG・CS の program と start）・stop（terminate）・push（ring の insert と doorbell）・wait（sync の値、CS の FAULT/FATAL の ack と記録）・report（1 行）。driver の stream（group 0）の self-test: NOP＋SYNC_ADD64 |
| `src/drivers/gpu/mali/render.{h,c}` | render の node（gpu1）: session ごとの AS の table、VA の free run、buffer object（参照の数）、resource・blob（VkDeviceMemory の blob を含む）、CPU の view（uncached RAM）、export・import・display への backing、stage の memory（submit ごとの CS と descriptor） |
| `src/drivers/gpu/mali/sched.c` | group slot 1・stream 0・doorbell 2・AS slot 2。submit のたびに session の table を slot 2 に bind（前の table は disable で cache を clean）、ring に FLUSH・CALL・WAIT・FLUSH・SYNC_ADD64・ERROR_BARRIER、sync の値を 5 s 待つ。FAULT・FATAL・timeout は 1 行＋group の terminate と再 start、session を lost。capset（4、powervr と同じ形）・frame・job の marker（同期なので commit で完了）・recovery（stop・fault・reset・isolate） |
| `src/drivers/gpu/mali/vk/vk.h`・`vk-object.c`・`vk-stream.c` | executor の session、object の登録、transport（reply の領域、trailer、nested stream） |
| `vk-device.c`・`vk-format.c` | instance、physical device（Mali-G310、Vulkan 1.1、limit）、memory（1 heap、device local と host visible+coherent）、queue family 1 つ、format の表（RGBA8・BGRA8・sRGB・R8・RG8・16F・32F・32UI 他）、device、queue と timeline |
| `vk-memory.c` | memory（新規・blob の import・export）、buffer、linear の image（行は 64 byte に揃える）、view、sampler の descriptor、fence、semaphore |
| `vk-layout.c` | shader module、set layout（uniform・storage buffer、combined image sampler、各 binding 1 個）、pipeline layout、cache、pool、set（alloc・free・update）、render pass（1 subpass、color だけ）、framebuffer |
| `vk-pipeline.c` | compute・graphics の pipeline: `mali_shader_compile` で各 stage、code を session の bo に（128 byte 境界）、vertex input・topology・cull・blend・静的な viewport/scissor |
| `vk-command.c` | command pool・buffer、begin/end/reset、vkCmd* の record（bind・viewport・scissor・sets・index・vertex・draw・draw indexed・dispatch・copy buffer/image/buffer↔image・blit・fill・clear color image・clear attachments・barrier・push constants・render pass） |
| `vk-desc.{h,c}` | Valhall の descriptor の pack（SPD、Local Storage、Resource、Buffer、Texture＋Generic Plane、Attribute、Blend と固定機能の式、RGB Render Target、Framebuffer Parameters、Tiler Heap、Tiler Context、Primitive flags） |
| `vk-build.h`・`vk-queue.c` | vkQueueSubmit: 全 command buffer を 1 本の stream に。REQ_RESOURCE・SET_SB_ENTRY、copy・fill・clear は CS の loop、dispatch は RUN_COMPUTE（SRT・FAU・SPD・TSD・workgroup の staging register）、FAU に push constants・buffer の address・range・texture の size、resource table（texture・sampler・attribute・vertex buffer） |
| `vk-draw.c` | render pass: tiler の heap（8 MiB の 1 chunk）・geometry buffer・sample の位置、tiler context、FBD と RT（clear 色）、draw ごとに vertex（position と varying）・fragment の SPD、blend、ZSD、RUN_IDVS の staging register、最後に FINISH_TILING・RUN_FRAGMENT |
| `mali-attach.c`・`mali-mmu.c`・`mali-private.h` | device の lock（watch の PING と submit を直列に）、queue の後に render の開始、AS の unmap・destroy、型 |
| `platform/arm64/mali.mk` | 上の file と C の compiler の file（va-compile・va-encode・va-schedule）、共通の spirv・optimize・vk-codec |
| `tests/mali-cs-host-test.c` | 命令の encode（v10.xml の field の位置から手で詰めた値）、ring の列（panthor の値と照合）、copy・fill・行の loop を小さな interpreter で走らせて memory を照合 |
| `tests/mali-vk-desc-host-test.c` | descriptor の pack と blend の式（replace・straight alpha・premultiplied、MIN の拒否） |

## 判断（推しで進めた所）

- **同期の submit**: 1 回の vkQueueSubmit は GPU の終わりを待ってから返る（5 s で timeout）。fence・job の marker・frame の callback はその場で完了できる。scheduler は group 1 つ・slot 1 つの直列で、session の切り替えは AS の table の差し替え（group は動いたまま、ring は空）。速さより確かさ。
- **AS の差し替え**: 動いている group の AS slot の TRANSTAB を、ring が空の間に disable（cache の clean）→ enable で替える。**未確認**: firmware がこれを許すか。だめなら group の terminate・config・start に替える（`mali_queue_stop`・`mali_queue_open` は既にある）。
- **転送は CS の loop**: copy・fill・clear・同じ大きさの blit を front end の LOAD/STORE_MULTIPLE の loop で行う（GPU の上で動き、shader が要らない）。拡大・縮小・形式の変換の blit は走らせず 1 行を出す。
- **render pass の LOAD**: tile は clear 色で始まり、前の内容を読み戻さない（pre-frame の shader が要る、未実装）。LOAD の pass は session ごとに 1 度「draws on a cleared tile」を出す。**desktop への影響**: compositor が damage の部分だけを LOAD で描くと、他の部分が消える。
- **tiler の heap**: 8 MiB の 1 chunk を pass ごとに使い直す。HEAP_SET・heap の context・TILER_OOM の処理は無い（溢れたら FAULT として出る）。
- **tile の大きさ**: 16×16（256 px）、colour buffer は target あたり 1 KiB。hierarchy mask は TILER_FEATURES の levels から Mesa の手順の前半で。
- **workgroup memory（shared）を使う compute** は走らせない（1 行）。
- **image は linear だけ**（AFBC・tiled 無し）、1 level・1 layer・1 sample。
- **float**: kernel は `-mgeneral-regs-only` なので、viewport の変換と clear 色の unorm8 は整数（16 bit の小数の固定小数点）で計算する。
- **display（gpu0）への出し方**: render の node は image の export の backing（連続の page）を出す。display の node（`display/display.c`）は今は import を拒否する（P7 の担当、下の「P7 への案」）。

## 夜の serial で見る行（`mali:` で始まる。gpu.md §9 の行の後）

1. `mali: queue: group 0 stream 0 started on an empty ring (… N registers)`。**N が 96 未満**なら executor は `the command stream has N registers, 96 needed` で submit を拒否する（render の node 自体は出る）。
2. `mali: queue self-test: NOP and SYNC_ADD64 ran (sync 0 -> 1, 40 bytes through the ring)`。失敗なら `mali: queue self-test failed (…): sync …, fault …, fatal …` と `mali: queue self-test: group 0 stream 0 insert … extract … active … at … wait … blocked … resource … fault … fatal … group state …`。extract が 0 のままなら doorbell か ring の address（AS 1）の問題、fatal の下の byte が exception type。
3. `mali: queue: group 1 stream 0 started on an empty ring (… ring at 100000, doorbell 2 …)`（render の stream）。
4. `mali: render node ready (stream on group 1, address space 2, ring at 100000)`。失敗なら `mali: render: …`・`mali: render node not registered (…)`。
5. desktop の起動の後: `mali: vk: command frame failed opcode=N error=E bytes=B`（executor が拒否した command。N は uapi/gpu-op.h の番号）、`mali: vk: stage S not compiled (E) at I: reason`（C の compiler の拒否）、`mali: vk: recorded command N is not run (E)`。
6. GPU の失敗: `mali: render: submission K failed (E): stream … bytes …, fault …, fatal …, F faults` と `mali: render: group 1 stream 0 insert … extract … at …`、続けて `mali: MMU fault …`（gpu.md §9）。fault の address と `at`（CS の命令の位置）で、どの命令・どの descriptor かを stage の中で切り分ける。
7. `mali: vk: a pass that loads its attachment draws on a cleared tile`・`a scaling or converting blit is not run`・`a blend the fixed function does not take …`・`a clear of attachments inside a pass is not run` は機能の不足の印（1 度だけ）。

## 確かめ

- build: `cd /home/awe/zedBSD-worktrees/p7-ws313b && timeout 1200 make -j8 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/pd build/pd/vmunix` → exit 0、warning 0（2026-10-29、`grep -c " warning:" build/pd/build.log` = 0、C の未 commit の compiler の版を含む worktree で）。
- host: `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-cs-host-test.c src/drivers/gpu/mali/cs/cs-build.c` → `all checks passed (61)`。`tests/mali-vk-desc-host-test.c src/drivers/gpu/mali/vk/vk-desc.c` → `all checks passed (47)`（出力は build/pd/mali-check/）。
- 未実施: 実機（全部）、QEMU（S7D・Mali の machine が無い）、libvulkan の regression（libvulkan は変えていない）、executor の wire の host 試験（command buffer から stream の組み立てを偽の memory で回す試験は書いていない）。

## 残り（夜の結果で直す順）

1. self-test と render の stream の起動（上の 1〜4）。
2. compute の dispatch（`vkdemo` の compute、C の compute shader）: RUN_COMPUTE の task increment（今は 1）、TSD、FAU の pointer の形。
3. 三角形: RUN_IDVS の staging register、tiler context・heap、FBD・RT、blend の memory format（BGRA を RGBA 順にしている、未確認）、ZSD。
4. compositor: LOAD の pass の pre-frame の shader（C に fragment program を頼む）、clear attachments、拡大の blit、AFBC 無しの性能。
5. 改善: 非同期の submit（ring に複数の job、IRQ で完了）、TILER_OOM、shared memory、mip・array・depth。

## P7 への案（変えていない）

- `display/display.c` の `import_image`: render の node の export（`DRV_GPU_BACKING_CONTIGUOUS`、page の表）を受け、OSD1 の flip（§5 の (a)）の対象にする。今は import を拒否するので、compositor は display の node の自分の resource に copy して present するしかない（CPU か、Vulkan の image を display の resource に写す道）。
- libvulkan: 変更なし（powervr と同じ capset 4・vendor の印 SDBZ・flags 7 を返す）。
