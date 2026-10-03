# WS031 表示の参照データ（Linux 6.8 i915、VFIO passthrough、2026-09-18 採取）

採取環境: KVM ホスト（Dell Latitude 5330）上の QEMU、`-device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0`、q35、`-cpu host,host-phys-bits-limit=39`、4 GiB、**firmware は SeaBIOS**（zedBSD の試験は OVMF）。guest は Ubuntu 24.04、kernel 6.8.0-139、`i915.enable_guc=0`。採取は `handover/tools/collect_display.sh`（読み取りのみ）。

## 確認できたこと

**QEMU の passthrough 経由で、guest の i915 が実機の内蔵 panel を駆動している。**

| 項目 | 値 |
|---|---|
| connector | `eDP-1` connected／enabled／dpms On。`DP-1`、`DP-2`、`HDMI-A-1` は disconnected |
| panel | AUO（EDID model 11161、descriptor "B133HAN"）、13.3 型、2021 年製、6 bpc、EDID 1.4 |
| mode | 1920×1080、60.01 Hz、pixel clock 140.8 MHz、H 1920/1936/1952/2080、V 1080/1083/1097/1128、+HSync −VSync |
| pipe | pipe A active、transcoder EDP A（`PIPE_DDI_FUNC_CTL_A=0x8a210002`: DP SST、6 bpc、x2）、`PIPEACONF=0xc0000000`、dither、bpp 18 |
| port | DDI A／PHY A（combo）、`DDI_BUF_CTL_A=0x80000002`（enabled、x2） |
| link | DPCD rev 1.1、最大 HBR 2.7 Gbps、**2 lane**、enhanced framing なし、eDP 1.1。link status 0x77（両 lane で CR／EQ／symbol lock） |
| PLL | DPLL0（combo、refclk 38.4 MHz）`cfgcr0=0x00e001a5`、`cfgcr1=0x00000088`、`DPCLKA_CFGCR0=0x01e07800` |
| plane | plane 1A、XR24（XRGB8888）、1920×1080、`PLANE_CTL=0x94000000`、`PLANE_SURF=0x00180000`（GGTT offset） |
| パネル電源 | `PP_STATUS=0x80000008`（on）、`PP_CONTROL=0x6f`、`PP_ON_DELAYS=0x07d00001`、`PP_OFF_DELAYS=0x044c0001`。i915 の panel timings: power up 200 ms、power down 110 ms、backlight on 80 ms、backlight off 200 ms |
| backlight | `intel_backlight` type=raw、**PCH の PWM**（`BLC_PWM_PCH_CTL1=0x80000000`、`CTL2=0x00017700`＝周期 96000、duty 96000）。CPU 側 PWM は未使用 |
| PSR／DSC／VRR | sink は PSR 非対応、DSC 不使用、VRR なし |
| VBT | **8704 bytes 取得**（`i915_vbt.bin`、sha256 3bff4a09…） |
| OpRegion | **8192 bytes 取得**（`i915_opregion.bin`）、guest の `ASLS=0x7fffb000` |

## 以前の調査メモの訂正

`handover/notes/survey-display-lcd.md` では「VBT と OpRegion は bare metal でしか取れない」と書いたが、**誤り**。今回の Linux guest（SeaBIOS 起動）では ASLS が設定され、OpRegion と VBT が読めた。QEMU は `x-igd-opregion=on` のとき host の OpRegion を fw_cfg（`etc/igd-opregion`）で guest firmware へ渡す仕組みで、zedBSD の試験（OVMF 起動）で ASLS=0 だった（E-53）のは firmware 側の違いによると推定している（OVMF 側の原因は未確認）。少なくとも VFIO そのものの制約ではない。

bare metal でしか確認できないのは、GOP が pipe を active のまま残した状態の引継ぎ（VFIO 下では device が reset され、`rombar=0` で GOP も走らない）だけになった。

## zedBSD が VBT を得る方法の候補（未決定）

1. 採取した `i915_vbt.bin` を、DMC firmware と同じ「固定参照 blob」として image に入れる（対象機 1 台に絞る現段階では最も単純。bare metal では OpRegion から読む経路が別途必要）。
2. OVMF のまま、zedBSD 側で fw_cfg の `etc/igd-opregion` を読む（QEMU 専用の経路）。
3. parity の OpRegion 経路（ASLS → mailbox 4）を実装し、ASLS が設定される firmware 構成で起動する（bare metal と同じ経路を試験できる）。

## ファイル

