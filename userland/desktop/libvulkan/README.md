# zedBSD libvulkan

Vulkan 1.0 の137 core commandと、Vulkan 1.0に適用される
`VK_KHR_surface`、`VK_KHR_display`、`VK_KHR_swapchain`、
`VK_KHR_display_swapchain` の18 commandを提供する共有ライブラリです。
公開ヘッダは `include/libc/vulkan/`、インストール先は
`/lib/libvulkan.so`、SONAMEは `libvulkan.so` です。

アプリは標準Vulkan APIを使います。zedBSDのGPU ioctl、resource ID、
Venus wireをアプリへ公開しません。`userland/tests/vkdemo/` が標準APIを
使う実例で、別OSのVulkan実装でもビルドできます。

現在のbackendはamd64 zedBSD上のVenusです。QEMUのvirtio-vga-gl、
virglrenderer 1.1.0とVulkan 1.1以上のnative deviceを使い、
公開するcore versionを1.0に制限しています。元のGPU能力から
実装で保持できる能力を選択し、deviceごとに拡張の対応を列挙します。
初期受け入れ環境はLinux Intel i915/ANVです。

## 構成と所有権

- `instance.c` / `device.c` は複数GPU、物理能力、logical deviceとqueueを管理します。
- `objects.c` / `wire.c` / `context.c` は動的handle、allocator callback、
  private protocol IDと返信、共有command streamを管理します。
- API familyごとのCファイルが標準入力を独立したwire表現へ変換します。
  shaderはアプリが渡したSPIR-Vをnative driverへ送ります。
- `memory.c` はGPU共有資源を `mmap` します。公開HOST_VISIBLE typeは
  HOST_COHERENTだけで、CPU copyをcoherent mappingの代用にしません。
- `wsi*.c` はdirect-display surface / swapchainとFIFO順序を実装します。
  GPU完了を待って表示側所有のimageへ転送し、表示中imageをアプリへ返しません。
- Kは普通のGPU session/resource/map/submit/displayインタフェースを使い、
  権限、範囲、fileとmappingの寿命を独立して検査します。

後続core、外部memory/sync拡張、Wayland WSI、EGLは公開しません。
Wayland対応時は `VK_KHR_wayland_surface` のWSI backendを追加します。
EGLは別途GLES-on-Vulkanを扱う時の課題です。

## ビルドと検証

通常のbase package設定で `libvulkan` を選択します。
amd64用vkdemo検証設定ではlibvulkanと動的アプリをまとめてビルドできます。
公開headerはsysrootにも `vulkan/` の階層を保持します。

```sh
make -j16 BUILD="$PWD/build/vkdemo-amd64" \
  ZEDBSD_CONFIG="$PWD/plan/ws014/tests/config-vkdemo-amd64.mk" \
  "$PWD/build/vkdemo-amd64/bin/vkdemo"
```

ELF checkerは155 exports、SONAME、依存ライブラリとinterpreterを照合します。
[全API検証台帳](../../../plan/ws030/phase004/api-verification.md) は
各commandを実装と独立試験に対応させています。
`plan/ws030/tests/` の限定試験は実objects/codec/family、実K/VM/PCI/HALの
対象境界を通常実行とASan/UBSanで確認します。
`plan/ws014/tests/run-vkdemo-remote.py` は指定したprivate QEMUホストで
標準アプリの実画面とGPU readbackを照合します。イメージ転送を伴います。

この実装・内部試験は、Khronos CTS合格や正式な適合認証を意味しません。
全featureの全組合せや全limitの境界を実GPUで実測したとは主張しません。
256MiBのhost-visible apertureは有限で、容量不足を標準allocation errorとして
返します。native返信の有限watchdogもあり、非常に長いcompile等では
contextをdevice-lostとして終了する場合があります。

## Vulkan Video（H.264 decode）と synchronization2（ws083）

`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264` と
`VK_KHR_synchronization2` は、capset の native の語（176 byte の record の
byte 168 の tag と byte 172 の bit 0）で H.264 decode を約束した backend
（native の i915）の時だけ列挙します。Venus（fork・stock）では出さず、
backend の queue family の video の flag と format の video の feature も隠します。
`video.c` は profile と parameter set の key を手元で検べ、session・parameters・
記録を zedBSD 独自の opcode（`uapi/gpu-op.h` の `0x10000`〜`0x1000d`）で送ります。
`sync2.c` は synchronization2 の 6 command を 1.0 の command に翻訳します
（1.0 に無い stage は ALL_COMMANDS、access は MEMORY_READ|MEMORY_WRITE に広げる）。

