# WS099 作業の手引き（2026-10-01）

この file は、WS099（Keiland の compositor のデモの基準）を、追加の調査なしで続けるための手引き。記録の正本は [ws.md](ws.md) と各 phase.md。
ここに書いた「提案」の Phase は main（Q1）が ws.md に足すまで実行しない。Queue に入っていない Phase は実行しない（AGENTS.md）。
command は repo の root（`/home/awe/zedBSD-claude1` か、その worktree の root）から実行する。`<W>` は作業の名前（例 `ws099-p017`）に置き換える。
build と回帰の一般の command は [WS104 の commands.md](../ws104/commands.md) の節を参照し、ここでは WS099 に固有の分だけを書く。

## ゴール

対象は compositor の zdesktop（`/bin/wayland`、`userland/desktop/wayland/`）と、それが起こす greeter・session の遷移だけ。基準は ws.md の「達成基準」（2026-09-30 ユーザーが「案のまま確定」）。

| # | 基準 | 合否の場所 | 支える fg010 の場面 |
| --- | --- | --- | --- |
| C1 | 起動→greeter、login→デスクトップ、Log Out→greeter、Shut Down の替わり目で黒い画面と文字の console が 0 枚 | QEMU（自動）＋**5330 の目視（ユーザー）** | S1・S2・S15 |
| C2 | 窓の移動・四隅と四辺の resize・最大化と戻し・最小化と戻しが意図の位置と大きさ | QEMU | S11 |
| C3 | 全画面・最大化を解いた窓の title bar が system bar に重ならず drag できる（Esc と下からの swipe） | QEMU | S8・S11 |
| C4 | 窓を閉じると同じ desktop の次の窓に focus | QEMU | S11 |
| C5 | App Home・Wiseview の開閉の最初の frame まで 100 ms 以内（窓 10 個） | **5330** | S3・S11 |
| C6 | 窓 10 個で pointer の移動から表示まで中央値 50 ms 以内 | **5330**（WS075 の計測） | S11 |
| C7 | すりガラスの上の文字の contrast 4.5:1 以上（既定と生成の 5 枚の壁紙） | QEMU | S2・S7 |
| C8 | Terminal の本体は直角、title bar は丸い。他の窓は両方丸い | QEMU | S10 |
| C9 | `criteria.sh` の C9 の一覧（10 本）が全て PASS | QEMU | 全体 |
| C10 | 1 時間の連続の操作で zdesktop が落ちず `ZWL ERROR` 0 | QEMU＋**5330（L3）** | 全体 |

「完了」の具体: 下の全てがそろい、最後に規約の全文の見直しの Phase（提案 p018）が cleared。

1. `criteria.sh` の C1〜C5・C7〜C10 が同じ image で全て PASS（QEMU の Venus）。
2. 5330 で C1（ユーザーの目視）・C5（`c5-hw.sh`）・C10（1 時間）を満たす。C6 は測った値を記録する（下の「注意」の C6 の扱い）。
3. 規約の全文の見直しと回帰（AGENTS.md・Awesome Plan §6 の必須の最後の Phase）。

## 今の状態（2026-10-01）

| 基準 | 状態 | 証拠 |
| --- | --- | --- |
| C1 | QEMU PASS（login・Log Out は p126 `--no-black`、起動と Shut Down は `c1-boot-shutdown.sh`）。**5330 の目視は未実施** | [p004](phase004/phase.md)、[p009](phase009/phase.md)、[p010](phase010/phase.md)。WS103 の後も PASS（[WS103](../ws103/ws.md) の V2） |
| C2・C3・C4・C8 | QEMU PASS | [p001](phase001/phase.md)、[p015](phase015/phase.md)（C3 に swipe の解除） |
| C5 | 5330 の passthrough で 34 回とも ≤ 100 ms（最大 55 ms）。QEMU は 90〜95 ms（App Home の最初の開きだけ 201 ms、QEMU だけ） | [p002](phase002/phase.md) |
| C6 | 未達。5330 の passthrough の中央値は 56.4〜58.8 ms（WS103 の V4、試料 200 個） | WS075（ws075-p024・p026）、[WS103](../ws103/ws.md) |
| C7 | QEMU 72/72 が 4.5 以上、最小 4.68 | [p005](phase005/phase.md) |
| C9 | 10 本 PASS。ただし p076 が全体の実行の中で時々 FAIL（[BUG-125](../bugs/BUG-125.md)、tracking） | [p003](phase003/phase.md)、[p007](phase007/phase.md) |
| C10 | QEMU PASS（p001）。**5330 の 1 時間は未実施** | [p001](phase001/phase.md) |

