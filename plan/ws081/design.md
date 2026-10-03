# WS081 設計: touch の時刻・補間と予測・app への渡し方・慣性（ws081-p001）

2026-09-29、WS081 の作業用サブエージェント。対象は [ws.md](ws.md) の「設計で決めること」の全項目。数式の比較の数値は、
host の試験 [tests/motion-compare.c](tests/motion-compare.c)（決定的。`plan/ws081/tests/run-motion-compare.sh` で build・実行し、
出力を [tests/motion-compare.out](tests/motion-compare.out) と照合する）の出力から引いた。design-reviewer の敵対的レビュー（§13）の指摘を
source と試算で確かめて反映した。実機の証拠は無い（touch LCD は未着）。

## 1. 今の事実（2026-09-28 の main のコード）

| 層 | 事実 | 場所 |
| --- | --- | --- |
| USB HID の受け取り | interrupt の URB は 1 本。完了の callback（`usb_hid_completion`）は `work_pending` を立てて worker thread を起こすだけで、decode・`drv_input_device_emit`・URB の出し直しは worker が順に行う。decode が終わるまで次の URB は出ないので、完了の時点の値はその buffer のもの | `src/drivers/usb/usb-hid.c`（1083〜1104、1427〜1472 行） |
| 完了の callback の文脈 | host controller による。xHCI は割り込みから呼ぶが、割り込みの moderation（IMOD 4000 × 250 ns = 1 ms、interrupter は全 device で共有）で最大約 1 ms 遅れうる。UHCI・EHCI は retirement worker thread から呼ぶ（その thread の遅れを含む） | `src/drivers/pci/pci-xhci.c`（`KERN_XHCI_IMOD`）、`pci-uhci.c`（`uhci_retirement_worker`）、`pci-ehci.c` |
| evdev の時刻 | `drv_input_device_emit` が **event 1 つごとに** `clock_milliseconds()`（tick。amd64・arm64 は 1 kHz、i386・m68030・sparcv9 は 100 Hz）を読んで `struct timeval` に入れる。値は CLOCK_MONOTONIC の ms で `tv_usec` は 1000 の倍数。1 つの報告の event が別々の ms を持ちうる。時刻は worker が走った時で、完了からの scheduling の遅れを含む | `src/drivers/generic/input.c`（863 行、`report_timestamp`） |
| 時計 | CLOCK_MONOTONIC（`kern_clock_gettime`）は tick の分解能。高分解能の counter（amd64 TSC、arm64 CNTVCT）は kernel の `kern_rtc_read_counter()`（`include/kern/clock.h`、実装は `src/kern/pmem.c` で HAL の `hal_rtc_read_counter` を呼ぶ）で読めるが、CLOCK_MONOTONIC と evdev は使っていない | `src/kern/clock.c` |
| touch の状態機械 | Touch Screen の Finger から protocol B（`ABS_MT_*`・`BTN_TOUCH`・`ABS_X/Y`）。Contact Count・hybrid あり。**Scan Time（0x0D:0x56）は読まない**。**変化の無い frame は何も出さない**（指を止めると evdev は黙る）。count の途中で次の count が来ると、開いた frame を書いてから新しい frame を始める（1 報告から 2 つの SYN frame） | `src/drivers/usb/hid-touch.c`（233、269〜275 行）、`usb-hid.c`（`touch_item`） |
| input の capability | `EV_SYN`・`EV_KEY`・`EV_REL`・`EV_ABS` だけ。`EV_MSC` は capability にも publish にも無い | `src/drivers/generic/input.c`（`capability_code_valid`・`drv_input_capability_event`）、`include/kern/input-capability.h` |
| 注入の device | touch の frame を kernel の中で USB と同じ状態機械に通す。時刻は write の時。setup と frame の `reserved` は 0 でなければ EINVAL（touchinject の自己試験がそれを確かめる） | `include/uapi/input-inject.h`、`src/drivers/generic/input-inject.c`、`userland/base/tests/touchinject/main.c` |
| compositor | SYN_REPORT の時刻（ms）で frame を `zwl_touch_frame` に渡す。EV_MSC は 64 event の frame に積まれ、touch.c は EV_ABS 以外を捨てる。wl_touch の down/motion/up は生の点。wl_touch を bind した client には pointer の代わりを送らない。frame は描くものがあるときだけ描き、fence の完了で歩調を取る（vsync の時刻は持たない） | `userland/desktop/wayland/input.c`・`touch.c`・`display.c`・`compose.c` |
| app | wl_touch を bind する Keiland の app は無い（Notes のペンは tablet、指は pointer の代わり）。browser は wheel の px を `browser_view` に渡す API を持つが、scroll の範囲を決める API も、delta を使い切ったかの返りも無い | `userland/desktop/*`、`include/libc/browser.h`（250〜267 行） |
| touchpad | Touch Pad（0x0D:0x05）は未対応。二本指の scroll も無い | — |

## 2. 時刻（kernel、p002）

### 2.1 何が効くか（数値の根拠）

§3 の試験で報告の時刻の付け方だけを変え、同じ補間（既定の方式 `lsq12-e12`、§3.3）を比べた（60 Hz の表示、雑音 1 px、周期の jitter 10%）。
`tick` は今の kernel の模型（worker が 0.05 ms + 平均 0.3 ms の指数の遅れ、5% は 1〜4 ms 余計に遅れて tick の ms を読む。**模型で、実測ではない**）、
`usb` は完了を host が知った時刻（完了 + 0〜1 ms の割り込みの揺れ）を µs で、`usb-ms` はそれを ms に丸めたもの、`scan` は panel の Scan Time、
`scan-ms` はそれを ms に丸めたもの。`cheap-*` は安い panel（jitter 30%、報告の 5% が失われる、雑音 2.5 px、8 ms ごとの poll。1 回の poll で送る報告は 1 つで、
待つ報告は次の poll へ）。

| 条件 | 率 | 誤差 rms px | 遅れ ms | judder | 停止の overshoot px |
| --- | --- | --- | --- | --- | --- |
| tick | 60 | 11.54 | 7.51 | 11.9% | 2.99 |
| usb | 60 | 10.87 | 7.11 | 9.4% | 2.79 |
| usb-ms | 60 | 10.81 | 7.05 | 10.1% | 2.78 |
| scan | 60 | 10.87 | 7.12 | 8.0% | 2.83 |
| tick | 120 | 3.50 | 1.89 | 10.1% | 3.77 |
| usb | 120 | 3.30 | 1.89 | 7.6% | 3.13 |
| usb-ms | 120 | 2.78 | 1.40 | 8.4% | 3.30 |
| cheap-tick | 60 | 19.58 | 12.00 | 33.2% | 6.40 |
| cheap-usb | 60 | 19.05 | 11.65 | 32.6% | 6.28 |
| cheap-scan | 60 | 19.12 | 12.12 | 21.2% | 6.09 |
| cheap-scan-ms | 60 | 19.17 | 12.14 | 21.5% | 6.09 |
| cheap-usb | 120 | 12.36 | 6.87 | 28.6% | 6.06 |
| cheap-scan | 120 | 8.97 | 4.67 | 19.7% | 7.00 |

（judder は 1500 px/s の一定の scroll で frame ごとの移動量のばらつきの rms を、理想の移動量 25 px に対する % で。全表は出力の file。）

読み取り:

1. **完了の時点で時刻を取る**（worker で取らない）と judder が約 2 point 下がる（tick → usb-ms: 11.9 → 10.1%、120 Hz で 10.1 → 8.4%）。
2. **µs にしても伸びは小さい**（usb-ms → usb: 60 Hz で 0.7 point）。USB の poll と割り込みの揺れが ms 程度あるうえ、CLOCK_MONOTONIC（使う側の時計）が
   ms だから。ms で始め、µs は時計ごと直すとき（§2.2 の 2）にする。
3. **Scan Time が効くのは poll の遅い panel**: 8 ms で poll される安い panel は、完了の時刻では間隔が崩れる。panel 自身の Scan Time を使うと
   judder が 32.6% → 21.2%（60 Hz）、28.6% → 19.7%（120 Hz）。ms に丸めても（`cheap-scan-ms`）ほぼ同じなので、app は wl_touch の ms の時刻で足りる。

### 2.2 決定

