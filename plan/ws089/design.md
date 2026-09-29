# WS089 設計: 設定のアプリ（Settings）

ws089-p001（2026-09-29）。目標と見本は [ws.md](ws.md)、見本の画像は [mockup-network.webp](mockup-network.webp)。
他の WS の source への変更の案は [proposed/](proposed/)（main の判断が要る）。

## 1. 名前と置き場所

| 項目 | 決定 |
| --- | --- |
| program | `userland/desktop/settings/`、package `settings`、`/bin/settings`（`ZEDBSD_USERLAND_PACKAGE` の program、desktop、amd64） |
| 画面の名前 | 窓の題と App Home は「Settings」。OS の名前は「Kei」（About）。Keiland・libkeiland は画面に出さない |
| 記号の接頭辞 | `st_`（型・関数）、`ST_`（定数） |
| 起動 | `settings [PAGE]`（例 `settings network`）。PAGE が無ければ Home。system bar の network の menu 等から頁を指して開ける（呼ぶ側の変更は別の WS） |
| 一つだけ | 二つ目の起動は新しい窓を作らずに終わる必要は無い（デモでは一つ）。単一の instance の制御は Future（§10） |

## 2. 項目と範囲（デモで働かせる範囲）

左の pane は見本の群をそのまま使い、群の間に細い区切りを置く。Home（全体の一覧）は titlebar の Home の control で開く。

| 群 | 項目 | デモ（2026-10-17）での扱い | Phase |
| --- | --- | --- | --- |
| Connectivity | Wi-Fi | 働く: radio の入り切り、scan の一覧（強さ・鍵）、保存済みの network への join・disconnect | p003 |
| | Ethernet | 働く: 有線の interface、link、IPv4 の address・netmask、MAC、送受の量（読むだけ） | p003 |
| | Bluetooth | 準備中 | — |
| | VPN | 準備中 | — |
| | Network | 働く: 見本の頁（接続の状態の 4 枚、Wi-Fi・Ethernet の card、DNS（読むだけ）、通信量の graph） | p003 |
| Personalization | Appearance | 働く: 窓の透明度（desktop に即時に反映）、accent の色は §6.3 の判断による | p004 |
| | Wallpaper | 働く: 同梱の壁紙から選ぶと desktop の壁紙とすりガラスが替わる | p004 |
| | Notifications | 準備中 | — |
| | Sound | 働く: 出力の音量・mute（audiod）、出力の装置の有無と形式、試しの音（§6.4） | p005 |
| | Display | 働く（読むだけ）: 解像度・refresh・名前・拡大率。拡大の変更は §6.5 の判断 | p004 |
| Devices | Storage | 働く（読むだけ、安い）: `/` 等の mount の使用量（`statvfs`） | p004 |
| | Battery | 準備中 | — |
| | Keyboard | 働く: key の repeat の速さ・待ち | p005 |
| | Mouse | 働く: pointer の速さ、wheel の向き（natural） | p005 |
| | Touchpad | 働く: 慣性の scroll の on/off（libkeiland の scroller）、natural の向き、速さ | p005 |
| | Printers・Sharing | 準備中 | — |
| System | Users・Privacy・Security・Accessibility・Updates | 準備中 | — |
| | About | 働く: Kei の印と語、版、kernel、機械、CPU、core 数、memory、Graphics（Vulkan の装置名）、画面、hostname、uptime | p002 |

「準備中」の頁: 項目の icon・題・一行の説明を card に置き、「This page is coming in a later version of Kei.」と表示する（検索にも出る）。

## 3. 画面の構成