既知の bug（[Bug Board](../known-bugs.md)）:

| Bug | 状態 | WS099 との関係 |
| --- | --- | --- |
| [BUG-125](../bugs/BUG-125.md) | tracking。C9 の p076 の左端の resize の後の画面の判定が時々 FAIL | 提案 p017 で試験の側を直す |
| [BUG-119](../bugs/BUG-119.md) | resolved（ws073-p043: ACPI S5）。**5330 の電源断は未確認** | ws.md の段の表の p013（電源断の実装）は ws073-p043 で済んだので不要。5330 の確認は p012 に入れる |
| [BUG-122](../bugs/BUG-122.md) | resolved（p010、QEMU）。5330 は未確認 | p012 で sessiond の log を読む |
| [BUG-121](../bugs/BUG-121.md) | resolved（p011、5330 20 回・QEMU 200 回で 0） | 再び起きたら `resize-hw.sh` |
| [BUG-123](../bugs/BUG-123.md)・[BUG-117](../bugs/BUG-117.md) | resolved。5330 の素の起動での確認が残る | p012 のついでに見る |
| [BUG-120](../bugs/BUG-120.md) | tracking（窓 30 個で GPU の object の枠が尽きる） | デモの 10 窓では出ない。基準の外 |
| [BUG-124](../bugs/BUG-124.md) | tracking（QEMU の Venus で Model viewer を大きくすると swapchain が DEVICE_LOST） | 基準の外。C2・C9 の試験で Model viewer を大きくしない |

## 次の作業の順番

1. **ws099-p017（提案）BUG-125: C9 の p076 の画面の判定を安定させる**（QEMU だけで済む。全ての Phase の回帰の C9 が安定するので最初）。
   - 目的: `plan/ws035/tests/zdesktop-p076.sh` の手順 8 の `check`（`narrowed.png`・`widened.png`、zdesktop-p076.sh:153・158）が resize の途中の画面を撮ることがある。
     `ZWL RESIZE settled` の log（:152・:157）の後、期待の画素になるまで撮り直す（例: 0.5 秒ごとに最大 6 回）形にする。
   - file: `plan/ws035/tests/zdesktop-p076.sh` だけ（ws099-p003 と同じく WS035 の共有の試験を WS099 が直す。main に一言知らせる）。compositor の source は変えない。
   - 完了の条件: 単独 20 回で FAIL 0（`for i in $(seq 1 20)` で下の「コマンド」の p076 を回す）、`criteria.sh … C9` を 5 回で p076 の FAIL 0、BUG-125 の ticket と Bug Board の行を resolved に。
     試験の側の直しで FAIL が残る（画面が 3 秒たっても期待にならない）なら、compositor の resize の確定（`shell.c` の `ZWL RESIZE settled` を出す所）を調べる Phase に分けて uncleared。
2. **[ws099-p012](phase012/phase.md) C1 の 5330（ユーザーの目視）と、5330 での BUG-119・BUG-122 の確かめ**（手順は phase.md に追記した）。ユーザーの時間が要る。
3. **[ws099-p014](phase014/phase.md) C10 の 5330 の 1 時間**（新しい `c10-hw.sh` を作る。手順は phase.md に追記した）。
4. **ws099-p006 C6 の記録**（planning）。2026-09-30 午後のユーザーの判断「描画の高速化はラップアップし優先を下げる」により、速さの直しはしない。
   5330 で `measure-apps.sh` を 5 run 流し `c6.py` で中央値を ws.md に記録するだけ。デモ critical の中で後ろ。
