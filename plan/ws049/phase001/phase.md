<!-- awesome-plan project=zedbsd record=ws049p001 -->

# ws049-p001: 調査と設計

Phase ID: `ws049-p001`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27。HAL の差分の承認と `_OSI`・対象機の table は判断として残す）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザー指示「さらに4つのサブエージェントを起動して、依存関係が満たされているWSに取り組んでください。WS049, …」により
サブエージェントが branch の上で実行。主 session が merge のときに Queue と履歴へ記録する）

## 目的と受け入れ

kernel が ACPI の table を得る道、利用者（WS050・WS051・WS052）の要る範囲、source の置き場、試験の方法を調べ、有限の Phase に分けた設計を作る。
HAL の差分は案として plan に置き、適用しない。

## 成果

- [design.md](../design.md): 今の table の道（§1）、利用者の要求と範囲（§2）、構成と API（§3）、interpreter の設計（§4〜§6）、kernel への組み込み（§7）、
  `_OSI`（§8）、HAL の差分の案と代案（§9）、試験の方法（§10）、Phase（§11）、判断（§12）。
- [proposed/hal-acpi-rsdp.diff](../proposed/hal-acpi-rsdp.diff): `hal_get_arch_handoff("acpi.rsdp")` の差分の案（amd64 の pcat の 3 file）。
  **適用していない。** 確認: `git apply --check` で当たる。scratch の複製に当てて kernel の flag（`-Wall -Wextra -Werror`）で `acpi.c`・`boot.c` を
  compile して成功。`plan/tools/style-check.py` の件数は差分の前後で同じ（acpi.c 3、boot.c 19。既存の file を悪くしない）。
- [tests/qemu-acpi-dump.py](../tests/qemu-acpi-dump.py): QEMU の機種（既定 q35）の ACPI の table を QMP の `pmemsave` で読んで file に出す道具
  （SeaBIOS。console は読まない）。

## 調べた事実（証拠）

- RSDP の道: UEFI の loader → `zbl6_handoff_v2.rsdp` → `prekern_bsp_acpi_rsdp()` → `prekern_amd64_acpi_discover()`（`find_rsdp()` で検証）。
  kernel には渡らない。`hal_get_arch_handoff()` の名前は 4 つ（`boot.command-line`、`boot.selector`、`pcat.boot-font`、`pcat.framebuffer`）。
- QEMU q35 + OVMF（`/usr/share/OVMF/OVMF_CODE_4M.fd`）で `0xE0000`〜`0xFFFFF` に RSDP が無い（`QEMU_ACPI_WAIT=15 qemu-acpi-dump.py` を OVMF の pflash で走らせて 15 秒後に "no RSDP"）。
  SeaBIOS では有る。→ kernel が自分で走査する案は UEFI で使えない。
- kernel の stack は 16 KiB（`AMD64_SYS_STACK_SIZE`）→ 再帰下降の評価器に stack の予算を設ける（design §4.4）。
- 試験の table（git に入れない、`build/ws049/tables/`）:
  - QEMU 10.0.11（Debian の `qemu-system-x86_64`）q35: DSDT 8292 byte、pc: DSDT 8518 byte（`qemu-acpi-dump.py`）。
  - host の Fujitsu PRIMERGY RX2530 M4（`sudo acpidump -b`）: DSDT 272822 byte、SSDT 105728・2371・6994 byte。
  - 対象機（Latitude 5330）: **未取得**。`10.0.10.25` は 2026-09-27 に ssh で "No route to host"。
- host の道具: `acpica-tools` 20250404-1 を導入（`sudo apt-get install acpica-tools`）。`acpiexec -b "namespace" dsdt.dat` で q35 の namespace
  （244 object）を出せることを確かめた。

## 検証

設計の Phase。build・boot test は無し（source を変えていない）。

## 残る判断（ユーザー）

1. HAL の差分 `acpi.rsdp`（design §9、差分の file）。p006 以降の前提。
2. `_OSI` で名乗る Windows の版（design §8）。
3. 対象機と、その table の取り出し。
