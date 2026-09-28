<!-- awesome-plan project=zedbsd record=ws081-p005 -->

# ws081-p005: 慣性の scroll と touch の gesture の共通の library（libkeiland）

<!-- awesome-plan-current:start -->
Status: cleared（2026-09-29、WS081 の作業用サブエージェント。libkeiland の scroller・gesture と公開、host 試験・sanitizer・mutation、target の build。app への適用は p010〜p013）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p005（scroller と gesture の library、libkeiland）… host 試験で。libkeiland の公開（exports.map・keiland.h）を含めてよい」）。Awesome Plan の Queue の item ではない
Resume point: なし（次は main の指示どおり p012）
<!-- awesome-plan-current:end -->

## 範囲

[design.md](../design.md) §5・§6 の library: fling（τ・μ）、端の rubber band とばね、catch と連続の fling、cancel、軸の固定、gesture（tap・double tap・長押し・drag・
二本指の重心と拡大・cancel）。libkeiland の公開（Makefile・exports.map・keiland.h、`KEILAND_VERSION` 10）。app（Files・Terminal・PDF Viewer・Notes）と
browser と compositor は変えない。HAL・toolchain・kernel は変えない。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/libkeiland/scroll.c`（新） | `struct keiland_scroller`。軸ごとに生の位置（端を越えた分は生の越え d）・mode（rest・drag・fling・spring）・始まりの時刻・位置・速度を持ち、位置は `now` の閉じた式（frame の率に依らない）。fling は §5.1 の式、斜めは μ を速度の向きの成分に分けて（μ_i = μ·|v_i|/speed）両軸が同時に止まる。fling が端を越えたらその速さでばね（d0 = 0）、ばねが端を内へ越えたらその速度で fling。ばねは \|d\| < 0.25 px かつ \|v\| < 5 px/s、または 2 s で端に止める。見せる位置は端 + f(d)（c = 0.55、L = viewport）。press は動いていれば catch（≥ 50 px/s、速度と時刻を覚える）し、生の位置のまま drag に（ばねの中の catch は見えている位置のまま。§5.3 の f⁻¹ と同じ）。release は v_fling_min（300 px/s）未満なら fling しない、catch から 400 ms 以内・30° 以内なら caught の速さを足す、8000 px/s で頭打ち。軸の固定は 8 px 動いた時に軸から 22.5° 以内（両方向に動ける scroller だけ）。cancel は fling せず、端の外ならばね。範囲が縮んで位置が外に残ればばね |
| `userland/desktop/libkeiland/gesture.c`（新） | `struct keiland_gesture`。指は 5 本まで、指ごとに `keiland_motion`（作って使い回す）。slop 8 px（押した時の生の重心から）、長押し 500 ms（`keiland_gesture_next` が判定）、double tap は前の tap から 300 ms・16 px。drag の差は `base + 重心(now の resampling) − anchor`。指が増減したら、変わる前の重心を測ってから base に畳み anchor を置き直す（跳ばない）。二本指は開始時の距離に対する倍率と重心。最後の指が離れたら DRAG_END にその指の速度（§3.5）。cancel は指の motion を始め直し（device に学ばせない）CANCEL を出す。event は 16 個の環（満ちたら古いものを捨てる） |
| `include/libc/keiland.h` | scroller と gesture の節、`KEILAND_VERSION` 10（「10: the scroller and the gestures」）。catch した press は tap にしない（app の責任）を約束として書いた |
| `userland/desktop/libkeiland/Makefile`・`exports.map` | `scroll.c`・`gesture.c` を source に、`keiland_scroller_*`・`keiland_gesture_*` を export に |
| `plan/ws081/tests/host-scroll.c`・`run-scroll.sh`（新） | host 試験（下） |

### design からの違い

- gesture は `scroll.c` ではなく別の `gesture.c` に置いた（design §6 は scroll.c に両方）。責務が別で、scroller を使わない app（Notes）が gesture だけを使える。
- design §6 の `axis`・`axis_stop`（touchpad の axis の列）は入れなかった。axis の列は press・drag（累計）・release（`keiland_motion` の速度）で表せる。
  touchpad の最初の利用者が要ると分かった時に足す（p006 か app の Phase）。
- 斜めの fling は design §5.1 の「向きに沿った一次元の解を写す」と同じ（軸ごとに μ を分けた式は、向きの一次元の解の成分に等しい）。

## 確認（実行したもの）

| 確認 | 結果 |
| --- | --- |
| `plan/ws081/tests/run-scroll.sh`（host の clang、`-Wall -Wextra -Werror -Wconversion`） | `host-scroll: ok (87 checks)` |
| 同じ、`EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1"`（`build/ws081-p005-san`） | `host-scroll: ok (87 checks)` |
| mutation（scratchpad の script、定数 14 と reanchor の除去 1: τ、μ、v_max、c、ω、軸の角、加速の時間・角、catch の速さ、v_fling_min、slop、長押し、double tap の時間・距離） | 15 個すべて FAIL で検出 |
| `make -j16 ZEDBSD_CONFIG=plan/ws079/tests/config-amd64-pen.mk BUILD=build/amd64 build/amd64/dynamic/libkeiland.so`（`-Werror`） | rc=0、`warning:`・`error:` 0、`nm -D` に `keiland_scroller_*` 9 個・`keiland_gesture_*` 9 個。toolchain の展開・build 0 件 |
| `plan/tools/style-check.py`・`style-extra.py`（WS079 の写し）: scroll.c・gesture.c・keiland.h・host-scroll.c | 指摘 0 |

host 試験の中身: fling の閉じた式（3000 px/s → 1.415 s・1159 px、0.1 s ごとの位置）、299 px/s は fling しない、8000 px/s の頭打ち、斜めの向きが保たれ両軸が同時に止まる、
rubber band（L = 800 で 100 px → 51.5、400 px → 172.5）、ばねが端を越えずに 1 s 以内に止まる、端への fling が少し越えて戻る、ばねの中の press が見えている位置を保ち
drag が band を通って続く、catch（止まる、静止の press は catch しない）、連続の fling（同じ向きは速さを足す、逆向き・450 ms 後は足さない）、cancel、軸の固定と固定した fling、
範囲の縮み、4 ms と 132 ms の frame で同じ位置、不正な範囲の EINVAL。gesture: tap、double tap、3 回目は tap、遠い tap、長押し（400 ms で無し、500 ms で有り、離しても tap 無し）、
長押しの後の drag（`after_long_press`）、cancel、EEXIST・EBUSY・ENOENT、60 Hz・1500 px/s の drag（DRAG_BEGIN 1 回、resampling の差）、二本目の指の出入りで跳ばない、
pinch の倍率、DRAG_END の速度（2000 px/s の flick）。

sysroot の写し: `include/libc/keiland.h` と、main の merge で変わっていた `include/libc/pdf.h` を `build/amd64/sysroot/usr/include/` に写して
`.zedbsd-sysroot-complete` を touch した（p002 と同じ手順）。

## 未実施・制限

- guest（QEMU）と実機の確認は無し（library だけの Phase。app への適用の Phase で行う）。
- 係数（τ・μ・c・ω・slop 等）は design の既定。実機の調整は p007。
- 描いている位置からの fling の始まり（§5.1）は app の責任: `keiland_gesture_drag_offset` の差を scroller の drag に渡し、DRAG_END の速度で release する。

## 残り

- p012（PDF Viewer への適用）は main の指示どおり次に行う。p010・p011・p013 は main の指示の後。
