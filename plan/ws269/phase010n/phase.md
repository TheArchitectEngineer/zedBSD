<!-- awesome-plan project=zedbsd record=ws269-p010n -->

# ws269-p010n: render の FW の object（後半）

- ID: ws269-p010n（親 [ws269](../ws.md)）
- Status: in-progress（q1377-i01 で「残り」の 1〜5 を実装、host 試験と build まで。受け入れの判定は Q1。実機は設計どおり p010e）
- Disposition: normal
- Queue: q1375 / q1375-i01（2026-10-20 P1、部分）、q1377 / q1377-i01（2026-10-20 P1）
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

## 残り（q1375-i01 の時点の一覧。q1377-i01 で 1〜5 を実装、下の節）

1. session に render context（CCB・timeline・record・ctx state・VDM の call stack、identity、`handle_notice` の kind RENDER）。
2. SR・PT の program（PDS の DOUTW の control word の encoder、M9 の照合）。
3. render の kick（117 と 101、FENCE_PR、`pds_pr_bgnd` の仮の program、sync の word）。
4. 全部の context の recovery。
5. sync の word から時間切れの段、`ta: fail(free list)`、空の render、render check の骨、kick の組み立ての golden。

## q1377-i01 で作った物（2026-10-20 P1、main 8542aed8b を merge した後）

- `pds-encode.{c,h}`: `powervr_pds_write_control_word`（DOUTW の src1: LAST・DEST・CMODE・AO・BSIZE）、`powervr_pds_stream_clear`（STMC）、`powervr_pds_load_word`・`powervr_pds_store_word`（PDS の ld・st の src0）。
- `render-program.{c,h}`（新規、host で build）: SR の PDS（DOUTW 2 個を Mesa `pvr_pds_write_constant64` の H32・L32 の順で、control word は 2 と 0x80004002、DOUTU は data の 8 語目から）、PT の store（st 15・15・2 語、wdf、halt）と resume（stmc 0x10、ld 6・6・4 の 64 bit、wdf、halt）、SR の USC（`load_store_sr` の形: vi0〜3 を読み、store は special register 103 の大きさを head に st32、load は ld で読み返し、0 なら終わり、burst は store 1024（0 と書く）・load 256、index register 0 を通した shared register の regbl の ld・st、1 instance なので p0 と分岐で loop）。USC の group は compiler の `shader_backend_group`（P2 の p010-compiler で試験済みの形）と、p013 q1372 の regbl の形。store 352 byte・load 336 byte、13 temps。static state の task の word（VDMCTRL_PDS_STATE0/1/2、TA_STATE_STREAM_OUT1/2、CR_VDM_CONTEXT_STORE/RESUME_TASK0〜2、STATE_BASE）。
- `render-kick-layout.{c,h}`: static state を mainline の形に直した（`ctxswitch_regs[0]` だけ、buffer ごとに別の task の word、task 3・4 は 0。前の試行は 2 core に同じ word を書いていた）。render の 3 つの kick の command の組み立て `powervr_render_sequence_make`（geometry: FENCE_PR（waits）→ GEOM（data set と partial render の fence = geometry の timeline と G−1）→ UPDATE G。PR: FENCE_PR（waits と最後に geometry の G）→ FRAG_PR → UPDATE F−1。本番: FENCE_PR（geometry の G）→ FRAG → UPDATE F。fragment の `pds_pr_bgnd` が 0 なら既定の background を入れる）、時間切れの段 `powervr_render_stalled`（wrap を跨ぐ比較）、free list の印 `powervr_free_list_ran_out`、VDM の terminate の word、geometry の VDM stream base、background の 3 語（CR_PDS_BGRND0/1/3）。
- `render-context.c`（新規、kernel）: session の最初の render で render context を作る。FW の heap に record（680）・geometry state（160、call stack の初期 pointer を 8 に）・fragment state（256）・geometry と fragment の CCB（各 32 KiB）と control と timeline。GENERAL に call stack・VDM state（92）・TA state（484）、PDS の窓に buffer ごとの組（SR state 16432、PT state 128、SR の store・load の PDS、PT の store・resume の PDS、組 0 に仮の background の PDS（halt だけ））、USC の窓に SR の store・load の USC（node の起動の時に 1 回作る、`powervr_render_switch_shaders`）。identity は compute と別の通し番号（`next_identity`）。destroy・discard（reset の後）。`powervr_session_next_cleanup`・`powervr_session_cleaned` で compute・geometry（record+0）・fragment（record+152）を順に。
- `render-queue.c`: CCB の操作を `struct powervr_client` に一般化（compute の道は同じ動き）、`powervr_session_cleanup`・`stop_begin`・`stop_poll` が全部の context を順に CLEANUP、`handle_notice` は compute の identity なら COMPUTE、render context の identity なら RENDER（geometry → `ta`、他 → `3d`）、`powervr_session_kick_render`（geometry と PR を 117 で 1 回、geometry の kick に HWRT の cleanup ctl、PR には付けない、本番の fragment を 101 で cleanup ctl 付き。timeline は geometry G = 前 + 1、fragment F = 前 + 2、PR が F−1）、`powervr_render_finish`（fragment の timeline を上限付きで待ち、時間切れは sync の word から `ta`/`3d` の `fail(timeout)`、終われば free list の印を比べて `ta: fail(free list)` か `ta: ok`、`3d: ok`）。
- `render.c`: node の起動で SR の USC を作る、`render_close` は全部の context の cleanup を呼ぶ、`CONFIG_POWERVR_RENDER_CHECK` の weak の `powervr_render_check` を compute check の後に呼ぶ。
- `render-check.c`（新規、`CONFIG_POWERVR_RENDER_CHECK=y` の時だけ build）: 骨。自分の session、render context、64×64 の target、terminate だけの VDM stream（GENERAL）を作り、geometry の stream base を入れて 3 つの kick の command を組み、kick はせずに `powervr: render check: built, not run (no fragment programs yet)` を出して session を閉じる（fragment には pixel event の PDS と EOT の USC が要り、p010e の範囲）。
- `platform/riscv64/vmunix.mk`: render-program.c・render-context.c、`CONFIG_POWERVR_RENDER_CHECK` の時の render-check.c。
- 試験: `plan/ws269/tests/p010/program-host-test.c`（新規、PDS の word・SR と PT の PDS の golden（Mesa の手順から手で計算）・task の word・USC の build）、`kick-host-test.c` に static state の新しい形・sequence の golden（waits 2 個・0 個・31 個・32 個の拒否、既定の background の有無）・stalled・free list・terminate・stream base・background の語。`run.sh` に program-host-test を足した（kick と data は残した）。