```
 ┌ 浮いたタイトルバー（zdesktop が描く、CONTROLS）─────────────────────────────────┐
 │ [icon] Settings   <  >  ⌂   Settings › Network            [ Search settings ] ▯ …  │
 └───────────────────────────────────────────────────────────────────────────────────┘
 ┌ 項目の pane（すりガラスの card）┐ ┌ 頁の pane（すりガラスの card）──────────────────┐
 │ Wi-Fi                           │ │ Network                                         │
 │ Ethernet ...                    │ │ Manage connections and internet settings.       │
 │ ─────                           │ │ ┌ card ┐ ┌ card ┐                              │
 │ Appearance ...                  │ │ ...（card の群、縦に scroll）                   │
 └─────────────────────────────────┘ └─────────────────────────────────────────────────┘
```

- 窓: Wayland の xdg toplevel、題「Settings」、app id `settings`。大きさの既定は 1180x800、`configure_bounds`（xdg-shell v4）に収める
  （files の ws071-p018 と同じ方法）。最小は 720x480。
- タイトルバー: WS070 の CONTROLS。control は Back・Forward・Home（Home の頁）・Breadcrumb（「Settings › 頁の名前」、段を押すと
  そこへ）・Search（text の欄）・Sidebar（項目の pane の表示の切替）。最大化では system bar の Application Zone に docking する（WS070 の既定の動き）。
  見本の「grid・list」の切替は Settings には意味が無いので置かない（Home が grid の役）。
- 二つの pane: 窓の地は透明。左の項目の pane（幅 248、窓が 900 より狭いと 208）と右の頁の pane を、`keiland_glass_v1` の card として
  zdesktop に渡す（files の `fm_ui_panels`・`glass.c` と同じ作り: frame ごとに panel の矩形を送り、zdesktop が frosted glass・縁・影を描く）。
  外側はタイトルバーの幅に揃え、pane の間は 8 px（files の ws071-p017 と同じ寸法）。
- 項目の pane: 行は icon（線の 20 px）と名前（15 px）、高さ 36。選ばれた行は淡い青の地・青い字（見本どおり）。群の間は 1 px の区切り。
  縦に scroll する（wheel・drag）。上下の key で移る（focus が pane にあるとき）。
- 頁の pane: 頭に大きい題（30 px bold）と一行の説明、下に card の群。card は白の半透明の地（すりガラスの上の tint）、角 14、影なし
  （影は zdesktop の panel の影だけ）。頁の中は縦に scroll する（wheel、drag、key の PageUp/PageDown/Home/End）。
- 部品（widgets）: card（題・説明）、行（icon・題・値・右の chevron）、toggle（見本の青い switch）、slider、button（普通・危険の赤）、
  segmented、選択の一覧（radio の行）、状態の点（緑・灰・赤）、値の組（「IP address / 192.168.1.24 / IPv4 (DHCP)」）、line graph。
  dropdown は zdesktop の context menu（`keiland_menu_popup`、WS070 protocol version 2）で開く（自前の popup を作らない）。
- Home の頁: Kei の印と「Kei」、機械の名前、群ごとに項目の tile（icon・名前・今の状態の一行: 「Connected · en0」「Volume 60%」等）。
- 検索: タイトルバーの Search に打つと、頁の pane が「Search results」の頁になり、項目（名前・keywords）と各頁の設定の名前
  （頁が表に持つ語）の一致を並べる。Enter で最初の一致へ、Esc で元の頁へ。Ctrl+F で欄へ。
- 履歴: 頁の移動を Back・Forward（Alt+Left・Right）の履歴に積む（検索の結果の頁は積まない）。
- System Menu（WS070）: Settings（About Settings・Quit Ctrl+Q）、File（Close Window Ctrl+W）、Edit（Find Ctrl+F）、View（Show Sidebar）、
  Go（Back・Forward・Home と群ごとの頁）、Window（Minimize・Zoom）、Help（About Kei → About の頁）。
- 文字は英語（見本と files に合わせる）。日本語の UI は files の F-041 と同じく Future。
- touch: tap は click として扱い、頁の pane の drag は scroll（`keiland_scroller`、慣性は §6.6 の設定に従う）。p006 で確かめる。

