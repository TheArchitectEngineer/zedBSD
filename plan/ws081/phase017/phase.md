<!-- awesome-plan project=zedbsd record=ws081-p017 -->

# ws081-p017: L3 の計測（Windows の QEMU の touch の反応と慣性の frame の間隔）

Status: in-progress（2026-10-13 P3 q957: 手順 1・2（試験の file）と image の build は済み。手順 3 の QEMU の確かめは T1 待ち、手順 4 の Windows の計測はユーザー待ち（L2 も未実施）。係数の調整は計測の後）（旧: planned（2026-10-01 手順を追記。2026-09-30 午後のユーザーの判断で WS081 の L3 は後ろ））
Disposition: normal
Parent: [WS081](../ws.md)
Queue: q957（2026-10-13 Q1。ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう」）
依存: p016（`touch-latency.py`）、p018（Windows 用の image）、**L2 の合格**（ユーザーの Windows の `touchlog` の SUMMARY）、WS085（Windows の WINQ-EMU と usb-multitouch）

## 範囲と受け入れ

- L3 の数値目標（ws.md の段の表）: Windows の QEMU で、指を置いてから最初の合成の frame まで p95 ≤ 50 ms、慣性の scroll の frame の間隔の最長 ≤ 33 ms。
- 計測だけ。source（compositor・libkeiland・app）は変えない。満たさなければ uncleared にし、係数の調整（提案 p019、[手引き](../guide.md)）を main に提案する。
- 計測の道具: `plan/ws081/tests/touch-latency.py`（compositor の `--log-frames` の log の `KWL TOUCH report … host_ms=` と `KWL COMPOSE … at_ms=` を読む）。
- 試験の側の追加（この Phase で作る、試験の file だけ）: session の compositor を `--log-frames` で起こす session の script と、それを入れた Windows 用の image。

## 前提: L2 の結果（ここに書く）

| 日付 | image | rate_hz | scan_gaps | arrival_gaps | median_ms・p95_ms・max_ms | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| （未実施） | | | | | | |

L2 の手順と判定は [windows-touch.md](../tests/windows-touch.md)。L2 が不合格なら、この Phase は始めない。

## 手順（2026-10-01 追記）

1. session の script: `userland/desktop/sessiond/session.sh` を `plan/ws081/tests/session-log-frames.sh` に複写し、最後の行
   `exec /bin/wayland --session --glass --socket="$XDG_RUNTIME_DIR/wayland-0" $picture "$@"`（`session.sh:16`）に `--log-frames` を足す（他は変えない）。
2. `plan/ws081/tests/build-demo-win.sh` に、環境変数 `WIN_LOG_FRAMES=1` の時だけ `extra="$extra --file /etc/keiland/session=plan/ws081/tests/session-log-frames.sh --mode /etc/keiland/session=0755"` を足す
   （`--file` で既存の file を置き換えられることは `build-demo-image.sh` の `/etc/passwd` で使われている）。`sh -n plan/ws081/tests/build-demo-win.sh`。
3. image を作り、Linux の QEMU で `--log-frames` が効くことを確かめる:

```
mkdir -p build/ws081-p017
ls build/amd64/sysroot/usr/lib/crt1.o
WIN_LOG_FRAMES=1 sh plan/ws081/tests/build-demo-win.sh build/ws081-p017-win > build/ws081-p017/win-build.log 2>&1; echo "exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/ws081-p017/win-build.log | grep -vE '/packages/|^\.\./src/|userland/base/noct/noct/' | wc -l
OUTPUT=build/ws081-p017/boot plan/tools/boot-test.sh build/ws081-p017-win/hdd-image.img; echo "exit=$?"
GUEST_RUNTIME=$PWD/build/ws081-p017/run sh plan/ws035/tests/zdesktop-guest.sh start build/ws081-p017-win/hdd-image.img
GUEST_RUNTIME=$PWD/build/ws081-p017/run sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240
sleep 30
GUEST_RUNTIME=$PWD/build/ws081-p017/run python3 plan/tools/guest/guest.py run 'grep -c "KWL COMPOSE frame=" /run/user/1000/session.log'
GUEST_RUNTIME=$PWD/build/ws081-p017/run sh plan/ws035/tests/zdesktop-guest.sh stop
```

   - 1 行目（`crt1.o`）が無ければ先に `make -j64 sysroot-amd64`（[commands.md §2](../../ws104/commands.md)）。
   - PASS: build の `exit=0`、warning `0`、`boot-test: PASS …`、grep の数が 1 以上（session の compositor が frame を log に出している）。
