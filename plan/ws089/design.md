# WS089 設計: 設定のアプリ（Settings）

ws089-p001（2026-09-29）。目標と見本は [ws.md](ws.md)、見本の画像は [mockup-network.webp](mockup-network.webp)。
他の WS の source への変更の案は [proposed/](proposed/)（main の判断が要る）。

## 1. 名前と置き場所

| 項目 | 決定 |
| --- | --- |
| program | `userland/desktop/settings/`、package `settings`、`/bin/settings`（`ZEDBSD_USERLAND_PACKAGE` の program、desktop、amd64） |
| 画面の名前 | 窓の題と App Home は「Settings」。OS の名前は「Kei」（About）。Keiland・libkeiland は画面に出さない |
| 記号の接頭辞 | `se_`（型・関数）、`SE_`（定数）。`st_` は `<sys/stat.h>` が予約する（`st_mtime` は macro）ので使わない |
| 起動 | `settings [PAGE]`（例 `settings network`）。PAGE が無ければ Home。system bar の network の menu 等から頁を指して開ける（呼ぶ側の変更は別の WS） |
| 一つだけ | 二つ目の起動は許す（デモでは一つ）。二つの窓が同時に設定を書いても失われないよう、書き込みは key 単位（§6.3）。単一の instance は Future（§10） |
| link | desktop の app は `platform/amd64/vmunix.mk` に個別の動的 link の規則が要る（汎用の静的な規則の `filter-out` に `settings` を足し、files と同じ規則を足す）。vmunix.mk は WS089 の所有でないので [proposed/vmunix-link.md](proposed/vmunix-link.md)（p002 の前提、main の許可） |

## 2. 項目と範囲（デモで働かせる範囲）

**2026-09-29 ユーザーの方針**: 「設定項目は、ネットワークを中心にしてください。ディスプレイはまだスタブでいいです。」main の判断で、
Wi-Fi の新しい network への鍵の入力も含める（networkd の protocol の最小の追加は案を [proposed/](proposed/) に置いてから）。
Display は stub、accent と Touchpad は出さない。Appearance・Wallpaper・Sound・Mouse・Keyboard は Network の後（下の表の D7 は取り消し）。

左の pane は見本の群をそのまま使い、群の間に細い区切りを置く。Home（全体の一覧）は titlebar の Home の control で開く。

| 群 | 項目 | デモ（2026-10-17）での扱い | Phase |
| --- | --- | --- | --- |
| Connectivity | Wi-Fi | 働く: radio の入り切り、scan の一覧（強さ・鍵）、**保存済みの profile がある** network への join・disconnect。join の失敗（ENOENT 等）は行に理由を出す。scan は保存済みかを持たないので全ての行を押せる。QEMU に Wi-Fi は無いので確かめは実機だけ（未実施なら未実施と書く） | p003 |
| | Ethernet | 働く: 有線の interface、link（up・down）、IPv4 の address・netmask、MAC、MTU、送受の量（読むだけ）。速さと DHCP・static の別は kernel から分からないので出さない | p003 |
| | Bluetooth | 準備中 | — |
| | VPN | 準備中 | — |
| | Network | 働く: 見本の頁（接続の状態の 4 枚、Wi-Fi・Ethernet の card、DNS（読むだけ）、通信量の graph） | p003 |
| Personalization | Appearance | 働く: 窓の透明度（desktop に 1 秒以内に反映）。accent の色は出さない（D2） | p004 |
| | Wallpaper | 働く: 「Kei（既定）」と同梱の壁紙から選ぶと desktop の壁紙とすりガラスが替わる。既定を選ぶと key を消す | p004 |
| | Notifications | 準備中 | — |
| | Sound | 働く: 出力の音量・mute（audiod）、出力の装置の有無と形式。装置が無ければ「No sound device」で slider は無効（§6.4） | p005 |
| | Display | 働く（読むだけ）: 解像度・refresh・拡大率（常に 1）。拡大の変更は D1 | p004 |
| Devices | Storage | 働く（読むだけ、安い）: `/` 等の mount の使用量（`statvfs`） | p004 |
| | Battery | 準備中 | — |
| | Keyboard | 働く: key の repeat の速さ・待ち | p005 |
| | Mouse | 働く: pointer の速さ（相対の mouse だけ）、wheel の向き（natural） | p005 |
| | Touchpad | **準備中**（D9）。kernel に touchpad の driver が無い（`plan/ws081/design.md`: Touch Pad は未対応）ので、効く先が無い | — |
| | Printers・Sharing | 準備中 | — |
| System | Users・Updates（Privacy・Security・Accessibility は 2026-10-06 ユーザーの判断で頁を無くした、q824） | 準備中 | — |
| | About | 働く: Kei の印と語、kernel（`uname` の sysname・release）、機械（`x86_64`）、CPU、core 数、memory（§6.1）、Graphics（Vulkan の装置名）、画面の解像度、hostname、uptime | p002 |

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