見た目の値（色）は files の palette に合わせる: 文字 slate（0x1f2a3a 前後）、副の文字 0x6b7688、accent 0x2f7cf6、選択の地 accent の 14%、
card の地 白の 62%。正確な値は p002 で files の `ui.c` の定数から写し、[Kei の見た目の基準](../ws035/kei-identity-design.md) に沿わせる。

## 4. 部品の再利用（files から）

| 部品 | 方法 | 理由 |
| --- | --- | --- |
| canvas・text・icons（`userland/desktop/files/canvas.c`・`text.c`・`icons.c`、`canvas.h`） | **source を共有して compile する**（settings の Makefile の source の一覧に files の 3 つの file を載せ、`#include "../files/canvas.h"`）。files の file は変えない | この 3 つは `canvas.h` だけに依存し、files の他の部分を知らない。同じ見た目（角丸・影・gradient・polygon・glyph の cache）を複写無しで得る。files の Makefile が `artwork/mark.c` を載せているのと同じ形 |
| Kei の印（`userland/desktop/artwork/mark.c`） | 同じく共有して compile | About と Home に置く |
| window（Wayland）・present（Vulkan）・glass・titlebar・menu | **複写して settings 用に削る**（`st_` の接頭辞）。shader の SPIR-V（`shaders.h`）も複写 | files の版は `fm_app`・DnD・tab・preview に絡んでいる。pdfviewer・notes も同じく自分の版を持つ（前例） |
| 頁の部品（card・toggle・slider 等） | settings の新しい `widgets.c` | files に無い |
| 頁の icon（Wi-Fi・Ethernet・Bluetooth・盾・地球・palette・画像・鐘・speaker・画面・disk・電池・keyboard・mouse・touchpad・printer・共有・人・鍵・人の形・更新・i） | settings の新しい `glyphs.c`（`fm_canvas_line`・`round`・`circle`・`polygon` で描く） | files の `enum fm_icon` に足すと files を変えることになる |

- 共有の危険: files の agent が `canvas.h` の API を変えると settings の build が壊れる。merge の build で分かる。API の変更は無いと見込む
  （files は completed）。
- 本当の共有の library（F-038: canvas・text・icons を共有の UI library へ）は、この WS で 2 つ目の使い手ができたので trigger を満たす。
  files の source を動かすのは files の WS の範囲なので、この WS では行わず、F-038 の再検討を main に依頼する（§10）。

## 5. program の形

```
userland/desktop/settings/
  Makefile        package settings（依存: libvulkan libwayland libkeiland libtruetype）
  settings.h      model（st_app、頁の表、layout、hit、event）。Wayland・Vulkan を知らない
  window.h        Wayland・Vulkan の部分（st_window、st_present、st_titlebar、st_menu、st_glass）
  main.c          引数、main loop（Wayland の dispatch、backend の poll、描画、present）
  window.c        xdg toplevel、seat（pointer・keyboard・touch）、wl_output（Display の頁）、入力の ring
  present.c       Vulkan の canvas の quad（files から）、GPU の名前（About）
  glass.c titlebar.c menu.c   zdesktop の拡張（files から）
  ui.c            layout、2 つの pane、項目の pane、履歴、breadcrumb、検索、hit test、scroll
  widgets.c       card と部品
  glyphs.c        頁の icon
  pages.c         頁の表（id・名前・説明・icon・群・keywords・描画・入力・ready）
  page-about.c page-home.c page-soon.c           p002
  page-network.c backend-network.c               p003
  page-appearance.c page-wallpaper.c page-display.c page-storage.c   p004
  page-sound.c page-input.c（Keyboard・Mouse・Touchpad）              p005
```

- main loop: files と同じく `poll` で Wayland の fd と backend の fd（networkd・audiod は libkeiland の中）を待つ。backend は 250 ms ごとに
  `keiland_network_update` 等を呼び（待たない API）、変わったら頁を描き直す。描画は damage を取らず、変わった frame だけ全体を描く
  （files と同じ。F-037 の damage は Future）。
