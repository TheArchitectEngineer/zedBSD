<!-- awesome-plan project=zedbsd record=ws218-p002 -->
# ws218-p002: license の表示の削除と、引用の表示・名前の付け直し・block ごとの出典の comment

Status: in-progress（2026-10-13 P3 実施。実装・build・host 試験・機械比較は済み。QEMU と実機の AX211 の回帰は T1・ユーザーへの依頼で未実施。削除の 3 path は Q1 への依頼）
Parent: [WS218](../ws.md)

## 範囲（ws.md の「ユーザーの決定（2026-10-13、p001 の後）」と Q1 の依頼による）

1. OpenBSD: license と著作権表示を外し、引用元の表示を引用の箇所に残す（p001 の (D)）。MSI-X の cause の表（旧 transport.c:130-134）を独自の形に書き直す（機能は同じ）。
2. net80211: bss.c・bss.h の Onoe・Leffler の注記を外し、出典を IEEE 802.11 にする。
3. Intel: 定数（p001 の exact 41・near 約 130）、struct の tag、Intel に特有の field 名を付け直す。連続する macro の block・enum・表の頭ごとに出典と zedBSD の実装から分かった事実を書く。各 file の頭に「Intel の header を 1 次資料として値を収集した」旨と「翻案ではない」旨を書く（後者は 2026-10-13 ユーザーの追加の指示「ほかの OS のドライバを作る人にも役立つ資料であるから、これはIntelヘッダの翻案ではないと、ファイルヘッダに書きましょう。」）。tx-ring.h の「Intel documents …」と mmio.c の HPET の comment の誤りを直す。
4. license の表示: 31 file の頭の注記と SPDX を zedBSD の license（Zlib）だけにし、driver の license の package・inventory の記載を外し、試験を追従させる。firmware の package（intelax211-firmware）の license は変えない。
5. 動作は変えない。

## 行ったこと

### 1. file の頭（31 file ＋ 2 file）

- OpenBSD（ISC: genua・Fixup Software・Stefan Sperling）、Intel（BSD-3-Clause: Intel Deutschland・Intel Corporation）、net80211（Onoe・Leffler）の著作権表示・許諾文・`SPDX-License-Identifier: ISC AND BSD-3-Clause` を 31 file から外し、規約 13 節の zedBSD の頭（Copyright Awe Morris、SPDX Zlib）と file の説明の comment にした。
- 29 file の説明の comment に「Sources of the values.」の段落: 値は AX211 の hardware と firmware の interface の事実で、Intel は register・host command の仕様を公開していないので Intel 自身の driver の header（Linux iwlwifi v6.17、`drivers/net/wireless/intel/iwlwifi/` の file 名を file ごとに列挙）を 1 次資料として収集し、OpenBSD `if_iwxreg.h`・`if_iwx.c`（0f464d413c50）で照合した、各 block が出典を書く、と書いた。続く段落で「この file は header の翻案ではない。値を集め、独自の名前を付け、zedBSD の driver が AX211 の hardware で確かめたこと（どこで使い、device・firmware にとって何を意味し、device が要る順序、firmware の振る舞い）を書き加えた資料で、他の OS の driver の開発者が単独で使える参照として書いた」と書いた。
- bss.c・bss.h は driver の header の値を含まないので、出典を IEEE Std 802.11-2020（9.2.4.1、9.3.3.2、9.3.3.10、9.4.2）と Wi-Fi Alliance WMM（OUI 00-50-F2 type 2）とし、「driver の header の値は無い」と書いた。
- もとから Zlib だった file のうち Intel の値を持つ 2 か所（`intel-ax211.c` の PCI の部（旧 pci-intel-ax211.c）、`intel-ax211-pci-mmio.c`）にも同じ出典と「翻案ではない」の段落を足した。`intel-ax211-firmware.c` は FIPS 180-4 の定数と zedBSD が計った digest だけなので、表ごとの出典の comment だけを足した。

### 2. 引用元の表示（p001 の (D)）

