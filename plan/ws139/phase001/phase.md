<!-- awesome-plan project=zedbsd record=ws139-p001 -->

# ws139-p001: 計測の image と一括の script、E1 の基準値

Status: cleared（2026-10-05 Q1: T1-176 で計測の image・run・summary が 2 回とも完走し全指標に値。台帳への記入は次の Phase で P1）。以前: in-progress（2026-10-05 P1 generation17、q740。script と道具は済み。E1 の計測は T1、その 2 回の結果で受け入れ）
Disposition: normal
Parent: [WS139](../ws.md)
Queue: q740
依存: なし
時限の目安: script 3 h。T の 1 回の実行は 12 分程度の見込み

## 範囲

- **入る**: 台帳の P-01（C5）・P-02（直接の入力）・P-03（合成の費用）を、1 つの image と 1 つの guest で続けて測る script。
  結果を台帳の形の表にする道具。T1/T2 への E1 の計測の依頼と、台帳（ws.md）の更新。
- **入らない**: product の source の変更（計測の log を足すことも含めて、しない）。P-04〜P-10 の測り直し（p003 で要るかを決める）。E2（p002）。
- **所有する path**: `plan/ws139/`。他の WS の試験の script は呼ぶだけで、変えない。

## 始める前に読む物

1. [ws.md](../ws.md)（環境の印 E1〜E4、台帳、計測の道具）。
2. 呼ぶ script と、その使い方の先頭の comment。
   - `plan/ws099/tests/c5-transitions.sh`（`C5_ROUNDS` を受ける）。出力は次の 2 種類（`c5-parse.py:65-68`）。
     - `C5 <kind>[ windows=N]: first_frame_ms= settle_ms= frames= max_gap_ms= ok|FAIL` の行。
     - `C5 RESULT pass= fail= first_max= gap_max=`。
   - `plan/ws095/tests/type-latency.py`（`LATENCY name= trials= median_ms= max_ms= missed=` を出す）。
     使い方の手本は `plan/ws095/tests/latency-bug143.sh` の 0〜2 段（52-76 行）。
     3・4 段（辞書の保存、190 秒の待ちを含む）は P-02 と関係が無いので使わない。
   - `plan/ws134/tests/monitor-p003.sh` の 3 段（107-122 行）: compositor を `--log-frames` で起こし、monitor の sim を走らせ、`ZWL PERF compose` を読む。
3. `plan/ws089/tests/build-settings-image.sh`（`SETTINGS_CONFIG` で config を変えられる）。
4. `plan/ws089/tests/settings-wait.sh`（`wait_guest`・`find_window` の helper。latency-bug143.sh は 31 行で source している）。
   `plan/tools/guest/guest.py` の `wait`（`--timeout`）。

## 注意（数字の読み方）

- compositor は dirty か damaged の時だけ合成する（`userland/desktop/wayland/display.c:177-183`）。
  何も動かない時は `ZWL PERF compose` の行が出ない（`main.c:1077`。frames が 0 の窓では出さない）。だから idle の測定はしない。
  合成の費用（P-03）は、monitor が絶えず描く状態で測る。
- `ZWL FIRST_FRAME` と `ZWL COMPOSE at_ms=` は、画面に出た時刻ではなく submit の時刻である（`compose.c:396-405`）。
- この image は、Settings の image に IME と System Monitor を足した物である。昔の数字とは image が違う。
  - T1-006 の C5 は criteria の image（graphical boot、IME なし）。
  - T1-057 は monitor の image（IME なし）。
  - だから昔の数字と直に比べない（ws.md の台帳の注）。
- 背景: 呼ぶ script は `/usr/share/keiland/wallpaper.ppm` が在る時だけ背景を付ける（`c5-transitions.sh:28` など）。
  WS138（PNG）が main に入った後は、その script も `.png` に変わる。`env.txt` に背景の file の有無を記録する。

## 手順

### 1. image（`plan/ws139/tests/config-amd64-perf.mk`・`build-perf-image.sh`）

