<!-- awesome-plan project=zedbsd record=ws277-design -->

# WS277 設計: VisionFive 2 の DC8200 の display driver（zero copy の scanout）

- 版: 2（2026-10-20、P1、ws277-p001、q1343-i01）。[レビュー 1](design-review-1.md) の B1〜B3・M1〜M9・Minor 1〜11 に対応（末尾の表）。コードは書いていない。design-reviewer の差分の確認待ち。
- 版 1（2026-10-20、q1341-i01）は git の履歴。
- 目的: PowerVR（BXE-4-32）が線形で書いた render の buffer を、CPU の copy 無しで DC8200 が直接 scanout する。WS269 の display の node（gpu0、boot framebuffer への CPU の copy）を、DC8200 を自分で操作する node に置き換える。

## 0. 出典

出典の規則は [Guardrail](../guardrail.md) と WS256 の決定（[design-peripherals.md](../ws256/design-peripherals.md):40、2026-10-19 ユーザー「DC8200/HDMI は手順の参考を許す」）に従う: 値は `Values referenced:`、手順は `Procedure referenced:`、vendor の GPL の code・comment・構造・名前は写さない。

| ID | 出典 | 場所 | 使う物 |
| --- | --- | --- | --- |
| S1 | StarFive の vendor の Linux 6.12（読むだけ） | `~/visionfive-linux/drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_dc_hw.c/.h`・`vs_gem.c`・`vs_plane.c`・`inno_hdmi.c`） | DC8200 の register の offset、初期化と clock の手順、plane の format・modifier、HDMI の PHY の PLL の表 |
| S2 | 同 DTS | `~/visionfive-linux/arch/riscv/boot/dts/starfive/jh7110.dtsi:1569-1645`、`jh7110-clk.dtsi:46-48` | dc8200・hdmi・dssctrl・voutcrg の address・IRQ・clock・reset・PD |
| S3 | mainline Linux（GitHub の torvalds/linux、2026-10-20 に gh api で読んだ） | `drivers/gpu/drm/verisilicon/`（`vs_dc.c`・`vs_hwdb.c` 等）、`arch/riscv/boot/dts/starfive/jh7110.dtsi` | mainline の driver は汎用の `verisilicon,dc`（TH1520 向けに入った）で、chip の id の表に model 0x8200・revision 0x5720/0x5721 がある。mainline の DC の register の名前（`vs_primary_plane_regs.h` の `VSDC_FB_*`）と atomic_update の手順を Procedure referenced で使う。**mainline の jh7110.dtsi には dc8200・hdmi の node が無い**（voutcrg の node は PD_VOUT 付きである、約 1202 行。`hdmitx0_pixelclk` は固定 clock）。CID 0x310 の扱いは vendor（rev 0x5721）と mainline（0x5720）で違う。JH7110 での mainline の動作は未確認 |
| S4 | Mesa（gitlab.freedesktop.org mesa/mesa の main、head 0913fce718fe、2026-10-20） | `src/imagination/csbgen/rogue/pbestate.xml:58-64・412-418`、`src/imagination/vulkan/pvr_image.c:49-100・467-472`、`pvr_formats.c:460-506`、`pvr_wsi.c:82`、`src/imagination/common/device_info/bxe-4-32.h` | PBE の memory の layout の値、線形の stride の単位、WSI の modifier、BXE-4-32 の feature（`has_pbe_stride_align_1pixel` が無い）。Mesa の blob 検索は拒まれた（401）ので、それに頼る点は未確認 |
| S5 | JH7110 TRM（Preliminary V2、sha256 は WS256 design-peripherals.md:15） | — | memory map・PMU・CRG の事実。**この Phase では PDF の register の章を読み直していない**（WS256 が引いた address だけを使う）。VOUT の CRG・DSS の syscon の bit の意味は p002 で TRM と照らす（未確認） |
| S6 | zedBSD | WS269 [design.md](../ws269/design.md) 版 3.1 §3.5・§6、`src/drivers/gpu/powervr/display.c`・`render.c`・`render.h`、`src/drivers/platform/visionfive2/vf2-crg.h`・`vf2-pmu.h`・`vf2-gpu.c:60`、`include/drivers/gpu/gpu-scanout.h`・`gpu-display.h`（UNGATED・adoption_wait）、`include/uapi/gpu-scanout.h`・`gpu-display.h`、`include/kern/text-display.h`（`reveal_fatal`）、`src/drivers/platform/visionfive2/serial-console.c`、`userland/desktop/libvulkan/wsi-display.c`・`wsi-swapchain.c`・`memory.c`・`context.c`、`src/kern/gpu.c`、WS269 [p007](../ws269/phase007/phase.md):51-52、WS256 [p011](../ws256/phase011/phase.md) | 置き換える node の形、lookup、placement、shared の道の条件 |

## 1. 結論（先に）

1. **PowerVR は線形で書ける**（S4、Values referenced）: PBE の `MEMLAYOUT` は `LINEAR=0`・`TWIDDLE_2D=1`・`TWIDDLE_3D=2`・`TILED=3`。Mesa の PowerVR は `VK_IMAGE_TILING_LINEAR`・WSI の legacy scanout・`DRM_FORMAT_MOD_LINEAR` の image を `LINEAR` で作り、PBE で書く。WSI の modifier は LINEAR だけ。線形の stride は PBE の `linestride` が **2 pixel 単位の 16 bit**。BXE-4-32 の device_info に `has_pbe_stride_align_1pixel` が無いので **2 pixel 単位は確定**（Values referenced、レビュー Minor 2）。
   - 「VF2 の Linux が遅いのは PowerVR が tile しか書けないから」ではない（推測: vendor の閉じた DDK・GNOME の道が copy を選んでいた。未確認）。
