<!-- awesome-plan project=zedbsd record=ws217-p009 -->

# ws217-p009: scanout の 180°（kernel の UAPI・i915・libvulkan）

Parent: [WS217](../ws.md)
Status: planned（最初の段「Intel の公開の PRM で 180° の plane の回転を確かめる」は 2026-10-13 P2 が済ませた（§1〜§4）。実装は p001 第 4 版で review-3 の S10・S11・M17 を直した後）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue / attempts: Q1 の指示（2026-10-13、P2「p009 の最初の段（Intel の PRM の確認）だけ」。review-3 の判定で、この段だけ今始めてよい）。部分の範囲: PRM の確認と、tree の MIT の参照・zedBSD の i915 との照合だけ。コードは書いていない。
Purpose / goal: p001 第 3 版 §5 の設計どおり、表示の hardware の回転（plane の 180°）で 180° の scanout を出す。内容と受け入れの条件は [p001](../phase001/phase.md) の §5・§13（p009 の行）にあり、第 4 版で更新される。
Prerequisites: p001（第 4 版、review-3 の S10・S11・M17 の直し）。この段は前提なし。

## 1. 最初の段の結論

- **180° は linear（未 tile）の XRGB8888 で可**（Tiger Lake = display version 12、Alchemist = display version 13 の公開の PRM。Alder Lake-P の PRM は公開されていない、§2）。90°・270° は display version 12 で Y tile が要り、display version 13 の `PLANE_CTL` には値そのものが無い。
- **180° の時に `PLANE_SURF`・`PLANE_OFFSET`・`PLANE_POS` を補正しなくてよい**。PRM の `PLANE_OFFSET` の説明: 180° では hardware が offset に plane の大きさを足して、画像の右下から表示を始める。zedBSD の全画面の plane（offset (0,0)、位置 (0,0)、大きさ = pipe）は今の値のままでよい。設計の p001 §5.2「surface の offset は 180 で変えない」と一致する。
- `PLANE_CTL` の bit 1:0 の値（00b = 0°、01b = 90°、10b = 180°、11b = 270°、時計回り）は、zedBSD の `PLANE_CTL_ROTATE_*`（`src/drivers/gpu/i915/intel/plane.h:232-236`）と一致する。
- PSR・FBC: 0°/180° の回転は FBC と両立する（両方の PRM）。PSR2 の selective fetch は plane の回転と両立しない（両方の PRM）。zedBSD は PSR も FBC も有効にしないので、今は当たらない（§4）。
- **p009 を止める理由は見つからなかった**（p001 §5.2「PRM が違うことを言えば p009 を止めて Q1 に報告」には当たらない）。

## 2. 使った資料（Intel の公開の PRM）

Intel の Graphics for Linux の developer reference の一覧（https://www.intel.com/content/www/us/en/docs/graphics-for-linux/developer-reference/1-0/hardware-specs.html と、Alchemist/Arctic Sound-M の頁）にある PDF。2026-10-13 に取得した。

| 略号 | 資料 | URL |
| --- | --- | --- |
| TGL-2c | Intel Iris Xe and UHD Graphics Open Source PRM for the 2020-2021 11th Generation … "Tiger Lake" Platform, **Volume 2c: Command Reference: Registers, Part 2 – Registers M through Z**, December 2021, Revision 1.0（Doc Ref # IHD-OS-TGL-Vol 2c-12.21） | https://cdrdv2.intel.com/v1/dl/getcontent/703047 |
| TGL-12 | 同じ Tiger Lake の PRM の **Volume 12: Display Engine**, December 2021, Revision 1.0（Doc Ref # IHD-OS-TGL-Vol 12-12.21） | https://cdrdv2.intel.com/v1/dl/getcontent/705833 |
| ACM-2c | Intel Arc A-Series Graphics and Intel Data Center GPU Flex Series Open-Source PRM for "Alchemist" and "Arctic Sound-M", **Volume 2c: Command Reference: Registers, Part 2 – Registers L through Z**, March 2023, Revision 1.0（Doc Ref # IHD-OS-ACM-Vol 2c-3.23） | https://cdrdv2.intel.com/v1/dl/getcontent/774482 |
| ACM-12 | 同じ Alchemist の PRM の **Volume 12: Display Engines**, March 2023, Revision 1.0（Doc Ref # IHD-OS-ACM-Vol 12-3.23） | https://cdrdv2.intel.com/v1/dl/getcontent/774520 |

