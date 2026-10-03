# WS102 作業の手引き（2026-10-01）

WS102（スクリーンキーボード、compositor に直接）を、追加の調査なしで続けるための手引き。記録の正本は [ws.md](ws.md)・[design.md](design.md)・各 phase.md。
「提案」の Phase は main（Q1）が ws.md に足すまで実行しない。Queue に入っていない Phase は実行しない（AGENTS.md）。
command は repo の root から。`<W>` は作業の名前（例 `ws102-p022`）。build と回帰の一般の command は [WS104 の commands.md](../ws104/commands.md)。

**再開の条件（2026-09-30 夕 ユーザー）**: WS102 は「優先を下げてラップアップ」。P3 の担当は終わり、再開は**ユーザーが言うとき**（ws.md の Resume point）。
この手引きの作業は、ユーザーが WS102 の再開を言ってから Queue に入れる。master の優先順位ではデモ critical の上位の 8 番目。

## ゴール

台本 fg010 の **S14**（右下の角の swipe で flick（日本語）、左下の角の swipe で QWERTY と手書きの面、Text Editor に打つ）を支える。
S14 は Windows の上の QEMU（WS085）の touch で見せる（master「デモの touch」2026-09-30 ユーザー）。5330 の実機は mouse と keyboard。

達成基準 K1〜K8（ws.md）と段（design.md §3）:

| 段 | 数値目標 | 状態 |
| --- | --- | --- |
| L1 | 右下の swipe 10/10 で開き真上 0/10、表の網羅（host）、「aiueo123」「あいうえお」が誤り 0、C9 PASS | 済み（p002〜p005） |
| L2 | QWERTY 30 文字を 5 文字/秒で誤り 0、2 本指 100 打鍵で取りこぼし 0、手書きの線が 1 frame 以内（QEMU は「最初の frame」で代えた）、最大化の窓が keyboard の上に収まる | 済み（p006〜p009・p015〜p018・p020・p021・p023・p024）。p024 の全手順の回帰が未実施 |
| L3 | 送出まで p95 ≤ 5 ms、app の frame まで p95 ≤ 50 ms、開く動きの frame の間隔 ≤ 20 ms、IME と組んだ変換 | 色の絵文字 p019 だけ済み。速さ（p010・p011）は**優先を下げた**（2026-09-30 午後 ユーザー「描画の高速化はラップアップ」）。IME（p012）は WS095 が人間の作業中 |
| L4 | Windows の QEMU で物理の touch で S14 が 3/3 | 未着手（p013、WS085 と WS081 の L2 の後） |
| 最後 | 全文の規約と回帰 | p014（planned） |

「完了」の具体: L1・L2 の全手順の回帰が最新の main で PASS、S14 の L4 が 3/3（ユーザー）、p014 cleared。L3 の速さと IME は、ユーザーが優先を戻さない限りデモの範囲の外（Future Work に移すかは main とユーザーが決める）。

## 今の状態（2026-10-01）

| 項目 | 状態 | 証拠 |
| --- | --- | --- |
| flick（かな・英字・数字・記号）、QWERTY、補助の key、手書き（stub） | QEMU PASS | [p003](phase003/phase.md)・[p006](phase006/phase.md)・[p020](phase020/phase.md)・[p008](phase008/phase.md) |
| 作業の領域・inset・caret の中央寄せ | QEMU PASS（実機は未実施） | [p007](phase007/phase.md)・[p015](phase015/phase.md) |
| 2 本指の連打（ROUTE_OSK） | QEMU PASS | [p009](phase009/phase.md) |
| 道具の面（編集・直前の app・履歴）、`keiland_edit_v1`、クリップボードの履歴 | QEMU PASS。p024 は受け入れの手順だけ PASS、**全手順の回帰・C9・boot test は未実施** | [p016](phase016/phase.md)・[p017](phase017/phase.md)・[p018](phase018/phase.md)・[p023](phase023/phase.md)・[p024](phase024/phase.md) |
| 色の絵文字（font・libtruetype・描画の fallback） | QEMU と host PASS | [p019](phase019/phase.md) |
| 絵文字の面（keyboard の tab） | 未実装。tab は無効（`userland/desktop/wayland/keyboard.c:3457-3459` が 0 を返す、`:3580-3582` は何もしない） | p022（planned） |
| 統合の試験 `demo-s8-s9.sh` | PASS（2026-09-30 夕 Q1、main 722de611） | p024 の「統合の試験」 |

