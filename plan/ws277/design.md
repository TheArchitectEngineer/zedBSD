<!-- awesome-plan project=zedbsd record=ws277-design -->

# WS277 設計: VisionFive 2 の DC8200 の display driver（zero copy の scanout）

- 版: 3.1（2026-10-20、P1、ws277-p001、q1347-i01）。[レビュー 3](design-review-3.md)（差分の確認、Blocker 無し）の M1〜M3・Minor 1〜10 に対応（末尾 §14 の表）。Q1 の判断: M2 は serial-console に汎用で追加だけの口を足す事を許す。Q1 の差分の確認待ち。
- 版 3（2026-10-20、P1、ws277-p001、q1346-i01）。[レビュー 2](design-review-2.md) の新 B1・新 M1〜M6・Minor 1〜11・D10・D11 に対応（末尾 §13 の表）。Q1 の判断（`plan/queue.md` の q1346 の行）を反映: 新 B1 は display.c の中の保持の数、U0 の go/no-go は小さい Phase、R4 は VF2 の platform の wrapper の ops、M1 は推奨 (a)（D10）、M6 は p005 の前の追補。コードは書いていない。design-reviewer の差分の確認待ち。
- 版 2（q1343-i01、[レビュー 1](design-review-1.md) への対応）と版 1（q1341-i01）は git の履歴。
- 目的: PowerVR（BXE-4-32）が線形で書いた render の buffer を、CPU の copy 無しで DC8200 が直接 scanout する。WS269 の display の node（gpu0、boot framebuffer への CPU の copy）を、DC8200 を自分で操作する node に置き換える。
- 版 3 での Phase の番号: go/no-go の小さい Phase を p002 として足したので、版 2 の p002〜p007 は p003〜p008 になった（§7）。

## 0. 出典

出典の規則は [Guardrail](../guardrail.md) と WS256 の決定（[design-peripherals.md](../ws256/design-peripherals.md):40、2026-10-19 ユーザー「DC8200/HDMI は手順の参考を許す」）に従う: 値は `Values referenced:`、手順は `Procedure referenced:`、vendor の GPL の code・comment・構造・名前は写さない。

| ID | 出典 | 場所 | 使う物 |
| --- | --- | --- | --- |
| S1 | StarFive の vendor の Linux 6.12（読むだけ） | `~/visionfive-linux/drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_dc_hw.c/.h`・`vs_gem.c`・`vs_plane.c`・`inno_hdmi.c`） | DC8200 の register の offset、初期化と clock の手順、plane の format・modifier、shadow の止め方、HDMI の PHY の PLL の表 |
| S2 | 同 DTS | `~/visionfive-linux/arch/riscv/boot/dts/starfive/jh7110.dtsi:1569-1645`、`jh7110-clk.dtsi:46-48` | dc8200・hdmi・dssctrl・voutcrg の address・IRQ・clock・reset・PD |
| S3 | mainline Linux（GitHub の torvalds/linux、2026-10-20 に gh api で読んだ） | `drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_hwdb.c`・`vs_primary_plane.c`・`vs_primary_plane_regs.h`・`vs_crtc_regs.h`）、`arch/riscv/boot/dts/starfive/jh7110.dtsi` | 汎用の `verisilicon,dc`（TH1520 向け）、chip の id の表（model 0x8200・revision 0x5720/0x5721）、`VSDC_FB_*` の名前と atomic_update の手順（Procedure referenced）。**mainline の jh7110.dtsi に dc8200・hdmi の node は無い**（voutcrg は PD_VOUT 付き、約 1202 行。`hdmitx0_pixelclk` は固定 clock）。CID 0x310 の扱いは vendor（0x5721）と mainline（0x5720）で違う。JH7110 での mainline の動作は未確認 |
| S4 | Mesa（mesa/mesa の main、head 0913fce718fe、2026-10-20） | `src/imagination/csbgen/rogue/pbestate.xml:58-64・412-418`、`src/imagination/vulkan/pvr_image.c:49-100・467-472`、`pvr_formats.c:460-506`、`pvr_wsi.c:82`、`src/imagination/common/device_info/bxe-4-32.h` | PBE の memory の layout、線形の stride の単位、WSI の modifier、BXE-4-32 の feature。Mesa の blob 検索は拒まれた（401）ので、それに頼る点は未確認 |
| S5 | JH7110 TRM（Preliminary V2、sha256 は WS256 design-peripherals.md:15） | — | memory map・PMU・CRG。**この Phase では register の章を読み直していない**。VOUT の CRG・DSS の syscon の bit の意味は p005 で TRM と照らす（未確認） |
| S6 | zedBSD | WS269 [design.md](../ws269/design.md) 版 3.1 §3.5・§6、`src/drivers/gpu/powervr/display.c`・`render.c`・`render.h`、`src/drivers/platform/visionfive2/vf2-crg.h`・`vf2-pmu.h`・`vf2-gpu.c:60`、`src/kern/platform/visionfive2.c`、`src/drivers/generic/serial-console.c`、`include/drivers/gpu/gpu-scanout.h`・`gpu-display.h`（UNGATED・adoption_wait）、`include/uapi/gpu-scanout.h`・`gpu-display.h`、`include/kern/text-display.h`（`reveal_fatal`）、`src/drivers/gpu/gpu.c`、`userland/desktop/libvulkan/context.c`・`wsi-display.c`・`wsi-swapchain.c`・`wsi-image.c`・`memory.c`、WS269 [p007](../ws269/phase007/phase.md):51-52、WS256 [p011](../ws256/phase011/phase.md) | 置き換える node の形、lookup、placement、shared の道の条件、errno の対応 |

## 1. 結論（先に）

1. **PowerVR は線形で書ける**（S4、Values referenced）: PBE の `MEMLAYOUT` は `LINEAR=0`・`TWIDDLE_2D=1`・`TWIDDLE_3D=2`・`TILED=3`。Mesa の PowerVR は `VK_IMAGE_TILING_LINEAR`・WSI の legacy scanout・`DRM_FORMAT_MOD_LINEAR` の image を `LINEAR` で書く。線形の stride は PBE の `linestride` が **2 pixel 単位**（BXE-4-32 に `has_pbe_stride_align_1pixel` が無いので確定）。
   - 「VF2 の Linux が遅いのは PowerVR が tile しか書けないから」ではない（推測: vendor の閉じた DDK・GNOME の道が copy を選んでいた。未確認）。
2. **DC8200 は線形を読める**（S1 `vs_dc_hw.c:412-432`）。PowerVR の twiddled・tiled は無い。→ zero copy の唯一の形は **線形の 32 bpp・alpha を使わない X 系**（§2.5）。
3. **address は 32 bit**（S1 `vs_dc_hw.h:332-333・393-394`、`vs_dc_hw.c:1606-1607`。vendor の DMA の mask 40 bit との矛盾は未確認）→ 4 GiB 未満の物理。WS269 の render の buffer は 1 つの連続の run・上限 `0xffffffff`（`render.c:13`、`vf2-gpu.c:60`）。
4. **MMU は使わない**（S1 `vs_gem.c:475-481`）。物理の連続を import の時に検査する。
5. **最小の道**: U-Boot が立ち上げた mode を引き継ぎ、primary plane の register を shadow を止めて書き、commit し、**vblank の IRQ で latch を知ってから present を返す**（p003、D10 (a)）。その前に go/no-go の小さい Phase（p002: attach・判定・記録だけ）で引き継げるかを実機で決める。mode の設定は p005・p006。
6. **present は latch の後に返る**: libvulkan は present の成功の直後に前の front の hold を外すので、kernel は新しい buffer が DC8200 に採られた後にだけ成功を返す（§4.3）。
7. **buffer の寿命は display.c の中の保持の数で守る**（新 B1）: display の resource に front・pending の保持の数を持たせ、`resource_destroy` は保持中なら解放を遅らせ、最後に外す時に thread の文脈で `powervr_buffer_release` する。他 WS の変更（R8）は要らない（§4.5）。
8. **GPU の SLC**: buffer の属性として SLC を迂回し、その buffer の全部の写像を同じ属性にする（R3）。display は迂回の印の無い buffer を copy の道に回す（§3.2）。
9. **範囲外の読み**: zero copy は frame の幅・高さ・stride が引き継いだ plane と一致する時だけ（§4.2、新 M3 の守りも兼ねる）。
10. **HAL の変更は要らない**。panic の画面は VF2 の platform の wrapper の text の ops の `reveal_fatal` で plane を戻すだけで、描き直しは呼ばない（§4.6）。

