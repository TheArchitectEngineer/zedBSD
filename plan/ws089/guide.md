<!-- awesome-plan project=zedbsd record=ws089-guide -->

# WS089 作業の手引き（2026-10-01）

WS089（設定のアプリ Settings）を、追加の調査なしで続けるための手引き。記録の正は [ws.md](ws.md)・[design.md](design.md)・各 phase.md で、
この文書は 2026-10-01 の状態の要約と手順である。command は全て repo の root（`/home/awe/zedBSD-claude1`、または worktree の root）から実行する。
`<W>` は作業の名前（例 `ws089-p010`）に置き換える。

## 1. ゴール

### 1.1 デモの場面（[master.md](../master.md) の fg010）

| 場面 | 内容 | Settings の物 |
| --- | --- | --- |
| **S7** Settings | 壁紙の差し替え、窓の透明度、検索 | Wallpaper の頁（生成の壁紙 5 枚 + 既定）、Appearance の透明度の slider（85〜100%）、titlebar の検索（Ctrl+F） |
| S12 音量（WS100） | system bar の音量 | Settings の Sound の頁の slider と mute（ws100-p005 が足した。WS100 の物） |

### 1.2 受け入れ（ws.md「受け入れ（2026-09-29 確定）」）

| # | 条件 | 状態（2026-10-01） |
| --- | --- | --- |
| 1 | 項目を選ぶと頁が替わる（左の pane と右の頁、戻る・進む・breadcrumb）。App Home から起動 | QEMU で済み（p002・p006・p008） |
| 2 | Network が中心: 接続の状態、Wi-Fi の一覧・接続・切断（新しい network の鍵）、入り切り、Ethernet、address・netmask・MAC・DNS、通信量 | QEMU で済み（p003、networkd の stand-in `network-probe`） |
| 3 | About（OS の名前・版・機械） | 済み（p002） |
| 4 | Display は stub、accent と Touchpad は準備中 | 済み（p004） |
| 5 | Appearance・Wallpaper・Sound・Mouse・Keyboard | 済み（p004・p005・p007・p009。Sound の音量は WS100 p005） |
| 6 | その他の項目は枠と「準備中」 | 済み（p002） |

全て QEMU（Venus）の証拠。**実機は未実施**（ws.md「WS の完了の処理に要ること」: main の判断で実機の確認に回す）。

### 1.3 完了の定義

ユーザーの指示（2026-09-29「Settingsはある程度動いたらブラッシュアップは後回しにします。」）で新しい機能の Phase は始めない。残りは:
(a) 今の main での回帰の取り直し（p010 提案）、(b) 5330 での S7（p011 提案とユーザーの目視）、(c) 完了の処理（p012 提案、main）。
完了の後に WS090 の p007（Settings の libkeiui への移行）が始められる。「後回しの候補」（ws.md の表）は新しい WS かこの WS の再開で、ユーザーが言うまで始めない。

## 2. 今の状態

### 2.1 済み（証拠）

| Phase | 内容 | 証拠 |
| --- | --- | --- |
| p001〜p009 | 設計、骨格、検索、desktop の設定（`keiland_preferences_*`）、Network、Appearance・Wallpaper・Display・Storage、Mouse・Keyboard・Sound、生成の壁紙、規約と回帰とデモの通し | 各 phase.md。[phase006](phase006/phase.md): `settings-regress.sh` の 8 本が PASS（p002・p008 は試験の期待を直して）、boot test PASS |
| 後からの変更（他の WS） | ws100-p005（Sound の頁の slider と mute、`settings/sound.c`）、ws090-p014（chooser の sheet、Settings は chooser を使わない）、KEILAND_VERSION 13 → 20 | [ws100 の ws.md](../ws100/ws.md) の p005（`volume-p005: PASS`） |
| host（2026-10-01、この手引きの作成で再実行） | `sh plan/ws089/tests/host-build.sh` → `built build/ws089-host/settings-render`（exit 0）、`sh plan/ws089/tests/host-preferences.sh` → `host-preferences: PASS`、`build/ws089-host/settings-render --page=network draw=...` が描けた | — |

