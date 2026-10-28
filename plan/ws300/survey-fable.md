<!-- awesome-plan project=zedbsd record=ws300-survey-fable -->

# WS300 の調べ: 公開の資料（Fable 5.1、2026-10-29、q1666 の P5 の配下）

WS300 p001（調査と設計）の入力。設計ではない。Fable の報告を P5 が要約した（「確か」は資料で読んだ事、「推定」は推論か記憶）。
取った頁は `/tmp/claude-1000/-home-awe-zedBSD-claude1/c207bd28-5f4e-4220-a046-ef09b3e800c9/scratchpad/ws300-fable/`（git に入れない、以下 SP/）。GPL・LGPL の source は読んでいない（Linux は commit・mailing list の散文だけ）。
取れなかった物（止めて回避していない）: bugzilla.kernel.org・lkml.org（bot の確認）、lore.kernel.org（403）、Intel の RST の release notes（403）、Kioxia BG5 の地域の頁と Dell の Setup and Specifications の PDF（権限の判定で拒否）、PCIe Base の仕様（会員だけ）。

## 1. NVMe の電源の管理（NVMe Base 2.0e、SP/nvme-base-2.0e.pdf、確か）

- Identify Controller（5.17.2.1、Fig. 280）: NPSS は byte 263（0 起点、最大 32）、APSTA は byte 265 の bit 0、RTD3R は 87:84、RTD3E は 91:88（µs）。
- Power State Descriptor（Fig. 281、32 byte）: MP 15:0（MXPS の bit 24: 0 = 0.01 W、1 = 0.0001 W）、NOPS の bit 25、ENLAT 63:32・EXLAT 95:64（µs、0 は未報告）、RRT・RRL・RWT・RWL、IDLP 143:128（IPS 151:150）、ACTP 175:160（APW・APS）。
- Feature 02h Power Management（5.27.1.2）: CDW11 の PS 4:0・WH 7:5。Feature 0Ch APST（5.27.1.9、Fig. 332〜334）: CDW11 の APSTE の bit 0、256 byte の表（32 × 8 byte、bit 7:3 ITPS、31:8 ITPT の ms、0 はその状態で無効）。ITPS は非操作の状態で、今の状態より idle の電力が高くない事。
- Feature 11h NOPSC（bit 0 NOPPME）、10h HCTM（TMT1 31:16・TMT2 15:0、K）。
- 8.15: 状態 0 が最大の電力。遷移の時間 = 旧 EXLAT + 新 ENLAT。非操作の状態は I/O を処理せず、I/O で自ら最後の操作の状態へ戻る。APST の入りは「I/O の未処理が無い時間が ITPT を超えた時」。8.15.4 の RTD3 は D3cold だけ（D3hot ではない）。
- 3.6: D3 に入る前の正規の shutdown（I/O queue の削除、CC.SHN = 01b、CSTS.SHST = 10b を待つ、RTD3E か 0 なら 1 s）。8.9 HMB: shutdown・RTD3 の前に HMB を返させ、後で Memory Return つきで渡し直す。
- host の idle の時間の決め方（散文、確か（散文））: Linux の初めの形は (ENLAT+EXLAT) の 100%、後に 50 倍（遷移の時間を 2% 以下に）で最深の状態は 6000 ms 以上。Intel RSTe は最初の非操作の状態まで 60 ms、深い状態は EXLAT の 1000 倍と記述（lkml.iu.edu の散文）。
  FreeBSD の `nvme_ctrlr.c`（BSD、SP/freebsd-nvme_ctrlr.c）は tunable の表で APST を設定し、reset・resume のたびに設定し直す。Feature 02h は使わない。

## 2. NVMe と S0ix の D3

- Microsoft「Power Management for Storage Hardware Devices」（確か、頁の保存なし）: Modern Standby では最も深い D-state へ。`_DSD`（UUID 5025030F-842F-4AB4-A561-99A5189762D0）の `StorageD3Enable` = 1/0 が opt-in の推しの方法、registry `StorageD3InModernStandby` が上書き、順は registry → `_DSD` → platform の allow list。
  NVMe は DEVSLP 相当（5 mW 未満、100 ms 未満の復帰）の非操作の状態を持つべき、無ければ APST だけ。PCIe の storage は L1 substates を持つべき。
- 散文（確か（散文））: Linux の s2idle は「最深の非操作の状態に Set Features ＋ link は ASPM L1.2」に変えた（vendor が CC.SHN と D3 を避けるよう求めた）。D3 の後に APST を切る drive がある。FreeBSD の suspend は HMB を止め → queue を消し → CC.SHN、resume は全部の reset。
- Intel の RST と VMD の S0ix の公開の記述は見つからない。5330 の DSDT で `StorageD3Enable` を探せば Dell の意図が分かる（推定）。

## 3. PCIe の ASPM・L1SS・LTR

- FreeBSD `pcireg.h`（BSD、確か）: LNKCAP の ASPM 0xC00・L0s の exit 0x7000・L1 の exit 0x38000・Clock PM 0x40000。LNKCTL の ASPMC 1:0（L0s 1、L1 2）・Common Clock 0x40・ECPM 0x100。DEVCTL2 の LTR の enable 0x400。拡張の cap の L1PM 0x1E、LTR 0x18。
  L1PM の CAP（+0x04、bit 0〜4: PCI-PM L1.2・PCI-PM L1.1・ASPM L1.2・ASPM L1.1・L1SS）、CTL1（+0x08、同じ位置の enable）、CTL2（+0x0C）。FreeBSD は L1SS と LTR を設定しない。