2. **DC8200 は線形を読める**（S1 `vs_dc_hw.c:412-432`）: plane の modifier の先頭が `DRM_FORMAT_MOD_LINEAR`。PowerVR の twiddled・tiled は無い。→ zero copy の唯一の形は **線形の 32 bpp・alpha を使わない X 系**（XRGB8888・XBGR8888 として出す、§2.5）。
3. **address は 32 bit**（S1 `vs_dc_hw.h:332-333・393-394`、`vs_dc_hw.c:1606-1607`。vendor の DMA の mask 40 bit との矛盾は未確認）→ buffer は 4 GiB 未満の物理。WS269 の render の buffer は 1 つの連続の run・上限 `0xffffffff`（`render.c:13`、`vf2-gpu.c:60`）。
4. **MMU は使わない**（S1 `vs_gem.c:475-481`）。物理の連続を import の時に検査する。
5. **最小の道**: U-Boot が立ち上げた mode・clock・HDMI を引き継ぎ、**primary plane の address・stride・format・SIZE を書き、mainline の commit の手順で採らせ、latch を待ってから present を返す**（p002）。mode の設定（clock・PLL・HDMI・EDID）は p004・p005。
6. **present は latch の後に返る**（B1）: libvulkan は present の成功の直後に前の front の hold を外すので、kernel の present（UNGATED なら adoption_wait）は新しい buffer が DC8200 に採られた後にだけ完了を返す（§4.3）。
7. **GPU の SLC**（B2）: GPU が書いた shareable の image が SLC に残ったまま DC8200 が DRAM を読むと古い画になる。WS269 の render.c に「shareable の blob を常に SLC_BYPASS で写す」を依頼する（§3.2、他 WS への依頼 R3）。
8. **範囲外の読み**（B3）: zero copy は frame の幅・高さが plane が読む範囲（mode）と一致する時だけ。小さい frame は copy の道（§4.2）。
9. **HAL の変更は要らない**。panic の画面は kernel の text の ops の `reveal_fatal` で解く（§4.6、M3）。

## 2. DC8200 と HDMI のハードウェア（S1・S2・S3、Values referenced）

### 2.1 address・IRQ・電源

| 部品 | 物理 | 大きさ | IRQ | 備考 |
| --- | --- | --- | --- | --- |
| DC8200 の AHB の窓（id・IRQ） | 0x29400000 | 0x100 | 95 | `DC_HW_REVISION` 0x24、`DC_HW_CHIP_CID` 0x30、model 0x20（0x8200）、`AQ_INTR_ACKNOWLEDGE` 0x10（**読むと clear**）、`AQ_INTR_ENBL` 0x14 |
| DC8200 の display の register | 0x29400800 | 0x2000 | — | 下の offset は 0x29400000 から |
| DC8200 の MMU（使わない） | 0x17030000 | 0x1000 | — | §1.4 |
| HDMI TX（Innosilicon） | 0x29590000 | 0x4000 | 99 | PHY と DDC |
| DSS の syscon（dssctrl） | 0x295B0000 | 0x90 | — | vendor は 0x4 の bit 20、0x8 の bit 3 を立てる（`vs_dc.c:636・640・702・704`）。意味は未確認（p004 で TRM） |
| VOUT の CRG（voutcrg） | 0x295C0000 | 0x10000 | — | PD は `JH7110_PD_VOUT` |

- revision 0x5720 か 0x5721（CID 0x30B・0x310）。VF2 の値は未確認（p002 で読んで記録）。
- p002・p003 は PD・clock・reset に触らない（WS256 design-peripherals.md:230「VOUT の clock と PD_VOUT を止めない」と合う）。

### 2.2 clock・reset・PD（p004、S2 `jh7110.dtsi:1576-1600`、S1 `vs_dc.c:660-663・697-760`、Procedure referenced）

- clock: syscrg の `NOC_BUS_DISP_AXI`・`VOUT_SRC`・`VOUT_TOP_AXI`・`VOUT_TOP_AHB`、voutcrg の `DC8200_PIX0/PIX1/AXI/CORE/AHB`・`DOM_VOUT_TOP_LCD`・`HDMI_TX_SYS/MCLK/BCLK`、外の `hdmitx0_pixelclk`（DT の固定 297 MHz は名目の値で、実際の rate ではない。実体は HDMI の PHY の PLL の出力と推測）。
- reset: syscrg の `VOUT_TOP_SRC`・`NOC_BUS_DISP_AXI`、voutcrg の `DC8200_AXI/AHB/CORE`、`HDMI_TX_HDMI`。
- HDMI（display 0）の pixel clock: `DC8200_PIX0` の親を `hdmitx0_pixelclk`（`vs_dc.c:752-754`）。
- **zedBSD の不足**（M9）: `vf2-crg` に voutcrg の provider が無い。`vf2-pmu` は `DRV_VF2_PD_GPU` 以外を EPERM にする（`vf2-pmu.h:41`）。どちらも WS256 の source なので依頼 R1・R2（§9）。

### 2.3 timing・出力・pixel clock の範囲

