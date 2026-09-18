
## p011 増分E-22 (2026-09-16): Linux 外部対照 — IGD パススルーで i915 が bind せず（準備段階の壁）

専門家提供の eu_positive_control.py で Linux 外部対照を試行。テストホスト(Debian 13)上に Ubuntu 24.04
ゲストを構築し、同一 8086:46a8 を VFIO パススルー。

### 成功した部分
- Ubuntu 24.04 cloud image + cloud-init で自動化: ゲスト起動、intel-opencl-icd/clinfo/python3-pyopencl/
  numpy を apt 導入、eu_positive_control.py を自動実行。インフラは動作。
- ゲストは GPU を検出: `pci 0000:00:02.0: [8086:46a8] type 00 class 0x030000 PCIe Root Complex Integrated Endpoint`。

### ブロッカー: i915 が passthrough IGD に bind しない
- テスト結果: `FAIL: FileNotFoundError: '/sys/devices/pci0000:00/0000:00:02.0/driver'`
  → デバイスに driver シンボリックリンクなし = **i915 が bind していない**。
- ゲスト dmesg に **i915 メッセージ皆無**（drm_connector と modprobe@drm のみ）。手動 modprobe i915 でも bind せず。
- QEMU 起動時: `vfio_container_dma_map(..., 0x380000000000, 0x108000, ...) = -22(EINVAL)` +
  `PCI peer-to-peer transactions on BARs are not supported`。
- `x-igd-opregion=on` を付けても変化なし。

### 解釈（専門家判定表に沿う）
- 専門家「GPU 未列挙 / i915 未バインド / build 失敗 → 外部対照の準備段階。物理故障や共通 send 障害の
  証拠にしない」に該当。→ **EU の陰性結果ではない**。まだ Linux で EU が動くか未確定。
- 原因: **Intel IGD パススルーは Linux i915 に OpRegion/VBT/stolen memory(DSM)/GTT の適切な公開が必要**で、
  標準の `vfio-pci` + `x-igd-opregion=on` だけでは不足。IGD パススルーは legacy mode / x-igd-gms /
  host 側 IOMMU・BIOS 設定など finicky な条件を要する既知の難所。zedBSD ドライバはこれら BIOS/OpRegion
  依存を回避して直接 HW を叩くため動く（＝両者の初期化前提が根本的に異なる）。

### 次の選択肢
1. IGD パススルー設定を詰める（x-igd-gms でメモリ量、x-igd-legacy-mode、host kernel params、
   IGD を primary display にする legacy assignment 等）— 深掘りが必要な領域。
2. 専門家に「Linux i915 を ADL-P IGD passthrough で bind させる最小構成」を照会。
3. E-21 の成果（indirect-context 経路の実装・動作）を土台に、他の restore-time 差分を Linux ソース
   照合で探す方向（Linux 実機を経由せずコード比較）。

### 環境
テストホストの GPU は vfio-pci に維持（zedBSD テスト継続可）。Ubuntu ゲスト一式は /home/awe/linuxvm に保存。
default ビルド warning 0。この回の主成果は E-21（indirect-context 経路実装+動作、Wa_18022495364 単独では
EU ハング未解消）。