本文の OpenBSD・Linux を根拠として引く comment（mmio.c の stop の前置き・100 us の警告・prepare の fallback・MAC の byte の順と予約の address、boot.c と runtime.c の wide header の版 0、init.c の g2/c03・g0/c04、transport.c の LONG_GROUP、assoc.c の DTIM 無し・応答の flag、tx-ring.c の応答の flag、scan.c の 11g の probe）は残した。新たに各 block の頭に出典（Linux v6.17 の file 名と struct・enum の名前、必要なら OpenBSD）を付けた。scan.c の probe の本文には IEEE 802.11 の element と rate と Linux の `iwl_scan_probe_req` の segment の意味を足した。

### 3. 書き直し

- **MSI-X の cause の表**（旧 `ax211_fh_ivar_cause[]`・`ax211_hw_ivar_cause[]`、OpenBSD と同じ選び方と書く順）: 表を廃止し、`ax211_configure_msix_routes()` を「routed の cause の mask（`AX211_QUEUE_CAUSES_ROUTED`・`AX211_EVENT_CAUSES_ROUTED`）の bit の位置から vector map の byte を導く」形に書き直した。queue cause の bit 15:0 → RX vector map の byte 0〜15、bit 31:16 → cause vector map の byte 0〜15、event cause の bit n → cause vector map の byte 16+n（Linux v6.17 `iwl-csr.h` の enum の値と一致することを確かめた）。書く byte の集合と値（vector 0 | bit 7）は前と同じ 18 byte（RX 0・1、0x00・0x01・0x03・0x05、0x10・0x11・0x12・0x15・0x16・0x17・0x18・0x29・0x2a・0x2b・0x2d・0x2e）。書く順だけが bit の昇順に変わった（0x15 が 0x16 の前）。呼び出し元が全 cause を mask してから呼ぶので順は機能に関係しない。
- **tx-ring.h:68**「Intel documents that at most 255 of the 256 TFDs may be outstanding.」→ block の comment で出典を示す形に直した: Linux v6.17 `iwl-fh.h` の TFD の説明の文「A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.」（OpenBSD `if_iwxreg.h` の `struct iwx_tfh_tfd` の上にも同じ文）。
- **mmio.c の HPET の comment**「Raises the host-processor-event wait threshold.」→「Raises the DMA engine (FH) wait threshold to its maximum.」。Linux v6.17 `pcie/gen1_2/trans.c` の「Set FH wait threshold to maximum (HW error during stress W/A)」による。register 0x240 の block の comment にも同じ意味を書いた。

### 4. 名前の付け直し（動作は変えない）

道具: scratchpad の `ws218p2/renamer.py`（C を code・comment・文字列に分け、識別子だけを map で置き換える。文字列の中は変えない。comment の中は code に実在する識別子だけを置き換え、Intel の名前を引く文は残す）と `rename.txt`・`rename2.txt`。置き換えた識別子は約 470（driver の 42 file と `plan/ws004/tests/` の host 試験の C 6 file・gdb の python 1 file）。

