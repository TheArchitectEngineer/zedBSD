# Reads the riscv64 kernel's hart records on QEMU's sifive_u through the
# gdbstub (WS256 decision #17).  sifive_u has two clusters: the E51 (hart 0,
# no MMU, parked in OpenSBI) is inferior 1 and the four U54 harts are
# inferior 2, which gdb only sees after attaching to it.  The records are
# read through physical memory, because a hart caught in the firmware (M
# mode) cannot translate the kernel's addresses.  The machine must give the
# UART a real backend (-serial file:/dev/null): with -serial null or none
# the firmware's UART transmit stalls and the kernel is never entered.
# Run as
#   gdb-multiarch -q -batch -ex 'target extended-remote 127.0.0.1:PORT' \
#       -x read.gdb build/riscv64-virt/vmunix
# Expect: cpu_count 4, every ready 1, every timer_ticks > 0 and growing
# between two reads; cpu0's hart_id is the boot hart OpenSBI picked (1..4).
set pagination off
set confirm off
add-inferior
inferior 2
attach 2
python gdb.execute("file " + gdb.inferiors()[0].progspace.filename)
info threads
printf "satp=%lx priv=%lu\n", $satp, $priv
maintenance packet Qqemu.PhyMemMode:1
set $delta = 0x80200000 - (unsigned long)&__kernel_text_start
printf "cpu_count=%d\n", *(int *)((unsigned long)&cpu_count + $delta)
set $i = 0
while $i < 4
  set $r = (unsigned long)&percpu_records + $delta + $i * 112
  printf "cpu%d hart_id=%lu ready=%d timer_ticks=%lu\n", $i, *(unsigned long *)($r + 32), *(int *)($r + 40), *(unsigned long *)($r + 88)
  set $i = $i + 1
end
maintenance packet Qqemu.PhyMemMode:0
detach
inferior 1
detach
