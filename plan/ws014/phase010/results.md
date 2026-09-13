# WS014 p010 結果: GPU監督の共通化仕上げと局所隔離（q313）

状態: 完了。2026-09-14に最終artifactで10VMの実QEMU受入を通過し、q313-i01 / p010をclearedとする。GitHub Issues/Projectへのq312完了・q313開始・q313完了の公開は自動承認レビューで保留中（[start summary](../../.sync/drafts/q313-start/summary.md)）。source/patch/資料のgit add/commit/pushはユーザーが行う。

前提はp009自己レビュー [gpu-stack-review5.md](../gpu-stack-review5.md)。契約の差分は [gpu-supervision-contract.md](gpu-supervision-contract.md)、段階は [phase.md](phase.md)。

## 段階の実装

| 段階 | 内容 | 実装 |
| --- | --- | --- |
| S1 期限（D1/B3） | 停止期限の起点を `stop_begin` の実呼出しへ。実行期限内のbackend所有jobが残る間は停止を開始しない。escalationは PENDING かつ期限超過のときだけ | `gpu.c` `gpu_session_fail_locked` / `gpu_monitor_step` |
| S2 close（D2） | graceful close（`gpu_close`、hard=0）は `GPU_JOB_COMMITTED` の記録とそのfenceを終端せず、実完了時に本来の結果を公開。RESERVED/ordinary command は従来どおり終端。`gpu_fence_retire` はdrain後 | `gpu.c` |
| S3 移管 | `native_submitted` による停止shortcut、fault cancel後のsession失敗、`stop_poll`==0後のRESERVED回収（`gpu_session_reclaim`）、Venusの`stopping`admission検査除去、`CONFIG_GPU_CONTROL_MS` | `gpu.c`、`venus.c`、`transport.c`、`Makefile`、`kernel-options.list` |
| S4/B6 | `gpu_session_leave` の monitor 起床を停止中sessionに限定。`drv_gpu_recovery_ready` 除去 | `gpu.c`、`gpu.h` |
| S5 隔離（D3/B4/B5） | `drv_gpu_recovery_ops.isolate`（ops版9）。停止未確認contextは `GPU_STOP_QUARANTINED`、deviceのerrorは0のまま。Venus `drv_venus_transport_isolate` は当該contextのchainをQUARANTINEDにしcallbackをEIOで公開、destroy/closeはhost送信なし、遅延返却chainはFREEへ回収。idleの新規open（他owner無し）でchecked resetし回収。collectはvalidateのEIOだけをtransport全体の失敗にする | `gpu.c`、`gpu.h`、`venus.c`、`transport.c`、`display.c`、`internal.h` |
| U追従 | `vkWaitForFences` は自contextのPOLLERRが立つときだけdevice lossをlatchする。importした他processのfenceのERRORでは自deviceを失わない | `libvulkan/sync.c` |
| 回収待ち | checked reset中に到着した別processのfresh openはENODEVで拒否せず、resetの結果を待つ | `gpu.c` `gpu_recover_open` |

UAPI（ioctl番号・layout・size）は不変。HAL変更なし。

## 途中で見つかった問題

- **exit-hang-001**: 隔離自体は成功したが、consumerの自device作業が `vkMapMemory` で DEVICE_LOST。libvulkanがimport fenceのERROR（他processのproducer消失）で自deviceをlostにlatchしていた。上記U追従で修正。
- **exit-hang-004**: 隔離とpeer継続は成功、idle openのchecked resetも成功したが、直後の通常2process試験で子processが `vkEnumeratePhysicalDevices` 0件。reset中の2つ目のopenが `gpu_recover_open` でENODEVになっていた。resetを待つよう修正（005で回収PASS）。
- **producer-exit-001**（既定policy、event待ちjob、producer終了）: consumerのfenceが約20秒で **SUCCESS**。host（i915/ANV）がevent待ちsubmissionを最終的に完了させ、共通層はその実結果を公開した。p008/p009の「close時に即ERROR」期待はclose終端に依存していたためp010では成立しない。期待値を「実結果（SUCCESSまたは実行期限のDEVICE_LOST）かつconsumer継続」に更新（002でPASS、result=0 elapsed=19960 ms）。停止確認不能な真のhangは短縮期限の `producer-exit-hang` で受け入れる。
- exit-hang-002/003 はlibvulkan修正の宣言位置誤りでbuild失敗（source未変更の再実行）。libvulkan-notify fixtureはpollの健全性probe（timeout 0）を許容するよう追従。

## 最終の実QEMU受入（2026-09-14）

