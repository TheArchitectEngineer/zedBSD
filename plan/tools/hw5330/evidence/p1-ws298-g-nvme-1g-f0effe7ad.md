# 5330 bare start: p1-ws298-g-nvme-1g-f0effe7ad

- taken: 2026-10-27 08:18:30 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/p1-ws298-g-nvme-1g-f0effe7ad-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD kei 1.0.0-beta2+gf97c914 zedBSD 1.0.0-beta2+gf97c914 (f97c914 2026-10-27) x86_64
1793056494 SESSIOND HANDOFF go written=3 at_ms=9542
1793056632 SESSIOND SESSION start user=kei uid=1000 pid=7 runtime=/run/user/1000
1793056641 SESSIOND HANDOFF session ready=1 waited_ms=8549
1793056641 SESSIOND HANDOFF go written=3 at_ms=11096
```

## backlight (read, set 100, read)

```
BACKLIGHT device=/dev/backlight/backlight0 name=i915 type=0
BACKLIGHT brightness=100 levels=0
BACKLIGHT device=/dev/backlight/backlight0 name=i915 type=0
BACKLIGHT set=100
BACKLIGHT brightness=100 levels=0
BACKLIGHT device=/dev/backlight/backlight0 name=i915 type=0
BACKLIGHT brightness=100 levels=0
```

## i915 lines

```
i915 lines: 369
lease 1 claimed: 1
picture up: 1
device lost: 0
error or warn lines: 1
i915: perf: 26827 ms: 9 presents (0.33/s), 28 submits, 19 batches | per submit ms: 4.70 (build 0.05, run 4.65, gpu 2.81) | per present ms: 11.50 (copy 1.55, publish 0.00, flip 2.08)
i915: perf: 6149 ms: 1 presents (0.16/s), 2 submits, 1 batches | per submit ms: 4.29 (build 0.06, run 4.22, gpu 4.22) | per present ms: 1.36 (copy 1.33, publish 0.00, flip 0.00)
```

## survey and the digest of the display writes

```
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0xf5b3dee8 set 0xc8e761c5
i915: survey 1 pipe A (shows a resident buffer, powered), live surface buffer A, underruns 0
i915: survey 1 PWR_WELL_CTL=0x00000c03
i915: survey 1 DC_STATE_EN=0x00000000
i915: survey 1 DPLL0_ENABLE=0xcc000000
i915: survey 1 DPLL0_CFGCR0=0x00e001a5
i915: survey 1 DPLL0_CFGCR1=0x00000088
i915: survey 1 DPCLKA_CFGCR0=0x01e07800
i915: survey 1 DDI_BUF_CTL_A=0x80000002
i915: survey 1 MBUS_CTL=0xdc000700
i915: survey 1 DBUF_CTL_S1=0xc043c000
i915: survey 1 DBUF_CTL_S2=0xc043c000
i915: survey 1 DBUF_CTL_S3=0xc043c000
i915: survey 1 DBUF_CTL_S4=0xc043c000
i915: survey 1 PP_STATUS=0x80000008
i915: survey 1 PP_CONTROL=0x0000006f
i915: survey 1 SBLC_PWM_CTL=0x80000000
i915: survey 1 SBLC_PWM_FREQ=0x00017700
i915: survey 1 SBLC_PWM_DUTY=0x00017700
i915: survey 1 TRANS_CONF=0xc0000000
i915: survey 1 TRANS_DDI_FUNC_CTL=0x8a210002
i915: survey 1 PIPE_SRCSZ=0x077f0437
i915: survey 1 PLANE_CTL=0x94000000
i915: survey 1 PLANE_SURF=0xbc0c0000
i915: survey 1 PLANE_SURFLIVE=0xbc0c0000
i915: survey 1 PIPE_FRMCNT=0x0000000f
i915: survey 1 PIPE_CRC_CTL=0x00000000
i915: survey 1 PIPE_CRC_RES_1=0x00000000
i915: survey 1 PLANE_WM_0=0x80004010
i915: survey 1 PLANE_BUF_CFG=0x0fdb0000
i915: survey 1 WM_LINETIME=0x00000000
i915: survey 1 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 1 PIPE_SCANLINE=0x000001c1
i915: survey 2 pipe A (shows a resident buffer, powered), live surface buffer B, underruns 0
i915: survey 2 PWR_WELL_CTL=0x00000c03
i915: survey 2 DC_STATE_EN=0x00000000
i915: survey 2 DPLL0_ENABLE=0xcc000000
i915: survey 2 DPLL0_CFGCR0=0x00e001a5
i915: survey 2 DPLL0_CFGCR1=0x00000088
i915: survey 2 DPCLKA_CFGCR0=0x01e07800
i915: survey 2 DDI_BUF_CTL_A=0x80000002
i915: survey 2 MBUS_CTL=0xdc000700
i915: survey 2 DBUF_CTL_S1=0xc043c000
i915: survey 2 DBUF_CTL_S2=0xc043c000
i915: survey 2 DBUF_CTL_S3=0xc043c000
i915: survey 2 DBUF_CTL_S4=0xc043c000
i915: survey 2 PP_STATUS=0x80000008
i915: survey 2 PP_CONTROL=0x00000067
i915: survey 2 SBLC_PWM_CTL=0x80000000
i915: survey 2 SBLC_PWM_FREQ=0x00017700
i915: survey 2 SBLC_PWM_DUTY=0x00017700
i915: survey 2 TRANS_CONF=0xc0000000
i915: survey 2 TRANS_DDI_FUNC_CTL=0x8a210002
i915: survey 2 PIPE_SRCSZ=0x077f0437
i915: survey 2 PLANE_CTL=0x94000000
i915: survey 2 PLANE_SURF=0xbca00000
i915: survey 2 PLANE_SURFLIVE=0xbca00000
i915: survey 2 PIPE_FRMCNT=0x000000fc
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0x3f255d13
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x000001c0
i915: survey: lit: panel on, backlight on, transcoder on, frames +237, live surface resident, crc read 0x3f255d13 0x3f255d13, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 26 22:55:44 sessiond[6]: automatic login of kei
<37>Oct 26 23:01:17 sessiond[6]: automatic login of kei
<37>Oct 26 23:11:13 sessiond[6]: automatic login of kei
<37>Oct 26 23:14:47 sessiond[6]: automatic login of kei
<37>Oct 26 23:17:12 sessiond[6]: automatic login of kei
```

## the previous start (kernel.log.old)

```
lines: 789, perf windows: 5
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x2a08096e set 0x5aa16d9f
i915: survey: lit: panel on, backlight on, transcoder on, frames +143, live surface resident, crc read 0x3751fc39 0x3751fc39, underruns none
i915: perf: markers 2: wait 0.01 ms, run 0.00 ms
i915: perf: 32370 ms: 1 presents (0.03/s), 2 submits, 1 batches | per submit ms: 4.84 (build 0.07, run 4.76, gpu 4.76) | per present ms: 1.65 (copy 1.59, publish 0.00, flip 0.02)
i915: perf: markers 2: wait 0.00 ms, run 0.01 ms
i915: perf: 5311 ms: 1 presents (0.18/s), 2 submits, 1 batches | per submit ms: 5.06 (build 0.08, run 4.97, gpu 4.96) | per present ms: 1.53 (copy 1.49, publish 0.00, flip 0.01)
i915: vk: an object (kind 6) was destroyed before what names it: 0 views, 0 descriptor sets, 0 framebuffers and 1 command buffers let go of it
```
