<!-- awesome-plan project=zedbsd record=ws070p013 -->

# ws070-p013: 補強: TABS（見え方・切り方・keyboard・wheel）と menu の comment の照合

Phase ID: `ws070-p013`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の指示でサブエージェントが worktree の branch で実行。WS070・WS071 の締めの後の補強）

## 範囲

p011 の「残り」と p012 の「残り」:

1. docked の白い bar で active でない tab がほとんど見えない → 地に合った色。
2. 縮めた tab の題名の切れ方（"Docu…" が 3 つ並んで区別できない）→ 切り方と配分（窓の題名が先に譲る）。
3. dock・restore の animation の途中の strip の確認。
4. tab の keyboard（Ctrl+Tab・Ctrl+Shift+Tab・Ctrl+PageUp/PageDown、Ctrl+W、Ctrl+T）と、scroll する strip の wheel。
   drag での並べ替えは Future Work のまま。
5. `menu-shell.c` の長い段落の comment（p006・p012 の残り）と、`libzdesktop/menu.c`・`libwayland/menu-protocol.c`・`zdesktop.h` の
   手の照合。

## 実装（2026-09-27）

- `titlebar-shell.c`:
  - 見え方: active でない tab の面を白の 22% から ink の 7%（hover 13%）の薄い地に（白い bar でもガラスでも見える）。active は白
    （97%）に ink 16% の 1 px の縁。
  - 切り方: tab の題名を `glass_draw_text_middle`（新、glass.c: 先頭と末尾を残し間を "..."、末尾が "." で始まるときはその後から、
    "Doc...nt 4"・"REA...md"）で。× を描かない tab（active でも hover でもない）は × の分の空きを題名に回す。
  - 配分: TABS の窓の題名の幅（`zwl_titlebar_title_limit`）を、tab が望む幅（`shell_tabs_need`: 各 tab・"+"・"…"）が入らないとき
    5 分の 1 から最小 56 px まで先に縮める。tab の望む幅の計算は `shell_tab_natural` にまとめた。
  - keyboard: `zwl_titlebar_tab_key`（新、seat.c で window の menu の shortcut の後）: TABS の focus の窓で Ctrl+Tab・Ctrl+PageDown は
    次、Ctrl+Shift+Tab・Ctrl+PageUp は前の tab を `tab_activated`（端で回る）、Ctrl+W は active が closable なら `tab_close_requested`、
    Ctrl+T は "+" があれば `new_tab_requested`。menu の shortcut が同じ key を持てば menu が先（zdesktop-files 等は影響なし: CONTROLS）。
  - wheel: `zwl_titlebar_axis`（新、seat.c で App Home の後）: strip の上（tab・×・"+"・矢印）の wheel は、その向きの矢印が生きて
    いる（記録された）ときだけ strip を 1 tab 動かす（`ZWL TITLEBAR strip scroll ... by=wheel`）。strip の上の wheel は client へ送らない。
- `titlebar.h`・`glass.h`・`seat.c`: 宣言と呼び出し。`zdesktop.h`: tab の key の説明、context menu の listener の NULL と errno。
- 照合（comment と段落だけ、動きは同じ）: `menu-shell.c` の 55 か所（if・呼び出しの前の comment と段落の分け方、`shell_open` の
  if/if を if/else に。前後の token を比べて違いはこの 1 か所だけ）、`libzdesktop/menu.c`（段落の分け方 16 か所、insert の if/else の
  括弧）、`menu-protocol.c`（setter の送信を段落に、dispatch の case を段落に）、`zdesktop.h`（重複した `struct wl_surface;` を削除）。

## 検証（amd64 だけ、Venus の guest、2026-09-27）

- `titlebar-p013.sh`（新）PASS: keys（probe に tab 3・1・3・2・3・close 3・new の順）、dock の途中の画面 2 枚と docked、長い題名と
  5 tab の窓で題名が譲り（最初の tab が窓の左端から +118）strip が scroll しない、14 tab の strip の wheel で first=1・first=0。
- 回帰: titlebar-p010・titlebar-p011・menu-p002・menu-p003（GUEST_RUNTIME=build/ws071-run）PASS、files-p008・p011・p014・p018 PASS。
- 規約: 変えた file の style-check 0（menu-shell.c・titlebar-shell.c・glass.c・libzdesktop/menu.c・menu-protocol.c・zdesktop.h）。
  `git diff --check` 無し。
- 画面（`/home/awe/zedBSD-rpi4/build/ws070-shots/`）: 前 `p011-20260927-venus-docked.png`・`p011-20260927-venus-narrow.png`、後
  `p013-20260927-venus-docked-editor.png`（docked の白い bar で tab が見える）、`p013-20260927-venus-narrow.png`（"Doc...nt 4/5/6"）、
  `p013-20260927-venus-dock-1.png`・`-dock-2.png`（dock の途中: strip は形を保って動く）、`-docked.png`、`-keys.png`、`-title.png`
  （"A_rat..." が譲る）、`-wheel.png`。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- scroll する strip は 1 tab の幅を least と望む幅の小さい方にするので、tab が短いと右の矢印の前に空きが残る（`-wheel.png`）。
  見えている tab の実際の幅で数を決めるのが良い。
- scroll の矢印の長押し（連続の scroll）、tab の drag での並べ替え（Future Work）、touch の大きさ。
- 題名の配分は tab の望む幅を浮いたタイトルバーの control の大きさで数える（docked の bar は 2 px 小さいので少し控えめ）。
