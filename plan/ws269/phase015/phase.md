<!-- awesome-plan project=zedbsd record=ws269-p015 -->

# ws269-p015 安定性（板の連続実行・電源の入れ直し・GPU の reset からの回復）

- 親: [WS269](../ws.md)
- Status: in-progress（2026-10-21 Q1。(1)(2)(3) は板で済み。(4) 長時間はユーザーの確認待ち）
- 起点: [proposal-next-phases.md](../proposal-next-phases.md) の N2。ユーザー 2026-10-21「続けてください。」
- 板の kernel: build/uat/vf2-gpu/vmunix（main 06f1ba788、板の /boot/boot0/vmunix と cmp で一致）。

## 範囲と結果

| 確かめ | 手順 | 結果（2026-10-21、板の実機） |
| --- | --- | --- |
| (1) 1 回の起動で連続 10 回 | SSH で `sudo sh -c "gpu-selftest; vkmatrix; vkcoherency"` を 10 回（約 36 s/回） | 10 回とも gpu-selftest 13 ok / 0 fail / 0 skip、vkmatrix 15/15、vkcoherency PASS。dmesg に context reset・device lost・lockup 0 行 |
| (2) 電源の入れ直し 3 回 | `VF2_HOST=… VF2_PASSWORD=… plan/tools/vf2/run.sh build/uat/vf2-gpu/vmunix build/uat/vf2-gpu/runs/stabN`、各回の後に (1) の 3 つ | 3 回とも stages 全部 ok（present は -）、gpu-selftest 13/13、vkmatrix 15/15、vkcoherency PASS、reset 0。deploy から render node まで 38 s |
| (3) GPU の reset（意図的な lockup）からの回復 | `sudo /tmp/vkreset --isolated`（2026-10-21 P1、q1519。下の節）→ 別の process で vkcompute・vkmatrix・gpu-selftest | 板 98（2026-10-21 Q1、kernel main 8f8c97105）: hang・close・compute・clear が全部 PASS、`vkreset: PASS` exit 0（hang は 2052 ms で DEVICE_LOST、close 23 ms）。dmesg は `compute stream did not finish (42); the session is abandoned` → `context reset notice: context 0x2c reason 1 dm 4`（期待の (b)。`session lost to the reset of context` の行は無い）。後の vkcompute 4096/4096、gpu-selftest 13/13、vkmatrix 15/18（落ちた 3 つは q1520 の 64・128 bit の既知の不一致）、stages 全部 ok。firmware の再起動は不要。reset の間は stages の compute が `fail(timeout)` になり、次の gpu-selftest で ok に戻る |
| (4) 長時間（vkdemo 30 分など） | 負荷・耐久の試験はユーザーに確かめてから夜間（AGENTS.md） | 未実施（ユーザーの確認待ち） |

## (3) の試験の program（2026-10-21 P1、q1519 / q1519-i01）

### program

`userland/tests/vkreset/`（main.c・hang.comp・shader.h・Makefile）。

- 既定の gpu-selftest には入れない。`plan/tools/powervr/p010/config-visionfive2-selftest.mk` に `vkreset` を足した。
- link の規則は `platform/common/userland-dynamic.mk`。amd64 の静的 program の一覧からは外した（`platform/amd64/vmunix.mk`）。
- 手で `vkreset --isolated` と流す。引数が無いと 2 で終わる。

| 段 | 中身 | 合格 |
| --- | --- | --- |
| hang | 終わらない compute の kernel を投げる（hang.comp。i を 0 から 2 ずつ増やし、奇数の limit 1 と比べる。1 workgroup）。fence を 120 s まで待つ | vkQueueSubmit か vkWaitForFences が VK_ERROR_DEVICE_LOST |
| close | lost の device と instance を壊す（node を閉じる） | 戻ってくる（かかった ms を出す） |
| compute | 同じ process で新しい instance と device を作り、vkcompute の fill（128 wg × 32）を流す | 4096/4096 |
| clear | もう 1 つ新しい instance で、64×64 B8G8R8A8 を render pass で clear し、copy して読む | 4096/4096 pixel が 0xff336699 |

- **host の確かめ**:
  - hang.comp は powervr の compiler で compile できる（host の probe で `error=0 length=344`）。
  - VF2 の build: `timeout 3600 make ZEDBSD_CONFIG=plan/tools/powervr/p010/config-visionfive2-selftest.mk BUILD=build/q1519 -j16 vmunix build/q1519/bin/vkreset` で rc=0、自前の source の warning は 0。
