# WS014 p009: GPUレビュー対応とフレームワーク共通化

状態: 完了。2026-09-13に最終artifactで8VMの実QEMU受入を通過し、q312-i01 / p009をclearedとする（下記「最終の実QEMU受入」）。開始時のGitHub同期は[start-sync.json](start-sync.json)、Phaseは[#396](https://github.com/awemorris/zedBSD/issues/396)。source、patch、資料のgit add/commit/pushはユーザーが行う。

## 実装の責務

| 範囲 | 共通GPUフレームワーク | Venus backend / libvulkan |
| --- | --- | --- |
| 容量と待機 | device/openの容量世代、QUERY→WAIT、終了・割込み・故障、observerとcallback参照の保持 | backendは実slotのtry-reserveとFREE通知。Uは必要なmutexを外して待ち、取得後に状態・世代を再確認 |
| job監督 | 予約10秒・実行60秒・停止10秒の既定値と設定/実効値照会、session単位のsticky error、有限停止と全体故障への移行 | backendはnative投稿・完了、contextの停止確認、隔離、checked resetを提供 |
| 資源寿命 | 論理ERRORだけではcallback所有権を落とさない。closeも停止確認または隔離の後に資源破棄へ進む | backendは実DMA停止を証明し、不明な資源を再利用しない。native fence完了とhost callbackの最後の使用を分ける |
| direct WSI | 既存fdの故障・表示変更通知を提供 | Uは画像不足時だけwaiter固有の起床pipeを作り、画像返却・故障・表示変更を同時に待つ |
| NULL fence | job completionと容量通知を供給 | terminal private fenceをまとめてresetし、READYを再利用。PREPARING中のproofを競合submitへ貸さない |

内部opsはversion8、新しい容量照会/待機は48byte、実効期限照会は40byte。既存UAPIのlayoutを変更せず追加した。GPU固有のfenceはdrv_gpu内に保持し、汎用kernの機能を増やしていない。timelineのVenus固有上限もbackend検証へ移した。

詳細は[共通契約](gpu-job-contract.md)、[U同期・WSI](userland-sync-wsi.md)、[transport](transport-sync.md)、[stock互換の検証](stock-compat/README.md)を参照する。

## 検証の読み方

| 証拠 | 確認すること | 証拠の限界 |
| --- | --- | --- |
| [共通層の限定試験](gpu-core-verification/verification.json) | 実productionコードの容量枯渇、observer、late callback、期限、raw close、reset中の再故障、GPUなしbuild | fixtureの停止ACKは実native GPUの停止証明ではない |
| [U限定試験](userland-verification/verification.json) / [最終admission境界](userland-verification/admission-boundary.json) | 3mutexを外した待機、再試行所有権、reset batch、実ppoll/pipe2のlost wake・複数waiter・故障 | native peerのtransaction計数をそのまま実GPUの速度改善率へ外挿しない |
| [公開API検証](api-verification/verification.json) / [API family](family-verification/verification.json) | 170 API、両ABI、Noct8file再生成、対象familyの限定検証 | Vulkan CTSの合格を意味しない |
| [renderer停止の提案](renderer-quiesce/README.md) | raw streamを含むnative idleとcallback退役の停止契約 | 提案の適用・意味fixture・最終VMの状態を別々に記録する |

## 途中で見つかった問題

- 最初の複数process VMは`vkCreateDevice`で失敗した。backendの静的capabilityに新しい容量能力のbitが漏れていたためで、広告と実get_infoのfixtureを修正した。submitが正常に処理された試験とは数えない。
- managed job callbackがすべて退役していても、raw opaque streamが未追跡native仕事を投稿している場合がある。一般UAPIの停止証明をライブラリの使い方に依存させず、context全体のnative idle確認を追加する方針とした。
- 共通層のraw-only close、monitor起動失敗、withdrawal、reset中の再故障は、再現結果と修正後結果を保存した。正常なcloseでも停止確認前に資源を破棄しない。
- Uのcontext故障を無条件にdevice故障へ広げた途中実装は既存Wayland試験で検出し、通知対象とerrorの範囲を修正した。
- production Venus停止処理のローカル適用が自動承認レビューで一度拒否された。操作の実際の範囲、既存の実行承認、具体差分、限定検証を[承認文脈](renderer-quiesce/approval-context.md)へ記録し、適用と検証の結果を混同しない。
- 2構成のイメージを同時buildした際、異なるBUILD directoryでも共有される`build/amd64/sysroot`の生成処理が競合した。生成済みsysrootを`make sysroot-amd64`で再構築し、イメージbuildを逐次実行して両構成PASSを確認した。GPU実装の失敗や修正として数えず、初回logも保持する。

## 最終の実QEMU受入（2026-09-13）

private host `awe@10.0.10.25`、QEMU 10.0.11/KVM、2vCPU/1GiB、i915/Intel ANV、`virtio-vga-gl,venus=on,blob=on,hostmem=256M,max_outputs=1`、isolated q312-quiesce pair (flags7) で実行した。8VMすべて`status=pass`、`guest_completed=true`、QEMU exit0。各attemptのkernel/image/libvulkan hashはそれぞれのbuild directoryの現在の生成物と一致する（[runtime-verification/summary.json](runtime-verification/summary.json) `final_artifacts_consistent=true`）。VM時間はbuild・転送を含まない。

| attempt | VM時間 | 受入内容 |
| --- | --- | --- |
| q312-direct-003 | 41.935秒 | 標準Vulkan直接表示。診断6画面（fixed [0, 1000, 2500] ms、live [0, 640, 1270] ms、mode毎3種の異なる画像）と独立oracle、通常表示のBLOB scanout 126要求・同寸法legacy 0件、別processの表示競合拒否（VK_ERROR_NATIVE_WINDOW_IN_USE_KHR、owner 90 frame完走）、SIGINT/再open/console復帰。 |
| q312-wayland-002 | 47.342秒 | FIFO/MAILBOX各完了、同一fd並行admission、buffer/optimal allocationのproducer終了後独立context import、標準external fence、topology QUERY/ACK（sequence 1）、compositor 3世代（通常終了・強制終了・再起動）、client中断・再接続、surface loss、console復帰。 |
| q312-submit-load-005 | 11.833秒 | 2process×3round×96 submit=576 submit、検証25,165,824 byte。NULL fenceとexplicit fenceを交互に行い、slot枯渇によるOOM 0件。 |
| q312-completion-delay-003 | 25.316秒 | delay pairで実native SUCCESSの通知を15000 ms遅延。producerは15000 ms後にresult 0（既定60秒実行期限内、故障扱いなし）。別contextのpeerは同時に127件を20150 msで完了し、producer close後も継続（peer_after_close=True）。 |
| q312-context-timeout-003 | 25.138秒 | 実行期限8000 msの短縮config。同じ15秒遅延でproducerは8030 msで-4（DEVICE_LOST）となり、peerは126件を20070 msで完了・継続。単一jobの期限超過が他sessionを故障扱いにしない。 |
| q312-producer-stop-002 | 19.438秒 | 短縮configでproducerをSIGSTOP（fd open、pending=1）。実行期限により7770 msでDEVICE_LOST、U worker・final closeを使わずKの監督で終端。終端後にSIGCONTし最終waitpidを確認。 |
| q312-producer-exit-002 | 4.165秒 | event待ちで未完了のGPU仕事を持つproducerが終了。consumerはERROR（-4）を直ちに観測、成功を捏造しない。 |
| q312-recovery-002 | 14.051秒 | 試験所有rendererのみ停止。watchdog 10000 ms/status 42、peer failure、旧参照の退役gate、renderer再開後のchecked reset、新contextの4096 byte往復とdecoder完了。 |

| 生成物 | SHA256 |
| --- | --- |
| build/vkdemo-amd64/vmunix | `32f1d647f780e8eabb976e4600cd04c649f9fa37ea0101064797d39d2e8a5c29` |
| build/vkdemo-amd64/hdd-image.img | `732b50ab9c5f48eb171748289cbc7d17bb43f9ae60e9aa157307f5304bf7424d` |
| build/vkdemo-amd64/dynamic/libvulkan.so | `1a277b6b9565bce22607d021948d3353a7a2fcfb7a0040c5609ed014973abe08` |
| build/q312-context-amd64/vmunix | `20310954db9597dc5d37f038e4104b9cf386dc218395fc610d4b2ce288878042` |
| build/q312-context-amd64/hdd-image.img | `28f96b5381ddb24dd12242c9637d7d877386bf0345fcfc18bbbf884a6a98a4da` |
| q312-quiesce server / library | `745bef59c17c88975ee567c0ae8c2c689b5b06fca79171d121227d0f5f5f84c2` / `38447deeaac67a7c9ff4613115a49b2d60a10acda6c073f2d4c73d0af718fbd4` |
| q312-quiesce-delay server / library | `e250106ba75f5592799697616012375af3946440d96cf9b719720eee0ba88444` / `32d152dd251711b6f7a956abc199c81fcb59fa9ece263af7b773d78f36c7340f` |

production sourceはgit `f62dc635`（source-freeze.json `cca12445`のtree + 承認済みquiescence適用 + test-only followup）。最終snapshot後のproduction変更はない。

## 資源と性能の観測（実QEMU）

submit-load-005の`load_samples`（1 roundはprocessあたり96 submit、4 MiB検証）と、harnessが50 ms間隔で採取したhost CPU tick（100 Hz）。単一試行の有限観測であり、倍率や定常FPSの主張ではない。

| 項目 | NULL fence（4 round） | explicit fence（2 round） |
| --- | --- | --- |
| submit数 / 検証byte | 384 / 16,777,216 | 192 / 8,388,608 |
| 合計elapsed | 5330 ms（13.9 ms/submit） | 2500 ms（13.0 ms/submit） |
| guest user/system CPU | 30 / 400 ms | 0 / 0 ms |

QEMU processのCPU差分は試験全体でuser 130 tick + system 65 tick（1.95秒、161観測）。render server workerは各数tick以下。NULL fence経路がexplicit fenceより遅いという因果はこの観測からは言えず、rustatの分解能（10 ms）も粗い。private fenceの一括resetによるnative transaction削減は[U限定fixture](userland-verification/verification.json)の計数で確認したものであり、上表の実時間とは別に扱う。

## 失敗履歴

| attempt | 結果 | 原因と処置 |
| --- | --- | --- |
| q312-submit-load-001 | FAIL | 子processの`vkCreateDevice`が`VK_ERROR_INITIALIZATION_FAILED`。backendの静的capabilityに`GPU_CAP_JOB_CAPACITY`が漏れていた。登録とget_infoを修正し、mask 24576のfixtureを追加。002以降PASS。 |
| q312-producer-stop-001 | FAIL | 既定policy（実行60秒）では、SIGSTOP中のproducerのjobもKがsignalするためresult=0となり、旧p008の「停止=ERROR」期待に合わない。これは新契約どおりの挙動。試験を短縮実行期限config + delay pairに変え、実行期限による終端を検証する002でPASS。 |
| q312-recovery-001 | FAIL | 旧peerのresource読出しが`ENODEV`ではなくsticky timeout（42）を返した。K側の局所/全体故障分離の結果であり、test側の期待値を`wait.status`に合わせて修正（source変更なし）。002でPASS。 |
| q312-direct-001/002、wayland-001、submit-load-002〜004、completion-delay-001/002、context-timeout-001/002、producer-exit-001 | PASS | 途中artifact（旧strict3 pair、旧image）での通過。最終受入は上表の同一最終artifactでの再実行を採用し、これらは履歴として保持する。 |

## 再現

```
python3 plan/ws014/tests/run-vkdemo-remote.py --attempt <id> --build-directory build/vkdemo-amd64 \
  --config plan/ws014/tests/config-wayland-amd64.mk --lifecycle \
  --render-server /home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install/libexec/virgl_render_server \
  --renderer-library-dir /home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install/lib/x86_64-linux-gnu \
  --timeout 180 --transfer-timeout 1200
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> ... --lifecycle
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> ... --fault-test submit-load|producer-exit|recovery
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> ... --fault-test completion-delay   # q312-quiesce-delay pair
python3 plan/ws014/tests/run-wayland-remote.py --attempt <id> --build-directory build/q312-context-amd64 \
  --config plan/ws014/tests/config-wayland-context-timeout-amd64.mk --fault-test context-timeout|producer-stop   # delay pair
```

`--skip-build`は既存生成物を記録して再利用する。各試験は独立した使い捨てVMで実行し、実際のargvとhashは各`plan/ws014/temp/remote/<attempt>/result.json`に保存されている。

## 現在の制限

stock virglrenderer1.1.0のcallbackには、OOMやproxy断を実GPU停止と区別できない経路があり、必要なOPAQUE共有契約も不足する。strict pairを維持する。stockの通常描画が常に壊れる、またはDEVICE_LOST時のfence SUCCESSがVulkan違反だとは主張しない。

実行期限60秒は運用既定値であり、正当な長時間computeにも適用される。停止能力を持たないbackend、停止ACKを確認できない場合、共有transportの故障では全体隔離・checked resetへ進む。任意GPU仕事を安全に強制cancelする実装ではない。

直接/Wayland表示は通常BLOB scanoutとGPU内共有を維持する。一般Wayland/toolkit、任意GPU間のDMA、native i915、CTSはこのPhaseの受入ではない。HAL、host system package、GDM、VFIOは変更対象にしない。
