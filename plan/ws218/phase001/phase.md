<!-- awesome-plan project=zedbsd record=ws218-p001 -->
# ws218-p001: 監査（OpenBSD iwx(4) との比較、Intel の名前と table の出典）

Status: in-progress（2026-10-13 P1 実施、結果の判定と p002 の範囲は Q1 → ユーザー）
Parent: [WS218](../ws.md)

## ゴール

p002（license の削除と引用の表示の整理）の可否を判断する材料を作る。この段では license の file・inventory・source の注記を変えない。

1. `src/drivers/wifi/intel-ax211/` の全 file を OpenBSD iwx(4) と比べ、関数の分け方・名前・制御の流れ・表現（comment・変数名・文の並び）の一致を、機械比較と目視の両方で調べる。
2. 定数・struct・field の名前が Intel の header（Linux iwlwifi・OpenBSD if_iwxreg.h）の名前と同じか、写しに近いかを一覧にする。
3. table（command の id・group、TLV の型、register の offset と bit、firmware の format、rate、PHY の値など）が公開の仕様書・資料から導けるかを出典つきで確かめ、導けない値の数と性質を書く。
4. 結論を (A) OpenBSD の license (B) Intel の license (C) 書き直しが要る所 (D) 引用元の表示を残す所、に分ける。確かめた事実と推測を分ける。法的な最終判断はユーザー。

## 比べた相手（tree に入れず、P1 の worktree の `build/tmp/ws218/` に取得）

| 相手 | 版 | sha256 |
| --- | --- | --- |
| OpenBSD `sys/dev/pci/if_iwx.c` | commit `0f464d413c50396e4e6cd70948f15613d6a73081`（zedBSD の注記が引く commit）、`$OpenBSD: if_iwx.c,v 1.229 2026/05/28 kirill`、12884 行 | `c95315f7…6b00a5` |
| OpenBSD `sys/dev/pci/if_iwxreg.h` | 同 commit、v1.75、8961 行 | `63c2418d…91fe8` |
| OpenBSD `sys/dev/pci/if_iwxvar.h` | 同 commit、v1.50、858 行 | `0e6fd501…7c04` |
| OpenBSD `sys/net80211/ieee80211.h` | 同 commit、v1.67（bss.c・bss.h の注記が引く） | `4b10494a…cea4` |
| Linux `drivers/net/wireless/intel/iwlwifi/` | tag v6.17（commit `e5f0a698b34ed76002dc5cff3804a61c80233a7a`）。header 全部（`iwl-*.h`、`fw/*.h`、`fw/api/*.h`、`pcie/*.h`、`pcie/gen1_2/*.h`）と、code の比較には `*.c`・`fw/`・`pcie/`・`mvm/`・`mld/` の 216 file。SPDX は `GPL-2.0 OR BSD-3-Clause` 204、`GPL-2.0-only` 11 | — |
| Linux `iwl-csr.h`・`fw/file.h` | tag v5.15（名前の改名を確かめるためだけ） | — |

対象: zedBSD の AX211 driver の 42 file（.c 21、.h 21）、31399 行。頭の注記は 31 file が「OpenBSD（ISC: genua・Fixup Software・Stefan Sperling）＋ Intel（BSD-3-Clause: Intel Deutschland・Intel Corporation）」（bss.c・bss.h は net80211 の Onoe・Leffler も）、11 file（boot.c/h、command.h、firmware.c/h、pci-mmio.c/h、runtime-start.c/h、transport-backend.c/h）が Awe Morris の Zlib だけ。

対照（同じ道具で、無関係の hardware の driver を比べた値）: zedBSD の `src/drivers/wifi/rtl8822b/`（同じ project・同じ書き方、2.4 万 token）。

## 方法

道具は P1 の scratchpad の Python（tree に入れていない、`python3 -I` で実行）。

