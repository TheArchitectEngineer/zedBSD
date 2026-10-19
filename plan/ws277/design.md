<!-- awesome-plan project=zedbsd record=ws277-design -->

# WS277 設計: VisionFive 2 の DC8200 の display driver（zero copy の scanout）

- 版: 1（2026-10-20、P1、ws277-p001、q1341-i01）。コードは書いていない。design-reviewer の前。
- 目的: PowerVR（BXE-4-32）が線形で書いた render の buffer を、CPU の copy 無しで DC8200 が直接 scanout する。WS269 の display の node（gpu0、boot framebuffer への CPU の copy）を、DC8200 を自分で操作する node に置き換える。

## 0. 出典

出典の規則は [Guardrail](../guardrail.md) と WS256 の決定（[design-peripherals.md](../ws256/design-peripherals.md):40、2026-10-19 ユーザー「DC8200/HDMI は手順の参考を許す」）に従う: 値は `Values referenced:`、手順は `Procedure referenced:`、vendor の GPL の code・comment・構造・名前は写さない。

| ID | 出典 | 場所 | 使う物 |
| --- | --- | --- | --- |
| S1 | StarFive の vendor の Linux 6.12（読むだけ） | `~/visionfive-linux/drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_dc_hw.c/.h`・`vs_gem.c`・`vs_plane.c`・`inno_hdmi.c`） | DC8200 の register の offset、初期化と clock の手順、plane の format・modifier、HDMI の PHY の PLL の表 |
| S2 | 同 DTS | `~/visionfive-linux/arch/riscv/boot/dts/starfive/jh7110.dtsi:1569-1645`、`jh7110-clk.dtsi:46-48` | dc8200・hdmi・dssctrl・voutcrg の address・IRQ・clock・reset・PD |
| S3 | mainline Linux（GitHub の torvalds/linux、2026-10-20 に gh api で読んだ） | `drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_hwdb.c` 等）、`arch/riscv/boot/dts/starfive/jh7110.dtsi` | mainline の driver は汎用の `verisilicon,dc`（TH1520 向けに入った）で、chip の id の表に model 0x8200・revision 0x5720/0x5721 がある。**mainline の jh7110.dtsi には dc8200・hdmi の node が無い**（`hdmitx0_pixelclk` の固定 clock と voutcrg の入力だけ）。JH7110 での mainline の動作は未確認 |
| S4 | Mesa（gitlab.freedesktop.org mesa/mesa の main、head 0913fce718fe、2026-10-20） | `src/imagination/csbgen/rogue/pbestate.xml:58-64・412-418`、`src/imagination/vulkan/pvr_image.c:49-100・467-472`、`pvr_formats.c:460-506`、`pvr_wsi.c:82` | PBE の memory の layout の値、線形の stride の単位、WSI の modifier |
| S5 | JH7110 TRM（Preliminary V2、sha256 は WS256 design-peripherals.md:15） | — | memory map・PMU・CRG の事実。**この Phase では PDF の register の章を読み直していない**（WS256 が引いた address だけを使う）。VOUT の CRG・DSS の syscon の bit の意味は p002 で TRM と照らす（未確認） |
| S6 | zedBSD | WS269 [design.md](../ws269/design.md) 版 3.1 §3.5・§6、`src/drivers/gpu/powervr/display.c`・`render.c`・`render.h`、`src/drivers/platform/visionfive2/vf2-crg.h`・`vf2-pmu.h`・`vf2-gpu.c:60`、`include/drivers/gpu/gpu-scanout.h`、`include/uapi/gpu-scanout.h`、`userland/desktop/libvulkan/wsi-display.c`、WS269 [p007](../ws269/phase007/phase.md):51-52、WS256 [p011](../ws256/phase011/phase.md) | 置き換える node の形、lookup、placement、shared の道の条件 |

## 1. 結論（先に）

