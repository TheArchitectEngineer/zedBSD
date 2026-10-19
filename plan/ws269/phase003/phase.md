<!-- awesome-plan project=zedbsd record=ws269-p003 -->

# ws269-p003 firmware の package（powervr-firmware）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) §3.3・§4・§6 の p003・§8 の D3
- Queue: q1328 / q1328-i01（P2、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定: package・mirror の sha256・license の同意・VF2 の rootfs への install・menuconfig の host 試験 6 target が PASS。CONFIG_DRIVER_POWERVR の既定は driver が出来るまで n（D3 (a) の既定 on は p004 で driver を作る時に y にする、引き継ぎは本文）。VF2 の disk image・QEMU・実機は未実施）
- 前提: p002（cleared）、D3 (a)（決定済み）、mirror（[phase002](../phase002/phase.md) の「mirror」節、awemorris/zedBSD-firmware df57bb4）

## 目的と受け入れの条件

1. `userland/firmware/powervr/`（package `powervr-firmware`）を既存の firmware の package の流儀で作る: 取得、size・sha256 の検証、license・WHENCE・manifest の install、build の時の license の同意。
2. README に逆アセンブルの禁止を書く（design §4）。
3. driver を `config/drivers/firmware.list` に載せる（D3 (a)）。
4. VF2 の config で firmware が image に入ること、amd64 に影響が無いこと、build の warning 0。QEMU は起動しない。

## 実装

| file | 中身 |
| --- | --- |
| `userland/firmware/powervr/Makefile` | rtl8822b の package と同じ形（3 file の cache、size・sha256 の検証、壊れた cache は誤り、一時 directory で取って検証してから公開、fixture の goal）。上流は Imagination の fork（`gitlab.freedesktop.org/imagination/linux-firmware/-/raw`、commit `8a58f81883f7be458daa34e418cc4079f995b279`）、先に zedBSD の firmware の repository の `powervr/` から取る。install: `/lib/firmware/powervr/rogue_36.50.54.182_v1.fw`、`/usr/share/licenses/powervr-firmware/{LICENSE.powervr,WHENCE}`、`/usr/share/zedbsd/packages/powervr-firmware.manifest`。platform は `*`（bcm43455 と同じ、board の firmware） |
| `userland/firmware/powervr/powervr-firmware.manifest` | 上流の commit・path・size・sha256、mirror の commit、再配布の条件、`reverse-engineering=forbidden` |
| `userland/firmware/powervr/powervr-firmware.notice` | license の同意の画面に出す説明 |
| `userland/firmware/zedbsd-firmware.mk` | firmware の repository の revision を `b6ae441…` → `df57bb4b778ace43df7d472185d041e5bd4ef9ce`（powervr/ を足した commit、親が b6ae441 で足しただけ。他の package の bytes は同じで、size・sha256 はどちらでも検証される） |
| `userland/firmware/README.md` | `powervr/` の段落。**逆アセンブルの禁止**を太字で書いた |
| `config/drivers/architecture/riscv64.drivers`（新規） | `CONFIG_DRIVER_POWERVR`（bool、visionfive2、既定 n、label に「driver not built yet」） |
| `config/drivers/firmware.list` | `CONFIG_DRIVER_POWERVR|powervr-firmware` |
| `tools/release/license-components.json` | `powervr-firmware` の行（notices に LICENSE.powervr・WHENCE） |
| `Makefile` | 設定なしで走る goal に `powervr-firmware-fixture-cache` |
| `plan/ws269/tests/config-p003.mk` | 確かめの config（`config/ci/config-visionfive2.mk` に `powervr-firmware` を足しただけ） |

### firmware.list と driver の key の決め方