1. `config-amd64-perf.mk`:
   ```make
   # ws139-p001: the performance image: the Settings image with the input method (Text Editor, Terminal, keiland-ime;
   # popup-probe through the menu image) and the System Monitor, for plan/ws139/tests/perf-run.sh.
   include plan/ws089/tests/config-amd64-settings-ime.mk
   ZEDBSD_USER_PROGRAMS += monitor
   ```
2. 必要な program が選ばれるかを確かめる。
   ```sh
   make ZEDBSD_CONFIG=plan/ws139/tests/config-amd64-perf.mk \
       --eval 'ws139-programs: ; @echo $(ZEDBSD_USER_PROGRAMS)' ws139-programs | tail -1 | tr ' ' '\n' \
       | grep -xE 'wayland|popup-probe|textedit|terminal|keiland-ime|monitor'
   ```
   - 6 つとも出ること。出ない物があれば config に足す。
   - `make list-user-programs` は、選ばれた物ではなく全ての program を出すので、この確かめには使わない。
   - 2026-10-04 に P1 が `config-amd64-settings-ime.mk` で試した時は、monitor 以外の 5 つが出た。
3. `build-perf-image.sh BUILD` の中身は、次の 1 行と先頭の comment。
   ```sh
   SETTINGS_CONFIG=plan/ws139/tests/config-amd64-perf.mk exec sh plan/ws089/tests/build-settings-image.sh "$1"
   ```
   背景と `apps.conf` の `--file` は build-settings-image.sh が付けるので、WS138 の後もずれない。出力は `BUILD/hdd-image.img`。

### 2. 打鍵の遅れの script（`plan/ws139/tests/type-only.sh OUT`）

`latency-bug143.sh` の 0〜2 段（52-76 行）だけを、同じ command で写した script にする。

- textedit-direct（threshold 300、10 回）と terminal-direct（threshold 20、10 回）を測る。
- 写す物:
  - helper の `guest`・`expect_log`・`latency`（latency-bug143.sh の先頭）。
  - `. plan/ws089/tests/settings-wait.sh` の source（`find_window` が在る）。
- 出力は `OUT/latency.txt`。
- 終わりに Text Editor・Terminal を止める。compositor は止めない（次の段が止める）。
- 3・4 段は写さない。

### 3. 一括の script（`plan/ws139/tests/perf-run.sh [--env=E1|E2] OUT`）

前提: guest は先に起動しておく（下の「試験の依頼」）。`GUEST_RUNTIME` を export し、全ての段で同じ guest を使う。

1. **wait**: 最初に `timeout 300 python3 plan/tools/guest/guest.py wait --timeout 240`。失敗したら `perf-run: FAIL guest not up` で止める。
2. **env**: `OUT/env.txt` に次を書く。
   - commit: 環境変数 `PERF_COMMIT` があればそれ、無ければ `git rev-parse HEAD`（5330 の上には git が無いので、p002 が渡す）。
   - image: `$GUEST_RUNTIME/session.json` の `image` の path と、その SHA-256。
   - 環境の印（`--env`、既定 E1）と `date -Is`。
   - host: `nproc`、`uptime`（load average）、`/proc/pressure/io`（在れば）、`free -g`。
   - KVM: `python3 plan/tools/qmp.py "$GUEST_RUNTIME/qmp.sock" query-kvm` の答え。
     `enabled` が false なら TCG に落ちている（`guest.py` は `/dev/kvm` に書けないと黙って TCG で動く）。`perf-run: FAIL no KVM` で止める。
   - guest の背景の file の有無（`ls /usr/share/keiland/wallpaper.*`）。
   - host の Mesa の shader の cache の場所と大きさ（`du -sh ~/.cache/mesa_shader_cache* 2>/dev/null`）。2 回目が速い理由の手がかりにする。
3. **monitor**（P-03）: `monitor-p003.sh` の 3 段と同じ command を流す。
   - `--log-frames` の compositor と `/bin/monitor --source=sim --seed=9 --cpus=16 --gpus=2` を起こす。
   - 20 秒待ってから、`ZWL PERF` の全ての行（`ZWL PERF <N>ms:` と `ZWL PERF compose`）と `ZMON FRAME` の行を `OUT/monitor.out` に取る。
   - compositor の log の `ZWL DISPLAY device=` の行（`compose.c:837`）と monitor の `ZMON READY` の行（`monitor/main.c:458`）を、`OUT/device.txt` に取る。
     E2 で Intel かを見るために使う。
   - 判定はしない（記録だけ）。
