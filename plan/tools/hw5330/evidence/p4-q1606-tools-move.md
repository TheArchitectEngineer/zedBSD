# 5330 bare start: p4-q1606-tools-move

- taken: 2026-10-27 01:58:54 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/p4-q1606-tools-move-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD kei 1.0.0-beta2+g684a524 zedBSD 1.0.0-beta2+g684a524 (684a524 2026-10-26) x86_64
1793032580 SESSIOND HANDOFF go written=3 at_ms=12472
1793033849 SESSIOND SESSION start user=kei uid=1000 pid=7 runtime=/run/user/1000
1793033857 SESSIOND HANDOFF session ready=1 waited_ms=8496
1793033857 SESSIOND HANDOFF go written=3 at_ms=10991
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
i915: perf: 8981 ms: 4 presents (0.44/s), 10 submits, 6 batches | per submit ms: 3.40 (build 0.05, run 3.34, gpu 3.27) | per present ms: 7.63 (copy 1.76, publish 0.00, flip 2.04)
i915: perf: 10288 ms: 1 presents (0.09/s), 2 submits, 1 batches | per submit ms: 4.75 (build 0.06, run 4.69, gpu 4.68) | per present ms: 1.31 (copy 1.28, publish 0.00, flip 0.00)
```

## survey and the digest of the display writes

```
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x094a54fc set 0xc8e761c5
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
i915: survey 1 PIPE_SCANLINE=0x000001f4
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
i915: survey 2 PIPE_CRC_RES_1=0xd89c937b
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x0000045f
i915: survey: lit: panel on, backlight on, transcoder on, frames +237, live surface resident, crc read 0xd89c937b 0xd89c937b, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 26 14:54:13 sessiond[6]: automatic login of kei
<37>Oct 26 15:43:52 sessiond[6]: automatic login of kei
<37>Oct 26 16:57:29 sessiond[6]: automatic login of kei
```

## the previous start (kernel.log.old)

```
lines: 779, perf windows: 6
i915: w: 13594 writes in 588 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x657fa34d set 0x4ed76712
i915: survey: lit: panel on, backlight on, transcoder on, frames +240, live surface resident, crc read 0xa8a5986d 0xa8a5986d, underruns none
i915: perf: 6469 ms: 1 presents (0.15/s), 2 submits, 1 batches | per submit ms: 4.89 (build 0.09, run 4.79, gpu 4.79) | per present ms: 1.33 (copy 1.28, publish 0.00, flip 0.02)
ucsi: rec 154 R 20000000 1 0
i915: perf: markers 2: wait 0.00 ms, run 0.00 ms
i915: perf: 46043 ms: 1 presents (0.02/s), 2 submits, 1 batches | per submit ms: 5.66 (build 0.08, run 5.58, gpu 5.58) | per present ms: 1.31 (copy 1.27, publish 0.00, flip 0.01)
i915: vk: an object (kind 6) was destroyed before what names it: 0 views, 0 descriptor sets, 0 framebuffers and 1 command buffers let go of it
```
