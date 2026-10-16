<!-- awesome-plan project=zedbsd record=ws039-p019 -->

# ws039-p019 desktop の描画の速さ（app の window を開く animation が 1 fps 未満）

Status: in-progress（2026-10-17 01:55 P3: 4 段目の実機（17 節）と 5 段目の vkload の実機（19 節）を実施し、結果は 20 節。boost の既定を 1 に（20 節の判定、P3 の branch で 0f6b22184・de5de6360 を取り込んだ上の変更）。判定と clearance は Q1）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1235 / q1235-i01（1 段目）・q1235-i02（2 段目）・q1235-i03（3 段目）・q1235-i04（4 段目、P7）・q1235-i05（5 段目、P7、2026-10-17 ユーザー「DPMは既定でオンにします」）（承認 2026-10-16 ユーザー「appの起動時のウィンドウが開くアニメーションが1fpsも出ていないですので、高速化は必要です。」、Q1 の割当）
実行者: P5（phase-runner、worktree `/home/awe/zedBSD-worktrees/p5`、branch `agent/p5-ws039`）。4 段目は P7（worktree `/home/awe/zedBSD-worktrees/p7`、branch `agent/p7-p019`）
依存: p008（executor）・p009（desktop）・p017（窓の 1 GiB）は main に入っている（main f391e6b3c を cebb5882e で取り込んだ）。

## 範囲

RX 5600 XT の desktop で、window の開閉の animation・App Home の animation の 1 frame の時間の内訳を測り（compositor の KWL の frame の記録、executor の run の時間、GPU の fence の待ち、CPU の窓を通る写し・staging、pipeline・shader の compile が毎 frame 起きていないか、BO の確保・解放が毎 frame か、cache の flush・invalidate の過剰）、遅い所から直す。比べの基準は i915（5330）の同じ animation。実機 10.0.10.3 は P3 が流す（P5 は起動しない）。

## 1. code から見た毎 frame の重い処理の候補（2026-10-16 P5、実機の測定の前の見立て）

前提: amdrdna の executor は node に worker が 1 本で、全部の open（compositor と各 app）の frame を node の lock の下で順に流し、各 run（間接 buffer）の GPU の完了を同期で待つ（design 9.4）。ある app の frame が重いと、その間 compositor の frame も止まる。memory type は DEVICE（窓の外の VRAM）と DEVICE|HOST_VISIBLE|COHERENT（窓の中の VRAM）の 2 つ。CPU が窓の VRAM を読み書きする map は、kernel（`gmc.c` の `DRV_PCI_MAP_NOCACHE`）でも user（`gpu.c` の `VM_DEVICE_MMIO`）でも UC。

| # | 候補 | 根拠（file） | 見込みの重さ |
| --- | --- | --- | --- |
| 1 | **app の frame の buffer → image の copy が CPU の道**: libkeiland の present（`userland/desktop/libkeiland/ui/present.c`）は staging buffer の行を 64 byte に揃え、damage の region ごとに offset = y×pitch + x×4 で `vkCmdCopyBufferToImage`。executor は始まりか行が 256 byte でない region を CPU で写した。swapchain の image は WSI が最初の memory type（DEVICE、窓の外）に置くので `execute_bounce_copy`: region ごとに (a) 窓の VRAM を確保（`amdrdna_backing_create` が UC の 8 byte の書き込みで 0 にする）と session の空間への map（page table を UC で書いて HDP の flush）、(b) staging（窓の VRAM）を **UC で読んで**（両方の address が 8 byte に揃わない時、例えば x が奇数なら 1 byte ずつ）bounce に UC で書く、(c) quad、(d) その場で run して待つ、(e) unmap。 | `vk/vk-execute.c`（`execute_buffer_image`・`execute_bounce_copy`・`execute_cpu_copy`）、`src/kern/kcrt.c`（`kern_memcpy` は両方揃う時だけ 8 byte） | 最大。UC の読みは 1 回 0.5〜1 µs 程度の見込み（未測定）なので、1280x800 の window の最初の全面の frame（4 MB）は 8 byte の読みで 0.25〜0.5 s、1 byte ずつなら秒単位。その間 node の lock を持つので compositor も止まる → 「app の起動時の animation が 1 fps 未満」に合う |
| 2 | Venus の命令の stream を窓の VRAM の blob から UC で読む | `vk-node.c`（`amdrdna_vk_blob_read`）、`vk/vk-stream.c`（`stream_blob`） | frame ごとの stream の大きさ次第（数十 KB で数 ms の見込み）。毎 frame・全 open |
| 3 | 間接 buffer・資源の表を窓の VRAM に UC の 4 byte の書き込みで書く（draw ごとに register と表を全部書く形） | `vk/vk-pm4.c`・`vk/vk-execute.c`・`vk/vk-state.c` | App Home（300 draw 以上）で 1 frame に数万〜十万語。書き込みは読みより速いが数 ms〜十数 ms の見込み |
| 4 | user の map も UC: app が staging に行を写す（書き込み）、libvulkan の reply を読む | `src/drivers/gpu/gpu.c`（`VM_DEVICE_MMIO`）、`node.c`（`node_resource_map`） | app の側の時間。WC にすれば速いが BUG-331（WC の別名の候補）と p018 が先 |
| 5 | pipeline の compile は作る時だけ（毎 frame ではない）。ただし app の起動の時の compile も node の lock の下で、compositor の frame を待たせる | `vk/vk-pipeline.c` | app の起動の時に 1 回。測定で確かめる |
| 6 | 窓の VRAM の確保は 0 を UC で書く（mappable な blob、executor の run・pipeline の code） | `space.c`（`amdrdna_backing_create`） | 作る時だけ。app の起動の時の staging（window の大きさ）で数十 ms の見込み |
| 7 | run の待ちは 2 ms の spin の後 1〜1.5 ms の sleep（`kern_usleep_range` は busy の遅延） | `cp.c`（`amdrdna_ring_wait`） | run 1 つに最大 1.5 ms の遅れ。1 frame に run 1 つなら小さい |

cache の flush・invalidate: 間接 buffer の始めに全部の cache の acquire、pass の終わりと transfer の前後に barrier（RELEASE_MEM の GL2 の writeback と invalidate、待ち）。1 frame に transfer が多いと barrier が増えるが、#1 に比べて小さい見込み（測定で確かめる）。

## 2. 直した物（2026-10-16 P5）

