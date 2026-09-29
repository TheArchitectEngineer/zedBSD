<!-- awesome-plan project=zedbsd record=ws089-p001 -->

# ws089-p001: 設計（Settings）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ

項目の一覧とデモで働かせる範囲、画面の構成、Files の部品の再利用の方法、各項目の backend との接続、設定の保存先、program 名と
App Home への登録を決める。人間の判断が要る点は既定を選んで理由を書く。他の WS の source の変更・protocol の追加は差分の案を
[proposed/](../proposed/) に置く。

## 結果

- 設計: [design.md](../design.md)
- 他の WS の source への案（main の判断が要る）:
  - [proposed/desktop-preferences.md](../proposed/desktop-preferences.md): desktop の設定の file（`~/.config/keiland/desktop.conf`）、
    libkeiland の `keiland_preferences_*`、zdesktop の 1 秒ごとの確認と反映（壁紙・透明度・pointer・repeat）。Appearance・Wallpaper・
    Mouse・Keyboard が desktop に効くのに要る。新しい Phase ws089-p007 で行う案。
  - [proposed/libkeiland-audio.md](../proposed/libkeiland-audio.md): `keiland_audio_*`（audiod の装置の音量）。p005。
  - [proposed/libkeiland-network-link.md](../proposed/libkeiland-network-link.md): interface の address・MAC・送受の量と DNS。p003。
  - [proposed/libkeiland-system.md](../proposed/libkeiland-system.md): memory の量（About）。
  - [proposed/app-home-icon.md](../proposed/app-home-icon.md): App Home の歯車の絵（apps.conf の 1 行は p002 で足す）。
- 主な決定: program は `userland/desktop/settings`（`/bin/settings`、接頭辞 `st_`）。左の項目の pane と右の頁の pane を
  `keiland_glass_v1` の 2 枚の card に、タイトルバーは WS070 の CONTROLS（Back・Forward・Home・Breadcrumb・Search・Sidebar）。
  canvas・text・icons は files の source を共有して compile（files は変えない）、window・present・glass・titlebar・menu は複写して削る。
- 人間の判断（既定で進める）: design.md §9 の D1〜D8。特に D1（Display の拡大は読むだけ）、D2（accent の色は出さない）、
  D3（反映は file と 1 秒ごとの確認）、D4（libkeiland・zdesktop の最小の追加を main の許可の後にこの WS で行う）。

## 実施した確認

- 読んだもの: AGENTS.md、plan/coding-style.md、ws089/ws.md と見本、ws071/ws.md、ws035/kei-identity-design.md、files の Makefile・
  window.h・canvas.h・files.h（panel・titlebar の型）・glass.c・titlebar.c・menu.c の表、pdfviewer・notes の Makefile（複写の前例）、
  keiland.h（menu・titlebar・glass・network・scroller）、libkeiland/network.c の状態の読み方、networkd の protocol.h、audiod の protocol.h と
  main.c の DEVICE_VOLUME・SUBSCRIBE、zdesktop の main.c（`--wallpaper`・`--window-opacity`）・glass.c（壁紙の読み込み）・seat.c（repeat）・
  input.c、sessiond の session.sh、apps.conf、uapi の netif.h・system.h、libc の sysconf。
- build・試験: 無し（設計の Phase）。

## 残り・次

- p002（骨格と About）から。p003〜p005 は D4 の main の許可を待つ部分がある（p003 の link の詳細、p004・p005 は p007）。
