# 5330 bare start: p1-q1612-nvme-image-f0effe7ad

- taken: 2026-10-27 08:26:59 JST from centris, machine 10.0.30.3
- whole kernel log: build/hw5330/logs/p1-q1612-nvme-image-f0effe7ad-dmesg.txt (not in git)

## login, session, dropped log bytes

```
kern.boot.login: graphical
kern.msgbuf_dropped: 0
zedBSD kei 1.0.0-beta2+g1ee7b7d zedBSD 1.0.0-beta2+g1ee7b7d (1ee7b7d 2026-10-27) x86_64
1793057146 SESSIOND SESSION start user=kei uid=1000 pid=7 runtime=/run/user/1000
1793057146 SESSIOND HANDOFF session ready=1 waited_ms=338
1793057146 SESSIOND HANDOFF go written=3 at_ms=814
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
i915 lines: 382
lease 1 claimed: 1
picture up: 1
device lost: 0
error or warn lines: 1
i915: perf: 8283 ms: 251 presents (30.30/s), 503 submits, 253 batches | per submit ms: 4.55 (build 0.07, run 4.47, gpu 4.47) | per present ms: 16.56 (copy 1.95, publish 0.00, flip 7.12)
i915: perf: 18410 ms: 1 presents (0.05/s), 2 submits, 1 batches | per submit ms: 4.85 (build 0.06, run 4.78, gpu 4.78) | per present ms: 1.46 (copy 1.42, publish 0.00, flip 0.01)
```

## survey and the digest of the display writes

```
i915: w: 13584 writes in 578 entries (5 runs), 199 interrupt-register writes counted only, 0 dropped; order 0xc48b02ab set 0x36a11cac
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
i915: survey 1 PIPE_SCANLINE=0x00000181
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
i915: survey 2 PP_CONTROL=0x0000006f
i915: survey 2 SBLC_PWM_CTL=0x80000000
i915: survey 2 SBLC_PWM_FREQ=0x00017700
i915: survey 2 SBLC_PWM_DUTY=0x00017700
i915: survey 2 TRANS_CONF=0xc0000000
i915: survey 2 TRANS_DDI_FUNC_CTL=0x8a210002
i915: survey 2 PIPE_SRCSZ=0x077f0437
i915: survey 2 PLANE_CTL=0x94000000
i915: survey 2 PLANE_SURF=0xbca00000
i915: survey 2 PLANE_SURFLIVE=0xbca00000
i915: survey 2 PIPE_FRMCNT=0x0000004d
i915: survey 2 PIPE_CRC_CTL=0x83000000
i915: survey 2 PIPE_CRC_RES_1=0x3304a935
i915: survey 2 PLANE_WM_0=0x80004010
i915: survey 2 PLANE_BUF_CFG=0x0fdb0000
i915: survey 2 WM_LINETIME=0x00000000
i915: survey 2 PIPE_MBUS_DBOX_CTL=0xa9038806
i915: survey 2 PIPE_SCANLINE=0x0000014d
i915: survey: lit: panel on, backlight on, transcoder on, frames +62, live surface resident, crc read 0x3304a935 0x02ef38ea, underruns none
```

## the screen later: the compositor and the session (messages, greeter.log, session.log)

```
<37>Oct 26 23:25:46 sessiond[6]: automatic login of kei
```

## the previous start (kernel.log.old)

```
lines: 0, perf windows: 0
```
