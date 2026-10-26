# 5330 bare start: p1-ws298-d-full-f0effe7ad

- taken: 2026-10-27 08:09:39 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/p1-ws298-d-full-f0effe7ad-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD kei 1.0.0-beta2+gf97c914 zedBSD 1.0.0-beta2+gf97c914 (f97c914 2026-10-27) x86_64
1793055955 SESSIOND HANDOFF go written=3 at_ms=10843
1793056092 SESSIOND SESSION start user=kei uid=1000 pid=7 runtime=/run/user/1000
1793056100 SESSIOND HANDOFF session ready=1 waited_ms=8398
1793056100 SESSIOND HANDOFF go written=3 at_ms=10909
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
i915 lines: 371
lease 1 claimed: 1
picture up: 1
device lost: 0
error or warn lines: 1
i915: perf: 19993 ms: 4 presents (0.20/s), 10 submits, 6 batches | per submit ms: 3.26 (build 0.06, run 3.20, gpu 3.11) | per present ms: 3.57 (copy 1.42, publish 0.00, flip 0.85)
i915: perf: 6555 ms: 1 presents (0.15/s), 2 submits, 1 batches | per submit ms: 4.60 (build 0.08, run 4.52, gpu 4.52) | per present ms: 1.53 (copy 1.51, publish 0.00, flip 0.00)
```

## survey and the digest of the display writes

```
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x2a08096e set 0x5aa16d9f
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
i915: survey 1 PIPE_SCANLINE=0x00000276
i915: survey 2 pipe A (shows a resident buffer, powered), live surface buffer A, underruns 0
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
i915: survey 2 PP_CONTROL=0x0000006f
i915: survey 2 SBLC_PWM_CTL=0x80000000
i915: survey 2 SBLC_PWM_FREQ=0x00017700
i915: survey 2 SBLC_PWM_DUTY=0x00017700
i915: survey 2 TRANS_CONF=0xc0000000
i915: survey 2 TRANS_DDI_FUNC_CTL=0x8a210002
i915: survey 2 PIPE_SRCSZ=0x077f0437
i915: survey 2 PLANE_CTL=0x94000000
i915: survey 2 PLANE_SURF=0xbc0c0000
i915: survey 2 PLANE_SURFLIVE=0xbc0c0000
i915: survey 2 PIPE_FRMCNT=0x0000009b
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0x5d5be9d9
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x00000162
i915: survey: lit: panel on, backlight on, transcoder on, frames +140, live surface resident, crc read 0x5d5be9d9 0x5d5be9d9, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 26 22:55:44 sessiond[6]: automatic login of kei
<37>Oct 26 23:01:17 sessiond[6]: automatic login of kei
```

## the previous start (kernel.log.old)

```
lines: 785, perf windows: 5
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x9c717c71 set 0x4ed76712
i915: survey: lit: panel on, backlight on, transcoder on, frames +139, live surface resident, crc read 0x185b8dfa 0x185b8dfa, underruns none
i915: perf: markers 2: wait 0.00 ms, run 0.00 ms
i915: perf: 29669 ms: 1 presents (0.03/s), 2 submits, 1 batches | per submit ms: 5.18 (build 0.09, run 5.08, gpu 5.08) | per present ms: 1.33 (copy 1.29, publish 0.00, flip 0.01)
i915: perf: markers 2: wait 0.00 ms, run 0.00 ms
i915: perf: 16612 ms: 1 presents (0.06/s), 2 submits, 1 batches | per submit ms: 5.70 (build 0.11, run 5.58, gpu 5.58) | per present ms: 1.32 (copy 1.28, publish 0.00, flip 0.01)
i915: vk: an object (kind 6) was destroyed before what names it: 0 views, 0 descriptor sets, 0 framebuffers and 1 command buffers let go of it
```
