# WS081 作業の手引き（2026-10-01）

WS081（touch の操作の質: 慣性のある scroll と、低い fps の touch の補間）を、追加の調査なしで続けるための手引き。記録の正本は [ws.md](ws.md)・[design.md](design.md)・各 phase.md。
「提案」の Phase は main（Q1）が ws.md に足すまで実行しない。Queue に入っていない Phase は実行しない（AGENTS.md）。
command は repo の root から。`<W>` は作業の名前（例 `ws081-p017`）。build と回帰の一般の command は [WS104 の commands.md](../ws104/commands.md)。

## ゴール

- 目標（2026-09-28 ユーザー、ws.md）: (1) touch と touchpad の慣性の scroll をブラウザ・Files・Terminal・PDF Viewer 等で一貫した手触りに、(2) 低い fps の安い touch panel でも
  数式の補間・予測で滑らかに、(3) HID の driver・compositor・app の担当を 1 か所で設計。
- fg010 の場面: **S4**（Files の touch の scroll）、**S8**（Notes の指の scroll・pinch、「指で書く」）、**S9**（PDF Viewer の page mode の swipe・pinch）、**S14**（keyboard の多指、WS102）。
- デモの touch は **Windows の上の QEMU（WS085 の WINQ-EMU、usb-multitouch）** で見せる（2026-09-30 朝 ユーザー）。5330 の実機は mouse と keyboard。外付けの touch LCD は「間に合えば別の計画」。

| 段 | 数値目標 | 状態 |
| --- | --- | --- |
| L1 | Linux の QEMU の注入の touch で tap・double tap・長押し・慣性の scroll・pinch が 6 app で動く | 済み（p001〜p006・p010〜p015） |
| L2 | Windows の QEMU で host の touch が Kei に届く。報告の率 ≥ 60 Hz、欠け 0（10 秒の drag） | 道具と image は済み（p016・p018）。**ユーザーの Windows での計測が未実施** |
| L3 | Windows の QEMU で指を置いてから画面の反応まで p95 ≤ 50 ms、慣性の scroll の frame の間隔の最大 ≤ 33 ms | 未着手（[p017](phase017/phase.md)）。2026-09-30 午後のユーザーの判断で「WS081 の L3 は後ろ」 |
| L4 | 外付けの touch LCD | p007（planning、ユーザーが別に計画） |
| 最後 | 全文の規約と回帰 | p009（planning） |

「完了」の具体: L2 を Windows で満たし（ユーザーの SUMMARY）、L3 を満たすか、ユーザーが L3 を止める判断をし、p009 が cleared。

## 今の状態（2026-10-01）

| 項目 | 状態 | 証拠 |
| --- | --- | --- |
| kernel: 1 報告 1 時刻、Scan Time の `MSC_TIMESTAMP` | 済み | [p002](phase002/phase.md) |
| 補間・予測の library（`userland/desktop/libkeiland/motion.c`）、慣性の scroller と gesture（`scroll.c`・`gesture.c`） | 済み。host 試験 PASS（2026-10-01 にこの手引きの作成で `run-scroll.sh`・`run-motion.sh` を流し直して ok） | [p003](phase003/phase.md)・[p005](phase005/phase.md) |
| compositor の resampling・予測・指の drag and drop | 済み | [p004](phase004/phase.md)・[p014](phase014/phase.md) |
| app: Files・Terminal・PDF Viewer・Notes・ブラウザ | 済み（Linux の QEMU の注入） | [p010](phase010/phase.md)・[p011](phase011/phase.md)・[p012](phase012/phase.md)・[p013](phase013/phase.md)・[p015](phase015/phase.md)・[p006](phase006/phase.md) |
| L2 の道具 `touchlog`・`touch-latency.py`・ユーザーの手順 | 済み（Linux の QEMU で 60 Hz・90 Hz・欠け 3 を正しく数えた） | [p016](phase016/phase.md)、[windows-touch.md](tests/windows-touch.md) |
| Windows 用の demo の image | `build/ws081-demo-win/hdd-image.img`（2026-09-30 11:06、main の checkout にある）。**WS103（2026-10-01 完了）より前の build** | [p018](phase018/phase.md) |
| Windows の計測（L2） | 未実施（ユーザー） | — |