- **Alder Lake-P（5330、display version 13）の PRM は公開されていない**（上の一覧に Alder Lake・Raptor Lake・Meteor Lake・Arrow Lake・Lunar Lake の頁が無く、web の検索でも見つからなかった）。代わりに、同じ display version 13 の Alchemist（DG2）の PRM を使った。ADL-P と DG2 が同じ display version 13 であることは、tree の MIT の参照 `plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/intel_display_device.c:682`（`ip.ver = 13` の XE_LPD の族）・`:690`（`xe_lpd_display`）・`:703`（`xe_hpd_display`）・`:832`（`INTEL_ADLP_IDS(&xe_lpd_display)`）・`:835`（`INTEL_DG2_IDS(&xe_hpd_display)`）で確かめた。**ADL-P そのものの記述は未確認**（Alchemist は ADL-P と同じ display version の別の製品で、plane の数などが違う）。
- Meteor Lake・Arrow Lake（display version 14）: 公開の PRM が無く、**未確認**。tree の参照は display version 13 以上で 0°・180° だけ（`skl_universal_plane.c:2402-2403`）とし、display version 14 の Meteor Lake（`intel_display_device.c:759, 849` の `xe_lpdp_display`）もここに入る。
- 頁は「印刷の頁（PDF の頁）」で書く。引用は英語の原文のまま。

## 3. PRM の記述

### 3.1 PLANE_CTL の Plane Rotation の field

| 項目 | TGL（display version 12） | Alchemist（display version 13） |
| --- | --- | --- |
| 位置 | TGL-2c `PLANE_CTL`、753（PDF 783）: "1:0 Plane Rotation — This field controls hardware rotation of the plane." | ACM-2c `PLANE_CTL`、1127（PDF 1165）: 同じ文、"Access: Double Buffered" |
| 値 | 00b No rotation、01b 90 degree rotation、10b 180 degree rotation、11b 270 degree rotation | **00b No rotation、10b 180 degree rotation だけ**（01b・11b は表に無い） |
| Restriction | "90/270 degree rotation requires the surface to be Y Tiled. Interlaced mode is not supported with 90/270 degree rotation. Render-Display decompression is not supported with 90/270 degree rotation. 90/270 rotation is supported with plane width (pre-rotation) up to 4096 pixels."（753） | 90/270 の値が無い。ACM-12 237（PDF 241）: "90/270 rotation not supported."、238（PDF 242）: 同じ文 |
| Programming Note | "Hardware does not change the plane position when rotation is enabled. Software may need to adjust the plane position to match the physical orientation of the display."（753） | 同じ文（1127） |
| 二重 buffer | `PLANE_CTL` は "Double Buffered"、"_Custom_Display_DoubleBufferArmedBy: Write to PLANE_SURF or plane not enabled"、update point は "Start of vertical blank"（TGL-2c 745、PDF 775） | 同じ（ACM-2c の `PLANE_CTL` の頭） |

- 二重 buffer の記述から、回転の bit は `PLANE_SURF` の書き込み（flip）で次の vblank に入る。frame ごとに回転を変えても modeset は要らない（p001 §5.2 の「PLANE_CTL は flip ごとに計算して書く、noarm → arm」と合う）。
- Horizontal Flip（bit 8）: "Horizontal flip is not supported with linear surface formats."（TGL-2c 752、ACM-2c 1126）。180° は flip ではなく回転の field で出すので当たらない。

### 3.2 linear と 180°

- TGL-12 289（PDF 293）"Plane Feature Interoperability" の表（Display Features / Surface Formats）: 行 "Linear - 0/180 rotation" は RGB32 8:8:8:8 を含む全部の format に印がある。行 "Linear - 90/270 rotation" には印が無い。"Y Tiling (legacy) - 90/270 rotation" には RGB32 8:8:8:8 などに印がある（表は画像で、PDF の頁を描いて目で読んだ）。
- TGL-12 290（PDF 294）"Tiling Modes / Display Features" の表: Linear は "Rotation 0/180" に印、"Rotation 90/270" に印が無い。Y Tiling Legacy は "Rotation 90/270" に印と "(some surface formats not supported)"。
- ACM-12 237（PDF 241）: "All surface formats support 0 and 180 degree rotation with all tile formats." "Rotation 0 and 180 degrees supported with all tile modes. Rotation 0 and 180 degrees supported with interlacing, frame buffer compression, and render/media/unified compression."
- TGL-12 293（PDF 297）"Plane Rotation Programming"（ACM-12 240、PDF 244 も同じ文）: "The 180 rotation mode is unchanged and will continue to use the same programming. In the 180 rotation mode display hardware is responsible for walking the pages in the reverse order. The cacheline walk within the page is also reversed. The 90 and 270 rotation modes require more complicated page walk mechanism. The page walk is made transparent to the hardware by providing a different set of page translations (remapped GTT) …"

