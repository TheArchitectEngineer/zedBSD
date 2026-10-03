# WS014 p007: GPU共有・同期・描画/表示の境界

q310で追加した最終契約の対応表。実装と有限fixture、実QEMUの受入結果・限界は[results.md](results.md)と[final-verification.json](final-verification.json)に記録した。通常のGPU動的登録を維持し、GPU専用の登録サービスは追加しない。

## 公開要求と所有権

GPU要求は`GPU_ABI_VERSION=1`、固定幅の`version/size`と予約欄のゼロを検証する。新しい任意driver opsを含む内部`DRV_GPU_INTERFACE_VERSION`は6。既存要求の番号・layoutは維持する。要求中のfdは現在processのtableを参照し、Kは依存を待つ前に対象payloadの強参照を取る。

| 要求/入口 | 能力・ops | Kの責務と所有権 | Uの責務・成功の意味 |
| --- | --- | --- | --- |
| `GPU_COMMAND_SUBMIT` (12) | `GPU_CAP_NOTIFICATION`、非同期submit callback | 有界のsession-owned sequenceを発行、request/response/DMAと完了recordを保持。満杯はEAGAIN | 受理は描画成功ではない。decoder用timeline 0とqueue timelineを区別する |
| `GPU_COMMAND_WAIT` (13) | 同上 | exact sequence、timeout、終端errno、`GPU_WAIT_CONSUME`によるretire。copyout失敗でrecordを失わない | 通知後もreply trailerとnative VkResultを検証する |
| GPU fd `poll()` | notification / optional `GPU_CAP_DISPLAY_EVENTS` | 終端commandでPOLLIN、未ACKの表示変更でPOLLPRI、故障でPOLLERR。照会は各状態を消費しない | commandとtopologyのsequenceを区別し、単なる起床をrender完了にしない |
| `GPU_ALLOCATION_EXPORT` (14) | `GPU_CAP_ALLOCATION_SHARE`、share.export | full allocationを保持する独立handle fdを作る。size/device identityを確定、128B以内のU metadataを不変に保持。fd reserve→copyout→commit | buffer/optimal imageを含むallocation契約。metadataは権限を部分領域へ制限するものではなく、scanout適合性も示さない |
| `GPU_ALLOCATION_IMPORT` (15) | 同上、share.import | 同一GPUの別contextにaliasを作る。失敗/copyout失敗は未公開aliasを破棄。入力fdはK ioctlでは消費しない。別GPU renderer importはEXDEV | libvulkanがschema、size、memory type、deviceUUID/driverUUIDを検証。標準import全体の成功時だけfdをclose |
| `GPU_FENCE_CREATE` (16) | `GPU_CAP_FENCE`、共通K fence | `KERNEL_HANDLE_FENCE` payload、generation 1、pendingまたはsignaled。fdとpending workがそれぞれ参照を保持 | OPAQUE_FDの独立payloadを使う。Vulkan native fenceとK payloadを同一オブジェクトだと仮定しない |
| `GPU_FENCE_QUERY` (17) | 同上 | generation 0は現在世代、非0はexact世代。state/errorを返す | stale generationを現在世代への問い合わせで再観測する |
| `GPU_FENCE_WAIT` (18) | 同上 | exact世代をsleepして待つ。timeout、割込み、ESTALE、終端errorを区別。GPU session admissionを保持しない | 別aliasの合法resetでESTALEなら残りtimeout内で再観測する。未完了を成功にしない |
| `GPU_FENCE_RESET` (19) | 同上 | pending producerにbind中はEBUSY。未使用payloadを次世代pendingへ移す。0へのwrapを避ける | QUERY出力欄を入力へ流用しない。temporary payloadはVulkanの規則に従い解放しpermanentを復帰する |
| `GPU_FENCE_SIGNAL` (20) | 同上 | exact世代・producer ownerを検証。errorを含む終端を一度publishしてwake | libvulkanは実native VkFenceの成功を確認してからsignalする。Venus marker通知だけでは成功signalしない |
| `GPU_FENCE_BIND` (21) | 同上 | native submit前にopenとpayloadを関連付ける。`BIND_RELEASE`は未受理workの予約だけを取消す。final close/device lossは未確定payloadをerrorへ | submit失敗なら予約を戻す。受理済みworkの参照をdestroy/resetで消さない |
| `GPU_COMMAND_SUBMIT_SYNC` (22) | notification + fence | wait fdをadmission前に待ち、signal対象を保持してsubmit。transport failureを対応payloadへ伝える | command通知とnative実行成功の二段階を保持。GPU成功確認後のSIGNALが必要 |
| `GPU_DISPLAY_PRESENT_SYNC` (23) | display + fence | 描画waitをhardware access前に待つ。native display selection成功後にsignal。wait失敗ではpresent callbackを呼ばない | render完了fenceとdisplay完了fenceを区別。QEMUのnominal pacingを実機vblank保証と呼ばない |
| fence fd `poll()` | handle.ops.poll | signaledはPOLLIN、失敗終端はPOLLINとPOLLERR。reset後は未完了へ戻る | `dup`/SCM_RIGHTS先も同じpayloadを見る。read/write対象ではない |
| `GPU_DEVICE_QUERY` (30) | optional scanout.query_device | 安定したK device_id、render/display role、optional companion hint。hintはsharing保証ではない | libvulkanがnode群を列挙して組を作る。描画能力のないnodeをVkPhysicalDeviceに偽装しない |
| `GPU_DISPLAY_CONSTRAINTS` (31) | display + scanout.constraints | display generation、packed format、stride/offset alignment、placement、DMA address上限とshared/copy/foreign経路を返す | swapchain作成前に照会。stride/offset alignmentのゼロは追加制限なし（1byte alignment）。generation変化時は旧条件を使用しない |
| `GPU_DISPLAY_EVENTS` (32) | `GPU_CAP_DISPLAY_EVENTS`、任意のdisplay.events | 40Bの非破壊QUERYとexact ACK。各openのobserved/ackを保持し、成功copyout後だけ更新。通常ioctl admissionとcore lockの外で短いdriver snapshotを呼ぶ | QUERY→output/generation全再照会→取得済sequenceのACK。新規openは初回POLLPRI。新しい変更は古いACKで消えない |
| `GPU_BLOB_CREATE_PLACED` (33) | `GPU_CAP_BLOB`、任意のblob_create_placed | 64B。旧40B prefixを保持しplacement条件を追加。零条件は旧allocator、非零でopsなしはENOTSUP。成功は実backingによる全条件成立。callback失敗は自己解放、copyout失敗はcoreがdestroy | WSI共有画像作成時に物理条件を提出。ENOTSUP/ENOTTYだけを非対応としてcopy fallback選択に使い、ENOMEMやdevice lossを隠さない |
| `GPU_RESOURCE_IMPORT` + `GPU_IMPORT_SCANOUT` | 同一deviceは既存share.import_resource。foreignだけsource share.get_scanout_backingと表示側scanout.import_image | 同一deviceは通常の共有alias。foreignはsourceの実page列/cache属性/extentをborrowし、destination driverがformat/pitch/address/cache/DMA互換性を判定。native resourceのdestroy後まで元handleを保持 | 実import結果が経路を決める。native-only resource_id=0をrendererのresource IDへ渡さない |
| placement flags | `DMA32 / CONTIGUOUS / COHERENT`、inclusive max_dma_address、physical base alignment | 具体的な割当要求。driverは実page endpoint/連続性/cache属性/base addressを検証し、成立しなければENOTSUP。新allocatorは要求を無視して成功できない | physical alignmentの0/1は追加制約なし。他は2のべき乗。画像offset/row pitchのalignmentとは別であり、UがDMA到達性を推測しない |