既知の bug: [BUG-099](../bugs/BUG-099.md)（unreproduced / tracking、touchinject の replay の成否の行。診断の出力は p013 で入れた。再び出たら `SCRIPT.failed.log` を読む）。
[BUG-101](../bugs/BUG-101.md)（WS085: Windows の表示の所有者の切替で QEMU が止まった件、未検証）は Windows での計測の前提に関わる。

ws.md の段の表の L3 の「p018（係数の調整）」の ID は、Windows の image の Phase（p018）に使われた。係数の調整は下の提案 p019 にする（main が ws.md の段の表を直す）。

## 次の作業の順番

1. **L2 の計測（ユーザー）とその判定**。エージェントの用意:
   1. Windows 用の image を作り直す（WS103 の compositor の変更を入れるため）: 下の「コマンド」の `build-demo-win.sh`。
   2. image か Kei-nightly の zip をユーザーに渡す（下の「Windows に渡す」）。
   3. ユーザーが [windows-touch.md](tests/windows-touch.md) の「用意」と「3 行」を行い、`SUMMARY` の 6 行を送る。
   4. 判定は windows-touch.md の「結果の読み方」の表（`rate_hz` ≥ 60、`scan_gaps` 0、`arrival_gaps` 0、median ≤ 16.7 ms）。結果は [p017](phase017/phase.md) の「前提: L2 の結果」に書き、ws.md の段の表の L2 に記録。
   - L2 が不合格なら p017 に進まず、原因の Phase を提案する（`scan_gaps` が多い → QEMU の fork の SDL の間引き: WS085 の担当。`arrival_gaps` だけ → Kei の USB の poll: `src/drivers/usb/hid-touch.c`・`usb-hid.c` の touch の経路を調べる Phase）。
2. **[ws081-p017](phase017/phase.md) L3 の計測**（手順は phase.md に追記した）。ユーザーの 2 回目の Windows の操作が要る。
3. **ws081-p019（提案）係数の調整**: p017 が L3 を満たさない時だけ。design §3.9 の既定（`motion.c:38-112`・`scroll.c:33-60` の `#define`）と design §10 の 1〜3 の
   ユーザーの判断（`E`、連続の fling の加速、`τ`・`μ`）を、p017 の Windows の log に合わせて直す。host の試験（`run-motion.sh`・`run-scroll.sh`・各 app の `run-*touch.sh`）を全て流し直し、
   p017 の計測をもう一度。係数を変えるなら design §10 の既定の案を変えることになるので、ユーザーに値と理由を示して決めてもらう。
4. **ws081-p009 全文の規約と回帰**（WS の最後、必須）: WS081 の全ての source の変更（kernel の HID の Scan Time、input の層、libkeiland の motion・scroll・gesture、compositor の touch.c・data.c、
   Files・Terminal・PDF Viewer・Notes・ブラウザの touch.c、Text Editor）を [coding-style.md](../coding-style.md) の全文で見直し、`plan/tools/style-check.py`、build、host の試験の全て、
   guest の試験の主な物（p010・p011・p012・p013・p015 の `pNNN-guest.sh`）、C9、boot test。
5. 判断待ち（ユーザー）: **touchpad の二本指の scroll**（design §10 の 4、既定の案「この WS に Phase を足す」。5330 の touchpad が USB・I2C-HID・PS/2 のどれかの確認が先）。
   ws.md「touchpad の二本指の scroll の Phase は、design §10 の 4 のユーザーの判断の後に置く」。デモは mouse なので急がない。ユーザーに聞く時は design §10 の 4 の文をそのまま示す。
6. p007（外付けの touch LCD）はユーザーが別に計画を立てるまで planning のまま。

## 未知と調べ方

