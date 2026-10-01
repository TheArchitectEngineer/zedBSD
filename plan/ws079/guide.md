<!-- awesome-plan project=zedbsd record=ws079-guide -->

# WS079 作業の手引き（2026-10-01）

WS079（Notes と PDF Viewer、右上の角の swipe）を、追加の調査なしで続けるための手引き。記録の正は [ws.md](ws.md) と各 phase.md で、
この文書は 2026-10-01 の状態の要約と手順である（進捗が変わったら ws.md を直し、この文書は古くなったら日付を付けて直す）。
command は全て repo の root（`/home/awe/zedBSD-claude1`、または worktree の root）から実行する。`<W>` は作業の名前（例 `ws079-p017`）に置き換える。

## 1. ゴール

### 1.1 デモの場面（[master.md](../master.md) の fg010 の台本）

| 場面 | 内容 | この WS が持つもの |
| --- | --- | --- |
| **S8** Notes | 右上の角の swipe で Notes（全画面）、pen で書く、下の端から上への swipe（か Esc）で窓に | Notes（`userland/desktop/notes/`）、角の swipe（`userland/desktop/wayland/corner.c`）、pen の入力（kernel の HID の digitizer、compositor の tablet）。下の端の swipe の解除は WS099（ws099-p015、cleared） |
| **S9** PDF Viewer | PDF の頁送り・拡大 | PDF Viewer（`userland/desktop/pdfviewer/`）、libpdf（`userland/base/libpdf/`） |

5330 の実機は mouse と keyboard、touch は Windows の host の QEMU で見せる（master 2026-09-30 朝ユーザー「タッチはWindows上のQEMUでやります」）。

### 1.2 完了の条件（ws.md の「完了の条件」と「段の計画」）

| # | 条件 | 状態（2026-10-01） |
| --- | --- | --- |
| 1 | 右上の角から左下への swipe（pointer・pen・touch）で Notes が起動・最前面・全画面 | QEMU で済み（p010・p013・p016） |
| 2 | USB HID の digitizer の pen（筆圧 4096・傾き・消しゴム・button）を kernel が読み、compositor が `zwp_tablet_manager_v2` で渡す | QEMU の注入の pen で済み（p002・p003）。**実機の pen は未実施**（L3。外付けの touch LCD は 2026-09-29 に外した） |
| 3 | Notes: 筆圧の線・消す・page・undo・PDF の保存（ベクタ + 編集の metadata）と再編集 | QEMU と host で済み（p004・p005・p011・p014） |
| 4 | PDF Viewer: scroll と page の swipe の 2 mode、Notes で書き込む | QEMU と host で済み（p006〜p008・p015） |
| 5 | 段階 ②・③ | 済み（p007・p008・p015） |
| L2 | S8・S9 が 5330 の実機（mouse）と Windows の QEMU（touch）で通る。頁送り 1 回 ≤ 200 ms（A4 10 頁） | Linux の QEMU では PASS（p016: 最長 142 ms）。**実機と Windows の QEMU は未実施（ユーザー）** |
| L3 | 実機の pen（外付けの touch LCD + AES pen、間に合えば） | 未着手。機材の計画次第 |

**完了の定義（この手引きの提案）**: L2 の実機と Windows の QEMU の確認をユーザーが「できた」とし、L3（と条件 2 の実機の部分）を
「この WS で行う／Future Work に移す」のどちらにするかをユーザーが決めた時点で、p019（提案、下）で完了の処理をする。
L3 を Future Work に移すのは受け入れの縮小なので**人間の判断**（Awesome Plan §7）。エージェントが決めない。

## 2. 今の状態

### 2.1 済み（証拠）