既知の bug:

| Bug | 状態 | 関係 |
| --- | --- | --- |
| [BUG-125](../bugs/BUG-125.md) | tracking。C9 の p076 が全体の実行で時々 FAIL | WS102 の各 Phase の回帰の C9 で出た。直しは WS099 の提案 p017（[WS099 の手引き](../ws099/guide.md)）。直るまでは p076 だけ単独で流し直して記録する |
| [BUG-099](../bugs/BUG-099.md) | unreproduced / tracking（touchinject の replay の成否の行） | osk-guest の `touchinject … FAILED` が出たら、`/tmp/<名前>.script.failed.log` を `guest.py run` で読む |
| [BUG-124](../bugs/BUG-124.md) | tracking（Venus の swapchain の DEVICE_LOST） | Windows の QEMU のデモで出たら優先を上げる |

ユーザーの判断（ws.md）: D1「libkeiui に入れる」（Text Editor は libkeiui の text-input-v3 でかなを受ける）、D2「IME にかなの口を足す」（WS095 の人間の作業と調整、L3）。
D3・D4 は既定のまま。

## 次の作業の順番（再開の後）

1. **ws102-p024 の残りの回帰**（新しい Phase は要らない。p024 の phase.md に [手順](phase024/phase.md) を追記した）。
   全手順の osk-guest・1920x1080・WS079-p010・C9・boot test を最新の main で流し、p024 の「未実施」を埋める。FAIL が出たら uncleared の新しい試みとして記録。
2. **[ws102-p022](phase022/phase.md) 絵文字の面**（planned。手順は phase.md）。
3. **ws102-p013 L4（Windows の QEMU の物理の touch）**: WS085 の Windows の image に今の keyboard が入り、WS081 の L2（host の touch が Kei に届く）を
   ユーザーが確かめた後。エージェントはユーザーの手順の 1 枚（下の「Windows」）を渡し、結果を記録する。
4. **ws102-p014 全文の規約と回帰**（WS の最後）: WS102 の全ての source の変更（`keyboard.c`・`keyboard-layout.c`・`keyboard-hand.c`・`touch.c` の ROUTE_OSK・`inset.c`・`edit.c`・`clipboard.c`、
   libkeiland・libkeiui・libtruetype の `color.c`、`userland/desktop/picture/color-glyph.c`）を [coding-style.md](../coding-style.md) の全文で見直し、`plan/tools/style-check.py`、build、host 試験、osk-guest の全手順、C9、boot test。
5. 後回し（ユーザーが優先を戻したら）: p010・p011（L3 の速さ）、p012（IME と組んだ変換、WS095 が人間からエージェントに戻った後）、
   p018 の残り（text-input の purpose による除外: IME の file の許可が要る）。

## 未知と調べ方

