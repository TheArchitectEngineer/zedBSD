<!-- awesome-plan project=zedbsd record=ws288-p011 -->

# ws288-p011 端から端の統合（Verilator の上で libvulkan → vk/ → firmware → RTL）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p011 の行（H6 の場）、§6.7、§6.8（試験の道）。
- 状態: cleared（2026-10-25 11:32 Q1。vkdemo の 1 frame が Verilator の上で oracle と一致）
- 依存: p009・p027・p030（RTL）、p010（compiler）、p018（firmware、P2 の Verilator の firmware の harness `sim/verilator/fw_harness.cpp`・`fw-run.sh`）、p019（vk/、zedBSD の host 試験に model の GPU と stand-in の firmware）、p006（driver）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §8 の p011 の行）

- **場**: host で build した libvulkan（`userland/desktop/libvulkan/`、変えない）と zedBSD の driver（`src/drivers/gpu/ghci/`、`vk/` を含む）を、偽の drv_gpu（ioctl の代わりに関数、p019 の host 試験の model の形を広げる）でつなぎ、HCR と GPU の memory を Verilator の model（amaterrace-gpu の atg_top、CPU と firmware、KC705 の形）に DPI か共有の memory でつなぐ。
- **目標**: vkdemo（または同等の三角形・texture の program）の 1 frame を、libvulkan の API から描き、readback が CPU の参照と一致。加えて vkCmdFillBuffer・vkCmdCopyBuffer、compute の dispatch。
- **性能の最初の計測**: draw あたりの firmware の cycle、1 frame の往復。
- 見つかった食い違い（driver・firmware・RTL の境）は、その場で直すか、担当の file なら Q1 に送る。

## 受け入れ

vkdemo の 1 frame の readback が参照と一致（Verilator）、fill・copy・compute の試験が PASS、長い試験は T に回す。

## 記録

- 2026-10-25 09:59 Q1: 新設。
- 2026-10-25 10:18 P3 → Q1: 段 0（amaterrace p3-p011 c3816d5、zedBSD agent/p3-p006 50f5545aa）。MEM の capability（XCAP 8、register 0x1C0・0x1C4・0x1C8、既定 0 で載せない）と RESIDENT_WARPS[23:16] = NREGS。KC705 の board の top: LOCAL_BASE 0x8000_0000（MIG の 1 GiB の alias、2 GiB 未満を PCIe の host の bus address に残す: P3 の判断）、1 GiB、窓 64 MiB。driver の vk/ が [23:16] を読んで compile に渡す。P3 の申告: atg_cp_regs 6 行・atg_kc705_raster 5 行 PASS、lint 127、host 試験 126・76・139。
- 2026-10-25 10:58 P3 → Q1（途中）: 端から端の harness が動いた（libvulkan → 偽の core → driver の vk/ と compiler → Verilator の atg_top の KC705 の形が 1 process、driver の start・firmware の起動・GHCI_HEAP・session・e2e.fill・e2e.copy が PASS、fill 約 25 万 cycle、transaction 平均 3.3k cycle）。見つけた不具合 2 つ: (1) driver が CTRL.RESET の後に CPFW_CTRL.RUN を書いていなかった（板では firmware が動かない）→ ghci_firmware_start を足す、(2) RTL の atg_cpu_win: DONE の後 UOP_FIFO が空になるまで PKT_INFO[31] が 1 のままで、firmware が同じ class V の packet を取り直す（QUEUE_SUBMIT の frame が 12 回走った。host の bench は DMA が同期なので出なかった）→ PKT_INFO[31] に !done_pend を足す。Q1 が両方を承認。
- 2026-10-25 11:04 P3 → Q1: 段 1（amaterrace p3-p011 352634e、zedBSD agent/p3-p006 8962609b6）。Verilator の KC705 の形の上で libvulkan の e2e.fill・e2e.copy・e2e.compute（scale.comp、1024 invocation、device local → copy）が PASS。sim/verilator/link.*（atg_top を C の API に）、atg_cpu_win の直し（直す前 FAIL・後 PASS の case）、driver の ghci_firmware_start、plan/ws288/tests/ghci-e2e*。計測（100 MHz の cycle）: driver の start 157k、fill 247k（transaction 平均 3.3k、vkQueueSubmit の job の往復 15.4k）、compute 299k。P3 の申告: atg_cpu_win・atg_cpu・atg_top_fw・atg_cp_regs・atg_kc705_raster PASS、fwtest 6/6・kernels 2/2、lint 127。次は vkdemo の 1 frame。
- 2026-10-25 11:20 P3 → Q1: **vkdemo の 1 frame（offscreen）の readback が Verilator の上で独立の oracle（plan/ws014/tests/vkdemo_oracle.py）と一致**: mismatch 0、checked 76,045 pixel、前景 6,757、面 3。fill・copy・compute も同じ run で PASS。run 全体 2.6M cycle、ASan 付きで wall 約 37 s。見つけた不具合 3 つ目（firmware、直した、amaterrace p3-p011 0b19d6b）: vkCmdEndRenderPass の後に WAIT_IDLE が無く、external の subpass dependency が守られず readback の copy が draw の途中で走った → firmware は dependency を持たないので、Vulkan 1.0 の原則（過剰な同期は許される）で end render pass は常に WAIT_IDLE + INVALIDATE（passrec.c、IMEM 60,580 B）。計測: vkdemo の frame 1.09M cycle = 10.9 ms @ 100 MHz、vkQueueSubmit の job（draw 1 回 36 頂点 + copy 307 KB）の往復 148k cycle、firmware が packet の中にいる cycle 943k（大半が unit の待ち）。
- 2026-10-25 11:25 P3（q1572-i01、zedBSD の worktree p3-p006 基点 ad7d5b54a → 58aa4de56、amaterrace-gpu の worktree p3-p011 基点 5262af6 → 0b19d6b）: 実装・build・Verilator の端から端の run。判定は Q1。

