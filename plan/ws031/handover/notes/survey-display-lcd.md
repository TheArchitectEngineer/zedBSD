# WS031 表示／LCD 事前調査（2026-09-18、読み取りのみ）

目的: 対象 LCD 1 枚・1 mode を zedBSD から点灯するための、(1) 対象機の事実、(2) 正本（Linux 6.8.12 `display/`）の呼出し順と parity 側の現状の対応表、(3) probe 時の readout／sanitize と firmware が pipe を active で残した場合の未対応、(4) 参照データの採取計画、(5) 既存の scanout／framebuffer 関連コード。ファイルは一切変更していない。GPU の vfio-pci バインドもそのまま。

結論: **現状の parity 側は panel を点灯できる段階にない。** VBT parser は BDB block を数えるだけで child device を取り出さないため、bare metal で本物の VBT が得られると出力が 0 個になる。表示の書込み側（PLL／DDI／transcoder／pipe／plane）は全て未実装。

## 1. 対象機の事実

| 項目 | 事実 |
|---|---|
| KVM ホスト | **Dell Latitude 5330**（board 007WCW、BIOS 1.31.1 2025-07-04、chassis_type 10 = Notebook）。**native 試験はこの機体を zedBSD で直接起動する＝その間 KVM 試験環境は使えない** |
| 内蔵 panel | あり（laptop: BAT0、lid、ACPI BGRT）。BGRT のロゴ 345×323 が x=787 にあり 2×787+345=1919 → firmware は幅 1920 の mode。解像度（おそらく FHD）は推定で、読んだ値ではない |
| 外部出力 | TB4 root port 2、UCSI USB-C 2。HDMI の配線（combo port B か TC か）は VBT 無しでは不明 |
| GPU | 8086:46a8 rev 0c（ADL-UP3 GT2）、subsystem 1028:0b02、driver=vfio-pci（不変） |
| ホスト上の EDID／connector | **無し**（`/sys/class/drm` は `version` のみ、`/dev/dri` 無し、backlight 無し。GPU が vfio-pci のため）。root 無しでは PCI config 64 byte までしか読めず、ASLS(0xFC) は未読 |
| OpRegion／VBT（VFIO 下） | 台帳 E-53: `x-igd-opregion=on,rombar=0` では guest の **ASLS=0** → OpRegion も OpRegion VBT も無い。`rombar=0` で PCI ROM の `$VBT` も無い。E-96: `opregion=0` |
| OpRegion／VBT（bare metal） | firmware が ASLS を設定するので両方ある。parity は署名と version の検証のみ（`parity/probe.c:610-660`）。mailbox 4 の VBT は保持しない（`bios.c:275-279`） |
| VFIO 下の Linux guest | 既存の `linux-console.log` に `fb0: i915drmfb frame buffer device`。fbdev は接続済み connector がある時だけ登録されるので、Linux は VFIO 下でも missing-defaults VBT で port A の eDP を検出した可能性が高い。ただし connector 名や drm.debug ログは残っておらず**未確認** |

移植台帳 `linux-parity/ledger.md` の P2.8／P3／P5 行は古い（UNIMPLEMENTED のまま）。現状の正は `results-ws031.md` の E-86〜E-96。

## 2. 「panel を 1 枚点灯する」呼出し順と parity の現状

正本は `display/` 配下、parity は `src/drivers/gpu/i915/parity/` 配下。

