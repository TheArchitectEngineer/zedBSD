<!-- awesome-plan project=zedbsd record=ws090-guide -->

# WS090 作業の手引き（2026-10-01）

WS090（widget・control の共有 library `libkeiui` と、app の移行）を、追加の調査なしで続けるための手引き。記録の正は [ws.md](ws.md)・
[design.md](design.md)・各 phase.md で、この文書は 2026-10-01 の状態の要約と手順である。command は全て repo の root
（`/home/awe/zedBSD-claude1`、または worktree の root）から実行する。`<W>` は作業の名前（例 `ws090-p016`）に置き換える。

## 1. ゴール

### 1.1 目標（ws.md、2026-09-29 ユーザー）

「スクロールやボタンなど、ウィジェットやコントロールを共有ライブラリにする。独自のものでよい。慣性スムーズスクロールは少なくともライブラリにして再利用したい。」
＋「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」＋ file chooser の sheet と不透明（2026-09-30）。

### 1.2 デモの場面（[master.md](../master.md) の fg010）

WS090 は場面を直に持たないが、移した app が場面で使われる。**移行でデモの app を壊さないことが最優先**（design.md J5）。

| 場面 | 使う app | WS090 で移したもの |
| --- | --- | --- |
| S6 text | Text Editor | 窓・touch（1 本指で選択・2 本指で scroll）・clipboard・file chooser（sheet）・text input（p004・p006・p013・p014） |
| S8 Notes | Notes | 窓（p011）。scroll は未（p015） |
| S9 PDF Viewer | PDF Viewer | 窓・file chooser・password の card の keyboard の inset（p008） |
| S5 画像 | Image Viewer | 窓・file chooser・全画面（p008） |
| S7 Settings | Settings | 未（p007） |
| S4 Files | Files | 未（p009・p010） |
| S10 Terminal | Terminal | 窓（p011）。p088（drop）の回帰が未切り分け（p016 提案） |
| S14 キーボード | Text Editor | text input（p013）、keyboard の inset（KUI_VERSION 7、WS102） |

### 1.3 完了の定義

ws.md の Phase の表の全て（p007・p009・p010・p015・p012 を含む）が cleared。最後の p012 で規約の全文の照合と回帰（style-check 0、build warning 0、host 試験、boot test）。
design.md J5: **2026-10-10 までに移し終えた app だけ移す**。間に合わない app の移行は merge しない（Phase は uncleared のまま、デモの後に再開）。
p009・p010（Files）をデモの後に回すかは §3 の判断 J-A（人間）。

## 2. 今の状態

### 2.1 済み（証拠）

| Phase | 内容 | 証拠 |
| --- | --- | --- |
| p001〜p006 | 設計、描画の層、scroll・入力・文字の touch、窓の土台と Text Editor、部品と kuidemo、file chooser を libkeiui へ | 各 phase.md。KUI_VERSION 2〜5、KEILAND_VERSION 16 |
| p008 | PDF Viewer・Image Viewer を `kui_window` と `kui_file_chooser` へ（KUI_VERSION 10） | [phase008](phase008/phase.md): demo-s8-s9 前後 PASS、`viewers-p008.sh` PASS、Image Viewer の guest と touch PASS、C9、boot |
| p011 | Terminal・Notes の窓を `kui_window`（`KUI_PRESENT_NONE`）へ（KUI_VERSION 11） | [phase011](phase011/phase.md): 統合の試験 demo-s8-s9 PASS（ユーザーの指示で合格）。Terminal の p088 は FAIL のまま未切り分け |
| p013 | `kui_window` の text-input-v3、Text Editor（KUI_VERSION 6） | [phase013](phase013/phase.md): `textinput-p013.sh` PASS |
| p014 | file chooser を親の title bar の下の sheet に、不透明（Titlebar の mode 3 SHEET、KEILAND_VERSION 20） | [phase014](phase014/phase.md): `sheet-guest.sh` PASS、C9 10/10、boot |
| host（2026-10-01、この手引きの作成で再実行） | `host-input: 63/63 passed`・`host-widgets: 94/94 passed`（約 6 秒）・`host-draw: 13/13 passed`・`host-chooser: 85/85 passed`・`host-core: 34/34`・`host-termtouch: ok (20 checks)`・`host-notestouch: ok (52 checks)`・`run-pdfviewer-host: ok` | — |

