<!-- awesome-plan project=zedbsd record=ws269-p002 -->

# ws269-p002 資料の固定（firmware・FWIF の版・Mesa・BRN・vendor の値）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §6 の p002・§0.3・§0.4・§4
- Queue: q1326 / q1326-i01（P2、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: 資料の版と SHA（firmware 8a58f81883f7・rogue_36.50.54.182_v1.fw の sha256 が S6 と一致、mainline v7.3、Mesa 26.2.5）と BRN・feature・vendor の値を記録した。mirror の要否はユーザーに聞く）
- ユーザーの判断: 決定済み（ws.md の「決定」節、D1 (a)・D2 (a)・D3 (a)・D5 (a)・D6 A・D7 (a)）。

## 目的と受け入れの条件

design.md §6 の p002 の行と §6 の末尾の必須項目（§0.4 の BRN の組と XE_TPU2 の記録）に従い、以降の Phase が参照する資料の版を固定する。

1. firmware の file・LICENSE・WHENCE の出所（fork の branch と commit）と sha256。mirror の要否を Q1 に返す。
2. FWIF・register の値を取る mainline の tag と、使う file の blob の SHA。FW の版との対。
3. Mesa の参照の file（`bxe-4-32.h`・`pco_isa.py`・csbgen の XML）の版・blob の SHA・license の行。
4. BRN・feature の組の記録（照合ではない）。
5. vendor の DT と DDK の値の記録、§0.3 の未確認の整理。

firmware は逆アセンブルしていない（header も今回は読み直していない。sha256 が S10 の file と同じなので S10 の記録がそのまま当てはまる）。

## 結果

### 1. firmware（S6・S7）

| 項目 | 値 |
| --- | --- |
| 出所 | https://gitlab.freedesktop.org/imagination/linux-firmware branch `powervr`、head `8a58f81883f7be458daa34e418cc4079f995b279`（2026-04-15T11:14:09+01:00、「powervr: update Imagination Rogue firmware images」）。2026-10-19 に GitLab API で確認、design.md S6 の head 8a58f818 と同じ |
| `powervr/rogue_36.50.54.182_v1.fw` | 131072 byte、sha256 `59127d3a02e48876310a520b7dc355f7430d3748d2940caaa33f39237d517abb`（S6 の値と一致） |
| `LICENSE.powervr` | 2190 byte、sha256 `1c9aa6bd6703a7ce1cdb879542fa1d8aca115a327bd819b193c971de9c53f402` |
| `WHENCE` | 341319 byte、sha256 `ec86d7649ff9e80e728b129dd725db1728f8a441cf342f1715c0a76dcf8146c3`。powervr の節（8404-8424 行）: `Version: 1.1.OS@6976702`、`License: Redistributable. See LICENSE.powervr for details` |
| kernel.org の linux-firmware | `powervr/rogue_36.50.54.182_v1.fw` は 2026-10-19 も 404（未 merge のまま） |
| 取得物の置き場所 | `/home/awe/zedBSD-worktrees/p2/build/ws269-p002/fw/`（git に入れない。P2 の worktree の build/ の中。p003 は mirror か上の URL から sha256 で取り直す。片付けは Q1） |

**mirror（Q1 への返却）**: `awemorris/zedBSD-firmware`（main、2026-10-16 更新）に `powervr/` は無い（2026-10-19 `gh api` で一覧を確認）。fork の branch は未 merge・保護なしで書き換え得るので、design.md §4 の通り mirror が要る、と判断する。P2 は push しない。Q1（ユーザー）に、既存の流儀（README の表の行、directory に firmware・`LICENSE.powervr`・`WHENCE`・`README.md`、`SHA256SUMS` の行）で `powervr/rogue_36.50.54.182_v1.fw` を足す事を依頼する。p003 の依存。

### 2. mainline の tag（FWIF・register・MMU の値の版）

- tag: **v7.3**（`c03c1a14507dfaf8b856c2608f9ac216021fc222`、2026-10-19 時点の torvalds/linux の最新の tag）。以降の Phase の `Value(s) referenced:` / `Procedure referenced:` はこの tag を書く。
- `pvr_device.c:37` `PVR_FW_VERSION_MAJOR 1` → FW の file 名の `_v1` と対（FW 1.1.OS@6976702 ↔ v7.3）。
- v7.3 の SUPPORTED は 36.52.104.182・36.53.104.796 だけ（`pvr_device.c:534-536`）。36.50.54.182 は UNKNOWN の扱い。match の compatible は `thead,th1520-gpu`・`img,img-rogue`・`img,img-axe`（`pvr_drv.c:1497-1511`）。binding（`img,powervr-rogue.yaml`）に BXE の行は無い。v7.3 の `jh7110.dtsi` に gpu の node は無い。
- 使う file の blob SHA（v7.3、`drivers/gpu/drm/imagination/`）:

| file | blob |
| --- | --- |
| pvr_rogue_fwif.h | 5d590c4c25663e431601dd26944465b62c31526b |
| pvr_rogue_fwif_shared.h | fe54c1cad7a9c5383b09607c64421a70631226a4 |
| pvr_rogue_fwif_common.h | 6ebb95ba98a60fc29d4c3bd1ed1219cc097b64d8 |
| pvr_rogue_fwif_client.h | 6e224400083a2c5c85cffb76e8858d61ce47e729 |
| pvr_rogue_fwif_check.h | 5048866359b9dcbfed905b13924c095cecedecaf |
| pvr_rogue_fwif_dev_info.h | 168277bce9485cef1e9de69de3cdeb8d84e39089 |
| pvr_rogue_fwif_sf.h | 56e11009e1230ad552e8bad9a1b12e1cf22b6aca |
| pvr_rogue_fwif_resetframework.h | 1db1f4c532bc6b07bbd78fb582350bc08f49dff7 |
| pvr_rogue_fwif_stream.h | 1c2c4ebedc2575ce99eb3ac75ad59fc1f58b7429 |
| pvr_rogue_cr_defs.h | 790c97f80a2ac03ac76b933d009a2f9cfc6003f7 |
| pvr_rogue_mmu_defs.h | f361ccdd540517d78009ebb854e830c13644fde9 |
| pvr_rogue_mips.h | 41ed618fda3f9de369818c3e26b4ecd7687ed40c |
| pvr_fw_info.h | c3639440610eb16ce7118d32d0f90410db76d1be |
| pvr_fw.c | 850a3ec8e7750fd88fc73c3991df5c4b7eb60079 |
| pvr_fw_mips.c | 6914fc46db50131aa158ed752d5dab67774ee9ce |
| pvr_vm_mips.c | 5847a1c92bea8b0923628ad7b72913e8977d4b97 |
| pvr_fw_startstop.c | e24ed6fc4362f3c86c439a8b6a39ec800d5e1167 |
| pvr_device_info.c | d3301cde7d119e7dc0f04f5a069d49e187ef7925 |

- design.md の行番号（S8・S8a・S8b・§0.4）はレビュー時の最新 mainline か v6.12 の clone の物。v7.3 の行番号と違い得るので、実装の Phase は v7.3 で開き直して `Value(s) referenced:` に書く（未確認: 行番号の一致）。

### 3. Mesa（S5・S11）

- 版: **mesa-26.2.5**（`ddaf85e353442d9d7f192fdcf1823e652e3f2ee3`、2026-10-19 時点の最新の安定版。26.3.0-rc1 は使わない）。
- `docs/drivers/powervr.rst`: BXE-4-32（36.50.54.182）は「unsupported and not under active development」（S5 と同じ）。

| file | blob | license の行 |
| --- | --- | --- |
| src/imagination/common/device_info/bxe-4-32.h | 318b7967147b027112a17507ef4a66cd63879f62 | `SPDX-License-Identifier: MIT`、Copyright 2025 Imagination |
| src/imagination/common/device_info/bxm-4-64.h | ff99088080e9b28e3146cca63dff82393d435abf | （比較用、未読） |
| src/imagination/pco/pco_isa.py | d8e90778e645394e9eff9f52dbad8c5716bb704b | `SPDX-License-Identifier: MIT`、Copyright 2024 Imagination |
| src/imagination/csbgen/rogue/{cdm,cr,ipf,kmd_stream,lls,pbestate,pds,ppp,texstate,vdm}.xml | cdm 83cf0e89…、cr 82302665…、ipf 1d5322a2…、kmd_stream 8d3c24e0…、lls a736a938…、pbestate b06f02db…、pds b774fe41…、ppp 61605e64…、texstate d4028d75…、vdm f6a8a84a…（tree `rogue` 7c47ac698a4a66dd012de3c7f0f07cefec92a91e） | pds.xml の頭は MIT の許諾文（Copyright 2022 Imagination）。他の XML は同じ形と推測（未確認） |
| src/imagination/csbgen/gen_pack_header.py | 4060b5280cae35d37dffc74e313acda930c82342 | Copyright 2022 Imagination・2016 Intel |

### 4. BRN・feature の組（記録、照合ではない、§0.4）

| 出所 | BRN / ERN |
| --- | --- |
| FW の header の device info（S10、同じ sha256 の file） | BRN 63553・71242 |
| host 側（DDK 1.19、`rgxcore_km_36.50.54.182.h:61-62`、S9f） | BRN 63553・71317。71317 は mainline の FW の enum（S8a）の外なので host の定数として持つ |
| Mesa の userland（`bxe-4-32.h`、user の compiler・driver の側） | quirks: BRN 70165・72168・72463・74056。enhancements: ERN 35421・38748・42307・45493 |