→ 180° は linear で可、プログラムの仕方は 0° と同じ（回転の field だけ）。90°・270° は display version 12 では Y tile と remapped GTT（回転した GTT の view）と surface height が要り、display version 13 には無い。p001 の決定（90°・270° は compositor の turn pass、Y-tiled は Future Work）はこれと合う。

### 3.3 surface address・offset・position の補正

- TGL-2c `PLANE_OFFSET` 789（PDF 819）: "This register specifies the panning for the plane surface. The start position is specified in this register as a (x, y) offset from the beginning of the surface. **When performing 180 rotation, hardware will internally add the plane size to the offsets so the plane will start displaying from the bottom right corner of the image.**" Restriction: "Plane Size + Plane Offset should not exceed the surface <stride/width> (in pixels)"。ACM-2c `PLANE_OFFSET` 1159（PDF 1197）も同じ文。
- TGL-2c `PLANE_POS` 796（PDF 826）: "When performing rotation, the plane image is rotated by hardware, but the position is not, so it must be adjusted if it is desired to maintain the same apparent position on a physically rotated display." ACM-2c `PLANE_POS` 1167（PDF 1205）も同じ文。
- `PLANE_SURF`（TGL-2c 840、PDF 870）の field の説明に回転の記述は無い（Surface Base Address は回転と無関係）。

→ 180° で software が足す補正は無い（hardware が offset に plane の大きさを足す）。`PLANE_POS` を変えるのは pipe の一部だけを覆う plane を物理の向きで同じ所に見せたい時だけで、zedBSD の primary は pipe 全体（位置 (0,0)、大きさ = pipe）なので、回しても同じ矩形になる。

### 3.4 PSR・FBC・その他の制約

| 機能 | PRM の記述 | zedBSD の今 |
| --- | --- | --- |
| FBC | TGL-12 290（PDF 294）"Rotation Modes / Display Features" の表: "0/180 rotation" は Frame Buffer Compression に印（"90/270 rotation" は "RGB 16 bit format not supported"）。同じ頁の "Tiling Modes / Display Features" の表で Linear は Frame Buffer Compression に印。TGL-12 289 の "Frame Buffer Compression" の行は RGB32 8:8:8:8 と RGB 16-bit 5:6:5 に印。ACM-12 237: "Rotation 0 and 180 degrees supported with … frame buffer compression"、"Frame Buffer Compression (FBC) supported only with RGB32 8:8:8:8 without per-pixel alpha and RGB 16-bit 5:6:5." | FBC を使わない。takeover で firmware の FBC を止める（`src/drivers/gpu/i915/display/takeover.c:2298` が呼ぶ `i915_nogem_fbc_sanitize()`、`:5797-5825` で `DPFC_CTL_EN` を消す）。当たらない |
| PSR1 | TGL-12 の "Panel Self Refresh" の節に回転の記述は無い（"rotat" の全文の検索で PSR の節に出ない）。**明記が無いので未確認**（禁止の記述も無い） | PSR を有効にしない（`crtc_state->has_psr` は立たない、`intel_psr_disable` は `display/modeset-internal.h:824-825` の guard）。当たらない |
| PSR2 の selective fetch | TGL-12 346（PDF 350）"Selective Fetch" の Restrictions: "Not supported with plane rotation. Software calculations to adjust update region coordinates are too complicated."。ACM-12 295（PDF 299）も同じ文 | `enable_psr2_sel_fetch` を立てる code が無い（`display/plane.c:1210, 1361, 1383` は読むだけ）。当たらない。**将来 PSR2 の selective fetch を入れる時は、180° の plane で止める**こと |
| render・media の圧縮 | 0°/180° は可、90°/270° は不可（TGL-2c 751・ACM-2c 1124 の Render Decomp の Restriction "Decompression is not supported with 90/270 degree rotation."） | 圧縮を使わない（linear だけ） |
| 2 つの pipe に分けた画像（pipe joiner） | TGL-12 266（PDF 270）・ACM-12 の同じ節 "Rotation": 回転では左右の `PLANE_OFFSET`・`PLANE_SIZE` の割り当てを入れ替える | 使わない（`plane_count = 1`、joiner 無し）。当たらない |
| interlace | ACM-12 237: 0/180 は interlacing と可。TGL-12 290 の表も 0/180 と Interlacing に印 | interlace の mode を使わない |

