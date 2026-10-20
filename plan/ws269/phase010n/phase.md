<!-- awesome-plan project=zedbsd record=ws269-p010n -->

# ws269-p010n: render の FW の object（後半）

- ID: ws269-p010n（親 [ws269](../ws.md)）
- Status: in-progress（q1375-i01 は部分の実装で返却。下の「残り」が未実装。Phase は uncleared のまま続ける）
- Disposition: normal
- Queue: q1375 / q1375-i01（2026-10-20 P1）
- 設計: [design-p010.md](../design-p010.md) 版 2.1 の §4.1・§4.2・§8.1・§10 の p010n
- 依存: O-p010d（p010d の render-target.c、main に merge 済み）、O-ENC（SR の burst、p013 q1372 で今の encoder で書けると確認済み）

## 受け入れ条件（design-p010.md §10 の p010n の行）

session を FW の context ごと（compute・render、transfer は p010f）に、render context の static state と SR・PT の program（Mesa の形）、identity と recovery（105 の `dm`・`FLAG_ALL_CTXS`、112 は device lost）、combined kick（117）と FENCE_PR（215）、`partial_render_geom_frag_fence`、`pds_pr_bgnd`（口と仮の program）、cleanup ctl、kick ごとの sync の word、空の render、段の行の `ta`・`3d` と `reset`・`fault` の理由、render check の骨。host 試験と build。実機は p010e。

## q1375-i01 で作った物（2026-10-20 P1）

- `src/drivers/gpu/powervr/render-kick-layout.{h,c}`（新規、lock も memory も持たず host で build）:
  - render context の record の offset（geometry 0・fragment 152・static 304・680 byte、switch の register 184 byte × 2 core、各 buffer 80 byte の store/resume task 0〜4）と geometry・fragment の ctx state の大きさ（160、256 = 余裕を持った fragment の ISP store）。v7.3 の check header の値と `_Static_assert`。
  - KICK（101）と COMBINED_GEOM_FRAG_KICK（117）の command（kick_data 32 byte、cleanup ctl は最大 3、FRAG_PR には付けない）。
  - geometry の command の `hwrt_data_fw_addr`（4）と `partial_render_geom_frag_fence`（84、geometry の timeline と seqno − 1）、fragment の data set、`pds_pr_bgnd` の位置（16＋400）、CCB の型 201・203・204・215。
  - render context の static state の埋め方（`pvr_render_ctx_ws_static_state_init` の手順: VDM・TA の state buffer、store/resume の task の word を両方の buffer に）。task の word そのものは SR・PT の program の後（残り）。
  - 105 の `context_reset_data` の読み（id・reason・dm・flags・fault_address）と段への割り当て（render の geometry → `ta`、他の master → `3d`、compute → `compute`、transfer → `transfer`。`FLAG_PF` なら `fault <va>`、他は `reset <理由>`）。
- `stage.{h,c}`・`stage-log.c`・`powervr-private.h`: 理由 `reset` と、値付きの `powervr_stage_note_fail_value`・`powervr_stage_fail_value`（`fail(fault 0x80001234)`、`fail(reset lockup)`、未知の reason は 16 進）。reset の語は v7.3 `enum rogue_context_reset_reason` 0〜16。
- `interface.h`: `POWERVR_NOTICE_PAGE_FAULT` → `POWERVR_NOTICE_FIRMWARE_FAULT`（112 は FW 自身の fault）。
- `render-queue.c` の `handle_notice`: 105 を `render-kick-layout` で読み、段の行を出し、`FLAG_ALL_CTXS` なら全部の session を lost にする。112・109 は今までどおり device lost。今は session の context は compute だけなので kind は COMPUTE（render の identity は残り）。
- `platform/riscv64/vmunix.mk` に render-kick-layout.c。

session と heap の API は変えていない（P2 の p010h への影響なし）。

## 確かめ（2026-10-20）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/p010n/p010` | stage 24/0、image 55/0、target 77/0、**kick 50/0**、selftest-host 0 failed |
| executor（p009） | `sh plan/ws269/tests/p009/run.sh build/p010n/p009` | 160 passed, 0 failed |
| VF2・riscv64-virt・amd64 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-<c>.mk BUILD=build/p010n/<c> vmunix -j16` | 3 つとも rc=0、warning 0、error 0 |

kick-host-test の中身: KICK・117 の word（kick_data の各 offset、cleanup、FRAG_PR の 0）、geometry の fence（seqno − 1）、static state の 2 core × 2 buffer の全 offset、105 の frame の読みと 4 つの段への割り当て、値付きの段の行（最長の行が 64 byte に収まる）。期待値は v7.3 の check header から手で書いた。QEMU・実機は未実施（設計どおり実機は p010e）。

## 残り（次の試行で、この順に）

1. session に render context: geometry・fragment の CCB（各 32 KiB）と control、geometry・fragment の timeline、FW の render context の record（680 byte）、geometry・fragment の ctx state（160・256）、VDM の call stack（Mesa `PVR_VDM_CALLSTACK_MAX_DEPTH` 1）を ctx state の初期 pointer に。最初の render で作る（`powervr_session_target` と同じ時）。identity は compute と別の通し番号、`handle_notice` で kind を RENDER に。
2. SR・PT の program（Mesa `pvr_arch_job_context.c`・`pvr_pds.c` の `pvr_pds_generate_shared_storing_program`・`_stream_out_init_program`・`_stream_out_terminate_program`、usclib `load_store_sr`）: PDS は DOUTW 2 個（SR の address と +48、data は Mesa の `write_constant64` の H32・L32 の順）＋ DOUTU。PDS の DOUTW の control word の encoder が `pds-encode.c` に無い（`dword_mask_const`・cache control・DEST_UNIFIED・LAST）ので足す。VDMCTRL_PDS_STATE0/1/2・TA_STATE_STREAM_OUT1/2・CR_VDM_CONTEXT_STATE_BASE の field は `vdm.xml`・`ppp.xml`・`cr.xml`（M9 の照合を足す）。LLS の buffer の大きさは `lls.xml`。取得済み（scratchpad の `p010n/src/`、SHA は Mesa の tree の一覧と一致）。
3. render の kick: geometry の CCB に FENCE_PR（依存）→ GEOM（`powervr_geometry_partial_fence`）→ UPDATE（geometry の timeline・kick の sync の word）、fragment の CCB に FENCE_PR（geometry の timeline と seqno、最後）→ FRAG_PR → UPDATE、117 で 1 回。本番の FRAG は FENCE_PR → FRAG（`pds_pr_bgnd` に仮の program）→ UPDATE、KICK 101 に `powervr_target_cleanup_address`。
4. recovery: stop_begin・stop_poll・isolate・`powervr_session_cleanup` を compute・geometry（offset 0）・fragment（offset 152）の全部に（CLEANUP の context の address は record＋offset）。
5. 時間切れの時の sync の word から `ta`/`3d` の timeout、UPDATE_STATS の差から `ta: fail(free list)`、空の render（終わりだけの VDM）、render check の骨（`CONFIG_POWERVR_RENDER_CHECK`）と kick の組み立ての golden の host 試験。

## commit

- f55160351: `plan/ws269/tests/p010/target-host-test.c`（p010d の commit から漏れていた分、Q1 の依頼）
- 3d5f4b8b2: 上の source と kick-host-test・run.sh
- 片付け（Q1 へ）: `plan/ws269/tests/p010/run.sh.new`（誤って作った空の file、untracked）
