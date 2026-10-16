<!-- awesome-plan project=zedbsd record=ws039-p019 -->

# ws039-p019 desktop の描画の速さ（app の window を開く animation が 1 fps 未満）

Status: in-progress（2026-10-16 P5: code を読んだ見立てで一番重い道（app の frame の buffer → image の copy が CPU の窓を通る）を GPU の move に直し、host 試験と build まで。実機の測定（直す前後と i915 の 5330）は P3 への依頼待ち）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1235 / q1235-i01（承認 2026-10-16 ユーザー「appの起動時のウィンドウが開くアニメーションが1fpsも出ていないですので、高速化は必要です。」、Q1 の割当）
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

## 5. 残り・再開点

- 実機の結果で、次に重い所（候補 2 の stream の UC の読み、3 の間接 buffer の UC の書き込み、4 の user の map）を選ぶ。2・3 は run の memory と stream を system memory（GART）に置くか、窓を WC にする形（BUG-331・p018 と関わる）が候補。
- `kern_usleep_range` は busy の遅延なので run の待ちは CPU を使い続ける（速さには影響しないが、他の thread の時間を取る）。
- 削除の依頼（Q1）: worktree の `build/tmp/vk-execute.keep`（わざとの誤りの試験の退避）と `build/tmp/` の試験の出力（`amdrdna-vk.*`・`amdrdna-host.*`・`p19-*.log`）。
