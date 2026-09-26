<!-- awesome-plan project=zedbsd record=ws049 -->

# WS049: kernel 内の ACPI AML interpreter

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG003
Related Milestones: MG006, MG008
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
Resume point: p006（kernel image への組み込みと QEMU での確認）。HAL の差分 `acpi.rsdp` の承認を待つ
<!-- awesome-plan-current:end -->

## 目標

kernel の中に ACPI の AML interpreter を持ち、DSDT・SSDT を読み込んで namespace を作り、driver が AML の method（`_STA`・`_CRS`・`_PS0`・`_PS3`・
`_DSM`・`_Lxx`/`_Exx` ほか）を評価できるようにする。UCSI（WS050）、USB-C の DisplayPort Alternate Mode（WS051）、S0i3 の電源管理（WS052）の土台。

## きっかけ

2026-09-24 ユーザー指示: 「下記をそれぞれWSとして追加してほしいです。カーネル内ACPI AMLインタプリタの実装。…」

## 今あるもの

- ACPI の table の解析は HAL の中にだけある（`src/hal/amd64/bsp-pcat/acpi.c`、`src/hal/i386/acpi.c`: RSDP（loader が渡すものか firmware の走査）・XSDT・MADT・MCFG）。
- HAL に ACPI の table の専用の口は無いが、**機種依存の情報を名前で問い合わせる `hal_get_arch_handoff(name)`**（`include/hal/hal.h`）がある
  （2026-09-24 ユーザーの指摘）。今の名前は `boot.command-line`・`boot.selector`・`pcat.boot-font`・`pcat.framebuffer`（`src/hal/amd64/bsp-pcat/boot.c`）。
  kernel は、ここに足す名前（例: RSDP の物理番地か、検証済みの table の一覧）で ACPI の table を受け取り、`hal_space_map_device()` で読める。
  `hal.h` は変えずに済むが、`src/hal` の既存の宣言の実装に名前を足すので、**差分ごとの承認が要る**（p001 が案を作る）。
- SCI の割り込みは `hal_irq_register()`、PM1・GPE の register は `hal_io_inp*`・`hal_io_outp*`（port I/O）と `hal_mmio_*` で扱える見込み（p001 で確かめる）。
- 対象機は Dell Latitude 5330（Alder Lake-P、WS029・WS031 と同じ）を想定（仮定。ユーザーの確認が要る）。QEMU の q35 の DSDT は小さく、単体の試験に使える。

## 範囲

- AML の byte code の解析と namespace（Scope・Device・Method・Name・OperationRegion・Field・Mutex・Event・Alias、外部参照）。
- 評価器: 整数・文字列・buffer・package、制御（If・While・Return）、演算、`Notify`、`Sleep`・`Stall`、`Acquire`・`Release`。
- OperationRegion の access: SystemMemory、SystemIO、PCI_Config、EmbeddedControl（EC の driver が要る）、GenericSerialBus は要否を調べる。
- SCI と GPE の割り込み、`_Lxx`・`_Exx` の実行、`Notify` を driver へ。
- `_OSI` の答え方（Windows の版を名乗るかどうか。Alder Lake の機種は多くの機能を Windows の版で切り替える）。

範囲外（別 WS）: UCSI（WS050）、DP Alt Mode（WS051）、電源管理（WS052）。

## 受け入れ

- QEMU（q35）と対象機の DSDT・SSDT を読み込んで namespace を作り、全 method を評価せずに列挙でき、`_STA`・`_CRS`・`_HID` を評価できる。
- 対象機で EC を介する `_Qxx`（例: 電源 button・lid）か GPE の event が driver に届く。
- AML の試験の集合（自作の ASL を iasl で作った AML と、実機の table の抜き出し。license に注意）で評価の結果を確かめる。
- 規約（`plan/coding-style.md`）の全文、build（warning 0）、boot test。

## Phase 一覧

設計: [design.md](design.md)（2026-09-27、p001）。p002〜p005、p010、p011 は host だけで進められる（完了）。p006 以降は HAL の差分の承認が前提。

