# Let's Note SV7 の実機の試験の手引き（Gen9.5、i915 の passthrough）

2026-10-14 に P1（ws229-p005）が書いた。5330 の手引き [plan/tools/hw5330/README.md](../hw5330/README.md) と同じ道具を
`I915_MACHINE=sv7` で SV7 に向ける。規則（lock、timeout で括らない、serial の log の扱い、QEMU と実機の証拠を分ける）は
5330 の手引きの 0 節と同じで、[AGENTS.md](../../../AGENTS.md) が優先する。ここには SV7 で違う所だけを書く。

ユーザー（2026-10-14）:「ssh awe@10.0.10.27 で第8世代Core i5のLinuxに入れます。sudoはパスワードレスで使えます。
i915その他をblacklistして再起動すれば、5330同様に自動でテストできます。」「Core i5 8thのLet's Note SV7では、自動のテストなので、
細かく分けたテストでOKです」

## 1. 機械

| 項目 | 値 | 根拠 |
| --- | --- | --- |
| ssh | `awe@10.0.10.27`（centris から鍵で入れる。`~/.ssh/config` の alias は無い） | `plan/tools/hwsv7/setup.sh` の実行（2026-10-14） |
| OS | Debian 13（hostname `cfsv7`）、kernel 6.12.107+deb13-amd64。sudo はパスワード無し | 同上 |
| CPU・memory | Core i5 の第 8 世代、8 thread、物理 address 39 bit（launcher の `host-phys-bits-limit=39` と合う）、memory 15 GiB | 同上 |
| iGPU | `00:02.0` UHD Graphics 620 `8086:5917`（Kaby Lake R、Gen9.5）、subsystem `10f7:8338`。IOMMU の group に単独 | `lspci -nnk -s 00:02.0`、`/sys/bus/pci/devices/0000:00:02.0/iommu_group/devices` |
| 渡さない物 | SATA（`00:17.0`、AHCI `8086:9d03`）は SV7 自身の boot の disk | Q1（2026-10-14） |
| QEMU・firmware | QEMU 10.0.13（Debian）、`/usr/share/OVMF/OVMF_CODE_4M.fd`・`OVMF_VARS_4M.fd` | `setup.sh` |
| 作業の directory | `~/bigbang/`（`run-parity-vk.sh`・`igpu-mode.sh`、5330 と同じ物。`plan/ws031/tests/host/`） | `setup.sh` が置く |

### 1.1 host の設定（Q1 が 2026-10-14 に済ませた）

- `/etc/modprobe.d/vfio-igd.conf`: `blacklist i915`・`blacklist xe`・`options vfio-pci ids=8086:5917`。`/etc/modules` に `vfio-pci`。
  再起動の後、`00:02.0` は boot の時から vfio-pci に付いている（host の画面は firmware の framebuffer のまま）。
- 戻し方（file の注記）: その file を消し、`sudo update-initramfs -u`、再起動。
- `bigbang/igpu-mode.sh host`（host の i915 へ切り替え）は SV7 では使わない。blacklist の下で明示の `modprobe i915` が通るかは **未確認**。

## 2. 準備（1 回、または `plan/ws031/tests/host/` が変わった時）

```sh
plan/tools/hwsv7/setup.sh            # HOST の既定は awe@10.0.10.27
```

`~/bigbang/` を作り、`run-parity-vk.sh`・`igpu-mode.sh` を写して中身を比べ、QEMU を起動せずに次を確かめる:
iGPU が `8086:5917` で vfio-pci に付き group に単独、`/dev/kvm`、OVMF、QEMU、`sudo -n`、物理 address 39 bit 以上、空き memory 6 GiB 以上、
`~` の空き 8 GiB 以上、QEMU が走っていない。全部 `ok` なら最後に `setup: awe@10.0.10.27 ready`、終了の値 0。

## 3. 5330 との違い（`plan/ws031/tests/hw-machine.sh`）

`vkloop-hw.sh`・`plan/ws075/tests/test-hw.sh`・`plan/ws101/tests/hw/run-hw.sh` は `hw-machine.sh` を読み、`I915_MACHINE`（既定 `5330`）で
次を決める。環境に既に値がある変数はそれが勝つ。