1. **1 つの報告に 1 つの時刻、完了を host が知った時刻**: `usb_hid_completion` で `clock_milliseconds()`（atomic な load、割り込みの中でも読める）を読み、
   `work_pending` と同じ `hid->lock` の下で `struct usb_hid` に置く。worker はその報告の event を全部その時刻で出す。input の層に時刻を受ける emit
   （例 `drv_input_device_emit_at(device, type, code, value, milliseconds)`）を足し、今の `drv_input_device_emit` はそれを今の時刻で呼ぶ形にする。
   mouse・keyboard・pen・touch の USB HID の報告が全部これを使う。時刻の質は host controller による（xHCI は割り込みの moderation の 1 ms まで、
   UHCI・EHCI は retirement thread の遅れを含む）。USB の frame の番号は使わない（host controller ごとに違い、URB の API に出ておらず、完了の時刻と
   Scan Time で足りる）。
2. **evdev の時刻の意味**: 「その報告が host に届いたことを kernel が知った時刻を、CLOCK_MONOTONIC の ms で」。`clock_gettime(CLOCK_MONOTONIC)` と同じ基準なので、
   compositor と app は自分の時計と比べられる（遅延の測定、§3.4）。1 つの frame の event は同じ時刻を持つ。分解能は ms のまま（2.1 の 2）。
   µs にするなら CLOCK_MONOTONIC ごと（`src/kern/clock.c` で tick に counter の端数を足す）が筋で、WS081 の外の改善として Future Work の候補に置く。
   desktop の対象（amd64、arm64）は 1 kHz の tick。100 Hz の arch では 10 ms 刻みになり、この設計の数値は当てはまらない。
3. **Scan Time を `EV_MSC`/`MSC_TIMESTAMP` で出す**（Linux の hid-multitouch の event と同じ名と単位。code は書き写さない。下の差は意図したもの）:
   - parser: Touch Screen の collection の中で Finger の外にある Digitizer の Scan Time（0x0D:0x56）を、Contact Count と同じく報告の field にする。
   - 単位: field の Unit が時間（SI の秒）で Unit Exponent があればそれから µs を求める。そうでなければ（Unit が無い、Unit が時間でない。Unit と Unit Exponent は
     global の item なので X・Y の cm が残っていることがある）Windows の要件の 100 µs とみなす。wrap は logical maximum + 1（Linux は logical maximum を
     使うと記憶するが確かめていない。この設計は +1 を正とする）。
   - 状態機械（`hid-touch.c`）: **受け取った報告ごとに**前の報告の Scan Time との差（負なら wrap を足す）を µs にして積む。前の報告から host の時刻で
     1 秒以上空いたら 0 から始め直す。frame を書くときに `MSC_TIMESTAMP`（µs、`int32_t` に入れ、2^32 で wrap）を SYN_REPORT の前に出す。
     書く frame の値は、その frame を始めた報告の積算の値（1 報告から 2 つの frame を書くときは、書き出す古い frame は前の報告の値、新しい frame は今の報告の値）。
   - **Scan Time を持つ device は、変化の無い報告でも `MSC_TIMESTAMP` と SYN_REPORT を出す**（指が止まっても報告の歩調が evdev に届き、率の測定と
     離す前の停止の判定が保たれる。Linux の input core も timestamp だけの frame を渡すと記憶するが確かめていない）。Scan Time の無い device は
     今のとおり変化の無い frame は何も出さない（userland が空きを扱う、§3.4）。
   - capability: Scan Time を持つ touch screen だけ `EV_MSC`・`MSC_TIMESTAMP` を持つ。`HID_TOUCH_EVENT_MAX` は frame ごとに 1 つ増える（1 報告で 2 frame
     なので 2）。
   - input の層: capability に `EV_MSC`（`msc_bits`、`EVIOCGBIT(EV_MSC)`、`INPUT_CAPABILITY_COUNT_MAX` に 1）。`drv_input_capability_event` は持っている MSC の
     code を状態なしで通す。`include/uapi/input.h` に `MSC_TIMESTAMP`（0x05）と `MSC_MAX`（0x07）を**足す**（Linux と同じ値。既存の値と `input_event` の
     layout は変えない）。
   - device の時刻と host の時刻の合わせ方と、Scan Time の妥当性の検査は userland の library（§3.7）。kernel は evdev の時刻（host）を書き換えない。
4. **率の測定は kernel でしない**（ws.md の p002 の「報告の率の測定と公開」を変える。§8）。率は userland の library が報告の時刻から測る（§3.4）。
   kernel が測って ioctl で公開しても、時刻から測れる以上の情報は無く、新しい ABI が増えるだけ。panel の率は指の数や mode で変わるので、使う側が測る方が正しい。
5. **注入の device**（§11 の判断）: setup の `reserved` の bit 0 を「この device の frame は Scan Time を持つ」の宣言にし、そのとき frame の `reserved` を
   Scan Time（100 µs 単位、65536 で wrap）とする。宣言の無い device は今のとおり（`reserved` が 0 でなければ EINVAL）。device ごとの宣言なので、
   「capability のある device は毎 frame `MSC_TIMESTAMP` を出す」意味が保たれる。touchinject の自己試験（`reserved` = 1 が EINVAL）の直しが要る。
6. **HAL**: 変えない（`clock_milliseconds()` と input の層だけ）。hal.h の差分の案は無い。
7. **p002 の確認**: 違いの証明は host 試験で行う（Scan Time の decode・単位・wrap・1 秒の reset・hybrid の 2 frame・変化の無い報告の `MSC_TIMESTAMP`、
   `EV_MSC` の capability、1 報告 1 時刻）。QEMU の usb-tablet は event がほぼ同じ ms に出るので差を見分けられず、回帰（USB HID の pointer が動く）と
   boot test だけに使う。

## 3. 補間・予測の数式（library、p003）

### 3.1 評価の方法

- 指の真の道: 一定の scroll（1500 px/s）、円（120 px、1.5 回転/s）、急な停止（1500 px/s から 30 ms で止まる）、往復（±150 px、2 Hz）、
  flick 3 種（静止 50 ms の後 120 ms で 3000 または 800 px/s まで加速して加速度 0 で離す、3000 まで加速した後 30 ms で 2400 px/s に減速して離す）、
  停止してから離す（1000 px/s → 20 ms で止まり 120 ms 静止）、400 ms 静止した後に 1500 px/s で動く（静止中は黙る panel）。
- panel: 率 30・45・60・90・120 Hz、scan の周期に一様な jitter、報告の欠落、位置に正規の雑音。USB の poll（1 ms か 8 ms、1 回に 1 報告）で完了し、
  host は 0〜1 ms 後にそれを知る。§2.1 の時刻を付け、知った 1 ms 後に消費者が読める。消費者は到着を自分の時計（ms）で記録する。
- 表示: 60 Hz、位相は乱数。消費者の時計は ms（今の CLOCK_MONOTONIC と同じ）。各 frame で方式が描く点を、その frame の時刻の真の位置と比べる。
- 測るもの: 誤差の rms、遅れ（進行方向の誤差 ÷ 速さ）、judder（一定の scroll の frame の歩みのばらつき）、円の誤差と半径方向の誤差（形のずれ）、
  停止の後の overshoot（止まった点を越えた最大の距離）、往復の誤差、後退（一定の scroll と停止で、描く点が前の frame より戻った最大。停止の後に
  overshoot から戻る分を含む）。速度は離した時の推定と真の値の比（平均と散らばり）と、停止してから離した時の |v| の 95・99 percentile と最大（0 であるべき）。
- resampling の 1 cell は 64 本、速度の 1 cell は 1024 本の stroke（seed は固定、方式の間で同じ）。

比べた方式（`motion-compare.c` の `methods[]`）:

| 名 | 内容 |
| --- | --- |
| hold | 最後の報告をそのまま |
| android | frame の 5 ms 前を、前後の報告で線形補間、無ければ最後の 2 点で外挿（最後の間隔の半分と 8 ms まで、間隔が 2 ms 未満か 20 ms 超なら外挿しない） |
| lerp-T | 測った間隔 1 つ分前を線形補間 |
| lsq1-p / lsq2-p | 直近の窓（直線: 2.5 間隔・最低 40 ms、放物線: 3.5 間隔・最低 60 ms）の最小二乗の直線・放物線を frame の時刻で（予測、1 間隔 + 5 ms まで） |
| lsq12-p | 同じ窓の直線と放物線を両方あて、最後の報告より前は放物線、後（予測）は直線と放物線のうち進行方向に遠くへ行かない方（後ろへは行かない） |
| lsq2-eN / lsq12-eN | 外挿が N ms を超えないように frame より `behind = T̂ + d̂ − N`（負なら 0）前で評価（T̂ は測った間隔、d̂ は測った遅延）。外挿は N + T̂/2 で打ち切る |
| lsq12w-eN / lsq12a-eN | 窓を 1.6 倍 / stroke の中の残差の雑音が 1.5 px を超えたら 1.6 倍 |
| lsq12g-e12 | lsq12-e12 に、報告の空きの前へ仮の報告を置く（§3.4） |
| kal-eN | 等速の Kalman（白色の加速度 q = 10^6 px²/s³、観測の雑音 2 px²）を同じ規則で |
| ab-p | 固定 gain の α-β（α 0.6、β 0.3） |
| 1euro-p | 1€ filter（min cutoff 1 Hz、β 0.007、d cutoff 1 Hz）+ その速度で予測 |