- `csim.py`: C を token に分け（comment・前処理の行は除く）、2 つの mode で k-gram の一致を数える。**norm**（識別子 → I、数 → N、文字列 → S、keyword と記号は残す: 名前を付け替えた写しも拾う）、**ident**（識別子の接頭辞 `intel_ax211_`・`ax211_`・`iwx_`・`iwl_` などを外して小文字にし、数は値のまま）。関数ごとに一致の割合（containment）と最長の連続一致を出す。norm では enum や代入の並びの単調な繰り返しが誤検出になるので、k-gram の中の異なる token が 10 未満の物を除く（`MIND=10`）。
- `cruns.py`: 一致した最長の token 列を両側の file:行と本文つきで出し、目で分類する。
- `ccom.py`: comment（license の block を除く）の単語の n 語の一致。
- `cid.py`・`cval.py`: zedBSD の macro・enum 定数・struct の tag・field・関数の名前を、接頭辞を外して Intel 側（iwx と iwlwifi の header）の名前と照合する。`cval.py` は値も計算して「名前が同じ（exact）」「値が同じで名前の単語の Jaccard ≥ 0.5（near: 写しに近い）」「値だけ同じ（value）」「対応する値なし（none）」に分ける。
- 目視: firmware の TLV の parse、PNVM の parse、APM・prepare・stop の順、MAC address の読み出し、MSI-X の設定、association の手順、key の command、scan の probe の本文、context info の struct、command version の表を、iwx と Linux の該当の関数と並べて読んだ。

## 結果 1: OpenBSD iwx(4) との類似

### 機械比較（確かめた事実）

| 比較 | zedBSD AX211 | 対照 RTL8822B |
| --- | --- | --- |
| ident、k=8 の一致（名前つきの写し） | 0.12%（126/103906） | 0.03% |
| ident、k=12 | 0.07% | 0.00% |
| norm、k=20、MIND=10（名前を付け替えた写し） | 0.41%（425/103402） | 0.17% |
| comment の 6 語の一致 | 0 箇所 | — |
| comment の 5 語の一致 | 0 箇所 | — |
| comment の 4 語の一致 | 4 箇所（「the 5 ghz band」「which belongs to the」「256 tfds may be」「this table is the」） | — |
| Linux iwlwifi 216 file に対する ident k=8 | 0.29% | 0.10% |
| Linux に対する norm k=20 MIND=10 | 0.67% | 0.43% |
| Linux に対する comment 5 語 | 1 箇所（「is held by the caller」） | — |
| net80211 `ieee80211.h` に対する norm k=12 | 0% | — |

- ident で 12 token 以上続く一致は 2 箇所だけで、両方とも数の並び: 5 GHz の channel 番号の表（protocol.c:374、IEEE 802.11 の channel 番号）と、予約の MAC address `02:cc:aa:ff:ee:00`（mmio.c:1200、Linux `iwl-nvm-parse.c` と iwx の両方にある Intel の値）。
- norm で 24 token 以上続く一致は 30 箇所。全部が C の定型（`memset(&x, 0, sizeof(x)); x.f = ...`、`for (i = 0; i < n; i++) { e = &a[i];`、関数の終わりと次の関数の頭）で、iwx 側の相手は機能の関係が無い関数（例: zedBSD の PNVM の manifest ↔ iwx の `iwx_mld_rm_sta_cmd`）。対照の RTL8822B にも同じ型の一致がある。
- 関数の数: zedBSD の .c は約 572（平均 143 token）、`if_iwx.c` は約 268（平均 208 token）（簡易の parser の概数）。接頭辞を外した関数名が iwx と同じ物は 11（attach、match、resume、nic_lock、nic_unlock、post_alive、set_hw_ready、pnvm_parse、prepare_card_hw（mmio_ を付けた形）、delay、read32）。どれも device driver の一般的な名前か、Intel の driver（Linux）にも同じ名前がある操作。
- 構成: zedBSD は ops の表と状態機械（boot・runtime-start・scan-session・assoc の各 session）、世代番号・lock の規律、byte offset の明示の put/get で組み、iwx（`sc` を中心にした 1 file、C struct を直接 DMA に置く、net80211 の状態機械に従う）とも Linux（op_mode・trans の層）とも違う分け方。

### 目視の比較（一致した所の表）