- 窓: Wayland の xdg toplevel、題「Settings」、app id `settings`（App Home・titlebar・bar の app の印は zdesktop の `icon_app_ids` による、§7）。大きさの既定は 1180x800、`configure_bounds`（xdg-shell v4）に収める
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
- Home の頁（p008）: Kei の印と「Kei」、機械の名前、群ごとに項目の tile（icon・名前・今の状態の一行: 「Connected · en0」「Volume 60%」等）。
- 検索（p008）: タイトルバーの Search に打つと、頁の pane が「Search results」の頁になり、項目（名前・keywords）と各頁の設定の名前
  （頁が表に持つ語）の一致を並べる。Enter で最初の一致へ、Esc で元の頁へ。Ctrl+F で欄へ。
- 履歴: 頁の移動を Back・Forward（Alt+Left・Right）の履歴に積む（検索の結果の頁は積まない）。
- System Menu（WS070）: Settings（About Settings・Quit Ctrl+Q）、File（Close Window Ctrl+W）、Edit（Find Ctrl+F）、View（Show Sidebar）、
  Go（Back・Forward・Home と群ごとの頁）、Window（Minimize・Zoom）、Help（About Kei → About の頁）。
- 文字は英語（見本と files に合わせる）。日本語の UI は files の F-041 と同じく Future。
- touch: tap は click として扱い、頁の pane の drag は scroll（`keiland_scroller`、慣性あり）。p006 で確かめる。

見た目の値（色）は files の palette に合わせる: 文字 slate（0x1f2a3a 前後）、副の文字 0x6b7688、accent 0x2f7cf6、選択の地 accent の 14%、
card の地 白の 62%。正確な値は p002 で files の `ui.c` の定数から写し、[Kei の見た目の基準](../ws035/kei-identity-design.md) に沿わせる。

## 4. 部品の再利用（files から）

