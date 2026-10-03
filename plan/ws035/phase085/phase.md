<!-- awesome-plan project=zedbsd record=ws035p085 -->

# ws035-p085: 作業域が変わったら configure_bounds を送り直す

Phase ID: `ws035-p085`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-27、サブエージェント。実際に作業域が変わる場面は今の zdesktop に無く、送り直しの経路は動かしていない）
Phase disposition: normal
Queue: なし（2026-09-27 main の割り当て「re-send configure_bounds when the output/work area changes」）

## 範囲

ws071-p018 の残り: zdesktop は xdg-shell version 4 の窓に `configure_bounds` を configure の前に送るが、出力や作業域（glass の
look の有無）が変わっても送り直さない。

## 実装（2026-09-27）

- zdesktop `protocol.c`: `window_bounds`（glass なら `zwl_glass_space`、plain なら出力）、`send_bounds` が送った値を
  `server->bounds_width/height` に記録、`zwl_window_bounds_refresh`（新）: 今の作業域が記録と違えば `ZWL BOUNDS changed ...` を
  log し、configure 済みで fullscreen・docked でない version 4 の窓すべてに `zwl_window_send_configure`（bounds と configure）。
  `main.c` の event loop が毎回呼ぶ（比べるだけで軽い）。
- files `window.c`・`window.h`: 望む大きさ（起動の大きさ）を覚え、大きさを任された configure（0x0）では「望む大きさを
  bounds に収めたもの」を取る。bounds が広がれば元の大きさへ戻る（前は縮めるだけ）。

## 検証（amd64、Venus、2026-09-27）

- files-p018 PASS（1120x690、cascade、App Home）、zdesktop-p084 PASS（files を 600x560 で 2 つ）。
- 作業域が変わる場面は今の zdesktop に無い: 出力の大きさは起動の引数で決まり、glass の look を諦めるのは compose を開くとき
  （最初の client より前）。したがって送り直しの経路は実行されていない（未実施）。出力の大きさを変える機能（mode の変更、
  hotplug）や実行中の look の切り替えを入れる Phase で試験する。

## 残り

- 送り直しの経路の guest での確認（上の理由で未実施）。
