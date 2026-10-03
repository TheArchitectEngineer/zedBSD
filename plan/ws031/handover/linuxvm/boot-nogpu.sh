#!/bin/bash
# Boot the reference Ubuntu guest WITHOUT the GPU (no vfio passthrough, no GPU contention),
# just to gather source-package identity and fetch the exact Ubuntu kernel source.
cd /home/awe/linuxvm
sudo pkill -9 -f "qemu.*disk.img" 2>/dev/null
sleep 3
cp -f noble.img disk.img
qemu-img resize disk.img +6G >/dev/null 2>&1
rm -f nogpu-console.log nogpu-qemu.log
sudo timeout --kill-after=10s 3600s qemu-system-x86_64 \
  -machine q35,accel=kvm,memory-backend=mem -cpu host,host-phys-bits-limit=39 -m 4096 -smp 4 \
  -object memory-backend-memfd,id=mem,size=4G,share=on \
  -drive file=disk.img,format=qcow2,if=virtio -drive file=seed.iso,format=raw,if=virtio \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=n0 \
  -serial file:nogpu-console.log -display none >nogpu-qemu.log 2>&1 &
echo "nogpu qemu launched pid $!"