| 分類 | 数（概数） | 例（旧 → 新） |
| --- | --- | --- |
| CSR（BAR0）の register | 31 | `AX211_CSR_GP_CNTRL` → `AX211_CSR_POWER_CONTROL`、`AX211_CSR_HW_IF_CONFIG_REG` → `AX211_CSR_FUNCTION_CONTROL`、`AX211_CSR_RESET` → `AX211_CSR_RESTART_CONTROL`、`AX211_CSR_CTXT_INFO_ADDR` → `AX211_CSR_BOOT_CONTEXT_ADDRESS`、`AX211_CSR_IML_DATA_ADDR` → `AX211_CSR_LOADER_IMAGE_ADDRESS`、`AX211_CSR_MSIX_IVAR_AD_REG` → `AX211_CSR_MSIX_CAUSE_VECTOR_MAP`、`AX211_HBUS_TARG_WRPTR` → `AX211_CSR_QUEUE_WRITE_INDEX`、`AX211_RFH_Q0_FRBDCB_WIDX_TRG` → `AX211_CSR_RX_FREE_INDEX` |
| PRPH の register | 7 | `AX211_UREG_CPU_INIT_RUN` → `AX211_PRPH_UPPER_CPU_START`、`AX211_UREG_CHICK` → `AX211_PRPH_INTERRUPT_MODE`、`AX211_UREG_DOORBELL_TO_ISR6` → `AX211_PRPH_FIRMWARE_DOORBELL`、`AX211_RFH_GEN_STATUS_GEN3` → `AX211_PRPH_RX_DMA_STATUS` |
| register の bit・値 | 約 50 | `AX211_GP_MAC_CLOCK_READY` → `AX211_POWER_CLOCK_STABLE`、`AX211_HW_IF_NIC_READY` → `AX211_FUNCTION_OWNERSHIP_REQUEST`、`AX211_FH_CAUSE_*` → `AX211_QUEUE_CAUSE_*`、`AX211_HW_CAUSE_*` → `AX211_EVENT_CAUSE_*`、`AX211_LTR_*` → `AX211_LATENCY_*` |
| firmware の file の record の型 | 42 | `AX211_TLV_SEC_RT` → `AX211_RECORD_RUNTIME_SECTION`、`AX211_TLV_CMD_VERSIONS` → `AX211_RECORD_COMMAND_VERSION_TABLE`、`AX211_TLV_PNVM_SKU` → `AX211_RECORD_PNVM_BOARD`、`INTEL_AX211_TLV_MAGIC` → `INTEL_AX211_FIRMWARE_FILE_MAGIC`、`INTEL_AX211_CPU1_CPU2_SEPARATOR` → `INTEL_AX211_UPPER_CPU_IMAGE_MARK` |
| command の family（group） | 18 | `*_GROUP_LEGACY` → `*_FAMILY_CLASSIC`、`*_GROUP_LONG` → `*_FAMILY_CLASSIC_WIDE`、`*_GROUP_SYSTEM` → `*_FAMILY_PLATFORM`、`*_GROUP_MAC_CONFIG` → `*_FAMILY_LINK_SETUP`、`*_GROUP_DATA_PATH` → `*_FAMILY_TRAFFIC`、`*_GROUP_PHY` → `*_FAMILY_RADIO`、`*_GROUP_REGULATORY_NVM` → `*_FAMILY_REGULATORY_STORE` |
| command・notification（opcode と、同じ接頭辞の版・大きさ・step） | 約 110 | `NVM_GET_INFO` → `NVM_QUERY`、`NVM_ACCESS_COMPLETE` → `NVM_HANDOFF`、`PNVM_INIT_COMPLETE` → `PNVM_ACCEPTED`、`INIT_COMPLETE` → `INIT_FINISHED`、`EXTENDED_CFG` → `STAGE_SELECT`、`BT_CONFIG` → `COEXISTENCE`、`LTR_CONFIG` → `LATENCY_TOLERANCE`、`MCC_UPDATE` → `COUNTRY_UPDATE`、`TX_ANT` → `TRANSMIT_CHAINS`、`POWER_TABLE` → `POWER_POLICY`、`PHY_CONTEXT` → `CHANNEL_CONTEXT`、`MAC_CONFIG` → `INTERFACE_SETUP`、`LINK_CONFIG` → `LINK_SETUP`、`STATION_CONFIG` → `PEER_SETUP`、`RLC_CONFIG` → `RECEIVE_CHAINS`、`QUEUE_CONFIG` → `QUEUE_SETUP`、`SESSION_PROTECTION` → `TIME_RESERVATION`、`MCAST_FILTER` → `MULTICAST_LIST`、`MAC_POWER` → `POWER_SAVE`、`SCAN_ABORT` → `SCAN_CANCEL`、`SCAN_COMPLETE`・`ITERATION_COMPLETE` → `SCAN_FINISHED`・`SCAN_PASS_FINISHED` |
| protocol・scan・rx・tx・key の値 | 約 60 | `INTEL_AX211_PROTOCOL_ALIVE_STATUS_OK` → `..._ALIVE_HEALTHY`、`..._NVM_CHANNEL_VALID`・`ACTIVE` → `..._CHANNEL_FLAG_USABLE`・`MAY_TRANSMIT`、`INTEL_AX211_RUNTIME_API_*`・`CAP_*` → `..._API_BIT_*`・`FEATURE_BIT_*`、`INTEL_AX211_SCAN_ACTIVE_DWELL` → `..._PROBING_STAY_TU`、`AX211_RX_STATUS_*` → `AX211_RX_CHECK_*` 等、`AX211_TX_STATUS_*` → `AX211_TX_OUTCOME_*`、`AX211_KEY_FLAG_CCMP` → `AX211_KEY_SUITE_CCMP`、`INTEL_AX211_TX_RING_SLOT_COUNT` → `..._DESCRIPTOR_SLOTS`、`INTEL_AX211_OK` → `INTEL_AX211_SUCCESS`、slot（`*_STATION_ID`・`MAC_ID`・`LINK_ID` → `*_PEER_SLOT`・`INTERFACE_SLOT`・`LINK_SLOT`） |
| struct の tag | 5 | `intel_ax211_context_info_gen3` → `intel_ax211_boot_context`、`intel_ax211_sku_id` → `intel_ax211_board_signature`、`intel_ax211_tx_ring` → `intel_ax211_frame_ring`、`intel_ax211_rx_mpdu` → `intel_ax211_rx_delivery`（途中で `rx_frame` にしたが iwx の関数名 `iwx_rx_frame` と一致したので変えた） |
| Intel に特有の field 名 | 約 55（boot context の 24 を含む） | boot context: `prph_info_base` → `status_page_address`、`cr_head_index_base` → `completion_head_array_address`、`command_transfer_ring_base` → `command_ring_address`、`command_completion_ring_size` → `response_ring_size_code`、`message_ring_flags` → `ring_control_flags`、`prph_scratch_size` → `scratch_area_size` 等。その他: `gp2_on_air_rise` → `air_start_clock`、`mac_sku_flags` → `standard_flags`、`phy_sku` → `radio_profile`、`ebs_status` → `energy_skip_status`、`failure_rts`・`failure_frame` → `rts_retries`・`frame_retries`、`lar_enabled` → `firmware_regulatory`、`prph_info` → `status_page`、`umac_prph_offset` → `upper_cpu_prph_base`、`error_event_table`・`log_event_table` → `error_log_address`・`event_log_address`、`error_info` → `upper_error_log_address`、`imr_*` → `restore_region_*`、`flow_handler` → `queue_causes`、`general_flags` → `nvm_flags`、`nvm_version` → `nvm_revision`、`board_type` → `board_kind`、`last_schedule` → `final_schedule`、`probe_status` → `probe_result`、`ht_enabled`・`vht_enabled`・`he_enabled` → `standard_11n`・`standard_11ac`・`standard_11ax`、`cdb` → `concurrent_bands`、`tx_ring` → `frame_ring`、`beacon_tsf` → `beacon_timestamp`、`enabled_fh_causes`・`enabled_hw_causes` → `enabled_queue_causes`・`enabled_event_causes` |
| Intel に特有の関数名 | 約 25 | `*_nic_lock`・`*_nic_unlock` → `*_wake_hold`・`*_wake_release`（ops の field `nic_lock` 等も）、`post_alive` → `after_alive`、`ax211_set_hw_ready` → `ax211_request_ownership`、`drv_intel_ax211_mmio_prepare_card_hw` → `..._take_ownership`、`drv_intel_ax211_pnvm_parse` → `drv_intel_ax211_pnvm_select`、`*_nvm_get_info*`・`*_nvm_access_complete*`・`*_init_complete*`・`*_extended_cfg_*`・`*_context_info_gen3_encode` → `*_nvm_query*`・`*_nvm_handoff*`・`*_init_finished*`・`*_stage_select_*`・`*_boot_context_encode` |

