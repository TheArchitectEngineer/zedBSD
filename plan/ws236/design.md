<!-- awesome-plan project=zedbsd record=ws236-design -->

# WS236 設計: VC6（Raspberry Pi 4）の H.264 decode を Vulkan Video で

Parent: [WS236](ws.md)。Phase: [p001](phase001/phase.md)（この文書を書いた Phase）。
版: 第 1 版（2026-10-14、P3）。design-reviewer の review の前。

## 1. 目的と範囲

- Pi 4 の VideoCore VI の VPU の firmware（start4.elf）が持つ H.264 decoder（MMAL の `ril.video_decode`）を、ARM の kernel から VCHIQ 越しに使い、
  libvulkan の Vulkan Video（`VK_KHR_video_queue`・`VK_KHR_video_decode_queue`・`VK_KHR_video_decode_h264`）として出す。
- libmedia は今の `media_vkvideo_ops` の道（i915 と同じ）をそのまま使い、Video Player の H.264 の再生の CPU の負荷を下げる。
- 対象外: HEVC（license が無い、ユーザー 2026-10-14）、encode（今回は外す）、V3D での NV12 の sampling（compositor への zero-copy）、
  HDMI の音（WS207 p004、ただし VCHIQ の核はそれと共用できる形にする）。
- 既定 off: `CONFIG_DRIVER_BCM2711_VCHIQ`・`CONFIG_DRIVER_BCM2711_VIDEO`（既定 n）。off の build は今の vmunix と同じ object を作る
  （`#if` の外の行を変えない）。drop は config を n のままにすればよい。

## 2. 参考と license（コードは写さない、protocol の事実だけを使う）

| 参考 | license | 使う事実 |
| --- | --- | --- |
| FreeBSD `sys/contrib/vchiq/interface/vchiq_arm/`（vchiq_core.h・vchiq_core.c・vchiq_2835_arm.c・vchiq_kmod.c・vchiq_pagelist.h・vchiq_cfg.h） | BSD-3-Clause（GPL-2.0 との dual、BSD を選ぶ） | slot zero の配置、message の id の形、message の種類、service の open、bulk、pagelist、doorbell の register（BELL0 の読みで bit 2、BELL2 への書き）、Pi 4 の long bulk space の address の符号化 |
| FreeBSD `sys/arm/broadcom/bcm2835/bcm2835_mbox.h` | BSD-2-Clause | mailbox の channel 3 = VCHIQ |
| Raspberry Pi `userland`（`interface/mmal/*.h`、`interface/mmal/vc/mmal_vc_msgs.h`、`mmal_vc_client.c`、`mmal_vc_api.c`） | BSD-3-Clause（LICENCE、Broadcom 2012・Raspberry Pi 2015） | MMAL の service（fourcc `mmal`、version 16、min 10）、worker の message の id と構造、port・format・buffer header・event・parameter の構造と定数、component 名 `ril.video_decode`、bulk の順（message の後に bulk） |
| Linux の vchiq・bcm2835-codec・bcm2835-mmal-vchiq | GPL-2.0（vchiq の core は dual だが Linux の tree は見ない） | **使わない**（ユーザー指示 2026-10-14） |

- 参考の取り寄せ: `userland` は GitHub の master の tarball、FreeBSD は `main` の raw の file（2026-10-14 取得、P3 の scratchpad、commit しない）。
- zedBSD の code は Zlib。名前は全て独自（`rpi4_vchiq_*`・`rpi4_mmal_*`）、構造体は message の byte の配置だけを同じにし、配置の注釈は自分で書く。
- 32 bit の pointer: userland の構造体は 32 bit の VPU と 32 bit の ARM の両方で同じ配置になる前提で、pointer の field を持つ。
  64 bit の zedBSD では pointer の field を全て `uint32_t`（不透明な値）にし、host の試験で各 message の大きさと offset を固定値と照らす（§9）。

## 3. VCHIQ（`src/drivers/platform/rpi4/rpi4-vchiq*.c`）

### 3.1 hardware と memory

- DT: `compatible = "brcm,bcm2711-vchiq"`、`reg` は doorbell の block（DT の値 0x7e00b840、ARM 0xfe00b840）、`interrupts` は GIC の SPI。
  doorbell: block の先頭（+0x00）の読みが BELL0（bit 2 が「VPU が鳴らした」、読むと clear）、+0x08 の書きが BELL2（ARM → VPU）。
  （FreeBSD は reg の base を 0x40 下げて +0x40・+0x48 と書く。同じ register。）