今の版: `KUI_VERSION 11`（`include/libc/keiui.h:51`）、`KEILAND_VERSION 20`（`include/libc/keiland.h:49`）。

### 2.2 残り

| Phase | Status | 止めている物 |
| --- | --- | --- |
| p016（提案、下） | — | なし |
| [p015](phase015/phase.md) | planning | なし（p011 は cleared）。WS081 の試験 2 本の変更に main の許可 |
| [p007](phase007/phase.md) | planning | **WS089 の完了**（と WS104 の settings の Phase との順） |
| p009・p010 | planning | **WS094 の完了**（Files に `--desktop`、2026-09-29 main）。WS094 は incomplete（p011・p012・p007 が planned、p009 が uncleared） |
| p012 | planning | 全て |

p011・p008・p013・p014 の残り（各 phase.md の「残り」）: password の card を `kui_dialog`＋`kui_field` に、Terminal の clipboard・PRIMARY を `kui_window` へ、
`kui_field` の text input、親の無い chooser の画面の確かめ、sheet の間の親への hover・wheel・touch の押下。どれも後の候補で、この手引きでは Phase にしない。

### 2.3 既知の bug（[known-bugs.md](../known-bugs.md)）

| Bug | 状態 | WS090 との関係 |
| --- | --- | --- |
| [BUG-125](../bugs/BUG-125.md) C9 の p076 が全体の実行で時々 FAIL | reproduced / tracking | 回帰の C9 で p076 だけ落ちたら `C9_TESTS="p076"` で流し直す（ws090-p008 の前例）。WS090 の失敗と数えない |
| [BUG-111](../bugs/BUG-111.md) key の repeat が dispatch の前に発火 | resolved | `kui_window_repeat` は dispatch の後に呼ぶ（`keiui.h` の kui_window の注釈）。Settings の移行（p007）で守る |
| [BUG-112](../bugs/BUG-112.md) xdg_wm_base.destroy の protocol error | resolved | chooser の開閉（p006 で回避なしを確かめた） |
| [BUG-099](../bugs/BUG-099.md) touchinject の replay の行 | unreproduced / tracking | touch の guest 試験で 1 回だけ出たら、この bug の `SCRIPT.failed.log` |

## 3. 次の作業の順番

master の優先（2026-09-30 夜）: WS099・WS079・**WS090**・WS089…。2026-10-10 ごろから bug の修正と実機の調整だけ（master「デモまでの進め方」）。
p007・p009・p010・p015 は app ごとに独立で、途中で止めても他の app は動く（design.md §9）。

| 順 | Phase | 目的 | 条件 | 見込み |
| --- | --- | --- | --- | --- |
| 1 | **ws090-p016（提案）** | Terminal の p088（drop）の FAIL の切り分け（p011 の残り） | なし | 小。guest の試験 2〜3 本 |
| 2 | [ws090-p015](phase015/phase.md) | Terminal・Notes の scroll を `kui_scroll` へ | 10/10 までに終わる見込みのときだけ。WS081 の試験の変更は main の許可 | 中 |
| 3 | [ws090-p007](phase007/phase.md) | Settings を libkeiui へ | WS089 の完了の後、WS104 の p001〜p003・p007 の後（または main が順を決める）。10/10 まで | 中〜大 |
| 4 | ws090-p009・p010 | Files | WS094 の完了。**判断 J-A** | 大（Files は 2.9 万行） |
| 5 | ws090-p012 | 規約の全文と回帰 | 全て（デモの前に止めた Phase は除いて、移した分だけで行うかは main の判断） | 中 |

**判断 J-A（人間）**: design.md J5 では 10/10 までに移し終えた app だけ移す。今日（10/01）から見て、Files の移行（p009・p010）は WS094 の完了を待つので
10/10 までに済む見込みが低い。「p009・p010 をデモの後に回す」をユーザーに確かめる（エージェントは決めない。決まるまで p009・p010 を始めない）。

### 3.1 ws090-p016（提案）: Terminal の p088 の切り分け

- 背景: p011 の Terminal の回帰で `zdesktop-p088.sh` だけ FAIL（Files から Files の drag の段と Terminal への drop の段がともに MISSING）。
  p011 は「この image に p088 の前提の sample home が無い可能性」と書いた。