### 2.2 残り

1. 今の main（KEILAND_VERSION 20、WS099 p015・p016、WS100 p005 の後）での `settings-regress.sh` の取り直し（最後に全部を流したのは 2026-09-29 の p006。
   ws104 の commands.md §8 は 2026-10-01 に script を読んで書いた物で、流してはいない）。
2. 5330 の実機・passthrough での S7（未実施）。p004 の「未実施」: 透明度 85% の frame の率、壁紙の差し替えの時間、Wi-Fi の join、相対の mouse の速さ。
3. 試験の 1 回目の失敗（guest の起動の直後に Settings の窓が出ない、p004 の注記）の原因は未確認。`settings-wait.sh` で待つようにして以後は出ていない。
4. 完了の処理（試験を `plan/tools/settings/` へ、proposed の整理、ws.md の書き直し、Phase の directory の削除）。

### 2.3 既知の bug（[known-bugs.md](../known-bugs.md)）

WS089 に開いた bug は無い（2026-10-01）。関係する物:

| Bug | 状態 | 関係 |
| --- | --- | --- |
| [BUG-105](../bugs/BUG-105.md) 素の 5330 で USB の mouse（Logi Bolt）が使えない | 修正済み、素の 5330 での確認待ち | S7 の実機は mouse で触る |
| [BUG-125](../bugs/BUG-125.md) C9 の p076 が時々 FAIL | tracking | 回帰で C9 を流したとき。Settings の失敗と数えない |
| F-062（[future-work.md](../future-work.md)）guest の試験の固定の待ち | 後回し | `plan/ws089/tests/` に固定の `sleep` が残る。起動の遅い image で FAIL したら、まず待ちを疑う |

## 3. 次の作業の順番

master の優先（2026-09-30 夜）: WS099・WS079・WS090・**WS089**…。2026-10-10 ごろから bug の修正と実機の調整だけ。

| 順 | Phase | 誰 | 目的 | 依存・条件 |
| --- | --- | --- | --- | --- |
| 1 | **ws089-p010（提案）** | エージェント（Mid） | 今の main での回帰の取り直しと S7 の確かめ（QEMU） | なし |
| 2 | **ws089-p011（提案）** | エージェント（Mid） | 5330 の passthrough で S7 | `/tmp/i915-hw.lock` が空くこと |
| 3 | （Phase なし） | ユーザー | 5330 の単独の実機で S7（§6） | デモの image |
| 4 | **ws089-p012（提案）** | main | 完了の処理 | 1 の PASS、**WS104 の p001・p003（・p007）の適用の後**（下の注意） |

### 3.1 ws089-p010（提案）: 今の main での回帰と S7（QEMU）

- 目的: 2026-09-29 から変わった物（libkeiland の版、compositor の全画面の合成、Sound の頁）の上で、Settings の guest の試験が全て通ることを確かめる。
  S7 の 3 つ（壁紙・透明度・検索）は既存の試験が持つ: `settings-p004.sh`（Wallpaper の Aurora の click と既定、Appearance の slider の 85 と 100）、
  `settings-p009.sh`（生成の 5 枚を順に）、`settings-p008.sh`（Ctrl+F と結果）、`settings-p006.sh`（App Home からの起動と全頁と検索）。新しい script は要らない。
- 手順: §5.1 の host、§5.2 の 6 行（`settings-regress: PASS`）、§5.3 の音の頁（`volume-p005: PASS`）、boot test（commands.md §4）。
- 受け入れ: 全て PASS、画面（`build/<W>/settings-regress/p004/*.png`・`p008/*.png`・`p009/*.png`）を目で確かめてユーザーに見せる。
  FAIL は 1 回だけなら流し直し、2 回続けば原因を調べる（§4）。試験の期待の古さ（p006 の前例）なら試験を直す（WS089 の file）。

### 3.2 ws089-p011（提案）: 5330 の passthrough で S7

