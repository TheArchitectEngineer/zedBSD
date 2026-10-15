<!-- awesome-plan project=zedbsd record=ws248-p001 -->

# ws248-p001: AX211 の driver の出典を block ごとの referenced: に

Status: cleared（2026-10-15、q1201 P2。確かめは下の「結果」）
Parent: [WS248](../ws.md)
Approval: 2026-10-15 ユーザー（ws.md の原文）

## 範囲

ws.md の「完了の条件」の出典の 3 項目。src/drivers/wifi/intel-ax211/ と include/drivers/wifi/intel-ax211/ の comment だけ。動作は変えない。

## 確かめ

build warning 0、AX211 の host 試験、`cpp -fpreprocessed -P` の前後の比較で comment 以外に差が無い。

## 結果

2026-10-15 P2（worktree p2、branch agent/p2-ws248、基点 main d66559d02）。

### 変えた物

- 39 file（src/drivers/wifi/intel-ax211/ の 38 と include/drivers/wifi/intel-ax211/pci-intel-ax211.h）。comment だけ。
- file の先頭の comment から「Sources of the values」「This file is not an adaptation」「Values of the 802.11 frame itself」の段落を除いた（33 file。intel-ax211.c は 2 か所）。file の役割の 1 文は残し、intel-ax211.c・internal.h・mmio.c・transport.c は役割の説明を短く書き直した。
- 「Source:」「Sources:」「cross-checked with」の形を、block の直前の複数行の comment の最後の段落「Value referenced:」（5）・「Values referenced:」（78）・「Procedure referenced:」（17）へ改めた（計 100 段落、31 file）。path は `Linux v6.17 drivers/net/wireless/intel/iwlwifi/…`・`OpenBSD 0f464d413c50 sys/dev/pci/…`・`linux-firmware 20260410 (commit dc85cced…) intel/iwlwifi/…` の形。
- 本文の出典の名前を除いた: 「the places Linux uses as well」「Linux names the register…」「as OpenBSD does; Linux restarts…」「the value OpenBSD writes」「Linux documents the unit」「OpenBSD names bit 5…; Linux v6.17 names…」「just as the pinned OpenBSD implementation does」「Uses the exact pre-BZ OpenBSD…」「OpenBSD-derived」「the OpenBSD byte order／sentinel」「OpenBSD-style legacy 11g」「Linux iwlwifi and OpenBSD iwx both ignore…」「Match those production drivers」「OpenBSD API89 uses zero…」「OpenBSD sends zero…」「OpenBSD supplies the former's…」「the value Linux gives…」。手順の出典は Procedure referenced: に移した。
- tx-ring.h の iwl-fh.h の comment の原文の引用（"A maximum of 255 (not 256!) TFDs…"）は comment の写しなので除き、値（255）の出典だけを referenced: に書いた。
- 20 file の 1 行目の Emacs の mode 行 `c-file-style: "linux"` を、同じ directory の他の file と同じ `tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8` の形にした（語の除去。editor の設定は同等）。

### 確かめ

- preprocess の比較: 変えた 39 file を `gcc -E -fpreprocessed -P -x c` に通し、空白を除いて d66559d02 と比べた。39 file とも同一（差 0）。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix`（CONFIG_DRIVER_PCI_INTEL_AX211=y）が rc=0、log の warning 0、kernel include check PASS、amd64 vmunix check PASS。
- host 試験（plan/ws004/tests/、pin した firmware の cache あり）: core・dma・transport・boot・runtime-start・firmware-loader の 6 本が全部 PASS（rc=0）。p001 は preprocess で差 0 なので、変更前の結果と同じ。
- 未実施: run-intelax211-firmware-package-test.sh（userland の firmware package の取得の試験で、network から取り、script が host の rm を使う。driver の source に依らない）、run-intel-ax211-vfio-qemu.sh（QEMU）。QEMU・実機は行っていない。

### 出典の分からない物・要確認（推測で付けていない）

- 版の無い出典: 「the PCI ID database」（internal.h・intel-ax211.c の PCI の identity）、「the PCI Local Bus Specification」「the PCI Express Base Specification」（intel-ax211.c）、「the Wi-Fi Alliance WMM Specification」（bss.c）。元の comment に版が無い。
- 関数名を、元の comment に無かった所に足した。名前は host にある FreeBSD 15.0 の写し（sys/contrib/dev/iwlwifi・sys/dev/iwx。OpenBSD の iwx を移した物）で存在を確かめたが、v6.17・0f464d413c50 そのものでは確かめていない: iwl_pcie_apm_init・iwl_pcie_apm_stop_master・iwl_pcie_apm_stop・iwl_pcie_set_hw_ready・iwl_pcie_prepare_card_hw（pcie/gen1_2/trans.c）、iwl_pcie_set_ltr（trans-gen2.c）、iwl_pcie_txq_alloc_response（tx-gen2.c）、iwx_apm_stop・iwx_prepare_card_hw・iwx_set_hw_ready・iwx_apm_init・iwx_flip_hw_address・iwx_is_valid_mac_addr・iwx_nic_init・iwx_send_cmd・iwx_fill_probe_req・iwx_enable_txq・iwx_mac_ctxt_cmd_fill_sta・iwx_run_init_mvm_ucode・iwx_rx_pkt、IWX_UCODE_TLV_*・IWX_MSIX_HW_INT_CAUSES_*。このうち iwx_send_cmd（wide header の version に 0 を書く）と iwx_mac_ctxt_cmd_fill_sta（DTIM の無い association）は、FreeBSD の写しの中身が 0f464d413c50 と違う所があり、手順の対応は未確認。
- 名前の無い referenced: tx-ring.h の `drivers/net/wireless/intel/iwlwifi/fw/api/tx.h`（元の comment にも名前が無い）。「(all offsets)」「(the bits of the other registers)」など、struct・define の名前でなく説明で書いた所がある。

### 出典の無い値

interface の値（register・bit・command・layout）で referenced: の無い block は見つからなかった（定数の group を機械的に列挙し、残りは zedBSD の内部の result・state の enum と、comment に zedBSD の上限・時間と書かれた物だけ）。PCI の identity と firmware の file の size・SHA-256 は zedBSD の測定で、comment にそう書いた。

### 残り（範囲外）

- comment の中の開発の管理番号（`ws004 HW-T37`・`ws004 p038`・`ws052-p005`・`ws218-p002`・`BUG-158`）は残した（WS248 の指示の外）。