| Phase | 内容 | Status | 依存 | 対象 |
| --- | --- | --- | --- | --- |
| [ws049-p001](phase001/phase.md) | 調査と設計: table の道、利用者の要求、構成、評価の方式、HAL の差分の案、試験の方法 | cleared（2026-09-27） | — | 設計文書 |
| [ws049-p002](phase002/phase.md) | object・namespace・byte 列・DefinitionBlock の読み込み、host の harness | cleared（2026-09-27） | p001 | `src/drivers/acpi/` |
| [ws049-p003](phase003/phase.md) | 評価器: method、制御、全ての式の opcode、参照、変換、Store の規則 | cleared（2026-09-27） | p002 | 同上 |
| [ws049-p004](phase004/phase.md) | OperationRegion・Field・IndexField・BankField・BufferField、region の handler と `_REG` | cleared（2026-09-27） | p003 | 同上 |
| [ws049-p005](phase005/phase.md) | 同期と OS の口: Mutex・Event・Sleep・Notify・`_OSI`・Load/LoadTable/Unload・`_INI`、stack の予算 | cleared（2026-09-27） | p004 | 同上 |
| [ws049-p006](phase006/phase.md) | kernel への組み込み（amd64）: kernel image への link（`CONFIG_DRIVER_ACPI`、vmunix.mk、`pcat.c` の `drv_acpi_attach()`）、起動時の読み込み、診断の口、QEMU（q35・OVMF）での確認 | planned（**HAL の差分の承認待ち**。統合の差分は準備済みで、当てた kernel の build と boot test（ACPI は止まったまま）は PASS） | p010、**HAL の差分の承認** | `src/drivers/acpi/`、platform |
| ws049-p007 | SCI・GPE・固定 event・EC の kernel での確認: SCI の割り込み、event thread、QEMU の `system_powerdown`（固定の電源 button）と GPE | planned | p006、p011 | 同上 |
| ws049-p008 | 対象機（Latitude 5330）の table と実機の確認 | planned | p007、対象機の table | 同上 |
| ws049-p009 | 規約の全文の確認と最終の確認 | planned | p002〜p008、p010、p011 | WS の全 source |
| [ws049-p015](phase015/phase.md) | ECDT: `_REG`・`_INI` の前の EC（ECDT の検査、早い address space、後の device の GPE と query、`_CRS` との食い違い） | cleared（2026-09-27。kernel の上の実行は p007 で） | p011 | `src/drivers/acpi/` |
| [ws049-p014](phase014/phase.md) | FACS の Global Lock の hardware の手順（firmware と取り合い、pending・GBL_RLS・GBL_STS）。host の疑似の firmware で試験 | cleared（2026-09-27。kernel の上の実行は p007 で） | p011 | `src/drivers/acpi/` |
| [ws049-p013](phase013/phase.md) | p006 のうち承認なしでできる部分: 診断の口 `/dev/acpi`（read で namespace、path を write して評価）と、kernel と harness で共有する出力の処理（`acpi-text.c`） | cleared（2026-09-27。kernel の上の実行は p006 で） | p010 | `src/drivers/acpi/` |
| [ws049-p012](phase012/phase.md) | 壊れた table への堅牢性: AML の byte を変えた table を sanitizer の下で読み込み、全 method を走らせる（fuzz） | cleared（2026-09-27） | p011 | 試験 |
| [ws049-p011](phase011/phase.md) | p007 のうち承認なしでできる部分: event の核（FADT、ACPI mode、PM1・GPE、`_Lxx`/`_Exx`、wake GPE、割り込みと thread の分担）と EC（`_CRS`・`_GPE`・`_GLK`、protocol、EmbeddedControl の region、`_Qxx`）。host の疑似の hardware で試験、kernel の側は compile | cleared（2026-09-27） | p010 | `src/drivers/acpi/` |
| [ws049-p010](phase010/phase.md) | p006 のうち承認なしでできる部分: firmware の table の発見（`acpi-tables.c`、host の疑似の物理 memory で試験）、kernel の glue（`acpi-kern.c`、kernel の flag で compile） | cleared（2026-09-27） | p005 | `src/drivers/acpi/` |

## 人間の判断が要る点

- `hal_get_arch_handoff("acpi.rsdp")` を足す差分の承認（[design.md](design.md) §9、差分 [proposed/hal-acpi-rsdp.diff](proposed/hal-acpi-rsdp.diff)。
  `hal.h` は変えない。未適用）。p006 以降の前提。
- 診断の口の形と device 番号（[phase013](phase013/phase.md): UAPI を足さない text の `/dev/acpi`、`0x000B0000` を実装済み。ioctl や
  `/dev/system` への統合にするなら直す）。
- 対象機（Latitude 5330 でよいか）と、その table の取り出し（Linux で `sudo acpidump -b`）。
- `_OSI` でどの Windows を名乗るか（design §8、案は `Windows 2022` まで真）。
