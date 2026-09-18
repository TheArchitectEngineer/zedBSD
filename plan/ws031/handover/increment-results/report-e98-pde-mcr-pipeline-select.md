# WS031 E-98 報告: PDE 修正版で C1 再実行 → HANG（署名同一）。提出 bytes の Linux 比較で PIPELINE_SELECT の符号化誤りを発見

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 1. 実施したこと（指示の順）

| 指示 | 実施 |
|---|---|
| 2.1 PDE 修正が実表に反映されているか | 実表リンク=PRESENT\|RW（PPAT_CACHED_PDE）、scratch 塔=UNCACHED を分離維持。GPU-free 試験 +3（3 段のリンク属性と child DMA、leaf PTE の DMA 上位 bit・PAT 0・VA 非混入、未挿入頁→scratch[0]、未割当領域→scratch PDP） |
| 2.2 属性変更と公開処理を一組で | 正本 `fill_page_dma`／`write_dma_entry`／`gen8_ppgtt_insert_entry` の clflush に対応する `parity_gt_clflush` を表の書込み直後に追加（これまで表は WB ダイレクトマップに書くだけで flush 無し） |
| 2.3 新しく構築した表を提出直前に読む | `parity_gt_ppgtt_walk`：PDP0 から batch／IDD／kernel／EU marker／done marker の 5 VA を実表で辿り、各段の raw 64 bit・子表 DMA・leaf 属性を記録 |
| 3 EU 有効ビルドを先に通す | 別 BUILD dir で clean build（`-DPARITY_EU_TEST=1`）。GPU-free で ktest 371/0 を確認してから同一 image を実機へ。hash/flags を記録 |
| 4 MCR 3 件の読み戻し | `intel_gt_mcr_read` 相当（lock→selector 読→slice/subslice 設定・multicast 維持→読→復元）を slice 0 の全 DSS（0..4）で。EU 提出前 |
| 5 同じ C1 を 1 回 | E-97 と同条件（driver_register 後、forcewake 全保持、2 s、同一 batch 語）。停止時の記録を拡充（seqno と HWSP 観測値、CSB、context 識別、時間源） |

## 2. 結果（image: vmunix c1b35d79 / hdd df9a039b / BOOTX64 57f8eab6）

```
EU-TEST walk: 5 VA すべて PML4[0]=0x100329003 → PDP[4]=0x10032a003 → PD[2]=0x10032b003 → PT=…003（PWT/PCD 無し、pat=0）、PDP0=top と一致
MCR-PROBE: ROW_CHICKEN2(0xe4f4)=0xffff4100 / ROW_CHICKEN4(0xe48c)=0xffff0200 は全 DSS で期待 bit 一致
           SAMPLER_MODE(0xe18c)=0x00003020 は全 DSS で bit15(ENABLE_SMALLPL) が 0   ※E-27 の動作 Linux 読み値も 0x3020
EU-TEST record: rq seqno expected=2 hwsp_observed=1 (initial breadcrumb 着地) | ctx lrca=fffb9119 desc=00000020:fffb9119 | csb last=03ff8000:00008001 | time_base_fault=0
EU-TEST HANG: ready=c0ffee10 eu/done/cs=dead0000 idd_rb_ok=1 kernel_rb_ok=1 | ipehr=70040000 row_instdone=8610e87f fault=0 eu_dis=0 ack=3/3/3
teardown 正常、ktest 371/0（GPU-free／実機）
```

表現は是正どおりにします：**CS 側の実行と読み戻しは確認できたが、EU の期待書込みと request の正常完了は未確認。停止署名は以前と同じ。** SAMPLER_MODE の bit15 は Linux 側の読み値とも一致しているので Linux との差ではありません（上書きはしていません）。

## 3. 第 6 節（提出 bytes の比較）で見つかった誤り

Linux で完走した replay（`linux-c2-replay.c`、E-25）と、今回 zedBSD が実際に提出した batch（`e98-batch-as-submitted.hex`）を語単位で比べると、**PIPELINE_SELECT が違います**。

| | dword | 由来 |
|---|---|---|
| zedBSD（big-bang の draw／C0〜C3／golden、parity の eu_test、すべて） | `0x61041310` / `0x61041312` | `vk/linux/3dstate-gen12.inc` の `GEN12_CMD_PIPELINE_SELECT 0x6104`（CommandSubType 0） |
| Linux replay（完走） | `0x69041310` / `0x69041312` | 正本 `gt/intel_gpu_commands.h` `PIPELINE_SELECT = (3<<29)\|(1<<27)\|(1<<24)\|(4<<16)` = 0x69040000 |

genxml（gen125.xml）も CommandSubType=1／Opcode=1／SubOpcode=4、E-15 の設計メモにも `0x69041312` と書いてありました。つまり **zedBSD の batch では GPGPU パイプラインへの切替が一度も発行されておらず、3D 既定のまま MEDIA_VFE_STATE／GPGPU_WALKER を投入していた**ことになります。E-25 の「同一バイト」という報告は `.inc` 定数からの転記を同一と扱った私の誤りでした。

- 修正：`.inc` を 0x6904 に（出典コメント付き）、`eu_test.c` と GPU-free 試験の期待値も。EU 有効の clean build で ktest 完走を確認済み（image: vmunix b7de2a71 / hdd-image 861593e2 / flags -DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_EU_TEST=1、GPU-free ktest 371/0）。
- **修正版での C1 再実行は行っていません**（1 回の解除は消化済みのため）。ご指示があれば同条件で 1 回実行します。成功しても「累積修正版で EU 完了を確認」と記録します。
- 注意：PS 描画（3DPRIMITIVE）の停止は 3D 既定モードで起きているため、この誤りだけでは説明できない可能性があります。判断は再試験後に。
- replay とのその他の差（意図的）：batch VA（replay 0x100600000 / zedBSD 0x100401000）、zedBSD 側の IDD/kernel 読み戻し copy（診断追加分）。固定 fixture（SBA／VFE／MIDL／walker／marker／PC）は同語。

## 4. 提出物

- diff：`plan/ws031/handover/increment-results/e97-e98-changes.patch`（PDE 符号化・公開 clflush・実表 walk・default_state 継承・INHERIT 試験修正・eu_test・MCR probe・PIPELINE_SELECT 修正）
- 実データ：`e98-batch-as-submitted.hex`（322 dword）、walk／MCR／record 行はログ `run-parity-hw-e98-eu.log`（chaos）
- 台帳：E-98

git commit / push はしていません。HAL インタフェースは不変です。
