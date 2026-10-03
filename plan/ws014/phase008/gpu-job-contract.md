# GPU JOB とドライバ所有 fence の契約

WS014 p008 / q311 の共通GPUコア、fence payload、Venus接続の実装記録。Phaseの進捗・実QEMUの最終受入は [phase.md](phase.md) に従う。

## 所属と型の境界

- 実装は [src/drivers/gpu/gpu-fence.c](../../../src/drivers/gpu/gpu-fence.c)、公開するK内部宣言は [include/drivers/gpu-fence.h](../../../include/drivers/gpu-fence.h)。K内部名は `drv_gpu_fence_*`。既存の `GPU_FENCE_*` ioctl、fdのユーザー向け意味は維持する。
- `src/kern/handle.c` とfd/SCM_RIGHTSは、不透明object、refcount、release、任意のpoll callbackだけを扱う。汎用の分類は `KERNEL_HANDLE_DRIVER`。GPU image/allocationとfenceをkernのenumに列挙しない。
- `KERNEL_HANDLE_DRIVER` が同じでも、各ドライバ機能は自身の `kernel_handle_ops` の同一性を照合してからpayloadを読む。異なるGPU allocationとfenceは相互に解釈できない。GPUの不変identityも照合し、異なるデバイスのfence state操作を拒否する。
- payloadのfd、SCM_RIGHTSメッセージ、CPU waiter、producer bindingはそれぞれ独立した強参照を持つ。最後の参照がreleaseを呼ぶ。共有payloadはproducerのGPU open自体を共有しない。

## GPU JOB UAPI

新規の [include/uapi/gpu-job.h](../../../include/uapi/gpu-job.h) と `GPU_CAP_JOB`（8192）により、native submit前からKが完了責任を持つ。driver interfaceはversion 7で、通常の `drv_gpu_ops` に任意の `drv_gpu_job_ops` が追加される。

| 操作 | ABI | 意味 |
|---|---|---|
| `GPU_JOB_RESERVE` | G/34、40 bytes、入出力 | originating GPU openにcompletion record、任意fenceのexact generation、backend marker storageを予約する。まだnative markerは投稿しない。 |
| `GPU_JOB_COMMIT` | G/35、24 bytes、入力 | native submit成功の後に、同じ予約済みmarkerを追加allocationなしで投稿する。 |
| `GPU_JOB_CANCEL` / flags=0 | G/36、24 bytes、入力 | native仕事が確実に未受理の場合だけ予約を戻す。payloadは同じgenerationのPENDING/unboundを維持し、sequence recordは取り消す。 |
| `GPU_JOB_CANCEL` / `GPU_JOB_CANCEL_FAULT` | 同上、flags=1 | native受理が不明、またはdevice lossの場合。通常rollbackせず、backendのfailure/quarantine経路へ渡す。 |
| `GPU_COMMAND_WAIT` | 既存ABI | 正常commit、またはfaultしたsequenceの終端statusを観測・CONSUMEする。timeout/EAGAINは仕事を取り消さない。 |

`gpu_job_reserve` は `version/size/fd/flags/generation/timeline/reserved/sequence`。fd=-1ならgeneration=0で、共有payloadなしのsequence-only jobとなる。fdを指定する場合はnonzero generationが必要。timelineは1〜63。0はdecoder通知用なのでGPU成功の根拠として使えない。flags/reserved/出力sequenceは初期値0。

`gpu_job_action` は `version/size/sequence/flags/reserved`。COMMITはflags=0のみ。sequenceはglobally uniqueかつoriginating open固有であり、別openは操作できない。全fieldは固定幅で、ILP32/LP64のサイズ、offset、ioctl値を [gpu-uapi-layout.c](../tests/gpu-uapi-layout.c) で照合している。

## 予約から終端まで

