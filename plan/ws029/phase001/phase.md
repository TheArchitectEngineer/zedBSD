<!-- awesome-plan project=zedbsd record=ws029-p001 -->

# WS029 p001: 対象確定・ライセンス境界・移植方針の固定

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i01 cleared (whole Phase)
Dependencies: ws014-p010 cleared (drv_gpu ops v9, isolation)
Next: ws029-p002
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p001`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 120 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

実装を始める前に、対象 hardware、参照する Linux ファイルのライセンス、転記と新規実装の境界、VFIO 試験の前提を確定し、ユーザーの承認を記録する。host の状態は一切変更しない。

## 手順（順に実行、各項目は成果物を残す）

1. **移植方針の承認**: [設計資料 §2 決定 4](../i915-design.md) を要約してユーザーへ提示する。「MIT の定義/テーブルを出典付き `.inc` へ転記し、driver 論理は zedBSD 規約で新規に書く」に対する回答（承認、または「Linux 論理も転記する」等の変更）を `plan/ws029/phase001/approval.json`（回答原文、日時、SHA256）へ保存する。変更指示なら p002 以降の見積と本文を更新し、Queue を続ける前に報告する。
2. **参照ファイルの固定と監査**: `plan/ws029/tests/fetch-linux-refs.sh` を作る。固定 tag（既定 `v6.19`。`git ls-remote --tags https://github.com/torvalds/linux v6.19` で存在確認し、無ければ存在する最新の `v6.x`）を使い、設計資料 §10 の全ファイルを `https://raw.githubusercontent.com/torvalds/linux/<tag>/drivers/gpu/drm/i915/...` から `plan/ws029/temp/linux/<tag>/` へ取得する。各ファイルの SHA256 と先頭 40 行の SPDX/permission notice を機械判定し、`MIT` 以外（`GPL-2.0`、`GPL-2.0-only`、dual）が 1 つでもあれば非 0 で終了する。結果を `plan/ws029/i915-license-audit.md`（表: path、tag、SHA256、判定、根拠行）に書く。GPL のファイルは参照元から外し、代替（同じ定義を持つ MIT ファイル）を記録する。
3. **hardware 事実の再確認**: `plan/ws029/tests/host-i915-facts.sh`（`awe@10.0.10.25` で読取専用、`sudo -n` は `lspci -vvv`、`dmidecode`、`/sys/kernel/debug/dri/0/i915_capabilities` の読出しにだけ使う）で §1 の全項目を JSON 化し `plan/ws029/phase001/host-facts.json` に保存する。差異があれば設計資料を更新する。
4. **転記 symbol 一覧**: `plan/ws029/i915-symbols.md` に、p002–p005 が使う symbol（例: `GEN6_GDRST`/`GEN11_GRDOM_*`、`FORCEWAKE_*_GEN9`/`FORCEWAKE_ACK_*`、`GFX_FLSH_CNTL_GEN6`、`GEN11_GFX_MSTR_IRQ`、`GEN11_GT_INTR_DW*`、`GEN11_INTR_IDENTITY_REG*`、`GEN11_IIR_REG*_SELECTOR`、`GEN11_RENDER_COPY_INTR_ENABLE`、`GEN11_RCS0_RSVD_INTR_MASK`、`GEN11_BCS_RSVD_INTR_MASK`、`RING_HWS_PGA`/`RING_MODE_GEN7`/`RING_MI_MODE`/`RING_START`/`RING_CTL`/`RING_HEAD`/`RING_TAIL`/`RING_EXECLIST_SQ_CONTENTS`/`RING_EXECLIST_CONTROL`/`RING_EXECLIST_STATUS`/`RING_RESET_CTL`/`RING_CONTEXT_STATUS_PTR`、`GEN8_CTX_VALID`/`GEN8_CTX_ADDRESSING_MODE`/`GEN11_SW_CTX_ID_SHIFT`、`GEN12_CSB_*`/`GEN12_CTX_STATUS_*`、`gen12_xcs_offsets`/`gen12_rcs_offsets`、`MI_BATCH_BUFFER_START`/`MI_BATCH_BUFFER_END`/`MI_NOOP`/`MI_USER_INTERRUPT`/`MI_FLUSH_DW`/`MI_STORE_DWORD_IMM_GEN4`/`MI_MEM_VIRTUAL`/`MI_SEMAPHORE_WAIT`/`XY_SRC_COPY_BLT_CMD`/`XY_COLOR_BLT_CMD`/`PIPE_CONTROL`、`GEN8_PAGE_PRESENT`/`GEN8_PAGE_RW`/`GEN12_PPGTT_PTE_PAT*`、`GEN12_GLOBAL_MOCS`）を、出典ファイル・行番号・gen12 での有効条件と共に列挙する。値は書かない。
5. **VFIO 前提の机上確認**: `plan/ws029/i915-vfio-plan.md` に、IGD UPT（画面出力なし）、iommu group 0 単独、RMRR relaxable、`vfio-pci` module、QEMU `-device vfio-pci,host=0000:00:02.0`、`sudo -n` で QEMU を起動する理由（memlock）、host 側の attach/restore 手順（設計資料 §8）、ユーザーが本指示で許可した操作（GDM/Wayland 停止、i915 の使用終了と unbind、vfio 経由の passthrough）と、許可されていない操作（host の reboot、package 導入、kernel cmdline 変更）を分けて書く。

