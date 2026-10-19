<!-- awesome-plan project=zedbsd record=ws275 -->

# WS275 sun4u（sparcv9）に /dev/graphics を実装し、Xzed を動かす

- ID: ws275、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 停止: ユーザー「sun4uはバグの原因がわからなければBUGチケットに積んでいったんやめます。」。p001〜p005 cleared、ユーザーの「全部 merge する」で main に merge（50bd10c5f）。p006 は BUG-337 で uncleared、p007 planned）
- 起点（2026-10-19 ユーザー）:「では、sun4uでも/dev/graphicsを実装して、Xzedが動くようにします。」（WS267 で入れた QEMU sun4u の VGA の framebuffer の console の上に）
- 前提（Q1 の調べ）: /dev/graphics は今 pcat（src/drivers/platform/pcat/graphics/）と pc98 にある（include/uapi/graphics.h、docs/reference/control-devices.md）。Xzed は userland/x11/xzed（platform は *、既定は n）。sun4u の HAL は bsp-sun4u の framebuffer.c（std VGA）。sparcv9 は big endian の 64 bit で、pixel の並びと入力（keyboard・mouse）の道を設計で確かめる。sparcv9 はユーザーの担当の領域なので、変える file は設計で示す。
- 決定（2026-10-19 18:25 ユーザー）: 「私のSunBlade 150にはXVR-500か600がついているようです。大したアクセラレーションがついたボードとは思えないので、OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」 → console と /dev/graphics は OpenBoot が用意した firmware framebuffer（OBP の screen の node の property）を使う。QEMU sun4u も OpenBIOS の property から読む形に揃える。card の driver（XVR-500/600 など）は範囲外。実機の候補は Sun Blade 150。
- 訂正（2026-10-19 18:26 ユーザー）: 「あ、待って。firmware framebufferを使うけど、XVR-500などの既知のデバイスがあるなら、そっちを初期化することにします。」 → 既定は firmware framebuffer、既知の device（XVR-500 など）が見つかればその driver が初期化して /dev/graphics を受け持つ。上の「card の driver は範囲外」は取り消し、card の driver は Phase に分ける。
- 決定（2026-10-19 18:29 ユーザー）: 「まずAのフレームバッファだけ実装して、Sun PGX64とかはあとで対応できるように口だけ残しておきます。」 → 今回は firmware framebuffer だけを実装し、既知の card の driver が引き取る口（登録・優先度・引き取りの順）は作るが card の driver そのもの（PGX64・XVR-500/600 など）は後の WS。D8 は今は選ばない。
- 決定（2026-10-19 19:06 ユーザー、クリック）: D6 sparcv9 の file は「P の担当が変える」（merge の前に差分を見せる）。HAL の差分は「承認する」。
- 決定（2026-10-19 19:12 ユーザー、クリック）: HAL の差分は「改訂 3 で承認」。D4「(a) loader が tree から」。D1 (a)・D2 (a)・D5 (a)・D7 (b) は「既定のまま」。
- 担当: P1（p001 cleared、p002 は q1311、p003 は q1312、p004 は q1314、p005 は q1318）。

## 完了の条件（改訂 3）

1. QEMU sun4u で /dev/graphics が開け、pcat と同じ契約（include/uapi/graphics.h）で mode の列挙・ENTER・ioctl での描画（BLIT・FILL など、mmap は契約に無い）ができる。8 bpp と 32 bpp の両方。
2. QEMU sun4u で Xzed が起動し、画面に zterm が出て、keyboard と mouse で操作できる。判定は T1 の boot-test.sh の画面（`BOOT_TEXT_ORIGINS` で zterm の原点を渡す。mouse は zterm の double click と中の click の貼り付け）と既知の色の画素の値、読めない時は T1 の PNG の目視（design §5）。
3. X11 の program は platform `*` のまま、arch の分岐を足さない。base の全部を sparcv9 で build するのはユーザーの担当（外部の条件）。
4. 既知の card の driver が後で引き取れる口（backend の登録と優先度）がある。card の driver と実機（Sun Blade 150）は本 WS の外（F-107〜F-111）。

## Phase

| Phase | 目的 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（[design.md](design.md)）と design-reviewer | — | cleared（改訂 3.1、2026-10-19 P1 q1311、Q1 の判定: design-review-3 の結論） |
| p002 | loader（display の node・property・qext・`color!`・i8042、`ofw.h`）、boot.h と HAL の名前付きの値（fallback の時も）、HAL の早期の console | p001、HAL の承認（改訂 3 の文面）、D4・D6・D7 | cleared（2026-10-19、T1-743） |
| p002b | T1 の観察（gdbstub で handoff の値、`-g` x8・x32・既定の 3 条件の PNG） | p002 | 観察済み（T1-743、2026-10-19、QEMU のみ。qext の実値・色の順は未確認で p004 以降） |
| p003 | generic の前段（登録・優先度・行の分割）と host 試験 | p002b、D1 | cleared（2026-10-19） |
| p004 | firmware fb の backend、text の console と出力の道、8042 の共有と sun4u の poll、evdev、sun4u.c | p003、D2・D5・D6 | cleared（2026-10-19、T1-745・T1-746 (1)） |
| p005 | userland の config・Makefile・vmunix.mk・libX11 の BE、build（warning 0） | p004、D6 | cleared（2026-10-19、build・host） |
| p006 | T1 の試験（sun4u 8・32 bpp・既定の X、色の画素、mouse、回帰）。T1-746 (2) の client が出ない件の観察 | p005（boot-test の `QEMU_EXTRA`・`mouse:`・`BOOT_TEXT_ORIGINS` は main に入済み） | uncleared（BUG-337、2026-10-19 停止） |
| p007 | 規約の全文の見直し | p006 | planned |