- **draw が入っていない**: 同じ process の段に draw は入れていない。新しい process の draw は、下の手順の vkmatrix と gpu-selftest で見る。

### code で読んだ回復の道（driver は変えていない）

1. **submit の待ち**: `vk-queue.c` の `run` は、compute の stream を 2 s（`QUEUE_WAIT_MS`）待つ。
   - 終わらなければ `powervr: vk: compute stream did not finish (42); the session is abandoned` を出す（42 は ETIMEDOUT）。
   - session を abandoned にし、`submit_result` が VK_ERROR_DEVICE_LOST を返す。
2. **firmware の HWR**: firmware が自分で lockup を見つけた時は、context reset の通知（105）が来る。
   - `context reset notice …` と `session lost to the reset of context …` が出て、その session だけが lost になる。
   - device は lost にならない。
3. **close**: core の `gpu_monitor_close` が `powervr_render_stop_begin`・`_stop_poll` を呼び、firmware に context の cleanup を頼む。
   - 動いている kernel の context は BUSY のままになる。
   - 100 回（`CLEANUP_TRIES`）か 30 s で EBUSY・ETIMEDOUT になる。core の stop の期限（`CONFIG_GPU_JOB_STOP_MS` 10 s）でも同じ。
   - その後 `powervr_render_isolate` が `session isolated (compute context 0x…)` を出し、health check で firmware が答えれば、device の quarantined を数える。
   - 2 の道で cleanup が通った時は、isolate しない。
4. **reset**: 次の open が唯一の session（`sessions == 1`、他の open も share も無い）で、quarantined か error がある時だけ、core が `powervr_render_reset` を呼ぶ。
   - firmware を image から起動し直し、電源も入れ直す。これで終わらない kernel も止まる。
   - `powervr: firmware restarted after a reset` を出す。
   - **他の GPU の client（greeter・Keiland・gpu-selftest-boot）が node を開いていると reset されない**ので、試験は他の client が無い時に流す。

### 板の手順（Q1）

1. 試験用の image（`build/uat/vf2-gpu`、text の login、上の config に vkreset を足して作り直した物）で起動する。
   - 起動時の gpu-selftest（gpu-selftest-boot）が終わるのを待つ。
   - graphical login の U36 では、greeter を止めてから流す。
2. `ps ax` で、他に GPU を開く process が無いことを確かめる。
3. `dmesg > /tmp/before.txt`、`sudo vkreset --isolated` を流す。最大でおよそ 3 分（hang 2〜120 s、close〜40 s）。
4. 続けて新しい process で `vkcompute`、`vkmatrix`、`gpu-selftest` を流す。
5. `dmesg` の差と `sysctl hw.gpu.powervr.stages` を見る。

**期待する dmesg の行**:
- `powervr: vk: compute stream did not finish (42); the session is abandoned`
- 次のどちらか一方、または両方:
  - (a) `powervr: session isolated (compute context 0x…)` と、`powervr: firmware restarted after a reset`
  - (b) `powervr: context reset notice: …` と、`powervr: session lost to the reset of context 0x…`

**合格**:
- vkreset が `hang: PASS`、`close: PASS`、`compute: PASS (… 4096/4096 words)`、`clear: PASS (… 4096/4096 pixels)`、`vkreset: PASS` を出し、exit 0 で終わる。
- 4 の 3 つが通る: vkcompute PASS、vkmatrix 15/15、gpu-selftest 13/13。
- 次の行が無い:
  - `device lost: firmware restart failed`
  - `the render node stays lost`
  - `device quarantine armed`
  - `firmware restart failed`

**不合格**:
- 上のどれかが外れる。
- vkreset が 5 分を超えて戻らない（close が止まる）。

**結果を P1 に渡す物**: vkreset の出力、dmesg の差、stages。

## 残件

- (4): ユーザーの確認の後。

## 2 つ以上の session の 3D の lockup（q1530、2026-10-21 P1）

### 板の事実（Q1）