| 段 | 正本 | parity | 状態 |
|---|---|---|---|
| VBT 取得 | intel_bios.c:3094 `intel_bios_init`、intel_opregion.c:880 | bios.c:248、bios.c:180（ROM）、probe.c:610 | **部分**: OpRegion VBT を複製しない |
| VBT parse | intel_bios.c:2719 `parse_general_definitions`、:2679 `parse_ddi_port` | bios.c:92 `parity_bios_process_vbt` | **部分**: BDB block を数えるだけ。child device を取り出さない → 本物の VBT で encoder 0 個 |
| panel block | :794 panel_options、:1393 `parse_edp`、:1013 `parse_lfp_backlight`、:3159 `intel_bios_init_panel` | 無し | **無し** |
| missing defaults | intel_bios.c:2855 | bios.c:131 | 移植済み |
| outputs | intel_display.c:7534、intel_ddi.c:4864 `intel_ddi_init` | display_nogem.c:820、:728 | **部分**: 判定と encoder 記録のみ（A=eDP、B=DP+HDMI、C 不採用） |
| connector init | intel_ddi.c:4395/4625、intel_dp.c:6333 `intel_edp_init_connector`、:6521 | 無し | **無し**（E-90 でスコープ外） |
| AUX／DPCD | intel_dp_aux.c:237 `intel_dp_aux_xfer`、:774、intel_dp.c:3910 | AUX power well 記述（power_domains.c:193）と irq.c:264 の mask のみ | **無し** |
| GMBUS／EDID | intel_gmbus.c:868、:623 `do_gmbus_xfer` | display_nogem.c:405: pin 表と reset、`gmbus_adapters_unimplemented=1`（:444） | **部分**: 転送経路も EDID parser も無い |
| PPS | intel_pps.c:1679、:1599、:1420、:1483、:718 vdd_on、:921 on、:589 wait、:1050 | display_nogem.c:477-478: `pps_mmio_base=0x61200` のみ | **無し** |
| backlight | intel_backlight.c:1647、:1791、:1479 `cnp_setup`、:715、:780 | quirk 表のみ（display_state.c:605-670） | **無し** |
| link training | intel_dp_link_training.c:1405、:815、:966、intel_ddi.c:3577、:3627（DP_TP_CTL）、:1172 vswing | 無し | **無し**（buf-trans 表も無い） |
| DPLL | intel_dpll_mgr.c:4177 init、:3406 `icl_get_dplls`、:3214、:2604、:3662 write、:3862 enable、intel_ddi.c:1675 | display_nogem.c:261（7-PLL 表）、:1242 readout、:1400 sanitize-off、:943 clock gate | **部分**: compute、CFGCR 書込み、enable、DPCLKA 対応が無い |
| CDCLK | intel_cdclk.c | cdclk.c:406 `parity_bxt_set_cdclk`、:452、:517 | 移植済み（atomic の min-cdclk 計算は無い） |
| combo PHY | intel_combo_phy.c | combo_phy.c:180 | 移植済み（init／procmon のみ。lane power-down 無し） |
| pipe／transcoder | intel_display.c:1633 `hsw_crtc_enable`、:2624、:2736、:3162、:392、intel_ddi.c:584、:2597、:3203 | readout のみ: display_nogem.c:1068、:1084 | **無し**（書込み側） |
| plane | skl_universal_plane.c:907、:1252 `icl_plane_update_noarm`、:1344 `_arm` | PLANE_CTL.ENABLE の readout のみ（display_nogem.c:1131） | **無し** |
| WM／DBUF | skl_watermark.c:2902、:1461、:2381、:3524、:2995 | wm latency／SAGV（display_nogem.c:160、:227）、DBUF slice 電源と MBUS（display_core.c:108-146）、`wm_hw_state_read=1` はフラグだけ（:1513） | **部分**: WM／DDB の計算・書込み無し |
| framebuffer pin | intel_fb_pin.c:239、:107。linear framebuffer は GGTT を使う（DPT ではない、intel_fb.c:767） | gt_mem.c:86 の GGTT 窓は **256 頁 = 1 MiB**（gt_mem.h:50） | **無し**: FHD の約 8 MiB には窓が小さい。stolen（DSM）領域も無い（probe.c:574 は SMEM のみ） |
| vblank／flip | intel_display_irq.c:1002、:339 `flip_done_handler`、:26、intel_crtc.c:541/687、commit_tail intel_display.c:7115 | irq.c:675: ack と**計数のみ**（de_vblank_count、de_flip_done_count）、drm_device.c:125 vblank slot | **部分**: vblank counter／event、pipe-update の critical section、atomic commit が無い（driver_probe.c:248-262） |
| HPD setup | intel_hotplug.c:622、intel_hotplug_irq.c:943、:839 | driver_probe.c:36、:59、:126、:183、:204 | 移植済み（pin 4/5、SHOTPLUG_CTL_DDI=0x88） |
| HPD 処理 | intel_hotplug_irq.c:653、:551、:341、intel_hotplug.c:495、:145 storm、:309 digport work、:378 hotplug work、intel_dp.c:6172 | irq.c:689-697、:757-765: IIR を ack して数えるだけ | **無し** |

## 3. probe 時の readout／sanitize と、firmware が pipe を残した場合

**読むもの（書込み無し）** `parity_intel_modeset_readout_hw_state`（display_nogem.c:1169）: pipe ごとの power gate と有効 transcoder（:1014）、TRANSCONF.ENABLE、TRANS_HTOTAL/VTOTAL、PIPESRC、plane ごとの PLANE_CTL.ENABLE、DDI_BUF_CTL.ENABLE → TRANS_DDI_FUNC_CTL の port select と MST（:851）、7 PLL の PLL_ENABLE。**読まないもの**: color、DSC、VRR、bigjoiner、scaler、output_format、linetime、M/N、lane 数、DPLL CFGCR、plane の SURF/STRIDE/format、WM/DDB。

