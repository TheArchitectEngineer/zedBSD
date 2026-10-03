<!-- awesome-plan project=zedbsd record=ws029-p006 -->

# WS029 p006: VFIO passthroughテストループ（host手順・harness・初回起動）

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i06 cleared (whole Phase)
Dependencies: ws029-p005
Next: ws029-p007
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p006`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 180 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

private host の IGD を VFIO で QEMU に passthrough し、zedBSD の i915 が attach → selftest → `/dev/gpu0` 公開まで到達する再現可能な loop を作る。各 attempt の後に host の i915 と GDM を復旧する。

## 前提と承認

ユーザーは本指示で「GDM や Wayland を止め、i915 の使用を終了し、QEMU で PCI passthrough して試験する」ことを許可した。Queue 開始時にこの許可を journal に記録する。**host の reboot、package 導入、kernel cmdline 変更は許可に含まれない**。復旧に失敗した場合は host の状態を報告して停止し、reboot は別途承認を求める。

## 手順

1. **host script** `plan/ws029/tests/host-igd.sh`（host へ転送して `sudo -n` で実行、`set -eu`）:
   - `status`: `readlink /sys/bus/pci/devices/0000:00:02.0/driver`、`systemctl is-active gdm`、`ls /dev/vfio`、`ls /sys/class/drm`、`fuser -v /dev/dri/* 2>&1 | head` を JSON で出力。
   - `attach`: `systemctl stop gdm` → 10 秒以内に `/dev/dri/*` の利用者が無くなるまで待つ（残れば失敗）→ `modprobe vfio-pci` → `echo 0000:00:02.0 > /sys/bus/pci/drivers/i915/unbind` → `echo vfio-pci > /sys/bus/pci/devices/0000:00:02.0/driver_override` → `echo 0000:00:02.0 > /sys/bus/pci/drivers_probe` → `driver` symlink が `vfio-pci` と `/dev/vfio/0` を確認 → status。
   - `restore`: QEMU process 無しを確認 → `echo 0000:00:02.0 > /sys/bus/pci/drivers/vfio-pci/unbind` → `echo > .../driver_override` → `echo 0000:00:02.0 > /sys/bus/pci/drivers_probe` → `driver` が `i915`、`/sys/class/drm/card0` の再出現を 30 秒待つ → `systemctl start gdm` → 15 秒後 `is-active` → status。
2. **復旧 rehearsal（QEMU なし）**: `attach` → `status` → `restore` → `status` を 1 回行い、i915 再 bind と GDM 復帰を確認して `plan/ws029/phase006/host-rehearsal.json` に保存する。**復帰しない場合はここで停止して報告**（p006 の受入は「復帰手順が成立すること」を含む）。
3. **guest harness** `plan/ws029/tests/i915-qemu.py`: `plan/ws014/tests/venus-qemu.py` の `QMP`、`guest_text`、`console_text`（`pmemsave`）、`run()` の骨格を import して使い、QEMU 引数を `-machine pc,accel=kvm,memory-backend=memory -cpu host -m 1024 -smp 2 -object memory-backend-memfd,... -drive pflash×2 -drive boot.img -vga none -device vfio-pci,host=0000:00:02.0 -qmp unix:... -monitor none -serial none -nic none -debugcon file:... -no-reboot` にする（egl-headless/VNC/virtio-vga/trace は付けない）。`sudo -n` で起動し終了後に output の owner を `awe` に戻す。`--boot-only` は shell 到達 + guest.log に `i915: attach stopped at` が無く `i915: selftest bcs0 store=ok` があること。`--test` は `/bin/gpu-i915-test` を送り `GPUI915 PASS` を待ち、`i915: resource ... phys=` 行から dst の物理範囲を取り `pmemsave` で dump し、期待 pattern（copy: `0x5a000000+i`、fill: `0x3197a5e2`、store: 先頭 `0xdeadbeef`）と照合して `memory_check` を report に入れる。
4. **remote runner** `plan/ws029/tests/run-i915-remote.py`: `run-venus-remote.py` の build/prepare/transfer/evidence を import し、profile `i915`（config `config-i915-selftest-amd64.mk`、harness `i915-qemu.py`、modules なし）。実行順は `host-igd.sh status` → `attach` → QEMU → `restore` → `status`（`try/finally` で restore を必ず実行）。前後の status と restore 結果を result.json に保存し、restore 失敗は attempt を FAIL にする。`README-i915-remote.md` に手順・承認境界・復旧を書く。
5. **初回起動**: `run-i915-remote.py --attempt q314-i915-boot-001 --boot-only`。失敗した段階（`stage`）を guest.log から特定し、修正して再実行（同条件 3 回まで）。selftest 成功で p006 完了。

## 完了条件

rehearsal の復旧成功、`boot-001`（または再試行）の `status=pass`（attach 全段階、selftest、`/dev/gpu0`）、attempt 後の host status が開始前と同じ（i915 bound、GDM active）。証拠は `plan/ws029/temp/remote/<attempt>/` と `plan/ws029/phase006/`。記録同期。

## 実行境界

host 操作は `host-igd.sh` の 3 動作だけ。attach 中は他の attempt を並行しない。QEMU/host script の timeout: attach/restore 各 120 秒、VM 300 秒、build/転送 1200 秒。

## p006 実機結果（VFIO passthrough テストループ、2026-09-14）

Latitude 5330（`awe@10.0.10.25`）の IGD（`0000:00:02.0`、`8086:46a8` rev 0c）を `host-igd.sh` で vfio-pci に切替え、i915 selftest 付き image を QEMU で起動して attach → selftest → `/dev/gpu0` 公開まで到達した。各 attempt の後に i915 と GDM を復旧した。

### 復旧 rehearsal（QEMU なし）

`plan/ws029/phase006/host-rehearsal.json`。`attach` で driver=vfio-pci、`/dev/vfio/0` 出現、GDM inactive。`restore` 後 driver=i915、GDM active、`driver`/`gdm`/`dev_vfio`/`drm_nodes` が開始前と一致（`restored=true`）。復旧手順が成立することを確認。

### attempt 履歴（同条件の修正と再実行）

| attempt | mode | 結果 | 原因・修正 |
| --- | --- | --- | --- |
| boot-001 | boot-only | fail | harness の import 依存 `venus_rfb.py` を転送していなかった → 転送一覧に追加 |
| boot-002 | boot-only | fail | UEFI loader が GOP framebuffer 無しで `Locate GOP` 停止 → QEMU 引数を `-vga none` から `-vga std`（表示 backend なし）に変更 |
| boot-003 | boot-only | fail(attach) | i915 が `map-registers` で EINVAL → BAR0 を `drv_pci_device_claim_bar` してから map するよう修正（PCI は claim した BAR しか map させない） |
| boot-005 | boot-only | 進捗 | BAR0 は正しく map（regs va=0xffffffffe0000000, bus=0x380010000000）、GGTT/MSI まで到達、engine bring-up で停止 → 段階 log 追加 |
| boot-006 | boot-only | 進捗 | 両 engine init と `engines started` まで到達、selftest で停止 → selftest に log 追加 |
| **boot-007** | boot-only | **pass** | `i915: selftest bcs0 store=ok irq=1 seqno=1/1`、`registered native GPU node`。実機の BCS0 が store を実行し user interrupt を上げた |

### 受入

- attach 全段階（PCI enable、BAR0 claim/map、forcewake、GT reset、GGTT、MSI、engine×2、selftest、publish）を通過。
- `boot-007` boot-pass: guest.log に `attach stopped` なし、`selftest bcs0 store=ok`、`registered`。
- host 復旧: attempt 後 driver=i915、GDM active（`host_restored=true`）。attach 中は driver=vfio-pci、`/dev/vfio/0`、GDM inactive。
- 証拠: `plan/ws029/temp/remote/q314-i915-boot-007/`（result.json、guest.log、qemu.log、qmp.jsonl、console）、`plan/ws029/phase006/host-rehearsal.json`。

実機で GPU が実際に命令を実行した最初の到達点。copy/fill/store/job と host 側 RAM 照合は p007。