1. Uはnative fenceとlocal bookkeepingを準備してRESERVEする。共有fdを指定する場合、Kはdescriptor lookupで強参照を取得し、exact generationをproducerへbindする。K completionのobserver pinは、setupの途中に同じfdから別threadがlookup/consumeしてもslotが再利用されることを防ぐ。
2. Kはsequenceのcopyoutを済ませてからbackend reserveを呼ぶ。copyout失敗ならbackendにcallbackを渡さない。backendが予約を拒否した場合もcallbackは残らず、bindingとrecordを撤回する。失敗したioctlが書き戻したsequenceは有効な予約ではない。
3. 成功したRESERVEの時点で、Venusは10秒のproducer監督を開始する。native submit前、native reply後〜COMMIT前にUが停止した場合もこの期限が進む。予約中のcallbackはERRORになれるが、GPU成功にはできない。
4. Uはnative `VkResult` を確認する。成功ならCOMMIT、確実な未受理OOMならCANCEL0、不明・device lossならFAULT。通常submit、空submit、複数submit、sparse submitに同じ所有権規約を適用する。アプリがfenceを渡さない場合も、Uはprivate native fenceとsequence-only jobを用いる。
5. COMMITはpayloadをadmittedへ変更してから、用意済みmarkerを投稿する。これにより即時完了でもexact generationの成功が失われない。actionはbackend呼出し全体でcompletionのobserver pinを保持し、並行CONSUMEによるpointer再利用を防ぐ。
6. `drv_gpu_complete()` はsequenceの終端と、そのjobにbindされたpayload generationの終端を同じregistry critical sectionで公開する。bindingを切り離した後、強参照のputはlock外で行う。古いgeneration、重複callback、最終close後の遅い成功はERRORを成功へ戻せない。

COMMITの通常経路には追加allocationや空きslot取得がない。backendのreserveが容量不足を返すのはnative仕事を出す前である。COMMIT失敗はnative仕事が既に存在する可能性があるためFAULTへ渡し、確定未受理のrollbackとして扱わない。reserved/posted tokenとexpected completion pointerを同時に照合し、古いtokenから再利用済みslotを操作させない。

CANCEL0成功後の新しいWAIT lookupはENOENT。取り消し前にrecordを取得していたobserverはECANCELEDを観測でき、参照を離すまでslotは再利用されない。FAULTで終端したrecordはWAIT/CONSUMEのために残る。jobに属するbindingへUが直接 `GPU_FENCE_SIGNAL` や旧BIND_RELEASEを行うことは拒否する。取得済みimageなどの同期的なU完了は既存の独立した経路を使う。

## 完了を証明する境界

Venusのused-ring通知、INTx/MSI-Xの割り込み、timeline 0のdecoder返信、native `VkResult`、native GPU fence成功は別の事象である。通常command通知の成功だけでは共有fenceを成功にしない。

p008のstrict paired rendererは、native `vkQueueSubmit` / `vkQueueBindSparse` に渡した実際のnon-NULL `VkFence` の成功をqueue markerへ結び付ける。空の追加submitをsparse処理の完了証明として使わない。host failure、切断、失敗待機を正常retireへ変換せず、失敗contextはsticky化し、K側のfault/watchdogでERRORへ到達させる。詳細は [renderer-strict/](renderer-strict/) の資料とrootの実QEMU証拠を参照する。

capsetは168 bytes、offset160の既存vendor magic `0x5a424453` とoffset164のflags=3（OPAQUE=1、STRICT_QUEUE=2）を照合する。新libvulkanはstrict能力をdevice初期化時に必須とする。stock/旧hostではlegacy 2Dやcapability queryを残すが、このlibvulkanのGPU JOB成功を広告しない。

## K待機、最終close、復旧

CPUの `GPU_FENCE_WAIT` とfd pollは未投入のPENDINGも観測でき、明示したcaller timeoutを守る。対してGPU command/displayのK内dependency待機は、PENDINGが**COMMIT済みのadmitted job**に属する場合だけsleepする。unbound、旧U-only binding、RESERVEDのPENDINGはEAGAINで拒否し、Uの将来のsignalを無期限に待つ依存を作らない。SIGNALED/ERRORはその終端状態を返す。

最終GPU open closeは、payloadのproducer喪失をERRORとして先に公開してからbackend drainに入る。session、resource、completion storageはdrain終了まで保持する。dup、mappingや進行中syscallがfile参照を持つ間はまだ最終closeではなく、process cleanupのthread待ちもこの順序とは別である。