- driver の名前は design §3 の `src/drivers/gpu/powervr/` に合わせ `CONFIG_DRIVER_POWERVR`。
- `plan/tools/menuconfig-target-host-test.py`（MAC-T001）は「firmware.list の key は driver の catalog にある」「どの firmware の package も firmware.list のどれかの driver が要る」の両方を要求する。driver はまだ無いので、key だけを riscv64 の catalog（新規）に置いた。**既定は n**: driver の本体が無いうちに on にすると VF2 の image（CI を含む）が働かない driver のために license の同意と取得を求めるため。
- **p004 への引き継ぎ**: driver の本体（`src/drivers/gpu/powervr/`、Makefile の `CONFIG_DRIVER_POWERVR` の既定と `-D`）を作る時に、riscv64.drivers の既定を y にし、label の「not built yet」を外し、`config/ci/config-visionfive2.mk` を menuconfig で作り直す（D3 (a): 結果として VF2 の image に FW が入り、build の時に LICENSE.powervr の同意）。

### initrd

riscv64 の root の image（`root.img`・`root.ufs`）は同じ rootfs の tree（`platform/riscv64/rootfs.mk`）から作るので、package を選べば root がどちらでも `/lib/firmware/powervr/` に入る。riscv64 の build には initrd の image を別に作る規則は見当たらなかった（`grep -i initrd` は HAL の handoff だけ）。initrd を root にする形が足されたら、その形でも同じ tree を使うかを確かめる（p005 の遅い段が `/lib/firmware/powervr/` を読む前提）。

### いまの挙動（driver が未実装の間）

- `config/ci/config-visionfive2.mk` は変えていない（`CONFIG_DRIVER_POWERVR` の行が無い = n）。VF2 の CI の image には firmware は入らない。menuconfig で `CONFIG_DRIVER_POWERVR` を on にするか Firmware の menu で `powervr-firmware` を選ぶと入る。

## 確かめ（2026-10-19、worktree /home/awe/zedBSD-worktrees/p2、host）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| 上流と mirror の bytes | `curl` で mirror（df57bb4 の `powervr/`）と GitLab（8a58f818）から 3 file を取り `wc -c`・`sha256sum` | 両方一致: fw 131072 / `59127d3a…7abb`、LICENSE.powervr 2190 / `1c9aa6bd…f402`、WHENCE 341319 / `ec86d764…46c3` |
| license の同意と取得 | `yes \| timeout 600 make ZEDBSD_CONFIG=plan/ws269/tests/config-p003.mk BUILD=build/ws269-p003 firmware-licenses` | LICENSE.powervr の全文を出して `powervr-firmware: license accepted` |
| VF2 の rootfs | `timeout 1800 make ZEDBSD_CONFIG=plan/ws269/tests/config-p003.mk BUILD=build/ws269-p003 rootfs` | rc=0、log の `warning` 0 件。cache `build/sources/firmware/powervr/8a58f818…/` の 3 file を検証。`build/ws269-p003/rootfs/lib/firmware/powervr/rogue_36.50.54.182_v1.fw`（131072、sha256 一致）、`usr/share/licenses/powervr-firmware/{LICENSE.powervr,WHENCE}`、`usr/share/zedbsd/packages/powervr-firmware.manifest` を確かめた |
| menuconfig（amd64 を含む 6 target） | `timeout 600 make menuconfig-host-test` | PASS（firmware licenses・round-trip、「the drivers' firmware selected with them」）。firmware.list の行の platform は visionfive2 だけなので amd64 の選択は変わらない（`driver_firmware({'CONFIG_DRIVER_PCI_AMDRDNA':'y'},'amd64')` = `{'amd-navi-firmware'}`） |

未実施: VF2 の disk image の作成と実機・QEMU での起動（QEMU は起動しない指示。driver が無いので FW を読む者もいない）。amd64 の image の build（firmware.list・catalog の変更は visionfive2 だけに効き、zedbsd-firmware.mk の revision の変更は bytes の検証を変えないので、menuconfig の試験で足りると判断）。

## 残件

- p004: 上の「p004 への引き継ぎ」。
- 片付け（Q1 に依頼）: `/home/awe/zedBSD-worktrees/p2/build/ws269-p003/`、`/home/awe/zedBSD-worktrees/p2/build/ws269-p003.log`。