`GPU_DISPLAY_PRESENT_SYNC`の成功とdisplay fenceはscanoutの選択完了を表し、現在表示中のbufferを再利用可能にはしない。BLOB allocationは次の成功した置換、またはchecked releaseまで表示側が保持する。非同期`vkQueuePresentKHR`の`pResults`は呼出し中の受理結果だけを記録し、返却済みの配列をworkerが書き換えない。後から発生した失敗は次のacquire/idle等へ伝える。

Fence fdを保持していても生成元GPU openの未確定workは継続保証されない。producer終了前に成功が確認されたpayloadは独立に残るが、pendingのままfinal closeしたpayloadはerrorになる。これを成功扱いすることはない。descriptor自体の寿命と、実行中producerの生存条件を分ける。

## 標準Vulkanへの対応

| 公開API/拡張 | libvulkanでの実装境界 |
| --- | --- |
| `VK_KHR_external_memory_fd` | guest `OPAQUE_FD`のみ。exportされたallocationのサイズ・memory type・UUIDを検証し、成功importのみfdを消費する。stock hostではnative DMA-BUF対応範囲のみ。exact168B capsetのvendor magic/flagsとproxy/server handshakeで合意したpaired hostではnative OPAQUE_FDを使用する。どちらも内部backend契約 |
| `vkGetMemoryFdKHR` | export可能として作成したmemoryのみ。呼ぶたび独立fdを返し、元VkDeviceMemory破棄後もallocationを保持 |
| `vkGetMemoryFdPropertiesKHR` | Khronos VUID 00674はOPAQUE_FDを禁止するため、今回の対応型に有効な問い合わせはない。無条件に成功stubを返さない |
| `VK_KHR_external_memory_capabilities` / properties2 | 実native buffer/image能力からguest OPAQUE_FDへ変換。dedicated-only等の未実装profileは広告しない。deviceUUID/driverUUIDを提供 |
| `VK_KHR_external_fence_fd` | OPAQUE_FDの参照型payloadをexport/import。permanent/temporary、reset、wait、dup/SCM、producer終了を扱う。SYNC_FDは広告しない |
| `VK_KHR_external_fence_capabilities` | 対応型とimport/export可能性をK能力に合わせて公開。native Vulkan成功を検証するworkerがKsignalを担う |

