<!-- awesome-plan project=zedbsd record=ws049p007 -->

# ws049-p007: SCI・GPE・固定 event・EC の kernel での確認

Phase ID: `ws049-p007`
Parent: [WS049](../ws.md)
Status: in-progress（2026-10-04、q678-i02。T1-086 の FAIL の原因を code で特定して直した（SCI の line を unmask していなかった）。T1 の再試験待ち）
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

## T1-086 の結果（2026-10-04、main df57aad の image、Q1 の判定: q678 uncleared）

- `guest-events.sh`: **FAIL**（2 回とも同じ）。`sci: acpi: SCI on IRQ 9` は出たが、`button:`・`gpe:` が FAILED。QMP の `system_powerdown`・`device_add`
  の答えは `{"return": {}}`、`query-hotpluggable-cpus` は core-id 2・3 が空き。`boot.txt`・`after-button.txt`・`after-gpe.txt` は 3 つとも 3011 byte の
  同じ中身で、dmesg の末尾は `boot: starting init /sbin/init`。証拠: `/home/awe/zedBSD-worktrees/t1/build/t1-086-events-try1/`・`try2/`。
- `guest-compare.sh`: PASS（namespace same、device の違い 11 項目は p006 と同じ）。boot test: PASS。p016（橋の下の bus）の回帰も兼ねる。

### 切り分け（generation15、code を読んで。QEMU は未実行）

- dmesg の取り方は正しい: `dmesg` は `kern.msgbuf`（kernel の ring、amd64 は 512 KiB）を読み、init の後に kernel が何も log しなければ
  boot の log だけになる。3 つが同じなのは、event の後に kernel が何も log しなかったということ（`acpi: power button` も
  `Notify(...) has no handler` も無い）。button と GPE が両方とも届かないので、共通の道（SCI の割り込みの配送、event thread）を先に疑う。
- `ACPI: S5 is SLP_TYP 0/0`（q35 の `_S5`）、`acpi: SCI on IRQ 9`（`kern_irq_register(9)` は成功）は出ている。
- HAL の IOAPIC（`src/hal/amd64/bsp-pcat/ioapic.c` の `write_route`）は MADT の ISO の極性（3 = active low → bit 13）と trigger
  （3 = level → bit 15）を正しく encode している。QEMU の q35 の MADT は IRQ 9 を level・active high と書く。
- 残る仮説（未確認）: (a) QEMU の FADT の `PWR_BUTTON` の flag が 1 で固定の電源 button を有効にしていない（ただし GPE も届かないことは説明しない）、
  (b) IRQ 9 の line が unmask されていない、または level の EOI の扱い、(c) SCI_EN が立っていない（ACPI mode に入っていない。`enable_acpi_mode()` は
  失敗を log するが成功の log は無い）、(d) event thread が走らない、(e) PM1_EN・GPE_EN の書き込みが効いていない。
- 次の一手（再依頼の前に足す診断）: QMP の `human-monitor-command` の `info irq`（IRQ ごとの割り込みの回数。IRQ 9 が 0 なら配送の前で止まっている）と
  `info pic`、QEMU の gdbstub で `drv_acpi_sci_interrupt`・`drv_acpi_events_process`（public の symbol）に breakpoint。kernel の側に「最初の SCI」
  「ACPI mode に入った（SCI_EN）」「PM1_EN の値」の 1 回だけの log を足すと、script だけで切り分けられる。`guest-events.sh` に `info irq` の取得を足す。

## 原因と修正（2026-10-04、P1 generation16、q678-i02）

- **原因（code を読んで特定）**: `start_events()` は `kern_irq_register(9, sci_interrupt)` の後に `kern_irq_unmask(9)` を呼んでいなかった。
  amd64 の HAL（`src/hal/amd64/irq.c`）は全ての logical IRQ を masked で初期化し、`hal_irq_register()` は handler を置くだけで unmask しない
  （dispatch は `!service->masked` のときだけ handler を呼ぶ）。他の driver（ps2-8042・serial-mirror・ne2000・pci の INTx）は全て register の後に
  `kern_irq_unmask()` を呼ぶ。IRQ 9 が I/O APIC で mask されたままなので、PWRBTN_STS も GPE も SCI として CPU に届かず、log に何も出なかった
  （button と GPE が両方とも届かない症状、仮説 (b) と一致）。QEMU は起動していない。
- **修正**: `src/drivers/acpi/acpi-kern.c` の `start_events()` の最後（power button の handler の後）で `kern_irq_unmask((int)irq)`。
- **診断の log（恒常、1 回だけ）**: `acpi-event.c` の `drv_acpi_events_init()` の最後に `ACPI: SCI_EN n, PM1_EN 0x…, N runtime GPEs`、
  `acpi-kern.c` の event thread が最初の SCI を処理したときに `acpi: first SCI handled`。
- **実害の修正（p009 の findings、同じ道）**: `drv_acpi_events_init()` が PM1a の control block の無い FADT で port 0 を読まないよう ENODEV。
- `guest-events.sh`: QMP の `human-monitor-command` で `info irq`・`info pic` を boot・button の後・GPE の後に `irq-*.txt`・`pic-*.txt` へ取り、
  summary に `state:`（SCI_EN の行）と `first SCI:` の数を出す。合否の基準は変えない（`sci:`・`button:`・`gpe:`）。
- 確認（host）: `make -C plan/ws049/tests`（ASan・UBSan）、`make -C plan/ws049/tests kernel-check`（warning 0）、`run-asl.py`（18 passed）、`sh -n guest-events.sh`。
- T1 の再試験: 依頼の手順は上の「T1 への依頼」の 1〜5 と同じ（この修正の commit を含む main の image で）。追加の合格の目安: `state:` が `SCI_EN 1`、
  `irq-after-button.txt` の IRQ 9 の数が `irq-boot.txt` より増える。
