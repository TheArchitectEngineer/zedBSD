<!-- awesome-plan project=zedbsd record=ws139-p001 -->

# ws139-p001: 計測の image と一括の script、E1 の基準値

Status: planned
Disposition: normal
Parent: [WS139](../ws.md)
Queue: none
依存: なし
時限の目安: script 3 h。T の 1 回の実行は 10 分程度の見込み

## 範囲

- **入る**: 台帳の P-01（C5）・P-02（直接の入力）・P-03（合成の費用）を、1 つの image と 1 つの guest で続けて測る script。
  結果を台帳の形の表にする道具。T1/T2 への E1 の計測の依頼と、台帳（ws.md）の更新。
- **入らない**: product の source の変更（計測の log を足すことも含めて、しない）。P-04〜P-10 の測り直し（p003 で要るかを決める）。E2（p002）。
- **所有する path**: `plan/ws139/`。他の WS の試験の script は呼ぶだけで、変えない。

## 始める前に読む物

1. [ws.md](../ws.md)（環境の印 E1〜E4、台帳、計測の道具）。
2. 呼ぶ script と、その使い方の先頭の comment。
   - `plan/ws099/tests/c5-transitions.sh`: 出力は `C5 <kind>: first_frame_ms= settle_ms= frames= max_gap_ms= ok|FAIL` の行と
     `C5 RESULT pass= fail= first_max= gap_max=`。
   - `plan/ws095/tests/latency-bug143.sh`: `OUT/latency.txt` に `LATENCY name= trials= median_ms= max_ms= missed=`。
     4 段の全体で約 5 分（3 分の無入力の待ちを含む）。
   - `plan/ws134/tests/monitor-p003.sh` の 3 段（107-122 行）: compositor を `--log-frames` で起こし、monitor の sim を走らせ、`ZWL PERF compose` を読む。
3. image の作り方の手本: `plan/ws089/tests/build-settings-image.sh`（`plan/tools/guest/test-image.sh` と `--file` の行）。

## 手順

### 1. image（`plan/ws139/tests/config-amd64-perf.mk`・`build-perf-image.sh`）

1. `config-amd64-perf.mk`:
   ```make
   # ws139-p001: the performance image: the Settings image with the input method (Text Editor, Terminal, keiland-ime,
   # popup-probe through the menu image) and the System Monitor, for plan/ws139/tests/perf-run.sh.
   include plan/ws089/tests/config-amd64-settings-ime.mk
   ZEDBSD_USER_PROGRAMS += monitor
   ```
2. 必要な program が入るかを確かめる。
   ```sh
   make ZEDBSD_CONFIG=plan/ws139/tests/config-amd64-perf.mk list-user-programs | tr ' ' '\n' \
       | grep -xE 'wayland|popup-probe|textedit|terminal|keiland-ime|monitor'
   ```
   - 6 つとも出ること。出ない物があれば config に足す。
   - `list-user-programs` の出力の形が違えば、その出力から探す。
3. `build-perf-image.sh BUILD`: `exec plan/tools/guest/test-image.sh plan/ws139/tests/config-amd64-perf.mk "$build"` に、
   `plan/ws089/tests/build-settings-image.sh` の `--file` の行（背景の file と `make-home.sh`）を、その時の版のとおりに写して付ける。
   WS138 で背景が PNG になっていれば PNG の行になる。出力は `BUILD/hdd-image.img`。

### 2. 一括の script（`plan/ws139/tests/perf-run.sh OUT`）

前提: guest は先に起動しておく（下の「試験の依頼」）。`GUEST_RUNTIME` を export し、全ての段で同じ guest を使う。

script の先頭に記録する物（`OUT/env.txt`）:
- `git rev-parse HEAD`、image の path と SHA-256、host の `nproc`・`uptime`（load average）・`/proc/pressure/io`（在れば）・`date -Is`。
- 環境の印: 既定は `E1`。引数 `--env=E2` で変える（p002 が使う）。

段（順に。各段の出力は `OUT/<段>.out`）:

1. **idle**: P-03 の土台。
   - 他の compositor を止める。止め方は `monitor-p003.sh` の `stop_all` の形。
   - `/bin/wayland --timeout=40 --width=1280 --height=800 --glass --log-frames $picture` を起こして 30 秒待ち、client 無しの `ZWL PERF compose` の行を取る。
   - `$picture` は `monitor-p003.sh` の start() と同じく、背景の file が在れば `--wallpaper=`。
2. **monitor**: P-03。`monitor-p003.sh` の 3 段と同じ command（`--log-frames` の compositor と `/bin/monitor --source=sim --seed=9 --cpus=16 --gpus=2`）。
   - 20 秒待ってから、`ZWL PERF compose` の最後の 3 行と `ZMON FRAME` の行を取る。
   - 判定はしない（記録だけ）。
3. **c5**: P-01。`C5_ROUNDS=5 sh plan/ws099/tests/c5-transitions.sh OUT/c5`。5 回にするのは、1 回目だけ遅い現象を他と分けるため。
   - `C5 RESULT` が FAIL でも止めずに次へ進む。速さを測る段であり、合否の段ではない。
