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

### 再開点（2026-10-29 06:40 P2）

- 実機: Debian、RTX は vfio-pci、lock は返した。NVMe の zedBSD の kernel は 1746f7c18 の build（cfg は不変、ngg・memory の amdrdna の行は無害）。
- 次: host で BAR1 の window の直し（使用量を数える印 → host-visible だけ map）→ 次の枠で desktop と scene-5000。