- timing の register: `DC_DISPLAY_H` 0x1430、`H_SYNC` 0x1438、`V` 0x1440、`V_SYNC` 0x1448、`PANEL_CONFIG` 0x1418、`CURRENT_LOCATION` 0x1450（S1 `vs_dc_hw.h:136-145`、panel ごとに +4n）。
- HDMI の PHY は pre-PLL・post-PLL の表の pixel clock だけを受け、297 MHz を超える物は断る（`inno_hdmi.c:54-`・`:615-634`、D6）。
- DDC・EDID は p005（`inno_hdmi.c:321-332・594-601`）。

### 2.4 plane・panel の特定（M4）

- primary plane は 2 つ（plane n の register は base + 4n、S1 `vs_dc_hw.c:1290-1309`、S3 `VSDC_FB_ADDRESS(n)` = 0x1400 + 4n）。panel も 2 つ。plane が出る panel は `CONFIG_EX` の bit 19（DISPLAY_ID）。
- 使う register（plane n）: `ADDRESS` 0x1400、`STRIDE` 0x1408、`CONFIG` 0x1518（tile・rotation・scale・format・swizzle bit 23・26）、`CONFIG_EX` 0x1CC0（FB_EN bit 13、DISPLAY_ID bit 19、DEC bit 1、COMMIT bit 12）、`SIZE` 0x1810（width | height<<15）、`TOP_LEFT` 0x24D8・`BOTTOM_RIGHT` 0x24E0、`BG_COLOR` 0x1528、blend の設定（mainline `VSDC_FB_BLEND_CONFIG`、regs.h:51、offset は p002 で mainline の regs.h から Values referenced で取る）、`WATER_MARK` 0x1CE8。
- **引き継ぎの判定**（attach の時、§4.1）: model 0x20 が 0x8200、revision・CID を記録。両方の plane の `CONFIG_EX`・`CONFIG` を読み、FB_EN が立ち、DISPLAY_ID が HDMI の panel（推測で 0、vendor `vs_dc.c:747-755`。p002 の実機で確かめる）の plane を選ぶ。条件は全部: `ADDRESS == bootfb の物理`、`STRIDE == bootfb の pitch`、`SIZE == W×H`、線形・無回転・無拡縮・DEC 無し。1 つでも外れれば zero copy を出さず、今の copy の道（WS269 p007）だけ。
- 判定の時に plane の register 全部（ADDRESS・STRIDE・CONFIG・CONFIG_EX・SIZE・TOP_LEFT・BOTTOM_RIGHT・BG_COLOR・blend）を **snapshot** に取る（release と panic で戻す、§4.5・§4.6）。

### 2.5 format・alpha（M5）

- zero copy の plane は **X 系**（XRGB8888・XBGR8888）で出し、alpha を無視する。mainline と同じく blend を無効にする（`VSDC_FB_BLEND_CONFIG_BLEND_DISABLE`）。ARGB を blend 有りで出すと alpha 0 の frame が背景色になる。
- frame の byte 順（`GPU_PIXEL_BGRA8888` / `RGBA8888`）から `CONFIG` の format と swizzle（bit 26・23）を決めて書く。U-Boot の format と同じなら書き換えない。赤と青の対応は p002 の実機の項目（§10 の U2）。

### 2.6 commit・latch・vblank（B1、M4、Minor 6）

- commit（S3 mainline atomic_update、Procedure referenced）: plane の ADDRESS・STRIDE・CONFIG・SIZE・TOP_LEFT・BOTTOM_RIGHT・blend を書き、`CONFIG_EX` の COMMIT（bit 12）を set する。vendor はさらに `PANEL_CONFIG_EX` の bit 0 を操作する（`vs_dc_hw.c:1540-1547`）。要否は p002 の実機で確かめ、要らなければ書かない。
- latch: 新しい register の値は次の vblank で採られると推測（未確認）。**p002 は polling で待つ**: `CURRENT_LOCATION` の行が一度 vertical の active の外（≥ 表示の高さ）を通り、active の先頭に戻った事を見る（推測: その間に latch が起きる）。COMMIT bit が自己 clear するなら、それを第一の判定にする（未確認、p002 の最初に読み戻しで確かめ、結果で一方を選ぶ）。polling の道では `AQ_INTR_ACKNOWLEDGE` を読まない（読むと clear され、p003 の IRQ と競合する）。
- 待ちの上限: 1 frame の 3 倍（refresh 不明なら 50 ms）。超えたら（HDMI を抜いた・DC が止まった）flip を失敗にして EIO を返し、前の front を表示し続ける扱いにする（新しい buffer の参照は持ったまま、次の成功した latch か release で外す。DC が本当に読んでいないかは分からないので安全の側）。
- p003: vblank の IRQ 95（`AQ_INTR_ENBL` の display ごとの bit、`vs_dc_hw.c:1501-1512`）で latch を知る。IRQ の中では ack・時刻・sequence の記録と待ち手の起床だけを行い、参照は外さない（§4.5）。underflow は `FRAMEBUFFER_CONFIG` の bit 5（`:1514-1517`）を記録。
- refresh（M8）: DT の固定の 297 MHz は使わない。p002 は「不明なら 60 Hz」、p003 から vblank の間隔の実測（数 frame の平均）で報告する。

## 3. memory と coherency

### 3.1 PowerVR が書く buffer の条件（S4・S6）