1. **PowerVR は線形で書ける**（S4、Values referenced）: PBE の `MEMLAYOUT` は `LINEAR=0`・`TWIDDLE_2D=1`・`TWIDDLE_3D=2`・`TILED=3`。Mesa の PowerVR は `VK_IMAGE_TILING_LINEAR`・WSI の legacy scanout・`DRM_FORMAT_MOD_LINEAR` の image を `LINEAR` で作り、color attachment として PBE で書く。WSI の modifier は LINEAR だけを出す。線形の stride は PBE の `linestride` が **2 pixel 単位**（feature `pbe_stride_align_1pixel` の無い core）なので、幅を 2 pixel に揃える。BXE-4-32 がこの feature を持つかは未確認なので 2 pixel を前提にする。
   - よって「VF2 の Linux が遅いのは PowerVR が tile しか書けないから」ではない（推測: vendor の閉じた DDK・GNOME の道が copy を選んでいた。vendor の user 側は読んでいないので未確認）。
2. **DC8200 は線形を読める**（S1 `vs_dc_hw.c:412-432`、Values referenced）: plane の modifier の先頭が `DRM_FORMAT_MOD_LINEAR`。ほかは Vivante の tile・DEC の形で、PowerVR の twiddled・tiled は無い。→ zero copy の唯一の形は **線形の ARGB8888/XRGB8888 系**（BGRA・RGBA の byte 順、§3.2）。
3. **address は 32 bit**: plane の address の register は 32 bit で上位の register が無い（S1 `vs_dc_hw.h:332-333・393-394`、`vs_dc_hw.c:1606-1607`）。vendor は DMA の mask を 40 bit にしている（`vs_drv.c:230`）が、address の書き込みは 32 bit に切られる（矛盾、上位を書く道は未確認）。→ buffer は 4 GiB 未満の物理に置く。WS269 の render の buffer は既に 1 つの連続の run・`VF2_GPU_DMA_LIMIT = 0xffffffff`（`vf2-gpu.c:60`、`render.c:13`）なので合う。
4. **MMU は使わない**: vendor の defconfig に `CONFIG_VERISILICON_MMU` が無く、import は連続の sg だけを受ける（S1 `vs_gem.c:475-481`）。zedBSD でも DC8200 の MMU を使わず、物理の連続を要求する（render の buffer は連続なので合う）。
5. **最小の道**: U-Boot が立ち上げた mode・clock・HDMI をそのまま引き継ぎ、**primary plane の address と stride だけを書き換える**（p002）。mode の設定（clock・PLL・HDMI の PHY・EDID）は後の Phase（p004・p005）に分ける。p002 だけで「CPU の copy 無しの scanout」は成り立つ（U-Boot の mode の範囲で）。

## 2. DC8200 と HDMI のハードウェア（S1・S2、Values referenced）

### 2.1 address・IRQ・電源

| 部品 | 物理 | 大きさ | IRQ | 備考 |
| --- | --- | --- | --- | --- |
| DC8200 の AHB の窓（id・IRQ の ack・enable） | 0x29400000 | 0x100 | 95 | `DC_HW_REVISION` 0x24、`DC_HW_CHIP_CID` 0x30、`AQ_INTR_ACKNOWLEDGE` 0x10（読むと ack）、`AQ_INTR_ENBL` 0x14 |
| DC8200 の display の register | 0x29400800 | 0x2000 | — | 下の offset は 0x29400000 から（`DC_REG_BASE` 0x800 を含む値） |
| DC8200 の MMU（使わない） | 0x17030000 | 0x1000 | — | §1.4 |
| HDMI TX（Innosilicon） | 0x29590000 | 0x4000 | 99 | PHY と DDC を含む |
| DSS の syscon（dssctrl） | 0x295B0000 | 0x90 | — | vendor は 0x4 の bit 20、0x8 の bit 3 を立てる（`vs_dc.c:636・640・702・704`）。bit の意味は未確認（TRM で p002） |
| VOUT の CRG（voutcrg） | 0x295C0000 | 0x10000 | — | PD は `JH7110_PD_VOUT`（`jh7110.dtsi:1644`） |

- revision（S1 `vs_dc_hw.c:1321-1340`、S3 `vs_hwdb.c`）: 0x5720 か 0x5721（CID 0x30B・0x310）。VF2 の実際の値は未確認（p002 の最初に読んで記録する）。
- PD_VOUT は既存の `vf2-pmu` が触らない領域（`vf2-pmu.h:11-12`、WS256 design-peripherals.md:230「VOUT の clock と PD_VOUT を止めない」）。p002〜p003 は PD・clock・reset に触らない。

### 2.2 clock・reset（S2 `jh7110.dtsi:1576-1600`、S1 `vs_dc.c:660-663・697-760`、Procedure referenced）

