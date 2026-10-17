<!-- awesome-plan project=zedbsd record=ws245-p049 -->

# ws245-p049: flip と resident run の契約（EAGAIN・UNPLUGGED・RELIGHT・acquired）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i05（P1）
設計: [design.md](../design.md) §6.2 の p049 の行、§4.2 の run の契約

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-screen.c/.h` | flip の model `struct i915_xd_flip`（表示中・pending・置き換えられる buffer、armed、stuck、世代）: `drv_i915_xd_flip_refused`（lit でない・retained・busy・stuck・同じ buffer・4 KiB に揃わない）、`_arm`、`_judge`（event と SURFLIVE で DONE / NOT_LATCHED / TIMEOUT、DONE 以外は stuck）、`_idle`（poll: latch した flip を退く）、`_settled`（EIO で stuck）。run の契約 `drv_i915_xd_screen_run_verdict`（outcome → 0 / EAGAIN（clean な失敗で外部 DP の link を下げた）/ EIO、reason SERVED・UNPLUGGED・RELIGHT・FAILED、acquired） |
| `display/modeset.c`（殻） | `drv_i915_lcd_modeset_flip`・`_flip_poll`・`_flip_settle` と `i915_modeset_flip_arm` の判断を flip の model に（`ms->cur_surf`・`pend_surf`・`old_surf`・`flip_pending`・`flip_stuck`・`flip_gen` は写し: load/store）。hardware（plane の更新、event の待ち、SURFLIVE の読み、vblank の参照）は殻のまま。`drv_i915_lcd_kernel_resident_run` の戻りは run の verdict（`rep->display_acquired` を acquired に） |
| `tests/display/host-xd-screen-test.c` | +21 check（flip の拒み 5、armed と poll、DONE・NOT_LATCHED・TIMEOUT、stuck の poll、settle 2、run の verdict 5） |

段の phase.md の必須の項目:
- 消す file: 無し（`vblank.c` に、この段で呼び手が 0 になる関数は無い: flip の待ちは backend の `wait_event`）。
- 殻が更新する旧の写し: flip の 6 つの field（load/store）。
- 表 R: 書き込みは今と同じ（plane の更新は xd-pipe）。
- R11: `modeset.c` 0。

## 旧と同じに保った物と違い

- 確かめ方: `sh build/tmp/refcmp.sh 5d36d047b p049b` で旧の host 試験（dp 72・lcd 120）の出力が同じ（lcd の試験は flip の DONE・stuck・poll・settle と世代を見る）。
- 設計との差: (1) own の `present.c`・`head.c` の呼び先の付け替え（`drv_i915_xd_screen_run/_flip_*`）は p050 の口の段で（設計 §4.2: p050 で付け替える）。(2) RELIGHT は present.c の window の判断のまま（verdict の relight は 0 で呼ぶ: reason に出ない）。ENODEV（partner gone）は今のとおり unplugged の EIO で、present.c が window の unplugged を見る。(3) head の run は modeset の flip の関数を通るので同じ model を使う。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- flip の性能（`perf: … per submit`）、device lost 0、survey の SURFLIVE と CRC の進み、`picture up` の行。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p049/xd-host` → 全部 pass（screen 60）。
- 旧の host 試験: `sh build/tmp/refcmp.sh 5d36d047b p049b` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p049` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