## 2. DC8200 と HDMI のハードウェア（S1・S2・S3、Values referenced）

### 2.1 address・IRQ・電源

| 部品 | 物理 | 大きさ | IRQ | 備考 |
| --- | --- | --- | --- | --- |
| DC8200 の AHB の窓（id・IRQ） | 0x29400000 | 0x100 | 95 | `DC_HW_REVISION` 0x24、`DC_HW_CHIP_CID` 0x30、model 0x20（0x8200）、`AQ_INTR_ACKNOWLEDGE` 0x10（**読むと clear**）、`AQ_INTR_ENBL` 0x14 |
| DC8200 の display の register | 0x29400800 | 0x2000 | — | 下の offset は 0x29400000 から |
| DC8200 の MMU（使わない） | 0x17030000 | 0x1000 | — | §1.4 |
| HDMI TX（Innosilicon） | 0x29590000 | 0x4000 | 99 | PHY と DDC |
| DSS の syscon（dssctrl） | 0x295B0000 | 0x90 | — | vendor は 0x4 の bit 20、0x8 の bit 3 を立てる（`vs_dc.c:636・640・702・704`）。意味は未確認（p005 で TRM） |
| VOUT の CRG（voutcrg） | 0x295C0000 | 0x10000 | — | PD は `JH7110_PD_VOUT` |

- revision 0x5720 か 0x5721（CID 0x30B・0x310）。VF2 の値は p002 で読んで記録する。
- p002〜p004 は PD・clock・reset に触らない（WS256 design-peripherals.md:230「VOUT の clock と PD_VOUT を止めない」と合う）。

### 2.2 clock・reset・PD（p005、S2 `jh7110.dtsi:1576-1600`、S1 `vs_dc.c:660-663・697-760`、Procedure referenced）

- clock: syscrg の `NOC_BUS_DISP_AXI`・`VOUT_SRC`・`VOUT_TOP_AXI`・`VOUT_TOP_AHB`、voutcrg の `DC8200_PIX0/PIX1/AXI/CORE/AHB`・`DOM_VOUT_TOP_LCD`・`HDMI_TX_SYS/MCLK/BCLK`、外の `hdmitx0_pixelclk`（DT の 297 MHz は名目の値。実体は HDMI の PHY の PLL の出力と推測）。
- reset: syscrg の `VOUT_TOP_SRC`・`NOC_BUS_DISP_AXI`、voutcrg の `DC8200_AXI/AHB/CORE`、`HDMI_TX_HDMI`。
- HDMI（display 0）の pixel clock: `DC8200_PIX0` の親を `hdmitx0_pixelclk`（`vs_dc.c:752-754`）。
- **zedBSD の不足**: `vf2-crg` に voutcrg の provider が無い。`vf2-pmu` は `DRV_VF2_PD_GPU` 以外を EPERM にする（`vf2-pmu.h:41`）。依頼 R1・R2（§9）。

### 2.3 timing・出力・pixel clock の範囲

- timing の register: `DC_DISPLAY_H` 0x1430、`H_SYNC` 0x1438、`V` 0x1440、`V_SYNC` 0x1448、`PANEL_CONFIG` 0x1418、`CURRENT_LOCATION` 0x1450（S1 `vs_dc_hw.h:136-145`、panel ごとに +4n）。`CURRENT_LOCATION` は vendor・mainline とも定義だけで使っておらず、bit の並びは未確認（設計はこれに頼らない。p002 で読んで記録するだけ）。
- HDMI の PHY は pre-PLL・post-PLL の表の pixel clock だけを受け、297 MHz を超える物は断る（`inno_hdmi.c:54-`・`:615-634`、D6）。
- DDC・EDID は p006（`inno_hdmi.c:321-332・594-601`）。

### 2.4 plane・panel の特定

- primary plane は 2 つ（plane n の register は base + 4n、S1 `vs_dc_hw.c:1290-1309`、S3 `VSDC_FB_ADDRESS(n)` = 0x1400 + 4n）。panel も 2 つ。plane が出る panel は `CONFIG_EX` の bit 19（DISPLAY_ID）。
- 使う register（plane n）: `ADDRESS` 0x1400、`STRIDE` 0x1408、`CONFIG` 0x1518（tile・rotation・scale、**swizzle は bit 23-24 の 2 bit、format は bit 26-31 の 6 bit**）、`CONFIG_EX` 0x1CC0（FB_EN bit 13、DISPLAY_ID bit 19、DEC bit 1、COMMIT bit 12）、`SIZE` 0x1810（width | height<<15）、`TOP_LEFT` 0x24D8・`BOTTOM_RIGHT` 0x24E0、`BG_COLOR` 0x1528、**`BLEND_CONFIG` 0x2510**（vendor `vs_dc_hw.h:74`、mainline `VSDC_FB_BLEND_CONFIG`）、`WATER_MARK` 0x1CE8、panel n の **`PANEL_CONFIG_EX` 0x2518**（bit 0 で shadow の更新を止める、§2.6）。
- **引き継ぎの判定**（attach の時、p002 で作る）: model 0x20 が 0x8200、revision・CID を記録。両方の plane の `CONFIG_EX`・`CONFIG` を読み、FB_EN が立ち、DISPLAY_ID が HDMI の panel（推測で 0、vendor `vs_dc.c:747-755`。p002 の実機で確かめる）の plane を選ぶ。条件は全部: `ADDRESS == bootfb の物理`、`STRIDE == bootfb の pitch`、`SIZE == W×H`、線形・無回転・無拡縮・DEC 無し。1 つでも外れれば zero copy を出さず、今の copy の道（WS269 p007）だけ。
- 判定の時に plane の register 全部（ADDRESS・STRIDE・CONFIG・CONFIG_EX・SIZE・TOP_LEFT・BOTTOM_RIGHT・BG_COLOR・BLEND_CONFIG）を **snapshot** に取る。戻し先は U-Boot の mode の間だけ有効（§4.7、新 M6）。

### 2.5 format・alpha

- zero copy の plane は **X 系**（XRGB8888・XBGR8888）で出し、alpha を無視する。mainline と同じく blend を無効にする（`VSDC_FB_BLEND_CONFIG_BLEND_DISABLE`）。
- frame の byte 順（`GPU_PIXEL_BGRA8888` / `RGBA8888`）から `CONFIG` の format（bit 26-31）と swizzle（bit 23-24）を決めて書く。U-Boot の format と同じなら書き換えない。赤と青の対応は実機の項目 U2。

### 2.6 commit・shadow・latch・vblank（新 M1・新 M2・新 M3、D10・D11）