| 何 | 証拠 |
| --- | --- |
| 全 Phase（p001〜p016）cleared | [ws.md](ws.md) の Phase の表 |
| S8・S9 の QEMU の通し（注入の touch と pen） | [phase016](phase016/phase.md): `demo-s8-s9: PASS`、scroll の turn 最長 142 ms、page の frame 最長 140 ms。2026-09-30 夕の統合の tree でも PASS（[ws090 p011](../ws090/phase011/phase.md) の「統合の試験」、140・148 ms）。ws099-p015 の後も PASS（page の frame 135 ms、[ws099 の表](../ws099/ws.md)） |
| 規約の全文の照合 | [phase009](phase009/phase.md)（style-check 0、`style-extra.py` 7 件は規約に当たらない） |
| host 試験（2026-10-01 にこの手引きの作成で再実行） | `run-pdf-writer: ok`（約 5 秒）、`run-notes-host: ok`（約 16 秒）、`run-pdfviewer-host: ok`（約 31 秒）。`run-pdf-render.sh`（引数なし）は既定の fuzz の回数が多く 115 秒の timeout に掛かった。`run-pdf-render.sh 100` は `run-pdf-render: ok`（約 38 秒）→ 下の command は `100` を渡す |
| 実機（5330）・Windows の QEMU での S8・S9 | **未実施**。手順の 1 枚は [demo-s8-s9-manual.md](demo-s8-s9-manual.md) |

### 2.2 残り

1. 5330 の実機（mouse）と Windows の QEMU（touch）で S8・S9（ユーザー。手順は demo-s8-s9-manual.md）。
2. 5330 の passthrough での S8・S9 の自動の確かめ（エージェントができる部分、p017 提案）。
3. Notes の Ctrl+O が「Opening from Notes is not available yet」のまま（`userland/desktop/notes/main.c:990`。p005 の残り 1）。デモには要らない（p018 提案、任意）。
4. L3 の実機の pen（機材が無い）。
5. 記録の不整合: [phase002](phase002/phase.md)・[phase003](phase003/phase.md) の頭の `Status:` が `in-progress` のまま（ws.md の表は main の判断で cleared）。完了の処理（p019）で表に合わせる。

### 2.3 既知の bug（[known-bugs.md](../known-bugs.md)）

| Bug | 状態 | WS079 との関係 |
| --- | --- | --- |
| [BUG-105](../bugs/BUG-105.md) 素の 5330 で USB の mouse（logi M650、Logi Bolt）が使えない | resolved 待ち（ws073-p032 で修正、素の 5330 での確認待ち） | S8・S9 の実機は mouse で触るので、ユーザーの確認の前提。直っていなければ別の mouse（USB の有線）で |
| [BUG-111](../bugs/BUG-111.md) PDF Viewer の key の repeat が 2 回効く | resolved（QEMU）、実機は未実施 | S9 の → の頁送り。実機で 1 押しで 2 頁進んだらこの bug の再発 |
| [BUG-114](../bugs/BUG-114.md) Esc で全画面を解くと title bar が system bar に重なる | resolved（ws035-p138） | S8 の最後の段 |
| [BUG-099](../bugs/BUG-099.md) touchinject の replay の成否の行が出ないことがある | unreproduced / tracking | `demo-s8-s9.sh` の `touchinject: FAILED` が 1 回だけ出たら、この bug の `SCRIPT.failed.log` を見る |
| [BUG-125](../bugs/BUG-125.md) C9 の p076 が時々 FAIL | tracking | 回帰で C9 を流したとき。WS079 の失敗と数えない |

## 3. 次の作業の順番

master の優先（2026-09-30 夜）では WS079 はデモ critical の 2 番目（WS099 の次）。**2026-10-10 ごろから bug の修正と実機の調整だけ**
（master「デモまでの進め方」）なので、新しい機能（p018）は 10/10 より前に限る。

| 順 | Phase | 誰 | 目的 | 依存・条件 |
| --- | --- | --- | --- | --- |
| 1 | （Phase なし） | ユーザー | 5330 の実機と Windows の QEMU で S8・S9（[demo-s8-s9-manual.md](demo-s8-s9-manual.md)）。結果は ws.md の Resume point に | デモの image（§5.5） |
| 2 | **ws079-p017（提案）** | エージェント（Mid） | 5330 の passthrough で S8・S9 を自動で通し、画面と頁送りの時間を取る | WS075 の機械の lock（`/tmp/i915-hw.lock`）が空くこと |
| 3 | **ws079-p018（提案、任意）** | エージェント（Mid） | Notes の Ctrl+O を libkeiui の file chooser に | 10/10 より前。ユーザーが要ると言ったときだけ |
| 4 | **ws079-p019（提案）** | main | WS の完了の処理 | 1 の結果と L3 の扱いのユーザーの判断 |