1. **#1: buffer ↔ image の copy で、始まりか行が 256 byte でない region を command processor の move（DMA_DATA、L2 を通す、driver の `amdrdna_pm4_move_cached`）に**: 行ごとに 1 つの move（image の行の全部を buffer と同じ pitch で覆う upload は 1 つの move）。間接 buffer の順に並ぶので、run を途中で流さず、窓の VRAM の確保も CPU の読みも無い。transfer の前後の既存の barrier が draw との順を守る（前: CB の flush・EOP の待ち、move は engine が終わりを待つ bit 付き、後: K$・V$・GL1 の invalidate）。CPU の copy と bounce の copy は消した。256 byte に揃った region は前と同じ quad の draw。変えた file: `vk/vk-execute.c`（`execute_buffer_image` の分岐、`execute_move_rows`・`execute_move`）、`vk/vk-pm4.c`（`amdrdna_vk_pm4_move`: driver の packet を脇で作って写す。UC の buffer を読み戻さない）、`vk/amdrdna-vk-gfx10.h`（`AMDRDNA_VK_MOVE_MAX` = 0x03ffffe0、出典 Mesa 25.0.7 `radv_cp_dma.c`）、`vk/amdrdna-vk.h`（`amdrdna_vk_gpu_copied` の説明）。GFX10 の CP DMA は揃わない byte の address でよい（Mesa の realign は Carrizo 以前だけ）。node の `copies`・`copy_bytes` は move した copy の数になった。
2. **測定の道具**: boot の語 `amdrdna.vk.timing=N`（ms、既定 0 = 無し）で、executor の仕事を時間で数え、N ms ごとに（その後の最初の frame で）mark を出す（`vk-node.c`、`amdrdna-node.h` に `vk_timing` の 1 欄）:
   - `amdrdna: vk timing <ms>ms: frames <数> <合計 ms> max <us> | gpu runs <数> <合計 ms> max <us> | moves <数> <KiB>`
   - `amdrdna: vk timing: streams <数> <KiB> <ms> max <us> | memory <数> <KiB> <ms> max <us>`
   - `amdrdna: vk timing: op <GPU_OP の番号>: <数> <ms> max <us>`（時間の長い順に 6 つ。18 = QUEUE_SUBMIT、21 = ALLOCATE_MEMORY、65 = CREATE_GRAPHICS_PIPELINES、255 は 255 以上の全部）
   frames は frame（command の束）の CPU の時間の全部（node の lock の中）、gpu runs は run の積みから fence までの待ち、streams は窓からの stream の読み、memory は executor の VRAM の確保（窓の外の VkDeviceMemory と窓の中の run）、moves は move した copy（kernel A では CPU の copy）。

commit: `ad83acfa9`（測定の道具）、その次の WIP commit（move・試験・この記録）。

## 3. 試験（host）と build（2026-10-16 P5）

- `plan/ws039/tests/amdrdna-vk-host-test.sh`: PASS（177 checks、0 failures）。偽の GPU の decoder に move（op 0x50: 6 語、control 0xe0300000、偽の memory の中で写す）を足し、submit の場面を強めた: upload の record に「whole rows」の region（行 60・61 を buffer の 0x2004 から 1 つの move）、download の record に x=1 の 3x2 の region（20 byte の行、行の間の 8 byte を書かないこと）。確かめ: run 1 つ（前は CPU の copy のため 2 つ）、move 5 つ・560 byte、3 つの region の中身、barrier 12 個。わざとの誤り 2 つで FAIL になることを確かめた（buffer の行の進みを幅に、whole rows の分岐を無効に）。
- `AMDRDNA_LINUX=<P3 の temp/linux> plan/ws039/tests/amdrdna-host-test.sh`: PASS（PASS の行 390、audit-symbols を含む。最初 `perf`・`rank` が引用元の識別子と一致して FAIL → `timing`・`place` に改名）。
- `plan/ws039/tests/audit-vk.sh`: PASS（24 file）。出典の表に `radv_cp_dma.c` の行を足した（`plan/ws039/amdrdna-compiler-license-audit.md`）。
- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p5/usb build/p5/usb/vmunix`: rc 0、warning 0、amd64 vmunix check PASS。`git diff --check` 空。
- 未実施: 実機（P3 に依頼、4 節）。QEMU は amdrdna が無いので対象外。

## 4. 実機の測定（P3 への依頼、未実施）

kernel A = `ad83acfa9`（測定の道具だけ、copy は前の CPU の道）、kernel B = move の commit（直した物）。どちらも boot の語に `amdrdna.vk.timing=5000` を足す。同じ手順を A と B で:

1. 起動して desktop、10 s 何もしない（静止の frame の基準）。
2. App Home の開閉を 5 回（aat で Super、3 s おき）。
3. App Home から app を 3 つ（Terminal など、p018 の click の手順）起動して開いたまま、それぞれ 5 s 待つ。閉じる animation も 1 回。
4. 取る物: `dmesg` の `vk timing` の行、compositor の session.log の `KWL PERF`（compose・rec・main・SLOW_PASS）の行、`amdrdnactl status`、目視の感想（滑らかか）。

比べる物: A と B の frames の合計・max、op 18（QUEUE_SUBMIT）の max、moves の数・KiB、gpu runs の数・待ち、streams の ms、memory の ms、`KWL PERF compose frame_ms`・`rec submit/present`。基準: i915（5330）で同じ 2・3 の手順の `KWL PERF` の行（timing の行は i915 には無い）。

## 4b. 1 段目の実機の A/B（P3、2026-10-16 20:42〜20:50）

kernel A（CPU の copy）と B（DMA_DATA）、`amdrdna.vk.timing=5000`、4 節の手順。生の file は P3 の worktree の `build/tmp/p5-ab/{A,B}/`。
- A: frames 2545 計 7464 ms max 787.8 | op18 311 回 計 7188 ms max 787.8 | gpu runs 333 待ち 2132 ms max 222.9 | memory 107 回 819 ms max 79.8。SLOW_PASS 5 回
- B: frames 2484 計 2740 ms max 41.5 | op18 295 回 計 2534 ms max 41.5 | gpu runs 303 待ち 1852 ms max 19.8 | memory 100 回 378 ms max 33.2。SLOW_PASS 1 回（153 ms）
- B でも compose の frame_ms が period により 100〜1000 ms（Q1 の要約）。

## 6. 2 段目の切り分け（2026-10-16 P5、B の session.log と dmesg）

- **compose の 100〜1000 ms は timing の印そのものだった**: A・B とも、`vk timing` の印を含む KWL PERF の 5 s の period には、`rec begin`（vkBeginCommandBuffer、kernel への 1 往復）が 1.9〜2.2 s かかる frame が 1 つずつある（`walk replay max` が 2015〜2231 ms、B は 1891〜2075 ms）。印の無い period（B の 425・969・995 行）は frame_ms 18〜28 ms、replay max 1 ms 前後。印の period の長さも 5000 ms の指定に対して 6245〜6606 ms（印を出す時間が次の period に入る）。
- 仕組み: 印は `amdrdna_mark` → `kern_logf` → kernel の text console（pcat、`src/drivers/platform/pcat/graphics/text.c`）。amdrdna の display の lease（`present.c` の `present_claim`）は `kern_text_suspend` を呼ばない（i915 の `bootfb.c`・bcm2711 の `display-device.c` は呼ぶ）ので、desktop の間も console が GOP の framebuffer（VRAM、`backend.c` で UC の map）に描き続ける。行が最下行で scroll する度に 1920x1080 の全面を描き直す（UC の 4 byte の書き込み 200 万回）。印 8 行を node の lock の下（`vk_node_execute` の中）で出すので、その間 compositor の次の Vulkan の呼び出しが待つ。boot の行に `kmsg=quiet` が無い試験の image（`plan/ws039/tests/config.mk`）で起きる。印に限らず desktop の間の全ての kernel の行（例: session の始まりの input の行）が 1 行ごとに同じ時間を払う。
- 印の分を除いた B の 1 frame: compositor の frame は submit 13〜19 ms・present 4 ms・acquire 5〜10 ms で 18〜28 ms（40〜55 fps 相当）。executor の QUEUE_SUBMIT 1 回 = 8.6 ms（2534/295）、うち GPU の run の待ち 6.1 ms（1852/303）、CPU 2.3 ms。待ちの仕組み（`cp.c` の `amdrdna_ring_wait`: 1〜2 ms の spin の後 1〜1.5 ms の sleep）の遅れは高々 1.5 ms なので、run の 4.5 ms 以上は GPU の実行（または submit から始まりまで）と見られるが未確認 → 7 節 2 の測定を足した。
- 候補 2（stream の UC の読み）: B の streams は period に 0〜1 回、71〜80 KiB で 12〜16 ms（1 byte あたり約 0.2 µs）。回数が少ないので今は小さい。候補 6（確保の 0 埋め）: memory は app の起動の period に 6〜8 回・計 38〜44 ms・max 32〜33 ms。候補 3（間接 buffer の UC の書き込み）: QUEUE_SUBMIT の CPU の 2.3 ms に入る。どれも 1 frame の 18〜28 ms に比べて小さく、次に大きいのは GPU の run の 6 ms。2・3・6 は今回は直さない（根拠の測定が揃ってから）。4（user の map の WC）は BUG-331・p018 に関わるので案だけ: app の staging の書き込みと libvulkan の reply の読みを WC にする（p018 の後）。

## 7. 2 段目の修正（2026-10-16 P5）

1. **amdrdna の desktop の間は kernel の text console を描かない**（`present.c`、`amdrdna-node.h` に `text_held`）: lease の最初の frame の flip が始まったら `kern_text_suspend()`（i915・bcm2711 と同じ口）、lease の終わり（release・session の close・reboot の stop、`present_end_lease`）で `kern_text_resume()`（console が見えなかった間の行も含めて描き直す）。suspend の間 pcat の text は cell だけを更新し（`putc_locked` は `text_ready` が 0 なら描かない、scroll も描かない）、serial の mirror と dmesg の ring は前のまま。lease の途中で表示中の blob が消えて console の surface に戻った時（`amdrdna_present_resource_gone`）は resume しない（suspend 中の古い絵のまま、次の frame で lease の絵に戻る）。fatal の時は `kern_text_reveal_fatal` が `text_ready` に関わらず描く。
2. **timing の道具に GPU 自身の時間**（`space.c`・`cp.c`・`command-format.c`・`amdrdna-hw.h`・`amdrdna-private.h`・`vk-node.c`）: timing が on の時（`amdrdna_spaces_stamp`）だけ、graphics の ring の run の前後に GPU の timestamp の counter を write-back の slot（ring ごとの 64 byte の語 8・12）に書く（前: COPY_DATA の counter→memory、後: event fence の data を counter に）。vk-node は run の後に差（`amdrdna_space_run_ticks`）を足し、counter の rate は period の最初と最後の run の counter と CPU の時刻から測る（固定の 100 MHz を仮定しない）。印の新しい行: `amdrdna: vk timing: gpu busy <runs> runs <ms> max <us> (<ticks> ticks, counter <kHz>) | run buffers <KiB>`（run buffers は間接 buffer の合計）。timing が off なら ring の中身は前と同じ。出典: Linux v6.19 `nvd.h`（PACKET3_COPY_DATA の欄の位置）、Mesa 25.0.7 `src/amd/common/sid.h`（COPY_DATA_TIMESTAMP・COPY_DATA_DST_MEM・COUNT_SEL・WR_CONFIRM・EOP_DATA_SEL_TIMESTAMP）、手順は `radv_query.c`（radv_write_timestamp）。後の stamp の event fence は cache の書き戻しを 1 回足す（timing の時だけの小さな乱れ）。
- 他の担当との境: P3 の branch（cfd5d7801）も `space.c`・`vk-node.c`・`cp.c`・`amdrdna-private.h` を触るが、こちらは別の関数（`space_commands` の終わり・`amdrdna_space_lost` の後・`amdrdna_ring_fence_address` の後・timing の関数）に足した。`vk-execute.c`・`vk-state.c` は触っていない。

## 8. 2 段目の試験（host）と build（2026-10-16 P5）

- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p5/usb build/p5/usb/vmunix`: rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`git diff --check` 空。
- `AMDRDNA_LINUX=<P3 の temp/linux> sh plan/ws039/tests/amdrdna-host-test.sh`: PASS（PASS の行 390、audit-symbols 48 file を含む）。
- `sh plan/ws039/tests/amdrdna-vk-host-test.sh`: PASS（177 checks、0 failures）。`sh plan/ws039/tests/audit-vk.sh`: PASS（24 file）。
- packet の形: `command-format.c` を host で小さな C から呼び、COPY_DATA は `c0044000 00110509 0 0 <lo> <hi>`（Linux の `PACKET3(PACKET3_COPY_DATA, 4)` と Mesa の radv_write_timestamp の top of pipe と同じ並び）、stamp の fence は既存の fence と第 3 語だけ違う（`60000000`、interrupt 無し）ことを見た（試験は残さない、`build/tmp/p19b-stamp/`）。
- 未実施: 実機（9 節）。present.c の text の suspend・resume は host 試験の対象外（kernel の build だけ）。