- 頁の表: 各頁は `draw(app, canvas, area)`、`event(app, hit)`、`enter(app)`（backend を開く・scan を求める）、`leave(app)`、`keywords` を持つ。
  hit は files と同じ「frame が描いた矩形と種類と index」の表。
- host の試験: files と同じく、Wayland・Vulkan を除いた model と描画を host で build し、各頁を PNG に描く試験（`plan/ws089/tests/host-*.sh`）。
  backend は host では偽の値を与える（`st_backend_fake`、試験の build だけに入れる。production の環境変数の切替は作らない）。

## 6. backend との接続

原則（ws035-p042）: **Vulkan・Wayland・POSIX 以外の OS 依存（daemon の protocol、`/dev/system`、zedBSD の sysctl 名）は libkeiland を通す**。
settings は networkd・audiod と直接話さない。POSIX の `uname`・`sysconf`・`statvfs`・`gethostname`・`getmntinfo` 相当は直接使う。

### 6.1. About（p002、変更なし）

| 値 | 取り方 |
| --- | --- |
| OS | 「Kei」と「powered by zedBSD」（固定）、版は `uname().release`（例「0.1」）と `version` |
| 機械 | `uname().machine`（amd64）、CPU の名前は `cpuid` の brand string（amd64 だけの program なので `__asm__` で直接） |
| core 数 | `sysconf(_SC_NPROCESSORS_ONLN)` |
| memory | libkeiland の追加が要る（[proposed/libkeiland-system.md](proposed/libkeiland-system.md)）。無い間は行を出さない |
| Graphics | present の `VkPhysicalDeviceProperties.deviceName` |
| 画面 | `wl_output` の mode（幅・高さ・refresh）と make・model |
| hostname・uptime | `gethostname`、uptime は libkeiland の追加（同上）。無い間は行を出さない |

### 6.2. Network（p003）

- 状態・scan・join・disconnect・Wi-Fi の入り切り: 既存の `keiland_network_*`（networkd の SUBSCRIBE、変更なし）。
- address・MAC・link の速さ・送受の量・DNS: libkeiland の追加 `keiland_network_get_link`・`keiland_network_get_dns`
  （[proposed/libkeiland-network-link.md](proposed/libkeiland-network-link.md)、socket の `SIOCGIFADDR`・`SIOCGIFNETMASK`・`SIOCGIFHWADDR`・
  `SIOCGIFSTATS` と `/etc/resolv.conf`。networkd の protocol は変えない）。
- 通信量の graph: 窓を開いている間、1 秒ごとの送受の差を最大 10 分覚えて描く。見本の「過去 24 時間」は daemon の記録が要るので出さない
  （題は「Network activity (since this window opened)」、総量は boot からの値）。
- 新しい network への鍵を打っての join は `keiland_network_request(JOIN)` が保存済みの profile しか使わないので、p003 では保存済みだけ。
  鍵の入力は networkd の protocol の追加が要る（§10、Future）。
- 「Reset Network Settings」「VPN」「Proxy」は出さない（準備中の card にもしない）。DNS は読むだけ。

### 6.3. Appearance・Wallpaper（p004、compositor の変更が要る）

zdesktop は壁紙（`--wallpaper`）と窓の透明度（`--window-opacity`）を起動の引数でしか受けない。**実行中に desktop へ反映する仕組みが要る**。

既定の案（[proposed/desktop-preferences.md](proposed/desktop-preferences.md)）: **desktop の設定の file と、libkeiland の読み書きの API、
zdesktop の 1 秒ごとの確認**。