### 3.1 ws079-p017（提案）: 5330 の passthrough での S8・S9

- 目的: ユーザーの目視の前に、i915 の実 GPU で S8・S9 が動くこと（Notes の全画面・mouse の線・Esc、PDF Viewer の頁送り 10 回の時間・拡大）を
  エージェントが確かめる。Linux の QEMU（Venus）と違い、描画は i915 の実機の GPU（passthrough）。
- 型: [plan/ws099/tests/c5-hw.sh](../ws099/tests/c5-hw.sh)（WS075 の `hdmi-h4-hw.sh` の上で pointer・key を送り、session の log を Terminal で
  `/home/kei` に写して image から読む）。**`/run` は disk に無い**（c5-hw.sh:7 の注記）ので、`hdmi-h4-hw.sh stop` の読む
  `/run/user/1000/session.log` は使えない。app の stderr は session の log に入る（`userland/desktop/sessiond/session.c:589-593` が stdout・stderr を
  `session.log` に、compositor の `zwl_spawn`（`userland/desktop/wayland/home.c:774-815`）と角の swipe（`corner.c:85,801`）はそれを引き継ぐ）。
- 新しい file（WS079 の範囲）:
  - `plan/ws079/tests/build-demo-hw-image.sh BUILD`: `plan/ws075/demo/build-demo-image.sh` と同じ中身（apps.conf・壁紙・demo の利用者・guest の鍵・
    `I915_TEST_VBT=y`）に `--file /usr/share/ws079-tests/a4.pdf=<make-a4-document.sh の出力>` を足す。**build-demo-image.sh の後ろの引数で
    `ZEDBSD_TEST_EXTRA_FILES` を渡すと script の中の値を置き換えて apps.conf などが落ちる**（make の command line の変数は後の物が勝つ）ので、
    script を複写して 1 行足す形にする。build-demo-image.sh は WS075 の file なので変えない。
  - `plan/ws079/tests/s8-s9-hw.sh IMAGE OUTDIR`: c5-hw.sh と同じ形で、
    1. `hdmi-h4-hw.sh start`、75 秒待つ（kei の autologin）。
    2. S8: `ctl pointer drag 1915 4 1700 200`（右上の角から左下。5330 の eDP は 1920x1080 の想定 → 最初に `ctl shot desk` で確かめる）、
       3 秒、`ctl shot s8-fullscreen`、頁の上で `ctl pointer move 700 500 down move 800 520 move 900 560 up`（線）、`ctl shot s8-written`、
       `ctl hmp "sendkey esc"`、`ctl shot s8-window`、`ctl hmp "sendkey ctrl-w"`。
    3. S9: App Home から Terminal（c5-hw.sh の tile の座標 `terminal:1031:386` は App Home の並びに依る。最初に `ctl shot home` で確かめる）、
       `ctl keys "'pdfviewer /usr/share/ws079-tests/a4.pdf &\\n'"`、5 秒、`sendkey right` を 9 回（0.7 秒おき）と `sendkey left` を 1 回、
       `ctl shot s9-page9`、`sendkey ctrl-equal`（拡大）・`ctl shot s9-zoom`・`sendkey ctrl-0`。
    4. Terminal で `cp /run/user/1000/session.log /home/kei/s8s9-hw.log; sync`、QEMU を止めて `ufs-cat.py` で読む（c5-hw.sh:48-55 と同じ）。
    5. 判定: `NOTES START .* fullscreen=1`・`NOTES STROKE`・`NOTES LAYOUT window=`・`PDFVIEWER .*READY .*pages=10`・`TURN done` 10 行で
       `ms=` の最大 ≤ 200。最後の行 `s8-s9-hw: PASS`／`FAIL`。
- 受け入れ: `s8-s9-hw: PASS`、画面（`OUTDIR/*-live.png`、`h4-png.py` が作る）を目で確かめ、ユーザーに見せる。QEMU の passthrough の証拠として書き、
  「実機の単独の起動」と分ける（AGENTS.md「検証」）。
- 未知: §4 の U1・U2。