## 9. 実機の測定（2 段目、P3 への依頼、未実施）

kernel A' = main `9f5ffb9a1`（1 段目の DMA_DATA まで）、kernel B' = この 2 段目の commit `980c5d0f1`（base 9f5ffb9a1）。4 節と同じ自動の手順（起動 → desktop で 10 s 何もしない → App Home の開閉 5 回 → app を 3 つ起動して各 5 s → 閉じる 1 回）を 3 回:
1. B'、`amdrdna.vk.timing=5000`: 期待 = 印の period の長さが 5000〜5100 ms 前後（前は 6245〜6606）、KWL PERF の `walk replay max` に 2 s 前後の値が無い、compose の frame_ms が印の period でも 18〜30 ms。新しい `gpu busy` の行の counter の kHz（100000 前後の見込み）と busy の ms を、`gpu runs` の待ちの ms と比べる（busy ≒ 待ちなら GPU の実行が重い、busy が小さければ submit・待ちの側）。
2. A'、`amdrdna.vk.timing` 無し、3. B'、`amdrdna.vk.timing` 無し: ユーザーが見た「印の無い時」の比べ。session.log の KWL PERF（compose frame_ms・rec begin・SLOW_PASS の数と us）を A' と B' で比べる。特に login の直後・session の始まり（input の行など kernel の行が出る所）の period。dmesg の `display lease 1 claimed` の後の行の数も数える（desktop の間の kernel の行の数）。
取る物（各回）: `dmesg`、compositor の session.log、`amdrdnactl status`、目視の感想。lease の終わりの console の戻り（reboot の時に shutdown の行が画面に出る）も見られれば。

## 11. 2 段目の実機の結果（P3、2026-10-16 21:12〜21:23、生の file は P3 の worktree の `build/tmp/p5-ab2/{r1,r2,r3}/`）