5. **ws099-p018（提案）全文の規約と回帰**（WS の最後、必須）: WS099 で変えた全ての source（p002・p005・p007・p009・p010・p011・p015・p016 の差分、`git log --oneline -- userland/desktop/wayland userland/desktop/sessiond` で WS099 の commit を拾う）を
   [coding-style.md](../coding-style.md) の全文で見直し、`plan/tools/style-check.py` と build（warning 0）、`criteria.sh` の C1〜C5・C7〜C10、boot test。

ws.md の段の表にある p013（BUG-119 の電源断の実装）は ws073-p043 で済んだ。main は ws.md で p013 を「不要（ws073-p043）」として閉じるか、p012 に統合する。

## 未知と調べ方

| 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- |
| BUG-125 の原因が試験の間合いだけか、compositor の resize の確定の遅れか | 試験だけ直して隠すと、本物の resize の不具合を見逃す | `zdesktop-p076.sh` を `--log-frames` 付きの zdesktop で回す（試験の :75 の `/bin/wayland` の引数に一時的に足す）。`ZWL RESIZE settled` と、その後の最初の `ZWL COMPOSE frame=… at_ms=` の差を読む。差が常に 1 frame 以内なら試験の側の直しで足りる。compositor の側は `grep -n "RESIZE settled" userland/desktop/wayland/shell.c` で出す所を読む |
| 5330 の素の起動（USB）で C1 の黒い画面が出るか | QEMU の passthrough は firmware の GOP と i915 の引き継ぎが違う | ユーザーの目視（p012）。黒が出たら、出た替わり目（起動・login・Log Out・Shut Down）と秒数を聞き、WS084（i915 の引き継ぎ）の担当へ渡す。エージェントは 5330 に SSH で入り `/var/log/sessiond.log` の `SESSIOND` の行の時刻と照らす（console の log ではない） |
| 5330 で Shut Down が電源を切るか（BUG-119） | QEMU では `guest-shutdown` で終わるが、5330 の ACPI の S5 は未確認 | ユーザーの目視（電源の LED が消える、10 秒以内）。切れないなら、`drv_acpi_poweroff()`（`grep -rn drv_acpi_poweroff src/`）の `_S5` の値と PM1 の書き込みを ws073 の担当へ |
| 5330 の 1 時間で zdesktop が落ちるか | QEMU の 1 時間は Venus の上の話で、i915 の job の時間切れ・BUG-117 系は 5330 だけ | p014 の `c10-hw.sh`。落ちたら session.log の最後の `ZWL` の行と `SESSIOND GREETER failed reason=`、i915 の状態は WS075 の `engine-gdb.sh` で読む |
| C6 の残りの差（約 7 ms） | 基準 50 ms を保つ判断（2026-09-30 朝） | WS075 の担当。WS099 では値の記録だけ。`plan/ws075/tests/hdmi/c6.py OUTDIR...` |
| QEMU の guest の zdesktop が固まる・落ちる | 試験の FAIL の切り分け | QEMU の console・serial の log では判定しない（AGENTS.md）。user の process は guest の lldb（criteria の image に lldb は無いので、`guest.py run 'ps -A'` と zdesktop の log `/run/user/1000/session.log`・`/tmp/zdesktop.log`）。kernel は gdbstub: `GUEST_RUNTIME=$PWD/build/ws035-sq-run python3 plan/tools/guest/guest.py kgdb --symbols build/<W>-criteria/vmunix 'info threads' 'bt'`。画面は QMP（`plan/ws035/tests/zdesktop-shot.py OUT.png --runtime build/ws035-sq-run`） |

## コマンド

### 準備（host の起動ごとに 1 回）

```
ls build/ws035-sq-venus/install/libexec/virgl_render_server
sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128
```

- 1 行目が無いなら Venus の renderer が無い（`plan/ws035/tests/zdesktop-guest.sh:15-18` の手順で取る）。

### build

