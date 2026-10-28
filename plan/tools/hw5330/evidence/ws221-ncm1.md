# 5330 bare start: ws221-ncm1

- taken: 2026-10-29 06:04:22 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/ws221-ncm1-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD zedbsd 1.0.0-beta2+g8aa2420 zedBSD 1.0.0-beta2+g8aa2420 (8aa2420 2026-10-29) x86_64
1793221390 SESSIOND SESSION start user=user uid=1000 pid=7 runtime=/run/user/1000
1793221390 SESSIOND HANDOFF session ready=1 waited_ms=359
1793221390 SESSIOND HANDOFF go written=3 at_ms=1841
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
i915 lines: 366
lease 1 claimed: 1
picture up: 1
device lost: 0
error or warn lines: 1
i915: perf: 31677 ms: 13 presents (0.41/s), 44 submits, 28 batches | per submit ms: 32.98 (build 0.29, run 32.69, gpu 4.24) | per present ms: 111.36 (copy 1.69, publish 0.00, flip 2.22)
i915: perf: 17275 ms: 1 presents (0.05/s), 2 submits, 1 batches | per submit ms: 7.28 (build 0.07, run 7.21, gpu 7.20) | per present ms: 0.72 (copy 0.70, publish 0.00, flip 0.00)
```

## survey and the digest of the display writes

```
i915: w: 13584 writes in 578 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x54c68db5 set 0x36a11cac
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
i915: survey 1 PIPE_SCANLINE=0x00000361
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
i915: survey 2 PIPE_FRMCNT=0x00000070
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0x70f4303a
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x0000027e
i915: survey: lit: panel on, backlight on, transcoder on, frames +97, live surface resident, crc read 0x70f4303a 0x70f4303a, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
KWL THREADS count=8 names=io-file,io-device,font,bluetoothd,phone,volumed,networkd,audiod
```

## the previous start (kernel.log.old)

```
lines: 0, perf windows: 0
```
