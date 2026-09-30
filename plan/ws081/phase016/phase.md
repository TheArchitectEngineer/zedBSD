<!-- awesome-plan project=zedbsd record=ws081-p016 -->

# ws081-p016: Windows の QEMU の touch の計測の準備（L2）

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`、main を merge した上）。
Linux の QEMU の Venus で道具を確かめた。Windows の機械の計測はユーザー（未実施））
Disposition: normal
Parent: [WS081](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て「WS081 の L2 の準備: ws081-p016。ユーザーが 3 行の手順で計測できる状態まで」）

## 範囲と受け入れ

1. Kei の側で touch の evdev の報告の時刻（`MSC_TIMESTAMP` と到着の時刻）を記録する道具と、10 秒の drag の報告の数・間隔の分布・欠けの集計。
2. 注入の touch で、道具が正しく数えることを確かめる（報告の率の既知の値と比べる）。
3. ユーザー向けの手順 `plan/ws081/tests/windows-touch.md`（boot.bat → ssh で記録 → 10 秒 drag → 結果を読む）。
4. compositor の log から「指を置いてから画面の反応まで」の時間を出す script（L3 の p017 用）を QEMU で動くところまで。

## 作ったもの（`plan/ws081/tests/`）

- `touchlog.c`: evdev の touch screen（指定、または multitouch の位置を持つ最初の device）の SYN_REPORT ごとに、到着の時刻（CLOCK_MONOTONIC）、
  kernel の時刻、`MSC_TIMESTAMP`（scan の時刻）、指の数を記録する（`plan/ws084/tests/evlat.c` を元にした）。
  - 終わりに `SUMMARY` を出す: 指の触れていた報告の数・時間・率、scan の時刻と到着の時刻の間隔（中央値・p95・最長）、欠け
    （中央値の 1.5 倍を超える間隔）。
  - `--seconds=`（既定 15）、`--raw=`（1 行 1 報告の生の log）、SIGINT・SIGTERM で止まる。
- `build-touchlog.sh`: build の clang と sysroot で Kei 用に build する（`build/ws081-tests/touchlog`）。`image` を付けると試験の image も作る。
- `config-amd64-touchlog.mk`: pen の image（注入の touch）に `/usr/bin/touchlog`。
- `touchlog-check.sh`: 下の確かめ。
- `touch-latency.py`: zdesktop の `--log-frames` の log の `ZWL TOUCH report … host_ms=` と `ZWL COMPOSE … at_ms=` から、指を置いてから最初の合成の
  frame までの時間（件数・p50・p95・最長）と、動いた stroke の後の慣性の frame の間隔の最長を出す。
- `windows-touch.md`: ユーザーの 3 行の手順、結果の読み方、用意（image に touchlog、boot.bat の ssh の転送）。

## 確かめ（`touchlog-check.sh`、Linux の QEMU の Venus、`build/ws081-touchlog.img`）

**PASS**（`build/ws081-check6.log`、生の log と集計は `build/ws081-shots/p016/`）。

注入の touch は kernel の試験の touch screen（touchinject の `scan`: USB の touch screen の報告の経路と HID の Scan Time）。
Q1 の指示の「Linux の QEMU の `usb-multitouch`（WS085 と同じ device）」は、この host の QEMU に無い（`-device help` に `usb-multitouch` が無い。
WS085 の fork の source もこの機械に無い）ので、この注入で代えた。Kei の側の evdev（`MSC_TIMESTAMP` と SYN_REPORT）は同じ形。

| 場合 | 送ったもの | touchlog の結果 |
| --- | --- | --- |
| A 60 Hz | 1 本の指で 10 秒、16.667 ms ごとに 601 報告 | touching=601、rate 60.1 Hz、scan の中央値 16.70 ms、欠け 0 |
| B 90 Hz の揺れ | 11.111 ms ごと ±3 ms の揺れで 901 報告 | touching=901、rate 90.3 Hz、p95 13.8 ms、欠け 0 |
| C 欠け 3 | 60 Hz の drag の途中で 100 ms の間を 3 回 | scan_gaps=3、arrival_gaps=3（最長 116.7 ms） |
| D・E 画面の反応 | desktop の上の 10 回の tap と drag、Terminal の長い出力の fling | `touch-latency.py`: downs=15、p50 101 ms・p95 102 ms、慣性の frame の間隔の最長 246 ms |

- D の約 100 ms は、QEMU の Venus（lavapipe）での、指の報告から次の合成までの時間。合成の間隔そのものが約 110〜140 ms（この機械で約 8 fps）なので、
  L3 の目標（p95 ≤ 50 ms、frame の間隔 ≤ 33 ms）は Linux の QEMU では測れない。Windows の WHPX（WS085: 合成 16〜34 ms/frame）で p017 が測る。
  tap の直後の合成が約 100 ms 後に揃うのは、tap の判定（指を離すまで待つ）を含むため（推測）。p017 で drag の始まりと分けて見る。
- 途中の直し: touchinject の device は再生の間だけあるので、記録は再生を始めてから起こす形にした。
  fling の最初の指は device の登録の前だと届かないので、待ちを 3 秒にした。
  慣性の判定は動いた stroke だけにし、終わりの間隔を 250 ms にした（QEMU の合成が遅いため）。

## 未実施・残り

- Windows の機械での計測（ユーザー、`windows-touch.md` の 3 行）。その前に用意が要る（Q1 かエージェント）:
  - touchlog を入れた image を `C:\Work\winq-zedbsd\data\hdd-image.img` に置く。
  - boot.bat の network に `hostfwd=tcp::2222-:22` があるか確かめ、無ければ足す（boot.bat の中身はこの機械から見えない）。
- L3（p017）: `touch-latency.py` を Windows の session の log（zdesktop を `--log-frames` で）に使う。session の compositor に `--log-frames` を付ける方法は p017 で決める。