- vendor の register の文書（検索の要約、確か（要約））: L1SS の CAP の 15:8 Common_Mode_Restore_Time、17:16 T_POWER_ON の scale（2・10・100 µs）、23:19 値。LTR の cap +0x04: snoop 9:0 と scale 12:10、no-snoop 25:16 と 28:26、scale の段は 32 倍（0 = 1 ns）。
- 推定（記憶、仕様を読めていない）: CTL1 の 15:8 CMRT、28:16 LTR_L1.2_THRESHOLD の値、31:29 scale。CTL2 の 1:0 T_POWER_ON の scale、7:3 値。手順: 両端の ASPM を切る → 両端の Common Clock と retrain → CMRT・T_POWER_ON（両端の大きい方）・閾値を両端が D0 の時に書く → L1.2・L1.1 の enable を上流から → LTR の enable を ASPM L1 の前に → LNKCTL の L1 を上流から（切る時は逆）。
  散文（確か（散文））: 閾値の計算を 2+4+CMRT+T_POWER_ON µs とした時、報告の最大の LTR を超えて L1.2 に入らなかった drive がある。L1.2 の入りは CLKREQ# の解除と、LTR ≥ 閾値か「要求無し」。

## 4. Intel VMD（8086:467F）

- FreeBSD `vmd.c`（BSD、確か）: BAR0 が config の窓（子の offset = ((bus − bus_start)<<20)+(slot<<15)+(func<<12)+reg）、BAR1・BAR2 が memory の窓（2 つ目は 0x2000 の offset）。VMD_CAP 0x40 の bit 0 は bus の制限、VMD_CONFIG 0x44 の 9:8 が bus の始まり（0→0、1→128、2→224）、bit 1 が MSI の bypass。VMD_LOCK 0x70 は定義だけ。
  467F は BUS_RESTRICT と VECTOR_OFFSET（vector 0 を飛ばす）。MSI-X は VMD の vector を子で分け合う。suspend・resume は汎用の物で、何も保存・復元しない。
- Intel 12 世代 Datasheet Vol 1 §3.8（SP/intel/adl-datasheet-vol1.txt、確か）: VMD は root complex の中の endpoint、選んだ root port を隠す。CFGBAR・MMIO・register の lock・MSI の付け替えの無効。BIOS と driver の両方が要る。Vol 2 の register の本文は取れなかった。
- 散文（確か（散文））: 隠れた root port は BIOS から見えず ASPM・LTR が設定されず電力が高い。quirk は隠れた root port の全部の link の状態を許し、NVMe の LTR の max snoop が 0 なら snoop・no-snoop に 0x1003（3145728 ns）を書く（Rocket Lake〜Alder Lake、Raptor Lake は BIOS が書く）。
  隠れた root port と NVMe の L1SS の設定は OS が保存・復元する。ACPI の namespace は VMD の下を写さず、特別な `_ADR` で対を取る（D3cold・深い S0ix に要る）。

## 5. ADL-P の PMC と S0ix の妨げ

- Datasheet Vol 1（確か）: Package C10 には全 core の C10、GPU の RC6、device が適切な LTR を許す事が要る。PCIe の link は L1.2 以下・無効・device 無し。PC10 からの自動の降格は device の LTR が決める。LTR の数の閾値は公開されていない。
- 散文: pmc_core の SLP_S0 の residency と LTR の ignore・show。ADL の PCH は SLP_S0 の counter を持たず firmware が LPM の substate の counter をまとめる。PC10（north）は SLP_S0（south）なしでも入れる。

## 6. 5330 の SSD

- Dell の公式の仕様の PDF は取れず（拒否）。第三者の一覧: M.2 2230/2280 の PCIe NVMe、Dell の 2230 PCIe 4.0 x4 の「Class 35」（推定: DRAM 無し・HMB の 2230、Kioxia BG5 か WD SN740 が多い）。
- WD PC SN740 の product brief（SP/wd/、確か）: PCIe Gen4 x4、NVMe 1.4b、平均の動作の電力 50〜65 mW（MobileMark 2018）、Sleep（PS5）3.3 mW。ENLAT・EXLAT・APST の表は無い。
- Kioxia BG5（検索の要約）: PCIe 4.0、NVMe 1.4、HMB、動作 4.0〜4.3 W。idle・L1.2 の数は無い。

## 7. 実機で測る物（Debian、WS300 p001 の後半、Q1 の割当）

`nvme list`・`nvme id-ctrl -H`（NPSS・APSTA・PSD・RTD3E/R）・`nvme get-feature -f 0x0c/0x02/0x11/0x10 -H`、`lspci -vvv`（VMD の domain 10000: の root port と NVMe の LnkCap・LnkCtl・L1SubCap・L1SubCtl1/2・LTR・DevCtl2）、
`setpci -s 00:0e.0 0x40.w 0x44.w 0x70.l`、DSDT の `StorageD3Enable` と VMD の下の `_ADR`、`/sys/kernel/debug/pmc_core/{slp_s0_residency_usec,ltr_show,substate_residencies}`・`turbostat` の Pkg%pc10（s2idle の前後）。
s2idle を試すのは Debian の状態を変える（眠らせる）ので、別に許可を取る。