## 結果（2026-10-25 P3）

### 場（作った物）

- amaterrace-gpu `sim/verilator/link.h`・`link.cpp`・`link.vlt`・`link-build.sh`: atg_top を KC705 の board の形（CPU と committed の firmware image、64 B line、512 bit AXI、SU 1 つを board の形で、VF・RX・TU・ROP+stencil、MEM 1 GiB @0x8000_0000・窓 64 MiB）で Verilator にし、C の API（register 窓の read/write = register port の transaction、local memory の pointer = BAR2 の窓、IRQ の線、clock を回す、統計: cycle・firmware が packet の中にいる cycle・µop の語・register の read/write・local の外への access、DMEM の読み）で出す静的 library。診断: `LINK_TRACE_DMA`（CPU の line DMA の DMA_GO）、`LINK_TRACE_UOPS`（µop の語と units の busy の変化）、`LINK_READ_CYCLES`（register の read に PCIe の往復の cycle を足す）。
- zedBSD `plan/ws288/tests/ghci-e2e*`: 1 process・1 thread・simulation の時間。`ghci-e2e-kernel.c`（PCI の glue の代わりに link の BAR0/BAR2/IRQ、lock と wait queue（sleep は clock を回して IRQ の handler を呼ぶ、device の lock も ring の lock も持たれていない時だけ service thread の 1 回分）、clock = GPU の cycle、host memory は無い（board の GPU は DDR3 だけ））、`ghci-e2e-core.c`（偽の GPU core: /dev/gpu0、session・resource と mmap の token・queued command と completion の記録・job の reserve/commit/cancel/capacity・fence の payload と generation・poll の readiness。libvulkan の open/close/ioctl/mmap/munmap/poll/fcntl/opendir/readdir/closedir/clock_gettime/nanosleep を `--wrap` で受ける。monotonic の clock は simulation の時間）、`ghci-e2e-main.c`（case: fill・copy・compute=SPV・vkdemo=PPM と計測）、`ghci-e2e-wayland.c`（host の build に無い Wayland の surface の 2 つの入口）、`ghci-e2e/scale.comp`、`ghci-e2e.sh`（link-build、libvulkan をそのまま 27 file + vkdemo の renderer.c・display.c、driver と vk/ と compiler を ASan/UBSan で build、run、vkdemo の frame を `plan/ws014/tests/vkdemo_oracle.py` で判定。`GHCI_E2E_OUT` で再 build、`GHCI_E2E_TRACE=1` で executor の診断の trace）。libvulkan は変えていない。

### 結果（Verilator、自分で流した）

`AMATERRACE_GPU=<p3-p011> plan/ws288/tests/ghci-e2e.sh`（新しい directory、Verilator の build を含め wall 91 s）: `PASS e2e.fill: 32768 words`、`PASS e2e.copy: 16384 words`、`PASS e2e.compute: 1024 invocations`、`PASS e2e.vkdemo`、`PASS ghci-e2e`、oracle `PASS vkdemo-oracle`（mismatch 0、checked 76045 pixel、境界で除いた 755、前景 6757、見える面 3、前景の色 3711）。同じ run を 3 回、vkdemo の rgb sha256 は毎回 6a446b50…e9ac。

