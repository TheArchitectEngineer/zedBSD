# T1 results 2026-10-14 (generation 2): T1-606, T1-607, T1-609 (T1-608 below when done)

## T1-609 (ws231-p001 AHCI, main a25b73a3f tree, QEMU q35 ich9-ahci): FAIL (3 criteria), rest PASS
Image: /home/awe/zedBSD-worktrees/t1/build/t1-609-src/build/ws231-ahci/hdd-image.img (git archive main + copied main sysroot;
config plan/ws231/tests/config-amd64-ahci.mk). Artifacts: /home/awe/zedBSD-worktrees/t1/build/review/t1-609/.
- 1 read/write dd+cmp: cmp matched; attach/disk lines present. FAIL the polling criterion: "completion found by polling" up to (2048) after one 32 MiB dd/cmp (lines present since boot).
- 2 (a) flush: QMP flush_operations stays 0 after guest sync (1 after suspend); not increased at poweroff (last read 1). FAIL. wr_bytes 256 MiB after a 32 MiB dd (8-40x over-count).
- 3 partition: sda1, sda2 PASS. 4 error recovery (blkdebug raw, read EIO at one sector, write EIO at one sector, once): dd fails, next succeeds, "device error" lines, quarantined=0: PASS.
- 5 (c) throttle 2 MiB/s: 8 MiB write+read 33 s, cmp matched, no timeout; polling already FAIL.
- 6 (i) no-disk q35 boots (ahci 2 log lines); boot time ahci 14.58/14.63/14.63 s vs noahci 14.63/14.62/14.59 s (SSH ready, 2 s poll): no difference. (ii) second HBA, 3 disks, dd/cmp all match PASS.
- 7 S0 idle: sleep result=0 resume=0, ahci0 suspended/resumed, dd/cmp after resume OK PASS.
- 8 root on AHCI (guest.py copy with --disk sata): "/dev/sda2 on / type ufs", PNG sata-root.png PASS. 9 boot-test uefi-nvme and uefi-usb PASS (review/t1-609/{uefi-nvme,uefi-usb}/login.png).

## T1-607 (ws232-p001 pc98): PASS
pc98-boot.py with ~/qemu-pc98 qemu-system-i386, image /home/awe/zedBSD-worktrees/p1/build/pc98/hdd-image.img: 38 s, login: seen, root login,
uname -a = zedBSD ... i386. PNG from text VRAM: /home/awe/zedBSD-worktrees/t1/build/review/t1-607/{login,command}.png. screendump PNG not taken.

## T1-606 (ws229 Gen12 regression, 5330, main ~a25b73a3f): PASS
OUT build/ws229-p005/hw-r2 (/home/awe/zedBSD-worktrees/t1/build/ws229-p005/hw-r2/hw-*-ws229/run.log), run-hw2.out.
ktest: "i915: test ktest: PASS" (391 checks, 0 failures, 12 skipped). eu: "i915: test eu: PASS", "EU-TEST PASS", EU-REPEAT 5/5.
vkcs 21/21, vkx 9/9, vke1 6/6, vke2 21/21, vkc 9/9 PASS. No baseline batch_hash/EU-TEST fixture line in any log or earlier run: recorded as "no baseline".
Observation (not analysed): ktest run.log has one "i915: Unexpected fault addr=0x0 space=PPGTT engine_id=3" line and "init: oneshot vkprobe1 failed" at the end (all scenarios).