- 共有の memory: `drv_dma_device_create_window`（bus の窓 0xc0000000 + 物理、1 GiB まで、uncached）で 1 本の連続の領域を取る:
  slot zero（`sizeof` が 4 KiB に収まらない時は 2 slot）＋ data の slot 64（各 4 KiB、master 32・slave 32）＋ fragment の領域（128 byte × 64）。
  合計 約 272 KiB。fragment は使わない（§3.5）が、VPU が platform_data を読むので offset と数は書く。
- 起動: slot zero を埋め（magic `VCHI`、version 8、version_min 3、slot_zero_size、slot_size 4096、max_slots 128、max_slots_per_side 64、
  master・slave の slot の範囲、platform_data[0] = fragment の bus address、[1] = 数）、ARM（slave）の shared state を初期化して
  `initialised = 1`、mailbox の **channel 3** に slot zero の bus address を書く（FreeBSD と同じ。rpi4-firmware.c に raw の mailbox の書き込み
  `drv_rpi4_firmware_mailbox_write(channel, value)` を 1 つ足し、property の lock で直列にする）。
  代替（D-V1 が駄目な時）: property の tag `VCHIQ_INIT`（0x00048010）で同じ値を渡す。
- 待ち: master（VPU）の `initialised` と CONNECT の message を最大 1 秒待つ。来なければ driver は「VCHIQ なし」で attach を諦める（log 1 行）。
- QEMU（`drv_rpi4_board_emulated`）では attach しない（QEMU の raspi4b に VPU は無い）。

### 3.2 message と slot

- message の header: `msgid`（type << 24 | 送り側の port << 12 | 受け側の port）と `size`、data。8 byte 境界に丸めて slot に詰める。
  slot の残りが足りなければ PADDING で埋めて次の slot へ。
- 送信: 自分（slave）の `tx_pos` を進め、`remote->trigger` を signal（`fired = 1`、`armed` なら BELL2 を書く）。
- 受信: 割り込み（BELL0 の bit 2）で `local` の各 event（trigger・recycle・sync_trigger・sync_release）の `fired && armed` を見て、
  受信の thread（kernel thread 1 本）を起こす。thread は remote の `tx_pos` まで message を読み、type で振り分ける。
- slot の回収: 受け取った message の slot の `release_count` を進め、`use_count` と並んだら remote の `slot_queue[slot_queue_recycle++]` に
  その slot を返して `remote->recycle` を signal。自分の slot は VPU が `local->slot_queue` に返し `local->recycle` を signal する。
  送信は空きの slot が無ければ recycle を待つ（上限 1 秒、超えたら EIO）。
- 同期の message（sync slot）は使わない（MMAL は非同期で足りる）。

### 3.3 service

- `rpi4_vchiq_open(fourcc, version, version_min, callback, &service)`: OPEN（payload: fourcc、client_id 0、version、version_min）を送り、
  OPENACK（payload: version）を最大 1 秒待つ。port は ARM 側で 1 から割り当てる。
- callback は受信の thread から呼ぶ: DATA の message（data と size、使い終わったら `rpi4_vchiq_release(message)` で slot を返す）、
  bulk の完了（TX_DONE・RX_DONE、actual）、service の close。
- `rpi4_vchiq_send(service, data, size)`、`rpi4_vchiq_close(service)`。
- 共用（WS207 p004 の音、service `AUDS`）を考え、service の表は最大 8、callback は service ごと。

### 3.4 bulk

- ARM が slave なので、ARM は BULK_TX（ARM → VPU）・BULK_RX（VPU → ARM）の message（payload: pagelist の bus address、size）を送り、
  VPU が DMA で運んで BULK_TX_DONE・BULK_RX_DONE（payload: actual）を返す。service ごとに bulk は送った順に完了する。
- pagelist: `length`・`type`（0 = WRITE（ARM → VPU）、1 = READ）・`offset`（最初の page の中の位置）・`addrs[]`。
  Pi 4（`brcm,bcm2711-vchiq`、long bulk space）の `addrs` の 1 語は「物理 page 番号 << 8 | 続く page の数 − 1」（1 語で最大 256 page = 1 MiB）。
  pagelist 自体は bus の窓（|0xc0000000）の連続の memory に置く。
- 運ぶ memory: bitstream の staging（§6.4）と、decode の出力の VkImage の memory（§7）。どちらも uncached の連続の領域なので
  cache の保守は要らず、pagelist は数語で済む。

### 3.5 cache

- slot・pagelist・bulk の memory は全て uncached（Normal NC）。cache line の端の扱い（fragment）は要らないので READ の pagelist は
  常に type 1（READ_WITH_FRAGMENTS を使わない）。memory の順序は書いた後に `dsb sy` 相当（`kern_io_write_barrier`）の後で doorbell。

