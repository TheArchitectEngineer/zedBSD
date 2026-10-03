<!-- awesome-plan project=zedbsd record=ws089-p008 -->

# ws089-p008: 検索と Home の tile の今の状態

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（[design.md](../design.md) §3）

- 検索: タイトルバーの Search に打つと、頁の pane が「Search Results」になり、項目（名前・keywords）と各頁の設定の名前の一致を並べる。
  Enter で最初の一致へ、Esc で元の頁へ、Ctrl+F で欄へ。検索の結果の頁は履歴に積まない。
- Home の tile に今の状態の一行（「Connected · ue0」等）。
- 同時に行った依頼: App Home の歯車の絵（`userland/desktop/wayland/icons.c`、main の許可済み D4、[案](../proposed/app-home-icon.md)）。

## 実装

- 新しい `userland/desktop/settings/search.c`: 検索の状態（`struct se_search`、settings.h）、語の分割（空白、最大 8 語、小文字）、一致は
  **各語が名前か追加の語のどれかの語頭に一致**（「wi」は Wi-Fi・wired に当たり、bandwidth には当たらない）。結果は頁（Home を除く、
  表の順）の後に設定の表（Wi-Fi 4・Ethernet 5・Network 3・About 6 の 18 件。p004・p005 の頁が働くときに足す）。描画は頁の header の
  形の「Search Results」と一行（「3 results for "wi".」・「Nothing matches ...」）、結果の card（picture・名前・頁の名前か説明・chevron、
  hover、`SE_HIT_RESULT`）。
- `ui.c`: 検索中は頁の代わりに結果を描く。`se_ui_go` は検索を終えてから頁へ（同じ頁でも検索は終わる）。Back は検索を終えて頁へ戻る、
  Forward は検索を終えて次へ。Ctrl+F（menu が取らなかったとき）、検索中の Enter（最初の結果）と Esc（終わる）。検索中は頁の key の
  hook を呼ばない。タイトルバーの text の変化で検索、DONE の SUBMITTED で最初の結果、CANCELLED で終わる。breadcrumb は検索中
  「Settings › Search」、can_back は検索中 1。log に `RESULT index=` の行（試験が結果を押す）。
- `titlebar.c`: control `SE_CONTROL_SEARCH`（`KEILAND_CONTROL_SEARCH`、placeholder「Search settings」）、state に query と focus の serial、
  serial が変わると `keiland_titlebar_focus_control(FIELD)`。log の行に `query=`・`focus=`。
- `menu.c`: Edit の menu に Find（Ctrl+F、`KEILAND_MENU_ROLE_FIND`）→ `SE_ACTION_FIND`。
- `page-home.c`: 働く頁の tile の二行目を今の状態に: Wi-Fi（No Wi-Fi radio・Off・Searching・Joining・Connected · SSID・Unavailable）、
  Ethernet（Connected · ue0 / Not connected）、Network（Online · address / Offline）、About（Kei · x86_64）。接続の状態は緑・灰の点。
  `network.c`: 1 秒ごとの interface の読み直しで interface の数か address が変わったら、Home を描き直す（`network_read_links` が変化を返す）。
- App Home の歯車（WS035 の source、許可済みの最小の追加）: `icons.h` に `GLASS_ICON_APP_SETTINGS`、`icons.c` に線の cog（輪・8 つの歯・
  中心の輪。Gears の塗りの歯車と区別できる形）、名前「settings」、`icon_app_ids` に `settings`（0x6b7a8f、apps.conf の色）。atlas の
  App Home の行は 13 × 41 = 533 px（幅 1024 の中）。
- 試験: `plan/ws089/tests/host-render.c` に `search=`・`searchdone=`・`result=` と state の search の項、新しい `settings-p008.sh`（guest）。
- libkeiland・`keiland.h`・exports.map・`KEILAND_VERSION` は変えていない。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、`-Werror`）: `plan/ws089/tests/build-settings-image.sh` → exit 0、settings・zdesktop の warning 0
  （log に出た warning は noct の interpreter.c の既存の 1 件と perl の locale だけ）。
- 規約: `python3 plan/tools/style-check.py userland/desktop/settings/*.c` → 0。`git diff --check` → 0。全文の手の照合は p006。
- host: `plan/ws089/tests/host-build.sh` → 成功。Home（wifi・wired・down）、検索「wi」（3 件）・「ip addr」（2 件、result=1 で Ethernet）・
  「zzz」（0 件、searchdone=1 で終わる）、Esc・Ctrl+F の state（focus の serial が進む）、Back で検索が終わる → 期待どおり。
  `icons-host`（`plan/tools/titlebar/icons-host.c` を worktree の `build/ws089-host/` に build）→ PASS。
  画面: `build/ws089-shots/host-p008/home-*.png`・`search-*.png`・`icons-end.png`。
- **QEMU（Venus の guest、本物の networkd）**: `plan/ws089/tests/settings-p008.sh` → **PASS**。
  1. Home: Wi-Fi「No Wi-Fi radio」、Ethernet「● Connected · ue0」、Network「● Online · 10.0.2.15」（`home.png`）。
  2. Ctrl+F（zdesktop の menu の Find）→ `SEARCH focus`、`TITLEBAR state ... focus=1`、「wi」→ 3 件（`search-wi.png`、欄に wi、breadcrumb
     Settings › Search）、Enter → Wi-Fi の頁。
  3. Ctrl+F、「dns」→ 2 件、2 つ目（DNS servers）を click → Network の頁（`search-dns.png`・`search-dns-open.png`）。
  4. Ctrl+F、「zzz」→ 0 件（`search-none.png`）、Esc → 検索が終わり Network の頁（`search-ended.png`）。
  5. zdesktop の log に ERROR 無し。
  画面は `build/ws089-shots/p008/`（目で確かめた）。App Home の Settings の tile に cog の絵（`build/ws089-shots/p008/app-home.png`）、
  Settings の titlebar の app の印も cog（`home.png` の左上）。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws089-boot-test/login.png`）。
- 実機: 未実施（歯車の絵は atlas の新しい列を使う。i915 の実機の atlas の残課題に触れうるので、実機で App Home を見るまでは危険として扱う）。

## 残り

- 設定の表は働く頁の分だけ。p004・p005 で頁が働くとき、その頁の設定を `search.c` の表に足す。
- touch の drag の scroll と、検索の結果の key での選択（上下）は無い（p006 で必要なら）。