### 計測（100 MHz の cycle、Verilator。板は p014）

| 項目 | cycle |
| --- | --- |
| driver の start（identify・reset・firmware の起動・memory・ring・GHCI_HEAP・登録） | 157k |
| libvulkan の transaction（1 frame の往復、fill の case の平均） | 3.3k |
| vkdemo の 1 frame（vkdemo_render: reset・begin・12 record + end・submit・待ち・readback） | 1.09M（10.9 ms） |
| その内 record 12 個 + END の frame（firmware の µop への翻訳） | 103k（約 8.6k / record） |
| その内 QUEUE_SUBMIT の frame（replay と draw 1 回 36 頂点 320×240 + copy 307 KB） | 830k |
| vkQueueSubmit の job（commit → fence） | 148k |
| firmware が packet の中にいる cycle（frame の間） | 943k（大半は units の待ち） |

性能の所見: firmware は replay の µop を 16 語の UOP_FIFO に入れ、WAIT_IDLE で sequencer が止まる間 CPU も止まるので、QUEUE_SUBMIT の frame の RESULT（= libvulkan の vkQueueSubmit の返事）が GPU の仕事の終わりまで遅れる（830k のうち job の往復は最後の 148k）。host の thread が vkQueueSubmit の中で frame の時間だけ待つ。直すなら firmware が replay の前に frame の result を書く、または sequencer が recording の chunk を間接に読む µop（後の Phase の候補、Q1 の判断）。

### 見つけた食い違いと直し

1. **RTL に MEM の capability が無い**（GHCI §11.3）: atg_cp_regs・atg_cp・atg_top に LOCAL_BASE・LOCAL_BYTES・WINDOW_BYTES（既定 0）、XCAP id 8 は 0x1C0、KC705 の board の top に値（Q1 OK）。同時に RESIDENT_WARPS[23:16] = lane の register 数（Q1 の判断）、driver の compile が使う。
2. **driver が firmware を起動していなかった**: CTRL.RESET は CPFW_CTRL.RUN を 0 にするので、`ghci_firmware_start`（reset と recover の後に CPFW の RUN、READY・!FAULT・!STOPPED を待つ）を足した（Q1 OK）。
3. **RTL atg_cpu_win: DONE の後 FIFO が空になるまで PKT_INFO[31] が 1**で、firmware が同じ class V packet を取り直した（QUEUE_SUBMIT の frame が 12 回走った）。PKT_INFO[31] は DONE の書き込みから 0、fw_active は不変。atg_cpu_win_tb に case（直す前 FAIL・後 PASS を確認）（Q1 OK）。
4. **firmware: vkCmdEndRenderPass の後に待ちが無く**、render pass の external の subpass dependency が守られず readback の copy が draw の途中で走った。firmware は dependency を持たないので、end render pass は常に WAIT_IDLE + INVALIDATE（Vulkan 1.0 は過剰な同期を許す）。

### 確認（自分で実行、件数は出力で数えた）

- amaterrace-gpu: `sim/run.sh atg_cp_regs`（PASS 6 行）、`atg_kc705_raster`（PASS 5 行）、`atg_cpu_win`（PASS 5 行、直す前は新しい check が FAIL）、`atg_cpu`・`atg_top_fw` PASS、`tools/lint.sh` 127 clean・他 0、`fw/cp/tests/run.sh` fwtest 6/6・kernels 2/2、`tools/fw-build.sh` IMEM 60580 B / 64 KiB（warning 0）。
- zedBSD: `plan/ws288/tests/ghci-host-test.sh` 126・76・139 passed 0 failed、`make ZEDBSD_CONFIG=plan/ws288/tests/config-amd64-ghci.mk BUILD=build/p011 build/p011/vmunix` exit 0・warning 0・`kernel include check: PASS`・`amd64 vmunix check: PASS`。
- **未実施**: 板（KC705、p014）、QEMU、長い試験（下の T の一覧）、規約の全文の正式な見直し（p016。test の file は自分で 1 回: 関数の呼び出しを条件に入れない、条件演算子をやめる、など）。

### 自分で決めた事（Vulkan 1.0 の原則と GHCI の文面で）