- r1（B'、timing=5000）: 印の間隔 7552・5322・12921・6200・6270・6275・6269・6375・7767・7622・7625 ms。walk replay max 3.65 ms（2 s の止まりは消えた）。compose frame_ms は全 period 15.4〜25.7 ms。executor の frames 3287・計 4101 ms・max 41.9、op18 497 回 max 41.9、gpu runs 505・待ち 2951 ms・max 20.0。gpu busy は待ちの約 80%（run の max は待ち 15.5〜20.0 ms、busy 15.5〜19.7 ms）。counter 100007 kHz（runs 1 の period だけ 0）。SLOW_PASS 1 回（login 直後 0.5 s）。
- r2（A'、timing 無し）・r3（B'、timing 無し）: compose 13〜25 ms で差は小さい。SLOW_PASS は login 直後の 0.5 s が両方に 1 回。
- r3: 3 つ目の app（Terminal）の起動の直後に compositor が `VULKAN kernel error errno=17 device lost` → `KWL VULKAN_ERROR operation=submit result=-4` で落ち、session が作り直された（lease 1 released after 220 frames → lease 2）。

## 12. 3 段目の切り分け（2026-10-16 P5）

- **(a) r3 の EBUSY の device lost は 2 段目の変更に依らない（BUG-332、Q1 が記録、未割当）**: dmesg に executor の失敗の印（`vk: a frame failed`）が無いので、executor の frame は失敗していない。libvulkan で `VULKAN kernel error errno=…` を出すのは同期の ioctl だけ（`context.c` の `vulkan_kernel_error`）。そのうち EBUSY を返すのは `gpu.c` の `gpu_destroy_ioctl` の `mapping_count != 0`（資源がまだ map されている）。compositor の `kwl_spawn`（`userland/desktop/wayland/home.c`）は `fork()` で app を起動し、子は exec までの間、親の GPU の map（`vmspace.c` 4147 行: fork した device の region は extent を共有して保つ）を持つ。その間に親が map した VkDeviceMemory を解放すると destroy が EBUSY → libvulkan が context を lost にし、次の vkQueueSubmit が DEVICE_LOST。r3 は `HOME launch name=Terminal` の 5 ms 後の home-close の直後に落ちた。backend に依らない競合で、2 段目の text の suspend（cell の更新を止めるだけ、spinlock の下で数行）・timing（off）とは関係が無い。直しの候補: compositor の起動を `posix_spawn`（libc に `__vfork_spawn` がある）か vfork にする、または kernel の destroy を map の残る間は handle だけ外して後で解放する形（Linux の GEM と同じ）。
- **(b) GPU の run が 1 回 15 ms 前後かかるのは GFXCLK が boot の 300 MHz のままだからの見込み（未確認、14 節で確かめる）**: A1v の `sclk=300MHz mclk=875MHz`（VBIOS の firmwareinfo の boot の clock）、A4 の features は `0x0000000380000000`（FW_CTF・FAN_CONTROL・THERMAL の 3 bit だけ、design 4.4「DPM は有効にしない（boot の clock のまま）」）。RX 5600 XT の GFXCLK の上限は 1.6 GHz 前後なので、shader・CP・barrier の待ちのどれも 5 倍以上遅い。memory の clock（875 MHz）は上限のまま。r1 の数: 1 period（約 6.2 s）に gpu runs 46・busy 231 ms（1 run 平均 5 ms、max 15.5 ms）、run buffers 969 KiB（1 run 平均 21 KiB = 約 5,300 語）。compositor が frame を作るのは App Home の開閉の間だけ（poll 94%）で、executor の時間は period の 6% なので、GPU の時間は「1 frame の長さ」には効くが全体の詰まりではない。i915（5330）の同じ 1 frame の GPU の時間は zedBSD の道具に数が無い（timing の印は amdrdna だけ、KWL PERF の compose frame_ms が比べられる数）。register の全書き・barrier・shader の質は、clock を上げた後の busy で残りを見て切り分ける（300 MHz のままでは区別できない）。
- **(c) 印の間隔が 5000 ms より長いのは印の出し方による（lock の待ちではない）**: 印は「period が過ぎた後の最初の frame」（`vk_node_execute` の後の `vk_node_report`）で出る。compositor は damage のある時だけ frame を作るので、idle の間は次の frame まで印が遅れる。r1 の 6200〜6375 ms は App Home の開閉の間隔（約 3.1 s、`FIRST_FRAME home-open/close` の request_ms が 57839・60937・64040 …）の 2 回分、7.5〜7.7 s は app の起動の手順の間隔、12.9 s は idle の 10 s。時刻は TSC（`amdrdna_display_now_us`）で、GPU の counter の rate（100007 kHz）と一致するので測り方は正しい。印の行の長さ（ms）が period の実際の長さなので、率は行の ms で割れば出る。直さない。

## 13. 3 段目の修正（2026-10-16 P5）

1. **SMU の metrics の clock を読む**（`thermal-format.c`・`amdrdna-hw.h`・`amdrdna-private.h`・`smu.c`）: 監視の thread が 1 秒ごとに読む metrics の表（既に温度と fan を読んでいる）から、今の GFXCLK・SOCCLK・UCLK（`CurrClock` の 0・1・2）、GFXCLK の平均（offset 18）、graphics の平均の activity（offset 24、%）も読む。新旧の版で同じ位置（Value referenced: Linux v6.19 `smu11_driver_if_navi10.h` の `SmuMetrics_t`・`SmuMetrics_legacy_t`）。A4 の後に `A4 clocks gfx=…MHz average=…MHz soc=…MHz mem=…MHz busy=…% levels=off|asked` を 1 行、timing の印に `vk timing: clocks gfx …MHz average …MHz soc …MHz mem …MHz | gfx busy …%`（`amdrdna_power_clocks`、監視の最後の読み、1 s 以内）。firmware が読みを返さない（Tier S）なら `clocks not read`。
2. **既定 off の boot の語 `amdrdna.smu.gfxclk=1`**（`smu.c`・`amdrdna-hw.h`、Q1 の了解 2026-10-16、既定で on にするかは実機の温度・fan を見てユーザーに諮る）: A4 の Tier F で、permit の low の語に DPM_PREFETCHER（bit 0）・DPM_GFXCLK（1）と、clock を上げた時の守り PPT（23）・TDC（24）・GFX_EDC（25）・VR0HOT（29）を足す（`AMDRDNA_SMU_CLOCK_FEATURES_LOW` = 0x23800003。Linux の `navi10_get_allowed_feature_mask` が常に permit する物と GFXCLK。この board の pptable の FeaturesToRun 0x…a3ddaffb に全部入っている）。UCLK の DPM は入れない（GOP の watermark が p-state の切り替えを覆わず underflow しうる、design 8 節の危険）。EnableAll の後に `A4 features: graphics clock levels run|do not run (permitted 0x23800003)`。EnableAll が拒まれた時の第 2 の試し（by word）は今までの 3 bit だけを頼む（DPM の permit が害をなした時は 3 bit の Tier F に戻る）。停止の DisableAll は前と同じ。
- 他の担当との境: `smu.c`・`thermal-format.c` は p012（P3）の file だが、P3 は p014（display）で触っていない（Q1 に事前に知らせた）。`vk-node.c` は timing の関数（`vk_node_report` と新しい `vk_node_report_clocks`）だけ。

### 3 段目の試験（host）と build（2026-10-16 P5）

- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p5/usb build/p5/usb/vmunix`: rc 0、`warning:`・`error:` 0 行、kernel include check PASS、amd64 vmunix check PASS。`git diff --check` 空。
- `AMDRDNA_LINUX=<P3 の temp/linux> sh plan/ws039/tests/amdrdna-host-test.sh`: PASS（PASS の行 394、前の 390 から 4 増: `readings.clocks`・`readings.activity`・`linux.clocks`・`linux.clocks-legacy`。Linux の struct で書いた新旧の metrics の clock の欄を driver が同じ値に読む）。最初 audit-symbols が `soc_clock`・`clocks`・`rates`（引用元の識別子と一致）と hex の define の出典の欠けで FAIL → `system_clock`・`speeds_now` に改名、`AMDRDNA_SMU_FEATURE_GFX_LEVELS` に Value referenced を足した。
- `sh plan/ws039/tests/amdrdna-vk-host-test.sh`: PASS（177 checks、0 failures）。`sh plan/ws039/tests/audit-vk.sh`: PASS（24 file）。
- 未実施: 実機（14 節）。DPM の permit は host 試験の対象外（SMU の応答は実機だけ）。

## 14. 実機の依頼（3 段目、P3 へ、未実施）

kernel C = この 3 段目の commit（2 段目の text の suspend と timing を含む）。4 節と同じ自動の手順（起動 → desktop で 10 s → App Home の開閉 5 回 → app を 3 つ起動して各 5 s → 閉じる 1 回）を 2 回:
1. C、boot の行に `amdrdna.vk.timing=5000`（DPM 無し、今の既定）: dmesg の `A4 clocks …` と各 period の `vk timing: clocks …` の gfx の MHz（期待: 300 前後、average も 300 前後）、gfx busy %、`gpu busy` の ms・max。
2. C、boot の行に `amdrdna.vk.timing=5000 amdrdna.smu.gfxclk=1`: dmesg の `A4 features all on: 0x…`（low に bit 1 が立つか）と `A4 features: graphics clock levels run`、`A4 clocks`、各 period の clocks の gfx の MHz（App Home の開閉の間に上がるか、idle で下がるか）、`gpu busy` の max（期待: 15 ms → 3〜5 ms 前後）と `gpu runs` の max、KWL PERF の compose frame_ms・rec submit、SLOW_PASS。**温度と fan**: `amdrdnactl status` の power の行（temp・fan）を 1 の時と比べる、`thermal reading 10` の行、目視・音。underflow（A6 の行と `amdrdnactl status` の display の underflow）が 0 のままか。
3. 落ちた時: `A4 features: all on gave …` → `by word` で 3 bit の Tier F に戻れば system は動く（`A4 ok … mode=F`）。固まったら電源。`amdrdna.smu.gfxclk=1` を外せば 1 と同じ。
取る物（各回）: dmesg、compositor の session.log、`amdrdnactl status`（desktop の間に 2 回）、目視（App Home・window の開く animation が滑らかか、2 で 1 より速く見えるか）。r3 の device lost（BUG-332）は fork の競合なので、起きても 3 段目の判定には使わない（起きた時刻と `HOME launch` の行を記録）。比べ（任意）: 5330（i915）で同じ手順の KWL PERF の compose frame_ms。

## 15. 4 段目の切り分け（2026-10-17 P7、3 段目の実機の生の file は P3 の worktree の `build/tmp/p5-c/{r1,r2,r3}/`）

- 3 回の起動: r1 = DPM 無し（kernel の行に gfxclk の語が無く、`levels=off`）、r2 = `amdrdna.smu.gfxclk=1`、r3 = 既定（`levels=asked`）。DPM が on の時は `A4 features all on: 0x00000003a3800003`（low に bit 0・1・23・24・25・29・31）。
- **800 MHz は firmware 自身が選んだ値で、driver の message が足りないのではない見込み**。GPU の run の max は DPM 無しで 15.5 ms、DPM on で 6.2 ms（比 2.5 ≒ 800/300）。gpu busy の合計は period（5〜6 s）の 2〜4%（DPM on で 125〜209 ms）。navi10 の PMFW は workload ごとの activity monitor（Linux の `DpmActivityMonitorCoeffInt_t`。Gfx_MinActiveFreq・Gfx_BoosterFreq・PD_Data_limit_c）で GFXCLK を決める。目標の busy（PD の limit）に届かない限り、GFX が動いている間の下限（booster）に留まる。zedBSD の executor は run ごとに GPU の完了を同期で待つので、desktop の負荷は短い burst になり、平均の busy は低い。800 MHz は Navi10 の GFXCLK の下限としてよく見える値（OD の SCLK の下限）で、今回の値と合う。ただし firmware の係数そのもの（PMFW の内部）は見ていないので、ここは未確認。
- **Linux の初期化と比べて足りない物**（v6.19、`amdgpu_smu.c` の `smu_smc_hw_setup`・`smu_late_init`、`navi10_ppt.c`、`smu_v11_0.c`）: (a) levels の読み（`GetDpmFreqByIndex`、`smu_set_default_dpm_table`）、(b) 初期の workload（`smu_handle_task(COMPLETE_INIT)` → `smu_bump_power_profile_mode` → `SetWorkloadMask(1 << WORKLOAD_PPLIB_DEFAULT_BIT)`）、(c) averaging の設定の表（`smu_apply_default_config_table_settings`: `DriverSmuConfig_t` の 6 つの tau を 10 ms にして `TransferTableDram2Smu(TABLE_DRIVER_SMU_CONFIG=7)`）、(d) **graphics ring を使う間は 3D の workload profile**（`amdgpu_gfx_profile_ring_begin_use`: FULLSCREEN3D を足し、fence が 1 s 無ければ外す、`GFX_PROFILE_IDLE_TIMEOUT`）、(e) `NotifyPowerSource`（`smu_set_ac_dc`。desktop では AC の判定が環境に依存するので今回は送らない）、(f) PCIe の parameter（`OverridePcieParameters`、DPM_LINK を permit していないので不要）。soft min・soft max は、Linux も初期化では送らない（level が AUTO のまま変わらないため）。
- **gfx busy が常に 0% の読み**: offset は正しい（`SmuMetrics_t`・`SmuMetrics_legacy_t` の `AverageGfxActivity` は 24。host 試験の `linux.clocks` で Linux の struct と一致）。印は「period の後の最初の frame」で出て、その直前の 1 s の読みを使う。compositor は damage がある時だけ frame を作るので、印の直前の 1 s は idle で、0% は idle の読みの見込みが高い（clock の 800 も同じく idle の読み）。soc 0・mem 0 MHz は、DPM を有効にしていない clock の `CurrClock` を firmware が 0 で返すため（DPM 無しの r1 では gfx も 0）。直しは読みの位置ではなく、(c) の averaging の設定（Linux と同じ 10 ms）と、印に period の中の最高値を出すこと（16 節）。burst の中で 0% のままなら、firmware が activity を測る前提（未有効の feature など）が欠けている。それは 17 節の実機で分かる。

## 16. 4 段目の修正（2026-10-17 P7、commit 0f6b22184、base main 8146bfc19）

1. **GFXCLK の初期化を Linux と同じに**（`smu.c` の `power_clock_setup`・`power_clock_levels`）: EnableAll の後に DPM_GFXCLK（low の bit 1）が走っていれば、levels の数と最低・最高（`GetDpmFreqByIndex` 0x20、index 0xff で数、bit 31 は frequency ではない）を読む。続いて workload を既定に（`SetWorkloadMask` 0x24 に 0x1）、averaging の設定の表を渡す（table 7、44 byte、6 つの tau を 10 ms、`thermal-format.c` の `amdrdna_power_settings_build`）、GFXCLK の ceiling を外す（`SetSoftMaxByFreq` 0x1b に `0<<16 | 0xffff`、Linux の AUTO の形）。印: `A4 graphics clock: <数> levels <最低>-<最高>MHz (<err>), workload <err>, averages <err>, ceiling <err>, boost <mode>`。ここでの拒否は features を戻さない（firmware が自分で clock を動かす状態が残る）。`power_table_send` は table の id と大きさを引数に取る形にした。
2. **仕事の間だけ GFXCLK を上げる boost**（`amdrdna.smu.boost=`、0 = 無し、1 = Linux と同じ 3D の workload profile だけ、**2 = profile と floor（既定）**）: `amdrdna_space_run` の始めに `amdrdna_power_busy`（thermal の spinlock で時刻を記録し、上がっていなければ watch の thread を起こす）を呼ぶ。SMU の message は watch の thread が送るので、run の道では待たない。上げる時: `SetWorkloadMask(0x1|0x2)`、mode 2 なら `SetSoftMinByFreq(0<<16 | 最高の level)`。最後の run から 1 s 後に戻す: workload 0x1、floor 0（Linux の AUTO の形）。上げた 100 ms 後に metrics を 1 回読み、上がった clock が読みに出るようにした。拒否されたら boost をやめ、既定に戻して `smu: the graphics clock's boost refused …` を印に出す。上限は firmware の levels の最高（Linux の `AMD_DPM_FORCED_LEVEL_HIGH` と同じ値）。PPT・TDC・EDC・VR0HOT・THERMAL・FW_CTF の feature は on のままで、firmware の電力・温度の制限は変えていない。
3. **timing の印**（`vk-node.c`）: `vk timing: clocks gfx <今>MHz top <period の最高>MHz average … soc … mem … | gfx busy <今>% top <period の最高>% | boosts <累計>[ raised]`。最高値は watch の読み（1 s ごと、boost の 100 ms 後にも）の最大で、印の度に 0 に戻る（`amdrdna_power_clocks` は `struct amdrdna_power_clock_report` を返す）。
- 出典は block ごとに `Values referenced:`・`Procedure referenced:`（Linux v6.19 の `smu_v11_0_ppsmc.h`・`smu11_driver_if_navi10.h`・`smu_v11_0.c`・`navi10_ppt.c`・`amdgpu_smu.c`・`amdgpu_gfx.c/.h`）。Linux の名前は使っていない（audit-symbols PASS。最初に局所変数の `boost` が引用元の識別子と一致して FAIL → `raise_mode` に改名）。

