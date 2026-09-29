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
  - [proposed/app-home-icon.md](../proposed/app-home-icon.md): App Home の歯車の絵とデモの image への settings・audiod（apps.conf の 1 行は p002 で足す）。
  - [proposed/vmunix-link.md](../proposed/vmunix-link.md): `/bin/settings` の動的 link の規則（p002 の前提）。
- 主な決定: program は `userland/desktop/settings`（`/bin/settings`、接頭辞 `se_`）。左の項目の pane と右の頁の pane を
  `keiland_glass_v1` の 2 枚の card に、タイトルバーは WS070 の CONTROLS（Back・Forward・Home・Breadcrumb・Search・Sidebar）。
  canvas・text・icons は files の source を共有して compile（files は変えない）、window・present・glass・titlebar・menu は複写して削る。
- 人間の判断（既定で進める）: design.md §9 の D1〜D8。特に D1（Display の拡大は読むだけ）、D2（accent の色は出さない）、
  D3（反映は file と 1 秒ごとの確認）、D4（vmunix.mk・libkeiland・zdesktop の最小の追加を main の許可の後にこの WS で行う）、
  D9（Touchpad は準備中、慣性の on/off は出さない）、D10（デモの image に settings・audiod を足すのは main に依頼）。

## 設計のレビュー（design-reviewer、2026-09-29）

高 7・中 13・低 9 の指摘。source で確かめたうえで全て反映した（design.md・proposed・ws.md）。主なもの:

- H1: desktop の app は `platform/amd64/vmunix.mk` に個別の動的 link の規則が要る → [proposed/vmunix-link.md](../proposed/vmunix-link.md)、p002 の前提（main の許可）。
- H2: デモの image（`plan/ws075/demo/config-demo-hdmi.mk`）に settings も audiod も無い → main に依頼（D10）。
- H3: touchpad の driver が無く、慣性は touch screen の見せ場 → Touchpad は準備中、慣性の on/off は出さない（D9）。
- H4・M6: guest に HDA と相対の mouse が無い → p005 は WS089 専用の guest の script（`intel-hda`・`usb-mouse`）。装置の無い Sound の頁を仕様に。
- H5: Wi-Fi の join は保存済みの profile だけ、QEMU に Wi-Fi は無い → 前提と「実機だけ／未実施」を明記。
- H6・H7・M1・M2・M9・M11: preferences は greeter でないときに読む、HOME の決め方、key 単位の lock と再読、drag の後に書く、inode・size・ns の比較、unset。
- M3・M4・M10: 壁紙の差し替えの順序（新を作る→fence→差し替え→release）と計測、透明度の下限 85%、stat の危険の計測。
- M7: p002 を分け、検索と Home を p008 に。proposed ごとに `KEILAND_VERSION` と衝突の相手を記載。
- L1〜L9: `uname` の値（`x86_64`・`0.0.1`）、`wl_output` の名前は固定値、link の速さは出さない、接頭辞を `se_` に、歯車の絵の範囲、
  共有 object の危険、greeter との壁紙の差、guest は実際の daemon を通す。

## 実施した確認

- 読んだもの: AGENTS.md、plan/coding-style.md、ws089/ws.md と見本、ws071/ws.md、ws035/kei-identity-design.md、files の Makefile・
  window.h・canvas.h・files.h（panel・titlebar の型）・glass.c・titlebar.c・menu.c の表、pdfviewer・notes の Makefile（複写の前例）、
  keiland.h（menu・titlebar・glass・network・scroller）、libkeiland/network.c の状態の読み方、networkd の protocol.h、audiod の protocol.h と
  main.c の DEVICE_VOLUME・SUBSCRIBE、zdesktop の main.c（`--wallpaper`・`--window-opacity`）・glass.c（壁紙の読み込み）・seat.c（repeat）・
  input.c、sessiond の session.sh、apps.conf、uapi の netif.h・system.h、libc の sysconf。
- build・試験: 無し（設計の Phase）。

## 残り・次

- p002（骨格と About）から。p002 の link と guest は vmunix.mk の許可を待つ（許可が無い間は compile まで）。p003〜p005 は D4 の許可を待つ部分が
  ある（p003 の link の詳細、p004・p005 は p007）。