### 3.2 結果（抜粋。全表は出力の file）

普通の panel（`usb`: jitter 10%、雑音 1 px、1 ms poll）:

| 率 | 方式 | 誤差 rms px | 遅れ ms | judder | 円 rms px | 半径 px | overshoot px | 後退 px |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | hold | 32.60 | 19.40 | 100.3% | 23.89 | 1.01 | 1.37 | 1.02 |
| 30 | android | 32.71 | 19.68 | 97.3% | 23.99 | 1.02 | 1.37 | 0.99 |
| 30 | lerp-T | 52.69 | 35.10 | 4.4% | 39.48 | 1.38 | 1.14 | 0.52 |
| 30 | lsq1-p | 3.38 | 1.80 | 6.5% | 12.15 | 10.48 | 31.82 | 12.07 |
| 30 | lsq2-p | 4.09 | 1.80 | 10.9% | 3.72 | 2.68 | 22.48 | 11.71 |
| 30 | lsq12-p | 4.01 | 2.03 | 9.0% | 10.12 | 8.81 | 22.34 | 10.64 |
| 30 | lsq2-e12 | 35.53 | 23.65 | 5.7% | 26.83 | 1.02 | 4.97 | 3.30 |
| 30 | **lsq12-e12** | 35.57 | 23.67 | **5.4%** | 27.34 | 1.90 | **4.97** | 2.51 |
| 30 | lsq12-e16 | 29.62 | 19.69 | 6.0% | 23.07 | 2.74 | 6.45 | 3.14 |
| 30 | kal-e12 | 35.53 | 23.65 | 5.3% | 27.15 | 1.58 | 4.56 | 1.68 |
| 30 | ab-p | 4.46 | 2.00 | 6.3% | 29.10 | 22.85 | 44.53 | 9.60 |
| 30 | 1euro-p | 17.27 | 10.19 | 26.2% | 39.35 | 11.56 | 35.48 | 12.70 |
| 60 | hold | 18.18 | 11.11 | 25.4% | 13.13 | 0.99 | 1.70 | 1.23 |
| 60 | android | 11.41 | 7.37 | 12.0% | 8.62 | 1.42 | 3.02 | 1.89 |
| 60 | lsq1-p | 3.34 | 1.78 | 8.5% | 4.24 | 3.12 | 9.62 | 3.26 |
| 60 | lsq2-p | 4.10 | 1.77 | 14.8% | 3.28 | 1.88 | 4.83 | 4.32 |
| 60 | lsq12-p | 4.01 | 2.02 | 11.9% | 3.76 | 2.46 | 4.23 | 2.66 |
| 60 | lsq2-e12 | 10.75 | 6.99 | 10.8% | 8.00 | 1.35 | 3.05 | 2.44 |
| 60 | **lsq12-e12** | 10.87 | 7.11 | **9.4%** | 8.30 | 1.59 | **2.79** | **1.58** |
| 60 | lsq12-e16 | 5.45 | 3.20 | 11.5% | 4.56 | 2.24 | 3.85 | 2.36 |
| 60 | kal-e12 | 10.64 | 6.99 | 7.3% | 8.52 | 2.03 | 5.53 | 1.89 |
| 60 | ab-p | 2.98 | 1.74 | 5.3% | 8.02 | 6.83 | 17.66 | 3.41 |
| 60 | 1euro-p | 11.65 | 7.12 | 13.8% | 26.70 | 8.17 | 18.40 | 4.23 |
| 120 | hold | 10.77 | 6.59 | 17.0% | 7.90 | 1.00 | 1.64 | 1.22 |
| 120 | android | 10.36 | 6.81 | 7.5% | 7.86 | 1.05 | 1.83 | 1.39 |
| 120 | lsq2-e12 | 3.28 | 1.81 | 8.3% | 2.52 | 1.17 | 3.27 | 3.27 |
| 120 | **lsq12-e12** | 3.30 | 1.89 | **7.6%** | 2.78 | 1.43 | **3.13** | 2.36 |
| 120 | kal-e12 | 3.08 | 1.81 | 6.5% | 3.16 | 1.90 | 6.27 | 2.32 |
| 120 | ab-p | 3.01 | 1.77 | 6.2% | 3.21 | 2.02 | 7.04 | 2.46 |

安い panel（`cheap-scan`: jitter 30%、欠落 5%、雑音 2.5 px、8 ms poll、Scan Time の時刻）:

| 率 | 方式 | 誤差 rms px | 遅れ ms | judder | 円 rms px | overshoot px | 後退 px |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | hold | 41.85 | 24.55 | 112.1% | 31.53 | 3.04 | 2.43 |
| 30 | lsq12-e12 | 44.95 | 29.50 | 18.8% | 34.45 | 6.74 | 3.22 |
| 30 | lsq12w-e12 | 44.89 | 29.48 | 18.3% | 35.16 | 11.76 | 5.31 |
| 30 | ab-p | 9.94 | 1.90 | 34.4% | 36.89 | 53.84 | 11.77 |
| 60 | hold | 25.75 | 15.45 | 54.6% | 19.09 | 4.15 | 2.86 |
| 60 | android | 19.36 | 10.72 | 48.9% | 14.73 | 6.25 | 4.53 |
| 60 | lsq12-e12 | 19.12 | 12.12 | 21.2% | 14.69 | 6.09 | 3.56 |
| 60 | lsq12w-e12 | 18.74 | 11.99 | 17.0% | 14.34 | 8.39 | 4.08 |
| 60 | kal-e12 | 18.55 | 11.86 | 17.5% | 14.54 | 8.75 | 3.94 |
| 120 | lsq12-e12 | 8.97 | 4.67 | 19.7% | 7.68 | 7.00 | 4.16 |
| 120 | lsq12w-e12 | 8.38 | 4.53 | 14.7% | 7.26 | 11.08 | 4.76 |

雑音の無い panel（`ideal`）で窓を広げた場合:

| 率 | 方式 | judder | 円 rms px | overshoot px |
| --- | --- | --- | --- | --- |
| 60 | lsq12-e12 | 0.9% | 8.22 | 1.85 |
| 60 | lsq12w-e12 | 0.8% | 8.45 | 4.75 |
| 60 | lsq12a-e12 | 0.9% | 8.22 | 4.52 |
| 120 | lsq12-e12 | 0.2% | 1.61 | 3.24 |
| 120 | lsq12w-e12 | 0.1% | 2.68 | 9.35 |

分かったこと:

1. **judder は「描く時刻が frame の時刻と一様に進まない」ときに出る**。hold・android は 30〜45 Hz で 50〜100%（報告の無い frame で止まり、次で跳ぶ）。
   予測に上限を付けて「最後の報告の時刻 + 上限」で止める方式（初回の試験の `lsq1-p-c20` 等）も同じ理由で 30 Hz で 50〜90%。
2. したがって**評価の時刻は `now − behind`（behind は stroke の中でほぼ一定）**でなければならない。そのとき外挿の最大は
   `T + d − behind`（最後の報告の齢の最大）。外挿を N ms 以下に保つには `behind = T + d − N` が要り、これが遅れの下限になる。
   予測（外挿）の量と遅れの取引は率で決まる: 30 Hz で外挿を 12 ms に抑えると約 24 ms 遅れる（hold の平均の遅れ 19 ms より 4 ms 大きいが、
   judder は 100% → 5%）。60 Hz で約 7 ms、90 Hz で約 1.4 ms、120 Hz で 0。
3. 予測を長くすると停止で行き過ぎる。直線（lsq1-p）は 30 Hz で 32 px、60 Hz で 10 px。放物線は減速を見るが雑音に弱い（60 Hz の judder 14.8%）。
   **lsq12**（予測は直線と放物線の近い方、後ろへは行かない）は、放物線の減速の見方と直線の雑音への強さを両方取る。lsq2-e12 と比べ、
   judder（60 Hz で 9.4% 対 10.8%）と後退（1.58 対 2.44 px）が小さく、円の半径の誤差は少し大きい（1.59 対 1.35 px）。
