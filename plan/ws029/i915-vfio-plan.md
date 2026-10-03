# WS029 i915: VFIO passthrough 試験の前提（机上確認、p001）

対象 host は `awe@10.0.10.25`（Dell Latitude 5330、Debian kernel `6.19.13+deb13-amd64`）。事実は [phase001/host-facts.json](phase001/host-facts.json)（`plan/ws029/tests/host-i915-facts.sh` の読取専用出力）に基づく。本書は p006 の `host-igd.sh` / `i915-qemu.py` / `run-i915-remote.py` が従う前提と境界を固定する。host の状態は p001 では変更していない。

## 1. 成立条件（事実で確認済み）

| 項目 | 事実 | 帰結 |
| --- | --- | --- |
| GPU | `0000:00:02.0` Intel `8086:46a8` rev 0c（ADL-P Iris Xe）、現在 `i915` bound、`xe` は load 済みだが未 bind | attach では i915 から unbind する。`xe` が再 probe しないよう `driver_override` を `vfio-pci` にしてから `drivers_probe` を叩く |
| IOMMU | DMAR あり（`intel_iommu` は kernel 既定で有効、cmdline に `intel_iommu=` 指定なし）、iommu group 数 17、group 0 は `00:02.0` 単独 | group 内の他 device を巻き込まない。cmdline 変更は不要かつ許可外 |
| RMRR | IGD の RMRR は Linux で relaxable 扱い（`i915_capabilities`/`dmesg` に RMRR による拒否なし） | `vfio-pci` bind 時の `-EPERM (RMRR)` は想定しない。発生したら attempt を fail にし報告する（対処は許可外の cmdline 変更を伴うため） |
| VFIO | `CONFIG_VFIO=m`, `CONFIG_VFIO_IOMMU_TYPE1=m`, `CONFIG_VFIO_PCI=m`, `CONFIG_VFIO_PCI_IGD=y`。`modprobe -n -v vfio-pci` は `vfio` → `vfio_iommu_type1` → `vfio-pci-core` → `vfio-pci` を解決。現在 `/dev/vfio/` には `vfio` だけ | `sudo -n modprobe vfio-pci` で module 導入（package 導入ではない）。bind 後に `/dev/vfio/0` が現れる |
| QEMU | `/usr/bin/qemu-system-x86_64` 10.0.11（Debian）、`vfio-pci`、`vfio-pci-nohotplug`、`vfio-pci-igd-lpc-bridge` device あり、KVM 利用可 | `-device vfio-pci,host=0000:00:02.0` を使う。IGD の legacy 化（`x-igd-opregion`、LPC bridge、VGA）は使わない |
| 表示 | GDM active、awe の graphical session（seat0/tty2）と user manager が存在 | attach 前に `systemctl stop gdm`。awe の graphical session は終了する（SSH session は残る）。`fuser /dev/dri/*` が空になるまで待ち、空でなければ attach しない |
| sudo | `sudo -n` で `dmidecode`/`lspci`/debugfs 読出しが通っている | attach/restore/QEMU 起動はすべて `sudo -n`。password prompt が出る構成なら attempt を fail にする |
| 資源 | RAM 7.4 GiB、12 CPU、BAR0 16 MiB @0x6054000000、BAR2 256 MiB @0x4000000000 | guest は `-m 1024 -smp 2`。BAR は QEMU が guest へ再配置する |
| firmware | `/lib/firmware/i915/adlp_dmc*.bin`, `adlp_guc_*.bin`, `adlp_huc` なし | 本計画は GuC/HuC/DMC を使わない。必要になれば `userland/firmware/latitude-5330/` に機種別で置く（ユーザー指示） |

## 2. IGD UPT（Universal Passthrough）の前提

- guest には **画面出力がない**。`-vga none`、`-display none`。scanout/DMC/OpRegion は対象外。
- guest 側 zedBSD driver は BAR0（MMIO 8 MiB + GGTT PTE 8 MiB）と MSI 1 vector だけを使う。BAR2 aperture は map しない。
- host 側で観測できるのは (a) `-debugcon file:` に出る guest kernel log、(b) QMP `pmemsave` で取る guest RAM（GPU が DMA で書いた結果）、(c) QEMU の exit code / `-no-reboot`。**VRAM を直接読む手段はなく、GEM backing は guest RAM（stolen 不使用）なので pmemsave で足りる**。
- `vfio-pci-igd-lpc-bridge` / `x-igd-gms` / `x-igd-opregion` は legacy モード専用で、UPT では設定しない。

## 3. `sudo -n` で QEMU を起動する理由

VFIO は guest RAM 全体を pin する（`VFIO_IOMMU_MAP_DMA`）。非 root の `RLIMIT_MEMLOCK` 既定（8 MiB 級）では `-m 1024` の DMA map が `ENOMEM` で失敗する。`/etc/security/limits.d` の変更は host 設定変更に当たり許可外なので、`sudo -n qemu-system-x86_64 ...` で起動し、終了後に生成物（log、pmemsave dump、QMP socket の残骸）の owner を `chown awe:awe` で戻す。`/dev/vfio/0` の権限変更（udev rule）も同じ理由で行わない。

## 4. host 手順（p006 の `host-igd.sh`、設計資料 §8 を正本とする）