| 条件 | 値 | 根拠 |
| --- | --- | --- |
| layout | PBE `MEMLAYOUT_LINEAR` | S4 pbestate.xml:58-64 |
| stride | 32 byte の倍数（DC8200、vendor の pitch_alignment。HW の必須かは未確認なので強い側）かつ 2 pixel の倍数（PBE）、かつ ≤ 65535（vendor の `struct dc_hw_fb` の stride が u16、`vs_dc_hw.h:399`。register の幅は未確認、Minor 10） | §2.4 |
| 先頭 | import の offset は 0 だけ（`backing->pages[0]` の先頭）。`offset_alignment` は出さない（Minor 5） | — |
| 物理 | 1 つの連続の run、末尾 ≤ 0xffffffff | §1.3・§1.4 |
| 大きさ | `stride × plane の高さ` ≤ backing の大きさ（B3） | §4.2 |
| format | 32 bpp の BGRA・RGBA（X として出す） | §2.5 |

### 3.2 coherency（B2、Minor 3）

- CPU: render の buffer は確保の時に uncached の別名で零埋め済み（`render.c:322-333`）で、host は uncached の別名でしか触らないので、CPU の L2 に dirty な line は残らない。
- GPU の SLC: SLC_BYPASS は render の session の GPU MMU の写像ごとに付き、今は mappable な blob だけ（`render.c:981-985`、`:688-691`）。WSI の shared の image は `GPU_BLOB_SHAREABLE` で、HOST_VISIBLE の時だけ MAPPABLE（`memory.c:1246-1256`）。よって device-local の swapchain の image は SLC を迂回せず、FW が完了時に SLC を書き出すかも未確認（WS269 design.md:124・241）。
- 選ぶ道: **(a) `GPU_BLOB_SHAREABLE` の blob を常に SLC_BYPASS で写す**（写像は blob の作成と GPU の写像の時に決まるので、export・import の時点では遅い。作成の flag で決める）。依頼 R3（WS269 render.c）。代わりの (b) flip の前に FW へ `SLCFLUSHINVAL` を送る道は、display（gpu0）から render の FW の command の道へ依存が入り、present の遅れも増えるので採らない（(a) が出来ない事が分かったら (b) を R3 の代替として WS269 と調整する）。性能: 迂回は scanout の image の GPU の書き込みに SLC を使わない。影響の測定は p006。
- DC8200 自身は cache を持たず、CPU は buffer を cacheable に書かないので L2 の snoop の有無（WS256 p011 の未確認）は関係しない。
- 順序: flip は render の完了の後（libvulkan の worker が fence を待ってから present、`wsi-display.c:1459`）。driver は present の中で GPU の完了を再確認しない。

## 4. zedBSD の driver の形

### 4.1 置き場所・node・発見

- WS269 §3.5 の判断（display の node を powervr と同じ driver に置き、render の buffer を private な lookup で retain）を保つ。`src/drivers/gpu/powervr/display.c` を DC8200 を操作する形にし、register の操作は `dc8200.c`（p004 で `dc8200-hdmi.c`）に分ける（名前は実装で決める）。gpu0 = display、gpu1 = render。
- 発見（D2）: `visionfive2.c` の組み込みの記述（address・IRQ）を既定にし、DT の node があれば優先する。`visionfive2.c` は WS256 の source なので依頼 R4。
- 引き継ぎの判定は §2.4。外れたら copy の道だけ（fallback）。

### 4.2 `drv_gpu_display_ops` と scanout の ops

| op | 今（WS269 p007） | WS277 |
| --- | --- | --- |
| query・mode | loader の extent、60 Hz | p002: 引き継いだ plane の SIZE と panel の timing の register から extent。refresh は 60 Hz（不明）。p003: vblank の実測。p005: EDID（§4.4） |
| claim・release | console を止める・戻す | claim は同じ。release は §4.5 の順 |
| present（COPY） | CPU の copy を boot framebuffer へ | 残す。zero copy の flip の後に COPY が来たら、先に plane を bootfb へ戻す commit と latch の待ちを行ってから copy する |
| present（BLOB） | import の uncached の view から CPU の copy | **frame の幅・高さが plane の SIZE と一致する時だけ** zero copy（§2.6 の commit → latch を待って返る）。一致しない時（小さい frame）は今の CPU の copy の道（B3 (a)。誤った範囲を DC に読ませない） |
| wait | 即時 | latch の後の sequence を返す（present が既に latch を待つので、wait は記録の参照） |
| constraints | `COPY|SHARED|FOREIGN`、placement 0 | `COPY|SHARED|FOREIGN`（render は gpu1 で core は foreign と扱う、`gpu.c:5008`。Minor 1）。`stride_alignment = 32`。**placement は R5（render の `blob_create_placed`）が main に入るまで 0**、入った後に `GPU_PLACEMENT_CONTIGUOUS|GPU_PLACEMENT_DMA32`・`max_dma_address = 0xffffffff`（M1） |
| import_image | lookup して uncached の view を保持 | lookup で retain。加えて検査: backing の page が連続、末尾 ≤ 0xffffffff、offset 0、stride が 32 の倍数かつ ≤ 65535、`stride × image.height` ≤ 大きさ。外れた import は zero copy に使わない印を付け、copy の道で出す（EINVAL にしない。libvulkan が黙って壊れないように） |