4. 補間の区間（最後の報告より前）は放物線がよい（円の半径の誤差が直線の半分以下）。
5. Kalman（等速、dt を正しく扱う）は judder が少し小さい（60 Hz で 7.3%）が、停止の overshoot が 2 倍（5.5 px 対 2.8 px）。
   固定 gain の α-β は遅れが小さいが、停止の overshoot（60 Hz で 18 px、安い panel の 30 Hz で 54 px）と円の誤差が大きい。1€ は予測に使うと
   円の誤差が大きい（円 rms 27 px）。いずれも既定にしない（Kalman は速度の推定に使う、§3.5）。
6. 雑音の多い panel では窓を広げると judder が下がる（60 Hz で 21% → 17%）が、雑音の少ない panel で広げると停止と曲線が悪くなる
   （ideal の 120 Hz で overshoot 3.2 → 9.4 px）。stroke の中の残差で雑音を測って広げる方式（`lsq12a`）は、停止や曲がり角の当てはめの誤差を
   雑音と取り違えて広げる（ideal でも overshoot 4.5 px）。**雑音は device ごとに長い時間で測る**（§3.6）。
7. 静止中に黙る panel で、空きを間隔の測定から除かないと、動き始めに大きく遅れる（レビューの試算で 60 Hz の遅れが平均 51 ms・最大 133 ms）。
   device の前の周期を持ち、空きを除くと普段と同じになる（下の表。仮の報告を置くと最大がさらに少し下がる）。

| 条件 | 率 | 方式 | 動き始め 200 ms の遅れ 平均 ms | 最大 ms |
| --- | --- | --- | --- | --- |
| usb | 30 | lsq12-e12 | 22.0 | 36.0 |
| usb | 30 | lsq12g-e12 | 22.5 | 35.3 |
| usb | 60 | lsq12-e12 | 7.3 | 18.0 |
| usb | 60 | lsq12g-e12 | 7.2 | 16.8 |
| cheap-scan | 30 | lsq12g-e12 | 26.2 | 76.8 |
| cheap-scan | 60 | lsq12g-e12 | 11.2 | 25.7 |
| cheap-scan | 120 | lsq12g-e12 | 4.3 | 19.1 |

（この表は device の周期を既知として与えた。最大は、動き始めて最初の報告が届くまでの frame の遅れを含む。）

### 3.3 決定: resampling と予測

1 つの接触（指）の直近の報告 `(τ_k, x_k, y_k)`（最大 32 個）から、描く時刻 `now` の点を次で求める。

1. `T̂`（測った間隔）、`d̂`（測った遅延）、`E`（許す外挿、使う側が選ぶ。既定 12 ms）。§3.4。
2. `behind = max(0, T̂ + d̂ − E)`。stroke の始めに device の値（§3.4）から決め、stroke の中では 1 frame に 0.5 ms より大きく変えない（`d̂` は ms の刻みで
   跳ぶので、そのまま使うと描く時刻が跳ぶ）。
3. `t* = now − behind`。`t* ≤ τ_n + E + T̂/2` で打ち切り（遅れて届いた報告のときだけ効く）、`t* ≥ τ_0`（stroke の最初の報告より前は描かない）。
4. 報告の空き（間隔が `2.5·T̂` より長い）の直後の報告の前に、空きの前の報告の位置で時刻 `τ_k − T̂` の仮の報告を置く（黙っていた指はそこに居た）。
5. 直線の窓 `W1 = max(40 ms, 2.5 T̂)·s`（最低 2 報告）、放物線の窓 `W2 = max(60 ms, 3.5 T̂)·s`（最低 3 報告）、`s` は窓の倍率（§3.6）。
   時刻の原点を `τ_n` に置いて最小二乗で直線 `L(t)` と放物線 `Q(t)` をあてる（正規方程式、3×3 まで。報告が同じ時刻に重なる退化は次数を下げる）。
6. `t* ≤ τ_n` なら `Q(t*)`。`t* > τ_n` なら、`L` の速度 `L'` の向きに測った進む量 `a_Q = (Q(t*) − Q(τ_n))·L'`、`a_L = |L'|²·(t* − τ_n)` について、
   `a_Q ≤ 0` なら `Q(τ_n)`（後ろへ予測しない）、`a_Q < a_L` なら `Q(t*)`、そうでなければ `Q(τ_n) + L'·(t* − τ_n)`。
7. 報告が 1 つなら最後の点。
8. `E` の既定: 内容が指に付いて動くもの（scroll、window の drag、Home・Wiseview の引き出し）は 12 ms。Notes の線の先端の仮の尾は 16 ms（次の frame で描き直すので
   行き過ぎが残らない）。

実際の behind（`d̂` ≈ 2.3 ms、1 ms の poll と 1 ms の配達のとき）: 30 Hz 23.6 ms、45 Hz 12.5 ms、60 Hz 7.0 ms、90 Hz 1.4 ms、120 Hz 0。
「率ごとに係数を選ぶ」は、この式（behind と窓が `T̂` の関数）で連続に行う。率の表は持たない。

### 3.4 率・遅延・空き

- `T̂` = 直近 8 個の間隔の**平均**（空きを除く）。空きは device の周期（無ければ中央値）の 2.5 倍より長い間隔。中央値は欠落と束に強いが、panel の率より遅い
  poll（90 Hz を 8 ms で読むと 8・16 ms が並ぶ）では短い方の alias を返す（`cheap-usb` の 90 Hz で平均 31% の誤差）。平均はそれが無く（同 8.8%）、
  欠落の分だけ長くなる。
- `d̂` = 直近 8 個の「受け取った時刻（使う側の時計）− 報告の時刻」の中央値。compositor では evdev を読んだ時刻、app では wl_touch を受け取った時刻。
- `T̂`・`d̂`・雑音（§3.6）は **device（app では seat）ごとに stroke をまたいで持つ**（stroke の終わりに指数の移動平均で畳む、重み 0.25）。stroke の中で
  空きを除いた間隔が 3 つ集まるまでは device の値を使う。device の値も無ければ `T̂` = 16.7 ms、`d̂` = 2 ms。
- 報告の間が 1 秒以上空いたら、その接触の報告の履歴は捨てる（device の値は残す）。

### 3.5 決定: 離した時の速度（fling の速度）

| 条件 | 率 | 推定 | 3000 px/s の flick | 散らばり | 800 px/s | 減速して離す | 停止後 p95 | p99 | 最大 px/s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| usb | 60 | 2 点 | −5.4% | 5.1% | −5.3% | +14.1% | 172 | 239 | 290 |
| usb | 60 | 直線 100 ms | −43.6% | 6.3% | −43.7% | +2.7% | 24 | 34 | 46 |
| usb | 60 | 放物線 max(100 ms, 3.5 T) | +7.6% | 4.6% | +7.6% | +37.9% | 87 | 118 | 188 |
| usb | 60 | impulse（Android の既定の考え方） | −4.4% | 5.7% | −2.7% | +14.9% | 297 | 400 | 502 |
| usb | 60 | Kalman | −4.1% | 5.0% | −4.0% | +13.9% | 182 | 248 | 310 |
| usb | 60 | 放物線 + 静止の門 | +7.6% | 4.6% | +7.6% | +37.9% | 0 | 0 | 0 |
| usb | 30 | **Kalman + 雑音の門** | −12.8% | 9.1% | −12.2% | +22.2% | 0 | 0 | 0 |
| usb | 60 | **Kalman + 雑音の門** | −4.1% | 5.0% | −4.0% | +13.9% | 0 | 0 | 0 |
| usb | 120 | **Kalman + 雑音の門** | −1.9% | 5.0% | −1.0% | +9.0% | 0 | 0 | 0 |
| cheap-scan | 60 | Kalman + 固定の門（150 px/s） | −6.7% | 7.1% | −6.4% | +17.4% | 223 | 387 | 586 |
| cheap-scan | 30 | **Kalman + 雑音の門** | −16.9% | 14.5% | −18.4% | +20.6% | 0 | 118 | 459 |
| cheap-scan | 60 | **Kalman + 雑音の門** | −6.7% | 7.1% | −6.4% | +17.4% | 0 | 0 | 458 |
| cheap-scan | 120 | **Kalman + 雑音の門** | −3.0% | 5.8% | −3.2% | +12.3% | 0 | 0 | 0 |

（「減速して離す」は 2400 px/s に対する誤差。放物線は、離す前に減速する flick で +38〜42% と大きく外れ、指の動かし方で fling の距離が大きく変わる。
直線 100 ms は加速している flick を平均して大きく過小に、2 点・impulse・Kalman は門が無いと雑音で停止の後に大きな偽の速度を出す。）