- kernel の driver の BRN の組は §0.4 の通り「FW の header の 63553・71242」と「host の 71317」の和。71242 は GPU_MULTICORE_SUPPORT があるので適用されない（§0.4 の推測のまま）。Mesa の BRN は user の Vulkan の道（p008 以降）で使う物で、kernel の組とは別に記録する。
- XE_TPU2: FW の header にあり DDK 1.19 の config に無い（S10、§0.4）。reset の手順は v7.3 の `pvr_fw_startstop.c` を参照する。
- Mesa の feature の主な値（`pvr_device_features_36_V_54_182`）: num_clusters 1、num_raster_pipes 1、common_store_size_in_dwords 16384、max_partitions 10、isp_max_tiles_in_flight 4、tile 16×16、usc_slots 64、unified_store_depth 208、max_usc_tasks 104、slc_cache_line_size_bits 512、fbcdc_algorithm 50、max_multisample 4、tpu_parallel_instances 4、gpu_multicore_support あり、roguexe・s8xe あり。S10 の FW の header の値（COMMON_STORE 16384、ISP の tile 4、MAX_PARTITIONS 10）と同じ。

### 5. vendor の DT と DDK（S9）

- clone: `~/visionfive-linux` 4cecf169f38eb94b40e307f5f870055e4d9d64f1（2025-09-29）、branch `JH7110_VisionFive2_6.12.y_devel`。
- DDK: `include/pvrversion.h:47-52` → 1.19、build 6345021（`1.19@6345021`）。FW の 1.1.OS@6976702 とは別の系列（vendor の kernel は DDK の FW を使い、zedBSD は mainline の FWIF と fork の FW を対で使う）。
- DT（`jh7110.dtsi:1403-1422`、design.md S9b の通り）: compatible `img-gpu`、reg `0x18000000/0x100000` と `0x130C000/0x10000`、clocks 6 個（clk_bv・clk_apb・clk_rtc・clk_core・clk_sys・clk_axi）、resets rst_apb・rst_doma、power-domains PD_GPUA、interrupts 82、`current-clock = <8000000>`、status disabled。VisionFive 2 の板の dtsi（`jh7110-starfive-visionfive-2.dtsi:567-569`）で `status = "okay"`。
- §0.3 の 2 つ目の reg: vendor の `sf_7110/sysconfig.c:462` は `platform_get_resource(..., IORESOURCE_MEM, 0)` だけを使い、index 1 を読むのは他の SoC の glue（`mt8173_mfgsys.c:232`）だけ。**事実: vendor の driver は 0x130C000 を使わない**。sf_7110 に SOC_TIMER の参照は無い。FW の SOC_TIMER との関係は未確認のまま（p005 で FWIF の init の値を作る時に見る）。

### 6. §0.3 の残り

| 項目 | 状態 |
| --- | --- |
| mainline の tag と FW の版の対 | 固定（v7.3 ↔ 1.1.OS@6976702、major 1） |
| Mesa の版と license の行 | 固定（26.2.5、MIT。csbgen の pds 以外の XML の行は未確認） |
| S10 と S9c の BRN の違い | 記録（上の 4） |
| 実機の DTB に GPU の node があるか | **未確認**（実機の DTB を読む必要がある。vendor の source の板の dtsi では okay。D2 (a) の組み込みの記述で動くので p004 の妨げではない。p004 の実機の起動で DT の道を通ったかを 1 行出すと分かる） |
| 2 つ目の reg | vendor の driver は使わない（上の 5） |

## 実行したコマンド（要約）

- `curl` で GitLab API（`projects/imagination%2Flinux-firmware/repository/branches/powervr`、tree）、raw の 3 file を取得、`sha256sum`。
- `gh api repos/awemorris/zedBSD-firmware[/contents...]`、`gh api repos/torvalds/linux/tags`、`.../contents/drivers/gpu/drm/imagination?ref=v7.3`、`jh7110.dtsi`・binding の yaml。
- GitLab API で Mesa の tag と tree、raw で `bxe-4-32.h`・`pco_isa.py`・`pds.xml`・`gen_pack_header.py`・`powervr.rst` の頭。
- `~/visionfive-linux` で grep（読むだけ）。

拒まれた操作は無い。

## 残り・Q1 への返却

- mirror: `awemorris/zedBSD-firmware` に `powervr/` を足す（Q1・ユーザー）。p003 はこれを待つか、mirror が出来るまで fork の commit `8a58f818…` の raw URL を sha256 で検証して使う（どちらにするかは Q1）。
- `build/ws269-p002/` は使い捨て（P2 の worktree の中）。片付けは Q1。