- 2026-10-01 の調べ: `zdesktop-p088.sh:90` は guest の `/usr/share/files-tests/make-home.sh` で `/tmp/fhome` を作る。p011 が使った WS079 の demo の image は
  `demo-s8-s9.sh:78-80` の build で guest の harness の file だけを入れ、`make-home.sh` を入れない。files の image（`plan/tools/files/build-files-image.sh`）は入れる。
  → **前提の欠けの見込みが高い**（未確認）。p088 は files の image（`plan/tools/files/config-amd64-files.mk` は terminal を含む:
  `plan/tools/titlebar/config-amd64-menu.mk` の `terminal`）で流すべき試験。
- 手順:
  ```
  mkdir -p build/ws090-p016
  sh plan/tools/files/build-files-image.sh build/ws090-p016-files > build/ws090-p016/files-build.log 2>&1; echo "exit=$?"
  GUEST_RUNTIME=$PWD/build/ws090-p016-run sh plan/ws035/tests/zdesktop-guest.sh start build/ws090-p016-files/hdd-image.img
  GUEST_RUNTIME=$PWD/build/ws090-p016-run sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
  GUEST_RUNTIME=$PWD/build/ws090-p016-run sh plan/ws035/tests/zdesktop-p088.sh build/ws090-p016/p088 > build/ws090-p016/p088.log 2>&1; tail -1 build/ws090-p016/p088.log
  GUEST_RUNTIME=$PWD/build/ws090-p016-run sh plan/ws035/tests/zdesktop-guest.sh stop
  ```
  PASS: `zdesktop-p088: PASS`（`zdesktop-p088.sh` の最後の行）。この image の terminal は今の main の物（p011 の後の `kui_window` の Terminal）。
- FAIL のとき: 前の Terminal（p011 の前、commit 31d88d38 の親）の `/bin/terminal` を `git worktree` の build で作り、`plan/tools/guest/guest.py put` で
  guest の `/bin/terminal` を入れ替えて同じ試験を流す（`GUEST_RUNTIME` を同じにする）。前で PASS・今で FAIL なら p011 の退行 → Bug を立てる
  （AGENTS.md「bug」、bug-analyzer）。両方 FAIL なら試験か zdesktop の側。log は `build/ws090-p016/p088/` の画面と、試験が SSH で読む `ZTERM DROP`・`DROP ask` の行。
- 受け入れ: p088 の結果と原因（前提の欠け／退行／他）を p011 の phase.md の「残り」に追記し、退行なら Bug の ticket。

### 3.2 p015・p007

手順と完了の条件は各 phase.md（2026-10-01 に作った）: [phase015/phase.md](phase015/phase.md)・[phase007/phase.md](phase007/phase.md)。

### 3.3 p009・p010（Files、判断 J-A の後）

design.md §10: p009 は描画の層（canvas・text・icons・theme）と scroll view、p010 は field・list・sidebar・dialog・chip と `kui_window`。受け入れは
Files の既存の試験（`plan/tools/files/files-regress.sh` の 14 本、host の `host-build.sh`・`host-run.sh`・`host-p0*.sh`）と画面が前と同じ。
WS094 が Files の source（desktop の icon、`--desktop`）を触っている間は始めない。始める時に phase.md を作り、p007 と同じ形（前の絵を host で取り、
byte で比べる）にする。Files の host の描画は `sh plan/tools/files/host-build.sh` と `sh plan/tools/files/host-run.sh [--fresh] draw=NAME.ppm ...`（script の頭）。

## 4. 未知と調べ方

| # | 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- | --- |
| U1 | p088 の FAIL の原因 | p011 の残り、S10 の drop | §3.1 |
| U2 | `kui_scroll` を `-Wconversion` で compile できるか | p015 で WS081 の試験の compile の列に入れる | **2026-10-01 に確かめた: 今の scroll.c は通る**（`clang -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -Ibuild/ws090/inc -c userland/desktop/libkeiui/scroll.c -o /dev/null` が clang・cc とも exit 0。`build/ws090/inc` は `host-input.sh` が作る）。p015 で足した後にもう一度流す |
| U3 | Terminal の負の向きの scroll（`touch.c:455` の `set_bounds(0,0,top,0,1,height)`）を `kui_scroll` で表す形 | p015 の API | phase015 の手順 3 の `kui_scroll_set_bounds`。`host-input.c` に負の最小の試験を先に書く |
| U4 | Settings の glass の panel の座標・key の repeat が `kui_window` で変わらないか | p007 | phase007 の「未知」 |
| U5 | Files の移行の大きさと 10/10 の間に合い | 判断 J-A | `wc -l userland/desktop/files/*.c`、design.md §1.1 の表（canvas 1396 行ほか）。WS094 の残り（[ws094 の ws.md](../ws094/ws.md)） |
| U6 | 5330 の実機での移した app の動き | 実機は未実施（全 Phase） | §6 |