決定:

1. 速度 = 等速の Kalman filter（白色の加速度 `q` = 10^7 px²/s³、観測の雑音 `R = max(2 px², σ̂²)`）の、最後の報告の時刻での速度。報告ごとに更新するので
   library では O(1)。
2. **静止の門**: 窓 `max(50 ms, 2.5 T̂)`（最低 3 報告）の直線の傾きの大きさが `v_rest = max(150 px/s, 4·√2·σ̂/W)`（その直線の傾きの標準誤差の 4 倍）未満なら 0。
3. **古い報告の規則**: 離した時刻（up の時刻）が最後の報告から `max(50 ms, 2 T̂)` 以上後なら 0（Scan Time の無い panel は止まった指で黙る。Scan Time の
   ある panel は §2.2 の 3 で報告が続くので門が効く）。
4. fling は `|v| ≥ v_fling_min`（300 px/s）のときだけ。速さは `v_max`（8000 px/s）で打ち切る。安い panel の偽の速度は p99 で 118 px/s、1024 本の最大で 459 px/s
   （30・60 Hz、稀に 300 px/s を越える）。
5. 偏りは指の動かし方と panel で −18〜+22%（30 Hz）、−7〜+17%（60 Hz）、−3〜+12%（120 Hz）。fling の距離はほぼ速度に比例するので、同じ幅で変わる。
   補正の係数は置かない（p007 の実機で見直す）。

### 3.6 雑音と窓の倍率

- 窓の倍率 `s` は既定 1.0。device の雑音 `σ̂` が 1.5 px を超えるとき 1.6（安い panel の 60 Hz の judder 21% → 17%）。
- `σ̂` は stroke ごとに、放物線（150 ms、最低 6 報告）の残差の rms（自由度で補正）を求め、device は直近 16 本の stroke の値を持って**小さい方から 4 番目**
  （約 20 percentile）を使う。5 本未満の間は 1 px。stroke の中の値をそのまま使うと、停止・曲がり角の当てはめの誤差を雑音と取り違える（§3.2 の 6）。
  この推定の精度と、静止の間の残差だけを使う案は、p003 で合成の雑音（0・1・2.5 px）に対して測る（この試験では評価していない）。
- 雑音の閾値と倍率は p007 の実機の panel で見直す。

### 3.7 device の時刻（Scan Time）と host の時刻の合わせ方

報告 k の host の時刻 `h_k`（evdev、ms を µs に）と device の時刻 `m_k`（`MSC_TIMESTAMP`、µs、wrap を展開したもの）について、
`h_k = m_k + o + δ_k`（`o` は定数の差、`δ_k ≥ 0` は転送・poll・割り込みの遅れ）。`o` の推定は差 `h_k − m_k` の**下側の包絡**:
`ô_k = min(h_k − m_k, ô_{k−1} + ρ·(m_k − m_{k−1}))`（`ρ` は時計の drift の許し、200 ppm）。報告の時刻は `τ_k = m_k + ô_k`（host の時計の上で、転送の遅れを含まない。
`τ_k ≤ h_k` が常に成り立つ）。

- 始め直し: `MSC_TIMESTAMP` が前より小さく戻った（kernel が 1 秒の空きで 0 から始め直した）か、`h_k − τ_k` が 20 ms を超えた（device の時計が遅れて
  包絡から離れた）ら、`ô` を今の報告から始め直す。
- **妥当性の検査**: device ごとに、0.5 秒以上の区間で `Σ Δm / Σ Δh` が [0.9, 1.1] の外にあるか、`Δm ≤ 0` の報告（wrap の展開の後）が続いたら、
  その device の Scan Time は信じず `τ_k = h_k` に落とす（Scan Time が進まない panel、単位を取り違えた panel）。
- `MSC_TIMESTAMP` の無い device は `τ_k = h_k`。compositor はこの `τ_k` を ms にして wl_touch の時刻にする（§4.3）。
- 試算（レビュー）: この包絡で作った τ を使っても flick の速度の差は 1 point 未満、時刻の逆行は 0 件だった。

### 3.8 Notes の線（ペン以外の指で描く線）

線は resampling しない: 生の報告を全部使い、報告の間を **centripetal Catmull-Rom**（α = 0.5）でつなぐ。80 px の円を 2 回転/s で描く試験で、
直線でつなぐと 30 Hz で最大 2.1 px 離れる（rms 1.32 px）のが、spline で最大 0.29 px（rms 0.08 px）。雑音 1 px があると両方 rms 約 0.9 px で差は
雑音に埋もれる。最後の報告から先は、§3.3 の `E` = 16 ms の予測点までの仮の尾を描き、次の報告で描き直す。

### 3.9 既定の係数（p003 の library の既定。p007 で実機に合わせる）

| 名 | 既定 | 意味 |
| --- | --- | --- |
| E（内容・drag） | 12 ms | 許す外挿 |
| E（Notes の尾） | 16 ms | 同 |
| behind の変化 | 0.5 ms/frame | stroke の中の上限 |
| 間隔・遅延の履歴 | 8 | `T̂`・`d̂` の報告の数 |
| 空き | 2.5 T̂ | それより長い間隔は空き |
| device の値の畳み込み | 0.25 | stroke の終わりの指数の移動平均の重み |
| 直線・放物線の窓 | max(40 ms, 2.5 T̂)・max(60 ms, 3.5 T̂) | resampling |
| Kalman | q = 10^7 px²/s³、R = max(2, σ̂²) px² | fling の速度 |
| 門の窓・v_rest | max(50 ms, 2.5 T̂)・max(150 px/s, 4√2 σ̂ / W) | 静止の門 |
| v_fling_min・v_max | 300・8000 px/s | fling の範囲 |
| 古い報告 | max(50 ms, 2 T̂) | 離す前に止まっていた |
| 雑音の閾値・窓の倍率 | 1.5 px・1.6 | 雑音の多い panel |
| 雑音の履歴 | 16 本の 4 番目、初め 1 px | §3.6 |
| 時計の drift・始め直し・妥当性 | 200 ppm・20 ms・[0.9, 1.1] | §3.7 |
| 履歴の捨て | 1 秒 | 報告の空き |

px は論理の px（出力の scale 1 の px）。長さは本来 mm で決めるべきだが、今の desktop は scale を 1 に固定しているので px で置き、
DPI の違う出力に広げるときに mm に直す（Future Work の候補）。

## 4. app への渡し方（compositor、p004）

### 4.1 (a)(b)(c) の比較と決定

| 案 | 内容 | 利点 | 欠点 |
| --- | --- | --- | --- |
| (a) | compositor は touch screen の接触を `wl_touch` で渡し、app が gesture（tap・scroll・長押し）と慣性を決める | Wayland の標準の形（GTK・Qt・Firefox・Chromium が同じ）。app だけが scroll の対象・端・入れ子の scroller を知る。Keiland の app は libkeiland で共通の手触りにできる | wl_touch を bind した app は pointer の代わりを受けなくなるので、tap→click・長押し・文字の選択・menu・drag を touch で作り直す必要がある（§5.6）。wl_touch を扱わない app（X11・古い app）は慣性を持たない |
| (b) | compositor が scroll の gesture を判定して `wl_pointer.axis`（`axis_source = finger`、終わりに `axis_stop`）に変え、app が `axis_stop` で慣性を付ける | touchpad の二本指の scroll の Wayland の標準の形。wl_touch を持たない app にも scroll が届く | touch screen で使うと、どの指が scroll かを compositor が決めることになり、app の tap・選択・drag と衝突する |
| (c) | compositor が慣性まで作り、指を離した後も axis を送り続ける | app が何もしなくてよい | 標準の外（離した後の axis に `axis_source` の意味が無い）。compositor は scroll の端も入れ子も知らず、端の rubber band ができない。app の慣性と二重になる |

**決定: touch screen は (a)、touchpad の二本指は (b)、(c) は採らない。** 慣性は app の側で、Keiland の app は libkeiland の共通の実装（§5）で行う。
(b) は touchpad（Touch Pad の HID）が入るときの形として定め、libkeiland の同じ scroller が `axis`（`finger`）の列から速度を測り `axis_stop` で fling を始める。
touchpad の対応そのもの（kernel の Touch Pad、compositor の二本指の判定）はこの WS の Phase に無い（§8、§10）。

### 4.2 生の点を渡すか、resampling した点を渡すか

**生の点**（device の報告ごとの down/motion/up、時刻は §3.7 の `τ_k` の ms）を渡す。理由:

