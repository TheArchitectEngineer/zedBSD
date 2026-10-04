<!-- awesome-plan project=zedbsd record=ws049p006 -->

# ws049-p006: kernel への組み込み（amd64）

Phase ID: `ws049-p006`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27。承認済みの HAL の差分と統合を当て、QEMU の guest の中の namespace と device の評価が host の aml-host と一致）
Phase disposition: normal
Queue: なし（2026-09-27 のサブエージェントの作業。rate limit で止まった分を `salvage/ws049` eb6e6696（親 ba99a981）から片付けのサブエージェントが検証して commit した）

## 目的と受け入れ

ACPI の driver（`src/drivers/acpi/`）を amd64 の kernel image に入れ、起動時に firmware の table を読み込み、QEMU（q35、OVMF）の guest の中から
namespace と `_STA`・`_CRS`・`_HID` の評価を確かめる。build（warning 0）、boot test。

前提: **HAL の差分 [proposed/hal-acpi-rsdp.diff](../proposed/hal-acpi-rsdp.diff) の承認**（[design.md](../design.md) §9）。それまで kernel は
`kern_boot_handoff("acpi.rsdp")` で NULL を受け取り、ACPI を止めたまま起動する。

## 承認の前の準備（2026-09-27）

- 共有の build の file への差分 [proposed/p006-integration.diff](../proposed/p006-integration.diff)（SHA256
  `10ec652e180df60b17ccb72ce7dcc14f2f7f4d3375c4b47b4339579074e082ba`）: `config/drivers/architecture/amd64.drivers` に `CONFIG_DRIVER_ACPI`
  （amd64 の既定 y）、`Makefile` の既定と `-DCONFIG_DRIVER_ACPI`、`platform/amd64/vmunix.mk` の `AMD64_ACPI_SOURCES`
  （`src/drivers/acpi/*.c`）、`src/kern/platform/pcat.c` で PCI の列挙の後に `drv_acpi_attach()`。**branch には commit していない**
  （主 session の作業中の `vmunix.mk` と衝突しないように。merge の後に当てる）。
- この差分を worktree に一時的に当てて確かめ、元に戻した:
  - `make -j16 vmunix`（`config.mk` は主の tree の複製、amd64・native）: warning 0、`kernel include check: PASS (299 objects, 7763 dependencies)`、
    `amd64 vmunix check: PASS`。`vmunix` の SHA256 `b14c6b7c23fb21d49649df867541efbb28d6b36fe0882d0e744fe86ff9787137`、`drv_acpi` の symbol 60 個（LTO で
    `drv_acpi_attach` は inline）。
  - boot test: 主の tree の `build/amd64/hdd-image.img`（2026-09-26 08:00）の複製の ESP の `vmunix` をこの kernel に置き換え、
    `OUTPUT=build/ws049/boot/test-acpi-off plan/tools/boot-test.sh build/ws049/boot/hdd-acpi.img` → **PASS**
    （`build/ws049/boot/test-acpi-off/login.png`、login prompt）。HAL が RSDP を渡さないので ACPI は止まったまま（QEMU の証拠。実機は未実施）。

## 承認の後にすること

1. `git apply plan/ws049/proposed/hal-acpi-rsdp.diff` と `plan/ws049/proposed/p006-integration.diff`。
2. 診断の口（design §7。案: `/dev/acpi` の read で namespace の一覧、path を write してから read で評価の結果。ユーザーの判断）。
3. QEMU（q35、OVMF）で guest の中から namespace と `_STA`・`_CRS`・`_HID` を確かめ、host の aml-host の `--firmware`（QEMU の table）と比べる。
4. build（warning 0）、boot test。

## 実行（2026-09-27）

承認: 2026-09-27 ユーザー「HAL approvalsは3つとも承認します。」（[Guardrail](../../guardrail.md) の表、`hal-acpi-rsdp.diff` SHA256 `6208a4dd…`）。

### 当てたもの

