<!-- awesome-plan project=zedbsd record=ws081-p001 -->

# ws081-p001: 設計（時刻・補間と予測の数式・app への渡し方・慣性の物理・試験）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。設計の全項目と数式の比較の host 試験、敵対的レビューの反映まで。ユーザーの判断（design §10）は既定の案のまま未確認、範囲の判断（§11）は main 待ち）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（WS081 の作業用サブエージェント、2026-09-28）。Awesome Plan の Queue の item ではない
Resume point: なし（p003 へ。p002 は design §11 の 1・2 の判断の後）
<!-- awesome-plan-current:end -->

## 範囲

[ws.md](../ws.md) の「設計で決めること」の全項目: 時刻（kernel の時刻の精度、evdev の時刻の意味）、補間・予測の数式（速度の推定、resampling、予測の上限、
率ごとの係数）、app への渡し方（(a)(b)(c)、生の点か resampling した点か）、慣性の物理（減速、閾値、rubber band、二本指、fling の中断、libkeiland）、試験。
数式の比較は host の小さな試験（合成の入力 30・45・60・90・120 Hz と不揃いの jitter）で数値の根拠を付け、design-reviewer で 1 回敵対的レビューする。

## 成果物

| file | 内容 |
| --- | --- |
| [../design.md](../design.md) | 設計（§1 今の事実、§2 時刻、§3 数式、§4 app への渡し方、§5 慣性と gesture、§6 library、§7 試験、§8 Phase の見直し、§9 HAL・ABI、§10 ユーザーの判断、§11 main の判断、§12 制限、§13 レビューの反映） |
| [../tests/motion-compare.c](../tests/motion-compare.c) | 比較の host 試験（決定的）。20 の resampling の方式、12 の速度の推定、10 の時刻・panel の条件、静止中に黙る panel、率の推定、Notes の線 |
| [../tests/run-motion-compare.sh](../tests/run-motion-compare.sh)・[../tests/motion-compare.out](../tests/motion-compare.out) | build・実行と、設計が引く出力との照合 |

## 決定の要点（詳細は design.md）

- **時刻**: 1 報告に 1 時刻、URB の完了を kernel が知った時刻（今は event ごとに worker の時刻）。分解能は ms のまま（µs にしても judder で 0.7 point）。
  Scan Time を `EV_MSC`/`MSC_TIMESTAMP`（µs）で出し、Scan Time を持つ device は変化の無い報告でも出す。率は kernel で測らない（library が時刻から測る）。HAL は変えない。
- **resampling**: `lsq12-e12`: frame より `behind = max(0, T̂ + d̂ − E)` 前（E = 12 ms）を、放物線（補間）と、直線・放物線のうち進まない方（予測、後ろへは行かない）で。
  60 Hz の普通の panel で judder 25%（今の hold）→ 9.4%、遅れ 11 → 7 ms、停止の行き過ぎ 2.8 px。30 Hz は judder 100% → 5%、遅れ 19 → 24 ms。
- **率**: 直近 8 間隔の平均（空きを除く。中央値は遅い poll の alias に弱い）。空きの後は device の周期と仮の報告。
- **fling の速度**: 等速の Kalman + 雑音に比例する静止の門（停止後の偽の速度 p99 0〜118 px/s）。放物線は離す前に減速する flick で +38〜42% 外れるので採らない。
- **渡し方**: touch screen は (a) 生の wl_touch（時刻は Scan Time から作った τ の ms）、touchpad は (b) axis finger、(c) は採らない。慣性と gesture は app の側で libkeiland。
- **慣性**: `dv/dt = −v/τ − μ·sign(v)`（τ 0.45 s、μ 300 px/s²）、生の越えを `f(d) = L·c·d/(c·d + L)` で見せ、ばね ω 16 s⁻¹。
- **library**: `userland/desktop/libkeiland/motion.c`（p003）・`scroll.c`（p005）。公開（keiland.h）は最初の利用者の p004。

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-motion-compare.sh`（host の clang、`-std=gnu11 -O2 -Wall -Wextra -Werror`、約 10 秒） | build は warning 0。出力が `motion-compare.out` と一致（決定的） |
| design-reviewer（敵対的レビュー、1 回） | 高 3・中 9・低 多数の指摘。source（`hid-touch.c` 233 行、`pci-xhci.c` の IMOD、`pci-uhci.c` の retirement worker、`input-device.h`）と試算で確かめ、全部を design §13 のとおり反映。試験の模型（8 ms の poll、割り込みの揺れ、到着の ms、後退の指標、静止中に黙る道、減速して離す flick、1024 本の速度）を直して表を作り直した |

## 未実施・制限

- 実機（touch LCD）・QEMU での確認は無い（設計の Phase）。数値は合成の模型の上のもの（design §12）。
- 試験の file は plan の試験で、`plan/tools/style-check.py` の対象にしていない（製品の source ではない）。
- design §3.6 の device の雑音の推定と §3.3 の behind の変化の上限は p003 で評価する。

## 残り

- ユーザーの判断（design §10）: 30 Hz の遅れと滑らかさ（E）、連続の fling の加速、fling の長さ、touchpad の Phase、wl_touch を持たない app の scroll、gesture の割り当て。
- main の判断（design §11）: p002 の範囲の変更と一覧に無い file、注入の device と touchinject の拡張、library の置き場所、p005 の分割と p006 の WS074 への依存、Future Work。
