#!/bin/bash
# WS031 parity — REFERENCE-condition launcher for the zedBSD parity image.
# CPU/RAM/vCPU/memory-backend/VFIO are the manifest reference values
# (host-phys-bits-limit=39, 4 GiB, 4 vCPU, x-igd-opregion=on); the zedBSD boot
# media (OVMF + IDE image + debugcon log) is OS-specific and kept as-is.
set -u
cd /home/awe/bigbang
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd vg-parity.fd 2>/dev/null || cp -f /usr/share/OVMF/OVMF_VARS.fd vg-parity.fd

# Reference common settings (replace, not append).
COMMON=(
  -machine q35,accel=kvm,memory-backend=mem
  -cpu host,host-phys-bits-limit=39
  -m 4096
  -smp 4
  -object memory-backend-memfd,id=mem,size=4G,share=on
  -device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0
)
# zedBSD boot media + diagnostics (OS-specific).
MEDIA=(
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd
  -drive if=pflash,format=raw,file=/home/awe/bigbang/vg-parity.fd
  -drive file=/home/awe/bigbang/guest-parity.img,format=raw,if=none,id=zd0 -device nvme,drive=zd0,serial=zedbsd0
  -vga std -display none -monitor none -serial file:/home/awe/bigbang/run-parity-serial.log -nic none
  -debugcon file:/home/awe/bigbang/run-parity.log -no-reboot
)

# WS031 p014: QMP=1 adds a QMP socket (the capture harness reads guest RAM with pmemsave and sends
# input) and a USB tablet and keyboard; VK_STOP_RE replaces the end-of-run pattern of the watcher.
if [ "${QMP:-0}" = 1 ]; then
  rm -f /home/awe/bigbang/qmp.sock
  MEDIA+=( -qmp unix:/home/awe/bigbang/qmp.sock,server=on,wait=off
           -device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 -device usb-kbd,bus=xhci.0 )
fi
STOP_RE="${VK_STOP_RE:-VKDEMO DONE|VKDEMO FAILED|oneshot vkprobe1|resident: stopping}"

echo "=== expanded QEMU args ==="
printf '  %s\n' "${COMMON[@]}"
echo "  (media) ${MEDIA[*]}"
echo "=== boot (VFIO stderr -> run-parity-qemu.log) ==="
# E-127: end the run a few seconds after the application has said its last word (the log is the result).
(
  for i in $(seq 1 340); do
    sleep 1
    if grep -aqE "$STOP_RE" /home/awe/bigbang/run-parity-serial.log 2>/dev/null; then
      sleep "${VK_LINGER:-12}"
      sudo -n pkill -TERM -f 'qemu-system-x86_6[4]'
      break
    fi
  done
) &
WATCHER=$!
sudo -n timeout 360 qemu-system-x86_64 "${COMMON[@]}" "${MEDIA[@]}" >/home/awe/bigbang/run-parity-qemu.log 2>&1 || true
echo "=== VFIO / qemu stderr (dma map errors?) ==="
grep -iE "vfio|dma|iommu|error|-22" /home/awe/bigbang/run-parity-qemu.log | head -10 || echo none
kill $WATCHER 2>/dev/null