- **書く手順（常に、新 M3）**（S1 `vs_dc_hw.c:1519-1547`、`vs_dc.c:1352・1359`、Procedure referenced）: (1) panel の `PANEL_CONFIG_EX` の bit 0 で shadow の更新を止め、`FRAMEBUFFER_CONFIG_EX` の COMMIT（bit 12）を **clear** する（vendor と同じ。bit 12 が level の bit でも edge が出る、レビュー 3 Minor 1） → (2) plane の ADDRESS・STRIDE・CONFIG・SIZE・TOP_LEFT・BOTTOM_RIGHT・BLEND_CONFIG を書く → (3) `CONFIG_EX` の COMMIT（bit 12、S3 mainline atomic_update）を set → (4) bit 0 を戻す。害は無いので「要らなければ書かない」の分岐は置かない。bit 0 の向き（止めるのが 1 か 0 か）は vendor から Values referenced で取る。mainline は同じ bit 0 を `VSDC_DISP_PANEL_CONFIG_EX_COMMIT` と名付け set するだけ（S3 `vs_bridge_regs.h:51-52`、`vs_bridge.c:172・237`）で、vendor の「set で止める」と意味が食い違う（§11、p002 の読み戻しで記録）。
- **書く区間の排他（レビュー 3 Minor 5）**: 手順 (1)〜(4) と s0 の取得は、IRQ を禁じた短い spinlock（display の `commit_lock`）の中で行い、区間の中で fatal の印（§4.6）を見直す。印が立っていれば書かずに ENXIO。panic の戻し（§4.6）は同じ spinlock を上限付き（数千回の試み）で取りに行き、取れなくても書く。
- **二重の守り**: p003 の zero copy は **stride が snapshot の STRIDE と等しい時だけ**（SIZE も等しい、§4.2）。shadow の止め方が効かなくても、ADDRESS だけが先に採られた frame は新しい buffer の範囲の中を読む（stride・高さが同じで、import で `stride × height` ≤ 大きさを検査済み）。
- **latch の知り方（D10、推奨 (a) を採る）**: vblank の IRQ 95（`AQ_INTR_ENBL` の display ごとの bit、`vs_dc_hw.c:1501-1512`）を p003 に繰り上げる。polling は使わない（1 kHz の tick と busy の `kern_usleep_range` では 0.67 ms の帰線を観測できない、`include/hal/arch.h:100`、`clock.h:196-201`）。
  - **前提（レビュー 3 M1）**: vblank の IRQ は latch の点と同じか、その後に出る。これが外れる（IRQ が帰線の始め・latch が帰線の終わりで、handler の遅れが帰線を超える）と、s0 + 2 の IRQ が latch の前に来て、前の front を解放した後に DC が読む。前提は p003 の IRQ handler が `CURRENT_LOCATION`（bit の並びは p002 で決める）の行の位置を記録し、実機の U11 で確かめる。**前提が外れたら s0 + 3 にする**。
  - **s0 の取り方**: s0 は手順 (4)（shadow の更新を戻した後）の直後に、同じ spinlock の中で読んだ sequence とする。(1) の前に取ると書く間に来た vblank を数えてしまう。
  - 判定: **s0 + 2 の vblank を観測した時に latch 済み**とする。commit が帰線の中・latch の点の後に書かれた場合は s0 + 1 の vblank では採られておらず次で採られる。s0 + 1 の IRQ が commit の直前に発火の途中だった競合も s0 + 2 で吸収する。
  - **代価（throughput）**: libvulkan は PRESENT の ioctl の間 global の `display_mutex` を持つので present は直列（`wsi-display.c:1455-1489`）。前の present が IRQ k+2 で返り直後に次の commit を書くと s0 = k+2、latch は k+3、返りは k+4 になる。定常では 2 frame に 1 回で、**最大 refresh/2（60 Hz で 30 fps）**。今の copy の道（即時に返る）より遅くなり得る。p004 で 60 fps に戻す（§7 の p004 の範囲）。p007 の比較ではこの制限の有無を記録する。
  - COMMIT bit の自己 clear と `CURRENT_LOCATION` は p002 で読み戻して記録する。p003 の判定には使わない。p004 で、`CURRENT_LOCATION`（commit を書いた時の行の位置）か COMMIT の自己 clear で **commit の後の最初の vsync を判定し**、s0 + 1 相当で返して 60 fps に戻す（p004 の範囲。U11 の結果と U6 で確かめる）。
  - IRQ の handler は ack（`AQ_INTR_ACKNOWLEDGE` を読むのは handler だけ）・sequence と時刻（p003 から `CURRENT_LOCATION` の行も）の記録・待ち手の起床だけを行う。保持の数は触らない（§4.5）。
  - **数え方（レビュー 3 Minor 7）**: `AQ_INTR_ACKNOWLEDGE` は両方の panel の共有（vendor は全 panel を vblank として扱う、`vs_dc.c:1335-1338`）。sequence は **HDMI の panel（§2.4 で特定した panel）の bit が立った時だけ** 進める。IRQ は **attach の時に有効にし常に有効**とする（claim の時からにすると、有効にする前に取った s0 が意味を持たない）。attach で有効にした後、最初の vblank を観測するまで zero copy を出さない。
- **上限と出力の喪失（D11、新 M2）**: 上限は時計の待ちでなく vblank の進みで判定する。
  - commit の後 vblank の sequence が 100 ms（60 Hz の 6 frame、refresh が測れたら 6 frame）進まない: DC が止まった・HDMI が抜けたと見なす。register を前の front へ書き戻して commit し（上の手順）、**ENXIO**（libvulkan で `VK_ERROR_SURFACE_LOST_KHR`、`wsi-display.c:1634-1635`）を返す。新しい buffer の保持は外さず「未確定」に残す（DC が後で読むかもしれない。次の確かな latch か release・close の latch で外し、そこでも latch が来なければ leak を選ぶ、§4.5）。
  - vblank が進んでいる限り s0 + 2 は必ず来るので、latch の待ちの失敗の道はこの上限だけ。ほかに present が ENXIO を返すのは fatal の印（§4.6）が立っている時だけ（レビュー 3 Minor 8）。
  - EIO・ETIMEDOUT は使わない（libvulkan で DEVICE_LOST、`wsi-display.c:1637-1639`）。
  - ENXIO の後は display を「出力喪失」の状態にし、書き戻しの latch（s0' + 2）を確かめるまで次の present も ENXIO を返す。新しい buffer が後から採られる恐れは、libvulkan が swapchain を作り直すまでの間に限られる（崩れは出得るが、memory は保持の数で守る）。
  - **回復の制限（レビュー 3 Minor 3）**: Keiland は SURFACE_LOST で `output_lost` にし（`compose.c:4064-4068`）、回復を hotplug の fence に頼る。window mode でない時や `register_device_event` が無い時は回復しない（`output-switch.c:93-95・148`）。DC8200 の node は hotplug を通知しない（Future Work）ので、**ENXIO の後 desktop は止まったままになり得る**。WS277 はこれを許し、hotplug（IRQ 99）は Future Work の候補に残す。
  - HDMI を抜いても DC は timing の発生源なので vblank は続くと推測する（PHY の PLL を止める操作は zedBSD に無い）。その場合 ENXIO は起きない（U6 の合格はこの両方を許す）。
- underflow は `FRAMEBUFFER_CONFIG` の bit 5（`:1514-1517`）を記録（p004）。
- refresh: DT の 297 MHz は使わない。p003 から vblank の間隔の実測（数 frame の平均）、測る前は 60 Hz。

## 3. memory と coherency

### 3.1 PowerVR が書く buffer の条件（S4・S6）

| 条件 | 値 | 根拠 |
| --- | --- | --- |
| layout | PBE `MEMLAYOUT_LINEAR` | S4 pbestate.xml:58-64 |
| stride | 32 byte の倍数（vendor の pitch_alignment、HW の必須かは未確認なので強い側）かつ 2 pixel の倍数（PBE）、≤ 65535（vendor の stride が u16、`vs_dc_hw.h:399`。register の幅は未確認）、**かつ snapshot の STRIDE と等しい**（§2.6） | §2.4 |
| 先頭 | import の offset は 0 だけ。`offset_alignment` は出さない | — |
| 物理 | 1 つの連続の run、末尾 ≤ 0xffffffff | §1.3・§1.4 |
| 大きさ | `stride × plane の高さ` ≤ backing の大きさ | §4.2 |
| format | 32 bpp の BGRA・RGBA（X として出す） | §2.5 |
| SLC | buffer が SLC 迂回の属性を持つ（R3 の照会） | §3.2 |

### 3.2 coherency（新 M4）

- CPU: render の buffer は確保の時に uncached の別名で零埋め済み（`render.c:322-333`）で、host は uncached の別名でしか触らないので、CPU の L2 に dirty な line は残らない。
- GPU の SLC（レビュー 3 Minor 4 で main の現状に合わせた）: e4d7edbd7 で `powervr_gpu_buffer_flags(mappable, shareable)` が入り（`gpu-mmu.c:524-535`、`render.c:1043`）、写像は mappable **か** shareable なら SLC_BYPASS。SHAREABLE の blob と storage の resource（mappable=1、`render.c:629-630`）は作った側も迂回している。`render_import` は import の側を mappable=1 で写すので迂回。
- 残る穴は **mappable でも shareable でもない blob** だけで（作った側が SLC 経由）、`render_export`（`render.c:843-`）はそれの image の export を拒まない。
- 選ぶ道（D8 (a) を直した形、R3）:
  1. **R3 を小さくした形（推奨）**: `render_export` が image の export を「mappable でも shareable でもない blob」に限って拒む（EINVAL）。これで export された buffer は全部の写像が迂回になり、**照会の口は要らない**。
  2. 代わり（1 が受け入れられない時）: `render.h` に「この buffer は SLC を迂回しているか」を返す照会の口を足し、display の import は迂回の無い buffer を copy の道に回す。
  3. R3 が main に入るまでの p003 の扱い: zero copy は既定で off とし、内部の印 `dc8200_zero_copy_test`（boot parameter `dc8200.zerocopy=test`、display.c の static な int）が立つ時だけ有効にする。立てた時に zero copy に使うのは import の resource の全部で、**試験の program が mappable な blob（作った側も迂回）だけを present する事を試験の手順で守る**（display からは照会の口なしに判別できないので、判別を試験の側の責任にする）。R3 (1) が入ったら印を外し、常に有効にする。