| # | zedBSD の場所 | 相手 | 一致の中身 | 分類 |
| --- | --- | --- | --- | --- |
| 1 | `intel-ax211-transport.c:130-134` `ax211_fh_ivar_cause[]`・`ax211_hw_ivar_cause[]` | iwx `iwx_conf_msix_hw()`（if_iwx.c:2577-） | MSI-X の cause の 16 個の選び方と書く順（`0x29` SW_ERR の後に `0x15` SW_ERR_V2）が iwx の書く順と同じ。Linux v6.17 の `causes_list_common` は TOP_FATAL_ERR を含み SW_ERR は別の表で、順も違う | **OpenBSD の選択と順の写し**（小さい。各 cause は独立の register なので順は機能に関係しない） |
| 2 | `intel-ax211-mmio.c:1040-1100` `ax211_prepare_fallback()` | iwx `iwx_prepare_card_hw()`（:2379）、Linux `iwl_pcie_prepare_card_hw()` | 10 回の再試行・200 us の poll・25 ms の間隔・150 ms は 3 者で同じ。150 ms の予算を全試行で共有する（iwx は `t` を loop の外で初期化、zedBSD の `polled_us` も同じ）のは iwx の振る舞いで、Linux は試行ごとに 0 に戻す。表現（関数の分け方、error の扱い、comment）は違う | **OpenBSD 固有の振る舞いに従った**（考え方の水準。文の写しではない）。comment で「OpenBSD-derived」と自分で書いている |
| 3 | `intel-ax211-mmio.c:279-430` apm_init・stop | iwx `iwx_apm_init()`・`iwx_apm_stop()`、Linux `iwl_pcie_apm_init()`・`_stop()` | register の書く順（GIO chicken → HPET → HAP wake → L0S 無効 → INIT_DONE → clock を poll、stop の link-power の前置きと 1 ms・5 ms・100 us） | **hardware の手順から必然**（3 者同じ）。出典は Intel の driver |
| 4 | `intel-ax211-mmio.c:1180-1225` MAC の decode と検査 | iwx `iwx_flip_hw_address()`・`iwx_is_valid_mac_addr()`、Linux `iwl_flip_hw_address()`・`reserved_mac` | byte の並べ替えと、予約・0・broadcast・multicast の拒否 | **hardware の仕様から必然**＋予約の address は Intel の値。表現は別 |
| 5 | `intel-ax211-internal.h:187-211` `struct intel_ax211_context_info_gen3` | iwx `struct iwx_context_info_gen3`、Linux `iwl_context_info_gen3` | field の順と幅（device が読む DMA の layout）。名前は略語を開いた形（`cr_head_idx_arr_base_addr` → `cr_head_index_base` など） | **hardware の layout から必然**。名前は写しに近い（結果 2） |
| 6 | `intel-ax211.c:56-99` TLV の enum、`:255-` firmware の parse | iwx `iwx_read_firmware()`、Linux `fw/file.h` | TLV の番号と名前（`AX211_TLV_SEC_RT` ← `IWX_UCODE_TLV_SEC_RT`）。parse の流れ（loop と switch）は format から必然。zedBSD は byte offset で読み、重複・未知の TLV を拒み、固定の版を要求する別の作り | 番号は **firmware の format から必然**、名前は **写しに近い** |
| 7 | `intel-ax211-protocol.c:373-378` 5 GHz の channel の表 | iwx `iwx_nvm_channels_uhb[]`、Linux `iwl_uhb_nvm_channels[]` | 36…181 の 37 個の並び | channel 番号は **IEEE 802.11**、NVM の応答の中の順は **Intel の firmware の format** |
| 8 | `intel-ax211-scan.c:598-635` probe の本文 | iwx の probe の作り方 | 802.11 の probe request（SSID 0 長、rates 8 個、extended rates 4 個、DS parameter）。comment が「OpenBSD-style legacy 11g probe body」と書く | 本文は **IEEE 802.11 から必然**。segment の長さ（26・26・19・45）は Intel の `iwl_scan_probe_req` の layout |
| 9 | `intel-ax211-assoc.c` の手順（MAC add → LINK add → PHY → RLC → LINK assign → activate → STA add → queue → session protection） | iwx `iwx_auth()`（PHY → RLC → MAC → binding → STA → queue → session）、Linux mld | 手順の順は iwx と違い、Linux の MLD の順に近い | **firmware の protocol から必然**（iwx の写しではない） |
| 10 | `intel-ax211-key.c` SEC_KEY_CMD の encode | iwx `iwx_mld_set_sta_key_cmd()`、Linux `fw/api/datapath.h`（`iwl_sec_key_cmd`） | byte offset（action 0、sta_mask 4、key_id 8、flags 12、rx の PN 64） | **firmware の command の layout から必然**。出典は Intel の header |
| 11 | `intel-ax211-protocol.c:74`・`intel-ax211-runtime.c:76` command の版の表 | なし（iwx は `iwx_lookup_cmd_ver()` で実行時に読む） | 固定した firmware の `CMD_VERSIONS` TLV の中身を表にした物 | **公開の firmware の file（linux-firmware）から導ける** |
| 12 | `intel-ax211-assoc.c:1401-1404` EDCA の AC と FIFO の対応 | iwx `IWX_AC_*`・`IWX_GEN2_EDCA_TX_FIFO_*` | firmware の AC の順（BK・BE・VI・VO）と FIFO 番号 | **firmware の protocol の値**。出典は Intel の header |