- DMC・DC 状態（DC5/DC6・DC3co）と frame ごとの回転の切り替えの相互作用は PRM に記述が見当たらず、**未確認**（p001 review-3 の残る点。実機 5330・5320 の受け入れで見る）。

## 4. tree の MIT の参照と zedBSD の i915 との照合

参照: `plan/ws031/linux-parity/linux-reference/ubu-i915-src/display/`（SPDX MIT、Linux 6.8 系の Ubuntu の i915）。zedBSD: `src/drivers/gpu/i915/`（main 0992c5791 を merge した agent/p2-ws215、`display/plane.c` の最後の変更は 846464d1e）。

| 点 | PRM | MIT の参照 | zedBSD | 判定 |
| --- | --- | --- | --- | --- |
| `PLANE_CTL` の回転の field の位置 | bit 1:0（TGL-2c 753、ACM-2c 1127） | — | `PLANE_CTL_ROTATE_MASK REG_GENMASK(1, 0)`（`intel/plane.h:232`） | 一致 |
| 180° の値 | 10b | `skl_plane_ctl_rotate()` の `DRM_MODE_ROTATE_180` → `PLANE_CTL_ROTATE_180`（`skl_universal_plane.c:841-842`） | `PLANE_CTL_ROTATE_180 = REG_FIELD_PREP(mask, 2)`（`intel/plane.h:235`）、`i915_skl_plane_ctl_rotate()` の `MODE_ROTATE_180` → `PLANE_CTL_ROTATE_180`（`display/plane.c:793-794`） | 一致 |
| 90°・270° の値（向き） | 01b = 90、11b = 270（hardware は時計回り。TGL-2c 753） | DRM の 90（反時計回り）→ `PLANE_CTL_ROTATE_270`、270 → `_90`（`skl_universal_plane.c:837` の注記） | `MODE_ROTATE_90` → `PLANE_CTL_ROTATE_270`、`_270` → `_90`（`display/plane.c:779-796`）、値は 1 と 3（`intel/plane.h:234, 236`） | 一致（WS217 は使わない） |
| 0° の値 | 00b | — | `PLANE_CTL_ROTATE_0 = 0`、`MODE_ROTATE_0` は 0 を返す（`display/plane.c:789-790`） | 一致 |
| display version 13 以降の 90/270 | 値が無い（ACM-2c 1127、ACM-12 237-238） | `DISPLAY_VER >= 13` は `ROTATE_0 | ROTATE_180` だけ（`skl_universal_plane.c:2402-2403`）、12 以前は 4 つ（`:2404-2407`） | zedBSD は回転の property を作らず、`hw.rotation` は `MODE_ROTATE_0` 固定（`display/plane.c:293, 378`）。`i915_skl_plane_ctl_rotate()` は 90/270 を display version で断らない | 一致（p009 の UAPI は 180 だけ、p001 D16。実装の時に `hw.rotation` へ入るのは 0 か 180 だけにする） |
| 180° と linear | Linear - 0/180 に印（TGL-12 289-290）、全部の tile で 0/180 可（ACM-12 237） | `skl_plane_check_fb()` が linear で断るのは `REFLECT_X`（`:1431-1436`）と 90/270（Y/Yf が要る、`:1438-1443`）だけ。180 は断らない | 今の scanout は linear XRGB8888 だけ（`display/plane.c:233-236, 335-338`）。`i915_skl_plane_ctl_tiling()`（`:733-776`）は linear で 0 | 一致 |
| 180° の offset の補正 | 要らない（hardware が plane の大きさを足す、TGL-2c 789、ACM-2c 1159） | `skl_check_main_surface()` は 180 で x・y を変えない（`skl_universal_plane.c:1691-1756`）。補正を足すのは GMCH 系の `i9xx_plane.c:292-303`（Haswell・Broadwell 以外の旧い plane、"HSW/BDW do this automagically in hardware"）だけ。`intel_plane_check_clipping` の `drm_rect_rotate`（`intel_atomic_plane.c:918, 936`）は全画面の src を同じ矩形に写す | `view.color_plane[0].x/y` は memset の 0 のまま（`display/plane.c:265, 358` の memset、`:1306-1307` で `PLANE_OFFSET` に書く）、`PLANE_SURF` は `ggtt_offset`（`:301, 386`） | 一致（補正は要らない） |
| 180° の stride | 0° と同じ（TGL-12 293 "unchanged … same programming"） | `skl_plane_stride_mult()` は `drm_rotation_90_or_270` だけ見る（`skl_universal_plane.c:587-597`） | `i915_skl_plane_stride_mult()` も 90/270 だけ（`display/plane.c:569-589`） | 一致 |
| 180° の plane の位置 | hardware は位置を回さない（TGL-2c 753・796） | — | 全画面の plane の dst は (0,0)〜(W,H)（`display/plane.c:290-291, 375-376`） | 一致（全画面なので補正不要） |
| `PLANE_CTL` の書き込みの時 | 二重 buffer、`PLANE_SURF` の書き込みで arm（TGL-2c 745） | `icl_plane_update_arm()` が `PLANE_CTL` の後に `PLANE_SURF` | `i915_icl_plane_update_noarm()` → `_arm()`（`display/plane.c:308-309, 407-416, 480-482`） | 一致 |
| FBC と 180 | 可（TGL-12 290、ACM-12 237） | `skl_fbc_rotation_is_valid()` は RGB565 の 90/270 だけ断る（`intel_fbc.c:996-1006`） | FBC は使わない | 当たらない |
| PSR2 selective fetch と回転 | 不可（TGL-12 346、ACM-12 295） | 6.8 の参照の `intel_psr.c` に回転の検査は無い（"rotat" の検索で出ない） | selective fetch を有効にしない | PRM と参照が食い違うが、zedBSD は当たらない |

