#!/bin/bash
# Builds hdd-reloc.img: the parity image with kernel_phys=0x2000000 added to
# ZEDBSD.CFG so the UEFI loader relocates the kernel away from 2 MiB.
set -eu
cd ~/zedBSD
SRC=build/amd64/hdd-image.img
DST=build/amd64/hdd-reloc.img
cp -f "$SRC" "$DST"
found=""
for off in 1048576 68157440; do
  for path in ::/ZEDBSD.CFG ::/EFI/BOOT/ZEDBSD.CFG ::/EFI/ZEDBSD/ZEDBSD.CFG; do
    if mtype -i "$DST@@$off" "$path" >/tmp/zedbsd-orig.cfg 2>/dev/null; then
      found="$off $path"; break 2
    fi
  done
done
if [ -z "$found" ]; then
  echo "ZEDBSD.CFG not found; listing:"; mdir -i "$DST@@1048576" -/ :: | head -20; mdir -i "$DST@@68157440" -/ :: | head -20; exit 1
fi
set -- $found
echo "cfg at offset $1 path $2:"; cat /tmp/zedbsd-orig.cfg
{ cat /tmp/zedbsd-orig.cfg; echo "kernel_phys=0x2000000"; } > /tmp/zedbsd-reloc.cfg
mcopy -o -i "$DST@@$1" /tmp/zedbsd-reloc.cfg "$2"
echo "--- new cfg:"; mtype -i "$DST@@$1" "$2"
scp -q "$DST" solaris10-man:/home/awe/bigbang/guest-parity-reloc.img
ssh solaris10-man 'cd ~/bigbang && for s in run-parity-nogpu run-parity-ref; do sed -e "s#guest-parity.img#guest-parity-reloc.img#; s#run-parity.log#run-parity-reloc.log#; s#run-parity-qemu.log#run-parity-reloc-qemu.log#" $s.sh > $s-reloc.sh; chmod +x $s-reloc.sh; done; grep -n "reloc" run-parity-nogpu-reloc.sh | head -4'
echo "reloc image and scripts ready"
