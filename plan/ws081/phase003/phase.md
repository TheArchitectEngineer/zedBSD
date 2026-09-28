<!-- awesome-plan project=zedbsd record=ws081-p003 -->

# ws081-p003: 補間・予測の library（host で試験、率・jitter ごとの誤差の測定）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。library と host 試験まで。libkeiland の build への組み込みと keiland.h での公開は最初の利用者の p004）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（WS081 の作業用サブエージェント、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（p004 で Makefile・exports.map・keiland.h に入れる）
<!-- awesome-plan-current:end -->

## 範囲

[design.md](../design.md) の §3（resampling・予測・率・遅延・空き・fling の速度・雑音・Scan Time の時刻）と §6（置き場所と API）の library を実装し、
host で試験する。§7 の p003 の試験（1〜7）。Notes の線の spline（§3.8）と慣性・gesture（§5）は含めない（Notes の Phase と p005）。HAL・kernel・compositor に触れない。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/libkeiland/motion.h`（新） | API（p004 で keiland.h へ移すまでの library の中の header）。`keiland_motion_device_create/destroy/time/interval/noise`、`keiland_motion_create/destroy/begin/add/point/velocity/end`、`KEILAND_MOTION_EXTRAPOLATION_CONTENT`（12 ms）・`_INK`（16 ms）。時刻は CLOCK_MONOTONIC の µs、位置は論理 px |
| `userland/desktop/libkeiland/motion.c`（新） | design の lsq12（直線と放物線の最小二乗、`behind = max(0, T̂ + d̂ − E)`、stroke の中では 1 回 0.5 ms まで、外挿の打ち切り `E + T̂/2`、最初の報告より前は描かない、後ろへ予測しない）、率（空きを除いた直近 8 間隔の平均、3 つ集まるまでは device の値）、遅延（直近 8 の中央値）、空きの前の仮の報告、窓の倍率（device の雑音 > 1.5 px で 1.6）、速度（等速 Kalman q 10^7・R max(2, σ̂²)、雑音に比例する静止の門、古い報告の規則、8000 px/s の上限）、雑音（150 ms の放物線の残差、stroke の中央値、device は 16 本の 4 番目・5 本までは 1 px）、stroke の終わりに device へ畳む（重み 0.25）、1 秒の空きで履歴を捨てる、Scan Time の写し（下側の包絡・drift 200 ppm・20 ms で始め直し・0.5 秒の窓で速さ 0.9〜1.1 を検査・信じられない間は host の時刻・逆行しない） |
| `plan/ws081/tests/motion-compare.c` | 外の方式（`KIND_EXTERNAL`・`external_point`）の hook。比較の出力は変わらない（`run-motion-compare.sh` で照合） |
| `plan/ws081/tests/host-motion.c`・`run-motion.sh`（新） | host 試験（下）。比較の program を取り込み、同じ模擬の panel・道・指標で library を 1 つの方式として測る |

Makefile（`LIBZDESKTOP_SOURCES`）・`exports.map`・keiland.h には入れていない（design §6: 公開は最初の利用者の p004）。

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-motion.sh`（host の clang、library は `-std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion`） | `host-motion: ok (426009 checks)`、warning 0 |
| 同（`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"`、`build/ws081-p003-san`） | `ok (426009 checks)` |
| 試験の感度（worktree の使い捨ての `build/ws081-p003-mutate.sh`、motion.c の写しを 1 か所ずつ壊す） | 8 つ全部を検出: 後退の禁止を外す 15 件 FAIL、behind の変化の上限を外す 2、Scan Time を常に信じる 1、仮の報告を置かない 1、静止の門を外す 10、率を最後の間隔にする 14、包絡の drift を外す 1、雑音の窓の倍率を外す 5 |
| target の compile（main の共有の `build/llvm` と sysroot を読むだけ、出力は worktree の `build/ws081-p003-target`）: amd64（`x86_64-unknown-zedbsd`、`-fPIC -Wall -Wextra -Werror -Wconversion`）と arm64（`aarch64-unknown-zedbsd`） | rc=0、warning 0 |
| `plan/tools/style-check.py`（motion.c・motion.h）、`git diff --check` | 指摘 0。clang-format は host に無く未実施 |
| `plan/ws081/tests/run-motion-compare.sh`（hook を足した後） | 出力が `motion-compare.out` と一致 |