private host `awe@10.0.10.25`、QEMU 10.0.11/KVM、2vCPU/1GiB、i915/Intel ANV、`virtio-vga-gl,venus=on,blob=on,hostmem=256M,max_outputs=1`。10VM全て `status=pass`、`guest_completed=true`、QEMU exit0。各attemptのkernel/imageはそれぞれのbuild directoryの現在の生成物と一致（[runtime-verification/summary.json](runtime-verification/summary.json) `final_artifacts_consistent=true`）。既定policy（build/q313-amd64）と短縮policy（build/q313-context-amd64、実行8秒・停止10秒）は同じ最終sourceからbuildした。

| attempt | VM時間 | 受入内容 |
| --- | --- | --- |
| q313-exit-delayed-003（新設） | 19.698秒 | delay pair、既定policy。commit済み有限jobを持つproducerが終了。consumerのimport fenceは14750 msで **VK_SUCCESS**（host通知遅延15000 ms）、consumerの自deviceで後続submit成功。device faultなし（D1/D2） |
| q313-exit-hang-006（新設） | 28.178秒 | 短縮policy。event待ちjobを持つproducerが終了。consumerは8000 msで実行期限のDEVICE_LOST、Kは`context=2 quarantined`を記録し、consumerは22秒後も自deviceで作業継続（ISOLATION_PEER_OK）。全process終了後の新規openでchecked reset（`recovered after all prior sessions`）、続く通常2process試験PASS（回収 {'checked_reset': True, 'status': 'pass'}）（D3） |
| q313-producer-exit-002（期待値更新） | 24.447秒 | 既定policy。event待ちjobのproducer終了後、consumerは19960 msでhostの実結果（result=0）を観測し自deviceで継続。close時の捏造ERRORなし |
| q313-direct-002 | 41.796秒 | 標準Vulkan直接表示lifecycle回帰 |
| q313-wayland-002 | 47.258秒 | Wayland WSI lifecycle回帰（compositor世代・client中断・console復帰） |
| q313-submit-load-002 | 10.886秒 | 2process×576 submit、OOM 0 |
| q313-completion-delay-002 | 25.213秒 | 15秒遅延完了: producer 15010 ms result 0、peer 126件/20000 ms継続 |
| q313-context-timeout-002 | 25.296秒 | 短縮policy: producer 8020 msでDEVICE_LOST、peer 126件継続（stop確認後、隔離なし） |
| q313-producer-stop-002 | 19.341秒 | SIGSTOP中のproducerを7780 msで終端 |
| q313-recovery-002 | 13.926秒 | renderer停止: watchdog 10000 ms/status 42、device全体faultとchecked reset |

| 生成物 | SHA256 |
| --- | --- |
| build/q313-amd64/vmunix | `165b603876fdd07d779569f9cfb5f513fd36aaa8f2abc4873f691f7572960576` |
| build/q313-amd64/hdd-image.img | `79ce6fa460e5715c83edf989f0c5547fe125173e97d3777c569c1504b5ab65da` |
| build/q313-amd64/dynamic/libvulkan.so | `82f07b64cc45651aca3e3d56a49336f541ecd8696abe8d1fd0f33962327e38f9` |
| build/q313-context-amd64/vmunix | `e4133a1c7de25100be542b6e96ce05a8fda69e5cd5b6caf3085d8ecd3ccf77aa` |
| build/q313-context-amd64/hdd-image.img | `5587396332d2ffdd17bcdde0b3a3cd9070dae1d87e71fc88e42b5afdfde1c5a8` |
| q312-quiesce server / library | `745bef59c17c88975ee567c0ae8c2c689b5b06fca79171d121227d0f5f5f84c2` / `38447deeaac67a7c9ff4613115a49b2d60a10acda6c073f2d4c73d0af718fbd4` |
| q312-quiesce-delay server / library | `e250106ba75f5592799697616012375af3946440d96cf9b719720eee0ba88444` / `32d152dd251711b6f7a956abc199c81fcb59fa9ece263af7b773d78f36c7340f` |

履歴attempt（旧artifact・修正前）は summary.json の `history_attempts` に保持: exit-delayed-001/002、exit-hang-001/004、direct/wayland/submit-load/recovery/completion-delay/context-timeout/producer-stop-001、producer-exit-001（失敗）。

## 限定検証

[core-verification/verification.json](core-verification/verification.json)。実productionコードを含むGPU core fixture 10種、handle-fd、Venus backend/transport/sharing/client/edid、GPU build selection（6platform×Venus有無）、ws030 libvulkan sync/notify/external-fence/job-race/command-context を通常＋ASan/UBSanでPASS。`git diff --check` OK。

追加したfixture: gpu-supervision（committed継続、metadata-only closeのshortcut、予約回収、per-context隔離とpeer継続、idle openでのreset回収、global fallback保持）、gpu-job（committed job survives close / reserved terminates at close）、venus-backend（隔離contextのdestroy/close/reset、admissionは共通層）、venus-transport（per-context quarantine、peer継続、遅延返却の回収、supervised refusalの局所化）。