- 目的: i915 の実機の GPU で、壁紙の差し替え・透明度 85% の窓・検索が見た目どおりに動くことを、エージェントが画面で確かめる。
- 型: [plan/ws099/tests/c5-hw.sh](../ws099/tests/c5-hw.sh)（WS075 の `plan/ws075/tests/hdmi-h4-hw.sh` の上で pointer と key を送り、session の log を
  Terminal で `/home/kei` に写して image から `ufs-cat.py` で読む。**`/run` は disk に無い**、c5-hw.sh:7）。
- 新しい file（WS089 の範囲）: `plan/ws089/tests/settings-s7-hw.sh IMAGE OUTDIR`:
  1. `H4_MINUTES=20 plan/ws075/tests/hdmi-h4-hw.sh start IMAGE OUTDIR`、75 秒待つ（kei の autologin）。
  2. App Home の Settings: `ctl pointer move 22 16 sleep 100 down up sleep 1500 move 887 386 sleep 150 down up sleep 8000`（c5-hw.sh:26 の `settings:887:386`。
     先に `ctl shot home` で tile の位置を確かめる、§4 U1）。`ctl shot settings`。
  3. 検索: `ctl hmp "sendkey ctrl-f"`、`ctl keys "wall"`、`ctl hmp "sendkey ret"`（Wallpaper の頁）、`ctl shot search`。
  4. 壁紙: Wallpaper の頁の Aurora の tile を click（座標は 3 の画面から決める）、3 秒、`ctl shot wallpaper-aurora`、既定（Kei）の tile を click、`ctl shot wallpaper-default`。
  5. 透明度: 左の pane で Appearance（Up・Down の key か click）、slider を左端へ `ctl pointer drag X0 Y X1 Y`、`ctl shot appearance-85`、右端へ戻す。
  6. Terminal（`terminal:1031:386`）で `cp /run/user/1000/session.log /home/kei/s7-hw.log; sync`、QEMU を止め、
     `ssh "$host" "python3 bigbang/ufs-cat.py bigbang/h4/guest.img /home/kei/s7-hw.log"`（c5-hw.sh:48-55 と同じ）。
  7. 判定: session の log に `ZSETTINGS SEARCH`（`SEARCH open page=wallpaper`）、`ZSETTINGS LOOK set key=wallpaper`、`ZWL PREFERENCES key=wallpaper applied`、
     `LOOK set key=window.opacity value=85`、`ERROR` が 0。最後の行 `settings-s7-hw: PASS`／`FAIL`。
- image: `sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-pt passthrough`（§5.4）。
- 受け入れ: PASS と画面（`OUTDIR/shots/*-live.png`）をユーザーに見せる。証拠は「5330 の passthrough」と書き、単独の実機と分ける。

### 3.3 ws089-p012（提案）: 完了の処理（main）

ws.md「WS の完了の処理に要ること」のとおり。AGENTS.md の完了の形: ws.md を Status: completed（結果、制限・移管、Phase の一覧）に書き直し、Phase の directory と
この WS だけの試験を削除し、今後も使う試験は `plan/tools/settings/` に移して master の Tools 節に登録する（main の仕事）。

- 移す物（ws.md の候補）: `settings-regress.sh`・`settings-wait.sh`・`settings-guest.sh`・`build-settings-image.sh`・`config-amd64-settings.mk`・`settings-p002.sh`〜`p009.sh`
  （名前を役割の名前に）・`host-build.sh`・`host-render.c`・`host-network.c`・`host-preferences.c`・`host-preferences.sh`。script の中の自分の path
  （`plan/ws089/tests/settings-wait.sh` を source する行など: `grep -rn "plan/ws089" plan/ws089/tests`）も直す。
- **順の制約**: [plan/ws104/patches/p001-paths.patch](../ws104/patches/p001-paths.patch) は `plan/ws089/tests/host-build.sh`・`host-preferences.sh` を、
  ws104-p003 は `plan/ws089/tests/host-build.sh:37`（audio.c の path）を直す。ws104 の commands.md §8 と ws104-p002・p003 の手順も `plan/ws089/tests/` を名指す。
  **試験を移すのは WS104 の p001・p003 の適用の後**にする（前に移すと patch が当たらず、WS104 の手順が壊れる）。WS104 の後なら、移す時に WS104 の
  commands.md の §8 の path も main が直す。
