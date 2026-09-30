# Windows の QEMU で touch の報告を測る（ws081-p016、L2）

目標（WS081 の L2）: Windows の touch panel の指が Kei に届き、**報告の率 ≥ 60 Hz、報告の欠け 0**（10 秒の drag）。

## ユーザーの手順（3 行）

1. `C:\Work\winq-zedbsd\boot.bat` を起動し、Kei の desktop が出るまで待つ。
2. Windows の PowerShell で `ssh -p 2222 root@127.0.0.1 touchlog --seconds=15` を打つ（password は `root`）。
   「touchlog: recording … drag a finger now」と出る。
3. すぐに Kei の画面の上を指 1 本で 10 秒ほど大きく左右に drag し続ける。15 秒で `SUMMARY` の 6 行が出るので、そのまま貼って送る。

## 結果の読み方（エージェント）

| 行 | 見るもの | 合格 |
| --- | --- | --- |
| `SUMMARY touching_seconds= rate_hz=` | 指が触れていた間の報告の率 | `rate_hz` ≥ 60 |
| `SUMMARY scan_gaps=` | touch panel の scan の時刻（HID の Scan Time、`MSC_TIMESTAMP`）の間隔で、中央値の 1.5 倍を超えたもの（panel が送らなかったか、途中で落ちた報告） | 0 |
| `SUMMARY arrival_gaps=` | Kei に届いた時刻の間隔の欠け（QEMU・Kei の側の遅れ） | 0 |
| `SUMMARY scan_intervals= median_ms= p95_ms= max_ms=` | 報告の間隔の分布 | median ≤ 16.7 ms |

- `rate_hz` が 60 を下回り `scan_gaps` も多いなら、QEMU の fork の SDL が event を間引いている疑い（ws.md の進め方）。
- `scan_gaps` が 0 で `arrival_gaps` だけ多いなら、Kei の側（USB の poll、evdev の読み）を疑う。
- `touchlog: no touch screen` なら、QEMU の `usb-multitouch` が付いていない（boot.bat の `-device usb-multitouch,bus=xhci.0,port=1`）。

## 用意（エージェントか Q1、ユーザーの手順の前に 1 回）

- image に `touchlog` を入れる: `sh plan/ws081/tests/build-touchlog.sh build/amd64` で `build/ws081-tests/touchlog` を作り、Windows に置く
  image の config に `ZEDBSD_EXTRA_FILES += --file /usr/bin/touchlog=build/ws081-tests/touchlog` を足して build する（`config-amd64-touchlog.mk` と同じ形）。
  その image を `C:\Work\winq-zedbsd\data\hdd-image.img` に置く。
- SSH: image に `openssh` があること（既定の image にはある）と、boot.bat の QEMU の network に `hostfwd=tcp::2222-:22` があること。
  無ければ boot.bat の `-nic`（または `-netdev user`）にそれを足す。
- 道具の確かめ（Linux の QEMU、注入の touch）: `plan/ws081/tests/touchlog-check.sh`（60 Hz・90 Hz の揺れ・3 つの欠けを正しく数える）。

## 参考: 画面の反応までの時間（L3、p017）

zdesktop を `--log-frames` で起こした session の log（`/run/user/1000/session.log`、または試験の `/tmp/zdesktop.log`）を
`python3 plan/ws081/tests/touch-latency.py LOG` で読むと、指を置いてから最初の frame までの p50・p95・最長と、慣性の scroll の frame の間隔の最長が出る。
