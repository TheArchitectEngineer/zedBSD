<!-- awesome-plan project=zedbsd record=ws279-p006 -->

# ws279-p006 HVS6 の display（引き継ぎ・UPM・plane・PV の vblank・flip・zero copy）

- 親: [ws279](../ws.md)、担当: P2（2026-10-22、worktree `agent/p2-pi5gpu`）
- Status: in-progress（段 P0〜R3 は実機 PASS、`/dev/gpu0` の display device が出た。Keiland からの lease・cursor・zero copy の実機の確認と scaled overlay が残り）
- 依頼の元: 2026-10-22 Q1「続けて p006（HVS6 の display: 引き継ぎ・plane・vblank・flip・zero copy）と p005（描画の経路）を進めて」。同じ安全策（switch、sysctl での開始、vmunix.good、3 分）。

## 目的と受け入れの条件（design §4）

1. P0/P1: HVS6 の版（C0/D0）、channel、firmware の dlist、IOMMU4 を読んで出す。firmware の plane が boot の framebuffer と一致する事。
2. R0: 自分の dlist（firmware の plane の写し）に LPTRS を切り替え、DL で取り込みを確かめる。取り込まれなければ firmware の list に戻す。
3. R1: pixelvalve の VFP_START の割り込みで vblank を数える（約 60 Hz）。
4. R2: 自分の UPM の handle・領域の overlay plane を 2 つの list の間の flip で動かし、全部の flip が frame の始まりで取り込まれる事。
5. R3: 共有の display device（`display-device.c`、lease・3 slot の flip・cursor・primary/overlay の zero copy）を HVS6 の dlist で動かし、node を出す。Keiland（p007）が使う。

## 作った物

- `src/drivers/gpu/bcm2711/display/hvs6.c`・`hvs6.h`: P0（node・窓・PV の IRQ を mask のまま登録）、P1（読むだけ）、R0、R1、R2。family は `hvs6`（`hvs6.stop=R1` など）。
- `attach71.c`: 起動時に display の P0 も行い、`hw.gpu.start=1` で display の段 → V3D の段の順に走る（片方の失敗は他方を止めない）。
- 書く register: dlist の memory（4096 word の上の 768 word）、使う channel の LPTRS、PV の INTEN（R1 の間だけ、終わったら元に戻す）。HVS の CONTROL・COB・CSC・PV の timing・HDMI は書かない。

- `display/hvs6-flip.c`: Pi 4 の `display-flip.c` の写し（同じ関数名・契約、Pi 5 の source の一覧で選ぶ）。plane の語を HVS6 に、plane ごとの UPM（primary handle 28 base 634、overlay 29/30、cursor 31、各 129・3 word。firmware の console の plane は firmware の handle 1 のまま写す）、list の pointer は channel の LPTRS/DL、console の list は R0 の写し、slot は 128 word × 3、compositor の clock は firmware のまま（core 500 MHz）、拡大縮小する overlay は ENOSPC で断る（未実装）、40 bit の address・pitch ≤ 16384。
- `hvs6.c` の R3 `bcm2712_display_attach_device()`: 永続の PV の vblank の service（DL を flip の状態に渡す）、R1 で測った refresh、共有の `bcm2711_display_register()`（frame の観測・自分の 2 つの buffer の flip・premultiplied の合成・node の公開）。
- build に `display-device.c`・`share.c`・`gpu.c`・`gpu-fence.c` を足した（Pi 5 の GPU の switch の時だけ）。

## 確認（実機、2026-10-22 P2、kei@10.0.30.5 D0、e1a9170d7 の次の build、`sudo sysctl hw.gpu.start=1`）

- `hvs6: P1 version 54 (D0) control 802000f0 list 4096 lbm 1024 ubm 1024`
- `hvs6: P1 ch0 ctrl0 877f0437 ...`（1920×1080 で動作）、ch1 も 640×480 で enable・動作（list 0、firmware の物、触らない）、ch2 は無効。
- `hvs6: P1 iommu ctrl 00000000 cap 00000fff bypass 00000000-00000fff ...`（IOMMU4 は無効: design §8 U6 の答え。物理 address の plane で表示できる）
- firmware の list（672〜）: 1 つの plane が 32 word の枠（8 word の plane ＋ 0xb0b0b0b0 の詰め物）、続いて valid の無い 32 word の枠が 7 つ、END は 928。plane は RGBA8888・ARGB の並び・unity・1920×1080・address 3f400000・pitch 7680・UPM handle 1 base 0（U7・U9 の答え）: `hvs6: P1 plane0 addr 3f400000 pitch 7680 upm 1@0 = boot framebuffer`
- `hvs6: R0 ok own list taken after 4000 us`（3328 の自分の list、33 word）
- `hvs6: R1 64 vblanks in 1064 ms`（PV0 の INTEN は firmware では 0 だった）
- `hvs6: R2 overlay moved 30/30 steps, slowest flip 10000 us; console list back`（UPM handle 32、base 1021）
- `hvs6: display self-test ok`。その後の V3D の self-test も全 PASS。LAN・SSH は使えた。
- 未実施: 画面の目視・写真（overlay は約 0.5 秒だけ動く）。C1 の基板。
- 2026-10-22 P2（3f2da780b の build）: `hvs6: R3 device on console list 3328, 60000 mHz, core 500000000 Hz` → `rpi4gpu: P2 two flips and console adopted` → `rpi4gpu: P3 composed frame and console adopted` → `rpi4gpu: P5 display node ready hdmi0 copy FIFO`、`/dev/gpu0` が出た。V3D の self-test も PASS。（`P1 frames 108 in 1s` は 1 ms の sleep 1000 回の観測の窓が 1 秒より長いため。R1 は 65 frame / 1080 ms で 60 Hz。）
- 未実施: Keiland・client からの lease・cursor・zero copy（p007）、拡大縮小の overlay、画面の目視。