その他、本文の comment が OpenBSD・Linux の振る舞いを根拠として引く所が 12 箇所ある（結論の (D)）。

## 結果 2: Intel の header の名前との一致

`cval.py`（946 個の数の定数、iwx と Linux の header・code と照合）:

| 分類 | 数 | 対照 RTL8822B（267 個） |
| --- | --- | --- |
| exact（接頭辞を外すと Intel の名前と同じ） | 41 | 0 |
| near（値が同じで名前がほぼ同じ） | 129 | 1 |
| value（値だけ同じ。小さい数の偶然の一致が大半） | 719 | 143 |
| none | 57 | 123 |

exact の 41（全部 register と firmware の値、`AX211_` を付けただけ）:
`CSR_HW_IF_CONFIG_REG` `CSR_INT_COALESCING` `CSR_INT` `CSR_RESET` `CSR_GP_CNTRL` `CSR_HW_REV` `CSR_HW_REV_TYPE_SO` `CSR_GIO_REG` `CSR_UCODE_DRV_GP1_CLR` `CSR_MBOX_SET_REG` `CSR_HW_RF_ID` `CSR_MAC_SHADOW_REG_CTRL` `CSR_LTR_LONG_VAL_AD` `CSR_GIO_CHICKEN_BITS` `CSR_CTXT_INFO_ADDR` `CSR_IML_DATA_ADDR` `CSR_IML_SIZE_ADDR` `CSR_DBG_HPET_MEM_REG` `CSR_DBG_LINK_PWR_MGMT_REG` `CSR_MSIX_FH_INT_CAUSES_AD` `CSR_MSIX_FH_INT_MASK_AD` `CSR_MSIX_HW_INT_CAUSES_AD` `CSR_MSIX_HW_INT_MASK_AD` `CSR_MSIX_AUTOMASK_ST_AD` `CSR_MSIX_RX_IVAR_AD_REG` `CSR_MSIX_IVAR_AD_REG` `HBUS_TARG_MEM_RADDR` `HBUS_TARG_MEM_RDAT` `HBUS_TARG_WRPTR` `UREG_DOORBELL_TO_ISR6` `UREG_CPU_INIT_RUN` `UREG_CHICK` `UREG_CHICK_MSIX_ENABLE` `RFH_Q0_FRBDCB_WIDX_TRG` `RFH_GEN_STATUS_GEN3` `RFH_RXF_DMA_CFG_GEN3` `RXF_DMA_IDLE` `FW_ADDR_CACHE_CONTROL` `TX_STATUS_SUCCESS` `TX_STATUS_DIRECT_DONE` `ICT_SIZE`（INTEL_AX211_）。