### 4 段目の試験（host）と build（2026-10-17 P7）

- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p7/usb build/p7/usb/vmunix`: rc 0、`warning:`・`error:` 0 行、kernel include check PASS、amd64 vmunix check PASS。`git diff --check` 空。
- `AMDRDNA_LINUX=<P3 の temp/linux> AMDRDNA_FIRMWARE=<main の build/sources/firmware/amd-navi/dc85cced…> sh plan/ws039/tests/amdrdna-host-test.sh <P3 の temp/dump/cold>`: rc 0、PASS の行 450（audit-symbols 53 file を含む）。足した試験: `settings.averages`・`settings.rest`（合成）、`linux.settings-file`・`linux.settings-size`・`linux.settings`（`amdrdna-power-linux.c` が Linux の `DriverSmuConfig_t` に `navi10_get_default_config_table_settings` の値を入れて書いた 44 byte と、driver の表が byte 単位で一致）。
- `sh plan/ws039/tests/amdrdna-vk-host-test.sh`: PASS（196 checks、0 failures）。`sh plan/ws039/tests/audit-vk.sh`: PASS（24 file）。
- 未実施: 実機（17 節）。SMU の応答（levels の値、workload・soft min の受理、activity の値）は host 試験の対象外。boost の watch の thread の動き（起こされ方、1 s 後の戻し）も実機だけで分かる。

## 17. 実機の依頼（4 段目、P3 へ、未実施）

kernel D = commit 0f6b22184（main 8146bfc19 の上、既定 `amdrdna.smu.gfxclk=1`・`amdrdna.smu.boost=2`）。4 節と同じ自動の手順（起動 → desktop で 10 s → App Home の開閉 5 回 → app を 3 つ起動して各 5 s → 閉じる 1 回）を 2 回:
1. D、`amdrdna.vk.timing=5000`（既定の boost=2）。
2. D、`amdrdna.vk.timing=5000 amdrdna.smu.boost=1`（Linux と同じ 3D の profile だけ）。
見る物（各回）:
- dmesg の `A4 graphics clock: …` の行: levels の数と最低・最高 MHz（RX 5600 XT は最高 1.6〜1.75 GHz の見込み）、workload・averages・ceiling の err が 0 か、boost の mode。`smu: the graphics clock's boost refused` が無いこと。
- 各 period の `vk timing: clocks gfx … top …MHz … | gfx busy …% top …% | boosts N`: **合格の目安は、App Home の開閉の period で top が最高の level 付近（1 では profile だけでどこまで上がるか）、boosts が period ごとに増えること、idle の 10 s の period の今の値が下限（800 前後）に戻ること**。gfx busy の top が 0 のままかどうかも見る（0 のままなら activity は firmware の前提の欠け、15 節）。
- `gpu busy … max …us`: 1 の期待は max 6.2 ms → 3 ms 前後（clock 比）。KWL PERF の compose frame_ms・rec submit、SLOW_PASS の数。
- **温度と fan**: `amdrdnactl status` の power の行（temp・fan）を desktop の間に 2 回（3 段目の r2・r3 は 50〜52 C、fan 0 rpm）、`thermal reading 10` の行。目視・音。thermal fault の行が無いこと。
- underflow: A6 の行と `amdrdnactl status` の display の underflow が 0 のままか。
- 目視: window・App Home の開く animation が 3 段目より滑らかか。
落ちた時: `amdrdna.smu.boost=0` で boost 無し（3 段目と同じ）、`amdrdna.smu.gfxclk=0` で DPM 無しに戻る。固まったら電源。

## 18. 5 段目の切り分け（2026-10-17 P7、4 段目の実機の生の file は P3 の worktree の `build/tmp/p19-d/{r1,r2}/`、3 段目は `build/tmp/p5-c/{r1,r2}/`）

1 run あたりの GPU の時間（`gpu busy` の ms ÷ runs、App Home の開閉の period）:

| 起動 | GFXCLK の設定 | 印の clock | 1 run の平均 | 1 run の最大 |
| --- | --- | --- | --- | --- |
| p5-c r1 | DPM 無し | 300 MHz（boot の値） | 5.1 ms（235/46） | 15.5 ms |
| p5-c r2 | DPM on、boost 無し | 今 800、average 802 | 1.95 ms（125/64） | 6.2 ms |
| p19-d r1 | DPM on、boost=2（profile と floor） | 今 800、**top 1780**、average 807〜808 | 1.89 ms（121/64） | 6.1 ms |
| p19-d r2 | DPM on、boost=1（profile だけ） | 今 800、top 800〜1030 | 1.94 ms（125/64） | 6.2 ms |