- resampling は描く者の frame の時刻で行うのが最も遅れが小さい。compositor が自分の frame の時刻で resampling した点を渡すと、app はさらに
  自分の描く時刻までの差を抱え、二重の平滑化で遅れが増える。
- 線を描く app（Notes）は生の点の全部を要る（§3.8）。resampling した点では線の形が平滑化で崩れる。
- wl_touch の event は device の event という意味を保つ（Wayland の他の compositor と同じ）。

Keiland の app は libkeiland の `motion`（§6）に wl_touch の点と受け取った時刻を入れ、自分の frame を描く時に `now` の点を求める。
wl_touch を扱う他の app（GTK 等）は自分の方法で扱う（他の compositor の上と同じ）。

代案（compositor が出力の frame ごとに resampling した motion を送り、frame callback と歩調を合わせる）は、Android が app の process で行う resampling を
compositor で代わりにする形で、wl_touch を扱う他の app にも効く。しかし上の 3 点に反するので採らない（将来、他の app の手触りが問題になれば、
wl_touch を bind した client の選択として再考する）。

### 4.3 時刻の精度: `zwp_input_timestamps_v1` は今は入れない

wl_touch の時刻は ms（`uint32_t`）。§2.1 のとおり ms の時刻での劣化は小さい（60 Hz の judder で、完了の時刻は µs に対して 0.7 point、Scan Time の時刻は 0.3〜0.8 point）。
ns の時刻を渡す標準の拡張（input-timestamps-unstable-v1）は、kernel の時刻が µs になってから（§2.2 の 2 の Future Work）考える。
compositor の出す時刻は CLOCK_MONOTONIC の ms の低い 32 bit なので、app は `clock_gettime(CLOCK_MONOTONIC)` と比べて `d̂` を測れる
（Wayland の規定は基準を定めないが、この compositor の約束として keiland.h に書く）。

### 4.4 compositor が自分で描くものへの適用

- 浮いた窓のタイトルバーの一本指の drag（WS079 p013）、Home・Wiseview の引き出し、端のジェスチャーの追従は、compositor が frame を描き始める時刻を `now` として
  §3.3 の点を使う。指で追う間は、報告の無い frame も描き直す（dirty にする）。指が止まり、描く点が 0.5 px 以上動かなくなったら描き直しをやめる。
- compositor は vsync の時刻を持たない（fence の完了で歩調を取る）ので、`now` は描き始めの時刻。表示の時刻が分かる出力（page flip の vblank）が
  入ったら、`now` を表示の予定の時刻に替える（遅れがさらに 1 frame 近く減る）。
- ジェスチャーの判定（tap・二本指の flick、WS079 p013 の閾値）は生の点で行い、resampling は描く位置だけに使う。
- evdev の読み取りは `EV_MSC`/`MSC_TIMESTAMP` を frame から取り出して §3.7 の `τ` を作る。指が止まって `MSC_TIMESTAMP` だけの frame が来ても、
  client に wl_touch の event は送らない（送るものが無い）。1 frame 64 event の上限は、16 本の指で越える既存の問題（WS081 では扱わない）。

### 4.5 wl_touch を持たない app

今の fallback（指 → 左 button の pointer）を変えない。X11 の app（xserver 経由）も同じ。scroll の慣性は付かない。

## 5. 慣性の物理と touch の gesture（libkeiland、p005）

### 5.1 fling

減速は指数の減衰と一定の摩擦の和: `dv/dt = −v/τ − μ·sign(v)`。閉じた式で、

- `v(t) = (v0 + μτ)·e^(−t/τ) − μτ`、止まる時刻 `t_s = τ·ln(1 + v0/(μτ))`、位置 `x(t) = (v0 + μτ)·τ·(1 − e^(−t/τ)) − μτ·t`、全距離 `τ·v0 − μτ·t_s`。
- 一定の摩擦の項があるので有限の時刻でちょうど止まる（閾値で打ち切って跳ぶことが無い）。frame の時刻の関数なので frame の落ちに強い。
- 既定 `τ = 0.45 s`、`μ = 300 px/s²`: 300 px/s → 0.53 s・64 px、1000 px/s → 0.96 s・321 px、3000 px/s → 1.42 s・1159 px、8000 px/s → 1.84 s・3351 px。
- 二次元の fling は速度の向きに沿って一次元で解き、向きに写す（斜めの減速が軸ごとに違わない）。
- **始まりの位置**: fling は up の時点で**描いていた位置**（§3.3 の resampling した点）から、§3.5 の速度で始める。指の最後の報告の位置へ跳ばない
  （behind の分、30 Hz で約 24 ms × 速さの差がある）。

### 5.2 始まりと取り消し

- touch の slop: 押した点から 8 px 動くまでは tap・長押しの候補。越えたら scroll（またはその app の drag）になり、押した点との差を捨てずに追いつく。
- 軸の固定: slop を越えた時の向きが軸から 22.5° 以内ならその軸に固定（二方向に動ける scroller だけ）。
- 離した時の速度は §3.5。`v_fling_min` 未満なら fling しない（端の外なら戻るだけ）。
- **fling の中の touch**（catch）: fling を即座に止め、その時の速度 `v_c` と時刻を覚える。その押しは tap にしない（link や button を押さない）。そのまま動けば drag を続ける。
- **連続の fling の加速**（§10 の判断）: 既定の案は、catch から 400 ms 以内に離した fling が catch した fling と 30° 以内の同じ向きなら、速さに `|v_c|` を足す（`v_max` まで）。
- `wl_touch.cancel`（compositor が指を取った）: gesture を捨て、fling を始めない。端の外なら §5.3 のばねで戻る。

### 5.3 端

scroll の位置は「生の越え」`d`（指の動きどおりの端からの距離）で持ち、見せるのは `o = f(d) = L·c·d/(c·d + L)`（`L` は viewport の長さ、`c = 0.55`）。
`f` は 0 で傾き `c`、`L` に近づく（越えるほど抵抗が増える。L = 800 px で d = 100 px → 51 px、400 px → 172 px）。

- **drag で端を越える**: `d` は指のとおりに動き、見せるのは `f(d)`。
- **離した時に端の外**: `d` に臨界減衰のばね `d(t) = (d0 + (v0 + ω·d0)·t)·e^(−ω t)`、`ω = 16 s⁻¹`（1% に収まるまで 0.41 s）。`v0` は `d` の速度（指の速度）。
  ばねが端（d = 0）を内へ越えたら、その時の速度で §5.1 の fling に渡す（端を越えて中身の側へ戻ることはない）。
- **fling が端に着いた**: その速さ `v_e` を持ったまま同じばね（`d0 = 0`）に入る。生の越えの最大は `v_e/(ω e)`（1000 px/s で 23 px、8000 px/s で 184 px）で、
  見せるのは `f` を通すので L = 800 px で 12 px・90 px に収まる（打ち切りの不連続が無い）。
- **ばねの中の catch**: 見えている越え `o` から `d = f⁻¹(o) = o·L / (c·(L − o))` に戻して drag を続ける。
- 端の無い方向（地図のような無限の面）は端の処理を持たない。

### 5.4 二本指

- touch screen: 二本指が同じ向きに動けば、指の重心で scroll（一本指と同じ scroller）。指の数が変わったら重心を置き直す（跳ばない）。二本指の距離が変われば
  拡大（PDF Viewer 等、app が選ぶ）。浮いたタイトルバーの上の二本指の上への flick（WS079 p013）は compositor が先に取る（client には cancel）。
- touchpad: §4.1 の (b)。app の側は `axis` の列を 1 つの接触と見て同じ scroller に入れる。

### 5.5 scroller の状態

`idle` → 押す → `pressed`（slop の内）→ `dragging` → 離す → `flinging`（§5.1）/ `springing`（§5.3）/ `idle`。
`pressed`・`dragging` で `wl_touch.cancel` → `springing`（端の外）か `idle`。`flinging`・`springing` で押す → catch（§5.2）→ `pressed`。
`flinging` と `springing` は frame の時刻の関数で位置を返し、止まったら `idle`。app は描くたびに位置を聞き、動いている間は frame callback を続ける。

### 5.6 touch の gesture（wl_touch を bind した app の責任）

(a) では、wl_touch を bind した app は pointer の代わりを受けないので、libkeiland は scroller と一緒に gesture の補助を持つ:
tap（slop の内で離す）、double tap（前の tap から 300 ms・16 px 以内）、長押し（slop の内で 500 ms。context menu・文字の選択の始まり）、drag・scroll（slop を越える）、
二本指（§5.4）、cancel。app ごとの割り当て（Files: tap で選ぶ・開く、長押しで menu、drag で scroll。Terminal: drag で scroll、長押しから選択。PDF Viewer: drag で
scroll、二本指で拡大。Notes: 指で線）は app ごとの Phase で決める（§8）。