**sanitize がすること**（:1451）: FBC reset、無効 encoder の clock gate（実機で "port A … ungated DDI clock, gate it"）、未使用 PLL を off、power well を逆順に sweep、active pipe には PIPEDMC 有効化と vblank on。これまでの実機では全 CRTC が無効で `vga_already_off=1`（`rombar=0`、GOP 無しと整合）。

**bare metal で GOP が pipe を active で残した場合の未対応**:
- `intel_crtc_disable_noatomic` 未実装（:1343-1375）。encoder の無い active pipe は WARN とフラグだけで、pipe は動いたまま。
- encoder のある active pipe でも、primary 以外の plane を止めず、BIOS の背景色も消さない。
- `intel_crtc_initial_plane_config`／`skl_get_initial_plane_config` が無い → BIOS の framebuffer を引き継ぎも予約もしない。
- active CRTC での `intel_initial_commit` は -EOPNOTSUPP を返し、probe は `where=intel_initial_commit` で止まる（probe.c:1650-1654）。
- PPS と backlight の状態を読まない。
- OpRegion VBT は「ある」と印が付くだけで parse されず、出力 0 個。
- parity は GGTT 表を消さない（上端 1 MiB の窓だけ触る）ので、下位にある GOP の PTE は残る（gt_mem.h:29-33）。

## 4. 参照データの採取計画（panel に画像が出ている Linux 起動で 1 回）

kernel 6.8.x を `drm.debug=0x1e log_buf_len=16M` で起動。`intel-gpu-tools`、`edid-decode`、`acpica-tools` を用意。`D=/sys/kernel/debug/dri/0`。行 7〜10 の pipe／port／transcoder の文字は pipe A・DDI A を仮定しているので、`i915_display_info` の実際の値に置き換える。

| # | データ | コマンド／パス | bare metal でしか取れないか |
|---|---|---|---|
| 1 | VBT | `cat $D/i915_vbt > vbt.bin`、`intel_vbt_decode vbt.bin` | **はい**（VFIO 下は VBT 無し） |
| 2 | OpRegion | `cat $D/i915_opregion > opregion.bin`、ASLS: `setpci -s 00:02.0 fc.l` | **はい** |
| 3 | EDID | `cat /sys/class/drm/card*-eDP-1/edid > edid.bin`、`edid-decode` | guest で AUX が動けば不要（§1 参照）。正本は bare metal の写し |
| 4 | DPCD | 6.8 に `i915_dpcd` node は無い。`dd if=/dev/drm_dp_aux0 bs=1 count=$((0x100))` と 0x200-0x2ff、0x700-0x7ff | いいえ |
| 5 | 表示状態 | `$D/i915_display_info`、`i915_shared_dplls_info`、`i915_ddb_info`、`i915_power_domain_info`、`i915_dmc_info`、`i915_display_capabilities`、`i915_edp_psr_status`、`$D/crtc-0/i915_pipe` | いいえ |
| 6 | panel timing／PPS | `$D/eDP-1/i915_panel_timings`、`intel_reg read 0xC7200 0xC7204 0xC7208 0xC720C 0xC7210`（PCH 側 PP_STATUS/CONTROL/ON/OFF/DIVISOR。parity が記録する `pps_mmio_base=0x61200` との関係を再確認） | VBT 由来の delay は**はい**（VFIO 下で書かれる値は既定値） |
| 7 | backlight | `ls /sys/class/backlight`、`intel_backlight/{type,max_brightness,actual_brightness}`、BLC_PWM_CTL/FREQ/DUTY `0xC8250 0xC8254 0xC8258`、drm.debug で VBT PWM か DPCD AUX backlight か | **はい**（controller・周波数・極性は VBT 由来） |
| 8 | timing | `intel_reg dump`、または TRANS_* `0x60000-0x60050`、PIPESRC `0x6001c`、TRANSCONF `0x70008`、TRANS_DDI_FUNC_CTL/CTL2 `0x60400/0x60404`、link M/N `0x60030-0x60044`、TRANS_CLK_SEL `0x46140` | いいえ |
| 9 | PLL／DDI | DPLL_ENABLE `0x46010/14`、DPLL0/1 CFGCR0/1（offset は `intel_reg dump` から）、DPCLKA_CFGCR0 `0x164280`、DDI_BUF_CTL_A `0x64000`、DP_TP_CTL/STATUS、combo PHY A の PORT_TX_DW2/4/5/7 と PORT_CL_DW5/10 | いいえ |
| 10 | plane／WM／DBUF | PLANE_CTL/STRIDE/POS/SIZE/SURF/COLOR_CTL `0x70180 0x70188 0x7018c 0x70190 0x7019c 0x701cc`、PLANE_WM `0x70240..`、PLANE_BUF_CFG `0x7027c`、DBUF_CTL_S1/S2 `0x45008/0x44fe8`、MBUS `0x4438c`、CDCLK_CTL `0x46000`、DC_STATE_EN `0x45504` | いいえ |
| 11 | HPD／IRQ | SHOTPLUG_CTL_DDI `0xC4030`、SHOTPLUG_CTL_TC `0xC4034`、SHPD_FILTER_CNT `0xC4038`、SDEIMR/IIR/IER `0xC4004/8/C`、GEN11_DE_HPD_IMR `0x44474`、`$D/i915_hpd_storm_ctl`、抜き差し 1 回の drm.debug ログ | いいえ |
| 12 | 全ログ | drm.debug 付き `dmesg`（VBT child dump、PPS delay、link rate／lane、vswing、選ばれた DPLL） | 両方（VFIO と bare metal の差を見るため） |
| 13 | GOP 引継ぎ状態 | 行 8〜10 を `i915.modeset=0` か module load 前に、`/sys/firmware/acpi/bgrt/*` | **はい** |

