<!-- awesome-plan project=zedbsd record=ws252-p005 -->

# ws252-p005: ICL の compiler と state

Status: in-progress（2026-10-26 q1599 P4: 実装・host 試験・build 済み。ICL の実機は無く未確認。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §6.2 の「直す（p005、compiler と state）」

## やった事（4e316feac）

- compiler: `intel/eu-encoding-gen11.h`（Gen11 の type の番号、Mesa 25.0.7 `brw_type_encode`）と eu.c の `i915_eu_format_gen11`（Gen9 の field・opcode・SEND の形に Gen11 の type。Gen9 と Gen11 の opcode が同じ事は Mesa `brw_eu.c` の表で確かめた）。compile.c は版 11 を受け、sampler の header（版 11 以上は Gen12 の形）、render target の header と dual source（版 12 未満は Gen9 の header: Gen11 の ex_desc の 15:12 は即値に入らない、Mesa `lower_send_descriptors`）。compute: thread の終わりの URB の bit は版 11 未満だけ、barrier の ID の mask は版 11 以上で Gen12 の値、fence は版 11 で buffer と shared memory の 2 つを別の reply に送り、両方の reply を null に MOV して待つ（Mesa の Gen11 以降の fence と Gen12 前の scheduling fence）。
- render: `intel/genxml-gen11.h`（VS 364・GS 224 threads、L3CNTLREG 0x80000640（URB 32・All 64・Wa_1406697149）、HALF_SLICE_CHICKEN7・TCCNTLREG・CS_CHICKEN1、3DSTATE_CPS）。版 11 の形: PIPE_CONTROL は data cache の flush（HDC の bit は版 12）、PIPELINE_SELECT の mask は 0x3、STATE_BASE_ADDRESS は 22 dword（bindless sampler 有り）、L3 bypass・depth bounds・CPS pointers・primitive replication・SF の deref・IDD の preemption は版 12 だけ、VF_SGVS_2 は有り、URB は Gen9 の packet と配分、depth/stencil は Gen9 の形、unorm の path は有り、MOCS の uncached は index 0、EOT だけの window は Gen9 の命令（同じ bytes）、VFE は subslice ごとに 56 threads で gateway timer 無し、GPGPU への切り替えと 3D への戻りは Gen9 の 2 つの PIPE_CONTROL と mask（VFE の再送・L3 の切り替え・CC の pointer の clear は Gen9 だけ）、context の始めに L3 の partition と 3 つの register と 3DSTATE_CPS（0）。kernel の版は 11（blit・pipeline）、device の名前「zedBSD i915 (Gen11)」。

## 確かめた事（2026-10-26）

- `sh plan/ws252/tests/render-host/run.sh`（新しい試験、ws229 の state-test を版 11 に広げた写し）: Gen12 と Gen9 の state・batch の 35 file が 684a5243f と byte で一致、ICL の 12 batch（738 命令）と 14 構造体が Mesa の gen110.xml で decode でき（field の外の bit 無し）、Gen12 との field の差は列挙した理由の物だけ → PASS。
- `sh plan/ws252/tests/eu-gen11/run.sh`（新しい試験、Mesa 25.0.7 の brw_disasm/brw_asm `--gen=icl` を `plan/tools/gpu-compute/host/mesa-tools.sh` で build）: encoder、vkdemo・mview・tests/render の vertex・fragment・geometry、ws229 の render target の module、compute の全部が disasm で誤り無く再 assemble で同じ bytes、拒むべき module は拒まれる、意図の 9 項目（barrier mask 0x7f000000・WAIT・SLM の fence・reply の MOV・URB bit 無し・sampler header を r0 から写さない・target 3 の header・dual の mlen/ex_mlen）→ PASS。
- 既存の回帰: `plan/tools/i915-gen9/eu-gen9-host.sh` PASS、`plan/ws031/tests/run-vk-gentool-test.sh`（Gen12）PASS。build 3 組 warning 0。

## 制限・未確認

- ICL の実機は無い: 描画・compute・blit の結果は未確認（p007）。slice hash table（pixel pipe が非対称な SKU で要る、Mesa `emit_slice_hashing_state`）は作っていない。ICL の SKU の URB・L3 bank の数の差は保守的な Gen9 の配分で避けた。
- hardware の試験の fixture（tests/fixtures/draw-fixture.c、tests/execution）は ICL の形を持たない（render-host は ICL の fixture を作らない）。p007 で実機の試験をする時に要る。