- clock: syscrg の `NOC_BUS_DISP_AXI`・`VOUT_SRC`・`VOUT_TOP_AXI`・`VOUT_TOP_AHB`、voutcrg の `DC8200_PIX0/PIX1/AXI/CORE/AHB`・`DOM_VOUT_TOP_LCD`、外の `hdmitx0_pixelclk`（DT の固定 297 MHz、`jh7110-clk.dtsi:46-48`、実体は HDMI の PHY の PLL の出力と推測）。HDMI は voutcrg の `HDMI_TX_SYS/MCLK/BCLK` と `hdmitx0_pixelclk`。
- reset: syscrg の `VOUT_TOP_SRC`・`NOC_BUS_DISP_AXI`、voutcrg の `DC8200_AXI/AHB/CORE`、`HDMI_TX_HDMI`。
- HDMI の出力（display 0）の時の pixel clock の道: `DC8200_PIX0` の親を `hdmitx0_pixelclk` にする（`vs_dc.c:752-754`）。U-Boot の後は vendor が一度 rate と親を戻してから設定し直す（`vs_dc.c:657-663` の注記: U-Boot が同じ値を残すので clock の枠組みが変更を飛ばす事への対処）。zedBSD の `vf2-crg` には枠組みの cache が無いので、この回避は要らない見込み（p004 で確かめる）。
- **zedBSD の不足**: `vf2-crg` は syscrg・stgcrg・aoncrg を扱い、**voutcrg は未対応**（`vf2-crg.h` の API は node と名前で引くが voutcrg の provider が無い）。`hdmitx0_pixelclk` は外の固定 clock の扱い。p004 で voutcrg を `vf2-crg` に足す（WS256 の source、その WS の範囲外なので Q1 と調整、§5 D3）。

### 2.3 timing・出力・pixel clock の範囲

- display の timing の register: `DC_DISPLAY_H` 0x1430、`H_SYNC` 0x1438、`V` 0x1440、`V_SYNC` 0x1448、`PANEL_CONFIG` 0x1418、`CURRENT_LOCATION` 0x1450（S1 `vs_dc_hw.h:136-145`）。出力の種類は HDMI で `OUT_DP`（`vs_dc.c:756-760`、DPI は DSI 用）。
- HDMI の PHY は pre-PLL・post-PLL の表で pixel clock を作る。**表にある pixel clock だけを受け、297 MHz を超える物は断る**（`inno_hdmi.c:54-` の表、`mode_valid` `:615-634`）。表の値（25.175・27・…・148.5・297 MHz 等）を Values referenced で使う。1080p60（148.5 MHz）・4K30（297 MHz）は入る。
- DDC: HDMI の block の中の I2C（`DDC_BUS_FREQ_L/H` を tmds/4 / SCL で設定、EDID の割り込み、`inno_hdmi.c:321-332・594-601`）。EDID の読み方は p005。

### 2.4 plane と page flip

- primary plane の register: `DC_FRAMEBUFFER_ADDRESS` 0x1400、`STRIDE` 0x1408、`CONFIG` 0x1518、`CONFIG_EX` 0x1CC0、`SIZE` 0x1810、`TOP_LEFT` 0x24D8・`BOTTOM_RIGHT` 0x24E0、`BG_COLOR` 0x1528、`WATER_MARK` 0x1CE8（S1 `vs_dc_hw.h:28-55`）。overlay・cursor は使わない（後の候補）。
- 制約: stride の揃えは vendor の dumb の確保で 32 byte（`vs_dc_hw.c:1103・1124・1145` の `pitch_alignment`）。これが HW の必須か確保の都合かは未確認なので、**32 byte の倍数を要求**する。PBE の 2 pixel（8 byte）より強いので 32 byte を出す。先頭の address の揃えは未確認（page に揃った run の先頭 + offset 0 だけを受ける、offset の揃えを 32 にする）。
- format: ARGB8888・XRGB8888・ABGR8888・XBGR8888 は primary の format の表にある（`vs_dc_hw.c` の primary の formats）。zedBSD の `GPU_PIXEL_BGRA8888`・`RGBA8888`（byte 順）に対応させる（対応の取り方は p002 の実装で決め、実機の目視で赤と青を確かめる）。
- page flip: register は shadow で、vendor は書き込みの間 shadow を外し（`CONFIG_EX` の bit 12 を落とす）、plane の register を書き、bit 12 を戻す（`vs_dc.c:1343-1360`、`vs_dc_hw.c:1519-1535`、Procedure referenced）。新しい address が vblank で効くと推測（latch の時点は未確認、p003 で現在の行 `CURRENT_LOCATION` と IRQ で確かめる）。
- vblank: IRQ 95 で `AQ_INTR_ACKNOWLEDGE` を読んで ack、enable は `AQ_INTR_ENBL` の display ごとの bit（`vs_dc_hw.c:1501-1512`）。underflow は `FRAMEBUFFER_CONFIG` の bit 5（`:1514-1517`）。

