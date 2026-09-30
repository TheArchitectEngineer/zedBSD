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

## 用意（ユーザーの手順の前に 1 回。自分で確かめられる形）

1. **image**: Q1 が置く demo の image（`build/ws081-demo-win/hdd-image.img` を main の `build/` に複写したもの。touchlog・Notes・PDF Viewer・
   Text Editor・スクリーンキーボードの入った Windows の QEMU 用）を、`C:\Work\winq-zedbsd\data\hdd-image.img` に上書きで複写する
   （元の image を残したいなら先に名前を変えておく）。
2. **boot.bat の ssh の転送**: `C:\Work\winq-zedbsd\boot.bat` をメモ帳で開き、QEMU の行に `hostfwd=tcp::2222-:22` があるかを見る。
   - ある: そのままでよい。
   - 無い: network の引数に足す。例（usb-net を使う場合、QEMU の行の最後に 1 つ足す）:
     `-netdev user,id=net0,hostfwd=tcp::2222-:22 -device usb-net,bus=xhci.0,port=2,netdev=net0`
     （既に `-netdev user,id=…` があれば、その後ろに `,hostfwd=tcp::2222-:22` だけを足す）。
3. **確かめ**: boot.bat で起動し、Kei の desktop が出たら PowerShell で `ssh -p 2222 root@127.0.0.1 touchlog --seconds=3` を打つ（password `root`）。
   - 「touchlog: recording /dev/input/event…」の後に `SUMMARY` の行が出れば用意は済み（触らなければ `touching=0` でよい）。
   - `Connection refused` なら 2 の転送が無い。`touchlog: not found` なら 1 の image が古い。`touchlog: no touch screen` なら boot.bat に
     `-device usb-multitouch,bus=xhci.0,port=1` が無い。

## 参考: 画面の反応までの時間（L3、p017）

zdesktop を `--log-frames` で起こした session の log（`/run/user/1000/session.log`、または試験の `/tmp/zdesktop.log`）を
`python3 plan/ws081/tests/touch-latency.py LOG` で読むと、指を置いてから最初の frame までの p50・p95・最長と、慣性の scroll の frame の間隔の最長が出る。
