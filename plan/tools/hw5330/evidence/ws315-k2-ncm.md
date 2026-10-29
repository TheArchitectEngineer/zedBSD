# 5330 bare start: ws315-k2-ncm

- taken: 2026-10-30 04:55:26 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/ws315-k2-ncm-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD zedbsd 1.0.0-beta2+gfe31561 zedBSD 1.0.0-beta2+gfe31561 (fe31561 2026-10-30) x86_64
1793300613 SESSIOND HANDOFF go written=3 at_ms=2369
1793303653 SESSIOND SESSION start user=user uid=1000 pid=7 runtime=/run/user/1000
1793303654 SESSIOND HANDOFF session ready=1 waited_ms=516
1793303654 SESSIOND HANDOFF go written=3 at_ms=2322
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
i915: perf: 31721 ms: 14 presents (0.44/s), 45 submits, 27 batches | per submit ms: 6.18 (build 0.32, run 5.85, gpu 4.35) | per present ms: 98.33 (copy 2.08, publish 0.00, flip 0.89)
i915: perf: 13310 ms: 1 presents (0.07/s), 2 submits, 1 batches | per submit ms: 6.87 (build 0.09, run 6.77, gpu 6.77) | per present ms: 0.74 (copy 0.71, publish 0.00, flip 0.00)
```

## survey and the digest of the display writes

```
i915: w: 13592 writes in 586 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x892e22d1 set 0x02767e94
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
i915: survey 1 PIPE_SCANLINE=0x000002ee
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
i915: survey 2 PIPE_FRMCNT=0x00000064
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0x4730151c
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x00000381
i915: survey: lit: panel on, backlight on, transcoder on, frames +85, live surface resident, crc read 0x4730151c 0x4730151c, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 29 17:03:50 sessiond[6]: automatic login of user
<37>Oct 29 17:07:02 sessiond[6]: automatic login of user
<37>Oct 29 18:11:42 sessiond[6]: automatic login of user
<37>Oct 29 18:15:39 sessiond[6]: automatic login of user
<37>Oct 29 19:03:32 sessiond[6]: automatic login of user
<37>Oct 29 19:33:33 sessiond[6]: sleep asked from user
<37>Oct 29 19:33:33 sessiond[6]: sleep: NOSLEEP network reason=radio radio=- error=EINVAL
KWL THREADS count=9 names=io-file,io-device,font,blur,bluetoothd,phone,volumed,networkd,audiod
```

## the previous start (kernel.log.old)

```
lines: 952, perf windows: 52
i915: w: 13584 writes in 578 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0x4b2b67ac set 0xb0b1175f
i915: survey: lit: panel on, backlight on, transcoder on, frames +296, live surface resident, crc read 0x6aed72d3 0x6aed72d3, underruns none
i915: resident shim: ring rewound (idle context)
i915: perf: markers 2: wait 0.00 ms, run 0.00 ms
i915: perf: 60000 ms: 1 presents (0.01/s), 2 submits, 1 batches | per submit ms: 4.04 (build 0.01, run 4.03, gpu 4.02) | per present ms: 1.83 (copy 1.81, publish 0.00, flip 0.00)
ucsi: rec 210 R 20000000 1 0
ACPI: EC query _Q66 #2
```
