# IGD passthrough による i915 リモート検証（WS029 p006）

`run-i915-remote.py` は、i915 selftest 付き image のビルド、一時 image の準備、`awe@10.0.10.25` への転送、host の IGD を vfio-pci へ切替、QEMU での起動と証拠回収、host の復旧を 1 回のコマンドで行う。ビルド・転送・証拠回収は `plan/ws014/tests/run-venus-remote.py` を import して再利用する（`PROFILES['i915']` を実行時に追加）。

```sh
python3 plan/ws029/tests/run-i915-remote.py --attempt q314-i915-boot-001 --boot-only
python3 plan/ws029/tests/run-i915-remote.py --attempt q314-i915-test-001 --test --skip-build
```

## 承認境界（ユーザー指示 2026-09-14）

許可: GDM/Wayland の停止と再開、i915 の使用終了と unbind、`vfio-pci` への bind、QEMU（`sudo -n`）での passthrough、各 attempt 後の復旧。
許可外（行わない）: host の reboot、package 導入、kernel cmdline/modprobe.d/udev/limits の変更、BIOS 変更、他プロセスの kill。復旧に失敗した場合は host の状態を `result.json` と `host.log` に残して attempt を FAIL にし、reboot は別途承認を求める。

## 手順

1. `host-igd.sh status`（読取専用）を保存し、既に QEMU が動いていれば中止。
2. `host-igd.sh attach`: `systemctl stop gdm` → `/dev/dri/*` の利用者が 10 秒以内に消えること → `modprobe vfio-pci` → i915 から unbind（120 秒上限）→ `driver_override=vfio-pci` → `drivers_probe` → `driver`=vfio-pci と `/dev/vfio/0` を確認。
3. `sudo -n python3 i915-qemu.py --image boot.img --output capture ... --boot-only|--test`（root で実行: VFIO は guest RAM を pin するため memlock 制限を避け、QMP socket と `pmemsave` の出力は root が作る。終了時に `--owner` の uid:gid へ chown する）。QEMU 引数: `-machine pc,accel=kvm,memory-backend=memory -cpu host -m 1024 -smp 2 -object memory-backend-memfd,... -drive pflash×2 -drive boot.img -vga std -display none -device vfio-pci,host=0000:00:02.0 -qmp unix:... -monitor none -serial none -nic none -debugcon file:guest.log -no-reboot`。`-vga std` は計画の `-vga none` からの変更で、zedBSD の UEFI loader（`bootloader/uefi/bootx64.c`）が GOP framebuffer 無しでは `Locate GOP` で停止するため（boot-002 で確認）。標準 VGA は class match の PC/AT graphics driver が取り、IGD は exact ID match の i915 が取る。表示 backend は付けない。
4. `host-igd.sh restore`（`try/finally` で必ず実行）: QEMU 不在を確認 → vfio-pci から unbind → override 解除 → `drivers_probe` → `driver`=i915 と `card0` の再出現（30 秒）→ `systemctl start gdm` → 15 秒後 active を確認。
5. `host-igd.sh status` を保存し、開始前と `driver`/`gdm`/`dev_vfio`/`drm_nodes` が一致すること（`host_restored`）。不一致は FAIL。

## 判定

- `--boot-only`: shell prompt 到達、guest.log に `i915: attach stopped at` が無く、`i915: selftest bcs0 store=ok`、`i915: registered native GPU node` がある。
- `--test`: 上に加え `/bin/gpu-i915-test` を送って `GPUI915 PASS copy=1 fill=1 store=1 job=1 src_handle=<h> dst_handle=<h>` を待ち、guest.log の `i915: resource ... phys=0x... handle=<h>` から src/dst の物理範囲を取り、QMP `pmemsave` で dump して独立に照合する（src: `0x5a000000+i`、dst: 先頭 `0xdeadbeef`、残り `0x3197a5e2`）。結果は `memory_check`。
- QEMU の正常終了、image/harness/console symbol の hash 一致、証拠ファイル（`result.json guest.log qemu.log qmp.jsonl console.log console.bin`、test では `src.bin dst.bin`）の回収。

保存先: local `plan/ws029/temp/remote/<attempt>/`（`result.json`、`build.log`、`transfer.log`、`host.log`、`remote.log`、`evidence/`）、remote `/home/awe/zedbsd-q314-i915/<attempt>/`。disk image、OVMF 変数、socket は回収しない。

## 復旧 rehearsal

QEMU を使わずに `attach → status → restore → status` を 1 回行い、結果を `plan/ws029/phase006/host-rehearsal.json` に保存する。復帰しない場合はそこで停止する。