規格に合わない点（ユーザーの判断 H2・HD6、2026-10-07）:

- N1: device の `apiVersion` は 1.0 のままで、Vulkan 1.1（と synchronization2）を
  前提とする video の拡張を名乗ります。
- N2: `VK_KHR_sampler_ycbcr_conversion` を名乗らずに NV12
  （`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`）と `PLANE_0/1` の aspect を使います。
- N3: OPTIMAL（decoder の Tile Y）の decode の絵に `vkGetImageSubresourceLayout`
  が plane の offset・pitch を答えます。zedBSD の私的な約束で、試験の app が
  de-tile に使います（decode の絵の SAMPLED・TRANSFER_SRC が入れば不要）。
- N4: slice が 256 を超える picture は decode せずに飛ばします（出力は未定義）。

parameter set の key の重複と H.264 の範囲の外の id は `VK_ERROR_INITIALIZATION_FAILED`
で拒みます（固定した 1.3.269 の header では `VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR`
が beta の encode の値のため）。result status query・inline query は持ちません。

## Display の制御と抜き差しの通知（ws113-p003）

instance の `VK_EXT_display_surface_counter` と device の `VK_EXT_display_control`（4 command）を
提供します。device の拡張は、renderer の node が display・topology の事象・display の制御
（`GPU_CAP_DISPLAY`・`GPU_CAP_DISPLAY_EVENTS`・`GPU_CAP_DISPLAY_CONTROL`）を持つ時だけ列挙します
（display が別の display 専用 node にある構成では出しません）。

- `vkRegisterDeviceEventEXT`（DISPLAY_HOTPLUG）: fence は登録時の topology の sequence
  （この renderer に属す display node の `GPU_DISPLAY_EVENTS` の値の和、非破壊の QUERY で ACK しない）を
  cursor に持ち、後の観測で和が cursor を超えたら signal します。thread は持たず、
  `vkGetFenceStatus`・`vkWaitForFences` の観測ごとに kernel に一度だけ待たずに聞きます
  （wait は 1 ms ごとの観測）。fence ごとの cursor なので、ある client の観測が別の fence を消費しません。
  client は signal の後に新しい fence を登録してから display を列挙し直し、古い fence を壊します。
- `vkRegisterDisplayEventEXT`（FIRST_PIXEL_OUT）: 登録時の `GPU_DISPLAY_REFRESH` の数を cursor にし、
  その後の refresh の境界で signal します。scanout していない出力は境界を作りません。generation が
  変わった（抜いて挿した）display は新しい cursor を取り直し、次の実の境界で signal します（挿したこと
  自体では signal しません）。
- signal した event の fence を reset すると、その event は再び signal しません（過去の event を再生しない）。
  未発火の fence の reset は監視を取り消しません。
- `vkDisplayPowerControlEXT`: この device が swapchain を持つ display の lease で `GPU_DISPLAY_POWER` を送ります。
  lease が無い・出力に power の制御が無い（i915 の HDMI など）・切断の時は `VK_ERROR_UNKNOWN`
  （規格がこの command に挙げる失敗は OUT_OF_HOST_MEMORY だけ）。lease の終わりで ON に戻ります。
- surface の counter は持ちません（`supportedSurfaceCounters = 0`）。`vkGetSwapchainCounterEXT` は
  `VK_ERROR_OUT_OF_DATE_KHR`、counter を求める swapchain の作成は `VK_ERROR_INITIALIZATION_FAILED`。
- 同時に出せる出力の数の制限（native の claim の `ENOSPC`）は、swapchain の作成で
  `VK_ERROR_INITIALIZATION_FAILED`（device を失わない、一時的）になります。

host の試験は `plan/ws113/tests/host-display-events.sh`、独立の client は `userland/tests/display-events/`。

## 宣言とprotocolの来歴

[API-PROVENANCE.md](../../../include/libc/vulkan/API-PROVENANCE.md) に
固定した公式宣言の版・hash・ライセンスとNoct再生成手順を記録しています。
実装は独立して記述し、Mesa、loader、virglrendererのC実装を移入していません。
公開APIとprotocol IDの必要なライセンス表示はそれぞれの資料を参照してください。