| file | 内容 |
| --- | --- |
| `src/hal/amd64/bsp-pcat/acpi.c`・`acpi.h`・`boot.c` | 承認済みの差分 [proposed/hal-acpi-rsdp.diff](../proposed/hal-acpi-rsdp.diff) そのもの（HAL の ACPI の発見が受け入れた RSDP の物理 address を `hal_get_arch_handoff("acpi.rsdp")` で返す。hal.h は不変）。**一致の確認**: 親 ba99a981 の 3 file に承認済みの差分を `patch -p1` で当てた結果と、salvage の 3 file が byte 単位で同じ |
| `Makefile`・`config/drivers/architecture/amd64.drivers`・`platform/amd64/vmunix.mk`・`src/kern/platform/pcat.c` | [proposed/p006-integration.diff](../proposed/p006-integration.diff) の統合（親に当てた結果と byte 単位で同じ。`CONFIG_DRIVER_ACPI`、amd64 の既定 y、`src/drivers/acpi/*.c` の link、PCI の列挙の後に `drv_acpi_attach()`） |
| `src/drivers/acpi/acpi-kern.c` | PCI_Config の region の handler: PCI の driver は address を padding まで含めて比べるので、探す address を 0 で埋めてから field を入れる（padding の違いで function が見つからないことを防ぐ） |
| `plan/ws049/tests/guest-compare.sh`（新）・`qemu-acpi-dump.py` | guest の `/dev/acpi` の namespace と device の評価を、同じ q35 の machine の table の aml-host の結果と比べる。dump の machine の引数を既定の後に置く（`-serial`・`-m` を上書きできる） |

### 確認（QEMU。実機は未実施）

| 確認 | 結果 |
| --- | --- |
| lean の image `make -j48 ZEDBSD_CONFIG=plan/tools/gnu-utils/config-amd64-base.mk BUILD=build/ws049/image disk-image` | status 0、warning 0、`kernel include check: PASS (301 objects, 7813 dependencies)`、`amd64 vmunix check: PASS`、`vmunix` の ACPI の symbol 76、SHA256 `48f57073…` |
| boot test `OUTPUT=build/ws049/boot-lean plan/tools/boot-test.sh build/ws049/image/hdd-image.img`（uefi-usb、ACPI が動く kernel） | **PASS**（`build/ws049/boot-lean/login.png`、login prompt） |
| `sh plan/tools/guest/hybrid-image.sh build/ws049/image build/ws049/hybrid.img` の後 `sh plan/ws049/tests/guest-compare.sh build/ws049/hybrid.img`（q35・OVMF・8 GiB・4 CPU、SSH） | namespace: host 278 行・guest 278 行、**同じ**。device の評価 105 行（`_HID` 29・`_CRS` 28・`_UID` 23・`_STA` 16・`_ADR` 7・`_CID` 2）: 94 行同じ、11 行が違う。違いは全て firmware が決める hardware の状態を AML が読むもの: `LNKA`〜`LNKH` の `_CRS` の IRQ（guest は OVMF が割り当てた 10・11、host の dump は firmware が走らず 0）、`PCI0._CRS` と `DRAC._CRS` の 64 bit の窓（OVMF の設定）、`HPET._STA`（guest 0xF、host 0）。guest の値が正しい |
| style（`style-check.py`） | 変えた kernel と HAL の file の件数は変わらない（`pcat.c` 31、`acpi.c` 3、`boot.c` 23、HEAD と同じ）、`acpi-kern.c` 0 |
| aml-host（`make -C plan/ws049/tests`、ASan・UBSan、`-Werror`） | build 成功 |
| full の desktop の image の boot test、i386 | 未実施（i386 は `pcat.c` を使わない。ACPI は amd64 だけ） |
| 実機（Latitude 5330） | 未実施（p008） |

### 残り

- SCI・GPE・固定 event・EC の kernel での確認は p007。対象機の table と実機は p008。