不一致は無かった。注記の更新（`display/plane.c:199` の "rotation 0"、`internal.h` の「rotation 0」の注記）は p009 の実装の段で行う（p001 §5.2）。

## 5. 実行したこと

- web の検索（Intel の公開の PRM の所在）と、上の 4 つの PDF の取得（`curl`、session の scratchpad に置いた。tree には入れない）。`pdftotext -layout` で本文を読み、表が画像の頁（TGL-12 の 289・290）は `pdftoppm -r 80` で描いて目で読んだ。
- tree の MIT の参照と zedBSD の i915 の source を `grep`・`sed` で読んだ。GPL の source・`.internal/` は読んでいない。
- コードの変更・build・試験は無い（この段の範囲外）。

## 6. 未確認の点

- Alder Lake-P（5330）そのものの PRM の記述（非公開。同じ display version 13 の Alchemist で代えた）。
- Meteor Lake・Arrow Lake（display version 14）の PRM（非公開）。
- PSR1 と plane の回転の関係（PRM に記述が無い）。5330・5320 の内蔵 panel（eDP）は firmware が PSR を使っていても zedBSD は PSR を有効にしないが、受け入れ（p001 §12.3）で内蔵 panel と HDMI の両方を見る。
- DMC・DC 状態と frame ごとの回転の切り替え（実機で見る）。
- 実機での 180° の向き（hardware が時計回り 180° = 上下左右の反転の組なので向きの取り違えは起きないが、観測はまだ無い）。

## 7. 残り（実装の段）

p001 第 4 版（review-3 の S10: i915 の 180 は present の item で運び、release・hold・stop で 0 に戻す、S11: surface の transform は scanout が当て capabilities と swapchain は IDENTITY のまま、M17: 核の `ROTATE_180` を driver が一様に断る形）の後に、p001 §5・§13 の p009 の行の内容を実装する。実装の時に `hw.rotation` へ入れる値は `MODE_ROTATE_0` か `MODE_ROTATE_180` だけにする（display version 13 に 90/270 の値が無いため）。