## 4. MMAL の client（`src/drivers/platform/rpi4/rpi4-mmal*.c`）

- service: fourcc `mmal`、version 16、version_min 10。最初に GET_VERSION で firmware の版を log に 1 行。
- message: 全ての message は header（magic `mmal`（MMAL_MAGIC）、msgid、control_service（u32、不透明）、waiter（u32、返事の照合に使う番号）、
  status、dummy）24 byte で始まる。返事は waiter の番号で待ち手に返す（最大 1 秒）。
- decoder の手順:
  1. COMPONENT_CREATE `ril.video_decode` → component handle、input 1・output 1。
  2. PORT_INFO_GET（input・output）→ port handle と既定の format。
  3. input の format: es type VIDEO、encoding `H264`、width・height（SPS の値、16 の倍数）、PORT_INFO_SET。
  4. output の format: encoding `NV12`、PORT_INFO_SET。input・output の buffer の数と大きさは PORT_INFO_GET の推奨（input は 1 本の
     bitstream の上限、§6.4）。
  5. COMPONENT_ENABLE、PORT_ACTION ENABLE（input・output）、output に buffer を BUFFER_FROM_HOST で数本渡す（data は buffer の番号）。
  6. FORMAT_CHANGED の event（EVENT_TO_HOST、cmd `EFCH`）: output の format（width・height・crop・stride の元になる値）を読み、
     必要なら output を disable → PORT_INFO_SET → enable → buffer を渡し直す。
- buffer の流れ（zero-copy にしない、`is_zero_copy` 0）:
  - input: BUFFER_FROM_HOST（length、flags = FRAME_END（＋最初の IDR で KEYFRAME）、pts = decode の通し番号）の後に BULK_TX（staging）。
    length が 128 以下なら message の中（payload_in_message）。input の buffer は decoder が読み終えると BUFFER_TO_HOST で戻る。
  - output: BUFFER_TO_HOST（length、pts、flags）が来たら、その場で BULK_RX を出す。宛先はその pts の decode の出力の image（§7）。
    RX_DONE の後で同じ buffer を BUFFER_FROM_HOST で渡し直す。EOS・長さ 0 の buffer は bulk なしで渡し直す。
- bulk の規則（review-1 S4、userland の BSD-3 の client と同じにする）:

| ID | 規則 |
| --- | --- |
| (a) | 受信の bulk の長さは buffer の length を 4 の倍数に丸める（受け手の memory はその分の余白を持つ） |
| (b) | EOS で長さ 0 の出力の buffer にも 8 byte の受信の bulk を出す |
| (c) | 256 byte を超える EVENT_TO_HOST の data は message の後に bulk で来る（受信の bulk を出して受ける） |
| (d) | `payload_in_message` は OPAQUE の encoding と clock の port だけ。H.264 の input は長さによらず常に bulk（送信の長さも 4 に丸める） |
| (e) | bulk を送った port の flush は `PORT_FLUSH`（buffer の message の形）に 8 byte の dummy の送信の bulk を付け、返事を待つ。出力の port は `PORT_ACTION FLUSH` |
| (f) | 長さ 0 の EOS の input は `BUFFER_FROM_HOST_ZEROLEN` と 8 byte の bulk（この WS では EOS を送らない） |
| (g) | 1 AU が input の buffer_size を超えれば複数の buffer に分け、最後だけ FRAME_END（review-1 M4） |
| (h) | `MMAL_PARAMETER_VIDEO_TIMESTAMP_FIFO`（0x20024）・`MMAL_PARAMETER_VIDEO_INTERPOLATE_TIMESTAMPS`（0x2002f）を明示に false（pts を絵に結び付けたまま出す、review-1 S5） |
| (i) | bulk の宛先・元の memory は RX_DONE・TX_DONE（か abort）まで解放も再利用もしない。出力の `offset + 丸めた length` が宛先を超える枠は bounce に受ける（review-1 S3） |

- flush: PORT_ACTION FLUSH（input・output）で decoder の中の参照を捨てる（Vulkan の RESET、§6.5）。
- 終わり: PORT_ACTION DISABLE、COMPONENT_DISABLE、COMPONENT_DESTROY。

## 5. decode の順と遅れ（stateful の MMAL と stateless の Vulkan）