| 何 | command | 出力 |
| --- | --- | --- |
| compositor だけ（compile の確かめ） | [commands.md §3](../ws104/commands.md) の `make -j64 build/amd64/bin/wayland` | `build/amd64/bin/wayland` |
| 基準の image（QEMU の Venus） | `sh plan/ws099/tests/build-criteria-image.sh build/<W>-criteria > build/<W>/criteria-build.log 2>&1; echo "exit=$?"` | `build/<W>-criteria/hdd-image.img` |
| 5330 の passthrough の demo の image（C5・C6・C10 の実機） | `sh plan/ws075/demo/build-demo-image.sh build/<W>-pt passthrough > build/<W>/pt-build.log 2>&1; echo "exit=$?"` | `build/<W>-pt/hdd-image.img` |
| 5330 の素の起動（USB）の demo の image（C1 の目視） | `sh plan/ws075/demo/build-demo-image.sh build/<W>-demo > build/<W>/demo-build.log 2>&1; echo "exit=$?"` | `build/<W>-demo/hdd-image.img` |

- 先に `mkdir -p build/<W>`。image の build は同時に 1 つだけ（commands.md §0）。必ず BUILD を渡す（省くと `build/amd64` を上書き: `build-criteria-image.sh:10`、`build-demo-image.sh:23`）。
- warning の確かめは commands.md §1 の grep を各 log に使う。
- `build-criteria-image.sh` は `config-amd64-criteria.mk`（graphical の login の image + Notes・Settings、`plan/ws099/tests/config-amd64-criteria.mk:6-7`）と生成の壁紙（`:19-22`）。
- `build-demo-image.sh` の既定は `display=edp`（`plan/ws075/demo/config-demo-hdmi.mk`）。passthrough は VBT を kernel に入れる（`build-demo-image.sh:15-17`）。

### QEMU の基準の試験（criteria.sh）

```
sh plan/ws099/tests/criteria.sh build/<W>-criteria/hdd-image.img build/<W>/criteria C1 C2 C3 C4 C5 C7 C8 C9
cat build/<W>/criteria/results.txt
grep -c ' FAIL ' build/<W>/criteria/results.txt
```

- 判定は `results.txt` の各行の `PASS`（行の形 `<Cn> <名前> <PASS|FAIL> seconds=N <詳細>`、`criteria.sh:62`）。criteria.sh 自体は常に exit 0。最後の grep が `0` なら PASS。
- 試験ごとに guest を起動し直す（`criteria.sh:37-45`、起動の待ちは 40 秒）。前もって guest を起こさない。C6 は常に `NOT-RUN`（`:86`）。
- 時間の目安: C1（2 本）〜8 分、C2 〜3 分、C9（10 本）〜15 分、C5 〜3 分、全部（C10 を除く）で約 40 分（概算、未計測）。C10 を加えると +65 分（`C10_SECONDS=3600`、`:21`）。
- 閾値は環境変数で変えられる（`criteria.sh:16-25`）。BUG-119 の解決の後なので、C1 では QEMU の終了も必須にできる:
  `C1_REQUIRE_QEMU_EXIT=1 sh plan/ws099/tests/criteria.sh build/<W>-criteria/hdd-image.img build/<W>/criteria-c1 C1`（`c1-boot-shutdown.sh:33`・`:159`）。
- C9 の一覧: `p052 p053 p072 p076 p126 p128 p134 p137 p138 plan/ws099/tests/cursor-owner.sh`（`criteria.sh:25`）。

### 1 本だけを回す（試験の直し・BUG-125）

```
sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-criteria/hdd-image.img
sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
sh plan/ws035/tests/zdesktop-p076.sh build/<W>/p076-1
sh plan/ws035/tests/zdesktop-guest.sh stop
```

- PASS の印は各 script の最後の行（例 `p076: PASS`）。`zdesktop-guest.sh start` はすぐ戻るので `wait` を挟む（commands.md §6）。runtime は既定の `build/ws035-sq-run`。
- 20 回: `for i in $(seq 1 20); do sh plan/ws035/tests/zdesktop-p076.sh build/<W>/p076-$i | tail -1; done | sort | uniq -c`（guest は 1 回の起動で回す。p076 は自分の zdesktop を起こし直す: `zdesktop-p076.sh:75`）。