- D8 (a) の費用: SHAREABLE・CROSS_DEVICE の全部の buffer が SLC を使わない事は、e4d7edbd7 で **既に main で払われている**。WS277 が新たに足す費用は無い。測定は p007 の記録だけ。
- 代わりの (b) flip の前の `SLCFLUSHINVAL` は、display から render の FW の command の道へ依存が入るので採らない（(a) が出来ないと分かったら R3 の代替として WS269 と調整）。
- DC8200 自身は cache を持たず、CPU は buffer を cacheable に書かないので L2 の snoop の有無（WS256 p011 の未確認）は関係しない。
- 順序: flip は render の完了の後（libvulkan の worker が fence を待ってから present、`wsi-display.c:1459`）。

## 4. zedBSD の driver の形

### 4.1 置き場所・node・発見

- WS269 §3.5 の判断（display の node を powervr と同じ driver に置き、render の buffer を private な lookup で retain）を保つ。`src/drivers/gpu/powervr/display.c` を DC8200 を操作する形にし、register の操作は `dc8200.c`（p005 で `dc8200-hdmi.c`）に分ける（名前は実装で決める）。gpu0 = display、gpu1 = render。
- 発見（D2）: `src/kern/platform/visionfive2.c` の組み込みの記述（address・IRQ）を既定にし、DT の node があれば優先する。依頼 R4。
- 引き継ぎの判定は §2.4。外れたら copy の道だけ。

### 4.2 `drv_gpu_display_ops` と scanout の ops

| op | 今（WS269 p007） | WS277 |
| --- | --- | --- |
| query・mode | loader の extent、60 Hz | p002: 引き継いだ plane の SIZE と panel の timing から extent。p003: vblank の実測。p006: EDID（§4.4） |
| claim・release | console を止める・戻す | claim は同じ。release は §4.5 の順 |
| present（COPY） | CPU の copy を boot framebuffer へ | 残す。zero copy の front が出ている時は、**先に bootfb へ copy し、それから** plane を snapshot（bootfb）へ戻す commit と latch の待ち（§2.6）を行う（レビュー 3 Minor 6: zero copy の間 bootfb は scanout されていないので、先に書いても見えない。逆の順では古い bootfb が 1〜2 frame 見える）。latch の後に前の front の `holds--` |
| present（BLOB） | import の uncached の view から CPU の copy | **frame の幅・高さ・stride が snapshot の SIZE・STRIDE と一致し、import の検査に受かり、SLC 迂回の buffer の時だけ** zero copy（§2.6 の手順 → s0 + 2 の latch で返る）。それ以外（小さい frame など）は今の CPU の copy の道。**その時 zero copy の front が出ていれば、COPY と同じく先に bootfb へ copy してから plane を戻す commit と latch の待ち**を行う（Minor 3、レビュー 3 Minor 6） |
| wait | 即時 | latch の後の sequence を返す（present が既に latch を待つので、記録の参照） |
| constraints | `COPY|SHARED|FOREIGN`、placement 0 | `COPY|SHARED|FOREIGN`（render は gpu1 で core は foreign と扱う、`src/drivers/gpu/gpu.c:5008`）。**`stride_alignment` は今の値のまま**で、R6 が main に入った後に 32 にする（Minor 4: 32 を先に出すと libvulkan が 32 の倍数でない image を import の前に拒み、readback の道に落ちて今より遅くなる、`wsi-display.c:1290-1293`、`wsi-swapchain.c:1104-1120`）。32 の検査は import の側で行い、外れたら copy の道。placement は R5 が main に入るまで 0、入った後に `GPU_PLACEMENT_CONTIGUOUS|GPU_PLACEMENT_DMA32`・`max_dma_address = 0xffffffff` |
| import_image | lookup して uncached の view を保持 | lookup で retain。加えて検査: backing の page が連続、末尾 ≤ 0xffffffff、offset 0、stride が 32 の倍数かつ ≤ 65535、`stride × image.height` ≤ 大きさ、SLC 迂回（R3 の照会、§3.2）。外れた import は zero copy に使わない印を付け、copy の道で出す（EINVAL にしない） |

- 範囲の検査: DC が読む範囲は `STRIDE × SIZE の高さ`。zero copy は幅・高さ・stride が snapshot と同じ時だけなので、import の `stride × image.height` の検査がそのまま DC の読む範囲を覆う。SIZE を frame に合わせて縮める道（D9 (b)）は Future Work の候補（§11）。
- placement を 0 にしても PowerVR の render の buffer は全部が連続・≤ 0xffffffff なので import の検査で受かる。R5 は将来の別の確保の道のための整合で、p003 の必須ではない。

### 4.3 present の契約（Minor 6・Minor 11）

- libvulkan は PRESENT の ioctl の成功の直後に前の front の `holds--` を行い（`wsi-display.c:1475-1483`）、acquire は holds だけを見る（`wsi-swapchain.c:3795-3803`）。
- よって **present（BLOB）は、新しい buffer が DC8200 に latch された後（s0 + 2、§2.6）にだけ成功を返す**。
- lock（Minor 6）: display の mutex は全部の session の `resource_create`・`resource_destroy`・`import_image`・close の数え上げにも使われる（`display.c`）ので、**mutex を持ったまま待たない**。p003 の present は mutex の中で「flip 中」の状態（pending の resource・s0）を作り、register を書き、mutex を外して vblank の起床を待つ。「flip 中」の間の次の present は flip の完了を待つ（同時に 1 つ）。release・close も「flip 中」なら完了（または ENXIO の確定）を待ってから §4.5 の順に入る。
- p004 で `DRV_GPU_DISPLAY_UNGATED` と `adoption_wait` の形（bcm2711、`gpu-display.h:60-75`）に移す: present は commit を記録して返り、core が lock 無しで adoption_wait を呼び、IRQ の latch で起きる。libvulkan の見る成功は adoption_wait の後なので契約は同じ。UNGATED では core の pin は callback の間だけなので、寿命は §4.5 の保持の数が守る。
  - **UNGATED と pending（レビュー 3 Minor 9）**: UNGATED の present は refresh の境を待たずに記録する（`gpu-display.h:60-75`）ので、「flip 中は次の present が完了を待つ」とぶつかる。p004 の設計で次のどちらかに決める: (i) 待つ形を保つ（adoption_wait の前の present を UNGATED でも直列にする）、(ii) pending を置き換える（まだ commit を書いていない pending だけ。置き換えられた resource の `holds--` はその場で行い、保持の数は front・pending・置き換えの途中の最大 3）。
- 性能（Minor 11、レビュー 3 M1）: libvulkan は PRESENT の ioctl の間 global の `display_mutex` を持つ（`wsi-display.c:1455-1489`）ので present は直列で、p003 の形では **最大 refresh/2**（§2.6）。acquire の `image_available` も最大 1〜2 frame 待つ。p004 で 60 fps に戻し、p007 の測定項目にする。

### 4.4 mode の選び方

- UAPI の mode の操作は ENUMERATE と VALIDATE だけで、設定の op が無い（`uapi/gpu-display.h:39-40・139-157`）。p005・p006 の mode の切り替えの経路は **未定義で、p005 の前の追補で決める**（D7。§4.7 の戻し先と一緒に）。候補は (i) claim の時に VALIDATE 済みの mode を渡す、(ii) present の frame の大きさで選ぶ、(iii) boot parameter。i915 の扱いは未確認。
- p005 の受け入れは「boot parameter で 720p を選んで画が出る」とし、UAPI の経路は D7 の結果で p006 に入れる。

### 4.5 buffer の寿命: display の resource の保持の数（新 B1、Q1 の判断）