文字列（kernel の log の文）は変えていない。

### 5. block ごとの出典と事実の comment

全 block に、出典（Linux v6.17 の file と struct・enum 名、必要なら OpenBSD・IEEE・FIPS・PCI・PCIe の仕様・linux-firmware の計測）と、zedBSD の実装から分かった事実（使う関数、byte offset と意味、送る順序、firmware が受け入れる値、拒む条件、固定した firmware の中身（例: runtime section 60 個（LMAC 15・UMAC 17・paged 26 と 2 つの mark）、loader 13944 byte、command version の表 217 entry、PNVM は 4 board・8 section）、firmware の振る舞い（API 89 の RLC の wide header の版 2、time reservation の notice の版 3 と layout 2、pass の報告が 112 entry の全体を出す、MAC_PM の応答が空のことがある等））を書いた。主な file: internal.h（PCI identity、silicon と radio、firmware と PNVM の固定、file の framing、ring と descriptor、TFD、header、mark、board signature と boot context の struct の各 field を device がどう読むか）、intel-ax211.c（record の型の enum の全体、PCI の部の 8 block）、mmio.c・mmio.h、transport.c・transport.h、pci-mmio.c、dma.c・dma.h、protocol.h・protocol.c（必要な版の表と 5 GHz の channel の表）、runtime.h・runtime.c（版の表）、init.h、assoc.h・assoc.c（EDCA の AC と FIFO の表）、scan.h・scan.c・scan-session.c、rx.h・rx.c、tx.h・tx.c、tx-ring.h・tx-ring.c、key.h・key.c、bss.c、firmware.c。

