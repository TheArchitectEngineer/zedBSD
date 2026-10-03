# Gen12 PS/compute ハング 第22報 — 方針転換を受領。参照固定・source 保存・実装台帳を作成（直近着手#1 完了）

方針転換（候補試験を止め、動作 Linux の実行経路を一式移植し、実装台帳で管理する）を受領しました。
ご指定の「直近着手」3 つのうち #1（参照版固定 + 関数対応表）を完了し、#2/#3 の骨格を用意しました。

## 1. 参照環境の凍結（linux-reference/manifest.txt）
動作した positive-control 系（10.0.10.25）から採取し固定しました。**この版から動かしません。**
- kernel: **Ubuntu 6.8.0-139.139-generic**（mainline base **6.8.12**、gcc-13）
- i915: `Initialized i915 1.6.0 20230929`、**enable_guc=0**（execlists）
- firmware（実際にロードされた物のみ）: **i915/adlp_dmc.bin v2.20** + **PXP**。GuC/HuC は enable_guc=0 で不ロード。
- GPU: 8086:46a8 rev0c ADL-P GT2、1 slice/5 DSS/559 threads、display v13
- QEMU/VFIO: `q35,accel=kvm` / `cpu host,host-phys-bits-limit=39`（MGAW=39, これが無いと vfio DMA map=-22）/
  `vfio-pci,host=00:02.0,x-igd-opregion=on,rombar=0` / IOMMU=Translated,lazy
- fixtures（名前を固定、kernel+batch hash 付き）: MI-only / init-only(旧C0) / eu-empty(旧C2) / eu-store(旧C1)。
  c2replay sha256 d8cc6a8e…、softpin SHARED_VA=0x100400000 BATCH_VA=0x100600000。

## 2. 参照 source を保存（linux-reference/i915-src）
v6.8.12 の `drivers/gpu/drm/i915`（386 個の .c、13MB）と uapi-drm を repo 内へ保存。
Ubuntu 6.8.0-139 の GEM/GT/probe 経路 delta は小さいため v6.8.12 を作業基準にし、
**差分検出時に Ubuntu 版と照合**（台帳 L0.2）。以後、emitter/定義/分岐は説明から再構成せず、この source を保持して使います。

## 3. 実装台帳（linux-parity/ledger.md）
**実際の source から call tree を抽出**し、zedBSD 現状へ対応付けました（記憶からの再構成をしていません）。
確認した Linux 実経路:
```
i915_driver_probe: pci_enable_device → i915_driver_create → early_probe → intel_vgpu_detect →
  intel_gt_probe_all → mmio_probe → hw_probe → display_probe_noirq → intel_irq_install →
  display_probe_nogem → i915_gem_init → intel_pxp_init → display_probe → i915_driver_register
hw_probe: dram_edram_detect → i915_set_dma_info → perf_init → ggtt_probe_hw → aperture_remove →
  ggtt_init_hw → gt_tiles_init → memory_regions_hw_probe → ggtt_enable_hw → pci_set_master →
  pci_enable_msi → gvt_init → opregion_setup → pcode_init → dram_detect → bw_init_hw
i915_gem_init: init_userptr → uc_fetch_firmwares → wopcm_init → i915_init_ggtt →
  intel_clock_gating_init → intel_gt_init → intel_engines_driver_register
intel_gt_init: init_workarounds → forcewake_get(ALL) → init_scratch → gt_pm_init → set_mocs_index →
  engines_init → uc_init → intel_gt_resume → init_hwconfig → __engines_record_defaults →
  __engines_verify_workarounds → … → forcewake_put(ALL)
intel_gt_resume: gt_sanitize → gt_init_hw → rps_enable → llc_enable → per-engine engine_resume →
  rc6_enable → uc_resume
__engines_record_defaults: intel_context_create(A) → i915_request_create → intel_engine_emit_ctx_wa →
  intel_renderstate_emit(&so) → i915_request_add → wait_for_idle → default_state 保存 → 継承
```
現 zedBSD attach（`i915_start`: dma→bar→uncore→gt-reset→ggtt→busmaster→irq→engines→selftest→vk）を
P0..P11 の枠へ写像し、各行に UNIMPLEMENTED / PORTED / VERIFIED / NOT_TAKEN を付けました。
主要な UNIMPLEMENTED: 早期 probe/runtime PM、device_info_runtime_init、gt_init_mmio、set_dma_info(DMA mask を
GGTT 前)、memory_regions、opregion_setup、pcode_init、display_probe_noirq/nogem(power domain 判定込み)、
**clock_gating_init**、wopcm、init_scratch、gt_pm_init、**intel_gt_resume**(RPS/LLC/RC6/engine_resume/init_hw)、
verify_workarounds、通常経路の record_defaults。