判定に QEMU の console・serial の log を使わない（AGENTS.md）。guest の program の log は SSH（`plan/tools/guest/guest.py run`）で読む。

## 5. コマンド

一般の build（§1）・1 つの library の build（§3、例 `make -j64 build/amd64/dynamic/libkeiui.so`）・boot test（§4）・WS099 の C9（§5）・Settings（§8）は
[plan/ws104/commands.md](../ws104/commands.md) を使う。ここは WS090 の物。

### 5.1 host の試験（Linux、各数秒〜30 秒）

```
sh plan/ws090/tests/host-draw.sh
sh plan/ws090/tests/host-input.sh
sh plan/ws090/tests/host-widgets.sh
sh plan/tools/keiui/host-chooser.sh
sh plan/tools/textedit/host-core.sh
sh plan/ws102/tests/host-inset.sh
sh plan/ws081/tests/run-termtouch.sh
sh plan/ws081/tests/run-notestouch.sh
sh plan/ws079/tests/run-pdf-render.sh 100
sh plan/ws079/tests/run-pdfviewer-host.sh
sh plan/tools/imageview/run-host.sh
```

- PASS（2026-10-01 の値）: `host-draw: 13/13 passed`、`host-input: 63/63 passed`、`host-widgets: 94/94 passed`、`host-chooser: 85/85 passed`、`host-core: 34/34`、
  `host-inset: PASS`、`host-termtouch: ok (20 checks)`、`host-notestouch: ok (52 checks)`、`run-pdf-render: ok`、`run-pdfviewer-host: ok`、
  `host-imageview: PASS`（最後の 2 本の行も 2026-10-01 に確かめた）。
- 絵は `build/ws090-shots/`・`build/keiui-shots/`。出力の directory は固定の名前（`build/ws090/host-*` など）なので 2 つの agent で同時に流さない。

### 5.2 guest の試験（QEMU の Venus）

S8・S9 の通し（Notes・PDF Viewer の回帰）は WS079 の [guide.md](../ws079/guide.md) §5.2（`demo-s8-s9: PASS`）。その image（`build/<W>-demo/hdd-image.img`）で:

```
GUEST_RUNTIME=$PWD/build/<W>-viewers-run sh plan/ws090/tests/viewers-p008.sh build/<W>-demo/hdd-image.img build/<W>/viewers build/amd64 > build/<W>/viewers.log 2>&1; tail -1 build/<W>/viewers.log
```

- PASS: `viewers-p008: PASS`。第 3 引数の BIN（既定 `build/amd64`）から imageview と画像の library を guest に入れる（script の頭）。今の tree で build した BUILD を渡す。

Text Editor の text input（IME の image。script が IMAGE を省くと `build/amd64` に作って上書きするので、必ず IMAGE を渡す）:

```
extra=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
make -j64 ZEDBSD_CONFIG=plan/ws090/tests/config-amd64-textinput.mk BUILD=build/<W>-ti "ZEDBSD_TEST_EXTRA_FILES=$extra" disk-image > build/<W>/ti-build.log 2>&1; echo "make exit=$?"
GUEST_RUNTIME=$PWD/build/<W>-ti-run sh plan/ws090/tests/textinput-p013.sh build/<W>-ti/hdd-image.img build/<W>/textinput > build/<W>/textinput.log 2>&1; tail -1 build/<W>/textinput.log
```

- PASS: `textinput-p013: PASS`（`textinput-p013.sh:112`）。
- 注意: `textinput-p013.sh:25-32` は IMAGE を省くと `BUILD=build/amd64` で作り、既定の image を上書きする。上の make は script の中の command（guest の harness の
  file だけを足す）と同じ形を自分の BUILD にした物（2026-10-01 に script を読んで確かめた。build は未実行）。WS095 の `config-amd64-ime.mk` の上に作る。