- 問題: Vulkan Video は 1 回の decode command で 1 枚を decode の順に出し、libmedia は 1 枚ごとに submit して fence を待ってから次を出す
  （libmedia が POC で表示の順に並べ替える）。MMAL の decoder は bitstream から自分で DPB を持ち、表示の順で（並べ替えの遅れの後に）出す。
  遅れがあると、libmedia は k 枚目の fence を待ち、decoder は k+1 枚目の入力を待って、止まる。
- 決め（D-S1）: **SPS を作り直す時に VUI を自分で書き、`bitstream_restriction_flag = 1`、`max_num_reorder_frames = 0`、
  `max_dec_frame_buffering = max(max_num_ref_frames, 1)` を入れる。** H.264 の出力の規則（C.4.5）では、並べ替えの数が 0 の時、
  decode した絵は待たずに出る。decoder は stream の値を信じるので、出力は decode の順になる。参照の扱い（frame_num・MMCO・
  sliding window）は slice の header のままなので、decode の結果は変わらない（出る順だけが変わる）。
- 決め（D-S2）: input の buffer に MMAL の FRAME_END の flag を付ける（1 buffer = 1 access unit）。decoder は次の AU を待たずに
  その AU を decode できる。
- 決め（D-S3）: pts = decode の通し番号。出た buffer の pts で、どの decode command の出力かを照合する。番号が合わない枠は
  捨てる（bulk は scratch へ受けて捨てる）。
- 確かめ（実機、p003 の後の §12 の試験）: B frame を含む 1080p の stream で、出力の pts が単調に増え、1 入力ごとに 1 出力が遅れなく返ること。
- 駄目だった時（R1、§11）の代わり（review-1 S6 と Q1 2026-10-14 の判断で改訂）: **ベータ2 では VIDEO_CAPABILITIES で profile 66（Baseline）だけを出す。**
  Baseline には B frame が無く並べ替えの遅れが 0 なので、decoder は入力ごとにすぐ出す。Main・High の stream は libmedia が
  profile の不一致で software に落ちる。旧 R1-a（executor が pts で戻し libmedia が先に submit）は、libmedia の decode k+1 の submit が
  transfer k の semaphore を待つ鎖と V3D の 1 本の FIFO で deadlock する（review-1 S6）ので採らず、順の外の retire か decode 専用の
  queue を含む再設計としてベータ3 に送る。
- 注（review-1 S6 (i)）: H.264 の Annex C の出力の順は bumping で決まり、`max_num_reorder_frames` を使うかは decoder の実装次第。
  D-S1 は規格が保証する道ではなく、firmware の実装に頼る hack で、実機の §12 の確かめを必須とする。

## 6. Vulkan Video の橋渡し（`src/drivers/gpu/bcm2711/video*.c`）

### 6.1 device の形（D-D1）

- **V3D の Vulkan の device（WS141 の executor）の queue family 0 に `VK_QUEUE_VIDEO_DECODE_BIT_KHR` を足す。** 別の VkPhysicalDevice は作らない。
  - 理由: libmedia は decode の family と transfer の family を同じ device に求め、decode の後に `vkCmdCopyImageToBuffer`（PLANE_0・PLANE_1）で
    host-visible の buffer に読み出す。V3D の executor は memory・buffer・image・command buffer・queue・fence・semaphore・CPU の copy を
    既に持つので、video の command だけ足せばよい。別の device なら、これら全てを作り直すことになる。
  - family を 1 つのままにする（GRAPHICS | TRANSFER | VIDEO_DECODE）。libmedia は decode と transfer に同じ family・同じ queue を使い、
    family をまたぐ semaphore も concurrent の sharing も要らない。Vulkan の規格は 1 つの family に graphics と video decode を持つことを許す。
- capset: `render-runtime.c` の capset を、video が使える時（VCHIQ が繋がり MMAL の service が開けた時）だけ 176 byte にし、
  byte 168 に native の tag（"TANZ"、0x5a4e4154）、172 に bit 0（H.264 decode）。libvulkan は無変更でこれを読み、video の拡張と
  synchronization2 を列挙する。VCHIQ が無い（QEMU、firmware が古い、config が off）時は今と同じ 168 byte。
- device の attach の順: rpi4 の platform の init で VCHIQ を GPU より前に attach し、GPU の attach の時に `rpi4_mmal_available()` を見て
  video を有効にする。

### 6.2 問い合わせ（i915 の video.c と同じ wire、libvulkan の video.c の形）