### 他の WS099 の試験（どれも guest を自分で起こす or 起こした guest の上）

| 試験 | command | PASS の印 | 備考 |
| --- | --- | --- | --- |
| compositor の回復（BUG-122） | `sh plan/ws099/tests/bug122-recovery.sh build/<W>-criteria/hdd-image.img build/<W>/bug122` | `bug122-recovery: PASS` | 自分で guest を起こす（`:60`）。runtime 既定 `build/ws099/run`（`:15`） |
| resize の stress（BUG-121） | `VENUS_SIZE=1920x1280 sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-criteria/hdd-image.img` の後 `sh plan/ws099/tests/resize-stress.sh build/<W>/resize` | `resize-stress: PASS` | 先頭の使い方 `resize-stress.sh:1-21` |
| Notes の swipe の解除（C3） | criteria の C3 に入っている | `c3-swipe-back: PASS` | 単独は guest を起こしてから `sh plan/ws099/tests/c3-swipe-back.sh build/<W>/c3` |
| Notes の角（WS079-p010、K7 と共通の回帰） | guest を起こしてから `sh plan/ws079/tests/zdesktop-p010.sh build/<W>-criteria build/<W>/p010` | `p010: PASS` | 第 1 引数の BUILD の `bin/wayland` を guest に入れる |
| boot test | [commands.md §4](../ws104/commands.md) を `build/<W>-criteria/hdd-image.img` に | `boot-test: PASS …/login.png` | PNG をユーザーに見せる |

### Phase の回帰の組（compositor の source を変えた Phase）

1. build（warning 0）。
2. `criteria.sh … C1 C2 C3 C4 C5 C7 C8 C9`（全て PASS。C5 の QEMU の値は参考で、App Home の最初の開きの 1 回だけ 100 ms を超えるのは既知）。
3. `plan/ws079/tests/zdesktop-p010.sh`（PASS）。
4. boot test（PASS、PNG）。
5. 窓・App Home・Wiseview・全画面に触れたなら 5330 の `c5-hw.sh`（下）。
- 試験の script だけを変えた Phase（p017 など）は、変えた試験と C9 だけでよい（AGENTS.md「回帰の範囲は Phase の性質で決める」）。

## 実機（Dell Latitude 5330）

一般の手順（USB への書き込み、boot、SSH、lock、passthrough の host）は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（別のエージェントが作成中。無ければ main に聞く）。

### passthrough（エージェントが自動で使う。i915 を QEMU に渡した 5330）

- 道具は WS075 の `plan/ws075/tests/hdmi-h4-hw.sh`。5330 の host は `I915_HOST`（既定 `solaris10-man`）。
- **lock**: `start` が `flock /tmp/i915-hw.lock` を取り（`hdmi/h4-lock.sh`）、`stop` まで持つ。他の run が持っている間は待つ。`start` を `timeout` で囲まない（`hdmi-h4-hw.sh:17-21`）。
  owner は `/tmp/i915-h4-owner`。`ctl`・`fetch` は自分の run が lock を持つ時だけ動く。
- 5330 の host の `~/bigbang/h4/` は全ての run で共有。同時に 1 run。