| 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- |
| 絵文字の面の格子の大きさで色の glyph が読めるか（CBDT は 136×128 の bitmap の縮小） | p022 の見た目。COLRv1 に替えるかの判断（p019 §1） | p022 の画面を 1280x800 と 1920x1080 で撮り、key の大きさ（`ZWL OSK` の rect の log）で 24 px と 36 px の glyph を比べる。host は `plan/ws102/tests/host-emoji.sh` |
| Text Editor（libkeiui の text-input-v3）が絵文字の commit を受けるか | p022 の受け手 | p004 の `send` の手順（Text Editor にかなを送る所、osk-guest.sh の `send`）と同じ形で絵文字を送り、保存の file の byte を `guest.py run 'od -c /root/e.txt'` で見る |
| p076 の不安定（BUG-125）が ROUTE_OSK（p009）の後に増えたか | WS102 の変更が原因なら WS102 の bug | WS099 の提案 p017 の `--log-frames` の読み。p009 の前後の compositor で 20 回ずつ比べる（`git log --oneline -- userland/desktop/wayland/touch.c` で p009 の commit を見つけ、その前の `bin/wayland` を `zdesktop-p076.sh` の guest に入れる） |
| Windows の物理の touch で角の swipe が取れるか（28×28 の角の区域） | L4。指は mouse より太い | ユーザーの試し（L4）。取れなければ `keyboard.c` の角の区域の大きさ（`grep -n "zone kind" userland/desktop/wayland/keyboard.c`）を touch だけ広げる Phase を提案 |
| guest の zdesktop が止まる・落ちる | 試験の FAIL の切り分け | QEMU の console・serial の log では判定しない。`/tmp/zdesktop.log` の `ZWL OSK`・`ERROR` を `guest.py run` で読む、画面は QMP（`zdesktop-shot.py`）、kernel は `guest.py kgdb --symbols <BUILD>/vmunix` |

## コマンド

### build

WS102 の guest の試験は「guest の image」と「入れる compositor と app の BUILD（`BIN`）」の 2 つを使う（`osk-guest.sh:48-53`）。`install` の手順が BIN の
`bin/wayland`・`bin/textedit`・`bin/ime-probe`・`bin/wltest` と `dynamic/*.so`（libc・ld を除く）、色の絵文字の font を guest に入れる（`osk-guest.sh:196-209`）。

```
mkdir -p build/<W>
sh plan/ws102/tests/build-inset-image.sh build/<W>-inset > build/<W>/inset-build.log 2>&1; echo "exit=$?"
make -j64 ZEDBSD_CONFIG=plan/ws102/tests/config-amd64-inset.mk BUILD=build/<W>-inset build/<W>-inset/bin/ime-probe >> build/<W>/inset-build.log 2>&1; echo "exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/<W>/inset-build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
ls build/<W>-inset/bin/wayland build/<W>-inset/bin/textedit build/<W>-inset/bin/ime-probe build/<W>-inset/bin/wltest
```

- inset の image（`config-amd64-inset.mk` = WS079 の demo の image（注入の touch・pen、touchinject、Notes、PDF Viewer、wltest）+ Text Editor）は、
  これまでの「pen の image（`build/main-pen`）の複写」の代わりになる（pen の image は 9/29 の古い物で、fallback の字体が無い: p003 の注記）。
  ime-probe は構成に無いので 2 行目で同じ BUILD に足す（`make -n` で target があることを確かめた）。
- 2 行目の後に image を作り直さない（ime-probe は osk-guest の `install` が guest に入れる）。

### host の試験（guest 無し、各 1 分以内）

```
sh plan/ws102/tests/host-keyboard.sh
sh plan/ws102/tests/host-inset.sh build/<W>/host-inset
sh plan/ws102/tests/host-emoji.sh
```

- PASS: どれも exit 0（`host-keyboard: PASS`（`host-keyboard.c:203`）、`host-emoji` は `build/distfiles/NotoColorEmoji-2.047.ttf` が要る: 無ければ `make noto-color-emoji-download`）。

### guest の試験（QEMU の Venus、osk-guest.sh）

```
export GUEST_RUNTIME=$PWD/build/ws102-run
sh plan/ws079/tests/pen-guest.sh start build/<W>-inset/hdd-image.img
sh plan/ws079/tests/pen-guest.sh wait --timeout 240
BIN=build/<W>-inset sh plan/ws102/tests/osk-guest.sh build/<W>/osk-all install start pointer flick edges touch send close qwerty hand extra workarea roll tools history
sh plan/ws079/tests/pen-guest.sh stop
unset GUEST_RUNTIME
```