4. **c5**（P-01）: `C5_ROUNDS=5 sh plan/ws099/tests/c5-transitions.sh OUT/c5 > OUT/c5.out`。
   - 5 回にするのは、1 回目だけ遅い現象を他と分けるため。
   - `C5 RESULT` が FAIL でも止めずに次へ進む。速さを測る段であり、合否の段ではない。
5. **latency**（P-02）: `sh plan/ws139/tests/type-only.sh OUT/latency > OUT/latency.out`。
6. 最後に compositor と app を止める。

各段は `timeout` を付けて呼ぶ（wait 300 s、monitor 120 s、c5 300 s、latency 240 s）。
時間切れはその段の `TIMEOUT` として記録し、次へ進む。

### 4. 表にする道具（`plan/ws139/tests/perf-summary.py OUT`）

`OUT/env.txt`・`OUT/*.out`・`OUT/latency/latency.txt` を読み、`OUT/summary.tsv` と、画面向けの表（markdown）を出す。
列は `id	metric	value	unit	samples	env	image_config	commit`。`image_config` は `config-amd64-perf.mk`。

| id | metric | 取り方 |
| --- | --- | --- |
| P-03 | `compose_frame_ms` | monitor の段の `ZWL PERF compose … frame_ms=` の中央値 |
| P-03 | `compose_draw_ms` | 同じ行の `draw_ms=` の中央値 |
| P-03 | `compose_per_s` | 全ての `ZWL PERF compose` の `frames=` の和 ÷ 全ての `ZWL PERF <N>ms:` の N の和（秒） |
| P-03 | `monitor_fps`・`monitor_callback_ms` | `ZMON FRAME fps= callback_ms=` の中央値 |
| P-01 | `c5_first_frame_ms_round1`・`c5_first_frame_ms_rest` | 1 回目の 4 つの最大と、2 回目以後の中央値（何回目かの決め方は下） |
| P-01 | `c5_gap_ms_max` | `C5 RESULT … gap_max=` |
| P-02 | `textedit_direct_ms`・`terminal_direct_ms` | `LATENCY name=textedit-direct|terminal-direct` の `median_ms`（`max_ms` も別の行に） |

- C5 の行の読み方: 正規表現は `^C5 (\S+)( windows=\d+)?: first_frame_ms=(\S+)`。
  - 何回目かは、行の順ではなく、同じ kind の何回目の出現かで決める。kind は wiseview-open・wiseview-close・home-open・home-close。
  - `first_frame_ms=None` は欠けた値として数えない（`samples` に入れない）。
- 値が取れなかった metric は `value` を `NA` にし、理由（その段の TIMEOUT・行が無い）を表の下に書く。
- host で試す: T の出力が無い間は、過去の形の行を並べた小さな fixture（`plan/ws139/tests/fixture/`）で動かし、列が埋まることを確かめる。
  - fixture の行は、記録から写す（例: T1-057 の `ZWL PERF compose frames=16 draw_ms=103.58 (acquire 1.58 submit+present 79.27) frame_ms=115.11`）。
  - `windows=10` の付いた行と `None` の行も入れる。

### 5. 自分で行う確かめ（QEMU は起動しない）

```sh
sh -n plan/ws139/tests/perf-run.sh plan/ws139/tests/build-perf-image.sh plan/ws139/tests/type-only.sh
python3 plan/ws139/tests/perf-summary.py plan/ws139/tests/fixture   # fixture にある metric が全て埋まる
# 手順 1 の 2 の make --eval の確かめ
```

image の build は T が行う（T は自分の worktree の build/ で作る。AGENTS.md の Q1 の許可）。

## 試験の依頼（T1/T2）

commit と Q1 への merge 依頼の後に依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。

- **image**: `sh plan/ws139/tests/build-perf-image.sh BUILD`（commit の SHA を添える）。
- **guest**: `GUEST_RUNTIME=$PWD/build/ws139-run sh plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img`。
  settings-guest.sh は `GUEST_RUNTIME` を受ける（11 行）。
