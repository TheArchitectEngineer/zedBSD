# 5330 bare start: p1-ws299-p002-nvme

- taken: 2026-10-27 10:17:29 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/p1-ws299-p002-nvme-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD kei 1.0.0-beta2+g1ee7b7d zedBSD 1.0.0-beta2+g1ee7b7d (1ee7b7d 2026-10-27) x86_64
1793063676 SESSIOND HANDOFF go written=3 at_ms=920
1793063776 SESSIOND SESSION start user=kei uid=1000 pid=7 runtime=/run/user/1000
1793063776 SESSIOND HANDOFF session ready=1 waited_ms=438
1793063776 SESSIOND HANDOFF go written=3 at_ms=888
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
i915: perf: 31747 ms: 17 presents (0.53/s), 53 submits, 33 batches | per submit ms: 26.39 (build 0.29, run 26.09, gpu 2.88) | per present ms: 83.65 (copy 1.53, publish 0.00, flip 1.84)
i915: perf: 10379 ms: 1 presents (0.09/s), 2 submits, 1 batches | per submit ms: 4.69 (build 0.10, run 4.58, gpu 4.58) | per present ms: 1.40 (copy 1.35, publish 0.00, flip 0.01)
```

## survey and the digest of the display writes

```
i915: w: 13584 writes in 578 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0xa20d3996 set 0x426b2339
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
i915: survey 1 PIPE_SCANLINE=0x000002f3
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
i915: survey 2 PIPE_FRMCNT=0x000000a4
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0xf1637ca2
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x00000396
i915: survey: lit: panel on, backlight on, transcoder on, frames +149, live surface resident, crc read 0xf1637ca2 0xf1637ca2, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 26 23:25:46 sessiond[6]: automatic login of kei
<37>Oct 26 23:30:43 sessiond[6]: automatic login of kei
<37>Oct 26 23:56:56 sessiond[6]: automatic login of kei
<37>Oct 27 00:53:36 sessiond[6]: automatic login of kei
<37>Oct 27 01:06:09 sessiond[6]: automatic login of kei
<37>Oct 27 01:10:53 sessiond[6]: automatic login of kei
<37>Oct 27 01:12:57 sessiond[6]: automatic login of kei
<37>Oct 27 01:14:36 sessiond[6]: automatic login of kei
<37>Oct 27 01:16:16 sessiond[6]: automatic login of kei
```

## the previous start (kernel.log.old)

```
lines: 765, perf windows: 2
i915: w: 13587 writes in 581 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0xc4345a05 set 0x05ddc054
i915: survey: lit: panel on, backlight on, transcoder on, frames +149, live surface resident, crc read 0x23116eff 0x23116eff, underruns none
i915: survey: lit: panel on, backlight on, transcoder on, frames +149, live surface resident, crc read 0x23116eff 0x23116eff, underruns none
i915: perf: markers 46: wait 51.85 ms, run 0.01 ms
i915: perf: 22307 ms: 14 presents (0.62/s), 47 submits, 30 batches | per submit ms: 30.51 (build 0.36, run 30.15, gpu 4.62) | per present ms: 92.73 (copy 0.79, publish 0.00, flip 0.46)
i915: perf: markers 2: wait 0.00 ms, run 0.01 ms
i915: perf: 9458 ms: 1 presents (0.10/s), 2 submits, 1 batches | per submit ms: 6.98 (build 0.10, run 6.88, gpu 6.87) | per present ms: 0.77 (copy 0.74, publish 0.00, flip 0.00)
```