1. 場は DPI でなく 1 process の静的 link（Verilator の model を C の API で、時間は simulation の物）: 決定的で、cycle が正確に数えられる。
2. KC705 の LOCAL_BASE は 0x8000_0000（MIG は bit 29:0、2 GiB 未満を後の PCIe の host の bus address に残す）、窓は design §6.2 の 64 MiB。
3. firmware の起動は driver が CPFW の RUN を書く（GHCI §7.2 の「start of the firmware (RUN, a RESET)」の文面、CPFW の register は ghci_regs.h に有る）。capability が無い GPU は firmware が自分で走ると見なす。
4. end render pass は常に待つ（上の 4）。
5. 偽の core は kernel の core（gpu.c）を簡単にした物: 資格・監督の期限・open の間の共有は無し、display・export/import・placed blob は ENOTTY。

### T に流してほしい試験

1. amaterrace-gpu p3-p011 0b19d6b: `make sim-quick`（全部、atg_cpu_win・atg_top_fw・atg_cp_regs・atg_kc705_raster を含む）、`sim/verilator/fw-run.sh`（firmware の harness: 各 case の frame が 1 回だけ走り、µop・result・STATE が host と一致）、`sim/verilator/run.sh`。
2. zedBSD agent/p3-p006 58aa4de56 + amaterrace-gpu 0b19d6b: `AMATERRACE_GPU=<checkout> plan/ws288/tests/ghci-e2e.sh`（wall 約 1.5 分、期待: 上の PASS 8 行と oracle の mismatch 0）。

### 残件と再開点

- 性能の所見（vkQueueSubmit の返事が GPU の仕事の終わりまで遅れる）の扱いは Q1 の判断。
- link の read の往復（`LINK_READ_CYCLES`）を PCIe の値にした計測は未実施。
- 削除の依頼（Q1 の pipeline で）: zedBSD の worktree の `build/tmp/p011-try`・`build/tmp/p011-dev`・`build/tmp/ghci-e2e.*`・`build/tmp/ghci-host.*`・`build/tmp/p011-fresh.log`・`build/p011`・`build/p011-make.log`、symlink `build/llvm`・`build/NoctLang`。amaterrace-gpu の worktree の `build/verilator-link`・`build/fw-host`・`build/fw-cp`・`build/sim`・`build/lint-p011.log`・`build/link-build-run.log`。
- 2026-10-25 11:32 Q1: amaterrace-gpu の p3-p011 0b19d6b を main に（aa78b64）、zedBSD の agent/p3-p006 e2ead179a を main に（895147e55）。merge した両方の main で `AMATERRACE_GPU=~/amaterrace-gpu plan/ws288/tests/ghci-e2e.sh` が 8 行 PASS、vkdemo の oracle の mismatch 0・checked 76,045、sha256 6a446b50…e9ac（P3 の 3 回と同じ）（Q1 が流した）。画像は build/review/ws288-p011-vkdemo-4x.png。→ **cleared**。RTL の全体は T1-784。
  - 性能の残件（Q1 の判断、推し）: vkQueueSubmit の返事が GPU の仕事の終わりまで返らない（firmware が packet の中で記録を再生し WAIT_IDLE で止まる）→ firmware が再生の前に frame の結果を書いて返す形にする（firmware だけの変更、Vulkan の submit は待たない原則）。次の担当に。LINK_READ_CYCLES を PCIe に近い値にした計測は未。
- 2026-10-25 11:46 P3 → Q1（p011 の残件の直し）: amaterrace p3-p035 398a738、zedBSD agent/p3-p006 b31a778c8。firmware は submit の command buffer を確かめて list に置き、RESULT を上げた後に replay、その後で DONE（DONE で CP が driver の FENCE へ進むので、replay を DONE の後に回すと仕事の前に fence が signal される）。vkQueueSubmit が返るまでの cycle: vkdemo の frame 829,816 → 3,723、fill 21,932 → 3,711。P3 の申告: e2e 8 行 PASS・oracle の mismatch 0・sha は同じ、fwtest 6/6・kernels 2/2、IMEM 61,032 B、driver は不変。
- 2026-10-25 12:05 Q1: T1-784（amaterrace-gpu main aa78b64、p011 の merge の後）PASS: make sim 97、msaa 16、verilator 6、fw-run.sh 6 case 0 differ、lint 127。

## 追記: vkQueueSubmit を待たずに返す（q1574-i01 の 1、2026-10-25 P3）