near の 129 の内訳（例）:
- TLV の型 37: `AX211_TLV_SEC_RT` ← `IWX_UCODE_TLV_SEC_RT`、`AX211_TLV_NUM_OF_CPU` ← `IWX_UCODE_TLV_NUM_OF_CPU`、`AX211_TLV_FW_MEM_SEG`、`AX211_TLV_PNVM_SKU` …（`UCODE_` を落としただけの物が多い）。
- command の id・group 25: `INTEL_AX211_PROTOCOL_NVM_GET_INFO_OPCODE` ← `IWX_NVM_GET_INFO`、`INTEL_AX211_ASSOC_GROUP_DATA_PATH` ← `IWX_DATA_PATH_GROUP`、`INTEL_AX211_SCAN_ITERATION_COMPLETE_OPCODE` ← `IWX_SCAN_ITERATION_COMPLETE_UMAC` …
- register の bit 17: `AX211_GP_MAC_CLOCK_READY` ← `IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY`、`AX211_RESET_STOP_MASTER`、`AX211_HW_IF_NIC_READY`、`AX211_GIO_L1A_NO_L0S_RX` …
- protocol の値・radio・scan の値 約 50: `AX211_RX_STATUS_CRC_OK` ← `IWL_RX_MPDU_STATUS_CRC_OK`、`AX211_SCAN_ABORT_STATUS_IN_PROGRESS` ← `IWL_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS`、`INTEL_AX211_PROTOCOL_ALIVE_STATUS_OK`（0xcafe）、`INTEL_AX211_CPU1_CPU2_SEPARATOR`（0xffffcccc）、`INTEL_AX211_PAGING_SEPARATOR`、`INTEL_AX211_TLV_MAGIC`、`INTEL_AX211_MAC_TYPE_SO`・`_SOF` …
- 一般語の偶然（`AX211_ASSOC_STATION_ID` ← `IWX_STATION_ID` など）も少し含む。

struct の tag: `context_info_gen3`、`sku_id`、`tx_ring` が同じ。field は 498 の内 91 が同じ名前だが、大半は一般語（`flags`、`length`、`status`、`channel`）。Intel に特有の field 名の一致は `gp2_on_air_rise`、`mac_sku_flags`、`phy_sku`、`ebs_status`、`failure_rts`、`failure_frame`、`lar_enabled`、`prph_info`、`prph_scratch_size`、`umac_prph_offset`、`error_event_table`、`imr_enabled` など約 20。context info の field は略語を開いた写し（結果 1 の #5）。

iwx と Linux の片方だけに一致する定数を調べた: OpenBSD 側（if_iwxreg.h）だけに一致したのは 11 個。その内 `HW_IF` の 3 bit（NIC_READY・PREPARE・ENABLE_PME）は Linux v5.15 の `iwl-csr.h` に同じ名前がある（v6.17 で改名）。TLV の 3 個（35 `SDIO_ADMA_ADDR`、53 `FW_FMAC_API_VERSION`、59 `FW_FMAC_RECOVERY_INFO`）は Linux v6.17・v5.15 に名前が無く、if_iwxreg.h の Intel の license の部分にある（より古い Linux から来たと推測、未確認）。残りは一般語の偶然（`STATION_ID` など）か版の違い。**OpenBSD が新しく付けた名前（Intel の系統でない名前）を使っている定数は見つからなかった**。名前の由来は Intel の header の系統。

## 結果 3: table の出典

