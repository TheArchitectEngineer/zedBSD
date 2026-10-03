# Gen12 PS/compute ハング 第23報 — M0 完了。正本 source 確定・台帳の読み違い訂正（実装前の整備）

ご指摘（renderstate/clock-gating の読み違い、L0.2 先行、台帳の入口/分岐/効果の分離、行訂正、manifest 補足）を
すべて反映し、**M0（正確な参照と台帳整備）を完了**しました。新しい GPU 実行結果ではなく、参照資料と移植計画の
整備が今回の成果です。実装（M1/M2）はこの確定台帳の上で進めます。

## 1. 正本 source を確定（L0.2 / L0.3 完了）
ご指定の手順で、稼働 i915 に対応する source package を module 側から確定しました。
- `dpkg-query`: **source:Package=linux, source:Version=6.8.0-139.139**、`/proc/version_signature=
  Ubuntu 6.8.0-139.139-generic 6.8.12`、i915.ko srcversion=`F4AF37620F4AC270E54227A`。
- GPU を握らない構成（boot-nogpu.sh）でゲストを起動し、deb-src を有効化して
  `apt-get source --only-source linux=6.8.0-139.139` を取得。dsc/orig/diff の SHA256 を manifest に記録。
- **port 対象ファイルの ADL-P 経路は upstream v6.8.12 と同一**であることを diff で確認しました：
  ```
  SAME: i915_driver.c i915_gem.c gt/intel_gt.c gt/intel_gt_pm.c gt/intel_renderstate.c
        intel_clock_gating.c gt/intel_wopcm.c intel_pcode.c
  DIFF: gt/intel_workarounds.c (+3行: Wa_14019877138 は xelpg_ctx_workarounds_init=Meteor Lake, 非ADL-P)
        gt/intel_mocs.c        (1行: IP範囲 12,70-71 → 12,70-74 = Xe-LPG, 非ADL-P; 46a8 は Gen12.0)
  ```
  → **8086:46a8(ADL-P) では Ubuntu 6.8.0-139.139 i915 は v6.8.12 と機能的に同一**。差分 2 件は非 taken 分岐。
  作業基準 v6.8.12 は正当と確認し、Ubuntu 版 tree を正本として repo 保存しました（比較用に v6.8.12 も保持）。
- config 採取: **CONFIG_DRM_I915_DEBUG_GEM=未設定** → P7.11 verify_workarounds は `return 0`（NOT_TAKEN 確定）。
  PXP=y / GVT=y / WERROR=未設定。param 実値 enable_guc=0（modprobe.d 経由）、enable_dc=-1。

## 2. 台帳の読み違いを訂正（すべて保存 source で確認）
ご指摘は正確でした。source で裏を取り、以下を撤回・修正しました。
- **C1 撤回**: `intel_renderstate_emit` は Gen12 で **batch を発行しません**。`render_state_get_rodata()` の switch は
  ver 6/7/8/9 のみ→Gen12 は NULL→`so->vma==NULL`→`if(!so->vma) return 0`。P8.3 は「呼び出し + context pin/unpin +
  対象世代で発行 batch 無しの分岐」を移植します（Gen9 流用も新 batch 創作もしません）。
- **C2 撤回**: `intel_clock_gating_init` は ADL-P で **nop**。hook 選択に ALDERLAKE/gen12 分岐が無く
  `nop_clock_gating_funcs` へ。P6.4 は hook 選択と呼び出し位置を移植し、**HW 設定を足さないのが一致**とします。
  → 「golden の欠落核 2 点」を先行実装する案は取り下げました。「関数が呼ばれる」と「その GPU で
  レジスタ/batch を発行する」を、今後の台帳では必ず分けます。
- 行訂正（source 確認済）: P0.5 `intel_vgpu_detect`（vGPU 限定でなく、BAR 領域を map し magic 確認→物理 passthrough
  は非 vGPU になる経路を再現）、P2.9 `intel_pcode_init`（`!IS_DGFX → return 0`、ADL-P に DGFX poll を新設しない）、
  P6.2 `intel_wopcm_init`（`!guc_fw_size → return`）、P7.5b `intel_set_mocs_index`（SW 状態 index のみ、HW 書込の
  `intel_mocs_init` と別行）、P7.7 uC（enable_guc=0 でも `init_hw=__uc_check_hw` は残る。init/init_hw/early/cleanup を区別）。
