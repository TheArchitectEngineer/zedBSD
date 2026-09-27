<!-- awesome-plan project=zedbsd record=ws035p083 -->

# ws035-p083: 窓の中のすりガラスの card（`zed_glass_v1`）と see-through の Vulkan swapchain

Phase ID: `ws035-p083`
Parent: [WS035](../ws.md)（ws035-p057 から 2026-09-27 に分割）
Status: cleared（2026-09-27、サブエージェント、ws071-p015 と一緒に）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

ws071-p015（ファイラーの付箋のようなすりガラスの pane）に要る compositor の部分。設計は [glass-design.md](../glass-design.md)。

1. client の窓の alpha: libvulkan の Wayland WSI が `VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR` を広告し、`zed_gpu_buffer_v1` revision 3 の
   `set_alpha` で buffer ごとに zdesktop へ伝える。zdesktop は premultiplied の alpha で blend する。
2. `zed_glass_v1`（zdesktop・libwayland・libzdesktop の `zdesktop_glass_*`）: surface の card（角丸の矩形）を double-buffered で受け、窓の body の
   下に card の影とすりガラス（ぼかした壁紙、白、縁、flat）を描く。Wiseview の tile でも。
3. p057 に残すもの: 背後の窓のぼかし（毎 frame の backdrop）、damage。

## 受け入れ

1. Venus で zdesktop-files が glass の窓になり、zdesktop の log に card の位置が出て、card の間の画素がデスクトップ（壁紙）である（files-p015）。
2. 既存の窓（不透明の Vulkan・shm・X11・menu・titlebar）の見た目と動きが変わらない（menu-regress の zdesktop の試験、files-regress）。
3. warning 0、新しい file の style-check 0、変えた既存の file は悪くしない。boot test PASS。

## 実装（2026-09-27）

- libwayland: `protocol.c`（`zed_gpu_buffer_v1` version 3、`set_alpha`）、新 `glass-protocol.c`・`zed-glass-v1-client-protocol.h`、`internal.h`、
  `exports.map`、`Makefile`。
- libvulkan: `wsi-internal.h`（任意の op `composite_alpha`）、`wsi-swapchain.c`（advertise された 1 bit の alpha を受け、OPAQUE 以外は op へ）、
  `wsi-wayland.c`（factory ≥ 3 で PRE_MULTIPLIED を広告、lease の `premultiplied`、buffer ごとの `set_alpha`、binding を version 3 まで）、
  `wsi-display.c`（op は NULL）。
- libzdesktop: 新 `glass.c`、`include/libc/zdesktop.h`（`ZDESKTOP_VERSION` 5、`zdesktop_glass_*`）、`exports.map`、`Makefile`。
- zdesktop: 新 `panels.c`・`panels.h`、`zwl.h`（kind 2 つ、surface の `glass`・`panels`）、`protocol.c`（global 17、`factory_alpha`、dispatch、
  commit で `zwl_panels_commit`）、`import.c`（`zwl_import_set_alpha`）、`objects.c`（退場の hook）、`shell.c`（`draw_body`・`draw_tile`）、
  `shaders/panel.frag`（glass の sheen を `shape.z` で消せる）と `shaders.h`、`Makefile`。

## 検証（amd64 だけ、2026-09-27）

- guest（QEMU、Venus、lean image、warning 0）: `plan/tools/files/files-p015.sh` PASS（[ws071-p015](../../ws071/ws.md)）。
- 回帰: menu-regress（p059 p062 p063 p064 p065 p068 p069 p070 p071 p072 p014 p076 p077 p078 p079 p080）PASS、files-regress PASS（p007 の
  1 回の時間の揺れは ws071-p015 に記録）、boot test PASS。
- host の試験は無い（zdesktop は host で動かない）。wsi-wayland の新しい道は files-p015 が通す（GLASS on は PRE_MULTIPLIED が選ばれたとき）。
- 規約: 新しい file の style-check 0、変えた file は悪化なし（wsi-swapchain.c の goto 1 件は既存の cleanup の形）。
- 実機（i915）: 未実施。

## 判断が要る点（既定で進めた、戻せる）

- 標準の ext-background-effect（staging）ではなく zdesktop 独自の `zed_glass_v1`（角丸と card の意味を 1 request で言えるため）。標準は Future Work G-b。
- ガラスの中身は今はぼかした壁紙（背後の他の窓はぼけない）。本当の backdrop blur は p057 の残り。