- 範囲の検査（B3）: DC が読む範囲は `STRIDE × SIZE の高さ`。zero copy は image の幅・高さ == SIZE の時だけなので、import の `stride × image.height` の検査がそのまま DC の読む範囲を覆う。SIZE を frame に合わせて縮める道（B3 (b)、TOP_LEFT・BOTTOM_RIGHT・背景色）は後の候補（Future Work の候補、§11）。
- M1 の道の選択: placement を 0 にしても、PowerVR の render の buffer は全部が連続・≤ 0xffffffff なので import の検査で受かる。R5 は「将来の別の確保の道でも条件が守られる」ための整合で、p002 の zero copy の必須ではない。

### 4.3 present の契約（B1、Minor 6）

- libvulkan は PRESENT の ioctl の成功の直後に前の front の `holds--` を行い（`wsi-display.c:1475-1483`）、acquire は holds だけを見る（`wsi-swapchain.c:3795-3803`）。注記は「The kernel returns only after checked native selection and flush have completed」（`:1425`）。
- よって **present（BLOB）は、新しい buffer が DC8200 に latch された後にだけ成功を返す**。これで前の front は返った時点で DC から読まれていない。
- lock: p002 は ordinary admission（UNGATED にしない）で、present の中で display の mutex を持ったまま polling で待つ（最長 §2.6 の上限）。同じ open の他の op はその間待つが、present は 1 frame に 1 回なので許す。p003 で `DRV_GPU_DISPLAY_UNGATED` と `adoption_wait` の形（bcm2711、`gpu-display.h:60-75`）に移す: present は commit を記録して返り、core が lock 無しで adoption_wait を呼び、IRQ の latch で起きる。libvulkan の見る成功は adoption_wait の後なので契約は同じ。UNGATED では core の pin は callback の間だけなので、display は front・pending の buffer に自分の参照を持つ（§4.5）。
- 確かめの順: p002 の試験で「present が返った直後に前の buffer へ書いても画面が崩れない」を見る（§10 の U3）。

### 4.4 mode の選び方（M8）

- UAPI の mode の操作は ENUMERATE と VALIDATE だけで、設定の op が無い（`uapi/gpu-display.h:39-40・139-157`）。よって p004・p005 の mode の切り替えの経路は **未定義で、p004 の前に設計を足す**: 候補は (i) claim の時に VALIDATE 済みの mode を渡す、(ii) present の frame の幅・高さ・refresh から選ぶ、(iii) boot parameter。i915 がどうしているかは未確認で、p004 の前に調べる（D7）。UAPI に op を足すなら別の判断（kernel の UAPI、HAL ではない）。
- p004 の受け入れは「boot parameter で 720p を選んで画が出る」とし（UAPI を変えない最小の道）、UAPI の経路は D7 の結果で p005 に入れる。

### 4.5 front の参照と release・close（M2）

- 参照の所有: 今の `resource_destroy` は import の buffer をすぐ解放し（`display.c:466-468`）、core は close の前に session の resource を全部 destroy してから `end_lease` を呼ぶ（`display.c:349`）。`render.h` に retain は無い。→ **flip の成功の時に、front の buffer の参照（lookup の retain）を resource から display の側へ移す**（resource は参照を持たない印を付け、`resource_destroy` は display が持つ参照に触らない）。display は front と、p003 では pending の 2 つまでを持つ。新しい front が latch されたら古い front の参照を外す。
- 参照を外すのは thread の文脈だけ（`buffer_free` は uncached の別名を外して `kern_free` するので IRQ の中で呼べない）。p003 の IRQ は記録と起床だけで、外すのは adoption_wait・次の present・release の thread。
- release・close の順: (1) snapshot の plane の register を全部（ADDRESS・STRIDE・CONFIG・CONFIG_EX・SIZE・TOP_LEFT・BOTTOM_RIGHT・BG_COLOR・blend）戻す → (2) commit → (3) latch を待つ（上限 §2.6）→ (4) front・pending の参照を外す → (5) console の resume（描き直し）。(3) が上限を超えたら、DC が古い buffer を読み続ける恐れがあるので、参照を外さずに残して記録する（leak を選ぶ。memory の破壊より安全）。
- `end_lease` が resource の destroy の後に来ても、参照は display が持つので解放の順は守られる。

### 4.6 panic の画面（M3）

- VF2 の text の ops は serial-console で `reveal_fatal` が無い（`serial-console.c:68-69`、`visionfive2.c:188`）。panic の文字は HAL の fbcons が bootfb に描くが、zero copy 中の DC8200 は render の buffer を出している。
- 解き方: **kernel の text の ops（`include/kern/text-display.h` の `reveal_fatal`、BUG-158 の hook）を VF2 で持たせる**。DC8200 の driver が「致命の時の戻し」の関数を登録し、VF2 の `reveal_fatal` がそれを呼ぶ: 割り込み禁止・lock 無しで snapshot の plane の register を書き、COMMIT を立てる（latch を待たない、待つ道が無い前提。数 ms の polling は上限付きで可）。その後に HAL の fbcons の描き直し（既存の reveal と同じ道）。snapshot は attach の時に作った不変の値なので lock が要らない。
- 変える所: VF2 の text の ops（`serial-console.c`・`visionfive2.c`、WS256 の source、依頼 R4）に `reveal_fatal` を足し、DC の戻しの関数への hook（kernel の中の小さい登録の関数）を通す。**HAL（hal.h・fbcons）は変えない**。
- 確かめ: p002 の host 試験（偽の窓で戻しの書き込みの順）と、実機の項目 U5（zero copy 中に panic を起こし画面に文字が出る）。