4. **latency**: P-02。`sh plan/ws095/tests/latency-bug143.sh OUT/latency`。全体（4 段）を流す。
   - 3・4 段（辞書の保存）の合否は記録だけにする。
   - `OUT/latency/latency.txt` の `LATENCY` の行を使う。
5. 最後に compositor と app を止める。

各段は `timeout` を付けて呼ぶ（idle 60 s、monitor 120 s、c5 300 s、latency 600 s）。時間切れはその段の `TIMEOUT` として記録し、次へ進む。

### 3. 表にする道具（`plan/ws139/tests/perf-summary.py OUT`）

`OUT/*.out` と `OUT/latency/latency.txt` を読み、`OUT/summary.tsv` と、画面向けの表（markdown）を出す。
列は `id	metric	value	unit	samples	env	commit`。

| id | metric | 取り方 |
| --- | --- | --- |
| P-03 | `idle_compose_frame_ms` | idle の `ZWL PERF compose … frame_ms=` の中央値 |
| P-03 | `idle_compose_draw_ms` | 同じ行の `draw_ms=` の中央値 |
| P-03 | `idle_compose_per_s` | `frames=` ÷ その行の期間の秒（`ZWL PERF <N>ms:` の N） |
| P-03 | `monitor_compose_frame_ms`・`monitor_fps`・`monitor_callback_ms` | monitor の段の `ZWL PERF compose` と `ZMON FRAME fps= callback_ms=` の中央値 |
| P-01 | `c5_first_frame_ms_round1`・`c5_first_frame_ms_rest` | `C5 <kind>: first_frame_ms=` の、1 回目の 4 つの最大と、2 回目以後の中央値 |
| P-01 | `c5_gap_ms_max` | `C5 RESULT … gap_max=` |
| P-02 | `textedit_direct_ms`・`terminal_direct_ms` | `LATENCY name=textedit-direct|terminal-direct` の `median_ms`（`max_ms` も別の行に） |

- 値が取れなかった metric は `value` を `NA` にし、理由（その段の TIMEOUT・行が無い）を表の下に書く。
- 1 回目と 2 回目以後の分け方: `C5` の行は wiseview-open・wiseview-close・home-open・home-close の 4 つで 1 回なので、最初の 4 行を 1 回目とする。
  c5-parse.py の出力の順をその時の版で確かめる。
- host で試す: T の出力が無い間は、過去の形の行を並べた小さな fixture（`plan/ws139/tests/fixture/`）で動かし、列が埋まることを確かめる。
  fixture の行は ws.md の台帳の出どころの記録から写す（例: T1-057 の `ZWL PERF compose frames=16 draw_ms=103.58 … frame_ms=115.11`）。

### 4. 自分で行う確かめ（QEMU は起動しない）

```sh
sh -n plan/ws139/tests/perf-run.sh plan/ws139/tests/build-perf-image.sh
python3 plan/ws139/tests/perf-summary.py plan/ws139/tests/fixture   # 全ての metric が埋まる（fixture にある物）
make ZEDBSD_CONFIG=plan/ws139/tests/config-amd64-perf.mk list-user-programs   # 手順 1 の 2
```

image の build は T が行う（T は自分の worktree の build/ で作る。AGENTS.md の Q1 の許可）。

## 試験の依頼（T1/T2）

commit と Q1 への merge 依頼の後に依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。

- **image**: `sh plan/ws139/tests/build-perf-image.sh BUILD`（commit の SHA を添える）。
- **guest**: `GUEST_RUNTIME=$PWD/build/ws139-run sh plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img`。
  settings-guest.sh は `GUEST_RUNTIME` を受ける（11 行）。
- **流す物**: `GUEST_RUNTIME=$PWD/build/ws139-run sh plan/ws139/tests/perf-run.sh build/ws139-perf-e1` の後に `python3 plan/ws139/tests/perf-summary.py build/ws139-perf-e1`。
  合わせて 10 分程度。
- **返す物**: `summary.tsv`、表、`env.txt`、各段の `.out`。
- **合否**: 全ての段が TIMEOUT なしで終わり、`summary.tsv` の P-01・P-02・P-03 の metric が `NA` でないこと（速さの値そのものは判定しない）。
- **2 回目**: host の負荷で値が揺れるので、同じ guest でもう 1 回流してもらう（`build/ws139-perf-e1b`）。2 回の差を台帳に書く。

## 受け入れの条件

1. 手順 4 の確かめが通る。
2. T の 2 回の実行が上の合否を満たす。
3. ws.md の台帳の P-01・P-02・P-03 の行に、「2026-xx-xx E1（perf-run）」の値を足す。古い値は消さない。
4. E1 の値の読み方の注意（lavapipe、Venus の 10 ms、VNC の上限）を、表と一緒に「結果」に書く。

## 結果

（未実施）
