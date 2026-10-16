# ws039-p017 VRAM の 256 MiB の窓の固定の解除

- 親: [WS039](../ws.md)、状態: in-progress（2026-10-16 P3: 段 (1) を実機で ok、段 (2) は作業中）
- Queue: q1231 / q1231-i01（2026-10-16 Q1 の割当）。実行者: P3（`agent/p3-ws039`）
- 起点（2026-10-16 ユーザー）:「glassが解決したら、ファン制御をマージして、そのあと窓の固定は解除して自由に使えるように改善しましょう。」（§15 #7「まず256MBで実装します。」の後の段）
- 順: p009 の glass の修正 → p012（SMU の Tier F、fan の制御）の実機と merge → この段。
- 範囲（案、着手の時に design.md 5.5 で詳しく）: GPU だけが触る BO（render target・offscreen・texture 等）を BAR0 の窓の外の VRAM（6 GiB の全体）に置く、firmware が Re-Size BAR で広げた BAR0 は全域を使う、窓が足りない時の置き場の fallback。Terminal などの app が起動しない件（2026-10-16 の観察）が窓の不足なら、ここで解く。
- 実機: 10.0.10.3 で desktop と複数の app（Terminal 等）が起動する。

## 2026-10-16 の調べ（P3、p009 の中）

- app が起動しない理由は 256 MiB の窓: `amdrdnactl status` の cpu_free=25MiB（gpu_free=5862MiB は余る）。Music の起動で `KWL IMPORT … bytes=2961408` の後 `MUSIC FAILED operation=window error=5`（ENOMEM が EIO として app に返る）。
- executor の memory type が 1 つ（DEVICE_LOCAL|HOST_VISIBLE|COHERENT）で、blob・内部の確保が全部 CPU の窓の pool から取られる。
- この段の案: (1) memory type を 2 つに（DEVICE_LOCAL だけ = 窓の外、と窓）、CPU が触らない image・offscreen・swapchain は窓の外へ。node の blob も MAPPABLE が無い時は窓の外。libvulkan の memory.c は host-visible の type の時だけ MAPPABLE を付ける（Q1 が許可、i915・nvrtx・venus の振る舞いが変わらないことを確かめる）。(2) display の frame 2 枚を storage の present が来た時だけ確保。UEFI の Re-Size BAR の設定には頼らない。

## 段の分け方（2026-10-16 P3 の案、Q1 の了承「順は (1) → (2) で可」）

- (1) HAL・PCI に触れない: executor の memory type を 2 つにし、CPU が触らない memory を窓の外へ。これだけで app の起動の不足が解ける見込み。
- (2) OS の中で BAR0 を広げる（ユーザーの「窓の固定の解除」）: A0/A1 で BAR0 を 256 MiB から 8 GiB に、base（0x4000000000、8 GiB 整列）を動かさずに広げ、BAR2（doorbell、BAR0 の直後 0x4010000000）を上へ動かす。上の bridge（root port 00:01.0、switch 01:00.0・02:00.0）の prefetchable の窓を広げる PCI の層の関数が要る。Q1 の許可（2026-10-16）: `src/drivers/pci/`（pci-window.c・pci-pcat-assign.c）の変更は可、条件 (a) 新しい関数は driver が明示に頼んだ時だけ動き、他の device・他の機械の割り当ては変えない（呼ばれなければ今と同じ振る舞い）、(b) 失敗したら 256 MiB のまま続ける、(c) BAR0 の base を動かす必要が出たらその時点で止めて案を Q1 に（console の framebuffer の VA）、(d) merge の前に QEMU の起動（他の device の PCI の割り当てが変わらない確認）を T1 に頼む依頼文を Q1 に。kernel の device の窓の定数（`src/hal/amd64/space.c`、2 GiB）を広げるなら報告。
- HAL の API（`include/hal/hal.h`）の変更は要らない見込み（2026-10-16 P3 の調べ）: user への device の map（`hal_space_map` の `HAL_SPACE_DEVICE`）と kernel の device の窓（`hal_space_map_device`）は既にある。BAR0 の base を動かさなければ boot の framebuffer の固定の VA も古くならない。
- 実機の事実（2026-10-16 の Debian、lspci と dmesg）: BAR0 256 MiB @0x4000000000、BAR2 2 MiB @0x4010000000、bridge 3 つの prefetchable の窓は 0x4000000000〜0x40101fffff、root の bus の窓 `[mem 0x4000000000-0x7fffffffff]`（4 GiB より上に 256 GiB）。

## 段 (1) の実装（2026-10-16 P3）

