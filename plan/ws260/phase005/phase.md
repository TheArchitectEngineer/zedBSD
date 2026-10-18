<!-- awesome-plan project=zedbsd record=ws260-p005 -->

# ws260-p005 WM・DDB の計算を xd-dbuf へ

Status: in-progress（2026-10-18 q1253-i01、P1。cleared は Q1）

- 親: [WS260](../ws.md)。Queue: q1253-i01。設計: [design.md](../design.md) §3.2。

## 作った物

- `xd-dbuf.c/h`: display 9 の watermark の level（`i915_xd9_wm_level`: PRM の方法 1・2 の選び方、level 0 は lines 無し、上の level は 1 line 分の block 以上 +1 で下の level より小さくしない、31 line を超えれば使わない、要る割り当ては block +1）と 8 level の計算（`i915_xd9_wm_levels`）。`drv_i915_xd_wm_plane()`・`drv_i915_xd_wm_cursor_blocks()` は display_ver が 11 未満の時にこの display 9 の level を使う（transition と SAGV は display 9 に無い。display 11〜13 の道は変えない）。`drv_i915_xd9_dbuf_plan()`（1 slice を幅の比で分け、cursor の取り分を末尾に、plane は cursor と plane が両方入る最も深い level で残り全部、入らない深い level と使えない level を消し、line time を 1/8 us で）と `struct i915_xd9_pipe_plan`。share の分け方は既存の `drv_i915_xd_dbuf_pipe_range()`、cursor の取り分は既存の `drv_i915_xd_wm_cursor_blocks()`。
- `gen9-mode.c`: `drv_i915_skl_wm_compute()` を xd の plan を呼ぶ殻にし（xd が届いた pipe だけを写す。ENOSPC の時の途中の値も前と同じ）、level の計算・固定小数点の helper・定数を消した。WM・DDB の語（`drv_i915_skl_wm_word`・`_ddb_word`）は書き込みの段（p012）で移す。

## 確かめ（2026-10-18）

- golden（`run-golden.sh build/tmp/ws260-p005-g1`）: 4 つとも same（table の WM 2000 組・cursor・line time を含む）。
- gen9 host: mode 71・modeset 80・outputs 29 PASS。xd host 試験 all passed（display 11〜13 の WM は変わらない）。`kbuild.sh p005` 3 組 warning 0。