- 300→800 MHz で 2.6 倍（≒ 800/300 = 2.67）速くなった。この区間の run の時間は GFXCLK にほぼ比例する。800→「top 1780」では変わらない（3% 以内）。
- 4 段目の `top` は watch の読み（1 s ごと、boost を上げた 100 ms 後にも 1 回）の `CurrClock` の最大で、boost の floor を送った直後の読みに出る。`average`（firmware の平均の clock、10 ms の時定数）は全 period で 807〜808 で、印の直前の idle の読み。仕事の間の average は記録していない。
- **第一の仮説（ユーザーの見立て、2026-10-17）**: 「AMDですが、1780 MHzは、実際に負荷が高くならないと上がらない可能性があります。ファームウェアが賢いので。ドライバの指定は最大値かもしれないです」。→ `CurrClock` の 1780 は driver が許した値（floor）の表示で、desktop の短い仕事（1 run 2 ms、frame の間に 20 ms 程度の空き）では firmware が実際の clock を上げていない、と読む。run の時間が 800 MHz の時と同じことと、average が 807〜808 のままなことに合う（未確認）。
- 他の候補（今回は測らず残件、10 節）: (a) UCLK（memory の clock）。DPM の UCLK を有効にしていないので、clock は VBIOS の boot の値（A1v の `mclk=875MHz`）のままの見込み。metrics の `CurrClock` の UCLK が 0 なのは DPM が無い clock を firmware が 0 で返すため（15 節）で、101 MHz という値は dmesg に無い。300→800 MHz で GFXCLK に比例した事から、800 MHz の手前まで memory 律速ではない。(c) run の中の待ち（barrier の flush・EOP の待ち、CP の packet の処理）。
- ユーザー（2026-10-17、Q1 経由）:「300MHzと800MHzで変化があるなら、一回負荷を上げて測定した上で、記録として残していただき、先に進んでください。」→ 切り分けは広げず、高い負荷を長く掛ける 1 回の測定だけを足す。UCLK の実験・run の中の内訳の測定は残件（10 節）。

### 試験の app `vkload`（2026-10-17 P7）

- `plan/ws039/tests/vkload/main.c`・`vkload/spin.frag`、build は `plan/ws039/tests/vkload-build.sh BUILD`（vkhang と同じ形、glslc で SPIR-V の header を作り image の libvulkan に link）。
- 正方形の B8G8R8A8 の target（`--side`、既定 1024）を clear し、compositor の quad の vertex program と、16384 歩の loop の pixel program（`x = x * 1664525 + 1013904223`、始まりと歩数は texcoord × 0 から作るので compiler が畳めない）の quad で覆う。command buffer を毎回 pool の reset から記録し直して submit・fence の待ちを `--seconds`（既定 10）の間くり返す。`--gap=MS` で draw の間に眠る（0 = 連続の負荷、20 = desktop の短い burst に近い形）。
- 出力（submit から fence までの時間）: 1 秒ごとに `VKLOAD second=<s> draws=<n> mean=<us> min=<us> max=<us>`、最後に `VKLOAD ok seconds=<s> side=<n> gap=<ms> draws=<n> mean=<us> first=<us> last=<us>`（first・last は最初と最後の 1 秒の平均）、失敗は `VKLOAD fail <step> <VkResult>`。
- 見込みの重さ（未測定）: 1024×1024 の 1 draw は 800 MHz で数十〜100 ms 前後、1780 MHz で半分以下。GPU の run の期限（2 s、`VK_NODE_RUN_MS`）より十分短い。draw の間は executor の node の lock を持つので desktop は止まりがちになる（試験の間だけ）。

### 試験（host）と build（2026-10-17 P7）

- `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p7/usb build/p7/usb/dynamic/libvulkan.so build/p7/usb/dynamic/libc.so`: rc 0、`warning:` 0 行（link の相手）。
- `sh plan/ws039/tests/vkload-build.sh build/p7/usb`: rc 0（`-Wall -Wextra -Werror`）、check-dynamic-elf PASS、`build/p7/usb/bin/vkload`（21640 byte）。
- pixel program を amdrdna の GFX10 compiler と interpreter で: `gfx10-graphics-test`（amdrdna-compiler-host-test.sh の 3 と同じ build）に `spin.frag` を渡し `PASS spin-frag: 16 trials, 128 words, 9 VGPRs, 40 SGPRs`（IR の参照と一致。対照の vkhang の `loops.frag` も PASS）。
- kernel は変えていない（4 段目の kernel D = 0f6b22184 のまま）。
- 未実施: 実機（19 節）。

## 19. 実機の依頼（5 段目、P3 へ、未実施）

kernel D（commit 0f6b22184、P3 の `build/tmp/p19-d/vmunix` と同じ）と、app `vkload`（commit はこの段の WIP、`sh plan/ws039/tests/vkload-build.sh <BUILD>` で作り、guest に置く。P7 の build は `/home/awe/zedBSD-worktrees/p7/build/p7/usb/bin/vkload`）。2 回の起動:

1. D、boot の行に `amdrdna.vk.timing=5000 amdrdna.smu.boost=0`（DPM は on、driver の boost 無し = firmware だけが clock を決める）。
2. D、boot の行に `amdrdna.vk.timing=5000`（既定の boost=2、floor を最高の level に）。

各回の手順（desktop が出た後、ssh で）:
- `amdrdnactl status`（temp・fan を控える）→ 10 s 何もしない。
- `vkload --seconds=20`（連続の負荷）→ 5 s 待つ → `vkload --seconds=20 --gap=20`（短い burst の負荷）→ `amdrdnactl status`。
- 取る物: vkload の出力の全行、`dmesg` の `vk timing` の行（各 period の `clocks gfx … top …MHz average …`、`gfx busy … top …%`、`gpu busy … max`、`gpu runs`）、`amdrdnactl status` の power の行（temp・fan・fault）と display の underflow、`thermal` の行。

見る物:
- **1 の連続の負荷で firmware が GFXCLK を上げるか**: vkload の各秒の mean が 20 s の間に下がるか（first と last）、その period の `top` が 1780 付近まで上がるか。上がれば「firmware は長い高い負荷でだけ上げる」が確かめられる。上がらなければ、firmware が上げない別の理由（電力・温度の制限など）を疑う。
- **1 と 2 の比べ**: 連続の負荷の mean（2 が 1 の約 800/1780 倍 = 0.45 倍なら、floor で実際に clock が上がっている）。burst（`--gap=20`）の mean も 1 と 2 で比べる（desktop の形の負荷で floor が効くか）。
- 温度・fan: 2 で連続の負荷の後の temp・fan が 1 よりどれだけ上がるか、fault が無いこと、underflow 0。

判定の後（Q1・P7）: 2 の floor で draw が速くならない（mean が 1 と同じ）なら、boost の floor は効かないので既定を 0 か 1 にする（温度・fan の増えに見合わない）。速くなるなら既定 2 のまま、desktop の 1 frame の速さは clock より仕事の作り（run ごとの同期の待ち、draw ごとの register の全書き、barrier）で詰める（Q1 2026-10-17 の方針）。

## 20. 4 段目・5 段目の実機の結果（2026-10-17 P3、10.0.10.3、kernel D = P7 の build/p7/usb/vmunix の写し、sha256 先頭 f0a0330aaca050a6）