- PASS: 最後の行 `osk-guest: PASS`（`osk-guest.sh:852`）。各確かめは `ok`／`MISSING`・`FAIL` の行。画面は `build/<W>/osk-all/*.png`、log の抜き出しは `osk-log.txt`。
- `pen-guest.sh start` は `zdesktop-guest.sh start` を呼び、image の複写で起動する（元の image は変わらない）。`GUEST_RUNTIME` を必ず揃える（osk-guest の既定は `build/ws102-run`、`osk-guest.sh:52`。pen-guest の既定は `build/ws079-run`）。
- 時間: 全手順で約 20〜25 分（未計測、手順ごとに zdesktop を起こし直す）。
- 1920x1080: guest を `VENUS_SIZE=1920x1080` で起こし直し、`OSK_WIDTH=1920 OSK_HEIGHT=1080 BIN=build/<W>-inset sh plan/ws102/tests/osk-guest.sh build/<W>/osk-large install large`。
- 他の WS102 の guest の試験（使い方は各 script の先頭）: `inset-guest.sh IMAGE OUTDIR`・`edit-guest.sh IMAGE OUTDIR`（inset の image）、`clip-lock.sh IMAGE OUTDIR`（criteria の image、runtime `build/ws102-p018-run`）、
  `clip-guest.sh`・`edit-state-guest.sh`。

### Phase の回帰の組（keyboard.c など compositor を変えた Phase）

1. build（warning 0）と `python3 plan/tools/style-check.py userland/desktop/wayland/keyboard.c`（変えた file）。
2. host の試験の 3 本。
3. osk-guest の全手順（上）と 1920x1080 の `large`。
4. WS099 の C9（[WS099 の手引き](../ws099/guide.md) の criteria.sh、criteria の image）と WS079-p010（`plan/ws079/tests/zdesktop-p010.sh BUILD OUTDIR`、criteria の image の guest）。
5. boot test（[commands.md §4](../ws104/commands.md)）。PNG をユーザーに見せる。

## 実機（Dell Latitude 5330）と Windows

- 5330 の素の起動では keyboard は mouse で開ける（右下・左下の角の drag）。一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)。WS102 に固有の 5330 の試験は今は無い
  （作業の領域と inset の実機の確認が未実施: p007・p015）。確かめるなら 5330 の demo の image（`sh plan/ws075/demo/build-demo-image.sh build/<W>-demo`）で、ユーザーに
  「左下の角から内へ drag → QWERTY で Text Editor に打つ → 右下から flick」を見てもらう。
- **Windows の QEMU（L4、S14 の本番）**: image は WS081 の `plan/ws081/tests/build-demo-win.sh`（Text Editor 入り）。複写と起動は [WS081 の windows-touch.md](../ws081/tests/windows-touch.md) の「用意」。
  ユーザーの手順（p013 で渡す）: (1) boot.bat で起動、(2) 右下の角から指で内へ swipe し flick で「あいうえお」、(3) 左下の角から swipe し QWERTY で「Hello」、
  手書きの面に切り替えて 1 文字書く、(4) Text Editor に入ったかを見る。3 回通して報告。

## 注意

- **ユーザーの指示: 再開はユーザーが言うとき**（2026-09-30 夕）。IME（WS095）の file（`ime.h`・`text-input.c`・`input-method.c`、seat.c の IME の hook）は変えない（人間が作業中）。
- 速さ（p010・p011）は優先を下げた（2026-09-30 午後）。速さのために keyboard.c を最適化しない。
- `criteria.sh`（C9）は runtime `build/ws035-sq-run` に固定（`plan/ws099/tests/criteria.sh:33`）。osk-guest（`build/ws102-run`）とは別なので同時に走るが、host の CPU が足りずに時間の判定が揺れる。回帰は 1 本ずつ。
- image の build は同時に 1 つ。BUILD を必ず渡す（`build-inset-image.sh` の既定は `build/amd64`、`:10`）。
- `osk-guest.sh` の座標は 1280x800 の縁の配置（p021）に合わせてある。panel の配置を変えたら全手順の座標を直す（p021 の記録）。
- 2026-10-01 から WS104 が compositor の file を動かす（OS の境界）。keyboard.c に触る前に main に確かめる。
- 2026-10-10 ごろ以降は bug の修正と実機の調整だけ。
- commit は自分の path だけ `git commit -m WIP -- <path>...`。push しない。
