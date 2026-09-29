<!-- awesome-plan project=zedbsd record=ws089-p002 -->

# ws089-p002: Settings の骨格（窓・2 つの pane・履歴・About・準備中の頁・App Home）

Status: in-progress（2026-09-29）
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

## 確認

- build: `make ZEDBSD_CONFIG=plan/tools/files/config-amd64-files.mk BUILD=build/amd64 build/amd64/bin/settings`（worktree の build、-Werror）→ exit 0、
  warning 0。
- host: `sh plan/ws089/tests/host-build.sh`、`build/ws089-host/settings-render` で Home・About・準備中の頁を描いた（`build/ws089-shots/host/`）。
- guest: 実行中（image の build の後に `settings-p002.sh`）。

## Resume point

image（`plan/ws089/tests/build-settings-image.sh build/amd64`）→ `plan/ws089/tests/settings-guest.sh start` → `plan/ws089/tests/settings-p002.sh`
→ 画面を目で確かめる → 記録と commit。
