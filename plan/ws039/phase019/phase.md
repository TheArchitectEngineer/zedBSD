<!-- awesome-plan project=zedbsd record=ws039-p019 -->

# ws039-p019 desktop の描画の速さ（app の window を開く animation が 1 fps 未満）

Status: in-progress（2026-10-16 P5 2 段目 q1235-i02: 1 段目の A/B を読み、compose の 100〜1000 ms の残りは timing の印そのもの（kernel の text console の UC の再描画）と切り分けた。amdrdna の desktop の間の text console を止める修正と、GPU の timestamp の測定を足し、host 試験と build まで。実機の A/B は P3 への依頼待ち、9 節）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1235 / q1235-i01（1 段目）・q1235-i02（2 段目）（承認 2026-10-16 ユーザー「appの起動時のウィンドウが開くアニメーションが1fpsも出ていないですので、高速化は必要です。」、Q1 の割当）
実行者: P5（phase-runner、worktree `/home/awe/zedBSD-worktrees/p5`、branch `agent/p5-ws039`）
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

kernel A' = main `9f5ffb9a1`（1 段目の DMA_DATA まで）、kernel B' = この 2 段目の commit。4 節と同じ自動の手順（起動 → desktop で 10 s 何もしない → App Home の開閉 5 回 → app を 3 つ起動して各 5 s → 閉じる 1 回）を 3 回:
1. B'、`amdrdna.vk.timing=5000`: 期待 = 印の period の長さが 5000〜5100 ms 前後（前は 6245〜6606）、KWL PERF の `walk replay max` に 2 s 前後の値が無い、compose の frame_ms が印の period でも 18〜30 ms。新しい `gpu busy` の行の counter の kHz（100000 前後の見込み）と busy の ms を、`gpu runs` の待ちの ms と比べる（busy ≒ 待ちなら GPU の実行が重い、busy が小さければ submit・待ちの側）。
2. A'、`amdrdna.vk.timing` 無し、3. B'、`amdrdna.vk.timing` 無し: ユーザーが見た「印の無い時」の比べ。session.log の KWL PERF（compose frame_ms・rec begin・SLOW_PASS の数と us）を A' と B' で比べる。特に login の直後・session の始まり（input の行など kernel の行が出る所）の period。dmesg の `display lease 1 claimed` の後の行の数も数える（desktop の間の kernel の行の数）。
取る物（各回）: `dmesg`、compositor の session.log、`amdrdnactl status`、目視の感想。lease の終わりの console の戻り（reboot の時に shutdown の行が画面に出る）も見られれば。

## 10. 残り・再開点

- （2 段目の後）9 節の実機の結果で、GPU の run の 6 ms の中身（`gpu busy` と待ちの比べ）を見て次を選ぶ: busy が大きければ GPU 側（clock の DPM、draw ごとの register の全書き、barrier の待ち、shader）、小さければ submit・待ちの側。
- 実機の結果で、次に重い所（候補 2 の stream の UC の読み、3 の間接 buffer の UC の書き込み、4 の user の map）を選ぶ。2・3 は run の memory と stream を system memory（GART）に置くか、窓を WC にする形（BUG-331・p018 と関わる）が候補。
- `kern_usleep_range` は busy の遅延なので run の待ちは CPU を使い続ける（速さには影響しないが、他の thread の時間を取る）。
- 削除の依頼（Q1）: worktree の `build/tmp/vk-execute.keep`（わざとの誤りの試験の退避）と `build/tmp/` の試験の出力（`amdrdna-vk.*`・`amdrdna-host.*`・`p19-*.log`）。