| opcode | 答え |
| --- | --- |
| VIDEO_CAPABILITIES（0x10000） | profile: H.264 decode、4:2:0、8 bit、PROGRESSIVE、profile_idc 66・77・100。flags: `SEPARATE_REFERENCE_IMAGES` なし、decode の caps は `DPB_AND_OUTPUT_COINCIDE`。minCodedExtent 64×64、maxCodedExtent 1920×1088、pictureAccessGranularity 16×16、maxDpbSlots 17、maxActiveReferencePictures 16、maxLevelIdc 4.2（1080p60）、minBitstreamBufferOffsetAlignment 1、minBitstreamBufferSizeAlignment 16、stdHeaderVersion は H.264 decode の name と spec の版 |
| VIDEO_FORMAT_PROPERTIES（0x10001） | 1 つ: `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`、2D、OPTIMAL、usage `VIDEO_DECODE_DST | VIDEO_DECODE_DPB | TRANSFER_SRC` |
| QUEUE_FAMILY_VIDEO_PROPERTIES（0x10002） | family 0 = DECODE_H264 |
| query の status | 持たない（`queryResultStatusSupport` = 0）。libmedia は status の query を使わずに decode する |

### 6.3 session・parameters・memory

- session（0x10003）: profile と maxCodedExtent・maxDpbSlots・maxActiveReferencePictures・pictureFormat（NV12 だけ）を検べ、
  MMAL の component はまだ作らない（最初の decode の SPS で大きさが分かってから）。
- session の memory の要求（0x10005）: binding を 1 つ（4 KiB、memory type 0）返す。decoder の memory は VPU の側にあり、
  ARM の memory は要らないが、0 個の binding は app の bind の道を狭めるので 1 つにする（中身は使わない）。
- parameters（0x10007〜）: SPS（最大 32）・PPS（最大 256）を i915 と同じ形で手元に写す（scaling list を含む）。update は追加だけ。
- 各 decode の前に、その picture の SPS・PPS の id で値を引き、前回送った値と違えば NAL を作り直して bitstream の前に置く（§6.4）。

### 6.4 decode の command（記録と実行）

- 記録: `vkCmdBeginVideoCodingKHR`・`vkCmdControlVideoCodingKHR`・`vkCmdDecodeVideoKHR`・`vkCmdEndVideoCodingKHR` を
  V3D の command buffer の記録（prepared の event）に足す。i915 と同じく、記録の時に session・slot・resource の規則を検べる。
- 実行（V3D の executor の native job の 1 event、CPU の仕事と同じ並び）:
  1. RESET（control）なら MMAL の flush（§4）と、送った SPS・PPS の記録を消す。
  2. picture の SPS・PPS を引き、必要なら SPS（VUI を §5 の形に書き換え）・PPS の NAL を RBSP から作る
     （exp-Golomb、emulation prevention の 0x03、start code `00 00 01`）。初回と IDR の前と値が変わった時に送る。
  3. app の bitstream の buffer の `[srcBufferOffset, srcBufferOffset + srcBufferRange)`（libmedia は各 slice を `00 00 01` + NAL で詰める）を、
     SPS・PPS の後ろに続けて staging（session ごと、uncached、連続、4 MiB）に写す。4 MiB を超える picture は decode せずに飛ばす（log）。
  4. MMAL の input に 1 buffer（FRAME_END、pts）で送る（§4）。
  5. その pts の出力を最大 500 ms 待つ。来た枠を出力の image（setup slot の image = dst、COINCIDE）へ bulk で受ける。
  6. 失敗（timeout、MMAL の error、format の不一致）は picture を飛ばし、image は変えない（i915 と同じく、bitstream の問題は submit を
     失わせない）。続けて 3 回 timeout なら session を「壊れた」とし、以後の decode は待たずに飛ばす（log 1 行）。
- DPB は形だけ: 参照の slot の image は読まない（decoder は自分の DPB を持つ）。setup slot の image に出力を書くので、
  DPB の image の中身も本物の decode の結果になる。slot の active・inactive の規則は i915 と同じく検べる。
- 待つ間: 実行は V3D の executor の 1 step の中で同期に待つ（V3D の pass と同じ形）。待つ間は controller の mutex を離さない
  （R3、§11）。1080p で 1 枚 約 10〜20 ms の見込み。

### 6.5 seek と RESET

- libmedia は seek の後と最初の decode で `vkCmdControlVideoCodingKHR(RESET)` を記録する。executor は MMAL の input・output を flush して
  decoder の参照を捨て、次の IDR から始める（libmedia は seek の後に IDR から出す）。

## 7. VkImage の memory と NV12 の配置

- V3D の executor の memory は全て「1 GiB の下の連続の uncached の RAM」（`vulkan-memory.c`）。VPU の bulk は Pi 4 の long の address で
  物理 page を直接書けるので、**decode の出力は VPU が image の memory に直に書く（zero copy）。**