## 3. memory と coherency

### 3.1 PowerVR が書く buffer の条件（S4・S6）

| 条件 | 値 | 根拠 |
| --- | --- | --- |
| layout | PBE `MEMLAYOUT_LINEAR` | S4 pbestate.xml:58-64 |
| stride | 32 byte の倍数（DC8200）、かつ 2 pixel の倍数（PBE） | §2.4、S4 pbestate.xml:412-415・pvr_image.c:467-472 |
| 物理 | 1 つの連続の run、上限 0xffffffff | §1.3・§1.4、`render.c:13`、`vf2-gpu.c:60` |
| format | 32 bpp の BGRA・RGBA | §2.4 |

- 線形の render の target を PBE で書く事（render pass の PBE の state の memlayout を LINEAR にする事）は WS269 p010（graphics）の範囲。WS277 はそれを前提にし、p010 の後で zero copy を実機で確かめる（§4 の依存）。

### 3.2 coherency（GPU の書き込み → DC8200 の読み）

- GPU は CPU の cache を snoop しない。render の buffer は host が uncached の別名でしか触らない（`render.c:14-18`）ので、**CPU の L2（ccache）に dirty な line が残らない前提**が既にある。確保の時の零埋めがどの写像かは実装で確かめる（direct map で埋めたなら FLUSH64 を一度、WS269 の既存の扱いのまま）。
- GPU の SLC: render.c は「processor が読む allocation は SLC を迂回する」（`render.c:18`）。**scanout する buffer も同じ「SLC を迂回」の属性で写す**（DC8200 は SLC を通らず DRAM を読むと推測。SLC が GPU の中の cache で DC8200 から見えないのは推測、未確認）。こうすれば render の完了（fence）の時点で DRAM にあり、flip の前に cache の操作は要らない。
- DC8200 自身の読みに cache は無い（scanout の DMA）。DC8200 が L2 を snoop するかは WS256 p011 の未確認の点（design-peripherals.md:215-221）だが、この道では CPU は buffer を cacheable に書かないので関係しない。
- 順序: flip（address の書き込み）は render の完了の後に行う。WS269 の present は BLOB の present で fence を待ってから呼ばれる（libvulkan の worker が完了を証明する、`wsi-display.c:1459`）。driver は present の中で GPU の完了を再確認しない（WS269 の display.c と同じ契約）。

## 4. zedBSD の driver の形

### 4.1 置き場所と node

- WS269 §3.5 の判断（display の node を powervr と同じ driver に置き、render の buffer を private な lookup で retain）を保つ。**`src/drivers/gpu/powervr/display.c` の中身を、DC8200 を操作する形に置き換える**（file を分けるなら `src/drivers/gpu/powervr/dc8200.c`・`dc8200-hdmi.c` を足し、display.c は node の ops と lease を持つ。名前は実装で決める）。gpu0 = display、gpu1 = render の順は変えない。
- DC8200 の register の窓は DT の node（vendor の DTB には dc8200・hdmi の node があるが、zedBSD が使う DTB に無い場合がある。mainline の DTS には無い、S3）。WS269 D2 (a) と同じく `visionfive2.c` の組み込みの記述（address・IRQ）を既定にし、DT の node があれば優先する（§5 D2）。
- 引き継ぎの判定: 起動時に `DC_HW_REVISION` を読み、primary plane の `ADDRESS` が boot framebuffer の物理と一致し、display が有効なら「U-Boot の mode を引き継げる」とする。一致しなければ今の boot framebuffer の CPU の copy の道（WS269 p007 の実装）に落ちる（fallback を残す）。

### 4.2 `drv_gpu_display_ops` と scanout の ops の変化