生の file は P3 の worktree の `build/tmp/p19-d/{r1,r2}/`（4 段目）・`build/tmp/p19-e/{r1,r2}/`（5 段目、vkload は P7 の build の `bin/vkload` と `dynamic/` の libvulkan・libc を `/tmp` に置いた）。全部 desktop（graphical、`login=graphical`）、電源は warm reboot。

**4 段目（17 節、01:20〜01:27、4 節の自動の手順）**:
- 1（boost=2）: `A4 graphics clock: 2 levels 300-1780MHz (0), workload 0, averages 0, ceiling 0, boost 3d profile and floor`、boost refused 無し。period ごとの clocks: 今の gfx は常に 800、top は全 period 1780、boosts 1→17、gfx busy top 0〜38%。gpu busy max: idle 14 us・2.7 ms、App Home・app の period 6.09〜6.21 ms、app の起動 7.0・8.0 ms。compose frame_ms 中央 11.7・max 15.9、SLOW_PASS 1（498 ms、schedule_us）。温度 48→49 C、fan 0→921 rpm（driven 18%）、thermal fault 無し、underflow 0。
- 2（boost=1）: `boost 3d profile`、top 800〜1030 MHz、boosts 15、gpu busy max 6.1〜8.0 ms（1 と同じ）、compose 中央 9.4・max 18.3、SLOW_PASS 1（363 ms）、温度 46→50 C、fan 0、fault 無し、underflow 0。

**5 段目（19 節、01:46〜01:53、`vkload --seconds=20` → 5 s → `vkload --seconds=20 --gap=20`）**:
| 回 | 連続の mean（first → last） | burst（gap=20）の mean | 連続の間の clocks | 後の status |
| --- | --- | --- | --- | --- |
| 1 boost=0 | 89.03 ms（92.18 → 88.36）、225 draw | 95.30 ms（99.56 → 95.78）、174 draw | 負荷の period で now 1780・average 1608〜1755 MHz（firmware が自分で上げる）、gpu busy 4.0〜4.9 s/period・max 87〜94 ms | 59 C（max 73 C）、fan 1352 rpm driven 26%、fault 0、underflow 0 |
| 2 boost=2 | 88.51 ms（90.82 → 88.18）、226 draw | 87.81 ms（91.20 → 87.44）、186 draw | now 1780・average 1748〜1771 MHz、gpu busy max 86.7〜87.3 ms | 60 C（max 75 C）、fan 1464 rpm driven 32%、fault 0、underflow 0 |
- 読み: **長く高い負荷では firmware が自分で 1780 MHz まで上げる**（boost=0 でも average 1750 前後）。連続の負荷の mean は 1 と 2 でほぼ同じ（比 0.994、floor の見込みの 0.45 倍にならない）。burst では 2 が 8% 速い（0.92 倍）が、温度は max 73→75 C、fan 1352→1464 rpm。
- **判定（P3、Q1 の指示「2 で速くならなければ 1」）**: floor は連続の負荷を速くしない → `amdrdna.smu.boost` の既定を 1（3D の workload profile だけ、Linux と同じ）に変えた（`smu.c` の既定と注釈）。2 は boot の語で残る。desktop の 1 frame の速さは clock より仕事の作り（run ごとの同期の待ちなど）で詰める（Q1 2026-10-17 の方針）。
- 未実施: 既定 1 の kernel での desktop の確かめ（2 の 4 段目の数字と同じ見込み）、i915 との比べ。

## 10. 残り・再開点

- （5 段目の後）19 節の実機で firmware が連続の負荷で GFXCLK を上げるかと、boost=2 の floor で draw が速くなるかを見て、boost の既定（0・1・2）を温度・fan と効果で決め、merge を依頼する。
- 残件（2026-10-17 ユーザーの指示で今回は測らない）: (a) UCLK の実際の値（metrics の `AverageUclkFrequency`・`AverageUclkActivity` を読む）と、UCLK を最高の level に固定する opt-in の実験（DPM_UCLK と 2 つの memory の電圧の feature を permit し、soft min を最高に。SMU に watermark の表を渡していないので display の underflow を監視、design 8 節の危険）。(b) firmware の `ThrottlerStatus`・平均の電力の読み、仕事の間の速い読み。(c) run の中の GPU の時刻の内訳（barrier ごとに、CP が着いた時・描画の終わり・flush の後の timestamp）。(d) clock を固定する実験の語（soft min = soft max）。
- 削除の依頼（Q1、5 段目）: P7 の worktree の `build/tmp/p19e-libs.log`・`p19e-vkload.log`・`p19e-spin.*`。
- （4 段目の後）17 節の実機で boost=2 の clock と gpu busy を見る。下がり方と温度・fan に問題が無ければ既定の 2 のまま。1 で十分に上がるなら、1 を既定にするかを Q1 経由でユーザーに諮る。上がった後の残り（draw ごとの register の全書き、barrier）は、その時の `gpu busy` で選ぶ（3 段目の残りの (3)、今回は未着手）。
- 4 段目で入れていない Linux の物: DS_GFXCLK（idle の deep sleep。Linux は既定で permit。入れれば idle で 800 MHz より下がる）、SOCCLK・UCLK の DPM（UCLK は design 8 節の underflow の危険で入れない。SOCCLK は boot の clock のまま）、`NotifyPowerSource`。どれも今回の範囲（GFXCLK の上下）の外で、入れるかは実機の結果の後に決める。
- design.md の 12 節（boot の parameter）に `amdrdna.smu.boost=0|1|2`（既定 2）、4.4 に「levels の読み・workload・averaging 設定・ceiling の解除、仕事の間の boost」を足す直しは Q1 へ（P7 は design.md を触っていない）。
- 削除の依頼（Q1）: P7 の worktree の `build/tmp/` の `p19-*.log`・`amdrdna-host.*`・`amdrdna-vk.*`。

- （3 段目の後）14 節の実機で clock が 300 MHz と確かめ、`amdrdna.smu.gfxclk=1` で busy が下がれば、既定で on にするかを Q1 経由でユーザーに諮る（温度・fan・underflow の結果と一緒に）。下がった後の残り（register の全書き、barrier、shader）は、その時の busy で選ぶ。
- 2 段目の text の suspend は (a) の結果で問題が無いので main に入れる候補（3 段目の commit に含む）。
- design.md 12 節の boot の parameter に `amdrdna.smu.gfxclk=1`、4.4 に「DPM_GFXCLK だけを試す既定 off の語」を足す直しは Q1 へ。
- （2 段目の後）9 節の実機の結果で、GPU の run の 6 ms の中身（`gpu busy` と待ちの比べ）を見て次を選ぶ: busy が大きければ GPU 側（clock の DPM、draw ごとの register の全書き、barrier の待ち、shader）、小さければ submit・待ちの側。
- 実機の結果で、次に重い所（候補 2 の stream の UC の読み、3 の間接 buffer の UC の書き込み、4 の user の map）を選ぶ。2・3 は run の memory と stream を system memory（GART）に置くか、窓を WC にする形（BUG-331・p018 と関わる）が候補。
- `kern_usleep_range` は busy の遅延なので run の待ちは CPU を使い続ける（速さには影響しないが、他の thread の時間を取る）。
- 削除の依頼（Q1）: worktree の `build/tmp/vk-execute.keep`（わざとの誤りの試験の退避）と `build/tmp/` の試験の出力（`amdrdna-vk.*`・`amdrdna-host.*`・`p19-*.log`）。
