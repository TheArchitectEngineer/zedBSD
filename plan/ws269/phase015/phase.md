<!-- awesome-plan project=zedbsd record=ws269-p015 -->

# ws269-p015 安定性（板の連続実行・電源の入れ直し・GPU の reset からの回復）

- 親: [WS269](../ws.md)、Status: in-progress（2026-10-21 Q1。(1)(2)(3) は板で済み。(4) 長時間はユーザーの確認待ち）
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

- 既定の gpu-selftest には入れない。`plan/ws269/tests/p010/config-visionfive2-selftest.mk` に `vkreset` を足した。
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
  - VF2 の build: `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/q1519 -j16 vmunix build/q1519/bin/vkreset` で rc=0、自前の source の warning は 0。
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