4. ユーザーへ渡す手順（このまま送る。image は `build/ws081-p017-win/hdd-image.img`、置き場所は windows-touch.md の「用意」1）:
   1. `C:\Work\winq-zedbsd\data\hdd-image.img` を上の image に替えて `boot.bat` を起動し、desktop が出るまで待つ。
   2. 指で: desktop の何も無い所を 10 回 tap、App Home から Files を開き一覧を指で 5 回 fling、Terminal を開いて `ls -lR /usr` の出力を指で 5 回 fling。
   3. PowerShell で `scp -P 2222 root@127.0.0.1:/run/user/1000/session.log .`（password `root`）を打ち、できた `session.log` を送る（PowerShell の `>` は UTF-16 で書くので使わない）。
5. エージェントの集計:

```
cp <ユーザーの session.log> build/ws081-p017/win-session.log
python3 plan/ws081/tests/touch-latency.py build/ws081-p017/win-session.log | tee build/ws081-p017/latency.txt
tail -1 build/ws081-p017/latency.txt
```

   - RESULT の行の `downs`・p50・p95・最長と、慣性の frame の間隔の最長を読む（`touch-latency.py` の先頭の説明）。`downs` が 10 未満なら tap が届いていない（L2 を疑う）。

## 完了の条件

- p95 ≤ 50 ms、慣性の frame の間隔の最長 ≤ 33 ms（Windows の QEMU、`touch-latency.py` の RESULT）。満たせば cleared、ws.md の段の表の L3 に値を記録。
- 満たさなければ uncleared。値と、どちら（反応・慣性）が届かないか、Windows の合成の間隔（`KWL COMPOSE` の at_ms の差の中央値）を書き、提案 p019 の範囲を main に送る。
- QEMU（Linux）の値は参考（Venus の lavapipe で合成が約 8 fps のため、p016）。合否は Windows の値だけ。実機（5330）は touch が無いので範囲外。
- 試験の file の変更（`session-log-frames.sh`、`build-demo-win.sh`）は `sh -n` が通り、`WIN_LOG_FRAMES` 無しの build は今と同じ（`/etc/keiland/session` を置き換えない）。

## 実施（2026-10-13〜14 P3 q957）

- 手順 1: `plan/ws081/tests/session-log-frames.sh`（新）。`userland/desktop/sessiond/session.sh` と同じで、compositor に `--log-frames` を足しただけ（diff は冒頭の注記と最後の行）。
- 手順 2: `plan/ws081/tests/build-demo-win.sh` に `WIN_LOG_FRAMES=1` の時だけ `--file /etc/keiland/session=plan/ws081/tests/session-log-frames.sh --mode /etc/keiland/session=0755`。無い時の引数は今と同じ。`sh -n` は両方 ok。
- image: `WIN_LOG_FRAMES=1 sh plan/ws081/tests/build-demo-win.sh build/p3-ws081-p017-win` → exit 0（3 分）、warning 0（手順 3 の grep の数え方）。staging の `rootfs/etc/keiland/session` が `--log-frames` の版（0755）であることを確かめた。image は P3 の worktree の `build/p3-ws081-p017-win/hdd-image.img`（2.2 GB）。
- 道具の確かめ: `touch-latency.py` は今の compositor の行（`KWL TOUCH report … host_ms=… scan=…`、`KWL COMPOSE frame=… at_ms=…`）を読む（合成の log で RESULT が出た）。phase.md の古い `ZWL` の名は `KWL` に直した。
- 未実施: 手順 3（QEMU、T1 へ）、手順 4（ユーザーの Windows の計測）、L2。係数の調整（p018/p019 の提案）は Windows の `session.log` の後（Q1 2026-10-13: blocked のまま）。

### T1 への依頼（手順 3）

- image: P3 の worktree の `build/p3-ws081-p017-win/hdd-image.img`（main に merge した tree で作り直すなら `WIN_LOG_FRAMES=1 sh plan/ws081/tests/build-demo-win.sh build/t1-ws081-p017-win`）。
- 流す物: `plan/tools/boot-test.sh IMAGE`（PASS と PNG）、`plan/ws035/tests/zdesktop-guest.sh start IMAGE` → `wait --timeout 240` → 30 秒の後に `python3 plan/tools/guest/guest.py run 'grep -c "KWL COMPOSE frame=" /run/user/1000/session.log'` → `stop`。
- 合否: boot-test PASS、grep の数が 1 以上（session の compositor が frame を log に出す）。

### ユーザーへの手渡し（手順 4、Windows の機械）

1. `C:\Work\winq-zedbsd\data\hdd-image.img` を上の image に替えて `boot.bat` を起動し、desktop が出るまで待つ。
2. 指で: desktop の何も無い所を 10 回 tap、App Home から Files を開き一覧を指で 5 回 fling、Terminal を開いて `ls -lR /usr` の出力を指で 5 回 fling。
3. PowerShell で `scp -P 2222 root@127.0.0.1:/run/user/1000/session.log .`（password `root`）を打ち、できた `session.log` を送る。

受け取った後の集計は上の手順 5（`touch-latency.py`）。