| op | 今（WS269 p007） | WS277 |
| --- | --- | --- |
| query・mode | loader の extent、60 Hz | p002・p003: U-Boot の timing の register から extent と refresh を読む（pixel clock は `hdmitx0_pixelclk` の rate か HDMI の PLL の設定から、読めなければ 60 Hz）。p005 以降: EDID の mode の一覧 |
| claim・release | console を止める・戻す | 同じ。release で primary plane の address を boot framebuffer に戻し、console が描き直す |
| present（COPY） | CPU の copy を boot framebuffer へ | 残す（自分の resource の copy、互換） |
| present（BLOB） | import の uncached の view から CPU の copy | **primary plane の address・stride を import の物理（`backing->pages[0]` + offset）に書き換える**（shadow の手順）。copy しない |
| wait | 即時の完了 | vblank の IRQ で、flip が効いた時（新しい address の frame の開始）に完了。前の buffer の read hold はその時に外す |
| constraints | `COPY|SHARED|FOREIGN`、placement 0 | `COPY|SHARED`（FOREIGN は lookup の外を断るので外すか、p002 で lookup できる物だけと明記）。`stride_alignment = 32`、`offset_alignment = 32`、`placement = GPU_PLACEMENT_CONTIGUOUS|GPU_PLACEMENT_DMA32`、`max_dma_address = 0xffffffff` |
| import_image | lookup して uncached の view を保持 | lookup で retain（同じ）。加えて、backing の page が連続で 0xffffffff 以下か、stride が 32 の倍数かを確かめる。物理は backing の page の配列から取る（`drv_gpu_scanout_backing.pages`、`gpu-scanout.h:29-35`）。**HAL の API の変更は要らない**。render.h の private な API も今のままで足りる見込み（物理の連続を backing で確かめるので） |

- 二重の buffer の保持: flip を出した buffer は次の flip が vblank で効くまで retain し続ける（`resource_destroy` が来ても実体の release は flip の後）。tearing を避けるため、flip の後に古い buffer を libvulkan に返すのは wait の完了の時。

### 4.3 libvulkan の shared の道（WS269 p007:52）

- libvulkan が shared（zero copy）の道を選ぶのは `context->copy_display == VK_FALSE` の時だけで、それは renderer の capset（`context.c:176-211`、WINQ の XML 版と vendor の flag `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT`）で決まる。constraints の `GPU_SCANOUT_SHARED` は試してよい許可でしかない（`wsi-display.c:1286-1287`）。
- よって WS277 の zero copy には、**PowerVR の native の renderer がこの capset（少なくとも `HOST_SCANOUT` の相当）を出し、export する scanout の image を `GPU_IMAGE_LINEAR` で PBE の LINEAR で書く**事が要る。これは WS269 p010（render の backend）と WS270（Keiland の VF2）の範囲で、WS277 は条件を示して依頼する（§5 D4）。libvulkan の選択の条件を WS277 で変えない。
- 線形の stride の要求（32 byte）を libvulkan が守るか: `display_placement` は constraints の placement・max_dma_address を渡し、alignment は 0（`wsi-display.c:1940-1953・1981`）。stride の揃えは export の image の layout で決まるので、PowerVR の image の layout（WS269 p010）が 32 byte に揃える必要がある（1920×4・1280×4 などの普通の幅は自然に揃う。幅が 8 の倍数でない時は揃えて確保する）。

## 5. ユーザーの判断（D1…）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 最初の到達点 | (a) U-Boot の mode を引き継ぎ、plane の address だけを書き換える（p002・p003）を先に実機で確かめ、mode の設定（p004・p005）は後 / (b) 最初から clock・PLL・HDMI・EDID まで全部を作る | (a)（小さく、zero copy の効果をすぐ測れる。WS270 §2.3 の段 2 の fps の測定に足りる） |
| D2 | DC8200・HDMI の発見 | (a) `visionfive2.c` の組み込みの address・IRQ（DT の node があれば優先） / (b) DT の node が必須 | (a)（WS269 D2 と同じ。mainline の DTS に node が無い、S3） |
| D3 | voutcrg の clock・reset の provider | (a) WS256 の `vf2-crg` に voutcrg を足す（WS256 の source、Q1 の調整で WS277 の Phase として行う） / (b) DC8200 の driver の中で voutcrg を直に叩く | (a)（provider を 1 つに保つ）。p004 の時点で決めれば足り、p002・p003 は要らない |
| D4 | libvulkan の shared の道の条件（capset） | (a) PowerVR の renderer が capset を出す（WS269 p010・WS270 に依頼） / (b) libvulkan の選択の条件を変える（別の WS） | (a)（libvulkan を変えずに済む） |
| D5 | HDMI の audio | (a) 範囲外（Future Work） / (b) WS277 に含める | (a) |
| D6 | 解像度の範囲 | (a) PLL の表にある mode（≤297 MHz）だけ / (b) 表に無い mode の PLL の計算 | (a)（vendor と同じ。4K60 は出ない） |