- proposed の整理: desktop-preferences・libkeiland-network-link・vmunix-link・app-home-icon は適用済み、libkeiland-audio は WS100 で別の形で入った
  （`keiland_audio_*`、ws100-p003）、libkeiland-system は未適用（後回しの候補）。
- 移管: 「後回しの候補」の表を Future Work へ（main）。

## 4. 未知と調べ方

| # | 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- | --- |
| U1 | 5330 の passthrough の画面の大きさ、App Home の Settings の tile、Wallpaper の頁の tile と Appearance の slider の座標 | p011 の pointer | `hdmi-h4-hw.sh ctl shot NAME` → `fetch OUTDIR` → `OUTDIR/shots/NAME-live.png` を見る。host でも `build/ws089-host/settings-render --size=1920x1080 --page=wallpaper hits` が click の領域を出す（`plan/ws089/tests/host-render.c` の頭の注釈、`hits`）。窓の位置は compositor が決めるので、画面の座標 = 窓の左上 + hits の座標。host には `/usr/share/keiland/wallpapers/` が無く、Wallpaper の頁の tile は既定の 1 枚だけ（2026-10-01 に `ZSETTINGS LOOK pictures count=1`）なので、2 枚目からの tile の位置は guest の画面で決める |
| U2 | 1 回目の起動の直後の FAIL（p004 の注記） | 回帰の信頼 | 再び出たら、試験の直後に `GUEST_RUNTIME=... python3 plan/tools/guest/guest.py run 'cat /tmp/zdesktop.log; ps'` で zdesktop と greeter・session の重なりを見る（console は読まない） |
| U3 | i915 で透明度 85% の窓の frame の率（D の危険、p004 の未実施） | S7 の見た目の滑らかさ | p011 で透明度 85% の窓を出したまま `ctl rate A 10`（`plan/ws075/tests/hdmi/h4-ctl.py` の `rate`、flip の数/秒。pipe は `ctl shot` の json の live の pipe）。目標の数値は無いので、100% の時と比べて報告する |
| U4 | 壁紙の差し替えの時間（1920x1080） | S7 | p011 の log の `LOOK set key=wallpaper` と `ZWL PREFERENCES key=wallpaper applied` の間。時刻が log に無いので、画面を 0.5 秒ごとに撮る（`ctl splash` は VGA 用。`shot` を loop で）か、ユーザーの体感 |
| U5 | 実機の Wi-Fi（RTL8822B など）での Network の頁の join | 受け入れ 2 の実機 | 5330 の単独の実機でユーザーが Wi-Fi の頁で SSID を選び鍵を入れる。networkd の状態は SSH で `cat /run/...`（networkd の socket の path は `userland/desktop/libkeiland/network-link.c` を見る） |

判定に QEMU の console・serial の log を使わない（AGENTS.md）。guest の program の log は SSH（`plan/tools/guest/guest.py run`）、passthrough は image から読む。

## 5. コマンド

一般の build（§1）・boot test（§4）・WS099 の C9（§5）は [plan/ws104/commands.md](../ws104/commands.md)。**Settings と音の回帰は同 §8**（下の §5.2・§5.3 はその
`<W>` を埋めた形と PASS の根拠）。

### 5.1 host の試験（数秒）

```
sh plan/ws089/tests/host-build.sh
sh plan/ws089/tests/host-preferences.sh
build/ws089-host/settings-render --page=wallpaper draw=build/ws089-host/wallpaper.ppm hits
```

- PASS: `built build/ws089-host/settings-render`（`host-build.sh` の最後の行、exit 0）、`host-preferences: PASS`。3 行目は画面の確かめ（PPM を見る）と click の領域。
  `--page=` は `userland/desktop/settings/pages.c` の表の小文字の語: home wifi ethernet bluetooth vpn network appearance wallpaper notifications sound display
  storage battery keyboard mouse touchpad printers sharing users privacy security accessibility updates about。
- host の preferences は試験の専用の home（`build/ws089-host/prefs-home`・`render-home`）。本当の home を使わない。