- desktop の login の時に、3D の reset（`reason 1 dm 3`）が出る。
- vkmatrix の `load-1080-textured`・`-6`・`-linear` は、1 つずつなら PASS。
- 3 つを別の process で同時に流すと、`load-1080-textured` が `context reset notice: context 0x81 reason 1 dm 3 … pc 0x504c0001` → session lost になった。
- **2 つ以上の session が同時に描く時にだけ起きる。**

### 直した事（仮説）

- 我々の firmware は mainline のもの（rogue_36.50.54.182_v1.fw、1.1.OS@6976702）。
- 前の設定は vendor の driver（別の firmware の DDK 1.19）の値だった。
  - system flags に context switch の MEDIUM の profile。
  - OS flags 0xF（TDM・GEOM・3D・CDM の context switch を on）。
- mainline の driver（`pvr_fw.c`）は profile を付けず、OS の flags も立てない。
- switch が on だと、firmware は別の context の job のために 3D を止めて context を保存・再開する。我々の render context の switch の状態（保存・再開の program）は完全でない。
- **既定を mainline と同じに替えた**: profile なし、OS flags 0。
  - `interface.h` の `POWERVR_CONTEXT_SWITCH`（既定 0）で、前の値に戻せる。
  - 起動の行は `powervr: firmware: system flags 0x00000010, os flags 0x00000000` になる。
- 1 つの session では、switch の on・off で結果が変わらなかった（板 59・66、q1463）。

### 試験用の一時の例外（Q1 承認、2026-10-21）

- root の `sysctl hw.gpu.powervr.stages=N`（と skip の mask）で、render の job の draw を落とす試験用の filter（b07ea4a73）がある。
- coding-style.md §12 の「試験だけの切り替えを production に置かない」の例外とする。
- lockup の原因が分かったら外す。

### 板の確認（Q1）

1. 3 つの同時の vkmatrix（上と同じコマンド）を数回流し、reset が出ないこと。
2. desktop の login で 3D の reset が出ないこと。
3. gpu-selftest 13/13・vkmatrix の全部（1 つずつ）。

### 原因と直し（q1530、2026-10-21、Q1 が cleared）

- **原因**: 我々は render target の 2 つの half のうち、いつも half 0 だけで描いていた。
- **何が起きたか**（P2 が firmware の trace を読んだ、板 33）:
  1. 我々の TA と 3D の間に、別の session の transfer（3D の data master の TQ）だけが入る。
  2. firmware は、3D の側に載っている free list と render target data が同じなので、PM（parameter manager）の状態の読み直し（"Loading stack-pointers"）を省く。
  3. 我々の TA が書き換えた TE の catalogue（例 0x5cd0a001 → 0x5ccef001）が 3D に渡らない。
  4. 3D の parameter fetch（IPF）は前の render の catalogue をたどり、もう使い回された page で address 0 を読んで BIF fault → lockup になる。
- **2 つ以上の session でだけ起きた理由**: 別の session の render の 3D が間に入ると、firmware は読み直すので起きない。
- **直し**（562c6550c）: Mesa と同じく、render ごとに half を交互に使う。各 half は自前の macrolist と region headers を持つ。
- **合わせて入れた物**:
  - geometry は context の前の 3D を firmware の側で待つ（679e119bb）。
  - fragment は自分の geometry を待つ（前から）。
- **板 34**:
  - `vkmatrix -p 2 -r 10 load-1080-6` PASS ×2、`-p 3 -r 3 load-1080-textured` PASS、HWR 0。
  - desktop（Settings・Terminal・Files を開いた状態）で vkmatrix 29/29、vkcoherency PASS、reset 0。
- **途中の試しで原因でなかった物**: context switch の on・off（f9aa75bc9）、session ごとの address の分け（6d1e187c1）、free list の不足（grow・partial render の行は 0）。

### 片付け（q1533、2026-10-21 P1）

- 外した物:
  - 試験用の draw の filter（`hw.gpu.powervr.stages` への書き込み、`src/kern/sysctl.c`・`include/kern/sysctl.h` の hook、`stage-log.c` の解析、`render-job.c` の filter）。coding-style §12 の一時の例外は終わり。
  - `POWERVR_SESSION_SPREAD`。
- `POWERVR_DIAGNOSTICS` の時だけ出すようにした物:
  - `target made` の行。
  - reset の時の noted target・catalogue の行。
- 残した物（既定で出る）:
  - reset の通知の page catalogue。
  - HWR の dump の switch。
  - grow・partial render・out of memory の行。
