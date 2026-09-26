<!-- awesome-plan project=zedbsd record=ws049p006 -->

# ws049-p006: kernel への組み込み（amd64）

Phase ID: `ws049-p006`
Parent: [WS049](../ws.md)
Status: planned（HAL の差分 `acpi.rsdp` の承認を待つ。承認の前にできる準備は済んだ）
Phase disposition: normal
Queue: なし

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
