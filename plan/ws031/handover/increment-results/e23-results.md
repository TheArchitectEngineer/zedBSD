
## p011 増分E-23 (2026-09-16): ★Linux 外部対照が陽性 — 問題は zedBSD の init/context に確定

専門家提供の eu_positive_control.py を Linux ゲストで実行し、**決定的な陽性対照が取れた**。

### 結果（同一 GPU・同一 VFIO・enable_guc=0=execlists）
```
DIAG param enable_guc=0
[drm] Initialized i915 1.6.0 for 0000:00:02.0   （wedge なし）
GPU candidate: Intel(R) Iris(R) Xe Graphics, vendor_id=0x8086
Device: Intel(R) Iris(R) Xe Graphics; OpenCL 3.0 NEO (intel-opencl-icd 23.43)
PASS run=1: marker=0xc0ffee02, canary intact
PASS run=2: marker=0xc0ffee03, canary intact
PASS run=3: marker=0xc0ffee04, canary intact
probe_exit=0
GPU reset/hang/wedged: なし（クリーン）
```
kernel 6.8.0-139-generic。**同一物理 GPU(8086:46a8)・同一 VFIO パススルー・enable_guc=0（execlists、
zedBSD と同じ提出モデル）で、Linux i915+OpenCL が EU カーネルを実行しメモリに書き込み正常終了・reset なし。**

### 到達に必要だった設定（IGD passthrough + Linux 固有）
1. **`-cpu host,host-phys-bits-limit=39`**: host IOMMU MGAW=39bit（CPU phys=46bit）。ゲスト物理幅を 39 に
   制限して IGD の高位 IOVA(0x380000000000)マップ失敗(vfio_container_dma_map=-22)を解消。QEMU IGD 文書の既知対策。
2. **`linux-modules-extra-$(uname -r)`**: Ubuntu 最小 cloud image は i915 を含まない（modules-extra に分離）。
   導入後 i915 が bind（/dev/dri/card0, renderD128 作成）。
3. **`enable_guc=0`（modprobe.d）**: ADL-P i915 は既定で GuC 必須だが firmware(adlp_guc_70.bin)欠如で
   **GPU wedged**。enable_guc=0 で execlists にすると GuC 不要で init 成功。zedBSD の execlists と同条件。
4. QEMU: q35, x-igd-opregion=on, rombar=0（legacy mode は不使用）。

### 結論（調査の確定的分岐）
専門家判定表「Intel GPU で tag 一致・正常終了・reset なし → **自作側の init/context/memory・命令公開経路の
差分を優先**」に該当。→ **EU はこの GPU/VFIO で Linux(execlists) では動く。問題は zedBSD ドライバの
init/context/memory 設定に確定**。物理故障・VFIO パススルー・GPU 不能・GuC はすべて否定。
zedBSD も execlists（GuC 不使用）なので、**Linux execlists の init/context と zedBSD の差分**が原因。

### 次段
- Linux execlists 経路（enable_guc=0）の GT/context/LRC 初期化 vs zedBSD の初期化を照合し、
  EU スレッド実行に必要な設定の欠落を特定。
- 専門家の deferred 案「現在の C2 バイナリを Linux 側の GEM/context で実行」も有力（EU 実行環境を
  Linux で再現して zedBSD state と比較）。
- indirect-context 経路(E-21)は実装済みだが Wa 単独では不足 → 他の GT/context init 差分へ。

環境: Ubuntu ゲスト一式 /home/awe/linuxvm 保存（再現可能）。GPU は vfio-pci 維持。zedBSD テスト継続可。