### 3.2 ws079-p018（提案、任意）: Notes の Ctrl+O

- 目的: Notes の File の Open（Ctrl+O）で libkeiui の `kui_file_chooser` を開き、選んだ PDF を開く（今は `app_status(app, "Opening from Notes is not available yet")`、
  `userland/desktop/notes/main.c:990`）。Notes の窓は ws090-p011 で `kui_window` になったので、PDF Viewer（ws090-p008）と同じ型で足せる。
- 見本: PDF Viewer の `main.c` の chooser の扱い（`chooser_open` を見て `kui_file_chooser` を開き、答えを `pv_app_chosen` に渡す。ws090-p008 の phase.md の
  「file chooser の置き換え」）。filter は「PDF Documents」（pdf）と「All Files」。開く前に今の文書を保存する（autosave と同じ `NOTES SAVE reason=`）。
- 受け入れ: `run-notes-host.sh` ok、`run-notestouch.sh`（WS081）ok、guest で Ctrl+O → chooser → PDF を選ぶ → `NOTES OPEN pages=` の log と画面、
  取り消しで今の文書が残る、`demo-s8-s9: PASS`、style-check 0、build warning 0。
- 10/10 を過ぎたら行わず、[future-work.md](../future-work.md) への移し（main に依頼）を提案する。

### 3.3 ws079-p019（提案）: WS の完了の処理（main）

AGENTS.md「記録の置き場所」の完了の形: ws.md を Status: completed（結果、制限・移管、Phase の一覧）に書き直し、Phase の directory と
この WS だけの試験を削除する。今後も使う試験は `plan/tools/` に移して master の Tools 節に登録する（main の仕事。サブエージェントは
`plan/tools/` と master を変えない）。

- `plan/tools/` に移す候補: `demo-s8-s9.sh`・`config-amd64-demo.mk`・`make-a4-document.sh`（S8・S9 の回帰）、`build-notes-image.sh`・
  `config-amd64-notes.mk`・`notes-pen.sh`、`build-pen-image.sh`・`config-amd64-pen.mk`・`pen-guest.sh`・`p003-guest.sh`・`p012-guest.sh`・
  `zdesktop-p010.sh`・`zdesktop-p013*.sh`、host の `run-pdf-*.sh`・`run-notes-host.sh`・`run-pdfviewer-host.sh` と各 `host-*.c`・生成の `make-*.py`、
  `truetype-outline-*`・`truetype-render-*`、`demo-s8-s9-manual.md`。
- **WS104 の patch との衝突**: `plan/ws104/patches/p001-paths.patch` は `plan/ws079/tests/run-pdf-*.sh`・`run-pdfviewer-host.sh`・`truetype-*.sh` の
  path を直す。試験を移すのは ws104-p001 の適用の後にする（前に移すと patch が当たらない）。
- 移管の候補: L3（実機の pen）、Notes の Ctrl+O（p018 をしなければ）、F-051（保存の圧縮）。

## 4. 未知と調べ方

