<!-- awesome-plan project=zedbsd record=ws275-p001 -->

# ws275-p001 設計: sun4u の /dev/graphics と Xzed

- 親: [ws275](../ws.md)、Queue: q1303 / q1303-i01（P1、2026-10-19）
- Status: in-progress（改訂 2 を書いた、2026-10-19 P1 q1308。再レビューとユーザーの判断待ち）
- 成果: [design.md](../design.md)、HAL の案 [proposed/hal-sun4u-names.md](../proposed/hal-sun4u-names.md)（未適用）

## 受け入れの条件

1. /dev/graphics の契約と pcat の実装の要点、sun4u での実装の形（driver の置き場所、HAL との境界、HAL の API の変更の要否）、
   mode・framebuffer・MMU の属性、big endian、入力、userland の build、boot-test の判定、変える file、Phase と LW、判断を書く。
2. design-reviewer を通す。
3. ユーザーの判断（design.md §10 の D1〜D6 と HAL の承認）を得る。

## 行った事

- 読んだ物: ws.md、Guardrail（HAL の承認・受け渡し・出典・2026-10-19 の base の規則）、graphics.h、control-devices.md、pcat・pc98 の
  graphics、bsp-sun4u の framebuffer.c・console.c・boot.c、sparcv9 の space.c・irq.c、kern/platform/sun4u.c、sun4u-console.c、
  bootloader/sparcv9/handoff.h、xzed・libX11・zterm・zwm、top の Makefile の graphics の節、platform/sparcv9/vmunix.mk、boot-test.sh、
  pcat の ps2-8042.c。
- QEMU の source: v10.0.0 の `hw/sparc64/sun4u.c`・`hw/pci-host/sabre.c` を `build/tmp/ws275-docs/` に取得（URL・SHA-256 は design.md §9）。

## 途中の決定（2026-10-19、Q1 経由のユーザー）

- 「…OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」
- 訂正「あ、待って。firmware framebufferを使うけど、XVR-500などの既知のデバイスがあるなら、そっちを初期化することにします。」
- → design.md §4.2（firmware framebuffer の受け皿）と §4.2b（既知の card の driver の引き取り）、Phase p008〜p010、判断 D7・D8 を足した。

## 結論の要点

- hal.h の変更は要らない。HAL の名前付きの値（firmware framebuffer の 8、i8042 の 2）と boot.h の framebuffer の format の追加が要り、承認を求める。
- framebuffer の mmap は契約に無いので、user への写像は作らない。kernel は `hal_space_map_device` の IE（invert endian）・uncached の
  写像に `uint32_t` で書けば LE の framebuffer になる（BE の card は byte を逆にして書く）。8 bpp は loader が `color!` で RGB332 の palette、Xzed の PseudoColor は要らない。
- Xzed の RGB24 の BLIT は byte 列で endian に依らない。libX11 の `XChangeProperty` の format 16・32 は native の順で送る欠陥の候補。
- 入力は QEMU sun4u の ebus の i8042（keyboard と PS/2 mouse）を poll。
- p001〜p007 で 14 LW、card の driver は p008 以降に 4〜8 LW ずつ。

## 未確認

- IE の写像の 32 bit の store が VRAM に期待の byte で入るか（p003 の最初に gdb・monitor の `xp`）。
- i8042 が PCI の I/O の 0x60/0x64 に見えるか、OpenBIOS の tree の node（p002 で `info mtree`）。
- QEMU の i8042 の translate の既定（p004）。
- sparcv9 の static の X の program の build と root の大きさ（p005）。
- design-reviewer。

## 残り・再開の条件

- Q1 が design-reviewer を通し、ユーザーに D1〜D6 と HAL の承認を聞く。D7・D8 も。答えで design.md を直して p001 を cleared にする。