- 保存先: `$HOME/.config/keiland/desktop.conf`（`key=value` の行、UTF-8。path は内部の名前なので keiland のままでよい。画面には出ない）。
- 書くのは Settings だけ。libkeiland の `keiland_preferences_set` が一時 file に書いて `rename` する（原子的）。
- zdesktop（session のとき）は起動で読み、以後 1 秒ごとに `stat` の mtime を見て、変わった key だけを当てる: `wallpaper`（PPM を読み直し、
  blur と backdrop を作り直す）、`window.opacity`、（p005 の）`pointer.speed`・`pointer.natural`・`keyboard.repeat.rate`・`.delay`。
- clients（files・pdfviewer 等）は `scroll.inertia` を libkeiland の scroller が作られるときに読む（§6.6）。
- 代わりの案: Wayland の拡張 `keiland_preferences_v1`（set・changed の event）。即時で polling が無いが、libwayland・libkeiland・zdesktop の
  3 つに protocol を足す。デモまでの期間と、他の agent が zdesktop を同時に変えていることから、file の案を既定にした。

Appearance の中身（既定）: 「Window transparency」（slider、70〜100%、`window.opacity`）と「Glass」（すりガラスの on/off は zdesktop の
`--glass` が起動の時だけなので **出さない**）。accent の色は zdesktop（titlebar-shell.c の 4 か所の定数）と各 app が固定で持っており、
全体を替えるのは大きいので **デモでは出さない**（人間の判断、§9）。

Wallpaper の中身: 同梱の壁紙（`/usr/share/keiland/wallpapers/*.ppm`、今の既定の `wallpaper.ppm` を含む）の thumbnail の grid。選ぶと
`wallpaper=` を書き、desktop が替わる。thumbnail は settings が PPM を読んで縮める（P6 だけ、zdesktop と同じ）。
同梱の壁紙を増やすには image の build（WS035 の demo の image の script）に file を足す必要がある（§9、既定は Kei の画像
`kei-boot-splash.png` から作った PPM と今の既定の 2 枚で始め、足すのは main に依頼）。

### 6.4. Sound（p005、libkeiland の追加）

audiod の protocol には装置の音量（`AUDIOD_DEVICE_VOLUME`、0〜100、mute）と購読（`AUDIOD_SUBSCRIBE`・`AUDIOD_VOLUME_CHANGED`）がある。
libkeiland に `keiland_audio_*`（open・update・get・set_volume・set_muted）を足す（[proposed/libkeiland-audio.md](proposed/libkeiland-audio.md)）。
ws035-p026（system bar の音量、planning）も同じ API を使える。「試しの音」は stream の API が要るので、p005 の中で余裕があれば足し、
無ければ出さない。

### 6.5. Display（p004、変更なし・読むだけ）

zdesktop は起動の mode を使い続け、出力の拡大（HiDPI の scale）を持たない。デモでは **読むだけ**（解像度・refresh・名前・拡大率 100%）。
拡大・解像度の変更の control は置かない（準備中の小さな注記）。拡大を働かせるには zdesktop の出力の scale の対応（全 client の buffer の
scale、glyph の大きさ）が要り、別の WS の大きさ（§9）。

### 6.6. Mouse・Touchpad・Keyboard（p005、compositor と libkeiland の変更）

- `pointer.speed`（0.25〜3.0、既定 1.0）: zdesktop の相対の pointer の動きに掛ける（input.c）。絶対の pointer（QEMU の tablet・touch screen）
  には効かない。QEMU の確かめは相対の mouse（`-device virtio-mouse`／ps2）で行う。
- `pointer.natural`（0/1）: wheel の向きを逆にする（zdesktop）。Mouse と Touchpad の 2 つの key に分けるが、zdesktop が touchpad を
  区別できない間は同じ値を当てる（注記を出す）。
- `scroll.inertia`（0/1、既定 1）: libkeiland の `keiland_scroller_release` が fling を始めるかどうか。app ごとの変更は要らない
  （scroller を作るときに読む）。
- `keyboard.repeat.rate`・`.delay`: zdesktop が `wl_keyboard.repeat_info` で client に知らせる値（seat.c の固定値 25・400 を置き換え）。
  新しい keyboard の bind から効く（既存の client は次の focus で知らされる、zdesktop の実装しだい）。

