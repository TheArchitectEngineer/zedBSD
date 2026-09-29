<!-- awesome-plan project=zedbsd record=ws075p023 -->

# ws075-p023: 性能: どれが compositor を重くしているかを測る（分岐の中の ALU・draw ごとの停止・すりガラス）

Phase ID: `ws075-p023`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-30。最初の段の計測は済み、実装は未着手。main の wrap up で止めた。すりガラスの扱いはユーザーの判断待ち）
Phase disposition: normal
承認: 2026-09-29 main の判断（p022 の後。p023 の最初の段で (a)・(b) を測り大きい方を実装。ユーザーの見立てで (c) すりガラスを切った場合を追加）。

## 計測の方法

実機の 5330 の VFIO passthrough の QEMU、demo の passthrough の image、kei の session。`plan/ws075/tests/hdmi/measure-apps.sh`
（lock の下で desktop だけの物差し → App Home の 10 app を開く → 同じ物差し → session ごとの engine の時間（`engine-gdb.sh`）→ 撮影）を
各 image で 2 回。表は `plan/ws075/tests/hdmi/summarize.py` の出力。「1 run」は最も engine を使う session（compositor）の 1 回の実行の時間。

試験の build の変更は tree に入れていない（patch と shader は `plan/ws075/phase023/exp/`）:

| 記号 | 変更 | patch |
| --- | --- | --- |
| base | main（80314042）の tree、p021 まで | — |
| (a) | compositor の panel.frag を「全ての draw が image の道」の版に差し替え（kernel が module を見て置換。分岐の中の ALU と sample を除いた上限の見積もり、絵は崩れる） | `exp-a.patch`・`panel-image-only.frag` |
| (b) | draw ごとの PIPE_CONTROL 4 つ（context setup の flush と invalidate、primitive の前の CS stall、後の RT・depth・DC の flush）を出さない（STATE_BASE_ADDRESS は残す） | `exp-b.patch` |
| (c1) | すりガラスのぼかしだけ切る: compositor の backdrop（窓の下の scene の再描画と blur の pass）を出さない（glass は blur 済みの壁紙のまま） | `exp-c1.patch`（userland/desktop/wayland/backdrop.c） |
| (c2) | glass を全部切る: (c1) に加え、panel.frag の glass の分岐を単色（panel の色）に（kernel が module を置換） | `exp-c1.patch` + `exp-c2-shader.patch`・`panel-glass-solid.frag` |

## 結果（2026-09-29〜30）

| image・回 | 10 app: flip の率 | 10 app: latency 中央値（範囲） | compositor: 1 run・占有・run/s | engine 全体 | desktop だけ |
| --- | --- | --- | --- | --- | --- |
| base 1 | 8.3/s | 65.2 ms（15.1〜115.5） | 8.81 ms・49.5%・56.1 | 58.2% | 58.1/s・16.0 ms |
| base 2 | 8.6/s | 47.9 ms（15.3〜182.0） | 7.94 ms・49.5%・62.4 | 57.7% | 58.4/s・31.8 ms |
| (a) 1 | **12.4/s** | 48.5 ms（15.0〜115.2） | **4.86 ms**・41.7%・85.9 | 51.8% | 58.3/s・16.1 ms |
| (a) 2 | 無効（xterm を開いた後に画面の更新が止まった: PLANE_SURFLIVE が 0、実験の shader の版でのみ、原因は未調査） | — | — | — | 58.3/s・15.9 ms |
| (b) 1 | 8.1/s | 65.2 ms（14.9〜115.2） | 8.48 ms・49.8%・58.8 | 58.8% | 58.3/s・15.9 ms |
| (b) 2 | 8.2/s | 48.3 ms（15.2〜82.3） | 8.76 ms・50.3%・57.4 | 58.7% | 58.5/s・15.9 ms |
| (c1) 1 | 10.6/s | 32.7 ms（15.1〜98.6） | 9.19 ms・48.8%・53.1 | 55.7% | 58.3/s・15.9 ms |
| (c1) 2 | 10.8/s | 65.3 ms（15.1〜115.5） | 9.49 ms・49.6%・52.3 | 56.5% | 58.6/s・16.0 ms |
| (c2) 1 | 10.4/s | 49.3 ms（15.1〜114.5） | 8.94 ms・48.6%・54.4 | 56.3% | 58.3/s・15.8 ms |
| (c2) 2 | 比較できない（Gears の窓が出ず、Gears の animation が無い: 33.3/s・32.4 ms・2.66 ms/run） | — | — | — | 57.9/s・15.9 ms |

画面: 各 run の最後（X terminal まで開いた後）を並べた図 `build/ws075-shots/ws075-p023-variants.png`（上段 base 1・2、(a) 1・2、(b) 1、
下段 (b) 2、(c1) 1・2、(c2) 1・2）。(a) は絵が崩れる（全ての panel を画像として描く）。(c2) 2 は Gears が無い。
各 run の撮影は `build/ws075-p023/m-*/shots/`。

## 読み

- (a) 分岐の中の ALU（と使わない sample）を除いた上限: compositor の 1 run が約 8.4 → 4.9 ms（約 -45%）、flip の率 8.5 → 12.4/s。
  一番効く。ただし image の道だけの上限の見積もりで、実装（分岐ごとに飛ぶ）ではこれより小さい。1 回は画面が止まった（実験の版だけ）。
- (b) draw ごとの停止と flush: 変わらない（8.48・8.76 ms）。今の compositor では効かない。
- (c) すりガラス: ぼかし（backdrop）を切っても、glass を全部切っても、compositor の 1 run は変わらない（9.2〜9.5・8.9 ms）。
  flip の率は約 8.5 → 10.6/s に上がる（run の数が減る: 56〜62 → 52〜54 run/s）。GPU の時間の大半は glass の効果ではなく panel.frag
  そのもの（全ての分岐を全 pixel で）と見る（推測）。latency の中央値は run ごとのばらつき（32〜66 ms）が大きく、差を言えない。
- よって (a) → p023 のまま「分岐の中の ALU を飛ぶ」を実装するのが次（main の判断どおり）。(b) は ws.md の候補に残す。
  すりガラスをやめるかは効果が小さい（flip の率 +25% 程度、1 run は同じ）という材料をユーザーへ。

## 未実施・残り

- (c2) の 2 回目は Gears が出ず比較できない（1 回だけ）。(a) の 2 回目は画面が止まり無効（1 回だけ）。
- (a) 2 の画面の停止の原因（実験の shader の版）: 未調査。
- 実装（分岐の中の ALU を飛ぶ）: 未着手。
- 素の 5330: 未実施。