| 試験 | command | 出力 | PASS の印 | 時間 |
| --- | --- | --- | --- | --- |
| C5（開閉の最初の frame） | `sh plan/ws099/tests/c5-hw.sh build/<W>-pt/hdd-image.img build/<W>/c5-hw 3` | `build/<W>/c5-hw/first-frames.txt`・`session.log`・`shots/*-live.png` | `C5-HW RESULT count=N max_ms=M over=0` と `c5-hw: PASS` | 約 8 分（未計測。`H4_MINUTES=20` で上限、`c5-hw.sh:25`） |
| BUG-121（角の drag 20 回） | `sh plan/ws099/tests/resize-hw.sh build/<W>-pt/hdd-image.img build/<W>/resize-hw 20` | `build/<W>/resize-hw/` | `RESIZE-HW RESULT drags=20 vanished=0` と `resize-hw: PASS` | 約 15 分（未計測） |
| C6（記録だけ） | `sh plan/ws075/tests/hdmi/measure-apps.sh build/<W>-pt/hdd-image.img build/<W>-pt/vmunix build/<W>/c6-run1` を 5 回（run2…run5）、その後 `python3 plan/ws075/tests/hdmi/c6.py build/<W>/c6-run1 build/<W>/c6-run2 build/<W>/c6-run3 build/<W>/c6-run4 build/<W>/c6-run5` | 各 `measure.txt` | `c6.py` の中央値と判定（50 ms） | 1 run 約 10 分（未計測） |
| C10（1 時間、提案の新しい script） | [p014](phase014/phase.md) の手順 | — | `c10-hw: PASS` | 約 75 分 |

- 判定は script の PASS の行と画面（`shots/*-live.png`）と、guest の disk から読んだ session.log（compositor の log）。`hdmi-h4-hw.sh stop` が取る `kernel.log`・`serial.log` で合否を決めない。
- 途中で止める時: `plan/ws075/tests/hdmi-h4-hw.sh stop build/<W>/c5-hw`（lock を返す）。

### 素の起動（USB、ユーザーの目視）

C1 の目視・BUG-119 の電源断は [p012](phase012/phase.md) の手順。image は上の `build/<W>-demo/hdd-image.img`。

## 注意

- `criteria.sh` は guest の runtime を `build/ws035-sq-run` に固定する（`criteria.sh:33`、環境変数で変えられない）。`zdesktop-guest.sh` の既定も同じ（`zdesktop-guest.sh:30`）。
  **同じ host で criteria.sh と、`build/ws035-sq-run` を使う他の試験（WS104 の §5・§7 など）を同時に走らせない。**
- `plan/tools/boot-test.sh` は `OUTPUT` を `rm -rf` する（`boot-test.sh:70`）。専用の `OUTPUT=build/<W>/boot` を渡す。
- `zdesktop-guest.sh` は kernel の symbol を `build/ws035-sq/vmunix` に固定して記録する（`:55`）。gdbstub では `--symbols build/<W>-criteria/vmunix` を明示する。
- `c10-soak.sh` の既定の出力は `build/ws099-shots/c10`（`:87`）、`bug122-recovery.sh` は `build/ws099-shots/bug122`。必ず OUTDIR を渡す。
- 5330 の passthrough は WS075・WS094・WS102 などと共有。lock を持ったまま長く止めない。
- ユーザーの決定（ws.md・master）: 基準は C1〜C10 で確定（「案のまま確定」）。C6 は 50 ms を保つが、描画の高速化は 2026-09-30 午後のユーザーの判断で後ろ（WS099 では速さの直しをしない）。
  C7 の副次の文字の色 `0x56606f` を保ち、空の一覧の案内は基準の外（master の判断 7）。全画面は常に合成し、下からの swipe で窓に戻す（p015、ユーザー）。
  基準に無いものは作らない（Future Work か Bug Board へ）。
- 2026-10-01 から WS104（Keiland の OS の境界の整理）が compositor の file を動かす。WS099 の source の変更は WS104 の Phase と file がぶつからないか main に確かめてから。
- 2026-10-10 ごろ以降は bug の修正と実機の調整だけ（新しい機能を入れない、master のデモまでの進め方）。
- `build/ws035-wallpaper/` に `wallpaper.ppm`・`wallpaper-1080.ppm` が今は無い（`original-1280.png` だけ）。各 build の script は無ければ壁紙を入れずに進む（`build-criteria-image.sh:13`、`build-demo-image.sh:31`）。生成の 5 枚（`/usr/share/keiland/wallpapers/`）は入る。
- commit は自分の path だけ `git commit -m WIP -- <path>...`。push しない。`.internal/` を読まない。