- `vk/vk-device.c`: memory type 0 = DEVICE_LOCAL だけ（heap 0 = 窓の外の pool、5862 MiB）、type 1 = DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT（heap 1 = 窓）。仕様の順（性質の少ない方を先）。`vk/vk-memory.c`: buffer・image の memoryTypeBits は 0x3、type 0 で import でも export でもない memory は executor が node に頼んで窓の外に自分の storage を持つ（libvulkan は host-visible でない非共有の memory に blob を作らないので、これまでは storage が無かった道）。export される memory（WSI の swapchain など）は blob を待ち、node が blob の MAPPABLE で置き場を決める。
- `node.c`: blob は `GPU_BLOB_MAPPABLE` の時だけ窓の中、それ以外は窓の外。窓の外の blob の resource_read/write は間接の窓（MM_INDEX）で 1 語ずつ（語の境界だけ）、resource_map は EINVAL。
- `space.c`: backing に `cpu`（窓の中か）を持たせ、`amdrdna_backing_create(device, bytes, cpu, …)`。窓の外は copy engine（SDMA）の constant fill で 0 にする（`engines.c` の `amdrdna_engines_clear`、`command-format.c` の `amdrdna_copy_fill`、出典 Linux の sdma_v5_0.c の emit_fill_buffer と fill_max_bytes 4 MiB）。`amdrdna_backing_cpu` は窓の外で NULL。
- `vk-node.c`: `amdrdna_vk_memory_create`（窓の外の memory の storage と address space への map）、`amdrdna_vk_heap_bytes(owner, cpu)`、command stream の blob が窓の外なら EINVAL。
- `vk/vk-execute.c`: 256 byte に揃わない buffer と image の copy（これまで CPU が直に写した）で image が窓の外の時は、窓の中の linear image を経由（buffer → linear image を CPU、linear image → image を quad。逆向きは quad の後に CPU）。buffer も窓の外なら EINVAL。
- `present.c`（cursor の image が窓の外の blob なら行ごとに間接の窓で読む）、`display.c`（`amdrdnactl screen` が窓の外の surface を間接の窓で読む）。
- libvulkan（`userland/desktop/libvulkan/memory.c`）は**変えていない**: 非共有の blob は host-visible の時だけ作られ、共有の blob は host-visible の時だけ MAPPABLE が付く既存の振る舞いで足りた。i915・nvrtx・venus の振る舞いは変わらないので、T1 への libvulkan の試験の依頼は要らない。
- 試験: `amdrdna-vk-host-test.c` に device-local の memory（node が作る、CPU の address が NULL、release で backing が戻る）と memoryTypeBits 3 を足した（174 checks 0 failures）。`vkblend.c` に device-local の image の case と buffer の offset を 4 byte ずらした case（経由の道）を足した。`audit-allow.txt` に一般語 4 つ（left・made・outside・packets）。

## 段 (1) の実機（2026-10-16 18:28〜、10.0.10.3、vmunix の SHA-256 の先頭 402e634ad46e546e、Debian から USB の sda2 の vmunix を差し替え）

- 起動で A9 まで、desktop が出る（`display lease 1 claimed`）。`amdrdnactl status`: 起動の直後 **cpu_free=195MiB**（p009 の時は 25 MiB）、gpu_free=5676MiB。
- `vkblend`（20 case）: `VKBLEND memory types=2 host=1 device=0`、host の 15 case と device の 5 case（読み戻し 240x135・1920x64、premultiplied の blend、offset 4 の upload と readback = 窓の中の linear image の経由の両向き）**全部 ok（差 0）**。
- app: kei の session に `terminal`・`music`・`calendar`・`files`・`notes` を起動 → terminal・music（`MUSIC READY`）・calendar（`CALENDAR READY`）・files が動く（p009 では terminal・music・calendar が `vkAllocateMemory result=-2`・`error=5` で落ちた）。4 つの後 cpu_free=155MiB。capture `build/tmp/amdrdna-hw-p9-7/s14.png`（plane の surface は 0x80136f0000 = VRAM の 311 MiB、窓の外の swapchain を間接の窓で読んだ）。
- **notes は `stencil format failed (-11)`**（VK_ERROR_FORMAT_NOT_SUPPORTED: executor が depth・stencil の format を持たない。窓とは別の executor の制限。残り）。
- host: `amdrdna-host-test.sh`（LINUX・FIRMWARE・dump 付き）PASS 438、vk host 174/0、build warning 0、vmunix check PASS、style-check は新しい指摘無し（試験の printf の条件演算子を除く）。
