#!/bin/bash
cd /home/awe/linuxvm
sudo pkill -9 -f "qemu.*disk.img" 2>/dev/null
sleep 3
cp -f noble.img disk.img
qemu-img resize disk.img +6G >/dev/null 2>&1
rm -f dev-console.log
sudo timeout --kill-after=10s 5400s qemu-system-x86_64 \
  -machine q35,accel=kvm,memory-backend=mem -cpu host,host-phys-bits-limit=39 -m 4096 -smp 4 \
  -object memory-backend-memfd,id=mem,size=4G,share=on \
  -drive file=disk.img,format=qcow2,if=virtio -drive file=seed.iso,format=raw,if=virtio \
  -device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0 \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=n0 \
  -serial file:dev-console.log -display none >dev-qemu.log 2>&1