## 5. libvulkan の shared の道（M6）

- libvulkan が shared（zero copy）を選ぶ条件は全部（`context.c:180-210`）: vendor の capset の長さと magic、flag が**ちょうど** `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT`、WINQ の XML の版。STRICT_QUEUE・QUIESCE は意味の義務（queue の順序の厳守、quiesce の時に未完の仕事を残さない）を伴う。constraints の `GPU_SCANOUT_SHARED` は許可でしかない（`wsi-display.c:1286-1287`）。
- WS277 は libvulkan の選択の条件を変えない（D4 (a)）。PowerVR の native の renderer（WS269 p010・p011、WS270）に次を依頼する（R6・R7、§9）: 正確な capset と義務、export の scanout の image を PBE の LINEAR・stride 32 byte の倍数（幅を揃えて確保）で書く、shareable の SLC（R3）、placed の確保（R5）。
- `display_placement` は alignment 0 を渡す（`wsi-display.c:1940-1953・1981`）ので、stride の揃えは image の layout（WS269 p010）が担う。

## 6. ユーザーの判断（D1…）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 最初の到達点 | (a) U-Boot の mode の引き継ぎで plane を書き換える（p002・p003）を先に作り、mode の設定（p004・p005）は後 / (b) 最初から clock・PLL・HDMI・EDID まで | (a)。**go/no-go**: 実機の配布の U-Boot が HDMI と GOP を出し、ExitBootServices の後も DC8200 が動き、plane の ADDRESS が GOP の base と一致する事（WS256 p011 の実機の目視待ち、未確認）。外れたら (a) は成り立たず、p004（自前の mode の設定）が先になる。**効果（fps）の測定は p006 で、WS269 p010 の後**（GPU が線形で描いた frame はそれまで無い）。p002・p003 は CPU が書いた blob で zero copy の仕組みだけを確かめる |
| D2 | DC8200・HDMI の発見 | (a) `visionfive2.c` の組み込みの記述（DT があれば優先） / (b) DT 必須 | (a)（mainline の DTS に node が無い） |
| D3 | voutcrg・PD_VOUT の provider（p004） | (a) WS256 の `vf2-crg` に voutcrg、`vf2-pmu` に PD_VOUT を足す（R1・R2） / (b) DC8200 の driver が直に叩く | (a)（provider を 1 つに保つ）。p004 の前に決めれば足りる |
| D4 | libvulkan の shared の道の条件 | (a) PowerVR の renderer が正確な capset と義務を満たす（R6・R7） / (b) libvulkan の条件を変える（別の WS） | (a) |
| D5 | HDMI の audio | (a) 範囲外（Future Work） / (b) 含める | (a) |
| D6 | 解像度の範囲 | (a) PLL の表の mode（≤297 MHz）だけ / (b) 表に無い mode の計算 | (a) |
| D7 | mode を選ぶ経路（p004・p005） | (a) p004 は boot parameter、UAPI の経路は i915 の調査の後に p005 で決める / (b) 今 UAPI に mode の設定の op を足す / (c) present の frame の大きさで選ぶ | (a)（UAPI を急いで変えない） |
| D8 | GPU の SLC（B2） | (a) shareable の blob を常に SLC_BYPASS（R3） / (b) flip の前に SLCFLUSHINVAL | (a)（display から FW への依存を作らない） |
| D9 | 小さい frame（B3） | (a) mode と同じ大きさだけ zero copy、ほかは copy の道 / (b) SIZE・位置・背景色を設定して小さい frame も zero copy | (a)（p002 の範囲を小さく保つ。(b) は Future Work の候補） |

## 7. Phase の分割（案、LW の見積もり直し）

LW は 1 LW ≈ 実時間 15〜25 分、±50%。

| Phase | 中身 | 依存（出力の範囲） | LW | 確認の点 |
| --- | --- | --- | --- | --- |
| p002 | 引き継ぎの道: DC の窓の写像、model・revision・CID、plane・panel の特定と判定（§2.4）・snapshot、query（SIZE・60 Hz）、import の検査（§4.2）、present（BLOB）の zero copy（mode と同じ大きさだけ）: format・swizzle・blend 無効・commit・polling の latch の待ち（上限付き）、front の参照の移し替え（§4.5）、release・close の順、COPY との行き来、`reveal_fatal` の戻しの関数（§4.6、VF2 の text の ops の側は R4） | WS269 p007 の import の code（merge 済み、出力の範囲）、WS269 p006 の lookup、R4、D1 の go/no-go | 16〜24 | build（warning 0）、host 試験（偽の窓: 判定・commit の書き込みの順・範囲の検査・release の順・panic の戻し）。実機は §10 の U1〜U5 |
| p003 | vblank の IRQ 95、UNGATED と adoption_wait（§4.3）、pending の参照、refresh の実測、underflow の記録 | p002 | 8〜12 | host 試験（IRQ の偽の起床、参照の外す文脈）。実機 U6 |
| p004 | 自前の mode の設定: voutcrg・PD_VOUT（R1・R2）、clock・reset の順（Procedure referenced）、DSS の syscon の bit（TRM で確かめる）、timing、HDMI の PHY の PLL の表、boot parameter の mode（D7 (a)） | p003、R1・R2、D3・D7 | 14〜22 | 実機 U7 |
| p005 | DDC・EDID、mode の一覧（ENUMERATE・VALIDATE）、D7 の UAPI の経路（決まれば） | p004、D7 | 8〜14 | 実機 U8 |
| p006 | zero copy の通し: GPU が書いた non-mappable な線形の image（WS269 p010）で古い画が出ない事（B2 の確かめ）、Keiland の desktop の fps（copy の道と比較） | p003、R3・R5・R6・R7、WS269 p010・p011、WS270 の該当 Phase | 4〜8 | 実機 U9・U10 |
| p007 | 規約の全文の見直し | 全部 | 2〜3 | build（warning 0）、host 試験 |