- 2026-10-25 11:45 P3（amaterrace-gpu p3-p035 基点 aa78b64 → 398a738、zedBSD agent/p3-p006）: 実装・host 試験・Verilator の端から端の run。判定は Q1。
- **形**: firmware の QUEUE_SUBMIT は command buffer を確かめて（session の executable な primary か、one-time なら INVALID に）list（16 個）に置くだけにし、replay は packet の結果（record の結果・frame の結果）を書いて flush し RESULT を上げた後に走らせ、その後に DONE（`fw/cp/submit.c` の `fw_submit_run`、`fw/cp/packet.c`）。frame の中で submit でない record の前には list の replay を先に走らせる（後の command は前の submit の仕事を見る: 今までの順序と同じ）。list が満ちたら先に走らせる。malformed な submit は自分の entry を落とし one-time の状態を戻す。
- **Q1 の推しとの違い（P3 の判断）**: 「replay を packet の外（DONE の後）」は採らなかった。DONE で CP は次の ring の packet（driver が packet の後に置く FENCE）へ進むので、replay が DONE の後だと FENCE が仕事より先に走り fence が早く signal される。RESULT を先に・DONE を replay の後に、で FENCE の順序と後の packet の順序は変わらない。代わりに ring は replay の間は次の packet を取らない（今と同じ。後の transaction は GPU の仕事の後になる）。
- **fault・session**: replay が RESULT の後に失敗（recording の line を memory が拒む、marker の失敗）したら、その session を lost にし、後の QUEUE_SUBMIT は VK_ERROR_DEVICE_LOST で何も走らせない（Vulkan 1.0: device lost は後の command で知らせてよい、fence は success でもよい）。session の close は packet なので replay の後。RESET は CPU の .bss を消すので list も消える。watchdog・fault・halt の扱いは packet の中のままで不変。
- **GHCI の文**（docs/ghci.md §7.2）: result は「decoded and checked」、submit は仕事を待たない（firmware は RESULT の後に仕事を渡してよい、後の submit でない command と RING_RPTR の通過の前には渡す、driver は RPTR・RESULT を「渡した」と取らない、渡す時の失敗は session を lost に）。fw/cp/README.md も。driver は変えていない（frame の完了は result page で判断し、RPTR を使わない: vk-engine.c の frame_ready を確認。staging は decode の後には読まない）。
- 計測（zedBSD `plan/ws288/tests/ghci-e2e-main.c` に `__wrap_vkQueueSubmit` で vkQueueSubmit が返るまでの cycle を足した、`ghci-e2e.sh` の wrap に `--wrap=vkQueueSubmit`）:

| case | vkQueueSubmit が返るまで（前、aa78b64） | 後（398a738） |
| --- | --- | --- |
| fill | 21,932 | 3,711 |
| copy | 31,078 | 3,723 |
| compute | 9,863 | 3,711 |
| vkdemo の frame | 829,816 | 3,723 |

  vkdemo の frame 全体は 1,090,343 → 1,092,309 cycle（1 frame をすぐ待つので変わらない。job の commit → fence は 147,734 → 974,933 で、仕事の時間が fence の側に移った）。
- 確認（自分で実行、件数は出力で数えた）: `fw/cp/tests/run.sh` fwtest 6/6・kernels 2/2（新しい check 5 つ: RESULT が replay の前、replay が RESULT の後に走り fill が入る、frame の中の submit の後の reset の前に replay、その fill。前の firmware に戻すと「RESULT before the submit's replay」が FAIL を確認）、`tools/fw-build.sh` warning 0・IMEM 61,032 B / 64 KiB（+452 B）・DMEM image 3,616 B、`AMATERRACE_GPU=<p3-p035> plan/ws288/tests/ghci-e2e.sh` の PASS 8 行（link-build・fill・copy・compute・vkdemo・ghci-e2e・vkdemo-oracle・ghci-e2e.sh）、oracle mismatch 0・checked 76,045、sha256 6a446b50…e9ac（前と同じ）。
- `sim/verilator/fw-run.sh`（Verilator の firmware の harness、P3 が流した、2026-10-25 11:4x〜12:0x）: `PASS fw.sessions`・`fw.buffers`（10 step、µop 256 語の差 0、result 72 語の差 0）・`fw.images`・`fw.draw`・`fw.compute`・`fw.transfer`・`PASS verilator fw`（7 行、exit 0）。
- T に: `make sim-quick`（RTL は不変なので回帰の確認だけ）。
