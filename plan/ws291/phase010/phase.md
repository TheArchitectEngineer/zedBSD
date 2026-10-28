<!-- awesome-plan project=zedbsd record=ws291-p010 -->

# ws291-p010 NVIDIA（RTX 2070）: nvrtx の起動・desktop の計測と 60 fps、Linux の NVK の基準

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-29 06:20 P2、q1670-i01、worktree p2-ws291m、基点 main 42da8d889）。
- 承認: ユーザーの回答（クリック、2026-10-29）「WS291 の NVIDIA の段 (推し)」（問い: RX 5600 XT を外している間、P2 は何をするか）。
  機械の変更（原文）「再起動後、RTL8127と、RTX 2070が接続されています。RX 5600 XTはPCIスロットの高さの関係で、RTL8127と排他だからです。」
- Linux の基準の driver: ユーザーの回答（クリック、2026-10-29、Q1 経由）「NVK（nouveau） (推し)」→ Mesa の NVK、設定の file は変えず、計測の時だけ sysfs で
  vfio-pci から外して nouveau（GSP、`config=NvGspRm=1`）を読み込み、終わったら vfio-pci に戻す。
- BAR1 の window の直し: Q1 の判断（2026-10-29）「WS291 の NVIDIA の段の目標（desktop 60 fps・model 50 fps）を塞ぐので WS291 の中で直して」、
  形は「host-visible で要る物だけ map、device-local は map しない／要る時だけ」、直しの前後で window の使用量を数えて記録。vk-node.c の変更は WS250 の
  design.md の前提と食い違わない形に（食い違うなら Q1 へ）。

## 範囲

1. plan/ws291/tests/config.mk の `CONFIG_DRIVER_PCI_NVRTX := n` を戻す（nvrtx と GSP の firmware は CI の config から）。
2. RTX 2070 で今の nvrtx が起動・表示するかを確かめる（NVMe の zedBSD に kernel、BootNext）。
3. desktop の frame-ops の段（RDNA の p001 と同じ表）を 60 fps に。妨げる driver の不具合を直す。
4. Linux（Debian、NVK）で約 50 fps になる scene を決める（model の段は p011）。

## 受け入れ

- frame-ops の各段が 60 fps（p001 と同じ形の表）。amd64 の vmunix・変えた userland が warning 0。実機の証拠と未実施を分けて記録。

## 記録

### 起動（2026-10-29 06:20〜06:30、実機 RTX 2070、kernel = 1746f7c18 の build、cksum 2886737663）

- 1746f7c18: config.mk の NVRTX を戻した。vmunix は link でき（3412200 byte、AMD64_KERNEL_MAX_BYTES の内、P3 の packet pool の直しが main にある）warning 0。
- `rdna-nvme.sh kernel` で NVMe の p4 の vmunix に（前の物は vmunix.old）、`measure.sh zedbsd` で BootNext 0004。
  dmesg（SSH）: identify TU106 → layout → FRTS → booter → GSP-RM up（963 ms）→ static-info → vram-window（BAR1 256 MiB）→ copy-channel → golden-context →
  work-channel → display（HDMI の SOR 2、1920x1080）→ takeover → gpu-device 登録 → lease 1（compositor）。/dev/gpu0 があり wayland・sessiond が走る。
- 道具: plan/tools/gpu-perf/drivers/nvrtx.conf（status は無し、timing は `nvrtx.vk.perf=1`）と machines/rtx.conf（rdna.conf と同じ機械、driver nvrtx）。
  `GPU_PERF_MACHINE=rtx` で measure.sh を使う。担当の worktree の plan/tmp/guest/id_ed25519 は mode 600 が要る（664 だと measure.sh が zedBSD を見つけない）。

### desktop（build/ws291/rtx/desk1、frame-ops）

| 段 | frames | fps | 備考 |
| --- | --- | --- | --- |
| launch | 46 | 59 | terminal は開かない（下） |
| type | 3 | 77 | 空振り |
| keys | 11 | 71 | 空振り |
| output | 4 | 35 | 空振り |
| scroll | 4 | 20 | 空振り |
| hover | 2 | 48 | |
| home | 68 | 62 | |
| wiseview | 53 | 60 | work max 148 ms が 1 回 |

- compositor の frame: KWL PERF の submit+present 9.3 ms、draw 2.2 ms（RDNA より重い、次の調べ）。
- **不具合**: compositor 以外の client が全部 VK_ERROR_OUT_OF_DEVICE_MEMORY（errno ENOSPC = 8）。terminal は `ZTERM FAILED operation=vkAllocateMemory
  result=-2 errno=8`、mview は `vkCreateSwapchainKHR result=-2 errno=8`、ZFILES の swapchain も、keiland-shot は `ERROR buffer`。
  見立て（code を読んだ、未確認）: nvrtx の executor の GPU memory（vk-node.c `nvrtx_vk_gpu_alloc`）と blob は VRAM を必ず BAR1 の CPU の window
  （256 MiB、console 8.6 MiB を含む）に map する（`nvrtx_bo_map`、満杯で ENOSPC）。libvulkan は kernel の ENOSPC を OUT_OF_DEVICE_MEMORY にする
  （context.c `vulkan_kernel_error`）。compositor の画・import（1920x1036 の 7.6 MiB 等）で window が埋まる。

