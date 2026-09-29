<!-- awesome-plan project=zedbsd record=ws089-p002 -->

# ws089-p002: Settings の骨格（窓・2 つの pane・履歴・About・準備中の頁・App Home）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ

- `/bin/settings` が zdesktop の上で窓を開き、左の項目の pane と右の頁の pane を 2 枚のすりガラスの card として出す（glass=1）。
- タイトルバー（WS070 の CONTROLS）に Back・Forward・Home・Breadcrumb・Sidebar。System Menu（File・View・Go・Window・Help）。
- 項目を押すと頁が替わり、Back・Forward・breadcrumb・Home・上下の key で移れる。
- Home（全項目の tile）、About（Kei の印、機械と software の値）、準備中の頁（見本の他の項目）。
- App Home から起動できる（`plan/ws035/demo/apps.conf` の行）。
- build（warning 0）、host の描画の試験、Venus の guest で画面を撮って目で確かめる。
- 検索と Home の tile の今の状態は p008 へ（design.md §3）。

## main の許可（2026-09-29）

- `platform/amd64/vmunix.mk` の変更（settings の動的 link、[proposed/vmunix-link.md](../proposed/vmunix-link.md)）。
- D10: `plan/ws075/demo/config-demo-hdmi.mk` に settings と audiod（rc の起動を含む）。

## 実装（2026-09-29）

- `userland/desktop/settings/`: `settings.h`（model）、`ui.c`（layout・2 つの pane・履歴・入力・hit）、`widgets.c`（header・card・値の行・Kei の印）、
  `glyphs.c`（頁の線の絵 25 種）、`pages.c`（頁の表）、`about.c`（uname・CPUID・sysconf・gethostname）、`page-home.c`・`page-about.c`・
  `page-soon.c`、Wayland・Vulkan の部分は files から複写して削った `window.h`・`window.c`（wl_output の mode、touch は tap を click に）・
  `present.c`（GPU の名前を取る）・`shaders.h`・`glass.c`・`titlebar.c`・`menu.c`、`main.c`、`Makefile`（files の canvas.c・text.c・icons.c と
  artwork/mark.c を共有して compile）。
- 他の WS の file（最小の追記）: `platform/amd64/vmunix.mk`（filter-out に settings、link の規則）、`plan/ws035/demo/apps.conf`（Settings の行）、
  `plan/ws075/demo/config-demo-hdmi.mk`（settings・audiod。demo の kernel に HDA の driver は無いので audiod は装置無しで動く）。
- 試験: `plan/ws089/tests/host-build.sh`・`host-render.c`（host で頁を PPM に描く）、`config-amd64-settings.mk`・`build-settings-image.sh`・
  `settings-guest.sh`・`settings-p002.sh`（Venus の guest）。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、-Werror）: `make ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/amd64 build/amd64/bin/settings`
  → exit 0、warning 0。image: `plan/ws089/tests/build-settings-image.sh build/amd64` → exit 0、warning 0（clang・libcxx を含まない lean な config）。
- 規約: `python3 plan/tools/style-check.py userland/desktop/settings/*.c userland/desktop/settings/*.h` → 0（5 件を直した後）。`git diff --check` 0。
  全文の手の照合は p006。
- host: `sh plan/ws089/tests/host-build.sh`、`build/ws089-host/settings-render` で Home・About・準備中の頁・1180x690 の About を描いた
  （`build/ws089-shots/host/`）。
- **QEMU（Venus の guest、zdesktop --glass 1280x800）**: `plan/ws089/tests/settings-p002.sh` → **PASS**。READY glass=1（窓 1180x690、
  configure_bounds に収まる）、TITLEBAR ready controls=5、MENU ready、GLASS panels count=2、項目の行で Network、Back・Forward、Down の key で
  About（list が About の行を見せる）、Sidebar の control で list の出し入れ、breadcrumb の最初の段で Home、Home の tile で Wi-Fi、App Home に
  Settings があり icon で起動（HOME launch name=Settings）、zdesktop の log に ERROR 無し。About の値: kernel zedBSD 0.0.1、x86_64、4 processors、
  host kei、Graphics llvmpipe、Display 1280 x 800, 75 Hz。
- 画面（目で確かめた）: `build/ws089-shots/p002/home.png`・`network.png`・`about.png`・`nosidebar.png`・`home-again.png`・`wifi.png`・
  `apphome.png`・`apphome-settings.png`。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws089-boot-test/login.png`）。
- 実機: 未実施。

## 直した不具合（実行中に見つけた）

- 描画中に scroll を直して次の frame を求めても、`se_ui_draw` の最後で `dirty` を 0 にしていたので消えていた（list が About の行を見せない）。
  `dirty` を描画の最初で 0 にし、main loop は `dirty` のとき待たずに描く。

## 残り・次

- App Home の歯車の絵（今は頭文字「S」）は p006（[proposed/app-home-icon.md](../proposed/app-home-icon.md)）。
- 検索と Home の tile の今の状態は p008。touch の drag の scroll は p006。
- 次は p003（Network: networkd の状態、Wi-Fi の一覧・接続・切断、新しい network の鍵の入力、Ethernet、address・DNS）。