ローカルな表示不在・不適切な要求などのerrnoから、共通GPUコアが自動でdevice-wide failureへ昇格させる処理は廃止した。真のtransport failure、protocol破損、不確定なcleanup、監督期限切れはbackendが `drv_venus_transport_fail()` を明示し、callbackが0件でも登録GPUの状態へ伝える。

`drv_gpu_retain/release` はGPU wrapperを保持するAPIであり、hardware/backend寿命そのものではない。transportは公開時のwrapper参照を持ち、queue lock下でfault報告用の追加参照を取得し、lock外でreport/releaseする。unregisterまたはchecked stopは公開参照を切り、fresh recoveryで改めてretainする。

native受理が不明な予約期限切れもtransport全体をfailed/quarantineへ移す。ERROR公開はDMA解放の許可ではない。全ての旧session、mapping、shared allocation参照とactive callbackが退役した場合だけfresh openがchecked resetを行い、backingを解放・再初期化できる。古いVkDeviceやVkFenceを復活させる契約ではない。

## 容量とビルド選択

Venusは初期64 descriptor / 32 request slotを目標とし、deviceのqueue上限に合わせて下げる。32 slot構成ではqueue marker用28、通常control/decoderのための容量4を確保する。容量不足でnative未受理のRESERVEをEAGAIN拒否しても、将来のdecoderコマンドを投稿できる容量を残す。

Makefileの `KERN_GPU_BACKENDS` は現時点で `CONFIG_DRIVER_PCI_VENUS` を含む。GPU backendがyの場合だけ `gpu.c` と `gpu-fence.c` を `KERN_GPU_SOURCES` / `KERN_GPU_OBJS` へ入れる。amd64、arm64、pcat、pc98、sparcv9、x68kの各vmunixリストが同じ組を使う。将来GPU backendを追加した際はこの条件にも追加する。generic handle/fd-objectは常にkernに残る。

[run-gpu-build-selection-test.py](../tests/run-gpu-build-selection-test.py) は6構成それぞれVenus=n/yの実make展開を検査する。実binaryのGPU除外も、専用 `build/q311-nogpu-amd64` のamd64 kernelを `make -j16 vmunix` で構築して確認した。通常のamd64 vmunix検査はPASS。`llvm-nm --defined-only` に `drv_gpu_*`、`drv_venus_*`、`gpu_*`、`venus_*`、旧 `kernel_fence_*` は0件で、GPU objectも0件だった。`handle_get/put`、`fd_object_get/put`、`filedesc_commit_objects` はELFに残る。kernel/configのhashとsymbol抜粋は [nogpu-amd64.json](gpu-core-verification/nogpu-amd64.json) に保存した。全6アーキテクチャの実binary buildを行ったという意味ではない。

## 検証と限界

[保存した検証記録](gpu-core-verification/verification.json) と同directoryのsmall logが、通常・ASan/UBSan、ABI、make入力検証の結果を持つ。主なfixtureは以下の通り。

- [gpu-job.c](../tests/gpu-job.c): copyout rollback、予約拒否、無割当commit、origin/exact generation、即時完了とCONSUME、局所ENODEV/ETIMEDOUTの非汚染、明示fault、close/drainと遅い成功、wrapper参照。
- [gpu-fence-payload.c](../tests/gpu-fence-payload.c): driver所有payloadを実generic fd/SCM_RIGHTS経由で転送し、reset、producer authority、timeout、level pollを確認。
- [gpu-fence-reuse.c](../tests/gpu-fence-reuse.c): 実admitted jobをprerequisiteにして、K待機中のsignal fd close/reuseでも取得済み強参照が維持されることを確認。
- [gpu-fence-close.c](../tests/gpu-fence-close.c): 最終file参照、drain入口のERROR、session/resource/callback保持、遅い成功での上書き拒否。

これらは実GPU/cdev/fd/fenceコアをリンクするが、host completionとschedulerの進行は有限のpeerで与える。rendererの実VkFence意味論、物理GPUの障害、実kernelの並行schedulerをfixtureだけで検証したとは扱わない。実QEMUのproducer停止・退出、timeout recovery、direct/Wayland描画はrootのPhase受入記録と合わせて判断する。