`intel_ax211_boot_context` の comment には出典（Linux `pcie/iwl-context-info-v2.h` の `struct iwl_context_info_v2`、v6.17、と OpenBSD `if_iwxreg.h` の `struct iwx_context_info_gen3`、0f464d413c50）、wire の byte offset、各 field の AX211 での使い方を書いた。

### 6. license の inventory・package・試験

| path | 変更 |
| --- | --- |
| `Makefile` | `CONFIG_DRIVER_PCI_INTEL_AX211` で `intel-ax211-driver-license` を選ぶ 7 行を外した |
| `tools/release/license-components.json` | `intel-ax211-driver` の component を外した（`intelax211-firmware` は変えていない） |
| `plan/ws129/tests/license-inventory-test.sh` | case 2 を「`rtl8822b-tables` と `musl-regex` を外した表で、`src/drivers/wifi/rtl8822b/` の BSD-3-Clause と `src/libc/regex/` の SPDX 無しの第三者の著作権を uncovered として報告する」に替え、case 1 に「AX211 の source が第三者として報告されない」の確認を足した |
| `plan/ws004/tests/README.md` | driver-license の試験の参照と ISC/BSD-3-Clause の notice の説明を外し、driver が Zlib になったことを書いた |
| `plan/ws004/tests/run-intel-ax211-{core,dma,boot,runtime-start,firmware-loader,transport}-test.sh` | 名前の付け直しに追従した C の試験を走らせるため、host の `rm -rf` の trap を外し、作業 directory を `build/tmp/` に置く形にした（AGENTS.md の rm の規則）。core は他と同じく `INTEL_AX211_FIRMWARE_TEST_CACHE` を受ける |
| `userland/base/licenses/intel-ax211-driver/`（LICENSE・Makefile）、`plan/ws004/tests/run-intel-ax211-driver-license-test.sh` | 削除が要る。Q1 に path を送る（下）。Makefile から参照を外したので package は選ばれない |

WS129 の生成物（`plan/ws129/licenses-index.txt`・`licenses-generated.md`）には `intel-ax211-driver` の行が残る。他の WS の記録なので変えていない（Q1 が inventory を作り直す時に消える）。

## 確かめ（2026-10-13、P3 の worktree、host の上）