`00-summary.txt`（要約）、`edid-eDP-1.{bin,txt}`、`dpcd-drm_dp_aux0-{000,200,700}.bin`、`i915_vbt.bin`、`i915_opregion.bin`、`dbg-*.txt`（i915 debugfs: display_info、shared_dplls、ddb、power_domain、dmc、eDP-1、crtc-0 ほか）、`regs-selected.txt`（PPS／BLC／transcoder／PLL／DDI／plane／HPD）、`regs-dump.txt`、`dmesg.txt`、`lspci.txt`。

VBT と OpRegion、EDID は対象機固有の firmware データで、配布物に入れるかどうかは出典台帳で別途扱う（`provenance-ledger.md` に追記）。

## 再現（目視確認用）

KVM ホストで `~/linuxvm/boot-dev.sh` を実行すると、初期設定の後（約 2 分）に guest の i915 が panel を点灯し、Linux の framebuffer console（fb0）が表示される。guest は 90 分で自動終了し、終了時に vfio が device を reset して panel は消える。guest へは KVM ホストから `ssh -p 2222 dev@127.0.0.1`。

## 追記（E-107、2026-09-18）: 要約値を「設定に使える入力」にするための確認

保存済みの dump と固定参照（Linux 6.8.12）から埋めた。再採取が要ったのは DPCD 0x100 帯だけ（同じ guest 起動中に追加採取、`dpcd-drm_dp_aux0-100.bin`）。独立デコーダの出力は `vbt-decode.txt`（igt `intel_vbt_decode`）。

### 1. 最大 link rate と、実際に設定された値
| 項目 | 値 | 出所 |
|---|---|---|
| 能力: MAX_LINK_RATE（DPCD 0x001）／MAX_LANE_COUNT（0x002） | 0x0a（HBR 2.7 Gbps）／0x02（2 lane、enhanced framing なし） | `dpcd-…-000.bin` |
| **実設定: LINK_BW_SET（0x100）／LANE_COUNT_SET（0x101）** | **0x0a／0x02** | `dpcd-…-100.bin` |
| link M/N | DATA_M1=0x7e4b17e4（TU 64、M 4921316）／DATA_N1=0x800000、LINK_M1=273406／LINK_N1=524288 | `regs-selected.txt` 0x60030〜0x60044 |
| 検算 | LINK_M/N = 0.52148 = 140.8 MHz ÷ 270 MHz → link clock 270 MHz（HBR）と整合 | — |
| VBT の fast link 指定 | 0（fast link training 不使用。BDB 249 では `edp_fast_link_training_rate[]*20`） | parser 出力、igt |
今回は能力の最大値と実設定が同じ値だが、別の情報として保持する（能力＝sink の上限、実設定＝参照が選んだ値）。

### 2. eDP 出力と hardware transcoder の ID
README の「transcoder EDP A」は `intel_reg` が `TRANS_DDI_FUNC_CTL` の bit field を人向けに表示した文字列で、hardware ID ではない。
| 実 ID | レジスタ | 値 |
|---|---|---|
| **cpu_transcoder = TRANSCODER_A** | `TRANS_DDI_FUNC_CTL(TRANSCODER_A)` 0x60400 | 0x8a210002（enable、DDI A select、DP SST、6 bpc、x2） |
| | `TRANSCONF(TRANSCODER_A)` 0x70008 | 0xc0000000 |
| | `TRANS_HTOTAL_A` ほか 0x60000〜0x60014 | 1920/2080、1936/1952、1080/1128、1083/1097 |
| 旧 `TRANSCODER_EDP` の block（0x6F000／0x6F400／0x7F008） | | **すべて 0（未使用）** |
ADL-P（Xe-LPD）の transcoder は A〜D と DSI で、専用の TRANSCODER_EDP は無い。eDP panel だから TRANSCODER_EDP、とはしない。