- 合計 52〜83 LW（版 1 は 40〜65）。p002 の増分は B1・B3・M2・M3・M4・M5 の分。
- 実機の確認は VF2 で、T1 への依頼か §10 のユーザーの試験の一覧で行う（QEMU に DC8200 は無い。QEMU で通るのは host 試験と fallback の道だけ）。serial・console の log では判定しない。

## 8. HAL

- HAL の API の変更は要らない: 物理は scanout の backing の page の配列（`gpu-scanout.h:29-35`）、DC の窓と IRQ は既存の kernel の写像・IRQ の API、panic は kernel の text の ops の `reveal_fatal`（§4.6）。HAL の fbcons の側で解く設計にはしない（すれば HAL の責務の変更で承認が要る）。`proposed/` は置かない。

## 9. 他 WS への依頼の表（Q1 が各 WS の Phase へ渡す）

| ID | 相手 | source | 中身 | WS277 の使う Phase | 理由 |
| --- | --- | --- | --- | --- | --- |
| R1 | WS256 | `src/drivers/platform/visionfive2/vf2-crg.*` | voutcrg（0x295C0000）の provider を足す（DC8200・HDMI の clock・reset、`hdmitx0_pixelclk` の外の clock の扱い） | p004 | D3、§2.2 |
| R2 | WS256 | `vf2-pmu.*` | `DRV_VF2_PD_VOUT` を EPERM でなく受ける（今は PD_GPU だけ、`vf2-pmu.h:41`） | p004 | M9 |
| R3 | WS269（render.c、p010 の前か中） | `src/drivers/gpu/powervr/render.c` | `GPU_BLOB_SHAREABLE` の blob を作成の時から常に SLC_BYPASS で GPU MMU に写す（mappable でなくても）。不可なら代替として flip の前の SLCFLUSHINVAL の道を WS277 と調整 | p006（p002 は CPU の mappable で迂回済み） | B2、D8 |
| R4 | WS256（VF2 の platform） | `visionfive2.c`・`serial-console.c` | DC8200（0x29400000・0x29400800、IRQ 95）・HDMI（0x29590000、IRQ 99）の組み込みの記述。VF2 の text の ops に `reveal_fatal` を足し、display の driver が登録する「致命の時の戻し」を呼ぶ | p002 | D2、M3 |
| R5 | WS269 | `render.c`（render node の ops） | `blob_create_placed` を足す（`CONTIGUOUS|DMA32`・上限 0xffffffff を検査して受ける。全部が既に連続・32 bit なので検査だけ） | p006（p002 は placement 0） | M1 |
| R6 | WS269 p010・p011 | PowerVR の Vulkan の image・render pass | export する scanout の image を PBE `MEMLAYOUT_LINEAR` で書き、stride を 32 byte の倍数（かつ 2 pixel）に揃えて確保 | p006 | §3.1、M6 |
| R7 | WS269 p010・WS270 | PowerVR の native の renderer の capset | libvulkan の条件（`context.c:180-210`）に合う capset（長さ・magic・flag がちょうど `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT`・WINQ の XML の版）と STRICT_QUEUE・QUIESCE の義務 | p006 | D4、M6 |

- Awesome Plan §5 により、Q1 が R1〜R7 の各々を相手の Phase への変更（foreign Phase の comment と依存）として出す。WS277 はこれらの source を変えない。

## 10. 実機の確かめ（ユーザーの試験の一覧の形、今は頼まない）

VF2 の板の接続と実機の判定の方法は WS256 の #10・#15 で未決。それが決まり、該当の Phase が実装された後に、一覧に載せる（T1 が流せる物は T1 へ）。

| ID | Phase | 手順 | 合格 |
| --- | --- | --- | --- |
| U0 | D1 | 配布の U-Boot で起動し、kernel の記録の DC の model・revision・plane の判定の結果を画面に出す | HDMI に GOP の画が出ていて、判定が「引き継げる」 |
| U1 | p002 | 試験の program が mappable な blob に色の帯を書き、present。その後 present せずに CPU が blob を別の色に書き換える | 書き換えが present 無しで画面に出る（zero copy の時だけ変わる。copy の道と区別できる） |
| U2 | p002 | 赤・緑・青の帯で BGRA と RGBA の両方 | 色が合う |
| U3 | p002 | 2 つの buffer を交互に present し、present が返った直後に前の buffer へ別の模様を書く（libvulkan と同じ時点） | 崩れ・tearing が出ない（B1） |
| U4 | p002 | 画面の 1 行に plane の ADDRESS の読み戻しを出す。release で console に戻る。小さい frame は copy の道で出る | ADDRESS が blob の物理、release で console、小さい frame で範囲外の画が出ない（B3） |
| U5 | p002 | zero copy 中に panic を起こす（試験用の手段） | panic の文字が画面に出る（M3） |
| U6 | p003 | 交互の flip で wait の間隔を画面に出す | 60 Hz 前後、崩れ無し |
| U7 | p004 | boot parameter で 720p | 720p の画が出る |
| U8 | p005 | EDID の mode の一覧を画面に出し、選んだ mode | 一覧が出て、その mode で画 |
| U9 | p006 | GPU が描いた non-mappable な image を続けて present | 古い画が混ざらない（B2） |
| U10 | p006 | Keiland の desktop の fps を copy の道と比べる | 数値の記録（ws.md 完了の条件 2） |