試験の中身と結果:

1. **正確さ**: 一定の直線は design の式の時刻（6 ms 後ろ）の直線上に 1e-6 px 以内、遠い未来は外挿の打ち切り（12 + 8 ms）で止まる。減速する放物線は、最後の報告より前（補間）でも後（予測、放物線が選ばれる）でも 1e-6 px 以内。
2. **端の入力**: 報告 0 は ENOENT、1 つはその点・速度 0、時刻の戻りは EINVAL、同じ時刻の束で有限の値、1 秒の空きで履歴を捨てる、NULL の拒否、behind の変化（遅延が 2 → 12 ms に跳ぶ）が 1 回 0.5 ms。
3. **率**: 90 Hz を 8 ms の poll で読む alias で 11.1 ms の ±15% 以内、欠落 5% で −3〜+10% 以内。
4. **Scan Time**: 正しい時計・200 ppm 速い・遅い・wrap・1.2 秒の空きの後の始め直しで、host より後にならず逆行せず、信じてからは scan + 最小の遅延から 400 µs 以内。止まった Scan Time と 10 倍速い Scan Time は host の時刻のまま。
5. **雑音の推定**（16 本の混ぜた道、60 Hz）: 雑音 0 px → 0.426 px、1 px → 1.027 px、2.5 px → 2.555 px（design §3.6 の「±30%、停止・曲がり角で膨らまない」を満たす）。
6. **resampling を design の方式と同じ報告で**（ideal・usb は lsq12g-e12、cheap-scan は窓を広げた lsq12w-e12 と。device は 16 本で学ばせてから）: 全 15 cell で judder +1 point・遅れ +0.5 ms・overshoot +0.5 px・後退 +0.5 px 以内。例 usb 60 Hz: judder 9.37%（ref 9.38%）、遅れ 7.11 ms（7.11）、overshoot 2.79 px（2.79）。cheap-scan 60 Hz: 17.23%（16.97%）、12.01 ms（11.99）、8.53 px（8.39）。
7. **静止中に黙る panel の動き始め**: usb・cheap-scan の 5 率で、動き始め 200 ms の遅れが lsq12g-e12（周期を与えたもの）に対して平均 +1 ms・最大 +5 ms 以内（usb 60 Hz は 7.2/16.8 ms で同じ、差の最大は cheap-scan 30 Hz の平均 +0.7 ms）。
8. **fling の速度を design の推定（Kalman + 雑音の門）と同じ報告で**（1024 本）: 3 種の flick の平均の誤差が 2 point 以内（usb 60 Hz: −4.1%・−4.0%・+13.9% で同じ）。停止の後の p99 は usb で 0、cheap-scan で 300 px/s 未満（率ごとの p99 の最大は 45 Hz の 230 px/s）。

## design からの差・判断

- behind の変化の上限は、stroke が自分の周期を持つまで（空きでない間隔が 3 つ）かけない（device の値から stroke の値へ移る間に 0.5 ms ずつでは追いつかないため）。design §3.3 の 2 の「stroke の始めに device の値から決め」を、「周期が測れるまでは式のとおり、以後は 0.5 ms ずつ」とした。
- 雑音の stroke の値は、4 報告ごとの測定（最大 16）の中央値。device の値は design のとおり 16 本の 4 番目。
- Scan Time の検査は、0.5 秒の host の時間ごとに判定し、最初の窓が済むまでは信じない（design §3.7 の「0.5 秒以上の区間で」の具体化）。空きの後の始め直しでも信頼は保つ（panel は変わらない）。

## 未実施・制限

- guest（QEMU）・実機では動かしていない（library は build に入っていない。p004 で compositor が使う）。
- 数値は design と同じ合成の模型の上のもの。
- clang-format は host に無く未実施。

## 残り

- p004: Makefile の `LIBZDESKTOP_SOURCES` と `exports.map`（`keiland_motion_*`）、keiland.h への宣言と `KEILAND_VERSION` 9、motion.h の削除か内部化、compositor での使用。
