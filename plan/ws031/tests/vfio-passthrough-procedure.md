# WS031 実機 VFIO パススルー テスト手順（確立、2026-09-15）

Latitude 5330（awe@10.0.10.25、Alder Lake-P IGD 8086:46a8）を、**起動時点で** ホスト GPU ドライバから解放し vfio-pci にバインドする。実行時 unbind（host-igd.sh）の競合が原因の間欠ハングを排除する。

## 確立した状態（適用済み）
- `/etc/modprobe.d/vfio-igd.conf`: `blacklist i915` / `blacklist xe` / `options vfio-pci ids=8086:46a8`
- `/etc/modules-load.d/vfio-igd.conf`: `vfio-pci`
- `sudo update-initramfs -u` 実行、再起動済み。
- drm は blacklist しない（i2c_hid 等が使う共有フレームワーク。i915/xe を外せば IGD は解放される）。

## 再起動後の検証（確認済み）
- `lsmod` に i915/xe なし（vfio_pci/vfio_iommu_type1/vfio のみ）。
- `lspci -nnk -s 00:02.0` → `Kernel driver in use: vfio-pci`。
- `/dev/vfio/0` あり。IGD は単独 IOMMU グループ。GDM inactive（ホストはヘッドレス）。

## QEMU パススルー（成功確認済み）
```
sudo -n qemu-system-x86_64 -machine q35,accel=kvm -cpu host -m 1024 \
  -display none ... -device vfio-pci,host=0000:00:02.0,rombar=0
```
`rombar=0` で IGD option ROM 警告を抑制。VFIO 初期化成功（10 秒稼働、busy/mmap エラーなし、EXIT=0）。root（sudo）で走るため memlock 制限を回避。

## ビッグバンテストでの使い方
デバイスは常時 vfio-pci にバインド済みなので、WS029 の `host-igd.sh attach/restore`（実行時 unbind）は**使わない**。zedBSD の i915+vk 入り disk-image を QEMU で直接パススルー起動する（`-vga std` で boot console、IGD は vfio-pci で guest へ）。run-i915-remote.py 系を使う場合は attach/restore を skip する分岐が要る。

## 復旧（ホスト表示を戻す場合）
```
sudo rm /etc/modprobe.d/vfio-igd.conf /etc/modules-load.d/vfio-igd.conf
sudo update-initramfs -u
sudo systemctl reboot
```
再起動後 i915 が IGD を掴み GDM が復帰する。
