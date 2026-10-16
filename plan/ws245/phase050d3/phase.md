<!-- awesome-plan project=zedbsd record=ws245-p050d3 -->

# ws245-p050d3: plane・flip・vblank を xd に

Status: in-progress（2026-10-17 q1229-i10 で実装・host 試験・build 済み（c93202cf5）、5330 は全部の段の後に 1 回）
Parent: [WS245](../ws.md)
Queue: q1229-i08・q1229-i10（P1）
設計: [design.md](../design.md) §6.2 の p033・p049・p050 の行、§2.10、§3.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 5

## 範囲

- plane の update（`plane.c` の書き手と plane state）、flip の arm/poll/wait/settle と SURFLIVE の読み、vblank の event と evasion（`vblank.c`、`modeset.c` の flip の本体）を xd-pipe の plane と xd-screen の flip（p049 の判断）に。frame counter の読み（own の `control.c` の `drv_i915_pipe_frame_read`）も xd-pipe に。
- 殻が更新する旧の写し（`plane_armed`、flip の field）を減らす。

## 受け入れ

- xd の host 試験（plane の語の列、flip の状態、vblank の event の待ちと evasion）、build 6 組 warning 0、旧 host 試験の前後の比べ、R11 の数。5330 は全部の段の後に 1 回（flip の性能 `perf`、SURFLIVE/CRC の進み）。

## 結果（2026-10-17 q1229-i10、P1。base ef3fc9723（p050d2 の記録）、段の commit c93202cf5）

形:
- xd-pipe: `drv_i915_xd_pipe_scanline`（PIPEDSL、0 は 1 us おきに最大 100 回読み直す、offset を足して vtotal で剰余）、`drv_i915_xd_pipe_evade_window`（vblank の始まりの 100 us 前から 1 行前まで。`struct i915_xd_evade`）、offset の値 `I915_XD_SCANLINE_OFFSET`（1）・`_HDMI`（2）。
- xd-screen: `drv_i915_xd_screen_update`（1 frame に入る更新: window が frame の外なら割り込み off だけ。そうでなければ vblank の参照 → 割り込み off → scanline が window の中の間は割り込み on で次の vblank まで眠る（2 tick で諦め、error）→ 参照を返す → arm の書き込み → 終わりの line と frame → event の参照と arm → 割り込み on → frame が進んでいたら「blank を跨いだ」error。hook は `struct i915_xd_update_hooks`、結果は `struct i915_xd_update_report`）。
- 殻: `plane.c` を書き直した（Linux の plane state・framebuffer・`skl_universal_plane.c` の写しを消し、`ms->plane` は `struct i915_xd_plane`。prepare・update・disable・flip の update は xd-pipe の noarm/arm と xd-dbuf の `drv_i915_xd_wm_write_plane`（plan の WM と plane の DDB）を直に、flip の update は `drv_i915_xd_screen_update` に backend の hook を渡す。file の上部の Linux の表示も消した）。`ms->plan`（prepare の plan を持つ）・`ms->flip`（`struct i915_xd_flip`。旧の `cur_surf`・`pend_surf`・`old_surf`・`flip_pending`・`flip_stuck`・`flip_gen` と load/store の写しを消した）。status の WM・DDB は `ms->plan` から。SURFLIVE の読みは xd の register（presenting thread も読むので共有の xd window を通さない）、frame counter は `drv_i915_xd_pipe_frame`（worker と own の `control.c`）。`drv_i915_lcd_modeset_evade_window` は xd の window。`vblank.c` の上部の Linux の表示を消した（残るのは割り込みの入れ子、event の取り消し、evade window の口、kernel の vblank・event の hook: p050d4 で xd-display へ）。
- 消した関数（34）: pipe.c `drv_i915_pipe_update_start/_end`・`drv_i915_crtc_vblank_evade_scanlines`・`drv_i915_usecs_to_scanlines`・`i915_crtc_get_vblank_counter`・`i915_crtc_needs_vblank_work`・`i915_mode_vblank_start`、vblank.c `drv_i915_get_crtc_scanline`・`drv_i915_crtc_update_active_timings`・`drv_i915_lcd_ms_active_timings`・`drv_i915_lcd_ms_crtc_funcs`・`drv_i915_lcd_ms_evade_window`・`drv_i915_pipe_frame_read`・`drv_i915_lcd_irq_save/_restore`・`drv_i915_wait_for_pipe_scanline_stopped`・`drv_i915_crtc_state_size_crtc_unit`・`i915_get_crtc_scanline`・`i915_crtc_scanline_offset`・`i915_g4x_get_vblank_counter`・`i915_wait_for_pipe_scanline_moving`、plane.c `i915_icl_plane_update_noarm/_arm`・`i915_icl_plane_disable_arm`・`i915_plane_xd_of/_words`・`i915_xrgb8888_format`、watermark.c `skl_write_plane_wm`・`i915_wm_plane_to/from_xd`・`i915_wm_level_to/from_xd`、modeset.c `i915_modeset_flip_load/_store`（16 file、+885 −1447）。world の `i915_lcd_flip_pipe`・`i915_plane_emit_crtc_state/_plane_state`、ms の `flip_event`・`cursor`・`fb`・Linux の `plane`/`plane_state` を消した。消した file は無い。
- 殻が更新する旧の写し: `ms->plane_armed`（own の present.c・modeset.c の判断が読む: p050d4 で xd-display へ）、`ms->flip_event_ref`・`events_cancelled`、`ms->crtc_state` の残り（commit の step の Linux の型: p050d4）。WM・DDB と flip の写しは無くなった。

確かめた事:
- 旧 host 試験の前後（`sh build/tmp/stagecmp.sh 38eef56d3 d3cmp2`）: dp SAME、lcd の trnorm の差 0 行、out50c の差 0 行。dp 72・lcd 120 check とも 0 failures（I: flip 4 回・event・vblank の参照、J1 の取り消し、J2 の evasion の window 1073..1079 と眠り、J3 の vblank が来ない時の諦めを含む）。
- `host-xd-screen-test` 82 check 0 failures（足した物: window 1073..1079、line 長 0 の window、0 の読み直しと HDMI の offset、最後の line の wrap、window の外は眠らない・割り込み off の中で書く・event が参照を 1 つ持つ、window の中は割り込み on で 1 回眠って line 4 で書く、vblank が来ない時は 2 tick で諦めて error 1・書く・参照の釣り合い、参照を拒まれた時は待たずに書き error 2、書き込みの間に frame が進んだ報告、frame の外の window）。`run-xd-host-tests.sh` 23 本 all passed。
- build（`sh build/tmp/kbuild.sh d3`）: 6 組 rc=0 warning 0。`git diff --check` 0。R11: 触った source の file は全部 0。

実機で確かめる事（5330 は全部の段の後に 1 回）: flip の性能（dmesg の `perf: … per submit`）、survey の SURFLIVE と frame counter・CRC の進み、`Atomic update failure` に当たる新しい文言（「the update crossed a vertical blank」「the update did not get out of the lines before the vertical blank in time」）が出ない事。HDMI の flip（scanline の offset 2）はユーザーの一覧。