| # | 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- | --- |
| U1 | 5330 の passthrough の guest の画面の大きさと App Home の tile の座標 | p017 の pointer の座標 | `plan/ws075/tests/hdmi-h4-hw.sh ctl shot desk` → `fetch` → `OUTDIR/shots/desk-live.png` を見る。tile の並びは `plan/ws035/demo/apps.conf` の順（Files・Notes・Settings・Terminal・PDF Viewer・Image Viewer・Text Editor・Browser・Model viewer・Gears・X terminal）。c5-hw.sh:26 の座標は 1920x1080 の並び |
| U2 | 角の swipe が passthrough の usb-tablet の drag で起きるか | S8 の入口 | `zdesktop-p010.sh` は QMP の pointer の drag で通っている（Venus）。passthrough でも `h4-ctl.py pointer drag` は同じ usb-tablet。session の log の `ZWL CORNER commit` で確かめる |
| U3 | 5330 の実機の i915 での頁送りの時間 | L2 の 200 ms | p017 の `TURN done ... ms=`。QEMU の Venus の 142 ms と比べる。超えたら p016 の判断（先読みの cache を小さな Phase で）を見直す。描画の中身は `userland/desktop/pdfviewer/view.c` の prefetch |
| U4 | Notes の線の遅れ（全画面は ws099-p015 以後は常に合成） | S8 の「線が遅れなく付いてくる」 | ws099-p015 の計測（QEMU の pen の遅れ中央値 74 → 117 ms）は `plan/ws099/tests/p015-pen-latency.sh IMAGE OUTDIR`（Venus、§5.2 の demo の image、注入の pen）。5330 の passthrough には pen が無い（QEMU は usb-tablet と usb-kbd だけ、`plan/ws075/tests/hdmi/h4-qemu.sh:33`）ので、実機の線の遅れはユーザーの目視。数値の目標は台本に無いので、遅いと感じたらユーザーに報告して判断を待つ |
| U5 | デモで開く PDF をどこに置くか（kei の home は sessiond が最初の login で空で作る、`userland/desktop/sessiond/session.c:511`） | S9 の素材 | WS091 の J8「デモの画像は main が選ぶ」と同じく **main（とユーザー）の判断**。今は USB メモリか Files の copy（demo-s8-s9-manual.md の「用意」）。image に入れるなら WS075 の `build-demo-image.sh` の変更を main に依頼する |
| U6 | 外付けの touch LCD と AES pen の HID の descriptor（L3） | 完了の条件 2 の実機 | 機材が戻ったら `plan/ws075/tests/hdmi-h1-hw.sh`（5330 の host の USB を記録し、新しい device の report descriptor を取る）。kernel の解析は `src/drivers/usb/hid-digitizer.c`・`hid-touch.c`、host の試験 `run-hid-pen.sh`・`run-hid-touch.sh` に descriptor を足して確かめる |

判定に QEMU の console・serial の log を使わない（AGENTS.md）。guest の中の program の log は SSH（`plan/tools/guest/guest.py run`）か、
passthrough では image から読む（上の p017）。

## 5. コマンド

一般の build・boot test・WS099 の基準・Settings と音の回帰は [plan/ws104/commands.md](../ws104/commands.md) の §1（build）・§4（boot test）・
§5（C1・C2・C9）・§8（Settings）を使う。ここは WS079 の物。

### 5.1 host の試験（Linux、数秒〜数分）

```
sh plan/ws079/tests/run-pdf-writer.sh
sh plan/ws079/tests/run-pdf-reader.sh
sh plan/ws079/tests/run-pdf-render.sh 100
sh plan/ws079/tests/run-pdfviewer-host.sh
sh plan/ws079/tests/run-notes-host.sh
sh plan/ws079/tests/run-pdf-update.sh
sh plan/ws079/tests/run-pdf-text.sh 20
sh plan/ws079/tests/run-pdf-ccitt.sh 100
sh plan/ws079/tests/run-hid-pen.sh
sh plan/ws079/tests/run-hid-touch.sh
sh plan/ws079/tests/truetype-outline-test.sh
```

- PASS: 各 script の最後の行 `run-pdf-writer: ok`（script:53）・`run-pdf-reader: ok`（:56）・`run-pdf-render: ok`（:94）・`run-pdfviewer-host: ok`（:77）・
  `run-notes-host: ok`（:68）・`run-pdf-update: ok`（:131）・`run-pdf-text: ok`（:305）・`run-pdf-ccitt: ok`（:170）と exit 0。
  `run-hid-*`・`truetype-outline-test.sh` は exit 0（失敗の行が無い）。
- 順: `run-pdfviewer-host.sh` は `run-pdf-render.sh` の作る `build/ws079-p006-host/notes.pdf` を使う（script の頭の注記）。
- 引数: `run-pdf-render.sh` の引数を省くと fuzz が長い（2026-10-01、115 秒で終わらなかった）。p009 と同じ `100`・`20`・`100` を渡す。
- 要る道具（host）: `qpdf`・`pdftoppm`・`pdfinfo`（poppler-utils）・`gs`・ImageMagick・python3 の PIL・fontTools（outline の試験）、
  `build/ws035-fonts/Inter.ttf`（main の checkout にある。worktree では main の物への読み取りの symlink）。