### Linux の NVK の基準（2026-10-29 06:30〜06:38、Debian 6.12.111、Mesa 25.0.7、nouveau NvGspRm=1、build/ws291/rtx/nvk1・nvk2・nvk3）

- 手順: `echo 0000:01:00.0 > /sys/bus/pci/drivers/vfio-pci/unbind`、driver_override に nouveau、`modprobe nouveau modeset=1 config=NvGspRm=1`
  （vulkaninfo: NVIDIA GeForce RTX 2070 (NVK TU106)）→ `GPU_PERF_MACHINE=rtx LINUX_MVIEW=… measure.sh bench linux` → 戻し: weston を止め、nouveau から
  unbind、vfio-pci に bind。`modprobe -r nouveau` は softdep で vfio_pci も外すので、その後 `modprobe vfio-pci`（4 function とも vfio-pci に戻ったのを確かめた）。
  次から modprobe -r はしない。

| run | fps | frame ms | record | submit | fence |
| --- | --- | --- | --- | --- | --- |
| scene-1 | 9086 | 0.11 | 0.01 | 0.01 | 0.07 |
| scene-1500 | 165.6 | 6.04 | 0.06 | 0.04 | 5.80 |
| scene-4500 | 54.5 | 18.30 | 0.07 | 0.04 | 17.85 |
| **scene-5000** | **50.1**（単独の run 49.1） | 19.90 | 0.08 | 0.05 | 19.36 |
| scene-5500 | 45.4 | 21.99 | 0.10 | 0.06 | 18.40 |
| scene-6500 | 38.4・39.2 | 25.5〜26.0 | 0.10 | 0.06 | 21.4〜25.2 |
| big-1000 | 136.2 | 7.34 | 3.15 | 0.03 | 4.09 |
| subs-1000 | 75.4 | 13.25 | 6.13 | 4.63 | 2.45 |

- → NVIDIA の model の scene は qs40 ×5000 instanced（Linux NVK 50.1 fps）。
- 条件の差: weston は NVK では全 repaint が「Using render-only state composition」（`[state] no overlay plane support`、dmabuf の modifier は
  NVIDIA_BLOCK_LINEAR_2D）で、RDNA の RADV のような plane-only（direct scanout）にならない。Linux 側は合成の分（1080p の 1 枚）を含む。

### BAR1 の window の直し（06cdbf381、印 cf48046e5）

- 06cdbf381: blob を作る時に window に map しない（VRAM は PRAMIN で 0 に clear、`nvrtx_bo_clear`）。CPU が初めて触る時に map（`nvrtx_node_backing_cpu`:
  resource の read・write・map、transport の stream・reply の blob、cursor の画、executor の CPU の copy は `nvrtx_vk_memory_cpu`）、その後は VRAM を返すまで
  map のまま。`nvrtx_bo_map` は二重 map を防ぐため確かめを lock の下に。window の使用・peak を nvrtx.vk.perf=1 の `perf: window` の行に、満杯は最初の
  8 回を `window full` の印に。ABI・HAL・libvulkan は不変。Q1 の形の案（host-visible の type だけ map）でなく「要る時だけ map」にした理由: nvrtx の
  memory type は 1 つ（DEVICE_LOCAL・HOST_VISIBLE・COHERENT）で、libvulkan は host-visible の type の blob に必ず MAPPABLE を付けるので、flag では分けられない。
- cf48046e5: VRAM の allocator の満杯（`vram full`）と work channel の VA の満杯（`work space full`）の印（各 8 回まで）。
- 確かめ（host）: vmunix（ws291 の config）warning 0、plan/tools/nvrtx/nvrtx-vk-host-test.sh PASS 60 checks（`nvrtx_vk_memory_cpu` の stub を足した）。

### 実機の A/B（2026-10-29 07:04〜07:19、plan/ws291/tests/rtx-window-ab.sh、build/ws291/rtx/ab1、desk-l2）

- eager（全 blob を作る時に map、nvrtx-eager-map.patch、vmunix-eager1 cksum 997937608）と lazy（06cdbf381、vmunix-lazy1 827899503）を nvrtx.vk.perf=1 で。

| | eager | lazy |
| --- | --- | --- |
| window の peak（frame-ops と probe の後） | 245936 / 253504 KiB、`window full` 2 回（16384・8100 KiB の要求） | 106984 / 253504 KiB、満杯 0 |
| terminal | `vkAllocateMemory result=-2 errno=8` | 開く |
| frame-ops | terminal が無く type〜drag は空振り | 全段が動く |

- lazy の desktop（counts 無し、ab1/lazy-nocount-desk と desk-l2）: keys 143〜167、output 65〜69、type 60、home 61〜62、wiseview 61〜64、launch 48〜49
  （terminal の起動の 1 回 78〜88 ms）、**scroll 19〜20、hover 21〜23、drag 32〜35 fps**（RDNA の p001 は全段 60）。