| 部品 | 方法 | 理由 |
| --- | --- | --- |
| canvas・text・icons（`userland/desktop/files/canvas.c`・`text.c`・`icons.c`、`canvas.h`） | **source を共有して compile する**（settings の Makefile の source の一覧に files の 3 つの file を載せ、`#include "../files/canvas.h"`）。files の file は変えない | この 3 つは `canvas.h` だけに依存し、files の他の部分を知らない。同じ見た目（角丸・影・gradient・polygon・glyph の cache）を複写無しで得る。files の Makefile が `artwork/mark.c` を載せているのと同じ形 |
| Kei の印（`userland/desktop/artwork/mark.c`） | 同じく共有して compile | About と Home に置く |
| window（Wayland）・present（Vulkan）・glass・titlebar・menu | **複写して settings 用に削る**（`se_` の接頭辞）。shader の SPIR-V（`shaders.h`）も複写 | files の版は `fm_app`・DnD・tab・preview に絡んでいる。pdfviewer・notes も同じく自分の版を持つ（前例） |
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
  settings.h      model（se_app、頁の表、layout、hit、event）。Wayland・Vulkan を知らない
  window.h        Wayland・Vulkan の部分（se_window、se_present、se_titlebar、se_menu、se_glass）
  main.c          引数、main loop（Wayland の dispatch、backend の poll、描画、present）
  window.c        xdg toplevel、seat（pointer・keyboard・touch）、wl_output（Display の頁）、入力の ring
  present.c       Vulkan の canvas の quad（files から）、GPU の名前（About）
  glass.c titlebar.c menu.c   zdesktop の拡張（files から）
  ui.c            layout、2 つの pane、項目の pane、履歴、breadcrumb、検索、hit test、scroll
  widgets.c       card と部品
  glyphs.c        頁の icon
  pages.c         頁の表（id・名前・説明・icon・群・keywords・描画・入力・ready）
  page-about.c page-soon.c                      p002
  page-home.c search.c                          p008
  page-network.c backend-network.c               p003
  page-appearance.c page-wallpaper.c page-display.c page-storage.c   p004
  page-sound.c page-input.c（Keyboard・Mouse）                        p005