- 今: display の resource は import の時に lookup の retain を 1 つだけ持ち（`display.c` import_image `:939`）、`resource_destroy` がすぐ外す（`:468`）。core は close の前に session の resource を全部 destroy してから `end_lease` を呼ぶ（`:349`）。`render.h` に lookup の外の retain は無い（`render.h:28-50`）。swapchain は同じ 2〜3 個の image を何度も present する。
- 形（display.c の中だけ。他 WS の変更・R8 は要らない）:
  - display の resource に **保持の数 `holds`**（front か pending として DC が読む・読むかもしれない数。0〜2）と **`destroyed` の印** を持たせる。lookup の retain は resource が持ったまま、移さない。
  - flip の開始（pending にする）で `holds++`。latch（s0 + 2）でその resource が front になり、**前の front の `holds--`**。同じ resource を続けて present した時も同じ規則（pending と front の 2 つ分を数える）。
  - `resource_destroy`: `holds > 0` なら `destroyed` を立てるだけで、retain と uncached の view は外さない（resource の構造体は display の front・pending の参照から生きる。session の一覧からは外す）。`holds == 0` なら今どおりすぐ解放。
  - **最後に外す時**（`holds` が 0 になり `destroyed` が立っている）に `powervr_buffer_release` と view の解放を行う。これは必ず thread の文脈（present・release・close の thread、p004 では adoption_wait の thread）。IRQ の handler は sequence の記録と起床だけで、`holds` に触らない（`buffer_free` は uncached の別名を外して `kern_free` するので IRQ で呼べない）。
  - 同じ resource は、retain を持って生きている限り何度でも present できる（二度目の present の問題が起きない）。
  - ENXIO（§2.6）の時: pending の `holds` は外さず「未確定」に残し、次の確かな latch か release で外す。
- release・close の順: (0)「flip 中」なら完了を待つ → (1) snapshot の plane の register を全部戻す（§2.6 の手順、戻し先が有効な時だけ、§4.7） → (2) commit → (3) s0 + 2 の latch を待つ（上限は §2.6） → (4) front・pending の `holds` を外し、`destroyed` の物を解放 → (5) console の resume。**描き直しは呼ばない**（Minor 2: VF2 の text の ops は serial で resume は何もしない。HAL の `riscv64_cons_redraw` は hal.h に無く、呼ぶと HAL の API の変更になる）。(3) が上限を超えたら `holds` を外さず残して記録する（leak を選ぶ）。
- `end_lease` が resource の destroy の後に来ても、保持中の resource は display が持つので解放の順は守られる。
- destroy を遅らせた resource の `owner`（session）は `display_close` の `kern_free(session)`（`display.c:350`）の後に無効になる。遅らせた resource の最後の解放は `owner` を参照しない（レビュー 3 Minor 8）。

### 4.6 panic の画面（Minor 1・Minor 2、Q1 の判断の R4、レビュー 3 M2）

- VF2 の text の ops は今、汎用の `src/drivers/generic/serial-console.c`（riscv-virt も使う、`src/kern/platform/riscv-virt.c`）で `reveal_fatal` が無い。panic の文字は HAL の fbcons が `hal_putc` から bootfb に描くが、zero copy 中の DC8200 は render の buffer を出している。
- 解き方: **`src/kern/platform/visionfive2.c` に wrapper の text の ops を置く**。serial-console の ops を包み、`reveal_fatal` だけを足す。
  - `serial_console_ops` は static で（`serial-console.c:59-70`）、`kern_text_*` は登録中の ops へ渡すだけ（`text-display.c` の static な `ops()`）なので、今の公開の口では wrapper から serial の op を呼べない（レビュー 3 M2）。
  - そこで **serial-console に汎用で追加だけの口を足す**（Q1 の判断、2026-10-20）: 例えば `serial-console.h` に「serial-console の text の ops の表を返す関数」（と、登録をせずに初期化だけする版、名前は実装で決める）を足す。既存の関数・ops・riscv-virt の動作は変えない。層の向きは汎用の側への追加なので保たれる（VF2 の platform が汎用の serial と DC の driver を結ぶ）。
  - VF2 の platform は、その表の各 op を呼ぶ wrapper の ops を `kern_text_register` に登録する。
  - DC8200 の driver が attach の時に「致命の時の戻し」の関数を VF2 の platform の中の小さい登録口に登録する。wrapper の `reveal_fatal` はそれを呼ぶ。
  - 戻しの関数: 割り込み禁止で、§2.6 の `commit_lock` を上限付きで取りに行き（取れなくても書く）、snapshot の plane の register を §2.6 の手順で書き、COMMIT を立てる。latch は待たない。待つなら数 ms の上限付きで **`CURRENT_LOCATION` の行の位置が一度 wrap する（帰線を越える）事**を polling する（`AQ_INTR_ACKNOWLEDGE` は読むと clear し handler 専用なので読まない、レビュー 3 Minor 8）。**描き直しは呼ばない**（文字は `hal_putc` が bootfb に描き、bootfb は HAL が持ち続けるので、それより前の分も bootfb に残っている）。
  - **fatal の印**: 戻しの関数は最初に display の「fatal」の印を立てる（atomic）。present・release の側は `commit_lock` の中（§2.6）でこの印を見て、立っていれば書かずに ENXIO で返る。`panic.c:58-71` は他の hart を止めずに `reveal_fatal` を呼ぶが、印の確認と書く区間が同じ spinlock の中なので、他の hart の present が reveal_fatal の後に commit し直す事は無い（レビュー 3 Minor 5）。
  - snapshot は attach の時に作った不変の値なので lock は要らない。戻し先が無効の時（§4.7）は何もしない。
- **HAL（hal.h・fbcons）は変えない**。
- 確かめ: p003 の host 試験（偽の窓で戻しの書き込みの順、fatal の印の後の present が書かない事）と、実機の項目 U5。

### 4.7 自前の mode の後の戻し先（新 M6、**未定・p005 の前の追補**）

- release・panic・COPY・小さい BLOB の戻し先は U-Boot の plane の snapshot と bootfb で、**U-Boot の mode の間（p003・p004）だけ有効**とする。
- p005 で mode を変えた後、または D1 が no-go で snapshot が無い時の扱いは **未定**で、**p005 の前の追補**で決める（D7 と一緒に）。決める事: (i) release・panic の時に mode も U-Boot の mode へ戻すか、(ii) console を今の mode に合わせる（bootfb の作り直しは HAL の責務に触れ得るので、その場合は HAL の承認が要るかを先に確かめる）か、(iii) D1 が no-go の時の console と panic の扱い。
- それまでの実装の規則: snapshot の有効の印を持ち、無効なら release は plane を戻さずに console の resume だけ、panic の戻しは何もしない（serial に文字は出る）。

## 5. libvulkan の shared の道（新 M5）

- libvulkan が shared（zero copy）を試す条件（`context.c`）:
  - 共通の必須: wire_version 1、capset が 156 byte 以上、timelines が 0 でない（`:155-174`）。`GPU_CAP_SHARE` と linear の SAMPLED（`wsi-image.c:52-58`）。
  - capset の XML の版が **1.3.269** なら、vendor の flag に関係なく shared の道を試す（copy_display は 1.4.343（WINQ）の時だけ真、`:176`）。外部 memory は DMABUF で、blob は `SHAREABLE|CROSS_DEVICE`（`wsi-image.c:63`、`memory.c:1249-1251`）。
  - **1.4.343（WINQ）** の時だけ、flag が **ちょうど** `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT` である事と STRICT_QUEUE・QUIESCE の意味の義務（`:205-211`）。
- constraints の `GPU_SCANOUT_SHARED` は許可でしかない（`wsi-display.c:1286-1287`）。
- WS277 は libvulkan の条件を変えない（D4 (a)）。依頼 R7 は「1.3.269 で出す（共通の必須だけ）か、1.4.343 なら完全な flag と義務」の選択を WS269・WS270 に任せる。WS277 の推奨は 1.3.269（要らない義務を課さない）。
- 書き手（Minor 10）: libvulkan は scanout の image を自分で `VK_IMAGE_TILING_LINEAR` で作り（`wsi-image.c:80`）、GPU の copy（`present_copy_shared`）で書く。よって R6 は (1) PowerVR の Vulkan の linear の image の `rowPitch` を 32 byte の倍数にする（descriptor はここから作られる）、(2) transfer の書き出しを PBE LINEAR にする、の 2 つ。
- `display_placement` は alignment 0 を渡す（`wsi-display.c:1940-1953・1981`）ので、stride の揃えは image の layout（R6）が担う。