- NV12 の image（`vulkan-resource.c` に足す、OPTIMAL・LINEAR とも同じ配置）: pitch = 幅を 32 に丸めた値、plane 0 の行の数 = 高さを 16 に丸めた値、
  plane 1 の offset = pitch × 丸めた高さ、plane 1 の行の数 = 丸めた高さ / 2、大きさ = pitch × 丸めた高さ × 3 / 2（4 KiB に丸める）。
  MMAL の NV12 の出力の枠（output の format の width = 32 の倍数、height = 16 の倍数、plane は続けて）と同じ配置。
  FORMAT_CHANGED の width・height がこの配置と違う時は、1 枚の bounce（staging に受けて行ごとに CPU で写す）に落とす。
- `vkGetImageSubresourceLayout`: NV12 は OPTIMAL でも PLANE_0・PLANE_1 の offset と pitch を答える（i915 の N3 と同じ私的な約束）。
- 読み出し: libmedia の `vkCmdCopyImageToBuffer`（PLANE_0・PLANE_1）を V3D の executor の CPU の copy（`vulkan-buffer-copy.c`）に
  NV12 の plane の扱いとして足す（V3D の tile の copy には回さない）。uncached からの読みなので 1080p で 1 枚 約 3 MiB の読みになる。
- barrier: sync2 を 1.0 に訳した barrier の video の layout（`VIDEO_DECODE_DST`・`VIDEO_DECODE_DPB`）を NV12 の image で受ける。

## 8. libvulkan・libmedia・vkvideo-probe

- libvulkan: 変えない見込み（capset の native の語で video を開く、i915 と同じ道）。p004 で確かめ、違えば p004 の範囲で直す。
- libmedia（p005）: 変えない見込み。確かめる点: decode と transfer が同じ family の時に queue を 1 つにすること、session の memory の
  binding が 1 つで通ること、COINCIDE の usage（`DPB | DST | TRANSFER_SRC`）。
- vkvideo-probe（p005）: 今は Intel の Tile Y を常に de-tile する。`VkPhysicalDeviceProperties.vendorID` が Intel（0x8086）でない時は
  linear として plane を読む形にする（ws083 の試験の hash はそのまま使える）。

## 9. 試験

- host（各 Phase）:
  - VCHIQ: slot zero の初期化の値と `sizeof`、message の詰め（8 byte の丸め、PADDING、slot の跨ぎ）、slot の回収の数え、
    pagelist の符号化（long bulk の語、page の連続の纏め、256 page の区切り）。
  - MMAL: 各 message の大きさと field の offset を固定の表と照らす（参考の userland の header を 32 bit で compile した値を
    p003 で一度だけ測って表にし、試験の中では userland を include しない）。
  - NAL: SPS・PPS の RBSP の書き出しを、既知の stream（ws083 の `plan/ws083/tests/streams`）の SPS・PPS を libmedia の parser で読み、
    書き戻して parser で読み直すと同じ値になること。VUI の bitstream_restriction の値。emulation prevention。
  - build: rpi4 の config で `CONFIG_DRIVER_BCM2711_VCHIQ=y`・`CONFIG_DRIVER_BCM2711_VIDEO=y` の vmunix（warning 0）と、
    既定（n）の rpi4・amd64 の vmunix が変わらないこと（object の比較）。
- 実機（T1、p006）: §10。

## 10. 実機の試験の依頼（T1、Pi 4、p005 の後）

- image: rpi4 の config に `CONFIG_DRIVER_BCM2711_VCHIQ := y`・`CONFIG_DRIVER_BCM2711_VIDEO := y` と `vkvideo-probe` を足した config
  （`plan/ws236/tests/config-rpi4-video.mk`）。1080p の H.264 の mp4（High profile、B frame あり）と、ws083 の stream と hash。
- 流すこと: (1) dmesg の VCHIQ・MMAL の行（firmware の版、service の open）、(2) `vkvideo-probe --list`（VIDEO_DECODE の family、H.264 の拡張）、
  (3) `vkvideo-probe --expect=… stream.h264`（hash の一致）、(4) Video Player で 1080p の mp4 を再生（画面の PNG、`top` の CPU の使用率を
  software の decode と比べる）、(5) seek。
- 合否: (2)(3) が一致、(4) が乱れなく再生され CPU が software より下がる。

## 11. 危険と代わり