file chooser の sheet（Text Editor。guest を先に起こす。`sheet-guest.sh` の既定の runtime は **WS094 の `build/ws094-run`** なので必ず自分の物を渡す）:

```
GUEST_RUNTIME=$PWD/build/<W>-sheet-run sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-demo/hdd-image.img
GUEST_RUNTIME=$PWD/build/<W>-sheet-run sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
GUEST_RUNTIME=$PWD/build/<W>-sheet-run BIN=build/amd64 sh plan/ws090/tests/sheet-guest.sh build/<W>/sheet install open move hold dock minimize saveas > build/<W>/sheet.log 2>&1; tail -1 build/<W>/sheet.log
GUEST_RUNTIME=$PWD/build/<W>-sheet-run sh plan/ws035/tests/zdesktop-guest.sh stop
```

- PASS: `sheet-guest: PASS`（`sheet-guest.sh:159`）。`install` は BIN の compositor・Text Editor・library を guest に入れる（新しい guest では全ての `.so` を
  入れないと `ld.so: undefined symbol`、p014 の記録）。

Terminal の回帰（files の image。§3.1 の image と同じ）:

```
for t in p079 p093 p100 p114 p086 p088; do
	GUEST_RUNTIME=$PWD/build/<W>-files-run sh plan/ws035/tests/zdesktop-guest.sh stop > /dev/null 2>&1
	GUEST_RUNTIME=$PWD/build/<W>-files-run sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-files/hdd-image.img > /dev/null 2>&1
	GUEST_RUNTIME=$PWD/build/<W>-files-run sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240 > /dev/null 2>&1
	sleep 10
	GUEST_RUNTIME=$PWD/build/<W>-files-run sh plan/ws035/tests/zdesktop-$t.sh build/<W>/$t > build/<W>/$t.log 2>&1
	echo "$t: $(grep -E ': PASS|: FAIL|status=' build/<W>/$t.log | tail -1)"
done
GUEST_RUNTIME=$PWD/build/<W>-files-run sh plan/ws035/tests/zdesktop-guest.sh stop
```

- p011 が使った形（試験ごとに guest を起こし直す、`.claude/worktrees/ws090-kui/build/ws090/zterm-guest.sh`）を repo の root の command にした物。
  PASS: 各行の `p079: PASS`・`zdesktop-p093: PASS`・`zdesktop-p114: PASS`・`zdesktop-p086: PASS`・`zdesktop-p088: PASS`。p100 は最後の行が
  `zdesktop-p100: status=0`（loop の grep の `status=` で拾う）。1 本 2〜10 分の見込み（未計測）。

### 5.3 回帰の組（AGENTS.md「検証」: 変えた領域の試験）

| 変えた所 | 流す物 |
| --- | --- |
| libkeiui の描画・部品 | §5.1 の host-draw・host-widgets・host-chooser、移した app の host 試験 |
| libkeiui の scroll・input・text-touch | §5.1 の host-input・host-core・WS081 の 2 本、guest の S8・S9 |
| libkeiui の窓（`window.c`） | §5.1 全て、§5.2 の S8・S9・viewers・textinput・sheet・Terminal、WS099 の C9（commands.md §5） |
| app 1 つの移行 | その app の host 試験と guest 試験（前後で同じ PASS と同じ画面）、S8・S9、C9 |
| 全て | build warning 0（commands.md §1）、boot test（commands.md §4）、`python3 plan/tools/style-check.py <変えた file>` 0、`git diff --check` 0 |

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（§1.4〜1.6 passthrough の画面・入力・結果の読み戻し、§1.7 lock、§2 USB の単独の起動、§3 デモの image、§4.3 passthrough の smoke）。WS090 は場面を直に持たないので、
**移した app の場面を実機で確かめる**のが WS090 の実機の確認である（全 Phase で未実施）。

