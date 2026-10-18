<!-- awesome-plan project=zedbsd record=ws245-p024 -->

# ws245-p024: well の所属（W3）と refcount（xd-power）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない））
Parent: [WS245](../ws.md)
Queue: q1229-i02（P1、ws245-p024〜p046 を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p024 の行、§3.4（照会は lock を取らない）、§6.7（p006 の保存した `xd-power.c` の get/put/wells_of を当て直す）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-power.c/.h`（Zlib） | 数え（新）: `struct i915_xd_power`（well ごとの利用者の数 `refs[64]`、遅れの返しの借り `owed[2][64]`、work の状態、lock 無しで読む on の記録 `on`、診断の数）。`drv_i915_xd_power_init`・`_bind_work`・`_wells_of`（機能の集合 → table の well の集合）・`_get`（table の順に数え、最初の利用者で on、work への借りがあれば引き取る、失敗で巻き戻す）・`_put`（逆順に返し、最後で off、off を拒まれた well は利用者 1 を残す）・`_put_later`（work への借りにして work を arm。arm 中の物は次の回へ、遅れは大きい方）・`_work`・`_flush`・`_owes`・`_has`（記録だけを atomic で読む）・`_record`。well の on/off は owner の `struct i915_xd_power_switch` を通す（DC_off の enable/disable は p025 まで殻の旧の本体）。手順は block ごとに `Procedure referenced:`（intel_display_power.c の __intel_display_power_get_domain・intel_display_power_grab_async_put_ref・__intel_display_power_put_domain・__intel_display_power_put_async・queue_async_put_domains_work・intel_display_power_put_async_work・intel_display_power_flush_work） |
| `display/xd.h` | `struct i915_xd` に `struct i915_xd_power power` |
| `display/power.c`（殻） | domain → 機能の集合の表 `i915_pw_functions`（display 12/13 の port の番号の違いは `i915_pw_port_function`。`Values referenced:` intel_display_power_map.c の xelpd・tgl の pwdoms）と wrapper だけに。`drv_i915_power_domains_init` は xd の table から `power_wells[]` の写し（name・ops・id・facts・`xd_well`。always-on の行は無くなり、TGL の TC cold の mailbox の well は写さない）と `domain_wells[]` を作る。get/put/put_async/work/flush は xd の数えを呼び、写しの `refcount` と async の診断の数を xd から写す。`is_enabled` は `drv_i915_xd_power_has`（lock 無し）。well の enable/disable/sync_hw の `hw_enabled` の書き込みは `i915_pw_set_state` で xd の記録にも写す。削除: `struct i915_pw_desc`、`i915_power_map_init`・`_init_tgl`・`_build`・`i915_pw_add`・`i915_pw_dom`、`i915_mask_*`・`i915_parked*`・`i915_verify_async_put_domains_state`・`i915_cancel_async_put_work`・`i915_grab_async_put_ref`・`i915_get/put_domain_locked`・`i915_queue_async_put_domains_work`・`i915_release_async_put_domains`、公開の `drv_i915_power_well_get/_put`、`IDX_*`・`BIT_PIPE_*`。3369 → 2962 行 |
| `display/power.h` | `drv_i915_power_domains_init` に `struct i915_xd *xd`、`drv_i915_power_well_get/_put` を削除 |
| `display/internal.h` | `struct i915_pw_domain_mask` を削除、`struct i915_power_well` の `domains`・`domains_all` を `xd_well` に、`struct i915_power_domains` の `async_put_domains[2]`・`async_put_wakeref`・`async_put_next_delay` を削除し `xd`・`display_ver` を足した。comment を今の形に |
| `display/display.c` | `drv_i915_power_domains_init` に `&display->xd` |
| `tests/execution/ktest-sync.c` | 遅れの fuse の ktest に xd の状態（ADL-P の記述と model の窓）を与えた（p023 から `pwc.xd` が無いと PW_1 の enable が EIO になっていた: ktest の I915_TESTS の組でしか走らない） |
| `tests/display/host-xd-power-test.c` | 数えの 25 の check（下）。24 → 49 |
| `plan/ws245/tests/run-xd-host-tests.sh` | power の試験の説明に数えを足した |

段の phase.md の必須の項目:
- 消す file: 無し（`power.c` は殻。init の列は p035、DC は p025 で xd に）。
- 消す関数と残る呼び手: 上の static 18 個と公開の 2 個（`drv_i915_power_well_get/_put` は power.c の外に呼び手が無い: `git grep` で 0）。`drv_i915_power_domain_wells`・`drv_i915_power_well_by_id`・`drv_i915_power_domain_hw_state_on` は殻に残る（呼び手は display.c・takeover.c・ktest-sync.c）。
- 殻が更新する旧の写し: `power_wells[]`（`refcount` は get/put/work/flush の後に xd の `refs` から、`hw_enabled` は well の操作で xd の `on` と一緒に）、`domain_wells[]`、`async_parked`・`async_grabs`・`async_work_runs`・`async_work_empty`・`async_released`・`async_requeues`・`async_flushes`・`async_state_errors`（xd の stats から）。読み手は display.c の well の log、takeover.c の sanitize、diagnostics.c の survey、dp-sink.c の stop の log、`tests/display/aux.c`。
- 表 R: PWR_WELL_CTL・_AUX・_DDI の書き手は p023 から xd-power だけ。数え（refcount）はこの段から xd、DC_off の操作は p025 まで殻。
- hook と lock: 殻が `pd->lock`（mutex）の下で xd の get/put/put_later/work/flush を呼ぶ。xd は lock を取らない。`drv_i915_display_power_is_enabled`（`interrupts.c` の `i915_pipe_power_on`・`i915_transcoder_power_on`、割り込みの reset と postinstall から）は lock を取らず、xd の `on` を `__atomic_load_n` で読む（旧も lock を取らずに `hw_enabled` を読んでいた。spinlock の下かは呼び手に依らず安全）。
- R11: power.c 0、power.h 0、xd-power.c/.h 0、xd.h 0、internal.h 0、display.c 0、ktest-sync.c 0、host-xd-power-test.c 0。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）・`host-lcd-modeset-test`（123）を段の前（9a98be12f の `git archive`）と後で流し、出力（821・394 行、pointer を除く）が一致。
- 確かめ方 2（scratch の harness、git に入れない: `plan/ws245/temp/harness/wells24.c`・`run-wells24.sh`。`-Wl,--wrap` で MMIO と wait を model に）: 全 domain の well の集合（名前）、全 domain の get → is_enabled → put の書き込みの列と refcount、INIT の get と is_enabled（pipe・transcoder）、sync_hw、AUX_USBC1 と PIPE_B の get、INIT の put、async（bind → put_async → get（引き取り）→ put_async 2 つ → work → flush_sync）を段の前後で比べた。**ADL-P（display 13）は全部一致**（1410 行）。
- **TGL（display 12）の違い（意図した物、Linux を優先: 決定 4）**: Type-C の port 5・6（domain の LANES_TC5/6・DDI_IO_TC5/6・AUX_USBC5/6・AUX_TBT5/6）が Linux の `tgl_pwdoms_*` のとおり DC_off・PW_2・PW_3 と自分の well を持つ（旧の表は TC1〜4 だけ）。旧と xd の両方で TGL の他の domain の列は一致。
- 違い: always-on の行が無くなった（ADL-P の `power_wells[]` は 30 → 29、TGL は 25 → 30（TC5/6 の 6 行を足し、always-on を除いた。TC cold の mailbox は写さない））。log の `intel_power_domains_init: wells=` の数と名前（`(xelpd map)` → `(<platform> wells)`）が変わる。dp-sink の stop の log の `well_refs` は always-on の数を含まなくなる。
- 違い: async の診断の数の意味: `parked` は遅れの返しの数、`grabs` は借りを引き取った get の数、`released` は work が返した well の利用者の数（旧は domain の数）。
- 違い: 遅れの返しは domain ではなく well の単位で work への借りになる。同じ well を持つ別の domain の get も借りを引き取る（hardware の振る舞いは同じ: well は借りがある間は落ちない）。work が返す順は 1 回の逆順の走査（旧は domain ごと）。
- TGL の TC cold の mailbox の well は殻が切り替えない（旧も持たなかった）。AUX_USBC・AUX_TBT の domain の get では xd の数えに入り、利用者がいる間は on と記録する（`is_enabled` が旧と同じ答え）。実際の TC cold の block は p044。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- well の集合が同じ（survey の PWR_WELL_CTL2 = 0x00000c03）、hotplug/TC の readout が well を取れる（`i915: TC:` の log が同じ）。
- `intel_power_domains_init: wells=29 … (Alder Lake-P wells)` の形の log（名前は platform の表の name）。
- 節目（ユーザーの一覧）: HDMI と USB-C DP の plug で 2 画面（AUX USBC の well、DDI B の well）、5320 の起動（TGL の well の木は PW_2〜PW_5）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p024/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 49、失敗 0。`I915_XD_STRICT` の検査は通る。
- 数えの check（host-xd-power-test）: pipe A は PW_A だけ、pipe B は PW_2 と PW_B、pipe C は DC_off・PW_2・PW_C、DDI B の IO は自分の well だけ（PW_2 無し）、Type-C 1 の AUX は PW_2 を要る、INIT の集合は DC_off・PW_2・PW_A〜D、DDI A・B の lanes は well 無し、pipe C の on の順と off の逆順、共有の PW_2 が残る、on の失敗で巻き戻す、off を拒まれた well が利用者を残し再び on しない、利用者の無い返しは error で切り替えない、on の記録（PW_2 だけでは pipe B は無い、PW_B も記録で有る、PW_2 の off で無い、機能 0 は常に有る）、遅れの返し（well は残り work を arm、引き取りで disarm、arm 中の返しは次の回で大きい遅れ、2 回目で残りを返して disarm、借りの無い回は何もしない、flush で全部、work 無しなら即時）。
- 旧の host 試験と harness: 上。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021`（`build/p021/k-*` の使い回し）で既定の vmunix（sha256 360d7b9aa09d9a1f…）と `I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組が rc 0・warning 0。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- PWR_WELL_CTL 0x00000c03、TC1・TC2 の declared・readout の行が基準と同じ、`display power: 29 wells (Alder Lake-P)`（今の文言、p042 で intel_power_domains_init の形から変わった）

満たしていない・未確認の基準と残る物:

- HDMI・USB-C DP の plug で 2 画面、5320 の起動: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
