<!-- awesome-plan project=zedbsd record=ws245-p034 -->

# ws245-p034: DBUF と watermark（xd-dbuf）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。design は「ユーザーが居る時間に流す」段）
Parent: [WS245](../ws.md)
Queue: q1229-i04（P1）
設計: [design.md](../design.md) §6.2 の p034 の行、§2.10

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-dbuf.c/.h`（Zlib、新） | slice: 表（xd-platform の `dbuf`）から join（display 13 だけ）と pipe の slice、slice の範囲、MBUS の half の始め、pipe の share（同じ slice の pipe の幅の重み、half の始めから数える）。watermark: 面の parameter（512 byte の block、行の block（16.16）、Y の 4 行、line time）、level（method 1・2、block と行、最小の割り当て（1 割増し／Y の fetch）、行の上限 31/255、latency 0・最小の level 0 の飛ばし）、transition（IPC の時、4 + 10 + 1）、SAGV の level 0 と transition、cursor の割り当て（深い level、1 pipe 32・複数 8 の大きい方）、vblank に入る深い level と SAGV の判定、その下の level を切る（enable だけ）、割り当て（cursor を末尾、level の最小が入る深い level、残りを data rate で、入らない level を消して下の level の block と行を写す、transition と SAGV の検査）。書き込み: plane の WM の語（level 0 は use_sagv で SAGV）・transition・display 13 の HW の SAGV の対・BUF_CFG、slice の電源（request → posting read → 10 µs → state、platform の slice の数だけ）、MBUS_CTL の join と tracker service（比 2、join で倍）、DBOX の語。`Values referenced:`（block、行の上限、transition、cursor、待ち、比）と `Procedure referenced:`（skl_watermark.c・intel_display_power.c の関数） |
| `display/watermark.c`（殻、4491 → 2829 行） | `i915_skl_build_pipe_wm`（plane state → `struct i915_xd_wm_surface`、xd の level → Linux の raw/optimal、vblank の切り）、`i915_skl_cursor_allocation`、`i915_skl_crtc_allocate_plane_ddb`、`skl_write_plane_wm`、`i915_update_mbus_pre_enable`、`drv_i915_mbus_dbox_update`（語は xd、書き込みは xd の window）、slice・join・範囲・offset・pipe の share は xd。変換は `i915_wm_xd_pipe`・`i915_wm_level_to/from_xd`・`i915_wm_plane_to/from_xd`。消した Linux の関数 34（WM の計算の全部、slice の表の引き、DDB の書き込み、DBOX の Xe_LPD+ の分岐） |
| `display/power.c` | `i915_gen9_dbuf_slices_update` は domains の lock の下で xd の slice の電源。`i915_gen9_dbuf_slice_set` を消した |
| `tests/display/host-xd-dbuf-test.c`（新） | 21 check（下） |
| `tests/display/host-lcd-modeset-test.c` | D の順の check の MBUS_CTL を rmw から write に（xd の rmw は読みと書き） |

段の phase.md の必須の項目:
- 消す file: 無し（`watermark.c` は QGV・帯域・DRAM・readout・sanitize が残る: p035〜p038、`watermark-internal.h` も）。
- 消した関数（本番の呼び手 0）: 上の 34 と power.c の 1。
- 殻が更新する旧の写し: crtc state の `wm.skl.raw/optimal`（xd の level から）、`plane_ddb[PRIMARY/CURSOR]`、`wm_level_disabled`、dbuf の global state の slices・joined・ddb（旧の関数が xd の答えで更新）。
- 表 R: DBUF_CTL の tracker と slice の電源の書き手は、mode set は xd-dbuf、core init は旧の power.c の `gen12_dbuf_slices_config`（p035 まで）。
- hook と lock: modeset の lock の下で `drv_i915_lcd_xd_io`。slice の電源は domains の lock の下で `pwc->xd->io`。
- R11: watermark.c 1 → 0、power.c 0。

## 旧と同じに保った物と違い

- 確かめ方: `host-dp-test`（72）は段の前（52261f591）と出力が同じ、`host-lcd-modeset-test`（120）は書き込みの列（`build/tmp/wr.py`）が全部一致（WM の 6 level・TRANS・SAGV の対・BUF_CFG 0x0fdb0000・MBUS_CTL・DBOX 0x01038806）。違いは D の rmw → write だけ。
- 違い: slice の電源は platform の slice だけを書く（旧は TGL でも 4 つの DBUF_CTL を書いた。Linux は platform の slice だけ）。TGL で S3・S4 の register を書かなくなる: 5320 の節目で確かめる。
- 違い: X/Y tiled と planar（NV12）の UV の watermark は持たない（受ける buffer は linear XRGB8888 だけ）。Y tiled の式は xd にあるが本番では通らない。
- 残り: WM の latency の mailbox の読み（`takeover.c` の `drv_i915_skl_setup_wm_latency`）は p035/p036 の takeover の段で xd に。LINETIME の値（`crtc_state->linetime`、この道では 0 を書く: 旧と同じ）の計算は持たない。QGV の帯域（`drv_i915_icl_*qgv*`）は p038 の plan まで殻。

## 実機で確かめる事（5330、全部の段の後の 1 回で。ユーザーが居る時間に）

- survey の PLANE_WM・BUF_CFG・LINETIME・MBUS_CTL・DBUF_CTL・DBOX が基準と同じ、underrun 0。
- 節目: 5320 の起動（TGL の 2 slice、8 level、join 無し、S3・S4 を書かない）。外部出力: 未確認（HDMI の 2 画面の relight と A+B の割り当ては p048 の節目で）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p034/xd-host` → 全部 pass（dbuf 21）、`I915_XD_STRICT` の検査は通る。
- dbuf の check: 5330 の panel（latency 3/54/83/102/147/147、IPC、SAGV 35 µs）の 6 level の語 0x80004010〜0x800280a1・TRANS 0x8000001e・SAGV 0x8000c031/0x8000003f・BUF_CFG 0x0fdb0000（cursor 36 block）が driver の run と一致、join と slice（ADL-P の A 単独・A+B、TGL）、B の half、2 pipe の重み、短い blank（level 0 だけ、SAGV 無し）、小さい share（level 0・1、下の写し）、入らない share、slice の電源と遅れ、MBUS の join/split と tracker、DBOX（ADL-P join 0x01038806・split 0x01038804、TGL 0x01038c02）、TGL は S3/S4 と MBUS_CTL を書かない。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p034` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