### 3. backlight: 方式と実レジスタ
| 項目 | 値 | 出所 |
|---|---|---|
| callback 群 | `cnp_pwm_funcs`（`INTEL_PCH_TYPE >= PCH_CNP`）: `cnp_setup_backlight`／`cnp_enable_backlight`／`cnp_disable_backlight`／`bxt_set_backlight`／`bxt_get_backlight`／`cnp_hz_to_pwm` | `intel_backlight.c:1711, 1803` |
| controller | 0 | VBT block 43（parser、igt） |
| control | `BXT_BLC_PWM_CTL(0)` = **0xC8250** = 0x80000000（`BXT_BLC_PWM_ENABLE`、polarity bit 29 = 0 → active high） | `regs-selected.txt` |
| frequency | `BXT_BLC_PWM_FREQ(0)` = **0xC8254** = 0x00017700（96000） | 同上 |
| duty | `BXT_BLC_PWM_DUTY(0)` = **0xC8258** = 0x00017700（96000 = 100%） | 同上 |
| 周期の由来 | rawclk 19200 kHz × 1000 ÷ VBT の 200 Hz = 96000（`cnp_hz_to_pwm`） | dmesg の rawclk、VBT |
| 極性／最小／最大 | active high（VBT active_low=0）／min level 15（BDB ≥ 234 の `brightness_min_level[]`。古い欄の 6 は使われない）／max = 96000 | parser、igt |
`intel_reg` が出す `BLC_PWM_PCH_CTL1/CTL2` という名前と「freq 1, cycle 30464」というデコードは古い世代の解釈で、CNP 以降の意味（CTL／FREQ／DUTY の 3 本）とは対応しない。CPU 側 PWM（0x48250）は未使用。

### 4. PPS: power-cycle delay（T11/T12）を含む採用値と由来
| 値（100 µs 単位） | VBT（eDP block、panel 2） | 参照が採用する値 | hardware に書かれた値 |
|---|---|---|---|
| T1+T3（power up） | 2000 | 2000（200 ms） | `PP_ON_DELAYS`=0x07d00001 → 上位 0x7d0 = 2000 |
| T8（backlight on） | 800 | 800（80 ms）を**ソフトウェアで待つ** | 下位 = **1**（`final->t8 = 1`。小さい値＝待機不要、ではない） |
| T9（backlight off） | 2000 | 2000（200 ms）をソフトウェアで待つ | `PP_OFF_DELAYS`=0x044c0001 → 下位 = 1 |
| T10（power down） | 1100 | 1100（110 ms） | 上位 0x44c = 1100 |
| **T11+T12（power cycle）** | **5000** | 5000 + 1000（`vbt->t11_t12 += 100*10`）= 6000 → `roundup(…, 1000)` = **6000（600 ms）** | `PP_CONTROL`=0x6f の bits 8:4（`BXT_POWER_CYCLE_DELAY`）= **6**（= DIV_ROUND_UP(6000, 1000)）。`PP_DIVISOR` は ICP 以降存在しない（読値 0xffffffff） |
採用規則は `intel_pps.c` の `pps_init_delays()`: BIOS（レジスタ）値と VBT 値の大きい方、どちらも 0 なら eDP 仕様の上限値へ fallback。i915 debugfs の panel timings（power up 200／down 110／backlight on 80／off 200 ms）と一致。

### 5. VBT の要点（parser と igt の一致を確認済み）
BDB version 249、block 12 個を採用、child 4 個: port A = eDP（type 0x1806、DVO DP-A、AUX A）、port B = HDMI（0x60d2、DDC pin 2）、TC1／TC2 = DP Type-C／TBT（0x68c6）。port C の child は無い。panel type 2、18 bpp、DRRS seamless、PSR block は未使用（sink 非対応）。VBT 内蔵の panel mode は汎用の 1024×768 で、実 mode は EDID（1920×1080@60.01、140.8 MHz）から得る。

## 追記（E-108、2026-09-19）: zedBSD の実 AUX 経路で取得した値との照合

OVMF＋明示 VBT の zedBSD から、実 AUX で読んだ値（`handover/increment-results/e108-run-parity-hw-aux.log`）:
DPCD 0x000〜0x00e = `11 0a 02 41 00 00 01 00 02 00 00 00 00 0b 00`、0x700〜0x702 = `01 18 00`、0x100／0x101 = `0a 02`、EDID 128 byte（sha256 50305822…、mfg 06af／product 2b99、checksum 0x74）。**すべて本 directory の採取値（Linux 経由）と一致**。
OVMF 下の PPS レジスタは未設定（全 0）で、`intel_pps_init()` が VBT から `PP_ON_DELAYS`=0x07d00001、`PP_OFF_DELAYS`=0x044c0001、power-cycle 欄 6 を設定した（＝上の §4 の表で Linux が設定していた値）。0x100／0x101 は 0a／02 と読めた。zedBSD は DPCD へ何も書いていない。これが panel の電源投入時の既定値なのか、以前の設定が残っているのかは未確認（link training を実装する段で、書いた値と読み戻しで確かめる）。