## 変更ファイル SHA256

| ファイル | SHA256 |
| --- | --- |
| src/drivers/gpu/gpu.c | `d70a500a092952dfdace8687fc3eb99f84f6c86a50d5c0ca5f0ecf07d4e699d8` |
| include/drivers/gpu.h | `7a20b3531f9065a455568be8f2c3beb568fec1eebd37c987b116618786f57962` |
| src/drivers/gpu/venus/venus.c | `0d383e301f28acf25217b2d4aa87109cfea736712e0959f530340bb5f27e822f` |
| src/drivers/gpu/venus/transport.c | `6cbbbdac9d23b1a7f4cb2dd9a443e55648eb38cb31ab46f615e6aae83a6aa20a` |
| src/drivers/gpu/venus/display.c | `0763cb4a8d1e2b6f86c0eb52c91a672f2015f3085f64cf6429e284afdac95ccd` |
| src/drivers/gpu/venus/internal.h | `bc7629b729cb181f9ec14af1473dc3f5d7d8950752a4b1dd0761486821d35ba3` |
| userland/base/libvulkan/sync.c | `39bad660785c8dea12f1afa48d2d79bd4e3f5143f6c5a61baea8bcb2067ef1d8` |
| userland/base/tests/gpu-fence/main.c | `18dc6e0881c745bef88db36da58270cc860679f4f4a1e850b864e88c6b1ea38d` |
| Makefile | `32f10869e05a146d02d4dc81b55f5aec7c75f7b9fc5d6f3f2301d617de455be6` |
| config/kernel-options.list | `b2ebe2db38c7273ea47c68e58359691614a535a66c76584e139924b9182b624c` |
| plan/ws014/tests/gpu-supervision.c | `cfd29bbc12179ea17fbd142b70ff2499491a35db4c14ee21e656bc4969741a87` |
| plan/ws014/tests/gpu-job.c | `123e22703d67e84f3fe456fa5228df0ba8ba7bc9cfb5197e4ba7728cf0f86430` |
| plan/ws014/tests/venus-backend.c | `23c6b8951ac9b99ee46b6c9edc22108ddb491d7d6afebcd6a2aad35a83adedad` |
| plan/ws014/tests/venus-transport.c | `06fada0ade489004f00a56c0136936bd8810f5cd5fbd3b000c028b2b7d4271f6` |
| plan/ws014/tests/wayland-qemu.py | `83ba46ef59eeedbf28576e58325a7084faabf3aaf4c138da36589d8f9442f212` |
| plan/ws014/tests/venus-qemu.py | `98a7b7dc3a00ad470ecf18f4fd5ebaf9337388a87e04177f3ba5115559757ed9` |
| plan/ws014/tests/run-venus-remote.py | `937f9706d62419a9f71d22f7bebb7412618bf269d683b77cae091cbf2f174d9c` |
| plan/ws030/tests/libvulkan-notify.c | `b3be0843b9ccab02828db35d99d364d9f3f6b05d05115b42a62ffb0fb74b69c6` |

## 現在の制限

- 隔離で失った容量は次のidle open（他のopen/shareが無い）でのchecked resetまで戻らない。隔離が続いて容量が尽きた場合の自動escalationは未実装。
- 隔離contextが表示ownerだった場合、scanoutの実状態はresetまで残る（arbitration記録だけ外す）。
- closeは残したcommit済みjobの退役まで待つため、process exitがその分遅れる（Linuxと異なる）。
- 既定policyでのevent待ちjobは、このhostでは約20秒で完了する。真のhangの受入は短縮policyのproducer-exit-hangで行った。
- 任意GPU間DMA、native i915、一般Wayland/toolkit、CTSは未受入。stock互換（R2）は対象外のまま。

## 再現

```
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> --build-directory build/q313-amd64 \
  --config plan/ws014/tests/config-wayland-amd64.mk --fault-test producer-exit-delayed \
  --render-server .../dependencies/q312-quiesce-delay/install/libexec/virgl_render_server \
  --renderer-library-dir .../dependencies/q312-quiesce-delay/install/lib/x86_64-linux-gnu --timeout 180
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> --build-directory build/q313-context-amd64 \
  --config plan/ws014/tests/config-wayland-context-timeout-amd64.mk --fault-test producer-exit-hang \
  --render-server .../dependencies/q312-quiesce/install/libexec/virgl_render_server \
  --renderer-library-dir .../dependencies/q312-quiesce/install/lib/x86_64-linux-gnu --timeout 300
```

他の回帰は p009 の再現手順と同じ（`--fault-test` を差し替え、`--skip-build` で既存生成物を再利用）。実際のargvとhashは各 `plan/ws014/temp/remote/<attempt>/result.json`。