- 出力: `build/ws079-host/`・`build/ws079-p005-host/`・`build/ws079-p006-host/`・`build/ws079-p007-host/`・`build/ws079-p014-host/`・`build/ws079-p015-host/`
  （固定の名前。2 つの agent が同時に流さない）。

### 5.2 S8・S9 の通し（QEMU の Venus、主な回帰）

```
mkdir -p build/<W>
extra=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
make -j64 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-demo.mk BUILD=build/<W>-demo "ZEDBSD_TEST_EXTRA_FILES=$extra" disk-image > build/<W>/demo-build.log 2>&1; echo "make exit=$?"
GUEST_RUNTIME=$PWD/build/<W>-demo-run sh plan/ws079/tests/demo-s8-s9.sh build/<W>-demo/hdd-image.img build/<W>/s8s9 > build/<W>/s8s9.log 2>&1
tail -3 build/<W>/s8s9.log
```

- **IMAGE を必ず渡す。** 省くと script が `BUILD=build/amd64` で image を作り（`demo-s8-s9.sh:77-84`）、既定の `build/amd64/hdd-image.img` を上書きする。
- image の build の command は 2026-10-01 に `make -n` で target が `build/<W>-demo/hdd-image.img` になることを確かめた（build は未実施）。
- PASS: 最後の行 `demo-s8-s9: PASS`（`demo-s8-s9.sh:189`）と、その前の `RESULT scroll_turn_ms=N page_frame_ms=N page_turn_ms=N limit=200`。
  画面は `build/<W>/s8s9/s8-fullscreen.png`・`s8-written.png`・`s8-window.png`・`s9-*.png`（目で確かめてユーザーに見せる）。
- 判定の元: guest の program の log を SSH で読む（`/tmp/zdesktop.log`・`/tmp/pv.log`・`/tmp/pv2.log`）。console は読まない。
- 時間: 未計測（guest の起動と 10 回の頁送り 2 組と拡大で 5〜10 分の見込み）。runtime `build/<W>-demo-run`（既定の `build/ws079/demo-run` は他の agent と衝突しうる）。
- host の準備（host の起動ごとに 1 回）: `sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128`、`build/ws035-sq-venus/install` があること（無い worktree では
  main の物への symlink。ws090-p008 の環境の注記）。
- 止まった guest が残ったら: `GUEST_RUNTIME=$PWD/build/<W>-demo-run sh plan/ws035/tests/zdesktop-guest.sh stop`。

### 5.3 pen と touch の guest 試験（注入の device の image）

```
sh plan/ws079/tests/build-pen-image.sh build/<W>-pen > build/<W>/pen-build.log 2>&1; echo "exit=$?"
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/pen-guest.sh start build/<W>-pen/hdd-image.img
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/pen-guest.sh wait --timeout 240
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/p003-guest.sh build/<W>/p003 > build/<W>/p003.log 2>&1; tail -1 build/<W>/p003.log
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/p012-guest.sh build/<W>/p012 > build/<W>/p012.log 2>&1; tail -1 build/<W>/p012.log
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/zdesktop-p013-touch.sh build/<W>-pen build/<W>/p013t > build/<W>/p013t.log 2>&1; tail -1 build/<W>/p013t.log
GUEST_RUNTIME=$PWD/build/<W>-pen-run sh plan/ws079/tests/pen-guest.sh stop
```

- PASS: `p003-guest: status=0`（`p003-guest.sh:128`）、`p012: PASS`（`p012-guest.sh:81`）、`p013 touch: PASS`（`zdesktop-p013-touch.sh:250`）。
- `zdesktop-p013-touch.sh` の第 1 引数の BUILD から `bin/wayland`・`dynamic/libwayland-client.so`・`bin/tablet-probe` を guest に入れる（script:101-103）。
  image を作った BUILD を渡す。
- build-pen-image.sh は Noct の build を含む（p009 の記録。main の checkout では lock のまま通った）。worktree で Noct の archive の取得が始まったら止め、main に報告する
  （toolchain の規則）。

### 5.4 Notes の pen と角の swipe（Notes の image）