| 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- |
| Windows の物理の touch panel の SDL の event が QEMU の usb-multitouch に届くか（WS085 では QMP の 2 指の注入だけ確かめた） | L2 の前提 | ユーザーの `touchlog --seconds=15` の SUMMARY。`touchlog: no touch screen` なら boot.bat の `-device usb-multitouch,bus=xhci.0,port=1`（windows-touch.md） |
| Windows の panel の報告の率（60 Hz 未満か） | 補間の係数（design §3.9）の前提 | 同上の `rate_hz`・`scan_intervals`。生の log が要る時はユーザーに `touchlog --seconds=15 --raw=/root/raw.txt` の後 `scp -P 2222 root@127.0.0.1:/root/raw.txt .` を頼む（`touchlog.c` の使い方、`:20`） |
| session の compositor を `--log-frames` で起こす方法 | p017 の `touch-latency.py` の入力 | session は `/etc/keiland/session`（`userland/desktop/sessiond/session.sh:16` の `exec /bin/wayland --session --glass …`）。p017 で `--log-frames` を足した session の script を image に入れる（p017 の手順） |
| Windows の WHPX の上の合成の間隔 | L3 の 33 ms の可否 | WS085 p001 の観測は「更新時 16〜34 ms/frame」。p017 の `ZWL COMPOSE … at_ms=` の間隔で測る |
| WS103（compositor の GPU の境界の移動）の後も Windows の表示と touch が動くか | 2026-09-30 の image より後の変更 | 作り直した image でユーザーの L2 の手順の 1 行目（desktop が出る）で分かる。動かなければ WS085 の担当（Windows の Venus の flags 15 の経路: `userland/desktop/libvulkan/context.c:170-196`・`wsi-display.c:818`） |
| Linux の QEMU での guest の不具合 | 試験の FAIL | QEMU の console・serial の log では判定しない。zdesktop の log（`/tmp/zdesktop.log`、session は `/run/user/1000/session.log`）を `guest.py run` で、画面は QMP、kernel は `guest.py kgdb --symbols <BUILD>/vmunix` |

## コマンド

### host の試験（guest 無し、各 2 分以内）

```
mkdir -p build/<W>
sh plan/ws081/tests/run-motion.sh build/<W>/motion
sh plan/ws081/tests/run-scroll.sh build/<W>/scroll
sh plan/ws081/tests/run-hid-scantime.sh build/<W>/hid
sh plan/ws081/tests/run-filestouch.sh build/<W>/files
sh plan/ws081/tests/run-termtouch.sh build/<W>/term
sh plan/ws081/tests/run-notestouch.sh build/<W>/notes
sh plan/ws081/tests/run-pdftouch.sh build/<W>/pdf
sh plan/ws081/tests/run-browsertouch.sh build/<W>/browser
```

- PASS: 各 script の最後の行が `host-<名前>: ok (N checks)` か exit 0（2026-10-01 に `host-scroll: ok (88 checks)`・`host-motion: ok (426009 checks)` を確かめた）。
- `run-browser-scroll.sh` は先に `sh plan/ws074/tests/host-build.sh plain`（WS074 の engine の object）が要る（script の先頭）。

### build

| 何 | command | 出力 |
| --- | --- | --- |
| touchlog だけ | `sh plan/ws081/tests/build-touchlog.sh build/amd64` | `build/ws081-tests/touchlog` |
| touchlog の試験の image（Linux の QEMU、pen の image + touchlog） | `sh plan/ws081/tests/build-touchlog.sh build/<W>-touchlog image > build/<W>/touchlog-build.log 2>&1; echo "exit=$?"` | `build/<W>-touchlog/hdd-image.img` |
| Windows の QEMU 用の demo の image | `sh plan/ws081/tests/build-demo-win.sh build/<W>-demo-win > build/<W>/demo-win-build.log 2>&1; echo "exit=$?"` | `build/<W>-demo-win/hdd-image.img`（約 2.2 GB） |

- **注意（`build-touchlog.sh:12`）**: 第 1 引数の BUILD の sysroot（`<BUILD>/sysroot/usr/lib/crt1.o`）が無いと、その BUILD に touchlog の image を丸ごと build する。
  `build-demo-win.sh` は中で `build-touchlog.sh build/amd64` を呼ぶ（`build-demo-win.sh:23`）。先に `ls build/amd64/sysroot/usr/lib/crt1.o` があることを確かめる。無ければ
  `make -j64 sysroot-amd64`（commands.md §2）を先に。これを怠ると `build/amd64/hdd-image.img` が touchlog の image で上書きされる。
- touchlog は `build/ws081-tests/` に固定で置かれる（全ての BUILD で共有）。
- image の build は同時に 1 つ。warning の確かめは commands.md §1 の grep。

### Linux の QEMU の試験

```
sh plan/ws081/tests/touchlog-check.sh build/<W>-touchlog/hdd-image.img build/<W>/touchlog-check
```