| ID | 危険 | 見込み・代わり |
| --- | --- | --- |
| R1 | firmware の decoder が `max_num_reorder_frames = 0` を無視し、表示の順・遅れて出す | p003 の後の起動時の確かめ（§12）で先に実機で確かめる。駄目ならベータ2 は Baseline だけ（§5）、Main・High はベータ3。B frame の無い stream（Baseline、多くの camera の動画）は影響しない |
| R2 | NV12 の出力が使えない（I420 だけ） | output を I420 にし、bulk を staging に受けて CPU で NV12 に並べ替える（1 枚 約 3 MiB の読み書き） |
| R3 | 同期の待ちの間（10〜20 ms）V3D の executor が他の session（compositor）を進めない | 1080p30 では 1 秒に 30 回、合計 30〜60% の時間。compositor の frame が遅れたら、decode の event を「送る step」と「待つ step」に分け、待つ step は 1 ms ごとに他の session に順を譲る（executor の step の仕組みはそのまま使える） |
| R4 | VPU の DMA が image の memory の物理 address（long bulk）を受けない | 出力を 1 GiB の下の staging（bus の窓）に受けて CPU で写す |
| R5 | VCHIQ の初期化の手順（channel 3）が今の firmware で動かない | property の tag `VCHIQ_INIT` に切り替える |
| R6 | 期限（2026-10-16 夕方）に実機の確かめが間に合わない | 既定 off のまま merge し、ベータ3 に送る（ユーザーの条件） |

## 12. R1 を先に確かめる試験（Q1 2026-10-14 の指示）

- p003 の MMAL の client ができた時点で、Vulkan の道（p004・p005）を待たずに実機で R1 を確かめる。
- 形: `CONFIG_DRIVER_BCM2711_VIDEO_PROBE`（既定 n、`CONFIG_BT_TEST_LOOPBACK` などと同じ試験の config）で build する kernel だけに入る
  小さな起動時の確かめ（`src/drivers/platform/rpi4/rpi4-mmal-probe.c`）。ws083 の試験の stream（`pb-high-352-pyramid.h264`、
  High、B の pyramid、352×288）を C の配列にして（`plan/ws236/tests/` の生成の script で作る、stream は ws083 が自分で x264 で作った物）
  起動の後に kernel thread で 2 回 decode する: (a) stream のまま（対照）、(b) SPS の VUI を §5 の形に書き換えて。TIMESTAMP_FIFO・
  INTERPOLATE は false（§4 (h)）。各出力の絵（crop の NV12）の checksum（FNV-1a 32）を、host で ffmpeg が decode した参照の
  decode の順・表示の順の checksum の表（`plan/ws236/tests/` の生成の script が作り probe に埋める）と照らし、各入力ごとに
  「入力の番号・出た絵が decode の順の何番か・入力から出力までの遅れ（入力の数）」を klog に 1 行。終わりに要約の 1 行
  `rpi4-mmal-probe: run=a order=display lag=… / run=b order=decode lag=0` の形（order は decode・display・other）。
  (a) で display と遅れが見えなければ、firmware の既定がもう decode の順で、判定は不能として報告する（review-1 S5）。
- T1 の手順: その config の image（`plan/ws236/tests/config-rpi4-mmal-probe.mk`）で Pi 4 を起動し、ssh で `dmesg | grep rpi4-mmal`。
  合否: (b) が order=decode・lag=0 で、(a) が order=display（対照が効いている）。同じ起動で、1080p の 1 枚の decode の時間、
  long bulk（1 GiB を超える物理 address の受け）、channel 3 の初期化、gpu_mem の不足の有無（MMAL の error）も log に出る。
- 判定: (b) が合えば §5 の D-S1 で p004 を進める。合わなければ Baseline だけを出す（§5）。

## 13. 判断（既定を選んで進める、Q1 経由でユーザーへ）

| ID | 問い | 既定（推し） |
| --- | --- | --- |
| J1 | video の queue は V3D の device に足すか、別の VkPhysicalDevice か | **V3D の device の family 0 に足す**（§6.1）。WS141 の executor の file に `#if CONFIG_DRIVER_BCM2711_VIDEO` の小さな追加が入る |
| J2 | WS141 の外の共有の file の変更: `Makefile`（-D の 2 行）、`platform/arm64/vmunix.mk`（file の一覧）、`config/drivers/architecture/arm64.drivers`（menu の 2 行、既定 n）、`src/kern/platform/rpi4.c`（attach の呼び出し、`#if` の中）、`rpi4-firmware.c`（raw の mailbox の書き込み 1 関数） | 既定 off の配線として必要。全て `#if`・`ifeq` の中か、off で使われない関数 |
| J3 | config の名前 | 既存に合わせて `CONFIG_DRIVER_BCM2711_VCHIQ`・`CONFIG_DRIVER_BCM2711_VIDEO`（依頼の仮の名 `RPI4_*` から変えた） |
| J4 | vkvideo-probe の de-tile を Intel の時だけにする（§8） | 直す（p005） |
| J5 | VUI の書き換え（§5）で decoder の出す順を変えるのは、app の bitstream の意味を変えない範囲の書き換え（出す順の指示だけ）で、規格の外の動きではない | この形で進める |