## 11. 未確認の一覧

- VF2 の DC8200 の model・revision・CID。HDMI が panel 0 か。
- U-Boot が HDMI と GOP を出すか、ExitBootServices の後も DC が動くか、GOP の base と plane の ADDRESS が一致するか（U-Boot が copy の framebuffer なら一致しない）。
- plane の address の上位 bit（vendor の 40 bit の DMA の mask との矛盾）。
- stride の 32 byte・先頭の揃えが HW の必須か、stride の register の幅。
- COMMIT bit が自己 clear するか、latch の時点、`PANEL_CONFIG_EX` bit 0 の要否。
- FW が完了時に SLC を書き出すか（R3 で迂回すれば関係しない）。SLC が DC8200 から見えない事（推測）。
- DSS の syscon の 0x4 bit 20・0x8 bit 3 の意味（TRM）。
- i915 などが mode をどう選ぶか（D7）。
- mainline の `verisilicon,dc` が JH7110 で動くか。
- vendor の Linux の GNOME が遅かった原因。
- Future Work の候補: 小さい frame の zero copy（SIZE・位置・背景色、D9 (b)）、overlay・cursor の plane、HDMI の audio（D5）、hotplug（IRQ 99）。

## 12. レビュー 1 への対応

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| B1 flip の完了と hold の解放 | present は latch の後に返る契約。p002 は polling（COMMIT の自己 clear か CURRENT_LOCATION、上限付き）、p003 で UNGATED・adoption_wait。実機 U3 | §1.6、§2.6、§4.3、§10 |
| B2 GPU の SLC | shareable の blob を常に SLC_BYPASS（R3、D8）、代替は SLCFLUSHINVAL。試験は GPU が書いた non-mappable な image（U9、p006） | §1.7、§3.2、§9 |
| B3 範囲外の読み | zero copy は mode と同じ大きさだけ、ほかは copy の道（D9 (a)）。DC の読む範囲で検査 | §1.8、§4.2、U4 |
| M1 placement と ENOTSUP | R5 が main に入るまで placement 0、import で連続・上限を検査 | §4.2、§9 R5 |
| M2 release・close | 参照を display へ移す、snapshot へ戻す → commit → latch → 参照を外す、thread の文脈だけ、vblank が来ない時の上限と leak の選択 | §4.5、§2.6 |
| M3 panic の画面 | VF2 の text の ops の `reveal_fatal` で snapshot へ戻す（kernel の中、HAL は変えない）、R4、U5 | §4.6、§8 |
| M4 plane・panel・commit | 2 つの plane と panel、DISPLAY_ID・FB_EN・DEC、判定の条件の全部、model 0x20、mainline の COMMIT の手順、PANEL bit 0 は p002 で確かめる | §2.4、§2.6 |
| M5 format と alpha | X 系・blend 無効、format・swizzle を書く。§1.5 を直した | §1.5、§2.5 |
| M6 D4 の条件 | capset の条件の全部と義務、R6・R7 | §5、§9 |
| M7 D1 の根拠・p002 の試験 | D1 に GOP の go/no-go と測定は p006 を明記。試験を U1（present 無しの変化）・U3・U4 に | §6 D1、§10 |
| M8 mode の経路・refresh | UAPI に設定の op が無い事を明記、D7、refresh は実測か 60 Hz（DT の 297 MHz は使わない） | §2.6、§4.4、§6 |
| M9 D3 の範囲 | vf2-pmu を含めた（R2）、他 WS の依頼の表 | §2.2、§6 D3、§9 |
| Minor 1 FOREIGN | 外さない | §4.2 |
| Minor 2 2 pixel | 確定（bxe-4-32.h） | §1.1、§0 S4 |
| Minor 3 零埋め | 確定済み（render.c:322-333） | §3.2 |
| Minor 4 mainline の DTS・CID | S3 を直した | §0 |
| Minor 5 offset の揃え | offset 0 だけ、offset_alignment は出さない | §3.1 |
| Minor 6 lock と IRQ | p002 は mutex を持って polling（ACK を読まない）、p003 は UNGATED、IRQ の中で参照を外さない | §2.6、§4.3、§4.5 |
| Minor 7 p007 の依存 | 「import の code（merge 済み）」の出力の範囲の依存 | §7 |
| Minor 8 LW | 見積もり直し（p002 16〜24、合計 52〜83） | §7 |
| Minor 9 実機の確認の書き方 | ユーザーの試験の一覧（U0〜U10）、T1 か一覧、板の接続は WS256 #10・#15 待ち、log で判定しない | §7、§10 |
| Minor 10 stride の幅 | ≤ 65535 を検査 | §3.1、§4.2 |
| Minor 11 ws.md 完了の条件 2 | fps の測定が WS269 p010・p011・WS270 に依る事を ws.md に書いた | ws.md |