## 4. source 接地で判明した構造的欠落 2 点（従来の selftest golden の核の欠落）
1. **intel_clock_gating_init**（i915_gem_init 内、intel_gt_init の**前**）。source コメントが明記:
   「display だけでなく **default-state 記録前に必要な context 設定を含む**」。zedBSD は未実装。
2. **intel_renderstate_emit**（__engines_record_defaults 内）。bootstrap context に emit_ctx_wa の**後**、
   **platform の null render-state batch**を流して 3D/compute pipeline の既定状態を image に焼き込みます。
   第20報群で実装した selftest golden は **これを欠いていました**（ctx WA 2 本のみ）。E-30 の golden で walker が
   直らなかったのは、この renderstate_emit + clock_gating_init の欠落が理由の可能性があります。
   （※ただしご方針どおり単独の候補として賭けず、通常経路への一式移植の中で正しく通します。）

## 5. 次の実装順（台帳の「直近着手」#2/#3）
1. 適合層（PCI/DMA/MMIO/同期/基盤）を定義し、最上位 attach を i915_driver_probe 構造へ置換（P0..P2 の順序:
   DMA mask → GGTT probe/init/enable → bus master → MSI を保持）。
2. intel_gt_init を P7 の順で移植（init_workarounds を **一覧生成 + emitter ごと**、init_scratch、gt_pm_init、
   **intel_gt_resume**、**__engines_record_defaults に renderstate_emit 込み**）。**clock_gating_init** を gt_init 前に。
3. 既存の PAT/AUX prologue/WA/golden を、この通常経路へ統合（selftest は通常入口を使うだけにする）。
受入試験は driver-ready 後に通常入口から MI-only→init-only→eu-empty→eu-store→PS の順、各段成功時のみ前進。

## 6. 伺いたいこと
1. **移植の刻み**: P0→P11 を上から順に（各段小コミット、単独でハング解消を前進条件にしない）で良いでしょうか。
   それとも「golden を正しく通す」ために **P6.4 clock_gating_init + P7.10/P8 record_defaults(renderstate_emit 込み)**
   を先に通常経路へ入れ、その後で P0..P2 骨格を整える、の順が良いでしょうか（どちらも簡略化せず全部やります）。
2. **display_probe_noirq/nogem**（P3/P5）の taken 範囲: headless/compute で **power domain 初期化のうち GT/render に
   作用する部分**はどこまで移植すべきか、能力判定で外れる分岐の見分け方をご教示ください（関数名だけで外しません）。
3. **intel_gt_resume の RPS/LLC/RC6**（P7.8）は enable_guc=0 でも全て taken と理解していますが、compute dispatch に
   必須か否かに関わらず、参照が実行する以上そのまま移植する方針で合っていますか（forcewake-ALL 常時保持の是正含む）。
4. 台帳の粒度: 現状は関数レベル + 一部子関数です。この深さで進め、実装しながら子関数を展開して良いでしょうか。

（成果物: `plan/ws031/linux-parity/`{ledger.md, linux-reference/{manifest.txt, i915-src, uapi-drm, fixtures}}。
 git 操作はしていません（コミットはお任せ）。build 影響なし。GPU vfio-pci 維持。次報で P0..P2 骨格 + clock_gating_init/
 renderstate_emit 統合の実装結果を提出予定。）