公開の資料の調べ（2026-10-13、web 検索）: Intel が公開する AX211 の資料は product brief・仕様の頁・規制の書類（例 [AX211 の仕様](https://www.intel.com/content/www/us/en/products/sku/204837/intel-wifi-6e-ax211-gig/specifications.html)、[product brief](https://cdrdv2-public.intel.com/632078/632078_Intel_Wi-Fi_6E_AX211_Garfield_Peak_2_Product_Brief_Rev1p4.pdf)）で、register の map・host の command の仕様は無い。firmware の file の TLV の format の記述も、検索で出るのは Linux（とその写しの FreeBSD）の `iwl-fw-file.h`・`fw/file.h` だけだった。linux-firmware の `LICENCE.iwlwifi_firmware` は再配布の条件で、format の記述ではない。**公開の仕様書から導けない、という結論は「見つからなかった」の水準**（非公開の NDA の資料の有無は分からない）。

| table の種類 | zedBSD の量（概数） | 公開の出典 | 判定 |
| --- | --- | --- | --- |
| PCI の vendor・device・subsystem の ID（8086:51f0、8086:4090、rev 01） | 9 | PCI ID の公開の database、実機の読み取り（HW-T37） | 導ける |
| IEEE 802.11 の frame の bit・element ID（0・3・5・61・221）・WMM（OUI type 2、subtype 0・1、version 1）・rate（500 kbps 単位）・channel 番号・周波数 | bss.c の 27、scan.c の rate 12、channel 37 など約 90 | IEEE Std 802.11-2020（§9 の frame の形式、§9.4.2 の element、附属書 E の channel）、Wi-Fi Alliance の WMM の仕様 | 導ける |
| SHA-256 の定数と初期値 | 72 | FIPS 180-4 | 導ける |
| firmware・PNVM の file 名・大きさ・digest・版（89、minor・serial） | 約 12 | linux-firmware の公開の file（固定した 20260410 の版）を自分で計った値 | 導ける |
| command の版の表（`ax211_api89_required`・`ax211_runtime_versions`、17 行） | 17 行 | 固定した firmware の `CMD_VERSIONS` TLV の中身 | 導ける（firmware の file から。ただし TLV の番号 48 自体は下の行） |
| firmware の file の TLV の型と header（magic 0x0a4c5749、型 6〜74・0x100〜、separator 0xffffcccc・0xaaaabbbb・0xddddeeee） | 約 50 | Intel の header（`fw/file.h`、if_iwxreg.h）だけ | **導けない** |
| command の group・opcode（LONG 1、SYSTEM 2、MAC_CONF 3、PHY_OPS 4、DATA_PATH 5、REGULATORY_NVM 0xc、各 opcode） | 約 60 | Intel の header だけ | **導けない** |
| CSR・PRPH・UREG・RFH の register の offset と bit、MSI-X の cause 番号、doorbell の値 | 約 110 | Intel の header だけ | **導けない** |
| command・notification の payload の layout（byte offset の put/get 159 箇所、固定の byte index 約 200、command の大きさ 約 30） | 約 390 | Intel の header（`fw/api/*.h`、if_iwxreg.h の struct）だけ | **導けない** |
| protocol の status・flag・filter・key・rx の値（ALIVE の 0xcafe、RX の status bit、scan の flag、TX の rate の形式 0x0100・0x4000 など） | 約 80 | Intel の header だけ | **導けない** |
| 時間の値（HW ready 50 us、APM 25 ms、prepare 150 ms/25 ms/200 us、reset 5 ms、stop master 100 us、SOC の xtal latency 12000、dwell 10/110 など） | 約 30 | Intel の driver（Linux・iwx）の code だけ | **導けない**（hardware の性質の値。表現ではない） |
| zedBSD 自身の設計の値（result・state・step の enum、容量・上限、watchdog） | 約 450 | 自分の設計 | 対象外 |

導けない値は合計で約 750（定数 約 330 ＋ layout の offset・index 約 390 ＋ 時間 約 30）。性質: (1) device の register の番地と bit（hardware の interface の事実）、(2) firmware との通信の番号・struct の layout（firmware の ABI の事実）、(3) firmware の file の format の番号、(4) 手順の待ち時間。全部が「動かすのに必要な機能上の値」で、選び方や並べ方に作者の裁量が入る表現（comment、説明の文、名前）とは別の層。ただし名前は Intel の物を使っている（結果 2）。

## 結論（判断の材料）

### (A) OpenBSD の license（ISC: genua・Fixup Software・Stefan Sperling）

- 確かめた事実: code の文・comment・変数名・関数の分け方の写しは、機械比較（対照と同じ水準）でも目視でも見つからなかった。名前の一致は Intel の名前の系統で、OpenBSD に固有の名前は見つからなかった。OpenBSD に固有と言える一致は 2 つだけ: MSI-X の cause の選び方と順（#1、16 byte の表）、prepare の 150 ms の共有の予算（#2、振る舞い）。
- 推測: 表現としての写しは無いので、OpenBSD の license は外せる見込み。#1 は表現の選択と見られ得るので、外すなら #1 は書き直す（cause の番号を数の順にし、Linux と自分の設計で要る cause を選ぶ。機能は変わらない）のが安全。#2 は考え方の水準で、引用の表示（「OpenBSD iwx(4) の prepare の振る舞いに従う」）を残せば足りると見る。
- bss.c・bss.h の net80211（Onoe・Leffler、BSD）の注記: `ieee80211.h` との名前・code・comment の一致は 0。値は IEEE 802.11 の値なので、この注記も外せる見込み（出典を IEEE 802.11 に書き換える）。

### (B) Intel の license（BSD-3-Clause: Intel Deutschland・Intel Corporation。if_iwxreg.h と iwlwifi の header の license）

- 確かめた事実: (1) 定数の名前は Intel の名前を使っている（exact 41、near 約 130、struct の tag 3、特有の field 名 約 20）。ws.md の条件 2 の前半「Intel の header の名前を使っていない」は**今は満たしていない**。(2) register・command・TLV・layout の値（約 750）は、公開の仕様書・資料から導けない（見つからなかった）。出典は Intel の header（iwlwifi、または if_iwxreg.h）だけ。条件 2 の後半「table が仕様書から導ける」は、IEEE・FIPS・PCI ID・firmware の file の中身の分は満たすが、**register・command・format・layout の分は満たさない**。
- 推測: 名前は書き直せる（(C)）。値そのものは機能上の事実（interface の番号と layout）で、名前と説明を自分の物にすれば表現の写しではなくなる、という見方がある一方、Intel の header だけが出典の大きな表をまとめて使うことを Intel の著作物の利用と見る見方もある。**どちらを採るかは法的な判断で、ユーザーが決める**。保守的には Intel の BSD-3-Clause は残す（BSD-3-Clause の条件は表示を残すことだけで、zedBSD の Zlib と両立する）。外すなら、名前の書き直しに加え、各 table に「値は Linux iwlwifi の header（Intel、GPL-2.0 OR BSD-3-Clause）による事実の値」の引用の表示を付ける。

### (C) 外すなら書き直しが要る所

1. 名前（Intel の license を外す場合）: exact の 41 と near の約 130 の定数、struct の tag（`intel_ax211_context_info_gen3`・`intel_ax211_sku_id`・`intel_ax211_tx_ring`）、Intel に特有の field 名 約 20、context info の field。主に `intel-ax211-mmio.c`、`intel-ax211-transport.c`、`intel-ax211.c`（TLV・register）、`intel-ax211-protocol.h`、`intel-ax211-assoc.h`・`.c`、`intel-ax211-scan.h`・`.c`、`intel-ax211-rx.c`、`intel-ax211-runtime.h`、`intel-ax211-internal.h`、`intel-ax211-tx.c`・`tx-ring.h`、`intel-ax211-dma.h`。動作は変えない名前の付け替え。
2. MSI-X の cause の表（OpenBSD の license を外す場合）: `intel-ax211-transport.c:130-134` の選び方と順。
3. 「Intel documents that at most 255 of the 256 TFDs may be outstanding.」（`intel-ax211-tx-ring.h:68`）: 出典は Intel の header の comment（if_iwxreg.h:1759 の「A maximum of 255 (not 256!) TFDs」）。文は書き換え済みだが「documents」は公開の資料を指すように読めるので、引用の表示に直す。
4. HPET の comment（`intel-ax211-mmio.c` の「Raises the host-processor-event wait threshold.」）: iwx・Linux の説明は「FH wait threshold」で、意味が違う。license とは別の小さな誤りなので、p002 で直すとよい。

### (D) 引用元の表示を残す箇所（p002 で注記を外した後も、引用の場所に残す）

| 場所 | 引用の内容 |
| --- | --- |
| `intel-ax211-mmio.c:379` | 停止の link-power の前置き（OpenBSD・Linux の `apm_stop`） |
| `intel-ax211-mmio.c:405-410` | 100 us の master disable の期限を警告として扱う（Linux・OpenBSD） |
| `intel-ax211-mmio.c:1040` | prepare の fallback（OpenBSD の 150 ms の共有の予算） |
| `intel-ax211-mmio.c:1180`・`:1195` | MAC の byte の順と予約の address（OpenBSD・Linux。予約の値は Intel） |
| `intel-ax211-boot.c:1123`、`intel-ax211-runtime.c:193` | wide header の版の byte を 0 にする（OpenBSD） |
| `intel-ax211-init.c:63` | g2/c03 の v1 の layout、g0/c04 の扱い（OpenBSD） |
| `intel-ax211-transport.c:1083` | 古い command を LONG_GROUP で送る（OpenBSD、API ≥ 50） |
| `intel-ax211-assoc.c:1155`・`:1749`、`intel-ax211-tx-ring.c:294` | DTIM 無しの association、応答の flag を無視する（OpenBSD・Linux） |
| `intel-ax211-scan.c:606` | 11g の probe の本文（OpenBSD の作り方） |
| `intel-ax211-tx-ring.h:68` | TFD は 256 の内 255 まで（Intel の header の comment） |
| （新たに付ける）TLV の型・command の id・register・layout の各 table の頭 | 「値は Linux iwlwifi `fw/file.h`・`fw/api/*.h`・`iwl-csr.h`・`iwl-prph.h`・`iwl-fh.h`（Intel、v6.17）と OpenBSD `if_iwxreg.h`（0f464d41）による」 |
| （新たに付ける）bss.c・scan.c の 802.11 の値、firmware.c の SHA-256 | IEEE Std 802.11-2020、Wi-Fi Alliance WMM、FIPS 180-4 |

## ユーザーに判断を求める点

1. **Intel の license**: register・command・TLV・layout の値（約 750）は公開の仕様書から導けず、出典は Intel の header だけ。名前を書き直したうえで「事実の値」として引用の表示だけにして license を外すか、BSD-3-Clause を残すか。
2. **名前の書き直し**: Intel の license を外す場合、約 170 の定数・数個の struct・約 20 の field の名前を付け替える（動作は変えない、p002 の量が増える）。外さない場合も付け替えるか。
3. **OpenBSD の license**: 表現の写しは見つからなかった。MSI-X の cause の表（#1）を書き直したうえで外してよいか。prepare の振る舞い（#2）は引用の表示だけでよいか。
4. **net80211 の注記**（bss.c・bss.h の Onoe・Leffler）: 一致は 0。出典を IEEE 802.11 に書き換えて外してよいか。

## 確かめ

- build: 対象外（source を変えていない）。
- 行った確認: 上の機械比較・名前の照合・目視、web の検索（Intel の公開の資料と firmware の format の記述）。
- 未実施: 非公開（NDA）の Intel の資料の有無の確認（できない）。Linux の旧い版・FreeBSD の iwlwifi・iwm(4)・iwn(4) との比較（今回の注記が引く相手ではないため行っていない）。zedBSD の旧い履歴（GitHub の `old`）で作られ方を調べること（行っていない）。

## 残り・再開条件

- p002 は Q1 がこの結果をユーザーに見せ、上の 4 点の判断を得てから。p002 で触る所（参考）: 31 file の頭の注記と SPDX、`userland/base/licenses/intel-ax211-driver/`（LICENSE・Makefile）、`Makefile` の `intel-ax211-driver-license`、`tools/release/license-components.json` の `intel-ax211-driver`、`plan/ws004/tests/run-intel-ax211-driver-license-test.sh`、`plan/ws129/tests/license-inventory-test.sh`。firmware の package（`intelax211-firmware`）の license は変えない。
- 比較の相手と結果は P1 の worktree の `build/tmp/ws218/`（git に入れない）。p002 の後に消してよい（Q1 に依頼）。
