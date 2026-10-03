# Gen12 PS ハング 第15報 — Linux 外部対照は IGD パススルーで i915 未 bind（準備段階の壁）

いただいた eu_positive_control.py で Linux 外部対照を試みましたが、**Linux i915 が passthrough された
IGD に bind しない**段階で止まっています。ご判定表の「i915 未バインド = 準備段階、EU 陰性ではない」に
該当します。ここを越えるためのご助言を伺います。

## 実施できたこと
- テストホスト(Debian 13)上に Ubuntu 24.04 ゲストを cloud-init で自動構築。intel-opencl-icd /
  clinfo / python3-pyopencl / numpy を導入し、eu_positive_control.py を自動実行するところまで動作。
- ゲストは GPU を検出: `pci 0000:00:02.0: [8086:46a8] ... PCIe Root Complex Integrated Endpoint`。

## ブロッカー
```
FAIL: FileNotFoundError: '/sys/devices/pci0000:00/0000:00:02.0/driver'   ← driver 未 bind
ゲスト dmesg: i915 メッセージ皆無（drm_connector と modprobe@drm のみ）。手動 modprobe i915 でも bind せず。
QEMU 起動時: vfio_container_dma_map(..., 0x380000000000, 0x108000, ...) = -22(EINVAL)
             + "PCI peer-to-peer transactions on BARs are not supported"
x-igd-opregion=on を付けても変化なし。
```

QEMU 起動コマンド（要点）:
`-machine q35,accel=kvm -cpu host -m 4096 -object memory-backend-memfd,share=on
 -device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0`

## 原因の理解
Intel IGD（統合 GPU）パススルーで Linux i915 を bind させるには、OpRegion/VBT に加え stolen memory(DSM)・
GTT 等の適切な公開と、IGD 特有の QEMU 設定（legacy assignment mode、x-igd-gms、host 側 IOMMU/BIOS 条件）が
必要で、標準の vfio-pci + x-igd-opregion=on だけでは不足のようです。zedBSD ドライバはこれら BIOS/OpRegion
依存を回避して直接 HW を叩くため動作します（＝両者の初期化前提が根本的に異なる）。DMA map の -22 は、
IGD の高位アドレス領域（GTT/DSM 相当）を guest IOMMU にマップできていないことを示唆します。

## 伺いたいこと
1. **ADL-P IGD(8086:46a8) を Linux ゲストで i915 に bind させる最小の QEMU/ホスト構成**をご教示ください。
   - `x-igd-legacy-mode=on` は IGD を primary display 化する等の条件があると理解していますが、ヘッドレスの
     UPT(universal passthrough) で bind させる要件（x-igd-gms の値、必要な ROM/VBT、host kernel params）は
     何でしょうか。
   - `vfio_container_dma_map=-22`（0x380000000000 の領域マップ失敗）を解消する方向（QEMU の
     igd 関連オプション、あるいは machine の aperture 設定）はありますか。
2. もし Linux IGD passthrough の bind が高コストなら、**外部対照の代替**として、テストホストで
   一時的に i915 を IGD に直接バインド（vfio を外す）して host 上で eu_positive_control.py を走らせる、
   という方法は「同一物理 GPU で Linux i915+EU が動くか」の対照として妥当でしょうか（VFIO 層は挟まない
   差はありますが、EU 実行そのものの物理陽性は取れます）。ホストは boot 時 vfio-pci バインド・i915 blacklist
   なので、一時 unbind→i915 bind→テスト→戻す、が必要です。

## 今回の主成果（再掲）
- 欠落していた **indirect-context restore 経路を実装し、実機で動作確認**（enter/leave marker 着地）。
- ただし **Wa_18022495364 単独では EU スレッドハングは解消しない**（C3-low は WA ありでも同一署名）。
- 収束点は不変: PS/compute 共通で「dispatch された EU スレッドが命令を実行/完了しない」。VA・kernel 内容・
  windower・store・電源/fuse/MCR はすべて除外/健全。

（テストホストの GPU は vfio-pci に維持、zedBSD テスト継続可。Ubuntu ゲスト一式は保存済み。）