| 確かめ | command | 結果 |
| --- | --- | --- |
| amd64 vmunix（CI の config、AX211 を含む） | `make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws218-p002 build/ws218-p002/vmunix` | exit 0、warning 0、kernel include check PASS、amd64 vmunix check PASS（AX211 の .c 21 個を compile） |
| AX211 core | `sh plan/ws004/tests/run-intel-ax211-core-test.sh`（`INTEL_AX211_FIRMWARE_TEST_CACHE` は main の `build/sources/firmware/intelax211/dc85cced…`） | PASS（ordinary・ASan/UBSan・analyzer・amd64/i386 syntax、固定の firmware と PNVM を含む） |
| AX211 dma | `run-intel-ax211-dma-test.sh` | PASS（同上、固定の blob を含む） |
| AX211 boot | `run-intel-ax211-boot-test.sh` | PASS |
| AX211 runtime-start | `run-intel-ax211-runtime-start-test.sh` | PASS |
| AX211 firmware loader | `run-intel-ax211-firmware-loader-test.sh` | PASS |
| AX211 transport（MSI-X の routing を含む） | `run-intel-ax211-transport-test.sh` | PASS |
| license inventory | `sh plan/ws129/tests/license-inventory-test.sh` | PASS（実の表で open 0、AX211 の source の報告 0、case 2〜5 ok） |
| 規約の機械検査 | `python3 plan/tools/style-check.py src/drivers/wifi/intel-ax211/*.[ch] --summary` | 690 件（変更前の tree と同数。新しい指摘なし、残りは以前からの債務） |
| `git diff --check` | | 指摘なし |

### 機械比較のやり直し（p001 の道具、`python3 -I`、比べる相手は P1 の `build/tmp/ws218/`）

| 比較 | 変更前（p001・同じ呼び方） | 変更後 |
| --- | --- | --- |
| `cval.py`（数の定数、iwx と Linux の header・code） | exact 41（再計測 42）・near 129（再計測 133） | **exact 0・near 0**（value 891・none 51、計 942） |
| `cid.py` macro の一致（exact／suffix） | 47／9 | 1／0（`AX211_PCI_REVISION` ↔ 一般語 `revision`） |
| `cid.py` enum 定数 | 2 | 2（`INTEL_AX211_SUCCESS`・`INTEL_AX211_INVALID` ↔ Linux の一般語の識別子 `success`・`invalid`） |
| `cid.py` struct の tag | 8 | 4（`event`・`result`・`staging`・`tx_request`、どれも一般語。Intel に特有の 3 と `rx_mpdu` は 0） |
| `cid.py` field | 105 | 80（下の理由） |
| `cid.py` 関数 | 13（＋suffix 7） | 8（＋suffix 0） |
| `csim.py ident` k=8／k=12（OpenBSD の 3 file） | 126／69 | 122／69（残りは IEEE の 5 GHz の channel 番号の並びと、Intel の予約の MAC address 02:cc:aa:ff:ee:00 の数の並び） |
| `csim.py norm` k=20 MIND=10 | 425（0.41%） | 415（0.40%）（C の定型の並び。p001 と同じ性質） |

残った一致とその理由:

- field の 80: 英語・802.11・PCI の一般語の単語（`flags`・`length`・`status`・`channel`・`key`・`tid`・`bssid`・`aifsn`・`dtim_period`・`short_slot`・`timestamp`・`tsf`・`subdevice`・`revision` など）と、hardware の機能の用語としての単語（`tfd`・`nvm`・`pnvm`・`iml`・`lmac`・`sku`・`mac_type`・`rf_id`・`rf_type`・`ltr_enabled`）、zedBSD 自身の設計の名前が偶然 Linux の名前と同じ物（`channel_profile`・`last_channel`・`last_error`・`frame_timestamp`・`beacon_timestamp`・`rx_status`・`pnvm_size`・`station_id`・`key_index`・`byte_count`・`channel_count`）。p001 が「Intel に特有」とした field（`gp2_on_air_rise`・`mac_sku_flags`・`phy_sku`・`ebs_status`・`failure_rts`・`failure_frame`・`lar_enabled`・`prph_info`・`prph_scratch_size`・`umac_prph_offset`・`error_event_table`・`imr_enabled` など）と boot context の field は全て付け直し、一致は 0。
- 関数の 8: driver の一般の動詞（`attach`・`match`・`resume`・`suspend`・`delay`・`read32`・`rx_dispatch`・`ltr_enabled`）。iwx・iwlwifi に特有の名前（`nic_lock`・`post_alive`・`set_hw_ready`・`prepare_card_hw`・`pnvm_parse` 等）は付け直した。
- 単語としての Intel の用語（TFD、PRPH、CSR、LMAC・UMAC、PNVM、NVM、SKU、IML）は、interface の部品の名前として識別子の一部や comment に残る。