```
sh plan/ws079/tests/build-notes-image.sh build/<W>-notes > build/<W>/notes-build.log 2>&1; echo "exit=$?"
GUEST_RUNTIME=$PWD/build/<W>-notes-run sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-notes/hdd-image.img
GUEST_RUNTIME=$PWD/build/<W>-notes-run sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
GUEST_RUNTIME=$PWD/build/<W>-notes-run sh plan/ws079/tests/notes-pen.sh build/<W>/notes-pen > build/<W>/notes-pen.log 2>&1; tail -1 build/<W>/notes-pen.log
GUEST_RUNTIME=$PWD/build/<W>-notes-run sh plan/ws079/tests/zdesktop-p010.sh build/<W>-notes build/<W>/p010 > build/<W>/p010.log 2>&1; tail -1 build/<W>/p010.log
GUEST_RUNTIME=$PWD/build/<W>-notes-run sh plan/ws035/tests/zdesktop-guest.sh stop
```

- PASS: `notes-pen: PASS`（`notes-pen.sh:172`）、`p010: PASS`（`zdesktop-p010.sh:231`）。
- `zdesktop-p010.sh` は BUILD の `bin/wayland`・`bin/wltest` を guest に入れる（script:95-96）。
- 他の Notes の guest 試験（`notes-p005.sh`・`notes-p011.sh`・`notes-home.sh`・`notes-gesture.sh`・`notes-p014.sh`）と PDF の demo（`pdf-demo-guest.sh` の 13 段）は
  各 script の先頭の使い方どおり。既定の runtime がそれぞれ違う（`build/ws079-p005-run` など）ので、上と同じく `GUEST_RUNTIME` を自分の物にする。

### 5.5 デモの image（5330 の USB と passthrough）

```
sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-hw > build/<W>/demo-hw-build.log 2>&1; echo "exit=$?"
sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-pt passthrough > build/<W>/demo-pt-build.log 2>&1; echo "exit=$?"
```

- 1 行目は USB に書く物（ユーザーの実機）、2 行目は 5330 の passthrough（p017）。どちらも Notes・PDF Viewer・libpdf を含む（`plan/ws075/demo/config-demo-hdmi.mk`）。
  A4 の PDF は含まない（U5）。
- **2 つを同時に走らせない**（image の build は 1 つずつ。commands.md §0）。

### 5.6 回帰の組（Phase の性質で選ぶ。AGENTS.md「検証」）

| 変えた所 | 流す物 |
| --- | --- |
| libpdf | §5.1 の全て、§5.2 |
| PDF Viewer | `run-pdf-render.sh 100`・`run-pdfviewer-host.sh`、§5.2、`plan/ws090/tests/viewers-p008.sh`（使い方は先頭） |
| Notes | `run-notes-host.sh`、`plan/ws081/tests/run-notestouch.sh`、§5.2、§5.4 |
| kernel の HID・注入 | `run-hid-pen.sh`・`run-hid-touch.sh`、§5.3、boot test（commands.md §4） |
| compositor の tablet・touch・角 | §5.3、§5.4 の p010、WS099 の C9（commands.md §5） |
| 全て | 最後に build の warning 0（commands.md §1）と boot test（commands.md §4）。規約は `python3 plan/tools/style-check.py <file>...` と `python3 plan/ws079/tests/style-extra.py <file>...` |

## 6. 実機（Dell Latitude 5330）

一般の手順（USB への書き込み、起動、SSH、passthrough の lock）は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（§1.4〜1.6 passthrough の画面・入力・結果の読み戻し、
§1.7 lock、§2 USB の単独の起動、§3 デモの image、§4.3 passthrough の smoke）。WS079 に固有の確かめ:

| # | 確かめ | 手順 | 誰 |
| --- | --- | --- | --- |
| H1 | S8・S9 の台本 | [demo-s8-s9-manual.md](demo-s8-s9-manual.md) の表（mouse）。A4 の PDF は `sh plan/ws079/tests/make-a4-document.sh a4.pdf` で作り、USB メモリで kei の `Documents` へ | ユーザー |
| H2 | 頁送りの時間 | 実機の単独の起動では SSH で `grep 'TURN done' /run/user/1000/session.log`（root の password は `root`、guest の鍵で入れる image。`build-demo-image.sh` の注記）。passthrough では p017 | ユーザー／エージェント |
| H3 | Notes の線が mouse に遅れず付く、Esc で窓に戻り title bar が system bar に重ならない（BUG-114） | H1 の S8 の 2・3 行 | ユーザー |
| H4 | key の repeat が 2 回効かない（BUG-111） | H1 の S9 の → を 9 回で「Page 10 of 10」に行き過ぎない | ユーザー |
| H5 | USB の mouse（BUG-105） | Logi Bolt の受信機で pointer が動く | ユーザー |
| H6 | L3 の pen | 機材が戻ったら U6 | ユーザー（機材）＋エージェント（descriptor の解析） |