## 完了条件

- `approval.json`（承認原文）、`i915-license-audit.md`（参照全ファイルが MIT、tag/SHA256 付き）、`host-facts.json`、`i915-symbols.md`、`i915-vfio-plan.md` が存在する。
- host の状態（GDM、i915 bind、`/dev/vfio`）は開始前と同じ（facts script が読取専用であることを diff で示す）。
- 記録を GitHub へ同期し読み戻す。

## 実行境界

source 変更なし。private host は読取専用アクセスのみ。ライセンス判定を推測で埋めない。

## q314 p001 完了: 対象確定・ライセンス境界・移植方針の固定（2026-09-14）

[WS029 p001](https://github.com/awemorris/zedBSD/issues/398)（q314-i01）を cleared にし、[p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02、driver 骨格）を in-progress にする。source 変更なし、host 状態変更なし。

成果物（すべて local）:
- `plan/ws029/phase001/approval.json`: 開始指示「では、実装してください。」を、MIT 定義/テーブルの分離転記＋論理新規実装、GDM 停止・i915 unbind・VFIO passthrough の承認として記録（設計資料 SHA256 付き）。firmware は必要時に `userland/firmware/<機種>/`。
- `plan/ws029/i915-license-audit.md`（`plan/ws029/tests/fetch-linux-refs.sh v6.19`）: 参照 29 ファイル（`drivers/gpu/drm/i915/` 26、`include/drm/intel/pciids.h`、`include/drm/intel/i915_drm.h`、`include/uapi/drm/i915_drm.h`）を tag v6.19 で取得し SHA256 を記録。判定は全ファイル MIT（SPDX MIT または MIT/X11 permission notice）。GPL のファイルは参照していない。script は MIT 以外があれば非 0 で終了する。
- `plan/ws029/phase001/host-facts.json`（`plan/ws029/tests/host-i915-facts.sh`、読取専用）: Latitude 5330、kernel 6.19.13+deb13-amd64、`00:02.0` = `8086:46a8` rev 0c、i915 bound、iommu group 0 単独（全 17 group）、`CONFIG_VFIO_PCI_IGD=y`、vfio 系 module 解決可、`/dev/vfio` は `vfio` のみ、GDM active、QEMU 10.0.11 に `vfio-pci` あり、RMRR `0x6c000000–0x707fffff`。設計資料 §1 と差異なし。
- `plan/ws029/i915-symbols.md`（`plan/ws029/tests/gen-symbols.py v6.19` が生成）: p002–p005 が転記・参照する 257 symbol を 10 群（補助 macro、device ID/PCI、forcewake、reset、engine register、割込み、command、LRC、GTT、MOCS）に分け、出典 file:line と gen12/ADL-P での有効条件を列挙。値は書いていない。未検出 0。論理の参照元関数（ggtt/ppgtt/uncore/irq/engine/lrc/execlists/reset/emission/mocs）を「転記せず挙動を新規実装」として別表に列挙。
- `plan/ws029/i915-vfio-plan.md`: IGD UPT（画面出力なし）、iommu group 0 単独、RMRR relaxable、`vfio-pci` module、QEMU `-device vfio-pci,host=0000:00:02.0`、`sudo -n` 起動の理由（memlock）、`host-igd.sh attach/restore/status` の手順、許可済み操作（GDM 停止、i915 unbind、VFIO passthrough、restore）と許可外操作（reboot、package 導入、cmdline/modprobe.d/udev/limits 変更、BIOS、他プロセス kill）、既知 risk の扱いを記載。
- `plan/ws029/phase001/host-facts-after.json` と `host-state-diff.txt`: p001 の前後で driver/GDM/`/dev/vfio`/drm node/cmdline が同一であることを示す。

判明した事項:
- `SNB_GMCH_CTRL`/`BDW_GMCH_GGMS_*` は v6.19 では `include/drm/intel/i915_drm.h`（MIT）にあり、`intel_pci_config.h` にはない。`I915_MOCS_PTE` は `include/uapi/drm/i915_drm.h`（MIT）の enum。両ファイルを参照一覧と監査に追加した。
- gen12 の CSB 判定は `GEN12_CSB_SW_CTX_ID_MASK`/`GEN12_IDLE_CTX_ID`/`GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE` を使い、`GEN8_CTX_STATUS_*` は使わない。CSB entry が `-1` のままの場合は `GEN8_EXECLISTS_STATUS_BUF`/`GEN11_EXECLISTS_STATUS_BUF2` の mmio mirror から読む（tgl HSDES 22011327657 相当）。
- LRC image の per-context batch pointer は gen12 では offsets 表の index 0x12（`lrc_ring_wa_bb_per_ctx`）で、`CTX_BB_PER_CTX_PTR` という define は存在しない。p004 では 0 を書く。

検証: 監査 script exit 0、generator 未検出 0、host 前後 diff すべて same。次: p002（`src/drivers/gpu/i915/` 骨格、PCI attach、MMIO/forcewake、GGTT、割込み）。