- 呼び出し一覧の追加漏れ: **setup_private_pat**（i915_gem_init の firmware/WOPCM と同 loop、`i915_init_ggtt` の前＝
  既存 PAT 修正の正しい実行位置）、**gt->vm=kernel_vm(gt)**（gt_pm_init 後・mocs_index/engines_init 前）、
  **sanitize_gpu**（mmio_probe 末尾、`intel_gt_init_mmio` とは別呼び出し。元位置で独立記録）。

## 3. 台帳 rev2 の構造
各行を「入口が呼ばれる条件 / 評価する guard と入力 / 選ばれる callback / 実行する効果 or 早期 return 理由 /
zedBSD 状態 / 根拠(SOURCE/CONFIG/RUNTIME)」に分離。NOT_TAKEN は**具体的分岐**に付け、親関数ごと削除しません。
作業単位 M0..M5 を定義し、実装は P 実行順を保ちつつ依存で刻みます（P8 は P9 部品を使うが実機呼び出し順は前倒ししない）。

## 4. manifest への補足（ご指摘 3 点）
- **firmware と feature init を分離**: DMC=blob load（adlp_dmc.bin v2.20, 成功）／PXP=feature・component init
  （CONFIG_DRM_I915_PXP=y、named blob load ではない）を別欄化。
- **IOMMU を 3 層で分離記録**: (1) ホスト IOMMU（vfio remap, host-phys-bits=39→MGAW=39）／(2) ゲスト仮想 IOMMU
  （guest dmesg: Translated, lazy）／(3) ゲスト i915 の DMA mapping（driver が作る mapping、(2) と別）。
  QEMU 引数抜粋だけからゲスト構成を推測しない旨を明記。
- **GPU 引き渡しを per-VM 管理**: PID/QMP で対象 VM を管理し正常終了を基本、強制終了は別条件で記録。
  「teardown で bus reset」は**未確認の前提**として扱い、新 reset 操作を闇雲に足さない旨を記載。
  （今回、source 採取用ゲストは GPU 無し起動→正常 poweroff で終了。GPU は vfio-pci 維持。）

## 5. 次（M1 → M2）
1. **M1 適合層**の契約を定義・実装（特に **CPU addr / DMA addr / GPU VA の 3 分離**、mapping 属性、barrier、
   forcewake/PM 参照管理、資源寿命）。未実装を成功で隠さず、開発版は未実装到達点で停止。
2. **M2 P0〜P2**を Linux i915_driver_probe 構造へ置換（early probe/MMIO/device_info/**sanitize_gpu**/DMA mask→
   GGTT probe/init/enable→bus master→MSI の順序保持、失敗経路も移植）。
   既存 PAT 修正を setup_private_pat の位置へ統合。
次報で M1 契約 + 実装差分 と P0〜P2 実行記録（呼び出し順・選択 guard/callback・戻り値・資源 acquire/release、
未実装到達点の明記）を提出します。この段階で C2 改善結果は求めません。

## 6. 確認（任意）
- L0.2 で「ADL-P 経路は Ubuntu=v6.8.12 同一」を確認したので、**実装は保存 v6.8.12/ubu-i915-src のどちらを引用しても
  同一**です。正本表記は Ubuntu、引用は同一内容、で進めます（相違が出れば ubu 優先）。この方針でよろしいでしょうか。
- M1 で forcewake-ALL 常時保持の是正は、対応する get/release/PM 参照管理がそろう P7.8(gt_resume)実装と同一単位で
  行う計画です（解放だけ先行しない）。この単位取りで問題ないかご確認ください。

（成果物: `plan/ws031/linux-parity/`{ledger.md(rev2), linux-reference/{manifest.txt(rev2), ubu-i915-src(正本),
 i915-src(v6.8.12 比較用), ubu-uapi-drm, uapi-drm, config-6.8.0-139-generic, ubuntu-source-hashes.txt, fixtures}}。
 git 操作なし。build 影響なし。GPU vfio-pci 維持。）
