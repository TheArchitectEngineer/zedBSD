# Latitude 5330 の Linux で採ったタッチパッドの情報（ws159-p001、2026-10-05）

採取: P1 generation17。5330（10.0.30.3、Debian 13、Linux 6.19.13）に SSH。ユーザーの許可「5330を起動しました。操作はすべて許可します。」（2026-10-05、Q1 の中継）。

| file | 中身 |
| --- | --- |
| `rdesc.txt` | `/sys/kernel/debug/hid/0018:06CB:CE65.0001/rdesc`（先頭の行が 665 byte の report descriptor、以下が Linux の解析） |
| `synaptics-06cb-ce65-rdesc.bin` | 上の先頭の行の binary（sha256 `c04a0ef6a551f8b1c330fdc3a1b02f1a589f68fba9343ec7c2a2698d003c90ac`）。host の試験の入力 |
| `input-devices.txt` | `/proc/bus/input/devices` |
| `evdev-absinfo.txt` | `../evdev-absinfo.py` の出力（Touchpad・Mouse・PS/2 の axis と key） |
| `rebind-dmesg.txt`・`dmesg-touchpad.txt` | `i2c_hid` の dynamic debug を on にして unbind→bind した時の log（HID descriptor、command の byte 列、feature の設定）と起動の時の log |
| `lpss-i2c-timing.txt` | clk_summary（133 MHz）と `i2c_designware` の HCNT・LCNT |
| `lspci-lpss-i2c.txt` | `lspci -vvnn -s 00:15.1`・`00:15.0` |
| `poll-probe-1.txt` | `../i2c-hid-poll-probe.py`（driver を外して input の register を読む）の結果 |
| `touch-pad-evdev.txt.gz`・`touch-ps2-evdev.txt`・`touch-run.log` | `../touch-record.sh 60` の結果（2026-10-05 01:17:30 から、ユーザーがタッチパッドを操作中、Q1 が実行）。Touchpad の evdev（`od -A d -t x1 -w24`、24 byte で 1 event、20070 event）と PS/2 の mouse（0 event）。host の試験の台本の素材 |