`/sys/kernel/debug/dri/0` は root が必要（linuxvm kit は guest 内で sudo を使っている）。既存の VFIO Linux guest でも同じ一式を取れば、bare metal との差がそのまま見える。

## 5. 既存ツリーの scanout／GGTT／framebuffer console

| 場所 | 内容 |
|---|---|
| `src/drivers/gpu/i915/vk/display.c:24-68` | stub。`i915_vk_display_init` は 1920×1080・stride 7680 を固定、`i915_vk_display_flip`（:50-58）は no-op。PLANE_* に触れない |
| `src/drivers/gpu/i915/vk/wsi.c:59-110` | linear BGRA の swapchain image を最大 WSI_MAX_IMAGES 個作り、それぞれ memory に bind（:84-100）。present は stub の flip を呼ぶ |
| `src/drivers/gpu/i915/ggtt.c:77-94, 379-438` | legacy 側の `i915_ggtt_boot_scanout`。`pcat.framebuffer` の boot handoff を受け、framebuffer が GMADR BAR2 内にあれば scratch fill 時にその GGTT PTE を保存・予約して firmware の scanout を生かす |
| `src/drivers/gpu/i915/ggtt.c:224` `drv_i915_ggtt_insert` | legacy 側の first-fit GGTT binder。scanout 用 pin に最も近い既存部品。parity 側は 1 MiB 窓（parity/gt_mem.h:50） |
| `src/hal/amd64/bsp-pcat/cons.c:332-375` | 早期の GOP framebuffer text console |
| `src/drivers/platform/pcat/graphics/backend.c:1022-1047` | kernel の graphics backend。GOP の linear framebuffer があれば使い、無ければ legacy VGA か Cirrus |

ツリー内に PLANE_CTL、PLANE_SURF、PLANE_STRIDE、TRANS_*、DDI_BUF_CTL を**表示のために書く**コードは無い。

bare metal では、parity が active pipe を止めず下位 GGTT PTE も消さない限り、GOP framebuffer console は動き続ける（現状どちらもしない）。これは「最初の画像」としては最も単純だが、**ネイティブ表示の成功ではない**（専門家の基準）。

## 6. 自前で panel を駆動するのに必要な実装（順）

1. VBT の child device と eDP／PPS／backlight block の parser（＋OpRegion VBT の保持）。
2. AUX と DPCD。
3. PPS と backlight。
4. combo PLL の compute と enable（DPCLKA 対応含む）。
5. DDI enable と link training（buf-trans 表）。
6. transcoder と pipe の programming。
7. universal plane 1 枚と最小の WM／DDB。
8. framebuffer が入る GGTT 窓（FHD で約 8 MiB。現状 1 MiB）と scanout 用 pin。
9. vblank／flip 完了（hw_done／flip_done／cleanup_done の区別）。
10. HPD の実処理（ack＋storm 対策＋port ごとの変更記録＋sleep 可能な文脈での再検出）。
11. firmware が残した active pipe の引継ぎ／停止（`intel_crtc_disable_noatomic`、initial plane config）。

> **訂正（E-106、2026-09-18）**: VBT と OpRegion は VFIO 下の Linux guest（SeaBIOS 起動）で取得できた。内蔵 panel も passthrough 経由で guest の i915 が駆動している。詳細と採取データは `plan/ws031/display-ref/README.md`。本メモの「bare metal でしか取れない」の記述のうち、GOP の引継ぎ状態以外は当てはまらない。
