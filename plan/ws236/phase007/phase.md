<!-- awesome-plan project=zedbsd record=ws236-p007 -->

# ws236-p007: 規約の全文の確認

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14 夜。T1-633 待ちの間に機械的に検出できる規則を通した）
Phase disposition: normal

## 範囲

WS236 が足した・変えた C（`src/drivers/platform/rpi4/rpi4-{vchiq,vchiq-wire,mmal,mmal-wire,mmal-probe,h264}.c/.h`、`src/drivers/gpu/bcm2711/vulkan-video.c/.h` と `#if CONFIG_DRIVER_BCM2711_VIDEO` の部分、`rpi4-firmware.c` の追加、`userland/tests/vkvideo-probe` の変更、`plan/ws236/tests/*.c`）を `plan/coding-style.md` の全文と照らす。

## 見つけてある点（2026-10-14、P3 の自己点検）と直し

- 条件の中の関数の呼び出し: `rpi4-vchiq.c`（`sched_ticks()`）、`rpi4-vchiq-wire.c`（`drv_rpi4_vchiq_slot_bytes()`・`drv_rpi4_vchiq_stride()`）、`rpi4-mmal.c`（`drv_rpi4_mmal_get32()` の多数）、`rpi4-h264.c`（`high_profile()`・`is_slice()`・`read_bits()`）、`rpi4-mmal-probe.c`（`sched_ticks()`・`kern_clock_gettime()`）、`vulkan-video.c`（`bcm2711_vulkan_video_available()`・`bcm2711_vulkan_object_find()`・`find_pps()`）。
- 式で作る Boolean: `vulkan-video.c` の `append_set`（`same = … && kern_memcmp(…) == 0`）、`rpi4-vchiq.c` の `send_bulk`（`open = …`・`full = …`）、`rpi4-mmal.c`（`busy = …`・`outstanding = …`）。
- 試験の C（`plan/ws236/tests/*.c`）は詰めて書いてある（宣言の後の空行、各段の注釈、関数の注釈）。

2026-10-14 夜（7db5f2b0f）: 上の「条件の中の関数の呼び出し」と「式で作る Boolean」は src の全部を直した（動作は不変、build warning 0、host の試験 4 つ PASS）。残り: 試験の C（`plan/ws236/tests/*.c`）の書き方、全文の通しの見直し（関数の注釈・段落の注釈・空行・`goto` なし等）。

2026-10-14 夜（2 回目、T1-633 待ちの間）: T1-625〜631 の直しで足したコードを含め、src の WS236 の C（`rpi4-{vchiq,vchiq-wire,mmal,mmal-wire,mmal-probe,mmal-dump,h264}.c`・`vulkan-video.c`）を機械的に走査して直した。走査の道具は scratchpad の自作 script で、残さない。動作は不変。

- 閉じ括弧の後の空行が無い所（計 141 箇所）: 空行を入れ、次の段落の目的の注釈を足した（`break`・`continue` の前は空行だけ）。
- 呼び出しの結果を直に返す所: `request`・`push_first`・probe の `push` を、失敗の return と成功の return に分けた。`link_get` の `__atomic_load_n` は builtin なので残す。
- 式で作る Boolean: probe の `copy_nals` の `set`。
- 説明の要る条件演算子: probe の `frame_map` の帯と縦帯の幅。`if` に直した。log の引数の短い対称の選択（"frame" : "scratch" など）は §6 の許容内として残す。
- 確認: build（`build/ws236-vp`・`ws236-video`・`ws236-probe`）は warning 0・vmunix check PASS。host の試験 3 つは PASS。`git diff --check` は clean。

残り（全文の見直しで人が読む物）:
- `get32`・`put32` の連続（`configure`・`port_info` など）の 1 行ごとの注釈。
- 呼び出しを入れ子にした引数（`maximum(get32(…), get32(…))`）を中間の変数に分ける。
- `vulkan-video.c` の 3 節以上の条件の改行。例: `if (error == 0 && (pool == NULL || …))`。
- `rpi4.c`・`render-runtime.c` などの `#if` の部分。
- userland の変更（vkvideo-probe の `--dump`、libmedia の `VKVIDEO_SKIP_FAILED`）。
- 試験の C（`plan/ws236/tests/*.c`）。
- コードは T1-633 の結果でまた変わり得るので、最後の通しの見直しはその後に行う。