| 変数 | 5330 | sv7 | 理由 |
| --- | --- | --- | --- |
| `I915_HOST` の既定 | `solaris10-man`（`test-hw.sh` だけは前のまま `awe@10.0.30.3`） | `awe@10.0.10.27` | |
| `I915_TEST_VBT` | `y`（5330 の取り込んだ VBT） | `n` | driver は Gen9 の display を持たない（絵は firmware の framebuffer のまま、ws229 の段階 1）。5330 の VBT は別の機械の物 |
| `I915_BOOT_EXTRA`（→ `ZEDBSD_BOOT_EXTRA_LINES`） | 無し（make の行は前と同じ） | `i915.gen9=on` | 2026-10-14 から Gen9 の device は既定で取る（ws229-p005 の実機 PASS の後、`I915_GEN9_DEFAULT_ON` が 1）。この語は不要だが害は無く、試した image と同じ flag を保つために残す。無効にするのは `i915.gen9=off` |
| `I915_IGPU` | 無し（確かめない） | `8086:5917` | 走らせる前に host の `00:02.0` の vendor:device を ssh で読み、違えば `the iGPU of HOST is ...` で止まる |
| `run-hw.sh` の `OUT` の既定 | `build/ws101-hw` | `build/ws101-hw-sv7` | 5330 の image と混ぜない |

- boot の語と VBT は `vkloop-hw.sh` の flag の記録（`$BUILD/.vkloop-flags`）に入るので、同じ `BUILD` を 5330 と SV7 で使い回しても作り直される。
  それでも `BUILD`・`OUT` は機械ごとに分ける。
- **lock は 5330 と共有**（centris の `/tmp/i915-hw.lock`）。結果の写しが centris の `/tmp/vkloop-last.log` を通るので、5330 と SV7 を同時に流さない。
- launcher は 5330 と同じ（`-device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0`、q35、4 GiB、4 vCPU、`-vga std`）。
  guest の画面は std VGA の GOP（`rombar=0` で iGPU の GOP は無い）。QEMU 10 の IGD の quirk（OpRegion・BDSM の stolen memory）が Gen9.5 で
  5330 と同じに働くかは **未確認**（最初の T1 の run で分かる）。
- 写真の道具（`capture_lcd.ps1`）・H4 の harness・HDMI の試験は SV7 に無い。capture（`CAPTURE=...`）の run は試していない。

## 4. 流し方（試験の場面を 1 つずつ）

```sh
# 全部（image は lock の外で先に作り、次に lock の下で 1 つずつ。1 場面 5〜10 分）
I915_MACHINE=sv7 OUT=build/<WS>-sv7 plan/ws101/tests/hw/run-hw.sh <TAG> ktest eu vkcs vkx vke1 vke2 vkc
# 1 つだけ
I915_MACHINE=sv7 OUT=build/<WS>-sv7 plan/ws101/tests/hw/run-hw.sh <TAG> eu
```

- `timeout` で括らない（中で build 3000 秒・run 3600 秒の上限が付く）。Bash の background で走らせて待つ。run の間は tree を編集しない。
- 結果: `OUT/hw-<scenario>-<TAG>/run.log` の verdict の行（`test-hw.sh` が最後に `verdict` の行を出す）。`serial.log` は guest の log の写し。
- image の build だけ（QEMU を起動しない）: `I915_MACHINE=sv7 BUILD=build/<dir> ZEDBSD_CONFIG=plan/ws075/tests/config-test-hw.mk VKLOOP_BUILD_ONLY=1 plan/ws031/tests/vkloop-hw.sh test <scenario>`。
  image の boot の config（`BUILD/zedbsd-native-uefi-…-i915.gen9=on.cfg`）の最後の行が `i915.gen9=on` になる。

## 5. 止まったとき

5330 の手引きの 1.9 節と同じ。SV7 の QEMU は launcher の `timeout 360` で止まる。残ったら `ssh awe@10.0.10.27 pgrep -af qemu-system` で見て、
Q1 に報告する（担当は host の process を止めない）。