## 6. ユーザーの判断（D1…）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 最初の到達点 | (a) U-Boot の mode の引き継ぎ（p002〜p004）を先に、mode の設定（p005・p006）は後 / (b) 最初から clock・PLL・HDMI・EDID まで | (a)。**go/no-go は p002**（attach・判定・記録だけ、2〜4 LW、Minor 8）: 配布の U-Boot が HDMI と GOP を出し、ExitBootServices の後も DC8200 が動き、plane が §2.4 の判定に受かる事（U0）。no-go なら p003 を止め、§4.7 の追補と p005（自前の mode）を先にする。効果（fps）の測定は p007 で、WS269 p010 の後 |
| D2 | DC8200・HDMI の発見 | (a) `src/kern/platform/visionfive2.c` の組み込みの記述（DT があれば優先） / (b) DT 必須 | (a)（mainline の DTS に node が無い） |
| D3 | voutcrg・PD_VOUT の provider（p005） | (a) WS256 の `vf2-crg`・`vf2-pmu` に足す（R1・R2） / (b) DC8200 の driver が直に叩く | (a)。p005 の前に決めれば足りる |
| D4 | libvulkan の shared の道 | (a) PowerVR の renderer が条件を満たす（R6・R7、1.3.269 を推奨） / (b) libvulkan の条件を変える（別の WS） | (a) |
| D5 | HDMI の audio | (a) 範囲外（Future Work） / (b) 含める | (a) |
| D6 | 解像度の範囲 | (a) PLL の表の mode（≤297 MHz）だけ / (b) 表に無い mode の計算 | (a) |
| D7 | mode を選ぶ経路（p005・p006）と自前の mode の後の戻し先（§4.7） | (a) p005 は boot parameter、UAPI の経路と戻し先は p005 の前の追補で決める / (b) 今 UAPI に op を足す / (c) present の frame の大きさで選ぶ | (a) |
| D8 | GPU の SLC | (a) buffer の属性として迂回し全部の写像を揃え、照会の口で display が判定（R3）。費用は SHAREABLE・CROSS_DEVICE の全 buffer が SLC を使わない事 / (b) flip の前に SLCFLUSHINVAL | (a) |
| D9 | 小さい frame | (a) snapshot と同じ大きさ・stride だけ zero copy、ほかは copy の道 / (b) SIZE・位置・背景色を設定して小さい frame も | (a)（(b) は Future Work の候補） |
| D10 | p003 で latch をどう知るか（新 M1） | (a) vblank の IRQ を p003（版 2 の p002）に繰り上げ、s0 + 2 で latch と判定 / (b) 厳密な polling（commit 直後の標本が active の中、wrap を待つ、帰線の中なら次の帰線まで。`CURRENT_LOCATION` の bit の並びを先に確かめる） | **(a)**（Q1 の判断。busy の待ちの CPU も、未確認の register への依存も避ける）。代価は直列の present で **最大 refresh/2**（§2.6、レビュー 3 M1）。前提「IRQ は latch と同じ点か後」を U11 で確かめ、外れたら s0 + 3。p004 で `CURRENT_LOCATION` か COMMIT の自己 clear により 60 fps に戻す |
| D11 | latch の上限を超えた時・HDMI を抜いた時（新 M2） | (a) vblank が 100 ms 進まなければ出力喪失: 前の front へ書き戻して commit、ENXIO（SURFACE_LOST）、pending の保持は残す / (b) EIO（DEVICE_LOST） / (c) ESTALE（OUT_OF_DATE、swapchain の作り直し） | (a)（DEVICE_LOST にしない。DC が止まったのは surface の喪失に近い）。制限: hotplug の通知が無いので ENXIO の後 Keiland は回復しないことがある（§2.6、レビュー 3 Minor 3） |

## 7. Phase の分割（案、LW の見積もり直し）

LW は 1 LW ≈ 実時間 15〜25 分、±50%。

| Phase | 中身 | 依存（出力の範囲） | LW | 確認の点 |
| --- | --- | --- | --- | --- |
| p002 | **go/no-go**: DC の窓の写像、model・revision・CID、plane・panel の特定と §2.4 の判定・snapshot、COMMIT・`CURRENT_LOCATION` の読み戻しの記録、結果の記録（kernel の記録と画面への表示）。zero copy は出さない | WS269 p007 の display の node の code（merge 済み）、R4 の **記述の部分**（DC8200・HDMI の address・IRQ、出力の範囲。レビュー 3 M3）。R4 の記述が main に無ければ、p002 は driver の中の既定の address（0x29400000・0x29400800、IRQ 95）で始め、記述が入ったら置き換える | 2〜4 | build（warning 0）、host 試験（偽の窓で判定）。実機 U0 |
| p003 | 引き継ぎの zero copy: vblank の IRQ 95（D10 (a)）、query、import の検査（§4.2）、present（BLOB）: format・swizzle・blend 無効・shadow を止めた書き込み・commit・s0 + 2 の latch・「flip 中」で mutex を外した待ち、D11 の ENXIO と書き戻し、保持の数（§4.5）、release・close の順、COPY・小さい BLOB との行き来、flip の記録の環（§10）、VF2 の wrapper の text の ops の `reveal_fatal` の戻しと fatal の印（§4.6、wrapper の側は R4） | p002 が go、WS269 の lookup の code（merge 済み、出力の範囲）、R4（serial-console の追加の口と wrapper の ops）、R3 の照会の口（無ければ試験の blob だけ、§3.2） | 18〜26 | build（warning 0）、host 試験（偽の窓と偽の IRQ: 判定・書き込みの順・s0 + 2・範囲の検査・保持の数（二度目の present、destroy の遅延、最後の解放の文脈）・release の順・panic の戻し・ENXIO）。実機 U1〜U5 |
| p004 | UNGATED と adoption_wait（§4.3、pending の扱いの決定）、`CURRENT_LOCATION` か COMMIT の自己 clear で commit の後の最初の vsync を判定して **60 fps に戻す**（§2.6、レビュー 3 M1）、refresh の実測、underflow の記録 | p003、U11 の結果 | 6〜10 | host 試験。実機 U6・U11 |
| p005 | **前に追補**: §4.7 の戻し先と D7。自前の mode の設定（voutcrg・PD_VOUT、clock・reset、DSS の syscon、timing、HDMI の PHY の PLL、boot parameter の mode） | p004（D1 が no-go なら p002）、R1・R2、D3・D7、§4.7 の追補 | 14〜22 | 実機 U7 |
| p006 | DDC・EDID、mode の一覧（ENUMERATE・VALIDATE）、D7 の UAPI の経路（決まれば） | p005、D7 | 8〜14 | 実機 U8 |
| p007 | zero copy の通し: GPU が書いた non-mappable な線形の image で古い画が出ない事、Keiland の desktop の fps（copy の道と比較）、acquire の遅れと SLC 迂回の費用の測定、`stride_alignment = 32` への切り替え | p004、R3・R5・R6・R7、WS269 p010・p011、WS270 の該当 Phase | 4〜8 | 実機 U9・U10 |
| p008 | 規約の全文の見直し | 全部 | 2〜3 | build（warning 0）、host 試験 |

- D1 が no-go の時の順（レビュー 3 Minor 10）: p002 → §4.7 の追補 → p005（自前の mode） → p003（IRQ・present・保持の数。依存は p002 でなく p005） → p004 → p006 → p007 → p008。
- 合計 54〜87 LW（版 3 は 53〜85。p004 に 60 fps に戻す分を足した）。増分は go/no-go の分離、IRQ の繰り上げ（p004 から p003 へ移し、p004 は 5〜8 に減る）、保持の数・ENXIO・fatal の印・flip の記録の環の分。
- 実機の確認は VF2 で、T1 への依頼か §10 の一覧で行う（QEMU に DC8200 は無い。QEMU で通るのは host 試験と fallback の道だけ）。serial・console の log では判定しない。

## 8. HAL