`host-igd.sh attach|restore|status`、`set -eu`、各 step 120 秒 timeout。

attach:
1. `status` を JSON で保存（before）。
2. `systemctl stop gdm`（graphical session が落ちる）。`loginctl` で seat0 の graphical session が消えるのを待つ。
3. `fuser -v /dev/dri/*` が空であることを確認。空でなければ **中止**（プロセスは殺さない。ユーザーに報告）。
4. `modprobe vfio-pci`。
5. `echo 0000:00:02.0 > /sys/bus/pci/drivers/i915/unbind`。`/sys/class/drm/card0` が消えるのを待つ。
6. `echo vfio-pci > /sys/bus/pci/devices/0000:00:02.0/driver_override`、`echo 0000:00:02.0 > /sys/bus/pci/drivers_probe`。
7. `driver` symlink が `vfio-pci`、`/dev/vfio/0` が存在することを確認して `status` を保存。

restore（`try/finally` で必ず実行）:
1. QEMU が終了していること（pid 不在）を確認。残っていれば `SIGTERM` → 10 秒 → `SIGKILL`。
2. `unbind`（vfio-pci）→ `driver_override` を空に → `drivers_probe` → `driver` symlink が `i915`、`/sys/class/drm/card0` の再出現を待つ。
3. `systemctl start gdm`。
4. `status` を保存し before と比較（bound driver、gdm、`/dev/vfio` の一覧）。不一致は attempt FAIL とし、差分を結果に書く。**reboot はしない**（必要なら別途ユーザー承認）。

status: `{"driver": ..., "gdm": ..., "dev_vfio": [...], "drm_nodes": [...], "graphical_sessions": n}`。

## 5. QEMU 引数（p006）

```
sudo -n qemu-system-x86_64 -machine pc,accel=kvm,memory-backend=memory -cpu host -m 1024 -smp 2 \
  -object memory-backend-memfd,id=memory,size=1024M \
  -drive if=pflash,format=raw,readonly=on,file=<OVMF_CODE> -drive if=pflash,format=raw,file=<OVMF_VARS copy> \
  -drive file=<boot.img>,format=raw,if=virtio \
  -vga none -display none -device vfio-pci,host=0000:00:02.0 \
  -qmp unix:<sock>,server,nowait -monitor none -serial none -nic none -debugcon file:<guest.log> -no-reboot
```

virtio-vga / egl-headless / VNC / trace は付けない。guest 側の `-device vfio-pci` に `x-no-mmap` 等の debug option は付けない。

## 6. 許可の境界（ユーザー指示 2026-09-14 に基づく）

許可済み（WS029 の試験に限る）:
- GDM/Wayland の停止と再開（`systemctl stop/start gdm`）。
- i915 の使用終了と unbind、`vfio-pci` への bind（`driver_override` + `drivers_probe`）、`modprobe vfio-pci`。
- QEMU による `00:02.0` の VFIO passthrough（`sudo -n`）。
- 各 attempt 後の restore（i915 へ戻し GDM を再開）。

許可外（実施しない。必要になったら理由を添えて別途承認を求める）:
- host の reboot、電源操作。
- package の導入・更新（apt 等）。firmware の配置も host には行わない（zedBSD 側 `userland/firmware/<機種>/` のみ）。
- kernel cmdline の変更（`intel_iommu=`, `iommu=pt`, `vfio-pci.ids=`, `i915.modeset=0`, `initcall_blacklist` 等）、`/etc/modprobe.d`、`/etc/default/grub`、udev rule、`limits.conf`。
- BIOS 設定変更。
- attach 中に残った他ユーザープロセスの kill。

## 7. 既知の risk と扱い

| risk | 兆候 | 扱い |
| --- | --- | --- |
| i915 unbind が hang | `unbind` の write が戻らない、`card0` が残る | 120 秒で attempt FAIL。host 状態を報告し、reboot は求めない（ユーザー判断） |
| GDM 再開後に画面が戻らない | `systemctl status gdm` は active だが session なし | restore は成功扱いにしない。`status` の差分を報告 |
| VFIO bind が `EPERM`（RMRR） | `dmesg` に `Device is ineligible for IOMMU domain attach due to platform RMRR` | attempt FAIL。cmdline 変更が要るため許可外、報告のみ |
| guest で MSI が届かない | `i915: attach stopped at irq` | driver 側の問題として p006 で対処（`-device vfio-pci,...,x-msi` 等の QEMU option は使わない） |
| guest GPU hang で host GPU が汚れる | restore 後 host の `dmesg` に i915 reset/wedged | restore 時に `dmesg | tail` を保存し報告。FLR 相当は vfio-pci の bind/unbind に任せる |
| QEMU が root 所有の file を残す | `guest.log`/dump が root:root | restore の後に `chown -R awe:awe <attempt dir>` |

## 8. p006 の受入との対応

- 初回起動 `--boot-only`: `i915: attach stopped at` が無く `i915: selftest bcs0 store=ok` がある。
- `--test`: `gpu-i915-test` が `GPUI915 PASS`、`pmemsave` の照合 PASS。
- 各 attempt の前後 `status` が一致（driver=i915、gdm=active、`/dev/vfio` は `vfio` のみ）。