証拠は「QEMU」「5330 の passthrough」「5330 の単独の実機」を分けて書く。やっていない物は「未実施」と書く。

## 7. 注意

- **toolchain・HAL・他の WS の file**: AGENTS.md の「禁止と承認」。WS079 のサブエージェントが直してよいのは WS079 の source（libpdf・pdfviewer・notes、
  main が許した compositor の `corner.c`・`tablet.c`、libtruetype の `outline.c`）と `plan/ws079/`。`input-inject.c`・`input-inject.h`・touchinject は WS081 の物（p009）。
  `plan/ws075/demo/`・`plan/ws081/tests/`・`plan/tools/` は読むだけで、変更は main に依頼する。
- **既定の BUILD を上書きしない**: `demo-s8-s9.sh` は IMAGE を省くと `build/amd64` に作る。`build-*-image.sh` は BUILD を省くと `build/amd64`。必ず `build/<W>-*` を渡す。
- **image の build を 2 つ同時に走らせない**（commands.md §0）。
- **runtime の衝突**: 既定の runtime が他の script と重なる。`build/ws079-run`（`pen-guest.sh`・`p003-guest.sh`・`p012-guest.sh`・`zdesktop-p013*.sh`）、
  `build/ws035-sq-run`（`zdesktop-p010.sh`、WS099 の `criteria.sh` が固定で使う）、`build/ws079/demo-run`（`demo-s8-s9.sh`）、`build/ws079-p005-run`（notes-*）。
  いつも `GUEST_RUNTIME=$PWD/build/<W>-...` を前に付ける。`zdesktop-guest.sh stop` を `GUEST_RUNTIME` なしで呼ぶと、`build/.zdesktop-guest-runtime` に記録された
  最後の guest（他の agent の物かもしれない）を止める（`zdesktop-guest.sh` の頭の注記）。
- **5330 の lock**: passthrough は `flock /tmp/i915-hw.lock`（`hdmi-h4-hw.sh` の start が待つ）。start を `timeout` で切らない（script の注記）。
- **WS104 の後の path の変更**: ws104-p001 が `include/libc/keiland.h`・`keiui.h`・`truetype.h`・wayland の header を `userland/desktop/keiland/` へ移す。
  WS079 の host の script（`run-pdf-ccitt.sh`・`run-pdf-render.sh`・`run-pdf-text.sh`・`run-pdf-update.sh`・`run-pdfviewer-host.sh`・`truetype-outline-test.sh`・
  `truetype-render-compare.sh`）の header の path は [plan/ws104/patches/p001-paths.patch](../ws104/patches/p001-paths.patch) で直る。ws104-p001 の適用の後は、
  §5.1 の command はそのまま、script の中の `include/libc/truetype.h` などが `userland/desktop/keiland/` を指す。`pdf.h`・`sha2.h`・`compat/` は動かない。
  ws104-p003（libkeiland の OS の file）・p007（install の path の macro、`notes`・`pdfviewer` の path の文字列）も WS079 の source に触れうるので、
  WS079 の source を変える Phase は WS104 と同時に走らせず、main に順を確かめる。
- **ユーザーの判断**（ws.md）: 名前（notes・pdfviewer・libpdf）、PDF Viewer の段階、keyboard の shortcut は標準、自動保存と journal、`pdf_outline_stroke()` を
  stroke の形の唯一の元に、touch は Windows の QEMU、2026-10-10 以後は bug の修正と実機の調整だけ。
- **commit**: `git commit -m WIP -- <自分の path>...`（WIP 以外の文字を入れない）。push しない。`.internal/` を読まない。集約の `make check` を走らせない。