[OPAQUE_FD memory](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryHandleTypeFlagBits.html)と
[OPAQUE_FD fence](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalFenceHandleTypeFlagBits.html)はdma-bufを必須にしない。memoryではdeviceUUIDとdriverUUIDの一致条件を守り、異なるrenderer GPUの任意共有を約束しない。

## 描画GPUと表示デバイスの組

- K/driverは安定identity、能力、companion hint、表示世代と実backing importを提供する。GPUを特別な登録サービスへ集約しない。
- libvulkanは明示companionを優先し、hintのないdisplay-only nodeは列挙中で最小device_idのrendererへ関連付ける。表示identityは(node id, display id)の組であり、別nodeの同じdisplay番号は衝突しない。surfaceとswapchainが表示nodeを保持する。
- libvulkanはswapchain作成時に条件を照会し、linear共有画像のallocation exportにplacement/maxDMAを渡してから共有scanout importを試す。ENOTSUP/ENOTTY/EXDEVによる非対応なら、広告されたcopy経路へ一度だけ切り替える。ENOMEM等の実行エラーを暗黙fallbackで隠さない。present毎の再判定や再openは行わない。
- 通常のVenus direct WSIはGPUでcrop/scale/黒余白を含むlinear表示画像を作り、BLOB scanoutへ渡す。読み出し・CPU合成・GPU_RESOURCE_WRITEを通常経路に入れない。
- コンポジタはクライアント画像の寿命・GPU合成・表示を標準WSIへ依頼する役割を持つ。実DMA互換性やCPU fallbackの判定はlibvulkan/driverに置く。現在のzwlは承認済みの全画面同期テストドライバのまま。

Venusはguestにnative physical backingを公開できないため、現backendのforeign physical importは未対応。別nodeの実コードfixtureではdisplay-only backendがDMA32/cache/extentを検証し、拒否と最後の参照解放を確認する。これは異種GPU実機やIOMMUの実証ではない。

VenusのblobはHOST3D allocationであり、guestのDMA physical backingではない。Venusは非零の物理placement要求をENOTSUPで拒否し、DMA32/contiguous/coherent対応を偽って広告しない。現在のVenus display constraintsは物理条件零なので、新64B要求から旧allocatorへ進み、通常BLOB経路を維持する。独立driver fixtureは実確保したstorageと別に定義したDMA page metadataで成功・不適合解放を確認する。これは新しい実機placement allocatorやHAL機能を実装したという意味ではない。

GPU node群はinstanceで最初にdiscoveryした時点の一覧を保持する。既存node内のoutput数とgenerationは問い合わせるが、実行中に追加されたGPU nodeの自動再列挙は今回の実装ではない。Kの表示変更通知基盤はGPU_DISPLAY_EVENTSとPOLLPRIで用意し、VK_EXT_display_control全体やWSIのbackground hotplug配信は追加しない。

表示通知のsequenceはoutput generationとは別であり、合流・重複を許す再照会要求である。Venusはtransport-owned sequenceをIRQ-safe queue lockで更新し、lockを離してpoll_notifyする。GPU登録handleをIRQから借りず、openがない時やunregister時にもそのhandleの解放と競合しない。INTx config bitまたはMSI-X device eventを観測し、通常のdisplay queryが遅延IRQより先にeventをclearする場合もclear前にpublishする。sequenceは1から始まり、UINT64_MAX後の新eventはsticky EOVERFLOWとする。新eventとACKが競合したときはlevel-readyが残り、copyout失敗では観測済cursorもACKも変わらない。dup/SCM_RIGHTSは同じopen descriptionのcursor、独立openは別cursorを持つ。