- 次の壁 (1): desktop の後の mview の swapchain が `work space full: 8100 KiB asked, 18688 KiB free in 8 runs`（vmunix-lazy2 = cf48046e5 の build、
  cksum 204755418）。work channel の GPU の VA の users の部分は 0x28100000〜0x40000000（約 383 MiB、`NVRTX_WORK_VA_BYTES` = PD の 1 entry の 512 MiB）。
- 次の壁 (2): 起動の直後（client 無し）の mview は `api=depth format result=-11`（VK_ERROR_FORMAT_NOT_SUPPORTED）。nvrtx の executor に depth の形式が無い。
- keiland-shot は eager・lazy とも撮れた（6222418 byte、shot の ERROR buffer は最初の起動の時だけ、未解析）。
- 手順の注意: `pkill -f rtx-window-ab.sh` は同じ語を含む自分の shell も止める（tools.md の注意と同じ、exit 144）。

### work channel の VA の拡張（01478d325、実機は未実施）

- NVRTX_WORK_VA_BYTES 512 MiB → 3.5 GiB（0x20000000〜0x100000000、first-level の entry 1〜7、resource manager の 4 GiB の range の手前）。
  mmu.c: 512 MiB ごとの double-entry directory（最初の物は今の 0x3000、2 つ目からは page table の後ろ）にし、page table は 1 本の列のまま
  （`nvrtx_mmu_gr_pte_offset` は不変、1 entry の range の image は前と同じ byte）。work.c の arena: tables 0〜0x70a000、instance 0x800000、
  USERD 0x801000、own buffers 0xa00000（arena 32 MiB は不変）。
- 確かめ: vmunix warning 0（build/ws291/vmunix-va1 cksum 1463007375）、plan/tools/nvrtx/nvrtx-sm75-host-test.sh 287 checks 0 failures、
  page table の walk の host の確かめ（scratchpad、7 つの VA で dir0・page table・PTE の位置が一致、PASS）。実機は次の枠。

### 再開点（2026-10-29 07:20 P2）

- 実機: Debian、RTX は vfio-pci、lock は返した。NVMe の zedBSD の kernel は vmunix-lazy2（cf48046e5 の build、cksum 204755418、前の物は vmunix.old）、
  cfg の nvrtx.vk.perf の行は外した。
- 次（Q1 に聞いた）: (1) work channel の VA を複数の 512 MiB の directory に広げる、(2) executor に depth・index の draw・mip（mview が動くまで、新しい
  Phase の案 p010a）、desktop の scroll・hover・drag の 20〜35 fps の調べ。案 A・B（RDNA）は host の harness の runner が tree に無く後回し。

### checkpoint（2026-10-29 08:12 P2、q1670-i01 の返却、Q1 の指示「checkpoint を返して終えて」）

- tip ddf4ff288（この節の commit がその後）、未 commit 無し。
- 実機: 08:10 からユーザーが RTL8127 の移設のため電源を切っている。最後の P2 の状態は Debian（RTX は vfio-pci）、NVMe の zedBSD の kernel は
  build/ws291/vmunix-lazy2（cksum 204755418）、cfg に nvrtx.vk.perf の行は無し。P1 の枠の script が kernel を一時的に替えて戻す予定だった（Q1）。
- 再開: (1) `sh plan/ws291/tests/rtx-va-check.sh build/ws291/rtx/va1 build/ws291/vmunix-va1`（01478d325 の build、cksum 1463007375。build/ は
  worktree p2-ws291m の物。新しい worktree なら `make ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/k build/ws291/k/vmunix` で作り直す）:
  desktop の後の mview が `work space full` を出さず `api=depth format` まで進めば VA の拡張は効いている。(2) [p010a](../phase010a/phase.md) の実装。
  (3) desktop の scroll・hover・drag の 20〜35 fps の調べ。案 A・B（RDNA）は host の harness の runner（plan/ws014/tests の gpu-job.c の build の手順）が
  tree に無く後回し（kernel と host の header の型の衝突で今の tree では compile できない: tid_t・sigset_t）。

### work channel の VA の拡張の実機（2026-10-29 08:49〜08:56 P2、q1670-i02、build/ws291/rtx/va1、worktree p2-ws291n）

- `sh plan/ws291/tests/rtx-va-check.sh build/ws291/rtx/va1 build/ws291/vmunix-va1`（01478d325 の build、cksum 1463007375）: desktop の後の mview 3 回とも
  swapchain を越えて `api=depth format result=-11`（[p010a](../phase010a/phase.md) で足す物）まで進み、kernel の行に `work space full`・`window full` は無い → VA の拡張は効いている。
- desktop（counts 無し）: keys 167、output 67、type 61、home 63、wiseview 62、launch 49、scroll 20、hover 23、drag 36 fps（前の lazy と同じ）。
- 終わり: Debian、RTX は vfio-pci、NVMe の zedBSD の kernel は vmunix-va1（前の物は vmunix.old）。
