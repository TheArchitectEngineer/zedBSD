<!-- awesome-plan project=zedbsd record=ws035p137 -->

# ws035-p137: 手前の窓を閉じた後の keyboard の focus（BUG-113）

Phase ID: `ws035-p137`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施。compositor の source の変更なし）
Phase disposition: normal
Bug: [BUG-113](../../bugs/BUG-113.md)
Queue: なし（2026-09-29 main の割り当て「BUG-113 を p137 として直す。閉じた窓の次に上にある、同じ desktop の窓へ focus を移す。WS094 の設計
（focus は desktop を click したときだけ移す）に合わせ、desktop の層には focus を渡さない」）

## 範囲と受け入れ

- 手前の窓が閉じたとき（client の終了、close button）、同じ desktop の次に上の窓が keyboard の focus（enter）を受け、key が届く。
- 別の desktop の窓には focus を渡さない（その desktop を表示したときに渡す）。
- WS094 の desktop の層（`keiland_desktop_v1`）には focus を自動で渡さない。

## 調べたこと

- zdesktop の focus: `zwl_schedule`（`display.c`）が window mode の毎回の pass で `front_surface = zwl_top_window()`（表示中の desktop の、
  最小化されていない map 済みの窓のうち map の順が最後のもの）とし、`zwl_seat_focus` が変われば leave・enter を送る。閉じた窓は
  `zwl_seat_surface_gone`（`seat.c`）で leave と focus の解除をされる。したがって閉じた後の pass で次の窓が enter を受ける。
- 試験 `plan/ws035/tests/zdesktop-p137.sh`（新）の 1 回目は FAIL だった。調べると、試験が `ps -A -o pid,args | grep '[p]opup-probe .*--token=b'` で
  窓の process を探していたが、**guest の `ps` の args は引数を出さない**（`36 R /bin/popup-probe`）ため何も kill しておらず、
  b の窓が残って focus を持ち続けていた（compositor の log に `ZWL CLEANUP` が無く、画面に b の窓が残る）。pid を記録して kill するように直した。
- BUG-113 の元の観測（ws095-p004）: その試験（`plan/ws095/tests/ime-p004.sh` の 5 の後）は
  `for p in $(ps -A -o pid,args | grep "[i]me-probe" | ...); do kill $p; done` で password の probe を閉じるつもりで、**ime-probe を全て**
  （残るはずの窓も）kill していた。残る窓が無かったので focus が戻らなかったと見る（ws095 の試験の log は見ていない。code からの推定）。

## 検証

**QEMU（amd64、Venus の guest 1280x800、変更前の image `build/p136-after.img`）**: `zdesktop-p137.sh`（pid で kill する版）→ `p137: PASS`。
1. a の上に b、b の process を kill: a が enter を受け（`POPUPPROBE focus window` 2 回目）、key x（45）が a に届く。
2. a の上に c、c の close button（`xdg_toplevel.close`、c が終わる）: a が enter を受け、key x が a に届く。
3. desktop 2（Ctrl+Alt+Right）で d を開いて kill: desktop 1 の a は focus を受けない。desktop 1 に戻ると（Ctrl+Alt+Left）a が受ける。
4. 最後の窓 a を kill: error 無し。

画面: `build/ws035-shots/p137/killed.png`（b が消え a だけ）、`closed.png`、`desktop2.png`、`empty.png`、`probe-a.log`。

- WS094 の desktop の層: 今の zdesktop に desktop の surface は無い（ws094-p002 は planned）。`zwl_top_window` は toplevel の窓だけを見るので、
  ws094-p002 で desktop の surface を足すときは、`zwl_top_window` の候補から外す（click したときだけ focus を渡す）必要がある。WS094 への引き継ぎ。

build・回帰: compositor の source を変えていないので行わない（試験の script だけ）。

## Resume point

2026-09-30: cleared（修正なし: compositor はすでに期待どおり。BUG-113 は試験の側の原因として resolved）。