## 6. Phase の分割（案）

LW は 1 LW ≈ 実時間 15〜25 分、±50%。

| Phase | 中身 | 依存 | LW | 確認の点 |
| --- | --- | --- | --- | --- |
| p002 | 引き継ぎの道: DC8200 の窓の写像、revision・CID の読み、U-Boot の timing の読み取り（query・mode）、引き継げない時は今の copy の道に落ちる fallback。present（BLOB）で primary plane の address・stride を shadow の手順で書き換える。constraints の placement。release で boot framebuffer に戻す | WS269 p007（実装済み）、WS256 p011 の実機の画 | 8〜12 | host 試験（register の書き込みの順の偽の窓）。実機（T1・5320 ではなく VF2）: render の node で線形の BLOB を手で書き（CPU の uncached の view で色の帯）、import → present で帯が出る、release で console に戻るのをユーザーが目視 |
| p003 | vblank の IRQ（PLIC の 95）と wait、flip の完了の時点、前の buffer の retain の解放、underflow の記録 | p002 | 6〜10 | 実機: 2 つの buffer を交互に flip して tearing が無い事、wait が 60 Hz 前後で返る事（時刻の差を画面に出して目視） |
| p004 | 自前の mode の設定の土台: voutcrg の provider（D3）、PD_VOUT・clock・reset の順（vendor の enable の順、Procedure referenced）、DSS の syscon の bit、timing の register、HDMI の PHY の PLL の表（Values referenced）、TMDS の出力 | p003、D3 | 12〜20 | 実機: U-Boot が 1080p の時に 720p（74.25 MHz）へ切り替えて画が出る |
| p005 | DDC・EDID、mode の一覧、hotplug（HDMI の IRQ 99）は後の候補 | p004 | 8〜12 | 実機: EDID の mode の一覧が query に出て、選んだ mode で画が出る |
| p006 | zero copy の通しの確かめ: WS269 p010 の PBE の LINEAR の render の target と capset（D4）を使い、Keiland の desktop の fps を測る（ws.md 完了の条件 2、WS270 §2.3 の段 2） | p003、WS269 p010、WS270 の該当 Phase | 4〜8 | 実機: fps の測定（copy の道との比較）。ユーザーが目視 |
| p007 | 規約の全文の見直し | 全部 | 2〜3 | build（warning 0）、host 試験 |

- 実機の確認は VF2 で、T1 に依頼する（QEMU には DC8200 が無い。QEMU で通るのは host 試験と fallback の道だけ）。
- p002・p003 は p004・p005 と独立に価値がある（D1 (a)）。

## 7. HAL

- HAL の API の変更は要らない（物理は scanout の backing の page の配列から、DC8200 の窓は既存の device の写像の道で）。`proposed/` は置かない。

## 8. 未確認の一覧

- VF2 の実際の DC8200 の revision・CID。
- plane の address の上位 bit の有無（vendor の 40 bit の DMA の mask との矛盾）。
- stride の 32 byte・先頭の address の揃えが HW の必須か。
- shadow の register が vblank で latch される時点。
- GPU の SLC の迂回で DC8200 が render の結果を確実に読めるか（SLC が DC8200 から見えない事）。
- BXE-4-32 の `pbe_stride_align_1pixel` の有無。
- DSS の syscon の 0x4 bit 20・0x8 bit 3 の意味（TRM）。
- U-Boot の後の HDMI の PLL の設定から pixel clock を読み戻せるか。
- mainline の `verisilicon,dc` driver が JH7110 で動くか（DTS に node が無い）。
- vendor の Linux の GNOME が遅かった原因（copy の道を選んだ層）。