**Notes の割り当ての改め（2026-09-29、main の指示「ペンは線、指は scroll・pinch」、ws081-p013）**
- Notes の指は線を描かない。担当は次のとおり。
  - 拡大した頁の scroll（慣性・rubber band）
  - 二本指の拡大
  - double tap（2 倍と頁全体の切り替え）
  - toolbar の tap
- 線はペン（と pointer）だけが描く。§3.8 の「指の線」（Catmull-Rom と予測の尾）は実施しない。ペンの線への適用は、今までどおり範囲外（§12）。
- 掌の扱い
  - touch screen が信頼しない接触（HID の Confidence 0）は kernel が離す（WS079 p012）。
  - Notes は、ペンが窓の近くにある間と離れて 500 ms の間は、新しい指を掌として無視する。
  - ペンが来た時に頁の上にある指は cancel し、離れるまで無視する。glide している頁はその場で止める。

**Notes の「指で書く」（2026-09-29 ユーザーの決定、ws081-p015）**
- 既定は上のとおり（指は書かない）。toolbar の「Finger」を入れた間だけ、頁の上の一本指が pointer と同じく選んだ道具で書く（消しゴムなら消す）。
- 書く指の報告は間引かずに線の点にする。報告の間は libpdf の outline の centripetal Catmull-Rom がつなぐ（§3.8 の spline）。§3.8 の予測の尾は入れていない。
- 二本指は scroll・pinch。書き始めて 250 ms 未満か 12 px 未満の動きのうちに二本目が触れたら、書きかけの線を取り消し（頁に残さない）、
  二本とも scroll・pinch に渡す。それより後の二本目は無視する。
- 入の間も toolbar の tap は button を押す。一本指の double tap は点を二つ書き、拡大しない。
- 掌の規則は同じ。ペンが近づいた時に書いている指の線は取り消す。glide している頁は書く指の下で止まる。

## 6. library の置き場所と API（p003・p005）

- **置き場所**: `userland/desktop/libkeiland/motion.c`（resampling・速度・率・空き・雑音・device の時刻、p003）と `scroll.c`（fling・端・scroller・gesture、p005）。
  compositor（`userland/desktop/wayland`）と Keiland の app は既に libkeiland に依存している（`Makefile` の依存）。libbrowser は libkeiland に依存しないが、
  browser の shell（`/bin/browser`）は依存している。ただし `browser.h` には scroll の範囲を決める API も、wheel の delta を使い切ったかの返りも無く、
  shell が wheel の px として慣性を送る形では rubber band も入れ子の scroller の端も作れない。p006 は WS074 の public header（`browser.h`）の変更
  （scroll の位置の設定・範囲の取得、または overscroll の返り）に依存する（§8）。
- **公開の時期**: keiland.h は「機能は最初の利用者と一緒に入れる」方針なので、p003 は `motion.c` と library の中の header（`userland/desktop/libkeiland/motion.h`）
  と host 試験だけにし、Makefile・`exports.map`・keiland.h への宣言（`KEILAND_VERSION` の 9）は最初の利用者の p004 で入れる。
- **API の形**（p003 で確定。keiland の他の object と同じく、library が作り壊す不透明な object）:
  - `struct keiland_motion_device`（device・seat ごと: `T̂`・`d̂`・雑音・device の時刻の差・Scan Time の妥当性）と `struct keiland_motion`（接触ごと: 直近 32 報告の環、
    Kalman の状態、behind）。
  - `keiland_motion_begin(motion, device)`、`keiland_motion_add(motion, stamp_us, arrival_us, x, y)`（時刻が戻った報告は EINVAL）、
    `keiland_motion_point(motion, now_us, extrapolation_us, &x, &y)`、`keiland_motion_velocity(motion, lift_us, &vx, &vy)`、`keiland_motion_end(motion)`（device の値に畳む）。
  - `keiland_motion_device_time(device, host_us, msc_timestamp, &stamp_us)`（§3.7）。
  - 時刻は CLOCK_MONOTONIC の µs（`uint64_t`）、座標は `double` の論理 px。浮動小数と `sqrt`・`exp`・`log` を使う（userland）。
  - p005: `struct keiland_scroller`（軸ごとの位置・端・viewport）と gesture の補助。press・drag・release（速度）・cancel・axis・axis_stop・step（`now` の位置、動いているか）。

## 7. 試験

| Phase | 試験 |
| --- | --- |
| p001 | `plan/ws081/tests/run-motion-compare.sh`（host、決定的、出力を `motion-compare.out` と照合） |
| p002 | host: Scan Time の decode（単位の有無・時間でない Unit・wrap・1 秒の reset・hybrid の 2 frame・変化の無い報告の `MSC_TIMESTAMP`）と `MSC_TIMESTAMP` の列、`EV_MSC` の capability、1 報告 1 時刻（usb-hid の publish が 1 つの時刻を全 event に）。WS079 の `host-hid-touch.c` の期待値の直し（§11）。kernel の build（amd64、warning 0）。QEMU は回帰（usb-tablet の pointer）と boot test だけ |
| p003 | host: (1) 等速で雑音 0 の点は外挿の範囲で誤差 1e-6 px 以内。(2) library を `motion-compare` の方式の 1 つとして組み込み（device の値は harness と同じく既知として与える）、§3.2 の条件（`usb`・`cheap-scan`、30〜120 Hz）で lsq12g-e12 の行に対し judder +1.0 point・overshoot +0.5 px・遅れ +0.5 ms・後退 +0.5 px 以内。(3) 速度: `usb` で 3 種の flick の誤差が §3.5 の表 ±2 point、停止の後 p99 が 0（`usb`）、古い報告の規則。(4) 率の alias（90 Hz を 8 ms）、欠落、束（同じ時刻）、時刻の戻り、空き（静止中に黙る）、1 秒の空き、報告 1 つ・0 個、behind の変化の上限。(5) device の時刻: drift ±200 ppm、wrap、reset、進まない Scan Time・単位の誤りで `τ = h` に落ちる。(6) 雑音の推定（0・1・2.5 px の合成で `σ̂` が ±30% 以内、停止・曲がり角で膨らまない）。(7) sanitizer（address・undefined） |
| p004 | guest（Venus、`touchinject`）: 率と jitter を変えた台本で、compositor が書く resampling の点の列（SSH で guest の file を取る。console・serial の log では判定しない）から judder を測り、window の drag の画面を撮る。wl_touch の時刻が `τ` であること（`tablet-probe --touch`）。回帰: WS079 の p010・p013 の試験。touchinject は今 ms の整数の sleep で、90 Hz や不揃いな間隔と Scan Time を作れないので、touchinject の拡張（§11）が要る |
| p005 以降 | host: scroller の閉じた式（止まる時刻・距離・ばね・rubber band と f⁻¹・端での fling への受け渡し・catch・連続の fling・二本指の重心の置き直し・cancel）、gesture（tap・double tap・長押し・slop）。guest: 各 app を touchinject の flick で scroll し、画面の列を撮る |
| p006 | WS074 と調整（browser の shell と browser.h） |
| p007 | 実機: 報告の率・jitter・雑音・Scan Time の有無と単位を evdev から測る道具で、E・窓・閾値・τ・μ を合わせる。実機の証拠と QEMU の証拠を分ける |

## 8. Phase の見直し（main へ）

- **p002 の範囲を「HID の報告の時刻（完了の時刻・1 報告 1 時刻）と Scan Time の `MSC_TIMESTAMP`」に変える**。率の測定は p003 の library（§2.2 の 4）。
  範囲の file: `usb-hid.c`・`hid-touch.c`・`hid-touch.h`・`hid-report.h`（Scan Time の field の code）・`input.c`・`include/kern/input-device.h`（時刻を受ける emit の宣言）・`include/kern/input-capability.h`（`msc_bits`）・
  `include/uapi/input.h`（`MSC_TIMESTAMP`・`MSC_MAX` の追加）と、WS079 の host 試験 `plan/ws079/tests/host-hid-touch.c` の期待値（WS079 の fixture は
  全部 Scan Time を持つので、capability の数、event の列、「変化の無い frame は何も出さない」の期待が変わる）。一覧に無い file は §11 の判断。
- **p005 を分ける**（(a) で app が touch の gesture を作り直す量が大きい。AGENTS.md の「大きすぎる Phase は分ける」）: p005 = libkeiland の scroller と gesture の補助
  （host 試験）、新しい Phase に Files、Terminal、PDF Viewer、Notes（指の線、ws.md の p004 の「Notes の線への適用」をここへ移す）の適用を 1 つずつ。