- 自分で guest を起こす（runtime `build/ws081/touchlog-run`、`touchlog-check.sh:19`）。PASS は最後の行 `touchlog-check: PASS`。約 10 分（未計測）。
- 各 app の guest の試験（`p010-guest.sh`〜`p015-guest.sh`）は pen の image の複写の上で動く（使い方は各 script の先頭、例 `p015-guest.sh:21-23`:
  `GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start <image>` の後）。元の手順の `/home/awe/zedBSD-rpi4/build/main-pen` は古い checkout なので、
  pen の image は `sh plan/ws079/tests/build-pen-image.sh build/<W>-pen` で作り直して使う。
- Windows の demo の image を Linux の QEMU で起動の確かめ: `OUTPUT=build/<W>/boot-demo-win plan/tools/boot-test.sh build/<W>-demo-win/hdd-image.img`（Venus の無い QEMU では console の login に戻るのが正しい、p018）。

### Windows に渡す

1. image のまま: `build/<W>-demo-win/hdd-image.img` をユーザーに知らせ、Windows の `C:\Work\winq-zedbsd\data\hdd-image.img` に上書きで複写してもらう（windows-touch.md の「用意」1）。
2. zip で（任意）: `make BUILD=build/<W>-demo-win kei-nightly-zip` で `build/<W>-demo-win/Kei-nightly.zip`（`tools/release/kei-nightly.mk:48-55`。2026-10-01 に `make -n` で確かめた）。
   base の zip を `build/releases/` に取るので network が要る。pin の SHA-256 は draft の値（`kei-nightly.mk:15-17`）で、`~/Kei-nightly.zip`（ユーザーが置いた元）と一致するかは未確認。

### Phase の回帰の組

- library（motion.c・scroll.c・gesture.c）を変えた Phase: host の試験の全て + その library を使う app の guest の試験 + C9（[WS099 の手引き](../ws099/guide.md)）+ boot test。
- 計測だけの Phase（p017）: source を変えないので回帰は不要。image の build の warning 0 と boot test だけ。

## 実機（Dell Latitude 5330）と Windows

- 5330 の内蔵の画面には touch が無い（デモは mouse と keyboard）。WS081 に 5330 の試験は無い。一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)。
- **Windows の QEMU** が WS081 の実機に当たる。エージェントは Windows を操作できない。ユーザーの手順は 3 行以内（ws.md の進め方）。
  手順の正本は [windows-touch.md](tests/windows-touch.md)（L2）と [p017](phase017/phase.md)（L3）。
  - 起動: `C:\Work\winq-zedbsd\boot.bat`（WS085 p001。WHPX・Venus・SDL・usb-multitouch・usb-net）。
  - SSH: `ssh -p 2222 root@127.0.0.1`（password `root`）。boot.bat に `hostfwd=tcp::2222-:22` が無ければ足す（windows-touch.md「用意」2）。
  - 出力: touchlog の `SUMMARY` の 6 行（L2）、session の log（L3）。PASS の基準は上の段の表。

## 注意

- ユーザーの決定: デモの touch は Windows の QEMU（2026-09-30 朝）。WS081 の L3 は後ろ（2026-09-30 午後「描画の高速化はラップアップ」）。Notes の指は既定で scroll・pinch、toolbar の切り替えで線（p015）。
  design §10 の 1〜3・5・6 は既定の案のまま進めている。値を変えるならユーザーに示す。
- Windows の機械と `C:\Work\winq-zedbsd` の binary はユーザーの物。QEMU の fork の source（`vendor/winq-emu-*`）はこの checkout に無い（WS085 の merge で submodule を外した）。
- `build/ws081-tests/touchlog` と `build/amd64/sysroot` は全ての agent で共有。消さない。
- 共有の runtime: `touchlog-check.sh` は `build/ws081/touchlog-run`、各 app の試験は `build/ws081-run`。同時に 2 つの試験を同じ runtime で走らせない。
- 2026-10-01 から WS104 が compositor と libkeiland の file を動かす（OS の境界）。library・compositor の source に触る前に main に確かめる。
- 2026-10-10 ごろ以降は bug の修正と実機の調整だけ。
- commit は自分の path だけ `git commit -m WIP -- <path>...`。push しない。