```

- main loop: files と同じく `poll` で Wayland の fd を待ち、timeout を 250 ms 以下にして、そのたびに backend の `keiland_network_update` 等
  （待たない API、fd の口は無い）を呼ぶ。変わったら頁を描き直す。描画は damage を取らず、変わった frame だけ全体を描く
  （files と同じ。F-037 の damage は Future）。
- 頁の表: 各頁は `draw(app, canvas, area)`、`event(app, hit)`、`enter(app)`（backend を開く・scan を求める）、`leave(app)`、`keywords` を持つ。
  hit は files と同じ「frame が描いた矩形と種類と index」の表。
- host の試験: files と同じく、Wayland・Vulkan を除いた model と描画を host で build し、各頁を PNG に描く試験（`plan/ws089/tests/host-*.sh`）。
  backend は host では偽の値を与える（`se_backend_fake`、試験の build だけに入れる。production の環境変数の切替は作らない）。
  guest の試験は実際の networkd・audiod・zdesktop を通す（偽物は host だけ）。

## 6. backend との接続

原則（ws035-p042）: **Vulkan・Wayland・POSIX 以外の OS 依存（daemon の protocol、`/dev/system`、zedBSD の sysctl 名）は libkeiland を通す**。
settings は networkd・audiod と直接話さない。POSIX の `uname`・`sysconf`・`statvfs`・`gethostname`・`getmntinfo` 相当は直接使う。

### 6.1. About（p002、変更なし）

| 値 | 取り方 |
| --- | --- |
| OS | 「Kei」と「powered by zedBSD」（固定） |
| kernel | `uname()` の sysname と release（libc の固定値、今は「zedBSD 0.0.1」） |
| 機械 | `uname().machine`（`x86_64`）、CPU の名前は `cpuid` の brand string（amd64 だけの program なので `__asm__` で直接） |
| core 数 | `sysconf(_SC_NPROCESSORS_ONLN)` |
| memory | libkeiland の追加が要る（[proposed/libkeiland-system.md](proposed/libkeiland-system.md)）。無い間と、一般の user が `/dev/system` を読めないとき（未確認）は行を出さない |
| Graphics | present の `VkPhysicalDeviceProperties.deviceName` |
| 画面 | `wl_output` の mode（幅・高さ・refresh）。make・model・name は zdesktop が固定値（「Unknown」「DISPLAY-1」）を送るので出さない |
| hostname・uptime | `gethostname`、uptime は `clock_gettime(CLOCK_MONOTONIC)`（boot からの時間であることを p002 で確かめる） |

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
  HOME が無い・空なら `getpwuid(getuid())` の home、それも無ければ保存しない（Settings は「Settings cannot be saved」を頁の頭に出し、
  値は desktop に届かない）。directory は無ければ作る。zdesktop と Settings は同じ user・同じ home で動く（Settings は App Home から
  起動する。試験も App Home から、または zdesktop と同じ HOME で起動する）。
- 書くのは Settings だけ。libkeiland の `keiland_preferences_set`・`_unset` は key 単位: file を lock（`flock`）→ 読み直す → その key だけ
  変える → 一意の一時 file（`mkstemp`）に書いて `fsync` → `rename`。二つの Settings や手の編集の変更を消さない。
- slider の drag の間は画面だけを変え、file に書くのは drag の終わり（または最後の変更から 200 ms 後）。
- zdesktop（greeter でないとき。`--session` でない試験の起動も含む）は glass を作る前に読み（壁紙の PPM を二度読まない）、以後 1 秒ごとに
  `stat` の inode・size・`st_mtim`（ns）の組を比べ、変わったら読み直して変わった key だけを当てる: `wallpaper`（PPM を読み直し、
  blur と backdrop を作り直す。新しい image を先に作り、使用中の frame の完了を待ってから差し替え、古い物を release する）、
  `window.opacity`、（p005 の）`pointer.speed`・`pointer.natural`・`keyboard.repeat.rate`・`.delay`。
- 危険: 壁紙の読み直しと blur は compositor の単一の loop で行うので、その間（数百 ms の見込み、未計測）入力と描画が止まる。p007 で host と
  guest で計る。1 秒ごとの `stat` は HOME の file system が lock 中だと frame を待たせうる（推測）ので、guest で frame の間隔の最大を計る。
- greeter は `--wallpaper` だけを使うので、login の時に壁紙が user の選んだ物へ替わる（仕様とする）。
- 代わりの案: Wayland の拡張 `keiland_preferences_v1`（set・changed の event）。即時で polling が無いが、libwayland・libkeiland・zdesktop の
  3 つに protocol を足す。デモまでの期間と、他の agent が zdesktop を同時に変えていることから、file の案を既定にした。

Appearance の中身（既定）: 「Window transparency」（slider、85〜100%、`window.opacity`）。100% 未満では zdesktop の差分の描画が止まり
（`shell.c` の `zwl_glass_still`）毎 frame 全画面を描くので、下限を 85% にし、i915 の実機での frame の率の測定を p004 の項目にする（実機は
未実施になりうる）。すりガラスの on/off は zdesktop の `--glass` が起動の時だけなので **出さない**。accent の色は zdesktop（titlebar-shell.c の 4 か所の定数）と各 app が固定で持っており、
全体を替えるのは大きいので **デモでは出さない**（人間の判断、§9）。

Wallpaper の中身: 「Kei（既定）」（key を消す: zdesktop は `--wallpaper` の値、無ければ手続きで描く風景に戻る）と、同梱の壁紙
（`/usr/share/keiland/wallpapers/*.ppm` と `/usr/share/keiland/wallpaper.ppm`）の thumbnail の grid。選ぶと `wallpaper=` を書き、desktop が替わる。
thumbnail は settings が PPM を読んで縮める（P6 だけ、zdesktop と同じ）。同梱の壁紙を増やすにはデモの image の script
（`plan/ws075/demo/build-demo-image.sh`、WS075 の所有）に file を足す必要がある（D8、main に依頼）。

### 6.4. Sound（p005、libkeiland の追加）

audiod の protocol には装置の音量（`AUDIOD_DEVICE_VOLUME`、0〜100、mute）と購読（`AUDIOD_SUBSCRIBE`・`AUDIOD_VOLUME_CHANGED`）がある。
libkeiland に `keiland_audio_*`（open・update・get・set_volume）を足す（[proposed/libkeiland-audio.md](proposed/libkeiland-audio.md)）。
ws035-p026（system bar の音量、planning）も同じ API を使える。「試しの音」は出さない（stream の API が要る）。

- 装置が無いとき（WELCOME の device が 0）audiod の set は何もせず、get は常に 100・非 mute を返す。頁は「No sound device」と出して
  slider を無効にする。
- 確かめ: 既存の guest（`zdesktop-guest.sh`・`guest.py`）には HDA の装置が無い。WS089 専用の guest の script（`plan/ws089/tests/`）で
  QEMU に `-device intel-hda -device hda-duplex` を足して確かめる。実機の HDA は未実施（master の「ユーザーが後で試す」のまま）。
- デモの image に audiod が入っていない（`plan/ws075/demo/config-demo-hdmi.mk` から続く明示の一覧に無い）。Sound をデモで働かせるには
  audiod（と rc での起動）を image に足す必要がある（WS075 の所有、main に依頼、D10）。

### 6.5. Display（p004、変更なし・読むだけ）

zdesktop は起動の mode を使い続け、出力の拡大（HiDPI の scale）を持たない。デモでは **読むだけ**（解像度・refresh・名前・拡大率 100%）。
拡大・解像度の変更の control は置かない（準備中の小さな注記）。拡大を働かせるには zdesktop の出力の scale の対応（全 client の buffer の
scale、glyph の大きさ）が要り、別の WS の大きさ（§9）。

### 6.6. Mouse・Keyboard（p005、compositor の変更）

- `pointer.speed`（25〜300 の百分率、既定 100）: zdesktop の相対の pointer の動きに掛ける（input.c）。絶対の pointer（QEMU の usb-tablet・
  touch screen）には効かない（頁に注記）。QEMU の確かめは WS089 専用の guest の script で `-device usb-mouse`（USB HID の相対の mouse）を足す
  （virtio-input の driver は無い）。
- `pointer.natural`（0/1）: wheel の向きを逆にする（zdesktop）。
- `keyboard.repeat.rate`・`.delay`: zdesktop が `wl_keyboard.repeat_info` で client に知らせる値（seat.c の固定値 25・400 を置き換え）。
  新しく bind した keyboard から効く（起動中の app には次の起動から。頁に注記）。
- 慣性の scroll の on/off（ws.md の案）は出さない（D9）: 慣性は各 app の touch screen の `keiland_scroller` にあり、切るとデモの見せ場
  （WS081）を消す。入れるなら `keiland_scroller_set_inertia` を足して各 app が呼ぶ形（scroll.c を preferences に依存させない。WS081 の
  host 試験が scroll.c を単独で build する）で、5 つの app の変更になる。

### 6.7. sessiond

sessiond は変えない。desktop の設定は user の home にあり、zdesktop が自分で読む。session.sh の `--wallpaper` は既定として残し、
`desktop.conf` の `wallpaper` があればそれが勝つ。

## 7. App Home への登録

- `plan/ws035/demo/apps.conf`（デモの image が `/etc/keiland/apps.conf` として入れる）に 1 行（依頼が許した最小の追記）: `Settings|/bin/settings|settings preferences control panel system network wifi display sound wallpaper about|6b7a8f|settings`。
- picture 名 `settings`（歯車）は zdesktop の `userland/desktop/wayland/icons.c` に無い。無ければ tile は頭文字「S」を出す（既存の動き）。
  歯車の絵を足すのは WS035 の source の追加（enum・名前の表・`icon_app_ids`・atlas の容量、[proposed/app-home-icon.md](proposed/app-home-icon.md)）で、
  main の許可で p006 に行う。
- **デモの image に settings を入れる**: `plan/ws075/demo/config-demo-hdmi.mk`（WS075 の所有）の `ZEDBSD_USER_PROGRAMS` に `settings` を足す
  必要がある（無ければ App Home は entry を出さない）。main に依頼（D10）。
- 試験の image: lean な config（`plan/tools/files/config-amd64-files.mk` に `settings` を足した `plan/ws089/tests/config-amd64-settings.mk`）。
  clang・libcxx を含まない（2026-09-29 main の注意）。

## 8. 検証の方法

- 各 Phase の途中: この worktree の中の `build/amd64`（worktree 専用、共有の build ではない）で settings の package の build（warning 0）。
- host: model と描画の PNG（頁ごと）。偽の backend。
- guest: Venus の guest（`plan/ws035/tests/zdesktop-guest.sh`、`GUEST_RUNTIME=build/ws089-run`。HDA・usb-mouse が要る p005 は WS089 専用の
  script）で zdesktop --glass 1280x800 と settings を起動し、
  `zdesktop-shot.py` で画面を撮って `build/ws089-shots/` に置き、目で確かめる。操作は QMP の pointer・key、状態は SSH（guest の app の log の
  行）で読む。QEMU の console・serial の log では判定しない。
- 起動の確認（回帰）は `plan/tools/boot-test.sh`（p006）。
- 実機は未実施（この WS では QEMU だけ。実機の確かめは main の判断）。

## 9. 人間の判断が要る点（既定を選んで進める）

| # | 点 | 既定 | 理由 |
| --- | --- | --- | --- |
| D1 | Display の「拡大」（ws.md の受け入れ案にある） | 読むだけ（変更は準備中） | zdesktop に出力の scale が無く、足すのは別の WS の大きさ |
| D2 | Appearance の accent の色 | 出さない | 色が zdesktop と各 app に固定。窓の透明度だけを出す |
| D3 | desktop へ反映する仕組み | file（`~/.config/keiland/desktop.conf`）+ libkeiland + zdesktop の 1 秒ごとの確認（反映は 1 秒以内） | Wayland の protocol を足さずに済み、他の agent との衝突が小さい |
| D4 | 他の WS の source の変更（vmunix.mk の link、libkeiland の追加 4 つ、zdesktop の設定の適用、App Home の歯車） | この WS の Phase で最小の追加として行う（main の許可の後）。`keiland.h`・exports.map・`KEILAND_VERSION` は WS081・WS035 と衝突しうるので、足すたびに main と順序を合わせる | 所有は WS035 等。[proposed/](proposed/) に差分の案 |
| D5 | canvas・text・icons の共有 | files の source を compile して共有（files は変えない） | 複写 2,500 行を避け、見た目を揃える。F-038 の本当の共有は files の WS の判断 |
| D6 | 準備中の項目 | 見本の項目を全部出し、働かないものは「準備中」 | ユーザーの見本の構成を保つ |
| D7 | 新しい Wi-Fi に鍵を打って join | **2026-09-29 取り消し: 含める**（main の判断、ユーザーの Network 中心の方針） | networkd の protocol の最小の追加は p003 で案を置いてから |
| D8 | 同梱の壁紙の追加 | 2 枚（今の既定と Kei の画像の PPM）で始める | デモの image の script（`plan/ws075/demo/build-demo-image.sh`）は WS075 の所有 |
| D9 | Touchpad の頁と慣性の scroll の on/off（ws.md の受け入れ案にある） | Touchpad は準備中、慣性の on/off は出さない | touchpad の driver が無い。慣性はデモの見せ場で、切る設定は 5 つの app の変更 |
| D10 | デモの image への `settings`・`audiod` の追加 | main に依頼（WS075 の config） | App Home の entry と Sound がデモで働くのに要る |

## 10. Future（この WS の外）

- F-038 の再検討: canvas・text・icons を共有の UI library（例 `userland/desktop/libkeiui`、static）へ。files・settings が使い手。
  それまでの危険: files に target 別の CPPFLAGS（`$(DYNAMIC_ZDESKTOP_FILES_OBJS): DYNAMIC_CPPFLAGS +=`）が付くと、共有の `.o` の中身が
  どちらの target から build されたかで変わる。付いたら settings は複写に切り替える。
- 単一の instance（二つ目の起動は既存の窓を前に出す）。
- 新しい Wi-Fi の鍵の入力（networkd の protocol）、VPN、Bluetooth、通知、電池、user の管理、更新。
- 出力の拡大と解像度の変更、accent の色の全体への反映。
- 日本語の UI（files の F-041 と一緒に）。