## 実装から分かった事実のうち、今回の記録で新しく分かったこと（動作は変えていない）

1. **起動時の latency tolerance（CSR 0x0d4）の no-snoop の半分**: zedBSD は `0x82fa` を書く（scale の bit を bit 25 に置いている）。Linux v6.17 `iwl-csr.h` の layout（scale は bit 28:26、値は bit 25:16）で読むと scale 0・値 762（762 ns）になり、snoop の半分（scale 2・値 250、約 256 us）や Linux の書く値（両方約 256 us）と違う。OpenBSD は shift 24 と mask 0x1c000000 の組み合わせで scale が 0 になる。mmio.c の block の comment に事実として書き、直していない。直すなら別の Phase（QEMU・実機の確かめが要る）。
2. **platform の command（SOC_CONFIGURATION）の flag 0x0a**: Linux v6.17 `fw/api/system.h` の layout では bit 0（discrete）が 0、bit 1（low-latency の crystal）が 1、bit 3:2（LTR の適用の遅れ）が 2。Linux は discrete の part で bit 0 を立てる。test machine の AX211 は MAC が platform の中にある CNVio2 の part。runtime.h に事実として書き、直していない。

## T1（QEMU）への依頼（未実施）

- image: P3 の branch の commit（下の SHA）を main に merge した tree で、`plan/ws004/tests/config-ax211-main.mk`（または CI の `config/ci/config-amd64.mk`）の build。
- 流す試験: (a) 起動の回帰（`plan/tools/boot-test.sh`、login prompt）。AX211 の driver は QEMU では attach しない（device が無い）ので、kernel が driver を含んで起動することの確認。(b) license: image の `/usr/share/licenses/` に `intel-ax211-driver/` が無く、`intelax211-firmware` を選んだ image では firmware の LICENSE が今までどおりあること。
- 合否: (a) login prompt、(b) path の有無。

## 実機（5320／5330 の AX211）への依頼（未実施）

AX211 は Latitude 5320・5330 の WiFi で、QEMU では driver の hardware の経路を確かめられない。今回の変更で hardware に届く違いは MSI-X の vector map の書く順（同じ 18 byte・同じ値、0x15 が 0x16 の前に）だけで、他は名前と comment。実機（または 5330 の AX211 passthrough、Guardrail の手順）で次を見る:

1. 起動して AX211 が attach し、firmware が ALIVE を報告し、PNVM の受理、NVM の読み取り、runtime の command 列が通る（kernel log の intel-ax211 の行に error が無い）。
2. scan で周囲の AP が見える（2.4 GHz と 5 GHz）。
3. WPA2（CCMP）の AP に接続し、DHCP で address を得て ping が通る。切断と再接続も。
4. 数分の通信で firmware の fault（error table の log）が出ない。

## 残り・再開条件

- Q1 による削除（下の path）と、T1・実機の結果。両方が PASS なら Q1 が cleared にする。
- WS129 の生成物の `intel-ax211-driver` の行（Q1 が inventory を作り直す時）。
- 上の 2 つの事実（latency tolerance の no-snoop、SOC の flag）を直すかどうかは別の判断（Q1・ユーザー）。

## 削除の依頼（Q1 へ）

- `userland/base/licenses/intel-ax211-driver/LICENSE`
- `userland/base/licenses/intel-ax211-driver/Makefile`（directory ごと）
- `plan/ws004/tests/run-intel-ax211-driver-license-test.sh`（参照は README から外した。master の Tools・試験の一覧、未完了の Phase、tests/ のシナリオ、T1 の台帳からは参照されていない）
- P3 の worktree の `build/tmp/zedbsd-ax211-*`・`build/tmp/zedbsd-intel-ax211*`・`build/tmp/license-inventory.*`・`build/tmp/ws218-*`、`build/ws218-p002/`（作業の出力）