- **p006 は WS074 の `browser.h` の変更に依存する**（§6）。
- **p004 は touchinject（WS079 の道具）と注入の device の拡張に依存する**（§7、§11）。
- touchpad（Touch Pad の HID と compositor の二本指 → axis）はこの WS の Phase に無い。ws.md の目標 1 の「touch と touchpad で」を満たすには
  Phase が要る（§10）。
- 他は ws.md の表のまま。

## 9. HAL・toolchain・ABI への影響

- HAL: 変えない。hal.h の差分の案は無い（`plan/ws081/proposed/` は作らない）。
- toolchain: 変えない。
- UAPI: `include/uapi/input.h` に `MSC_TIMESTAMP`・`MSC_MAX` を足す（追加だけ）。evdev の時刻の意味を「報告を kernel が知った時刻」に定める（基準と単位は変えない）。
  Scan Time を持つ touch screen は変化の無い報告でも `MSC_TIMESTAMP` と SYN_REPORT を出すようになる（読み手には event の増加）。
  注入の ABI の `reserved` の割り当ては §11 の判断。
- libkeiland: p004 で keiland.h に motion の API を足し `KEILAND_VERSION` を 9 に。

## 10. ユーザーの判断が要る点（既定の案つき）

1. **30 Hz の panel の遅れと滑らかさの取引**: 既定の `E = 12 ms` では、30 Hz の panel は指から約 24 ms 遅れて滑らかに動く（今の hold は平均 19 ms 遅れてガタガタ動く、
   judder 100% → 5%）。遅れを減らすには `E` を上げる（`E = 16 ms` で 20 ms 遅れ、停止の行き過ぎ 5.0 → 6.5 px）。既定の案: 12 ms で始め、p007 の実機で見て決める。
2. **連続の fling の加速**（§5.2）: 同じ向きに続けて fling すると速さを足すか（iOS 的、長い一覧で速く移動できる）、足さないか（Android 的）。既定の案: 足す。
3. **fling の長さの感じ**（§5.1）: 既定 `τ = 0.45 s`・`μ = 300 px/s²`（3000 px/s で 1.4 秒・1160 px）。iOS に近い長めの滑り。既定の案: これで始め p007 で合わせる。
4. **touchpad の二本指の scroll**（§8）: この WS に Phase を足すか（Touch Pad の HID と compositor の判定）、別の WS にするか。既定の案: この WS の
   目標 1 に書かれているので、p005 の後に Phase を足す（Latitude の touchpad が USB・I2C-HID・PS/2 のどれかの確認が先）。
5. **wl_touch を持たない app の scroll**（§4.5）: 今の pointer の代わりのままにするか、compositor が touch の drag を `axis` に変える選択を足すか。既定の案: 今のまま
   （選択との衝突を避ける）。
6. **touch の gesture の割り当て**（§5.6）: 長押し = context menu、長押しからの drag = 文字の選択、の既定でよいか。既定の案: この割り当て（iOS・Android と同じ）。

## 11. main の判断が要る点（範囲）

1. p002 の範囲の変更（§8）と、依頼の一覧に無い file（`hid-touch.h`・`hid-report.h`・`include/kern/input-device.h`・`include/kern/input-capability.h`、WS079 の `plan/ws079/tests/host-hid-touch.c`）
   を p002 で変えてよいか。
2. 注入の device の拡張（§2.2 の 5: setup の `reserved` の bit 0 と frame の Scan Time。`input-inject.c`・`input-inject.h`）と touchinject（WS079 の道具: Scan Time、
   µs の間隔の台本、自己試験の直し）を p002 か p004 に入れるか。
3. library の置き場所（`userland/desktop/libkeiland/motion.c`・`motion.h`、公開は p004）でよいか。
4. p005 の分割と、p006 の WS074 への依存（§8）。
5. µs の CLOCK_MONOTONIC（`src/kern/clock.c`）と、長さの単位の mm 化を Future Work に置くこと。

## 12. 制限・未実施

- 数値は合成の模型の上のもの。panel の雑音・jitter・率、worker の遅れ（`tick`）、割り込みの揺れは仮定で、実機の測定ではない。表示は 60 Hz だけを試した。
- 表示の遅れ（描いてから光るまで）は全方式に同じなので比較から外した。予測はその遅れを補わない（`now` を表示の時刻にすれば補える、§4.4）。
- flick は 3 種、停止は 1 種の形だけ。実際の指の動かし方の幅は p007 で見る。
- §3.6 の device の雑音の推定と §3.3 の behind の変化の上限は、この試験では評価していない（p003）。
- Linux の hid-multitouch・input core の振る舞い（timestamp だけの frame、Scan Time の wrap）は記憶によるもので、原文は確かめていない。
- ペン（tablet）の線への適用は範囲外（同じ library を使える）。
- 実機（10 インチの touch LCD）は未実施。

## 13. 敵対的レビュー（design-reviewer、2026-09-29）の反映

| 指摘（重大度） | 確かめ | 反映 |
| --- | --- | --- |
| 止まった指で hid-touch が黙ると T̂ と窓が壊れる（高） | `hid-touch.c` 233 行（変化の無い frame は書かない）。レビューの試算で動き始めの遅れ 60 Hz 51/133 ms | kernel: Scan Time を持つ device は毎報告 `MSC_TIMESTAMP`+SYN（§2.2 の 3）。library: 空きを除いた T̂、device の周期、仮の報告（§3.3 の 4、§3.4）。試験に静止中に黙る道を足し、遅れが普段と同じになることを確かめた（§3.2 の 7） |
| Scan Time の妥当性を検査していない、`τ > h` の規則は死んでいる（高） | 式から確定（`ô ≤ h − m`） | 比の検査・`Δm ≤ 0`・始め直しを `h − τ > 20 ms` に（§3.7）。単位の規則（§2.2 の 3） |
| WS079 の試験は変えないは成り立たない（高） | WS079 の fixture は全部 Scan Time を持つ | §8・§11 に WS079 の試験の直しを入れた |
| lsq12 が後ろへ予測する（中） | `motion-compare.c` の判定 | `a_Q ≤ 0` なら進まない（§3.3 の 6）。試験に「後退」を足した |
| 8 ms の poll の模型が 1 poll に 2 報告を送る（中） | generate に前の完了との比較が無かった | 1 poll に 1 報告に直し、表を作り直した。α-β の却下の理由を overshoot と円の誤差に改めた |
| 「完了の時刻」は hardware の完了ではない（中） | `pci-xhci.c` の IMOD 1 ms、UHCI・EHCI の retirement thread | 模型に 0〜1 ms の揺れを入れた。§1・§2.2 の 1 に host controller ごとの質を書いた。µs の不要の根拠を改めた |
| 離した瞬間に跳ぶ（中） | behind の分 | fling は描いていた位置から（§5.1） |
| 端の処理の未定義（中） | — | 生の越え `d` と `f`・`f⁻¹`、端を内へ越えたら fling へ、打ち切りの代わりに `f` で抑える（§5.3） |
| (a) で app が touch の gesture を作り直す量（中） | `touch.c` 37〜41 行 | gesture の補助と cancel の遷移（§5.2、§5.5、§5.6）、p005 の分割（§8） |
| browser の案が (a) の論拠と合わない（中） | `browser.h` 250〜267 行 | p006 の WS074 への依存（§6、§8） |
| 注入の `reserved` の再利用は互換ではない（中） | touchinject の自己試験 | setup の bit 0 で device ごとに宣言（§2.2 の 5） |
| p004 の試験の判定と touchinject の制限（中） | touchinject の sleep は ms の整数 | SSH で file を取る、touchinject の拡張への依存（§7、§11） |
| v_fling_min の根拠が弱い（低） | 64 本の p95 | 1024 本、p99 と最大（§3.5）。離す前に減速する flick を足し、速度の推定を放物線から Kalman + 雑音の門に改めた |
| d̂ が正確な到着を使う・Scan Time を ms に丸める・lsq2-e12 が表に無い・90 Hz の矛盾・RATE_WINDOW の comment・stroke の始め・USB の frame の番号・2 つの SYN frame・behind の跳び・p002 の QEMU の検査・p003 の許容・kern_rtc_read_counter の所在・100 Hz の arch（低） | — | それぞれ直した（到着を ms で記録、`scan-ms` の条件、表に追加、§3.2 の 2、comment、`t* ≥ τ_0`、§2.2 の 1、§2.2 の 3、§3.3 の 2、§2.2 の 7、§7、§1、§2.2 の 2） |
