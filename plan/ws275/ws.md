<!-- awesome-plan project=zedbsd record=ws275 -->

# WS275 sun4u（sparcv9）に /dev/graphics を実装し、Xzed を動かす

- ID: ws275、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（p001 を P1、q1303。旧: planning（2026-10-19 18:18 Q1 作成）
- 起点（2026-10-19 ユーザー）:「では、sun4uでも/dev/graphicsを実装して、Xzedが動くようにします。」（WS267 で入れた QEMU sun4u の VGA の framebuffer の console の上に）
- 前提（Q1 の調べ）: /dev/graphics は今 pcat（src/drivers/platform/pcat/graphics/）と pc98 にある（include/uapi/graphics.h、docs/reference/control-devices.md）。Xzed は userland/x11/xzed（platform は *、既定は n）。sun4u の HAL は bsp-sun4u の framebuffer.c（std VGA）。sparcv9 は big endian の 64 bit で、pixel の並びと入力（keyboard・mouse）の道を設計で確かめる。sparcv9 はユーザーの担当の領域なので、変える file は設計で示す。
- 決定（2026-10-19 18:25 ユーザー）: 「私のSunBlade 150にはXVR-500か600がついているようです。大したアクセラレーションがついたボードとは思えないので、OpenBootが用意したframebufferを使うのがコンソールとしてはいいですね。/dev/graphicsはfirmware framebufferにします。」 → console と /dev/graphics は OpenBoot が用意した firmware framebuffer（OBP の screen の node の property）を使う。QEMU sun4u も OpenBIOS の property から読む形に揃える。card の driver（XVR-500/600 など）は範囲外。実機の候補は Sun Blade 150。
- 訂正（2026-10-19 18:26 ユーザー）: 「あ、待って。firmware framebufferを使うけど、XVR-500などの既知のデバイスがあるなら、そっちを初期化することにします。」 → 既定は firmware framebuffer、既知の device（XVR-500 など）が見つかればその driver が初期化して /dev/graphics を受け持つ。上の「card の driver は範囲外」は取り消し、card の driver は Phase に分ける。
- 決定（2026-10-19 18:29 ユーザー）: 「まずAのフレームバッファだけ実装して、Sun PGX64とかはあとで対応できるように口だけ残しておきます。」 → 今回は firmware framebuffer だけを実装し、既知の card の driver が引き取る口（登録・優先度・引き取りの順）は作るが card の driver そのもの（PGX64・XVR-500/600 など）は後の WS。D8 は今は選ばない。
- 担当: 未割当（手の空いた担当から p001）。

## 完了の条件（案、p001 で確定）

1. QEMU sun4u で /dev/graphics が開け、pcat と同じ契約（include/uapi/graphics.h）で mode・framebuffer の写像・描画ができる。
2. QEMU sun4u で Xzed が起動し、画面に X の client（例 zterm）が出て、keyboard と mouse で操作できる。判定は boot-test.sh の画面。
3. userland/base と X11 の program は arch ごとに有無を分けない（2026-10-19 の Guardrail）。
4. 実機（Sun Blade 150、XVR-500 か 600）は、まず firmware framebuffer の道、次に card の driver。実機の確かめはユーザーの目視で、着手の時に決める。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: /dev/graphics の契約と pcat の実装の読み、sun4u の VGA での実装の形（kernel の driver の置き場所、HAL との境界、mode の設定、framebuffer の user への写像、big endian の pixel の並び）、入力（QEMU sun4u の keyboard・mouse の device と driver の有無）、Xzed の sparcv9 での build（X11 の package、userland の X の group）、boot-test の判定、Phase の表と LW。design-reviewer を通す | in-progress（q1303、P1、2026-10-19） |