## 確かめ（q1377-i01、2026-10-20 09:40）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/p010n/p010` | stage 24/0、image 55/0、target 77/0、**kick 81/0**、**program 38/0**、selftest-host 0 failed、data 83/0 |
| executor（p009） | `sh plan/ws269/tests/p009/run.sh build/p010n/p009` | 162 passed, 0 failed |
| p006 | `sh plan/ws269/tests/p006/run.sh build/p010n/p006` | PASS 68 FAIL 0 |
| compiler（p010-compiler） | `sh plan/ws269/tests/p010-compiler/run.sh build/p010n/p010c` | 318/0、group 20/0 |
| M9（tree の外） | Mesa 26.2.5 の tarball（sha256 `794b3d17…a0a5`）を `build/p010n/dl/` に取り、`src/imagination` を展開、`vdm.xml`・`ppp.xml`・`cr.xml`・`lls.xml` の blob SHA は scratchpad の写しと一致（f6a8a84a・61605e64・82302665・a736a938）。`python3 -I …/gen_pack_header.py <xml> > build/p010n/gen/<x>.h`、`build/p010n/m9/compare.c` で zedBSD の word と csbgen の pack を比べた | `M9: 57 words/values compared, 182 fields, 0 mismatches`（task 0〜2 の store・resume を 5 つの置き場所で、VDM・TA の state base、VDM の stream base、terminate、BGRND0/1/3、LLS の大きさと alignment） |
| VF2・riscv64-virt・amd64 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-<c>.mk BUILD=build/p010n/<c> vmunix -j16` | 3 つとも rc=0、warning 0、error 0 |
| render check 付きの VF2 | 上に `CONFIG_POWERVR_RENDER_CHECK=y`、`BUILD=build/p010n/vf2-check` | rc=0、warning 0（render-check.o ができた） |

QEMU・実機は未実施（設計どおり実機は p010e。この Phase では QEMU を起動していない）。

## 制限・未確認（推測を含む）

- SR の PDS の data は Mesa に合わせて 64 bit の値を H32・L32 の順に置いた。USC は vi0 を size の address の low として読むので、Mesa の形どおりなら vi0 に high の半分が入る（Mesa の render context の switch がこの形で動くかは未確認、推測では FW が geometry を切り替えた時にだけ走る）。実機で context switch が起きた時の fault の候補として p010e の実機の文面に入れる。
- SR の USC は hand で組んだ loop（p0 と分岐、1 instance の前提）。encoder の bytes は compiler の試験済みの group の形だが、program 全体は実機で未確認。
- render check は kick しない（fragment の program が無い）。`ta`・`3d` の段の ok・fail の道（`powervr_render_finish`）は呼ぶ所が render check の p010e の版だけで、今は呼ばれない。
- compute の context の CDM の cswitch の state（design-p010.md §4.1 の「p010n で Mesa の形と比べて足りない物を足す」）は今回の再開の手順に無く、未着手。p009 の compute の道を変えるので、別の試行か p010e の前に Q1 が範囲を決める。
- geometry の command の他の register（PPP_CTRL・TE_PSG の region stride・border colour table・`vdm_context_resume_task0_size`）は p010e・p010g（render pass）で埋める。

## commit

- f55160351: `plan/ws269/tests/p010/target-host-test.c`（p010d の commit から漏れていた分、Q1 の依頼）
- 3d5f4b8b2: 上の source と kick-host-test・run.sh
- 片付け（Q1 へ）: `plan/ws269/tests/p010/run.sh.new`（誤って作った空の file、untracked）
- q1377-i01: 2edec387f（pds-encode・render-program・static state の形・program-host-test・run.sh）、9de43386e（sequence・stalled・free list の印・kick の試験）、d337c9b7c（render-context.c・render-check.c・render-queue.c の一般化と kick・recovery・notice・render.c・vmunix.mk）、dd53a7bb8（`pds_pr_bgnd` の仮の program と background の語）、と下の phase.md・ws.md の commit

## q1388-i01（2026-10-20 P1）: compute の context の CDM の cswitch state

- `render-sr-program.{c,h}`: compute の SR の PDS（render の物の前に coefficient の pass を飛ばす branch・cc 付きの halt・branch、DOUTU は cc 付き。ERN 35421 あり、compute_morton なし）、fence terminate の PDS（0 の source、skip、DOUTC、halt）、static state の 7 語（CDM_CONTEXT_PDS0/1、TERMINATE_PDS/1、LOAD_PDS0、組 1 の PDS0・LOAD_PDS0）。Mesa `pvr_compute_ctx_ws_static_state_init`。
- `render-context.c`: `powervr_session_compute_switch`（session を開いた後、core の lock の外で: PDS の窓に組 2 つ（SR の state 16432 と store・load の PDS）と terminate、USC の窓に SR の store・load の USC、compute の record の 152 からの static state に 7 語）、destroy・discard。`render.c` の render_open で呼ぶ（失敗は log だけ、static state は 0 のまま）。
- 確かめ: program-host-test 47/0（compute の code・terminate・7 語の golden）、p009 165/0、3 つの build warning 0。M9 は未（CDM の 5 つの register を足す時に）。実機は未確認。

## q1391-i01（2026-10-20 P1）: M9 の照合（CDM の switch と ZLS）

- Mesa 26.2.5 の tarball（sha256 `794b3d17…a0a5`）を `build/p010n/dl/` に取り直し、`src/imagination` を展開、`python3 -I …/gen_pack_header.py …/rogue/cr.xml > build/p010n/gen/cr.h`（sha256 `16f0f16f6e450483…`、前と同じ）。比べる program は `build/p010n/m9/compare2.c`（tree の外）。
- 結果: `M9: 31 words/values compared, 108 fields, 0 mismatches`。内訳: compute の static state の 7 語（CDM_CONTEXT_PDS0・PDS1・TERMINATE_PDS・PDS1・CONTEXT_LOAD_PDS0 と組 1 の 2 つ）を 3 つの置き場所で、ZLS は D32 と D16 の 64×64（clear 0.5、store）で render-depth の語を render-command に通した fragment の command の ISP_ZLSCTL・ISP_ZLOAD_BASE・ISP_BGOBJDEPTH と、zls_stride（layer の byte 数）・DEPTHBUFFER の flag（v7.3 の header の bit 7、csbgen には無い）。
