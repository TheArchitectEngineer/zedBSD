# Gen12 PS ハング 第16報 — ★Linux 外部対照が陽性。問題は zedBSD の init/context に確定

ご指示のゲスト側 i915 登録確認 → IOMMU 幅に合わせた QEMU 設定 → EU 陽性対照、を実施し、
**決定的な陽性結果**が取れました。ご判定表の「Intel GPU で tag 一致・正常終了・reset なし →
自作側の init/context/memory 差分を優先」に到達しました。

## 到達までの3段（ご指摘がすべて的中）
1. **i915 未登録の切り分け**: ゲスト内で `resolve-alias FATAL: Module ...46A8... not found` を確認
   → 「stolen memory/VBT 不足」ではなく、**Ubuntu 最小 cloud image が i915 を含まない**（modules-extra に分離）
   ことが原因でした。`linux-modules-extra-$(uname -r)` を導入すると i915 が bind（/dev/dri/card0 作成）。
2. **DMA map -22 = IOMMU 幅**: ご指摘どおり host IOMMU MGAW を読むと **39bit**（CAP から算出、CPU phys=46bit）。
   `-cpu host,host-phys-bits-limit=39` で **vfio_container_dma_map=-22 が消滅**。QEMU IGD 文書の既知対策と一致。
3. **enable_guc=0**: modules-extra 導入後の初回 modprobe で、i915 が
   `GuC firmware i915/adlp_guc_70.bin: fetch failed -ENOENT → GPU wedged` になりました。
   ADL-P は既定で GuC 必須ですが firmware 欠如で wedge。**`enable_guc=0`（execlists）で GuC 不要となり
   init 成功**。これは zedBSD の execlists 提出モデルと同条件です。

## 陽性結果（同一 GPU・同一 VFIO・execlists）
```
DIAG param enable_guc=0
[drm] Initialized i915 1.6.0 for 0000:00:02.0     （wedge なし）
Device: Intel(R) Iris(R) Xe Graphics; OpenCL 3.0 NEO (intel-opencl-icd 23.43)
PASS run=1: marker=0xc0ffee02, canary intact
PASS run=2: marker=0xc0ffee03, canary intact
PASS run=3: marker=0xc0ffee04, canary intact
probe_exit=0
GPU reset/hang/wedged: なし
```
QEMU: `q35, -cpu host,host-phys-bits-limit=39, vfio-pci host=0000:00:02.0,x-igd-opregion=on,rombar=0`
（legacy mode 不使用、ご助言どおり）。kernel 6.8.0-139-generic。

## 結論（調査の確定的分岐）
**同一物理 GPU(8086:46a8)・同一 VFIO パススルー・enable_guc=0（execlists、zedBSD と同じ提出モデル）で、
Linux i915+OpenCL が EU カーネルを実行しメモリに書き込み正常終了・reset なし。**
→ **EU はこの GPU/VFIO で Linux(execlists) では動く。問題は zedBSD ドライバの init/context/memory 設定に確定**。
物理故障・VFIO パススルー・GPU 不能・GuC 依存はすべて否定されました。zedBSD も execlists（GuC 不使用）なので、
**Linux execlists の GT/context 初期化と zedBSD の差分**が原因です。

## 伺いたいこと — 差分特定の進め方
1. ご提示の「動く Linux 上で現在の C2 バイナリ・対応 state を実行して比較」を進める価値はありますか。
   それとも、まず **Linux execlists 経路（enable_guc=0）の GT/context/LRC 初期化コード**と zedBSD を
   ソース照合し、EU スレッド実行に必要な設定（例: GT init の特定シーケンス、forcewake、RC6/RPS、
   L3/URB、context の workaround batch 群、あるいは execlists 提出時の特定レジスタ）の欠落を探す方が
   効率的でしょうか。ご推奨の着眼点（Linux のどの初期化関数群を基準にすべきか）をご教示ください。
2. 今回 Linux は enable_guc=0 でも init に GuC firmware fetch を試みてから execlists に落ちています。
   zedBSD 側で EU 実行に効く GT 初期化として、Linux が execlists 経路で必ず通す処理
   （intel_gt_init / intel_engines_init / GT workaround / forcewake ドメイン等）のうち、
   「これが無いと dispatch した EU スレッドが実行/完了しない」典型はありますか。
3. C2 バイナリを Linux GEM/context で走らせる場合の最小ハーネス構成（Level Zero か、i915 の
   直接 ioctl か、既存ツールか）で、ご推奨があればお願いします。

（インフラ: Ubuntu ゲスト一式 /home/awe/linuxvm に保存・再現可能。zedBSD の C0/C2/C1/indirect-ctx 実装、
 refcs/refps/gentool も維持。GPU は vfio-pci 維持で zedBSD テスト継続可。default ビルド warning 0。）