### 6.7. sessiond

sessiond は変えない。desktop の設定は user の home にあり、zdesktop が自分で読む。session.sh の `--wallpaper` は既定として残し、
`desktop.conf` の `wallpaper` があればそれが勝つ。

## 7. App Home への登録

- `plan/ws035/demo/apps.conf` に 1 行: `Settings|/bin/settings|settings preferences control panel system network wifi display sound wallpaper about|6b7a8f|settings`。
- picture 名 `settings`（歯車）は zdesktop の `userland/desktop/wayland/icons.c` に無い。無ければ tile は頭文字「S」を出す（既存の動き）。
  歯車の絵を足すのは WS035 の source の小さな追加（[proposed/app-home-icon.md](proposed/app-home-icon.md)）で、main の許可で p006 に行う。
- 試験の image: lean な config（`plan/tools/files/config-amd64-files.mk` に `settings` を足した `plan/ws089/tests/config-amd64-settings.mk`）。
  clang・libcxx を含まない（2026-09-29 main の注意）。

## 8. 検証の方法

- 各 Phase の途中: `make BUILD=build/amd64 ...` で settings の package の build（warning 0）。
- host: model と描画の PNG（頁ごと）。偽の backend。
- guest: Venus の guest（`plan/ws035/tests/zdesktop-guest.sh`、`GUEST_RUNTIME=build/ws089-run`）で zdesktop --glass 1280x800 と settings を起動し、
  `zdesktop-shot.py` で画面を撮って `build/ws089-shots/` に置き、目で確かめる。操作は QMP の pointer・key、状態は SSH（guest の app の log の
  行）で読む。QEMU の console・serial の log では判定しない。
- 起動の確認（回帰）は `plan/tools/boot-test.sh`（p006）。
- 実機は未実施（この WS では QEMU だけ。実機の確かめは main の判断）。

## 9. 人間の判断が要る点（既定を選んで進める）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| D1 | Display の「拡大」 | 読むだけ（変更は準備中） | zdesktop に出力の scale が無く、足すのは別の WS の大きさ |
| D2 | Appearance の accent の色 | 出さない | 色が zdesktop と各 app に固定。窓の透明度だけを出す |
| D3 | desktop へ反映する仕組み | file（`~/.config/keiland/desktop.conf`）+ libkeiland + zdesktop の 1 秒ごとの確認 | Wayland の protocol を足さずに済み、他の agent との衝突が小さい |
| D4 | 他の WS の source の変更（libkeiland の追加 4 つ、zdesktop の設定の適用、App Home の歯車） | この WS の Phase で最小の追加として行う（main の許可の後） | 所有は WS035。[proposed/](proposed/) に差分の案 |
| D5 | canvas・text・icons の共有 | files の source を compile して共有（files は変えない） | 複写 2,500 行を避け、見た目を揃える。F-038 の本当の共有は files の WS の判断 |
| D6 | 準備中の項目 | 見本の項目を全部出し、働かないものは「準備中」 | ユーザーの見本の構成を保つ |
| D7 | 新しい Wi-Fi に鍵を打って join | 出さない（保存済みだけ） | networkd の protocol の追加が要る |
| D8 | 同梱の壁紙の追加 | 2 枚（今の既定と Kei の画像の PPM）で始める | image の script は WS035 の所有 |

## 10. Future（この WS の外）

- F-038 の再検討: canvas・text・icons を共有の UI library（例 `userland/desktop/libkeiui`、static）へ。files・settings が使い手。
- 単一の instance（二つ目の起動は既存の窓を前に出す）。
- 新しい Wi-Fi の鍵の入力（networkd の protocol）、VPN、Bluetooth、通知、電池、user の管理、更新。
- 出力の拡大と解像度の変更、accent の色の全体への反映。
- 日本語の UI（files の F-041 と一緒に）。