- HAL の API の変更は要らない: 物理は scanout の backing の page の配列（`gpu-scanout.h:29-35`）、DC の窓と IRQ は既存の kernel の写像・IRQ の API、panic は VF2 の platform の wrapper の text の ops の `reveal_fatal`（§4.6）で plane を戻すだけ。**HAL の描き直し（`riscv64_cons_redraw`）は呼ばない**。§4.7 で console の作り直しを選ぶ時だけ HAL の責務に触れ得るので、その時に承認を先に取る。`proposed/` は置かない。

## 9. 他 WS への依頼の表（Q1 が各 WS の Phase へ渡す）

| ID | 相手 | source | 中身 | WS277 の使う Phase | 理由 |
| --- | --- | --- | --- | --- | --- |
| R1 | WS256 | `src/drivers/platform/visionfive2/vf2-crg.*` | voutcrg（0x295C0000）の provider（DC8200・HDMI の clock・reset、`hdmitx0_pixelclk` の外の clock の扱い） | p005 | D3、§2.2 |
| R2 | WS256 | `vf2-pmu.*` | `DRV_VF2_PD_VOUT` を EPERM でなく受ける（今は PD_GPU だけ、`vf2-pmu.h:41`） | p005 | D3 |
| R3 | WS269（render.c、p010 の前か中） | `src/drivers/gpu/powervr/render.c` | （レビュー 3 Minor 4 で縮めた）写像の迂回は e4d7edbd7 で main に入っている。残りは **`render_export` が image の export を mappable でも shareable でもない blob に限って拒む**事だけ。受け入れられなければ代わりに `render.h` に迂回の照会の口。どちらも不可なら SLCFLUSHINVAL の道を WS277 と調整 | p003（無ければ試験の印だけ、§3.2）・p007 | 新 M4、D8 |
| R4 | WS256（VF2 の platform。持ち主は WS256 p008 cleared の後で Q1 が決める） | `src/kern/platform/visionfive2.c`、`src/drivers/generic/serial-console.c`・`.h`（汎用で追加だけ） | (1) **記述の部分**: DC8200（0x29400000・0x29400800、IRQ 95）・HDMI（0x29590000、IRQ 99）の組み込みの記述。(2) **text の部分**: serial-console に汎用で追加だけの口（text の ops の表を返す関数など。既存の関数・動作・riscv-virt は変えない、Q1 の判断）を足し、VF2 の platform にその表を呼ぶ **wrapper の text の ops** を置き、`reveal_fatal` で display の driver が登録する「致命の時の戻し」を呼ぶ（登録口も platform の中） | (1) は p002（無ければ既定の address で始める）、(2) は p003 | D2、§4.6、Minor 1、レビュー 3 M2・M3 |
| R5 | WS269 | `render.c`（render node の ops） | `blob_create_placed`（`CONTIGUOUS|DMA32`・上限 0xffffffff を検査して受ける） | p007（任意） | — |
| R6 | WS269 p010・p011 | PowerVR の Vulkan の image・transfer | (1) linear の image の `rowPitch` を 32 byte の倍数（かつ 2 pixel）にする、(2) transfer（`present_copy_shared` の GPU の copy）の書き出しを PBE `MEMLAYOUT_LINEAR` にする。入った後に WS277 が `stride_alignment = 32` を出す | p007 | §3.1、§5、Minor 4・10 |
| R7 | WS269 p010・WS270 | PowerVR の native の renderer の capset | 共通の必須（wire_version 1、capset ≥ 156 byte、timelines ≠ 0、`GPU_CAP_SHARE`、linear の SAMPLED）を満たし、版を選ぶ: **1.3.269**（推奨、vendor の flag の義務無し）か、1.4.343 なら flag がちょうど `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT` と STRICT_QUEUE・QUIESCE の義務 | p007 | D4、§5、新 M5 |

- R8（`render.h` の retain）は要らない（新 B1 は display.c の中の保持の数で解く、§4.5）。
- 版 2 からの直し: R3（属性・全写像・照会の口）、R4（path・wrapper の ops・serial-console は汎用のまま・持ち主・p002 から要る）、R6（rowPitch と transfer）、R7（版の選択と必須の条件）。Phase の番号の変化: R1・R2 は p004 → p005、R5・R6・R7 は p006 → p007、R3 は p006 → p003・p007。
- Awesome Plan §5 により、Q1 が R1〜R7 を相手の Phase への変更（foreign Phase の comment と依存）として出す。WS277 はこれらの source を変えない。

## 10. 実機の確かめ（ユーザーの試験の一覧の形、今は頼まない）

VF2 の板の接続と実機の判定の方法は WS256 の #10・#15 で未決。それが決まり、該当の Phase が実装された後に一覧に載せる（T1 が流せる物は T1 へ）。

**決定的な確かめ（Minor 9）**: 目視は 1 frame の崩れを見落とし、ADDRESS の読み戻しはおそらく shadow（書いた値）で latch の値ではない（推測）。よって p003 は kernel の中に **flip の記録の環**（present の開始・commit の sequence s0・latch と判定した sequence・present の返り・保持の数の増減の順）を持ち、試験の program がそれを読んで「返りは s0 + 2 の後」「前の front の `holds--` は新しい front の latch の後」を照合する。目視の項目はこの照合の補助とし、検出の限界を結果に書く。

| ID | Phase | 手順 | 合格 |
| --- | --- | --- | --- |
| U0 | p002 | 配布の U-Boot で起動し、DC の model・revision・CID・plane の判定・COMMIT と `CURRENT_LOCATION` の読み戻し（bit の並びを決める）を画面に出す | HDMI に GOP の画が出ていて、判定が「引き継げる」（go）。外れたら no-go を記録 |
| U1 | p003 | 試験の program が mappable な blob に色の帯を書き present。その後 present せずに CPU が blob を書き換える | 書き換えが present 無しで画面に出る（zero copy の時だけ） |
| U2 | p003 | 赤・緑・青の帯で BGRA と RGBA | 色が合う |
| U3 | p003 | 2 つの buffer を交互に present し、返った直後に前の buffer へ別の模様を書く | flip の記録の環の照合が全部合う。目視で崩れが無い（補助） |
| U4 | p003 | release で console に戻る。小さい frame・stride の違う frame は copy の道で出る。同じ image を何度も present し、途中で destroy する | 照合が合う、範囲外の画が出ない、二度目の present が出来る、destroy の後も表示中の buffer が解放されない（記録の保持の数） |
| U5 | p003 | zero copy 中に panic を起こす（試験用の手段） | panic の文字が画面に出る。fatal の後に present が commit しない |
| U6 | p004 | 交互の flip で wait の間隔を画面に出す。HDMI を抜く | 60 Hz 前後（p004 の後は 60 fps の present）。抜いた時に DEVICE_LOST と hang にならない。vblank が止まった時だけ ENXIO。どちらが起きたか（vblank が続いたか・ENXIO か）を記録する（レビュー 3 Minor 2） |
| U7 | p005 | boot parameter で 720p | 720p の画が出る |
| U8 | p006 | EDID の mode の一覧と選んだ mode | 一覧が出て、その mode で画 |
| U9 | p007 | GPU が描いた non-mappable な image を続けて present | 古い画が混ざらない |
| U11 | p003 | IRQ の handler が記録した `CURRENT_LOCATION` の行の位置（flip の記録の環）を読み、IRQ が frame のどこで出るかを数百 frame 分見る（レビュー 3 M1） | IRQ の点が latch の点（帰線の中の位置）と同じか後で、遅れが帰線に収まる。外れたら s0 + 3 にして記録 |
| U10 | p007 | Keiland の desktop の fps と acquire の遅れを copy の道と比べる | 数値の記録（ws.md 完了の条件 2）。refresh/2 の制限の有無（p004 の前か後か）も記録 |

## 11. 未確認の一覧

