#!/bin/bash
# Build and run the GPU-free contract tests for the Linux-parity adaptation layer.
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
CFLAGS="-std=c99 -Wall -Wextra -Werror -O1 -g"
OUT=/tmp/parity-tests
mkdir -p "$OUT"

rc_total=0

echo "=== building dma_contract_test ==="
$CC $CFLAGS -o "$OUT/dma_contract_test" \
    dma_contract_test.c mock_dma.c ../osdep/dma.c ../osdep/trace.c
echo "=== running dma_contract_test ==="
"$OUT/dma_contract_test" || rc_total=1

echo "=== building mmio_contract_test ==="
$CC $CFLAGS -o "$OUT/mmio_contract_test" \
    mmio_contract_test.c mock_mmio.c ../osdep/mmio.c ../osdep/trace.c
echo "=== running mmio_contract_test ==="
"$OUT/mmio_contract_test" || rc_total=1

echo "=== building pci_contract_test ==="
$CC $CFLAGS -o "$OUT/pci_contract_test" \
    pci_contract_test.c mock_pci.c ../osdep/pci.c ../osdep/trace.c
echo "=== running pci_contract_test ==="
"$OUT/pci_contract_test" || rc_total=1

echo "=== building sync_contract_test ==="
$CC $CFLAGS -o "$OUT/sync_contract_test" \
    sync_contract_test.c ../osdep/sync.c ../osdep/trace.c
echo "=== running sync_contract_test ==="
"$OUT/sync_contract_test" || rc_total=1

echo "=== building rpm_contract_test ==="
$CC $CFLAGS -o "$OUT/rpm_contract_test" \
    rpm_contract_test.c ../osdep/runtime_pm.c ../osdep/trace.c
echo "=== running rpm_contract_test ==="
"$OUT/rpm_contract_test" || rc_total=1

echo "=== building pte_contract_test ==="
$CC $CFLAGS -o "$OUT/pte_contract_test" \
    pte_contract_test.c ../pte.c ../osdep/trace.c
echo "=== running pte_contract_test ==="
"$OUT/pte_contract_test" || rc_total=1

echo "=== ALL TESTS rc=$rc_total ==="
exit $rc_total
