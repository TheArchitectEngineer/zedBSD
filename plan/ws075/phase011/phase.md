<!-- awesome-plan project=zedbsd record=ws075p011 -->

# ws075-p011: HDMI の主出力の実機の事前調査（H1）

Phase ID: `ws075-p011`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。EDID は読める（1920x1280）、pipe B・DVI で 3 つの mode を実機で出力、touch の USB は現れない）
Phase disposition: normal
承認: 2026-09-28 main の依頼（[hdmi-main-output.md](../hdmi-main-output.md) の H1、ユーザーの回答 6: H1 で 5330 を占有してよい）。

## 範囲

[hdmi-main-output.md](../hdmi-main-output.md) の H1。driver のコードは変えない。10 インチの touch LCD を 5330 の HDMI（DDI B）と USB に
つないだまま、既存の HDMI の場面（`tests/display/hdmi-output.c` の HDMI-B、`hdmi-hotplug.c` の HDMI-EDID）を走らせ、
EDID、点く mode、DVI、HDMI の前後の USB の device を記録する。

## 手順と道具

- `plan/ws075/tests/hdmi-h1-hw.sh SCENARIO OUTDIR [FLAGS]`（新規）: `/tmp/i915-hw.lock` の下で `vkloop-hw.sh test SCENARIO` を走らせ、
  同時に 5330 の Linux で USB の device（sysfs）を 1 秒ごとに記録する（guest の serial log の最新の HDMI の行と並べる）。run の前に
  無かった device が現れたら、その場で `lsusb -v`、`/sys/bus/hid/devices/*/report_descriptor`、`usbhid-dump -e descriptor` を取る。
  guest（QEMU）は物理の USB controller を passthrough していないので、touch の HID が見えるのは 5330 の host の側である。
- 試験の場面の変更（driver ではない）: `hdmi-output.c` の HDMI-B に build の switch を足した。`-DI915_TEST_HDMIB_CEA16`（CEA 16、
  1920x1080@60、148.5 MHz）、`-DI915_TEST_HDMIB_NATIVE`（この LCD の EDID の DTD1、1920x1280@60、164.36 MHz、+h −v）。既定は
  従来どおり CEA 4（1280x720）。mode を log に出す。window は `-DI915_TEST_HDMIB_WINDOW_MS=60000`。
- build: `BUILD=build/h1`（worktree の中）、`plan/ws075/tests/config-test-hw.mk`。

## 結果（実機、2026-09-28、5330 の VFIO passthrough の QEMU の guest。QEMU の emulation の証拠ではない）

| run | 証拠 | 結果 |
| --- | --- | --- |
| HDMI-EDID（port は消灯） | `build/ws075-h1/hdmi-edid/` | **PASS**。SDEISR の B live 1、connected、GMBUS pin 2 で 2 block（base + CEA-861）を 1 回で読めた（fails 0）。E-123 の環境（別の monitor）の NAK と違い、この LCD は port を点ける前に DDC に答える |
| HDMI-B 1280x720@60（CEA 4）、DVI | `build/ws075-h1/hdmib-720/` | **PASS**。pipe B の frame counter 2→12→3667（60 s、約 61 frame/s）、underrun なし、stop の後 TRANSCONF 0。点灯中の EDID の再読も同じ内容 |
| HDMI-B 1920x1080@60（CEA 16）、DVI | `build/ws075-h1/hdmib-1080/` | **PASS**（同じ判定。WRPLL cfgcr0=0x001001d0 cfgcr1=0x00000e84） |
| HDMI-B 1920x1280@60（EDID の DTD1、164.36 MHz）、DVI | `build/ws075-h1/hdmib-native/` | **PASS**（同じ判定。WRPLL cfgcr0=0x00a00201 cfgcr1=0x00000e84） |

### EDID（[lcd-edid.hex](lcd-edid.hex)、256 byte、checksum 0/0）

- 製造者 `JTG`、product 0x1230、名前 `S123`、serial 文字列 `NA`、EDID 1.3、digital、259x173 mm。
- **native（DTD1）は 1920x1280**（3:2）@59.999 Hz、164.36 MHz、h 1968/2000/2080、v 1283/1293/1317、hsync +・vsync −。
  ユーザーの見込み（1920x1080）と違う。1920x1080 は CEA の拡張の DTD（148.5 MHz、+h +v）と VIC 1〜4・16 等にある。
- range limits: 縦 48〜60 Hz、横 30〜180 kHz、最大 600 MHz。CEA の拡張に HDMI の VSDB（00-0c-03）と basic audio → HDMI sink。
  DVI mode（infoframe なし）で出している。

### 画面を見ること（未実施）

画面に絵が出たかは tool で確かめられない。5330 の内蔵 camera は利用者の側を向き、capture の build（`I915_TEST_CAPTURE`）は
display の hardware を使わない。確かめたのは driver 側の事実だけ（pipe B が timing を出し続けた、underrun なし、sink が connected で
点灯中も EDID に答える）。**LCD に 3 つの mode の絵が出たかは目視・写真が要る（未実施）。** DVI mode を LCD が受けるかも同じ。

### USB（touch・pen の HID）

- 5330 の host の USB は run の前・中・後で同じ 4 device（webcam 0c45:6d1d、指紋 0a5c:5842、Bluetooth 8087:0033、LAN 0bda:8156）。
  `usb-poll.log` は 1 秒ごとに 196 回（hdmib-720）、HDMI の点灯中（t=0〜60 s の window）も新しい device は無い。
- dmesg に boot 以降の USB の接続・descriptor の読み取りの失敗・over-current は無い。Type-C の port1 の partner は充電器
  （power role sink、alternate mode 0）。
- **touch の LCD の USB は 5330 に列挙されていない**（HDMI の有無によらない）。report descriptor は取れなかった。考えられる理由
  （未確認）: USB の cable が給電だけ、LCD の USB が別の口（電源の口）にささっている、LCD 側の電源が入っていない。
  cable と口の確認をユーザーに依頼する（WS079 の入力の前提）。

## 判断が要る点（ユーザーへ）

1. LCD の native は 1920x1280。H2 の既定の mode は EDID の DTD1（1920x1280）にする（EDID が読めるので）。1920x1080 を望むなら
   `display.mode=1920x1080` で与える。
2. touch の USB が見えない: cable と口の確認。

## 残り

- H2（ws075-p012）: `display=hdmi|auto` と `display.mode=`、resident の HDMI 出力。
- 目視・写真（ユーザー）。touch の USB（cable の確認の後、再び `hdmi-h1-hw.sh` で記録）。