| # | 確かめ | 手順 | 誰 |
| --- | --- | --- | --- |
| H1 | Text Editor の file chooser が title bar の下の sheet で開き、不透明、Open・Save As・取り消し | 5330 の demo の image（`sh plan/ws075/demo/build-demo-image.sh build/<W>-demo-hw`）で Files から txt → Text Editor → Ctrl+O・Ctrl+Shift+S | ユーザー |
| H2 | PDF Viewer・Image Viewer の chooser・全画面・頁送り | WS079 の [demo-s8-s9-manual.md](../ws079/demo-s8-s9-manual.md) の S9 と、Image Viewer の F（全画面）・Esc | ユーザー |
| H3 | Notes・Terminal の窓（p011）: Notes の全画面の起動、Terminal の key の repeat・wheel（notch）・選択 | S8 と S10 の台本 | ユーザー |
| H4 | passthrough での自動の確かめ（任意） | WS079 の guide.md §3.1（p017 提案）と同じ型。WS090 だけの hw の試験は作らない | エージェント |

touch（1 本指の選択・2 本指の scroll・sheet の touch）は Windows の QEMU で見せる（master 2026-09-30 朝ユーザー）。証拠は QEMU・passthrough・単独の実機を分けて書く。

## 7. 注意

- **J5（デモ）**: 2026-10-10 までに移し終えない移行は merge しない。デモの app（Text Editor・PDF Viewer・Image Viewer・Notes・Terminal・Settings・Files）を壊さない。
  10/10 以後は bug の修正と実機の調整だけ（master）。
- **版**: KUI_VERSION・KEILAND_VERSION を上げる Phase は、適用の直前に main の値を確かめ main の最新の次にする（design.md §8）。2026-10-01: KUI 11、KEILAND 20。
  ws104-p002 が KEILAND 21 を使う予定。報告に番号を明記する。
- **他の WS の file**（AGENTS.md「subagent の修正可能範囲」）: `platform/amd64/vmunix.mk`（app の link の規則）、`plan/ws079/tests/`・`plan/ws081/tests/`・
  `plan/ws089/tests/`・`plan/tools/` の試験の script は読むだけで、変更は main に依頼する（p008・p011 は Q1 が許可した前例）。
  `include/hal/hal.h`・toolchain に触れない。
- **runtime の衝突**: `sheet-guest.sh` の既定は `build/ws094-run`（WS094 の物）、`viewers-p008.sh` は `build/ws090/viewers-run`、`textinput-p013.sh` は
  `build/ws090/ti-run`、`zdesktop-p088.sh` などは `build/ws071-run`、`zdesktop-p079.sh` は `build/ws035-sq-run`（WS099 の `criteria.sh` が固定で使う）。
  いつも `GUEST_RUNTIME=$PWD/build/<W>-...` を付ける。`zdesktop-guest.sh stop` を `GUEST_RUNTIME` なしで呼ばない（最後に起こした他の guest を止める）。
- **既定の BUILD を上書きしない**: `textinput-p013.sh`・`demo-s8-s9.sh` は IMAGE を省くと `build/amd64` に作る。`build-*-image.sh` は BUILD を省くと `build/amd64`。
  image の build を 2 つ同時に走らせない（commands.md §0）。
- **WS104 の後の path の変更**: ws104-p001 が `include/libc/keiland.h`・`keiui.h`・`truetype.h`・wayland の header を `userland/desktop/keiland/` へ移す。
  WS090 の host の script（`host-draw.sh`・`host-input.sh`・`host-widgets.sh`、`plan/tools/keiui/host-chooser.sh`・`plan/tools/textedit/host-core.sh`、WS081 の
  `run-termtouch.sh`・`run-notestouch.sh`）の header の path は [plan/ws104/patches/p001-paths.patch](../ws104/patches/p001-paths.patch) で直る（§5.1 の command は
  そのまま）。その後は libkeiui の header の編集の場所が `userland/desktop/keiland/keiui.h` になる。ws104-p002・p003・p007 は settings・libkeiland・各 app の
  path の文字列に触れるので、p007（Settings）と p009・p010（Files）は WS104 と同時に走らせない。WS104・WS105 の Phase はこの WS の作業で触らない。
- **ユーザーの判断**（ws.md・master）: 文字の編集の view は 1 本指の drag が選択・2 本指が scroll（Files・Image Viewer の 1 本指の pan は変えない）、
  file chooser は libkeiui に、sheet（親が無ければ独立）、chooser は不透明、名前 `libkeiui`・`kui_`（画面に出さない）。
- **commit**: `git commit -m WIP -- <自分の path>...`。push しない。`.internal/` を読まない。集約の `make check` を走らせない。