## 14. review-1（2026-10-14）への対応

[review-1](review-1.md)。Q1 の判断（2026-10-14）: 期限までの最短の道を採る。p003 は最小（同期、EOS なし、NV12、bounce に受ける）で §4 の bulk の規則を守り、
§12 の probe を先に T1 の実機へ。R1 が外れたらベータ2 は Baseline だけ。WS141 の V3D の executor・libmedia（WS202）・vkvideo-probe（WS083）の
変更は #if・既定 off の範囲で WS236 の範囲として許可。libvulkan の変更が要れば止めて Q1 へ。gpu_mem（config.txt）はまだ変えない。

| 指摘 | 対応 |
| --- | --- |
| S1 status query | p004: V3D の executor に RESULT_STATUS_ONLY の query pool（create・destroy・get results・cmd reset・begin・end、decode の成否を書く）。§10 に「libmedia の log で Vulkan Video が選ばれた」を足す。§8 の「libvulkan 無変更」は query pool を V3D で持つ前提で維持 |
| S2 待つ間の mutex | p004: decode の event は「送る step」と「受ける step」に分け、受ける step は render window で mutex を離して MMAL を最大 1 ms 待ち、来ていなければ step を返して他の session に順を譲る。受信の thread は controller の mutex を取らない（lock の順は controller → mmal → vchiq） |
| S3 DMA の寿命 | §4 (i)。p003 の client は bulk の宛先を完了まで持ち、timeout の後に遅れて来た枠は client の bounce に受けて捨てる。p004 は image の memory を decode の記録が完了まで保持する |
| S4 bulk の規則 | §4 の表 (a)〜(g)、p003 の host の試験 |
| S5 R1 の判定 | §12（checksum と対照、TIMESTAMP_FIFO・INTERPOLATE を false） |
| S6 R1-a | §5（Baseline だけ、R1-a はベータ3） |
| M1 | p004: SEPARATE_REFERENCE_IMAGES を立てる |
| M2 | p004: video の有無は VCHIQ の接続と MMAL の service の open で決め、session の open の時点の capset で固定（latch）。QUEUE_FAMILY_VIDEO_PROPERTIES は capset が 176 の session では必ず答える |
| M3 | p004 の範囲に全て入れる（見積もりを +4 LW） |
| M4 | §4 (g) |
| M5 | 変えない（Q1 の判断）。§12 の試験で不足が分かれば Q1 へ |
| M6 | §12 の試験で 3 MiB の NC の copy の時間も測る（probe が bounce から読む時間） |
| M7 | p002 で実装済み（PAUSE・REMOTE_USE への返事、CONNECT を ARM から、bulk の actual -1、方向ごとの上限 4、message と bulk の組を send lock で直列） |
| M8 | p004: RESET の後に出力の buffer を渡し直す、component は session の作成で作り失敗をその場で返す |
| M9 | §10 に「on の時の既存の desktop」と libmedia の道を足す（p005） |
| m1〜m9 | m1 StdVideo の enum、m2 VUI は全体を合成（bitstream_restriction の他の値は制限しない値）、m3 ARM の EABI で測った（下）、m4 reserve_wait、m6 stream を `plan/ws236/tests/` に複写、m7 #if、m8 staging の `max_segment_size` を 4 MiB 以上に、m9 level 4.2 の制限は記録 |

m3: MMAL の message の大きさと offset は、userland の BSD-3 の header を `clang --target=armv7a-linux-gnueabihf` で compile した object の
symbol の大きさで測った（2026-10-14、P3 の scratchpad、commit しない）。buffer 296・event 304・port_info 316・port_info_set 304・port_action 100・
param_set 424・component_create 160・create の返事 44・version 40・header 24、MMAL_PORT_T 64・ES_FORMAT 32・ES の union 44・BUFFER_HEADER 56（pts は 32）。
parameter の id: ZERO_COPY 0x4、VIDEO_TIMESTAMP_FIFO 0x20024、VIDEO_DECODE_ERROR_CONCEALMENT 0x20025、VIDEO_INTERPOLATE_TIMESTAMPS 0x2002f。
