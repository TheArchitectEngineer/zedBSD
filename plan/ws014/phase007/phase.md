<!-- awesome-plan project=zedbsd record=ws014-p007 -->

# WS014 p007: GPUレビュー対応・BLOB直接表示・同期と転送性能の改善

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: q310 finished / q310-i01 cleared
Execution: whole p007 accepted; implementation and finite runtime verification complete
Dependencies: cleared ws014-p006; accepted p002/p003/p005 and WS030 outputs
Next: ws014-p004 planning, not queued; native i915 remains separate WS029
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p007`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4

## 目標とユーザー指示

i915着手前に、現行GPUスタックの実表示をGPU内のBLOB経路へ揃え、記録・応答・GPU完了・表示の同期を区別しながら、転送回数と不要な直列待機を減らす。ユーザー提供のgpu-stack-review.mdは静的レビュー資料であり、推奨案を実仕様と照合して対応する。ユーザーは新Phase作成と実行を明示し、GPU実装・描画経路・同期・性能を対象とした。

追加の明示判断: zwlの「1パス1surface・同期present」と、libwaylandの固定typed listener・未知protocolのdispatcher経由・wl_shm/wl_seat未実装はテストドライバとして問題ない。P9/P10の一般Wayland環境化は本Phaseで実装しない。複数window合成・入力・一般Toolkit対応・常駐化を完了条件へ追加しない。

前回p006はWayland経路でBLOB scanoutを実証したが、VK_KHR_displayと通常vkdemoにはCPU readback/uploadが残っていた。ユーザーの直接表示も自然なBLOB scanoutにする意図への対応不足を本Phaseで訂正する。過去の実測・cleared状態は実際の範囲を示したまま保持し、未実装の改善を過去の達成へ書き換えない。WS030を再利用しない。

## レビューへの処置と実装範囲

| 指摘 | 本Phaseの処置・正しさの境界 |
| --- | --- |
| P1 完了通知 | 既存PCI割り込み/待機基盤を使ってvirtqueueを回収し、sessionの完了sequence照会・待機・poll通知をGPU UAPIへ追加。送信受理、Venus decoder完了、VkQueue timeline通知を区別する。QEMU10.0.11のcontext/ring fenceを検証し、通知後もreply trailerとVkFenceの結果を確認する。通知だけでVK_SUCCESSや描画完了と扱わない |
| P2 vkCmdとreply | エンコード済みvkCmdをcommand bufferに蓄積し、Endまたは有界の完全command境界で送信する。戻り値のないrecord命令はreply不要。既存外部streamで64KiB超を扱い、reset/free/error/secondaryの意味を維持。reply/stream blobは既存mmapで扱い、返答snapshotの寿命とmemory orderingを守る |
| P3 表示中の全GPU排他 | 次refreshの待機をcontroller mutex外へ移し、復帰時にlease/surface/generationを再検証。他sessionのcommandを表示間隔だけ止めない。virtioのnominal pacingを物理vblank保証と呼ばない |
| P4 direct WSI | VK_KHR_displayもGPU copy/blit→linear共有画像→SET_SCANOUT_BLOB/RESOURCE_FLUSHへ移行。通常表示でGPU_RESOURCE_WRITEやCPU画像readbackを行わない。src/dst矩形のcrop/scale/黒余白もGPUで処理し、front allocationを置換・lease終了まで保持する |
| P5 非同期present | 呼出元の配列/pResultsに依存しない所有jobを保持し、QueuePresent内でwait semaphoreを消費するGPU submitの順序を確定した後に返す。workerは実GPU完了を待ってnative present。画像別のin-flight、acquire/release、idle/destroy/切断、非同期エラーを扱う。既存Wayland v1のzwl fenceはproducerのGPU順序保証に流用しない |
| P6 同じopenの並行操作 | 衝突だけをEBUSYにせずsleep可能な排他で待つ。通知/完了waitは同じopenのsubmitを止めるlockを保持しない。VulkanのVkQueue外部同期要件は維持し、独立queue/fence/複数processの有効な並行利用を確認する |
| P7 共有の限定 | 同一GPUのallocation共有をbuffer/optimal imageへ拡張できるmetadataと所有権に整理し、現実のVenus importで検証する。scanout適合性とallocation共有可能性を分離する。GPU_BLOB_CROSS_DEVICEはrenderer側export属性であり、guest別GPU import保証ではない。別GPUはEXDEVを維持し、write-combining等の新HAL変更は未承認・対象外 |
| P8 transport直列化・故障 | 割り込み駆動の有界in-flightとcommandごとの完了管理へ改め、待機中にcontroller全体を占有しない。timeout/device loss時は未確定DMAを解放せず、待機者へ終端エラーを伝える。安全なdevice resetと旧context/handleの無効化・再openを設計し、回復条件と実測限界を明示する |
| P9 zwl | ユーザー承認のテストドライバ制約として現状維持。front保持と一つ遅れるreleaseは表示中画像を上書きしないために必要 |
| P10 libwayland | ユーザー承認のテスト用subsetとして現状維持。汎用SDK・typed callback ABI・object index最適化を追加しない。未知protocolの全互換を主張しない |
| P11 demo/診断 | 通常vkdemoからreadback/hashを除き、検証・画像出力/offscreen等の明示用途だけ有効化。独立venus-frame codecはtransport診断として保持。gpu-share-testの実sourceをpackage内に置き、planファイルへのbuild依存を解消。wltestの検証用逐次待機は維持し、別の有限試験で並行性を測る |

付随所見は同じ対応台帳に記録する。capset256Bは選択Venusには足りており、virgl全体への拡張は行わない。公開Vulkan1.0/内部host1.1の区別、宣言formatとFIFO/MAILBOXの境界を保持し、必要な実能力だけを宣言する。「最短必ず1ms」「zwlのfenceで別contextのproducerも完了」「CROSS_DEVICEで任意GPU共有」は根拠にしない。

## 実装境界と段取り

GPU共通UAPI/driver ops、Venus transport/display/share、libvulkan context/commands/sync/WSI、vkdemoと有限検証プログラムが対象。既存drv_gpu_opsの通常の動的登録、fd/handleの参照所有権、userlandの標準Vulkan API境界を維持する。kernel object fdをLinux dma-buf/DRM ABIへ置き換えない。

一つのPhase/Queue項目の中で、transport/notification、command batch/map、direct BLOB/async WSIを独立に進め、contractを合わせて統合する。一般Wayland環境・EGL/GLES・native i915・VFIO操作・ホスト表示停止は含めない。HALが必要になれば既存許可を流用せず、具体差分を提示して承認済み部分だけ進める。

## 完了条件と有限検証

1. decoder/queue/scanoutそれぞれの通知境界を固定し、通知の前後競合・失敗・timeoutで誤った成功を返さない。irq/wait/pollと同一open/複数sessionの並行性、in-flight資源寿命・回復時の世代分離を実コードfixtureで確認する。
2. vkCmd記録の往復数が減り、通常reply/streamにtransfer ioctlを使わない。大きなcommand stream、reset/free/失敗、reader snapshotを限定fixtureと実Venusで確認する。回数と時間を実測し、根拠のない速度倍率を付けない。
3. 標準vkdemoの直接表示と既存Wayland表示でGPU allocation→BLOB scanoutを実際に使う。画像検証用readbackと通常表示を区別し、通常vkdemoにCPU画像往復がない証拠を残す。矩形合成・FIFO/MAILBOX・acquire/release・非同期presentの寿命も確認する。
4. 同一GPUのbuffer/optimal allocation共有と独立contextの利用を実測し、linear scanout適合性・他GPU拒否・producer終了後の参照寿命を維持する。
5. 既存private awe@10.0.10.25のQEMU10.0.11/virglrenderer1.1.0/Intel ANV環境を確認して使用。直接表示/Waylandの画面をVNCと独立期待値で照合し、終了/再open/console復帰と変更影響の並行試験を通す。source/image/環境hash・command・失敗履歴・実測限界を保存する。
6. 対象make -j16、影響する限定host/ABI/sanitizer試験、Noct再生成、git diff --checkと適用C規約全文を確認。P1–P11/付随所見の処置・実装・証拠・残る制約を一覧にし、最終APIをp004へ引き渡す。

Queue q310は単一項目q310-i01、720 active minutes見積、120分ごとに結果と残件を確認する。個別fixture120秒、build/転送1200秒、VM180秒を基本上限とし、変更理由を記録。同一条件無変更retryは3回まで。未達を合格へ言い換えず、判断が必要なら具体的事実を提示し独立作業を続ける。

## 規約と同期

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)、plan/coding-style.md全文、plan/master-design-policy.md、plan/standards/automation.mdを適用。Noct以外の恒久generator追加、aggregate make check、.internal参照、git add/commit/pushを行わない。private serverへのimage/source転送は既存明示承認を使用する。

開始時に最新Issue本文・コメント・lifecycle・Projectを照合し、Phase/WS/Master/Queue/Guardrail/Past Logとp004依存をGitHubへ同期・readbackしてから実装。途中の重要判断と最終結果も同期する。GitHub Issues/Projectへの公開とrepositoryのgit公開は別であり、source/docのadd/commit/pushはユーザー担当。

開始baseline: ea32e2367589ad1349f3ae08a6265586d28d6a1d（Add Wayland）。実装・試験成功はまだ記録していない。

## 追補B/C/E/F/Gの採用（2026-09-13ユーザー追加指示）

ユーザーはgpu-stack-review2.md末尾のfence handle fd、Khronos標準OPAQUE_FD、描画/表示deviceの組、importによる経路判定とUAPI表を重視するよう指示した。q310-i01/p007の同じ目標に次を追加する。zwl/libwaylandを一般Wayland環境へ広げないという直前の判断は維持する。元reviewと実装途中のscope/結果は履歴として保存し、追加機能が実装済みであるとは扱わない。

| 追補 | 今回の契約・検証 |
| --- | --- |
| B fence fd | KERNEL_HANDLE_FENCEの参照型payload、dup/close/SCM_RIGHTS、POLLIN=signaled、失敗状態、reset/世代とpending使用中の寿命を定義。command/presentのwait/signal fenceを受け付け、CPUの呼出元を描画完了まで待たせず、実GPU完了とnative表示完了を別に扱う。Venus timeline通知だけを描画成功のsignalとせず、実VkResultを確かめる。標準VK_KHR_external_fence_fdのOPAQUE_FDとしてexport/import、temporary/permanent payload、resetを検証する |
| C 標準memory fd | VK_KHR_external_memory_fdのOPAQUE_FDとしてkernel allocation handleを公開。必要なcapability問い合わせ・拡張列挙・pNext・dispatch/ABIと一緒に実装する。export元allocationのsize/type/device/driver互換を検証し、import成功時のみ入力fdを消費する。buffer/optimal imageと別processの標準APIテストを追加する。内部のVenus MESA importはbackend実装として残り、アプリAPIには出さない |
| E 描画/表示の組 | 1controller=1/dev/gpuNを維持し、描画能力・表示能力・安定identity・companion hintを照会できるようにする。libvulkanが描画VkPhysicalDeviceへ表示node群を関連付け、VkDisplay/plane/surfaceに出力先を固定。異なる描画VkPhysicalDevice間のOPAQUE_FD共有とは区別する。明示hintを優先し、孤立表示nodeの既定則と曖昧時の動作を文書化・fixture検証する |
| F importとfallback | 表示driverのoptional scanout import opがbackingの到達性・境界・layoutを検証。K coreが他driverのresource IDをそのまま流用しない。scanout目的のlinear外部importだけを許し、非対応はEOPNOTSUPP等で拒否する。制約を照会し、libvulkanがswapchain作成時にGPU共有経路か明示CPU fallbackかを一度決定。通常の現在のVenus組合せはBLOB経路を実受入条件にする |
| G UAPI表 | fence create/query/wait/resetとfd所有権、command/present wait/signal、GPU poll完了/topology通知、capability/identity/companion、scanout専用foreign import、制約、配置要求、allocation exportを一つの表へ整理。version/size/flags、必須/任意ops、未対応errno、寿命とU/K責務を示す。対応していない物理連続性やDMA/cache機能をflagだけで広告しない |

### 公式仕様に基づく修正

- OPAQUE_FD memory/fenceはPOSIX fdで参照を保持しSCM_RIGHTSで運べる。Linux dma-bufを要求する型ではない。DMA_BUFと、Linux Sync File/Android Fenceを指定するSYNC_FDは別のhandle typeであり、今回はguestで採用しない。
- OPAQUE_FDにはdeviceUUIDとdriverUUIDの一致条件がある。異なる描画GPUのimportを常に成功させる、というreviewの記述は採用しない。Kの表示専用foreign importも実backingの互換性・到達性をdriverが検証した場合に限る。失敗したimportを暗黙CPU copyの成功と偽装しない。
- 汎用handleにbacking pointerを一つ追加するだけでIOMMU/cache/foreign DMAが成立するとは扱わない。選択driverが供給できるbacking契約と保持期間を明示し、未知のbackingは拒否する。
- 標準external fenceのOPAQUE_FDは参照共有・reset可能なpayloadであり、single-shot通知sequenceをfdに包むだけでは満たさない。temporary importの復帰と複数aliasのpayload共有も検証する。

公式資料: [memory handle types](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryHandleTypeFlagBits.html)、[fence handle types](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalFenceHandleTypeFlagBits.html)、[memory import ownership](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html)、[fence import semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportFenceFdInfoKHR.html)。固定header/rendererとの対応は実装時に照合する。

### 追加受入と境界

標準memory/fence fdを独立processへ渡し、生成元のclose/終了、GPU signal/wait、import失敗時のfd保持、成功時の消費、payload reset/temporary復帰、pending中の参照を限定fixtureと実Venusで確認する。公開header・拡張依存・dispatch・問い合わせと実機能を一致させ、形式上の成功stubを作らない。

描画と表示の別node・制約・配置拒否・scanout import・一度だけのfallback判断は表示専用backendを用いる実コードfixtureで検証する。実QEMUでは現在のVenus/同一GPUのBLOB表示と標準外部共有を受け入れる。新しい物理display driver、native i915、他GPU実機/IOMMU受入、HAL cache変更を追加で許可されたと解釈しない。一般コンポジタはGPU合成と標準WSIを使う責務とし、今のzwlは同期テストドライバのままでよい。必要なfence転送のprotocol拡張はversionを交渉し、旧versionの意味を壊さない。

VK_KHR_external_semaphore_fd、VK_EXT_display_controlの全API、一般Wayland/Toolkit拡張は本追加で自動的に実装対象へ増やさない。必要なK同期/topology通知基盤を準備し、実装と能力を確認した拡張のみ公開する。未達や実測できない互換性は残件として明記し、標準に適合したと過大に主張しない。

追加指示を含むp007の全scopeをq310-i01へ反映し、開始時720 active minutes見積・120分レビューと有限command上限を維持。現在の独立したP1–P8/P11改善を続け、新規契約に依存する変更はこの追補のGitHub同期・readback後に進める。追加HAL/git公開の承認条件は変更しない。

## q310 checkpoint 01: 実QEMUで並行操作・標準fd共有を確認（2026-09-13）

[WS014 p007](https://github.com/awemorris/zedBSD/issues/394) / q310-i01 は実装・検証を継続中。p007は未完了であり、p004/native i915には進んでいない。

- 実QEMU `q310-wayland-005` で同一GPU fdの4 pthread×32回（128 allocation lifecycle）の書込/読出全4096 byte照合がPASS。
- 別processの標準Vulkan OPAQUE_FD fenceのexport/import/reset・実GPU signal・参照寿命がPASS。linear共有と標準buffer OPAQUE_FDのproducer終了後import/GPU copy/pixel検証もPASS。
- 同じ実行はoptimal image capability queryでVK_ERROR_FORMAT_NOT_SUPPORTEDとなりFAIL。対応が必要なnative dedicated allocation条件を確認し、必要なVK_KHR_get_memory_requirements2 / VK_KHR_dedicated_allocationを標準APIで補う。対応型の能力を偽って成功扱いにしない。Vulkan coreは1.0を維持する。
- 限定fixtureの通常版/ASan・UBSanではtyped fence/poll/SCM_RIGHTS、signal fd close/reuse競合、K allocation rollback・元所有者終了後の保持、display-only foreign backingのDMA/cache拒否と最後の解放、非同期presentの所有権、作成時に一度だけのfallback選択、command batching、mapped reply、IRQ out-of-order/制御用slot確保を確認した。fixtureと実GPUの証拠は区別する。
- 実試験で見つかったQUERY出力欄をRESET/WAIT入力へ再利用する不具合を修正。次に露呈したAMD64 pthread初期SPのC ABI不一致はlibcで修正し、005で例外が消えた。HAL変更は行っていない。
- 失敗履歴001（旧診断不足）、002（harnessで未対応の`;`を入力）、003（RESET EINVAL）、004（workerのmovapsでstack alignment例外）、005（上記optimal profile拒否）を保存。失敗を過去の成功へ書き換えない。

残件はoptimal allocationの標準API受入、direct BLOB/Waylandの画面・寿命・性能検証、故障/回復の有限試験、最終API/規約/宣言再生成確認。zwlとlibwaylandの承認済みテストドライバ制約は維持する。資料・コードは作業treeにありgit add/commit/pushはユーザー担当。GitHub Issues/Projectの同期とrepository公開を混同しない。

ローカル証拠: `plan/ws014/temp/remote/q310-wayland-001`〜`005`（実行ごとのsource/image hash、build/transfer/guest/renderer log）。UAPI対応表は `plan/ws014/phase007/gpu-uapi-contract.md`。同表は下記Phase本文にも掲載する。720 active minutes見積・120分レビューを維持し、今回のcheckpoint後もq310を継続する。


詳細な要求/所有権表: [gpu-uapi-contract.md](gpu-uapi-contract.md)。

## q310: optimal共有の原因を実測で訂正（checkpoint 01後）

実ホストのIntel ANVへ同じRGBA8 optimal / TRANSFER_SRC|TRANSFER_DSTの問い合わせを行った。native DMA_BUFはVK_ERROR_FORMAT_NOT_SUPPORTED、native OPAQUE_FDは成功（import/export、DEDICATED_ONLYなし）。専用割当て不足というcheckpoint 01時点の仮説は該当しない。不要なVK_KHR_dedicated_allocation/get_memory_requirements2の追加は取り下げ、公開APIは170関数のまま維持する。

固定virglrenderer 1.1.0のvkr_device_memoryはOPAQUE資源を扱える一方、proxy_contextの別context attachがOPAQUEを明示拒否する。guestでDMA_BUFへ一律変換する現経路ではoptimalの標準共有を満たせない。host側の正確な不足を補う最小差分を独立資料として準備し、native OPAQUE backingとguest kernel handle fdの対応を検証する。ホストのinstalled libraryやsystem構成はまだ変更していない。

実QEMU005で確認済みの128回並行fd操作・標準fence・buffer共有は有効な証拠として保持する。optimal受入は未達のまま。direct BLOB/Wayland受入は独立に継続する。q310/p007はactive、HAL追加変更・git add/commit/pushなし。


詳細な要求/所有権表: [gpu-uapi-contract.md](gpu-uapi-contract.md)。

## q310 checkpoint 02: BLOB表示・標準OPAQUE共有・回復の実受入

q310/p007はactiveのまま、最後の表示topology通知と統合確認を進める。i915/p004は未着手。

| 実QEMU attempt | 実測した結果 |
| --- | --- |
| q310-wayland-006 | 同一fdの4 pthread×32 allocation lifecycle、標準external fence、linear/buffer/optimal allocationの生成元終了後importと1024 pixel GPU照合がPASS。Wayland FIFO/MAILBOX各6画面の独立oracle、client abort/reopen、compositor SIGKILL後のsurface loss/reopen/console復帰もPASS（46.651秒） |
| q310-direct-002 | 標準Vulkan直接表示6画面を独立oracleで照合。通常vkdemoはreadback用storageなし、1910msまで13 frameをsubmitし、異なる2枚の実VNC画像で動きを確認。SIGINT、再open、表示lease競合、console復帰PASS（41.670秒） |
| q310-fence-exit-001 | pending fenceのproducer終了を別processが成功と誤認せずerrorとして観測し、終了処理PASS（15.076秒） |
| q310-recovery-002 | このVMだけのrenderer子processをpidfdで一時停止し、decoder待ちが実guest時間10000msでETIMEDOUT。別openのPOLLERR/旧資源ENODEV、旧参照中の再open拒否を確認。全旧参照retire後にrendererを再開、checked reset、新contextで4096 byte往復とdecoder完了PASS（13.909秒） |

recovery-001では未登録timelineへの要求がホストで即時成功し、想定した停止を作れなかったためFAILとして保存した。上記002はrenderer停止/再開の明示handshakeに変更した試験。direct-001は6画像/通常描画成功後、harnessのSIGINT後marker判定が古くFAIL。002で判定を修正し全受入した。失敗履歴は書き換えない。

### native OPAQUE用の隔離renderer

stock virglrenderer1.1.0 proxyは別contextのnative OPAQUE attachmentを拒否するため、その不足だけを補うpaired library/serverを私有dependency directoryへ作成。host system libraryやpackageは変更していない。native allocationのsize/type/deviceUUID/driverUUIDを保持してimport時に照合し、既存DMA経路を保持。標準guest memory/fenceの型はOPAQUE_FDのままで、guestへdma-bufを導入していない。

168-byte capsetのexact magic 0x5a424453/flags=1とpaired INIT ACKが一致した場合だけnative OPAQUEを選ぶ。stockでは従来のnative DMA対応subset、private WSIは常にDMA/CROSS_DEVICEを使う。INITのwrong magic/flags/stock-size負例も確認。実QEMUは/proc mapsで選択libを確認し、前後hash一致を検証した。

- patch SHA256: 04def7cd3fd9e50fad62ff98298a9e34880ce793de4b5942d5a908dd0132379f
- library SHA256: 3f692e604f7653153b6b7340afa0f97e65ee78956babe004eb7d0b8ead8a6a26
- server SHA256: c9383cef62253c995cb067d7e1eb11c29d801bdd3514fa5d0dcbdb26e9056f0d

patch/固定元license/導入手順/単体・実受入証拠はplan/ws014/phase007/renderer-opaque/へ保存。stock 1.1.0だけでoptimal共有が通ると報告しない。

### 残る確認

Noct生成8fileは一致。170公開command/145native opcode、ILP32/LP64 ABI（148type/942field/2394constant）、DSO dispatch等13限定jobと通常/ASan・UBSan版はPASS（userland-final-verification.md/json）。command fixtureは44個の独立数値wire、64KiB update payload×3の保持とEndでの一括送信、reset/失敗を検証した。実direct全testのtransport累計は3262 submitted/completed、3100 IRQ、702 sleeps。これは個別frameのGPU実行時間ではなく、6画面診断や終了・競合試験を含む累計である。通常13frame/約2秒という現在の速度上限を残し、根拠のない倍率改善を主張しない。

最終契約監査でGPU fd pollのcommand完了とfaultは実装済みだがtopology通知が不足していることを確認した。driver sequence、POLLPRI、copyout成功後だけのexact ACKを加え、複数openと通知/ACK競合を限定fixtureで確認する。VK_EXT_display_controlのAPI全体や一般Wayland化は追加しない。追加HAL変更なし、git add/commit/pushはユーザー担当。720 active minutes見積と120分レビューは維持する。


詳細な要求/所有権表: [gpu-uapi-contract.md](gpu-uapi-contract.md)。

## q310完了: BLOB直接表示・標準fd共有・GPU同期改善（2026-09-13）

WS014 p007 / q310-i01をcleared、q310をfinishedとする。active Queueなし。WS014はincompleteでp001/p004はplanning、p004とWS029 native i915は未queue。p006/q309・WS030の既存clearanceを維持する。

直接VK_KHR_displayもGPU copy/blit→共有linear画像→SET_SCANOUT_BLOBへ移行し、通常vkdemoのCPU画像readback/uploadを除いた。IRQ完了通知、vkCmd batchingとmapped reply、表示待ち中のcontroller排他短縮、所有jobによる非同期present、同一openの待機admission、安全なtransport回復を実装した。

review2 B/C/E/F/Gも反映: KERNEL_HANDLE_FENCE/POLLINとcommand/present wait/signal、標準VK_KHR_external_memory_fdとexternal_fence_fdのOPAQUE_FD、描画node＋表示nodeの組、driverによるscanout import判定とswapchain作成時に一度だけの経路選択。GPU_DISPLAY_EVENTS/POLLPRIのQUERY→列挙→exact ACKと、GPU_BLOB_CREATE_PLACEDによる物理配置要求を追加した。GPU ABI v1の旧要求layoutを保ち、内部drv_gpu_opsはv6。実backingで満たせない配置はENOTSUP、OOMやdevice lossはfallbackで隠さない。

OPAQUE_FDはguestにdma-buf/SYNC_FDを要求しない。標準memory共有は同じdeviceUUID/driverUUIDの互換範囲、別GPUの任意importは未対応。表示専用foreign import・DMA/cache/placement・別node組合せは実コードfixtureによる検証であり、異種実機DMAや新HAL allocatorの受入ではない。Venusは非零physical placementを拒否する。

| 最終実QEMU | 結果 |
| --- | --- |
| q310-wayland-008 | PASS、46.531秒、QEMU exit0。128回同一fd並行操作、標準fence・linear/buffer/optimal共有、初期topology QUERY/ACK、FIFO/MAILBOX各6画面・異常終了/reopen/console |
| q310-direct-004 | PASS、41.83秒、QEMU exit0。直接表示6画面の独立oracle、通常readback0の動く2実画像、QEMU BLOB trace/対象寸法のlegacy経路なし、SIGINT/reopen/lease競合/console |
| q310-fence-exit-003 | PASS、4.424秒、QEMU exit0。pending producer終了後DEVICE_LOST、独立30秒fault期限と実測、最終waitpid |
| q310-recovery-004 | PASS、13.807秒、QEMU exit0。所有rendererだけを停止、10000ms timeoutとpeer error/旧参照gate、再開後checked resetと新contextの4096byte/decoder |

最終buildと限定K/U/driver/WSI fixtureの通常・ASan/UBSan、170公開APIのdispatch/両ABI、Noct生成8file一致を確認した。通常デモは約2秒で13frame程度という実測を残し、速度倍率や物理vblank保証・CTS適合を主張しない。zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ制約として維持する。

optimal共有はstock virglrenderer1.1.0 proxyのOPAQUE attach不足を補ったisolated paired library/serverで受入した。exact168B capsetとINIT handshakeで合意した場合だけ選択し、private WSIのnative DMA経路を保持。system library/packageは変更していない。patch/library/server hashと手順はlocal/uncommitted plan/ws014/phase007/renderer-opaque/に保存した。stockだけでoptimal共有が通るとは扱わない。

失敗履歴wayland001–005、direct001、recovery001、fence-exit002を保存。fence-exit002はconsumer/driver双方10秒の期限競合でVK_TIMEOUTが先行した。fault専用期限を30秒に分け、final closeはpending fenceをerrorへしてからcallback drainを待つ順序へ改善した。修正前FAIL・修正後normal/sanitizer PASSの因果fixtureも保存し、実行中ioctl/file参照によるfinal close入口までの遅延とは区別する。

資料はlocal/uncommitted plan/ws014/phase007/results.md、gpu-uapi-contract.md、display-wsi.md、transport-sync.md、各verification JSONとQueue履歴plan/history/queue-q310.md。新HAL変更・GDM/VFIO操作・git add/commit/pushなし。GitHub Issues/Projectへの計画/受入同期と、ユーザー担当のrepository公開を区別する。