- **流す物**: 12 分程度かかり、T の Bash の 10 分の上限に当たる（T1-006 では上限で C8 が中断した）。だから background で流して終わりを待つ。
  ```sh
  GUEST_RUNTIME=$PWD/build/ws139-run sh plan/ws139/tests/perf-run.sh build/ws139-perf-e1
  python3 plan/ws139/tests/perf-summary.py build/ws139-perf-e1
  ```
- **返す物**: `summary.tsv`、表、`env.txt`、`device.txt`、各段の `.out`。
- **合否**: 全ての段が TIMEOUT なしで終わり、`summary.tsv` の P-01・P-02・P-03 の metric が `NA` でないこと（速さの値そのものは判定しない）。
- **2 回目**: host の負荷で値が揺れるので、同じ guest でもう 1 回流してもらう（`build/ws139-perf-e1b`）。2 回の差を台帳に書く。

## 受け入れの条件

1. 手順 5 の確かめが通る。
2. T の 2 回の実行が上の合否を満たす。
3. ws.md の台帳の P-01・P-02・P-03 の行に、「2026-xx-xx E1（perf-run、config-amd64-perf.mk）」の値を足す。古い値は消さない。
4. E1 の値の読み方の注意（lavapipe、Venus の 10 ms、VNC の上限、submit の時刻、image の違い）を、表と一緒に「結果」に書く。

## 結果

2026-10-05 P1 generation17（q740）。手順 1〜5 は済み、T1 の E1 の計測（2 回）は未実施。

| 物 | 結果 |
| --- | --- |
| `plan/ws139/tests/config-amd64-perf.mk` | `config-amd64-settings-ime.mk` に monitor を足す。`make --eval` の確かめで wayland・popup-probe・textedit・terminal・keiland-ime・monitor の 6 つが選ばれる |
| `build-perf-image.sh BUILD` | `SETTINGS_CONFIG=… build-settings-image.sh BUILD`（背景と apps.conf はそちらが付ける） |
| `type-only.sh [OUT]` | `latency-bug143.sh` の 0〜2 段を同じ command で写した（compositor は `--testing --timeout=900`、textedit-direct threshold 300・terminal-direct threshold 20 を各 10 回）。出力 `OUT/latency.txt` |
| `perf-run.sh [--env=E1\|E2] OUT` | wait → env（commit・image と SHA-256・印・date・host の CPU・load・IO・memory・KVM（TCG なら FAIL）・背景・Mesa の cache）→ monitor（P-03、`--log-frames` と monitor の sim、20 秒、`monitor.out`・`device.txt`）→ c5（P-01、`C5_ROUNDS=5`）→ latency（P-02）→ 止める。各段の終わり方を `steps.txt` に（c5・latency は timeout の 300・240 s、monitor は guest の call ごとの 90 s の上限で抑える。shell の関数は timeout の下で動かせないため） |
| `perf-summary.py OUT` | `summary.tsv`（id・metric・value・unit・samples・env・image_config・commit）と markdown の表。C5 は kind ごとの出現の回数で回を決め、`None` は数えない。値の無い metric は NA と理由 |
| fixture | `plan/ws139/tests/fixture/`（T1-057 の P-03 の行、T1-006 の C5 の形の行（`windows=10`・`None` を含む）、T1-024 の latency の行）: 12 の metric が全て埋まる |

確かめ: `sh -n` が 4 つの script で通る、`perf-summary.py fixture` が全ての metric を埋める、`make --eval` の確かめ。
image の build と QEMU の計測は T1（下の依頼の 2 回）。台帳（ws.md）の P-01〜P-03 の更新は T1 の結果の後。

## T1-176 の結果（2026-10-05 Q1）

PASS（QEMU Venus、2 回、TIMEOUT 無し、P-01・P-02・P-03 に NA 無し）。E1 の値（e1/e1b）: P-03 compose_frame 100.6/100.1 ms・compose_draw 90.6/89.9 ms・monitor_fps 4.6/4.5、P-01 c5 first_frame 203（初回）→95 ms、P-02 textedit_direct 307/339 ms・terminal_direct 302/306 ms。証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-176-out/`。台帳への記入は P1（WS139 の次の Phase）。