### 5.2 guest の回帰（QEMU の Venus、約 10 分）

```
mkdir -p build/<W>
sh plan/ws089/tests/build-settings-image.sh build/<W>-settings > build/<W>/settings-build.log 2>&1; echo "exit=$?"
GUEST_RUNTIME=$PWD/build/<W>-settings-run sh plan/ws089/tests/settings-guest.sh start build/<W>-settings/hdd-image.img
GUEST_RUNTIME=$PWD/build/<W>-settings-run sh plan/ws089/tests/settings-regress.sh build/<W>/settings-regress
GUEST_RUNTIME=$PWD/build/<W>-settings-run sh plan/ws089/tests/settings-guest.sh stop
```

- PASS: build の `exit=0`、最後の行 `settings-regress: PASS`（`settings-regress.sh` の最後）。各 test の行は `p006: ...PASS` の形で、FAIL の test の名前が
  `settings-regress: FAIL: p004` のように出る。各 test の log は `build/<W>/settings-regress/<test>.log`、画面は `build/<W>/settings-regress/<test>/`。
- `settings-guest.sh start` はすぐ戻る。`settings-regress.sh` が `settings-wait.sh` の `wait_guest`（SSH が答えるまで最大 2 分）で待つ。
- 1 本だけ流す: guest を起こした後に `GUEST_RUNTIME=$PWD/build/<W>-settings-run sh plan/ws089/tests/settings-p004.sh build/<W>/p004`（最後の行に PASS／FAIL）。
  p002・p003・p007・p008 は自分で guest を待たないので、先に `GUEST_RUNTIME=... python3 plan/tools/guest/guest.py wait --timeout 240`。
- image: `config-amd64-settings.mk`（files の image + settings + `network-probe`（Wi-Fi の stand-in））。生成の壁紙は `build/<W>-settings/wallpapers/` に作られて入る
  （`build-settings-image.sh`）。audiod は入らない見込みで、Sound の頁は「service が動いていない」を見る（`settings-p005.sh:10`）。
- 時間: commands.md §8 の値で約 10 分（未計測の見込み）。

### 5.3 音の頁（WS100 の試験。Settings の Sound の頁を使う）

```
sh plan/ws100/tests/host-audio.sh
sh plan/ws100/tests/build-volume-image.sh build/<W>-volume > build/<W>/volume-build.log 2>&1; echo "exit=$?"
sh plan/ws100/tests/volume-p005.sh build/<W>-volume/hdd-image.img build/<W>/volume-p005
```

- PASS: `host-audio: N/N passed`、`volume-p005: PASS`（6〜7 分、commands.md §8）。`volume-p005.sh` の runtime は `build/ws100-run` に**固定**なので、
  他の音の試験（`volume-p004.sh`）と同時に流さない。

### 5.4 デモの image（5330）

```
sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-hw > build/<W>/demo-hw-build.log 2>&1; echo "exit=$?"
sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-pt passthrough > build/<W>/demo-pt-build.log 2>&1; echo "exit=$?"
```

- 1 行目は USB に書く物、2 行目は passthrough。どちらも settings・audiod・生成の壁紙 5 枚・App Home の Settings を含む（`config-demo-hdmi.mk` の
  「ws089 (D10)」の行、`build-demo-image.sh` の壁紙の loop）。2 つを同時に走らせない。

### 5.5 回帰の組（AGENTS.md「検証」）

| 変えた所 | 流す物 |
| --- | --- |
| Settings の頁・描画 | §5.1、§5.2 |
| look.c（壁紙・透明度）・libkeiland の preferences・zdesktop の preferences | §5.1、§5.2（p004・p009・p007）、WS099 の C9（commands.md §5） |
| Sound の頁（sound.c）・libkeiland の audio | §5.2（p005）、§5.3 |
| network.c・page-network.c・libkeiland の network-link | §5.2（p003） |
| 全て | build warning 0（commands.md §1）、boot test（commands.md §4）、`python3 plan/tools/style-check.py userland/desktop/settings/*.c userland/desktop/settings/*.h` 0 |

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（§1.4〜1.6 passthrough の画面・入力・結果の読み戻し、§1.7 lock、§2 USB の単独の起動、§3 デモの image、§4.3 passthrough の smoke）。WS089 に固有の確かめ（全て未実施）:

| # | 確かめ | 手順 | 誰 |
| --- | --- | --- | --- |
| H1 | S7 の台本: App Home から Settings、Ctrl+F「wall」→ Wallpaper、Aurora など 5 枚を順に、既定に戻す、Appearance の slider を左端（85%）で窓が透ける、右端で戻る | §5.4 の 1 行目の image を USB に書いて起動（kei の autologin）。mouse と keyboard | ユーザー |
| H2 | 壁紙の差し替えが 1 秒ほどで出る、すりガラスが新しい壁紙を透かす | H1 の中で目視（U4） | ユーザー |
| H3 | 透明度 85% で窓の drag が滑らか | H1 の中で目視（U3） | ユーザー |
| H4 | Network の頁: 実際の Wi-Fi の一覧、鍵を入れた join、address・DNS | U5 | ユーザー |
| H5 | Sound の頁の slider と system bar の音量が同じ（WS100） | WS100 の L2（A7）と一緒に | ユーザー |
| H6 | passthrough の自動の確かめ | §3.2（p011 提案） | エージェント |

5330 の内蔵 LCD は `display=edp`（`config-demo-hdmi.mk`）。Display の頁は読むだけ（D1）なので、出る mode と 100% を見るだけ。

## 7. 注意

- **ユーザーの判断**（ws.md）: ブラッシュアップは後回し（新しい機能を足さない）、Network が中心、Display は stub、accent と Touchpad は出さない。
  2026-10-10 以後は bug の修正と実機の調整だけ（master）。
- **他の WS の file**: `plan/ws075/demo/`（demo の image、WS075）、`plan/ws100/tests/`、`plan/tools/`、`platform/amd64/vmunix.mk`、libkeiland・zdesktop の
  設定の file（WS089 が main の許可で足した行を除く）は読むだけで、変更は main に依頼する。
- **WS104 と同じ file**: ws104-p002 が `settings/look.c`・`settings.h`・`page-home.c`・`page-input.c`（`se_look_sound()` → `keiland_audio_available()`）、
  ws104-p003 が `plan/ws089/tests/host-build.sh:37`、ws104-p007 が settings の install の path の文字列、ws104-p001 の patch が `plan/ws089/tests/host-build.sh`・
  `host-preferences.sh` の header の path（`include/libc/keiland.h`・`truetype.h` → `userland/desktop/include/`）を変える（[plan/ws104/patches/](../ws104/patches/)）。
  ws104-p001 の後は §5.1 の command はそのまま、script の中の header の path が変わる。**Settings の source や `plan/ws089/tests/` を変える Phase は WS104 と同時に
  走らせず、main に順を確かめる。** WS104・WS105 の Phase はこの WS の作業で触らない。
- **WS090 の p007**（Settings の libkeiui への移行）はこの WS の完了の後。手順は [plan/ws090/phase007/phase.md](../ws090/phase007/phase.md)。
- **runtime の衝突**: Settings の試験の既定は全て `build/ws089-run`（`settings-guest.sh`・`settings-regress.sh`・`settings-p00*.sh`）。2 つの agent が既定のまま
  流すと同じ guest を奪い合う。いつも `GUEST_RUNTIME=$PWD/build/<W>-settings-run` を付ける。`volume-p004.sh`・`volume-p005.sh` は `build/ws100-run` に固定。
- **既定の BUILD を上書きしない**: `build-settings-image.sh` は BUILD を省くと `build/amd64`。`settings-guest.sh start` は IMAGE を省くと `build/amd64/hdd-image.img`。
  image の build を 2 つ同時に走らせない（commands.md §0）。
- **5330 の lock**: passthrough は `flock /tmp/i915-hw.lock`。`hdmi-h4-hw.sh start` を `timeout` で切らない（script の注記）。
- **commit**: `git commit -m WIP -- <自分の path>...`。push しない。`.internal/` を読まない。集約の `make check` を走らせない。
