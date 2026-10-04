<!-- awesome-plan project=zedbsd record=ws049p007 -->

# ws049-p007: SCI・GPE・固定 event・EC の kernel での確認

Phase ID: `ws049-p007`
Parent: [WS049](../ws.md)
Status: in-progress（2026-10-04。試験の道具を用意し、QEMU の試験を Q1 経由で T1 に依頼。結果待ち）
Phase disposition: normal
Queue: q678 / q678-i01（P1 generation15）。承認: q677 と同じ（2026-10-04 user の DP Alt Mode の目標と 17 時の体制の指示）

## 目的と受け入れ

p011・p014・p015 で host の疑似の hardware で試した event の核（FADT、ACPI mode、PM1・GPE、`_Lxx`/`_Exx`、SCI と thread の分担）、
Global Lock、EC が、p006 で kernel に組み込んだ後に実際の割り込みで動くことを確かめる（[design.md](../design.md) §7・§11）。

受け入れ（QEMU、q35・OVMF・KVM）:

1. 起動の log に `acpi: SCI on IRQ N`（SCI の割り込みの登録）。
2. QMP の `system_powerdown`（PM1 の PWRBTN_STS）で、kernel の固定 event の handler が `acpi: power button` を log に出す。
3. CPU の hot-add（QMP の `device_add`）で GPE 2 が上がり、`\_GPE._E02` → `\_SB.CPUS.CSCN` が新しい CPU に `Notify(…, 1)` を送り、
   kernel が `ACPI: Notify(\_SB_.CPUS.Cxxx, 0x1) has no handler` を log に出す（GPE の SCI、event thread での AML の実行、Notify の配送）。

EC（`_Qxx`）・蓋・AC・実機の電源ボタンは QEMU に無いので、実機の UAT（p008 の image と同じ回）で確かめる（下の「実機」）。

## 変更（2026-10-04）

kernel の source は変えていない（event・EC の code は p011・p014・p015 と p006 のまま。読み直して、QEMU の q35 で通るはずの道を確かめた:
`drv_acpi_events_init` が SMI_CMD に ACPI_ENABLE を書いて SCI_EN を待ち、全 event を mask・clear してから `_Exx` の GPE を有効にする。
SCI は FADT の `SCI_INT` を `kern_irq_register()`、IRQ の極性・trigger は HAL が MADT の override で設定する。固定の電源 button は FADT の
`PWR_BUTTON` が 0 のときだけ有効にする）。

| file | 内容 |
| --- | --- |
| `plan/ws049/tests/guest-events.sh`（新） | 受け入れ 1〜3 の QEMU の試験。guest の console は読まず、kernel の log は SSH の `dmesg`、event は QMP で起こす。`-smp 2,maxcpus=4`（`--qemu-extra`、QEMU の `-smp` は後の指定が前に重なる）で CPU の空きの slot を作る |
| `plan/ws049/tests/qmp-send.py`（新） | guest の QMP の socket に command を 1 つ送り、答えを JSON で出す |
| `plan/ws049/tests/config-acpi.mk`（新） | 試験の image の config（CI の amd64 の構成そのもの、`CONFIG_DRIVER_ACPI=y`） |

## 検証

| 確認 | 結果 |
| --- | --- |
| `sh -n guest-events.sh`・`python3 -m py_compile qmp-send.py` | 通る |
| QEMU の試験（T1） | **依頼中**（2026-10-04、Q1 経由。下の依頼） |
| 実機 | 未実施（ユーザーの UAT） |

### T1 への依頼（Q1 経由）

1. image: この Phase の commit を含む main で
   `plan/tools/guest/test-image.sh plan/ws049/tests/config-acpi.mk build/ws049-acpi`（`build/ws049-acpi/hdd-image.img`）。
2. `make -C plan/ws049/tests`（host の harness、guest-compare.sh が使う）。
3. `sh plan/ws049/tests/guest-events.sh build/ws049-acpi/hdd-image.img` → 合格: `sci:`・`button:`・`gpe:` の 3 行が FAILED でなく、終了 code 0。
   成果: `build/ws049/events/`。
4. p008 の回帰（同じ image）: `sh plan/ws049/tests/guest-compare.sh build/ws049-acpi/hdd-image.img` → 合格: `namespace: same`。
   device の評価の違いは p006 と同じ 11 行（LNKA〜H の `_CRS` の IRQ、`PCI0._CRS`・`DRAC._CRS` の 64 bit の窓、`HPET._STA`）だけ。
5. `plan/tools/boot-test.sh build/ws049-acpi/hdd-image.img` → login prompt（PNG）。

## 実機（ユーザーの UAT、p008 と同じ回）

| 観点 | 合格 |
| --- | --- |
| 電源ボタンを短く押す | dmesg に `acpi: power button`（固定 event）か、`Notify(\_SB_.PWRB, 0x80)`（control method の button） |
| 蓋の開け閉め | dmesg に `Notify(\_SB_.LID0, 0x80)`（EC の `_Qxx` から） |
| AC の抜き差し | dmesg に `Notify(\_SB_.AC__, 0x80)` か電池への Notify（EC の `_Qxx` から） |
| dmesg の EC の行 | `ACPI: EC at ports 0x930/0x934, GPE 0x…`、`EC command … failed` が無い |

Notify を受けて動く driver（電源の状態、蓋、ボタンの動作）は WS132・WS052。この Phase は event が kernel に届くところまで。

## 残り

- T1 の結果。FAIL なら gdbstub と QMP で解析して直す。
- 実機の確認。