- VF2 の DC8200 の model・revision・CID。HDMI が panel 0 か。
- U-Boot が HDMI と GOP を出すか、ExitBootServices の後も DC が動くか、GOP の base と plane の ADDRESS が一致するか（p002・U0）。
- plane の address の上位 bit（vendor の 40 bit の DMA の mask との矛盾）。
- stride の 32 byte・先頭の揃えが HW の必須か、stride の register の幅。
- COMMIT bit の自己 clear、latch の時点、`CURRENT_LOCATION` の bit の並び、ADDRESS の読み戻しが shadow か latch の値か、`PANEL_CONFIG_EX` bit 0 の向き。
- vblank の IRQ が latch と同じ点か後か（s0 + 2 の前提、U11。外れたら s0 + 3）。IRQ の点と s0 + 1 で足りるか（p004）。
- `FRAMEBUFFER_CONFIG_EX` bit 12 と `PANEL_CONFIG_EX` bit 0 の真の意味（vendor は bit 0 を shadow の停止、mainline は `VSDC_DISP_PANEL_CONFIG_EX_COMMIT` と名付け set するだけ、レビュー 3 Minor 1）。
- HDMI を抜いた時に vblank が止まるか（U6）。Keiland が VF2 で window mode か（ENXIO の後の回復、D11）。
- FW が完了時に SLC を書き出すか（R3 で迂回すれば関係しない）。SHAREABLE の blob が app と compositor の共有にも使われるか（D8 の費用）。
- DSS の syscon の bit の意味（TRM）。i915 などが mode をどう選ぶか（D7）。§4.7 の戻し先（未定、p005 の前の追補）。
- mainline の `verisilicon,dc` が JH7110 で動くか。vendor の Linux の GNOME が遅かった原因。
- Future Work の候補: 小さい frame の zero copy（D9 (b)）、overlay・cursor の plane、HDMI の audio（D5）、hotplug（IRQ 99）。

## 12. レビュー 1 への対応（版 2、要約）

版 2 で B1〜B3・M1〜M9・Minor 1〜11 に対応した（表の全文は git の履歴の版 2）。レビュー 2 で部分・未解決とされた物は §13 で直した。

## 13. レビュー 2 への対応（版 3）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| 新 B1 参照の移し替えで二度目の present ができない | 移し替えをやめ、display.c の中で resource に保持の数と destroyed の印。destroy は保持中なら遅らせ、最後に外す時に thread の文脈で release。R8 は要らない（Q1 の判断） | §1.7、§4.5、§9、U4 |
| 新 M1 polling の latch の判定 | D10 (a): vblank の IRQ を p003 に繰り上げ、s0 + 2 で latch と判定。polling・`CURRENT_LOCATION` に頼らない | §2.6、§6 D10、§7 |
| 新 M2 上限の EIO が DEVICE_LOST | D11 (a): 上限は vblank の進みで判定、出力喪失は前の front へ書き戻して ENXIO（SURFACE_LOST）、pending の保持は残す | §2.6、§6 D11、U6 |
| 新 M3 書く途中の部分 latch | vendor の shadow を止める → 書く → 戻すを常に使う。加えて stride・SIZE が snapshot と同じ時だけ zero copy | §2.6、§3.1、§4.2 |
| 新 M4 R3 の抜け道 | SLC 迂回を buffer の属性にし全写像を揃える、照会の口、迂回の無い buffer は copy の道。D8 の費用を明記 | §3.2、§6 D8、§9 R3 |
| 新 M5 libvulkan の条件の誤り | 共通の必須と、1.3.269 / 1.4.343 の版の選択に書き直し。1.3.269 を推奨 | §5、§9 R7 |
| 新 M6 自前の mode の後の戻し先 | **未定、p005 の前の追補**と明記。それまでは snapshot の有効の印で扱いを分ける | §4.7、§6 D7、§7 |
| Minor 1 path・持ち主・層の向き | path を直した。R4 は VF2 の platform の wrapper の text の ops、serial-console は汎用のまま、持ち主は Q1 | §0 S6、§4.6、§9 R4 |
| Minor 2 描き直しは無い | 描き直しの記述を消した（§4.5 (5)・§4.6）。fatal の印を present の側が見る | §4.5、§4.6、§8 |
| Minor 3 小さい BLOB の戻し | BLOB の行に、zero copy の front から bootfb へ戻す commit と latch を書いた | §4.2 |
| Minor 4 `stride_alignment = 32` | 今の値のまま、R6 の後（p007）に 32。import の側で検査 | §4.2、§7、§9 R6 |
| Minor 5 blend の offset | `BLEND_CONFIG` 0x2510 + 4n、swizzle 2 bit・format 6 bit | §2.4、§2.5 |
| Minor 6 mutex を持った待ち | 「flip 中」の状態を作り mutex を外して待つ | §4.3 |
| Minor 7 依存の書き方・ws.md | 「lookup の code（merge 済み）」の出力の範囲。ws.md の依存節に R1〜R7・WS269 p010 | §7、ws.md |
| Minor 8 U0 は code を要する | go/no-go を p002（2〜4 LW）に分けた | §6 D1、§7 |
| Minor 9 U3・U4 の検出の限界 | kernel の flip の記録の環で決定的に照合、目視は補助 | §10 |
| Minor 10 R6 の補足 | rowPitch と transfer の 2 つに | §5、§9 R6 |
| Minor 11 acquire の遅れ | p007 の測定項目 | §4.3、§7、U10 |
| D10・D11 の追加 | 足した | §6 |

## 14. レビュー 3 への対応（版 3.1）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| M1 (a) 速さ: 直列の present で最大 refresh/2 | 代価を「最大 refresh/2」に直した。p004 の範囲に `CURRENT_LOCATION` か COMMIT の自己 clear で commit の後の最初の vsync を判定して 60 fps に戻す事を入れた（候補の扱いをやめた）。p007・U10 で制限の有無を記録 | §2.6、§4.3、§6 D10、§7 p004、U10 |
| M1 (b) 正しさの前提・s0 の取り方 | 前提「IRQ は latch と同じ点か後」を明記。外れたら s0 + 3。s0 は手順 (4) の後に spinlock の中で取る | §2.6、§6 D10 |
| M1 (c) 試験で HW の latch の遅れを検出できない | IRQ handler が `CURRENT_LOCATION` の行を記録し、U11 で IRQ の位置を実機で確かめる。bit の並びは p002（U0） | §2.6、§10 U0・U11、§11 |
| M2 R4 の wrapper は作れない | Q1 の判断: serial-console に汎用で追加だけの口（ops の表を返す関数など）を足す事を許す。R4 の文言と §4.6 をそう書き換えた | §4.6、§9 R4 |
| M3 p002 の依存に R4 | p002 の依存に R4 の記述の部分（出力の範囲）。無ければ driver の中の既定の address で始め、後で置き換える。R4 を記述と text の 2 つに分けた | §7 p002、§9 R4、ws.md |
| Minor 1 bit 12 の clear、mainline の bit 0 の名前 | 手順 (1) で bit 12 を clear。mainline との意味の食い違いを §11 に | §2.6、§11 |
| Minor 2 U6 の合格 | DEVICE_LOST と hang にならない事。vblank が止まった時だけ ENXIO、どちらかを記録 | §2.6、U6 |
| Minor 3 D11 の回復の制限 | Keiland は hotplug に頼るので ENXIO の後 desktop が止まったままになり得る、と書いた | §2.6、§6 D11 |
| Minor 4 §3.2 の「今」 | e4d7edbd7 の現状に直し、R3 を「`render_export` が非 mappable・非 shareable の blob の image の export を拒む」に縮めた（照会の口は代わり）。試験の印の中身（boot parameter、判別は試験の側）を定めた。古い行番号を直した | §3.2、§6 D8、§9 R3 |
| Minor 5 fatal の印の競合 | 書く区間を IRQ 禁止の spinlock に入れ、区間の中で印を見直す。panic は上限付きで取り、取れなくても書く | §2.6、§4.6 |
| Minor 6 戻しの順 | 先に bootfb へ copy してから plane を戻し latch を待つ | §4.2 |
| Minor 7 IRQ の数え方 | HDMI の panel の bit の時だけ sequence を進める。IRQ は attach で有効にし常に有効 | §2.6 |
| Minor 8 細かい不整合 | 「失敗の道は無い」を fatal の印と整合させた。panic の polling は `CURRENT_LOCATION` の wrap（ACK は読まない）。遅らせた解放は `owner` を参照しない | §2.6、§4.5、§4.6 |
| Minor 9 p004 の UNGATED と pending | 待つ形を保つか pending を置き換えるか（保持の数は最大 3）を p004 の設計で決める事として書いた | §4.3、§7 p004 |
| Minor 10 D1 が no-go の時の順 | p002 → §4.7 の追補 → p005 → p003 → p004 … と書いた | §7 |
