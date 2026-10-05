#!/bin/sh
# Builds and runs the host test of the ACPI side of the BAR assignment
# (BUG-210): src/drivers/acpi/acpi-pci-root.c in the WS049 AML harness,
# on the Latitude 5330's DSDT and SSDTs.  The harness's main is renamed and
# its --resources walk is sent to the test's hook.
# Usage: plan/ws159/tests/run-host-pci-root.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws159-host-pci-root}
mkdir -p "$out"

cc=${CC:-cc}
flags="-std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
	-fno-sanitize-recover=all -fno-omit-frame-pointer -I$root/include -I$root/src"
sources="$(ls "$root"/src/drivers/acpi/aml-*.c) $root/src/drivers/acpi/acpi-tables.c \
	$root/src/drivers/acpi/acpi-event.c $root/src/drivers/acpi/acpi-ec.c \
	$root/src/drivers/acpi/acpi-text.c $root/src/drivers/acpi/acpi-resource.c \
	$root/src/drivers/acpi/acpi-pci-root.c $root/plan/ws049/tests/aml-host-hardware.c"
objects=
for source in $sources; do
	object="$out/$(basename "$source" .c).o"
	$cc $flags -c "$source" -o "$object"
	objects="$objects $object"
done
$cc $flags -Dmain=aml_host_main -Ddrv_acpi_resources_walk=resources_hook \
	-c "$root/plan/ws049/tests/aml-host.c" -o "$out/aml-host.o"
$cc $flags -c "$root/plan/ws159/tests/host-pci-root.c" -o "$out/host-pci-root.o"
$cc $flags $objects "$out/aml-host.o" "$out/host-pci-root.o" -o "$out/host-pci-root"

tables="$root/plan/ws049/tests/latitude5330/dsdt.dat"
for number in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
	tables="$tables $root/plan/ws049/tests/latitude5330/ssdt$number.dat"
done
"$out/host-pci-root" --absent-pci 0:10.6 --absent-pci 0:10.7 --quiet \
	--resources '\_SB_.PC00' $tables
