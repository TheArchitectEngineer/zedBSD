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

## T1-608 (SV7 Gen9.5, main bfebc43b7, out build/ws229-p005/hw-sv7): ktest PASS (390 checks, 6 skipped), eu PASS, vkcs 21/21, draw PASS, r1 PASS; vkx 2/9, vke1 5/6, vke2 20/21, vkc 8/9 FAIL.
## T1-612 (SV7 retest, main c28a9ff09, out build/ws229-p005/hw-sv7-r2): vkx 9/9, vke1 6/6, vke2 21/21, vkc 9/9 PASS.
## T1-610 (AHCI retest, main f41c0c12b): polling 0, wr_bytes 33554432 (sata0 and nvme), partial sector OK, sata0 flush +1 at close, +2 at poweroff PASS; NVMe flush_operations 0 at poweroff (FAIL). Artifacts build/review/t1-610/.
## T1-611 (Settings rotation R1-R6, main d9e8506): PASS; Settings died once on the second 90 (ZSETTINGS FAILED kl_window_present_parts result=-4). build/review/t1-611/.
## T1-613 (BUG-306 retest, main 93b88c49f): PASS. build/review/t1-613/.
## T1-614 (BUG-306 retest, ws217-p004 rotation speed and stale picture, main f66e92abe tree): all criteria PASS; two observations. QEMU Venus (KVM), AAT image.
Image: build/t1-614-src (git archive main f66e92abe + main sysroot copy, llvm symlinks), `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh build/ws217-aat` (~25 min incl. clang package); guest via plan/ws035/tests/zdesktop-guest.sh start, GUEST_RUNTIME=build/t1-614-run. Auto-login session; skipped Settings welcome (click Skip), opened `settings display` as kei.
Artifacts: /home/awe/zedBSD-worktrees/t1/build/review/t1-614/ (session.log copied before any logout, key-lines.txt, slow-pass.txt, displays-after-r3.txt, displays-final.txt, r<n>-1s/3s/late.png, lines-r<n>.txt). Steps: r1 90, r2 0, (r3 = a mis-click at the old position: no rotation, ignore), r3b 90, r4 270, r5 0.
Rotations (ms=, times): 
 1 0>90 ms=1938 prepare=764 glass=599 swap=1133 caches=694 save_ms=1; saved by=writer errno=0 ms=19; hold windows=1; window x=12 y=108 w=1180 h=680 moved=1; shown held_ms=1010 late=1
 2 90>0 ms=1881 prepare=594 swap=1244 save_ms=1; saved errno=0 ms=20; window x=50 y=108 w=776 h=800 moved=0; shown held_ms=1055 late=1
 3b 0>90 ms=1825 prepare=655 swap=1130 save_ms=0; saved errno=0 ms=15; window x=12 w=1180 moved=1; shown held_ms=1008 late=1
 4 90>270 (same size) ms=56 save_ms=1; saved errno=0 ms=26; no hold
 5 270>0 ms=1868 prepare=587 swap=1240 save_ms=0; saved errno=0 ms=1906 (writer thread, fsync; not on the main thread); drawn surface=19 client=4 after_ms=186; shown held_ms=186 late=0
Criteria: (1) 4 size-changing ms= 1938/1881/1825/1868 <= 2000, save_ms <= 1: PASS. (2) KWL DISPLAYS saved by=writer errno=0 after every turn: PASS. (4) late=0 only in rotation 5; rotations 1,2,3b late=1 with held_ms 1008-1055 (no drawn line: the client's new commit did not arrive within 1000 ms): recorded as allowed (PASS). (5) Settings mapped (client 4, surface 19) after all; FAILED lines 0 (PRESENT_FAILED, ZSETTINGS FAILED, KWL FAILED: none). (6) displays.conf after the first 90: rotate=Venus virtual display 0 90; final after 0: no rotate= line: PASS.
(3) PNG: no clipped window at 3 s in r1/3b/4/5. Observations: (a) the 1 s PNG failed with KEILAND-SHOT no-active-compositor in r1, r2, r3b, r5 (the turn's idle/swap holds the compositor; shot returned at ~1.4 s); r4's 1 s PNG exists. 3 s PNGs started at 3.0 s and finished at ~4.4 s (capture takes ~1.4 s). (b) At 3 s after r1 and r3b (to 90) the Settings picture is the old 1180x680 frame rotated, drawn at x=12 with the right part cut off (sidebar and page overlap, Rotation card incomplete: r1-3s.png, r3b-3s.png); it settles by ~8 s (r1-late.png, r3b-late.png). In r2 (to 0) the 3 s PNG shows the narrow 776x800 window (r2-3s.png) and by ~8 s the wide 1180 window. So the stale/odd frame is still visible at 3 s for two rotations (late=1); judged by the criterion text I mark (3) as PASS only for "not clipped off-screen"; Q1 to decide whether the layout shown at 3 s is acceptable.
SLOW_PASS > 1 s: 5 lines (slow-pass.txt): 1344000 at_ms=17615 (schedule_us), and 1.83-1.94 s with clients_us (at_ms 55712, 85751, 118942, 149392: roughly every 30 s, clients_us ~1.9 s each, not tied to a rotation).
Exe times: build ~25 min, guest test ~5 min. QEMU stopped.
