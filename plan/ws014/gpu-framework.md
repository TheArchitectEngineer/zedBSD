# WS014 p002: GPUフレームワークの登録契約

現行契約は末尾のp006補遺を参照。以前の各Queue・Phaseの記述は当時の履歴として保持する。

q305 / q305-i01、2026-09-12。ユーザーが指定した通常のデバイス登録APIへ修正した。
実装元は `include/drivers/gpu.h`、`include/uapi/gpu.h`、`src/drivers/gpu/gpu.c`。
q304のPCI専用公開table・登録wrapper・一括publish・固定8台の方式は置き換えた。
旧試験結果はq304履歴に保持し、この契約の検証結果と区別する。

以下の5 callback・capability・未実装項目はq305完了時点の記録。p003で追加した
内部interface version 2と、version 1を維持したUAPIの現行契約は
[Vulkan責務資料のp003追記](vulkan-api-responsibilities.md#p003の実装済みgpu契約q306--2026-09-12)を参照する。

## 公開API

```c
int
drv_gpu_register(
	const struct drv_gpu_ops *ops,
	void *private_data,
	struct drv_gpu_device **result);

int
drv_gpu_unregister(
	struct drv_gpu_device *device);
```

`ops`は実装ごとの不変の関数表、`private_data`は1台のhardwareを表すbackend state。
同じopsを複数deviceで共有できる。registerの各成功は独立したdeviceを返し、
`/dev/gpuN`へ公開する。新しいGPU実装を加えてもGPUコアの列挙表や登録tableの
編集は必要ない。GPUコアはバスの種類を知らず、公開ヘッダにPCI型・PCIヘッダへの
依存を持たない。

コアがdeviceを動的確保し、空いている最小番号を割り当てる。解除後は番号を再利用
できるが、古いinodeは元のcdev generationを保持する。GPU固有8台、共通cdev16個の
固定上限を除く。メモリや番号・generationの表現限界による失敗は通常のerrnoで返す。
1 sessionあたりのresource handle上限32個は、device台数とは独立した既存ABIの
資源上限であり、この変更では維持する。

| 操作 | 呼出し側とコアの所有権 |
| --- | --- |
| register前 | 呼出し側がhardwareを初期化し、opsとprivate_dataを保持する |
| register成功 | コアがdeviceとcdevを所有する。呼出し側は返却handleを保存し、ops/private_dataを有効に保つ |
| register失敗 | resultはNULL。コアが途中の確保・公開を巻き戻し、ops/private_dataの所有権は移らない |
| unregister開始 | コアが新規open/ioctlを停止し、nodeを非公開にする |
| 使用中のunregister | EBUSY。handleとbackend stateを保持し、最後のclose後に呼出し側が同じhandleで再試行する。lifecycle同時実行によるEBUSYならwithdraw前のこともある |
| unregister成功 | handleは消費され、呼出し側はops/private_dataを解放できる。成功後に同じhandleを再使用しない |
| 古いinode/fd | cdevの参照とgenerationで保護する。offline後のopen/ioctlはENODEV、pollはPOLLERR/POLLHUP。既存resource/sessionはcloseで回収 |

unregister成功後に残る古いinodeはofflineなコアwrapperだけを保持し、backendを
呼ばない。解除前に受け入れたcallbackはsession参照の内側で完了する。
同じregistrationに対するlifecycle操作は呼出し側が所有を管理する。

## PCIからの利用

PCI IDの照合とdriver種類の登録は既存の `drv_pci_driver_register()` が担う。
GPUのregisterは検出済みのdevice instanceをGPUコアへ渡すAPIである。

PCIがattach成功後の公開とhardware detach前の解除を管理する場合は、PCI側の
static service callbackから上記の共通APIを呼ぶ。既存の汎用
`drv_pci_service_interface`を使えるが、GPUヘッダにそのtableを公開しない。
backend instanceにops/private_dataと返却GPU handleを保持すればよい。

| PCI側の境界 | 通常のGPU API呼出し |
| --- | --- |
| publish | `drv_gpu_register(ops, instance, &instance->gpu)` |
| unpublish | `drv_gpu_unregister(instance->gpu)`。成功後にinstance->gpuをNULLへ戻す |
| hardware detach | unpublish成功後にhardware/private stateを解放。EBUSY中は保持 |

公開失敗の巻戻しやdetach失敗後の再試行は既存の汎用PCI lifecycleを使う。
PCI→GPUを接続する実production経路と最小adapterをホストfixtureで検証する。
これは実GPU backendの実装・描画成功を意味しない。

## 共通cdevとdevfs

cdev registryはkernelの初期BSS状態から利用でき、動的な登録listで管理する。
VFS初期化は登録をresetしない。attachがdevfs mountより早くても、登録した
デバイスをmount後にそのまま見つけられる。GPUだけの再公開関数や初期化hookは
不要である。`cdev_reset()`は明示的な全件解除操作として残る。

devfsのディレクトリsnapshotも実際の登録数に合わせて確保し、16件で打ち切らない。
snapshot作成時にはcdev参照を保持し、コピー後は名前とgenerationを保存して
readdir時に現行登録と照合する。解除や同名再登録でもgenerationを取り違えない。statvfsは固定device poolの空き数を報告しない。

## drv_gpu_ops

version=DRV_GPU_INTERFACE_VERSION、size=`sizeof(struct drv_gpu_ops)`、reserved=0。
capabilitiesは0またはGPU_CAP_RESOURCE。userlandのGPU_ABI_VERSIONとは別の
kernel内callback contractであり、UAPI layoutは変更しない。

| member | 引数 → 出力 | 必須性とcontract |
| --- | --- | --- |
| open | private_data, `void **session` → errno | 必須。open descriptionごとのbackend stateを作る。失敗時は自身で回収 |
| close | private_data, session → void | 必須。全resource_destroy後に呼ぶ。解放が完了してから戻る |
| get_info | private_data, session, `struct gpu_info *` → errno | 必須。ゼロ初期化された出力にdriver_nameとmax_resource_bytesを設定。coreがversion/size/capabilities/handle数を確定 |
| resource_create | private_data, session, `const struct gpu_resource_create *`, `void **object` → errno | capabilityとセットで任意。検証済みkernel copyと割当済みhandleを受け、成功時はNULL以外のobjectを返す。失敗時は自身で回収 |
| resource_destroy | private_data, session, object → void | resource_createと対。明示破棄、copyout失敗、final closeで解放を完了して戻る |

coreはcallback中にspinlockを保持しない。異なるsessionは並行してcallbackへ
入れるためbackendが共有stateを保護する。同じopen descriptionは1 ioctlのみ
受け入れ、並行・再入ioctlはEBUSY。dup/forkされたfdは同じsessionを共有する。

## /dev/gpuNのU/K契約

初期ABIはrootだけがopenでき、credential不在も拒否する。open時の書込み権利を
sessionへ保存し、後のcredentialやfile status flag変更で拡張しない。

| operation | Uの要求 | Kの検証・処理 |
| --- | --- | --- |
| GPU_GET_INFO | version/sizeを入れたgpu_infoを渡す | 固定長copyin、layout確認、get_info dispatch、初期化済みsnapshotをcopyout |
| GPU_RESOURCE_CREATE | bytes、STORAGE usage、flags=0、handle=0 | 書込み権利、capability、layout、上限と空きslotを確認。backend allocate後copyout。返却失敗ならdestroy |
| GPU_RESOURCE_DESTROY | 同一sessionで受け取ったhandle | 権利、layout、slot＋generation全体を確認してdestroy。無効・破棄済み・別sessionのhandleはEINVAL |
| poll | fdの切断を待つ | offlineでPOLLERR/POLLHUP。render完了やvblankは未実装 |
| final close | 最後のfd参照を解放 | 残存resource全破棄→backend close→session参照解除 |

payloadはpointerなしの固定幅整数。gpu_info=56 byte、gpu_resource_create=32 byte、
gpu_resource_destroy=16 byte。version/sizeは厳密一致。handleはkernel全体で単調な
generationとsession内slotから成るopaqueな64 bit値で、wrap前にEOVERFLOWを返す。
未知ioctlおよび未対応resource操作はEOPNOTSUPP。

## p003への引継ぎ

mmap、GPU VM/DMA、context/capset/blob、transport/submit/fence、scanout/present、
display権限、cursor/hotplugは未実装。libvulkan.soと実GPU backendも未実装。
現行VFSのcdev mmap dispatch不足を含め、p003でvirtio/Venusの利用から補う。
HAL責務変更はこの修正に含まれない。44 callback案は将来機能を含む検討資料で、
上の実装済み5 memberとは区別する。

## 検証

- `plan/ws014/tests/run-gpu-framework-test.sh`: 実GPU/cdev/PCI core、通常・ASan/UBSan、ILP32/LP64 ABI、複数device、handle/権限/rollback/解除。
- `plan/ws006/tests/run-dynamic-cdev-devfs-test.sh`: 共通cdev/devfsの動的登録・列挙・mount・generation/参照寿命。元WSの受け入れを変更せず、共通層修正の回帰確認に使用。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix`: amd64対象kernel buildと既存checker。

q305の検証は全項目PASS。40 GPU、80 cdev、通常・ASan/UBSan、共通層の既存GCC静的解析、ILP32/LP64 ABI、amd64 buildを確認した。
具体的な手順と対象hashは [Queue q305履歴](../history/queue-q305.md) に記録した。

## p006: 共有allocationと現行ops契約（q309）

本節は2026-09-13の実装を`include/drivers/gpu.h`、`gpu-share.h`、`gpu-display.h`、`include/uapi/gpu*.h`、`include/kern/handle.h`、`fd-object.h`とGPU coreへ照合した記録。q305/p002の登録設計と実績を保持し、p003・WS030からp006までに具体化された現在の境界を示す。単体検証と最終QEMU受入は[p006](phase006/phase.md)の結果で区別する。

### version、登録、resource

| 境界 | 現行値・動作 |
| --- | --- |
| K内`struct drv_gpu_ops` | `DRV_GPU_INTERFACE_VERSION = 4`、size厳密一致、reserved=0。基本11 callbackに`resource_map`、任意の`display`/`share` subtableを持つ。古いversionのopsを現在型として解釈しない |
| U/K固定幅要求 | `GPU_ABI_VERSION = 1`を維持。旧要求layoutを維持し、map、display、export/importを個別の要求として追加 |
| 登録・解除 | `drv_gpu_register(ops, private_data, **device)` / `drv_gpu_unregister(device)`。同じimmutable opsを複数deviceで共有し、GPU coreはPCI型へ依存しない |
| unregister待機 | 新規open/ioctlを止めて非公開化した後、sessionまたは独立share参照が残ればEBUSY。呼出側は成功までops/private_dataを保持する。最後のopenが閉じてもexported allocationが残る場合がある |
| resource table | sessionごとの動的listとopaqueな64bit世代handle。旧固定32 slotは現行ではない。backendの`gpu_info.max_resources`とbyte上限、割当失敗・表現限界で制限する。Venusのmax_resourcesはUINT32_MAX |
| fd table | 上記GPU resource数とは別に、現Kのprocess fd上限は`KERN_OPEN_MAX = 32`。共有allocationをfdへexportすると通常のfd枠を消費する |

必須のopen/close/get_info、resource_create/destroy、capset/blob、read/write、command、legacy STORAGE presentを維持する。resource_mapは`GPU_CAP_MAPPING`、display subtableは`GPU_CAP_DISPLAY`、share subtableは`GPU_CAP_SHARE`と対応する。coreはcapabilityとcallbackの整合を登録時に確認し、spinlock内でbackend callbackを呼ばない。同じopen descriptionの1 ioctl制限と、別session間のbackend同期責務は維持する。

### 任意opsの実型と所有権

| K interface | 現行の責務 |
| --- | --- |
| `resource_map(device, session, object, struct drv_gpu_mapping *view)` | 所有resourceの不変CPU viewを返す。coreがresourceとopen descriptionを実VM mapping/pin中保持する。DEVICE属性はMMIOとcoherent DMA RAMを区別する |
| `drv_gpu_display_ops` | `query/mode/claim/release/present/wait`の6 callback。display discovery、exclusive lease、完了済みframeの提示、native timingとscanout保持を担当する |
| `drv_gpu_share_ops.export_resource(device, session, object, const gpu_image_descriptor *, void **shared)` | source session resourceを壊さず、open寿命から独立したbackend shared object参照を1つ返す。失敗では所有権を返さない |
| `drv_gpu_share_ops.release(device, shared)` | そのexport参照だけを消費するinfallibleな最終回収。coreはbackend shareを落としてからdevice withdrawal barrierを解放する |
| `drv_gpu_share_ops.import_resource(device, destination_session, shared, void **object, uint32_t *resource_id)` | shared objectを借り、destination sessionに独立して所有されるresourceと非zero renderer identityを返す。後の回収には既存resource_destroyを使う |

`GPU_CAP_SHARE = 256`はBLOB能力とshare 3 callbackが全部揃った場合だけ登録できる。capabilityなしにshare tableだけを置く登録も拒否する。GPUごとのregister/publish専用wrapperは増やしていない。

### kernel object fdと共有UAPI

`kernel_handle`はrefcount、type、release ops、opaque objectを保持する独立wrapper。`handle_create()`成功でpayload所有権と返却1参照を受け持ち、最終`handle_put()`がpayload release後にwrapperを解放する。callerがwrapperを埋め込んで別途freeする契約ではない。

`handle_fd_create(h, flags)`は成功で新しいfd参照を取得し、callerの入力参照は成功・失敗とも残る。返り値はfdまたは負のerrnoで、O_CLOEXEC/O_CLOFORKを受ける。`handle_fd_get(fd, expected_type)`はexact typeの強い参照またはNULLを返す。通常のGPU callbackの成功0/正のerrnoとは返却規約を混同しない。

fd tableとSCM_RIGHTSは`struct fd_object`（NONE/FILE/HANDLE）の共通参照で統合した。単純なstruct copyは参照取得ではなく、get/install/putで所有権を動かす。送信待ちmessageも参照を保持し、受信fd予約・commit/rollback、close/dup/fork/exec/CLOEXEC/CLOFORKと切断の共通経路を使う。kernel pointerをUへ露出せず、inodeや巨大なpseudo-fileをGPU capabilityのために作らない。

| ioctl | 固定要求 | Uの入力とKの出力・検証 |
| --- | --- | --- |
| GPU_RESOURCE_EXPORT | `gpu_resource_export` 88byte、`_IOWR('G',10,...)` | source handle、version/size、fd=-1、CLOEXEC/CLOFORK flags、画像descriptor。writable open、同sessionのBLOB、完全な画像範囲を検証し、Kがdevice_idを付与。完全copyoutとfd予約commit後だけfd所有権を公開 |
| GPU_RESOURCE_IMPORT | `gpu_resource_import` 96byte、`_IOWR('G',11,...)` | version/size/fd以外の出力欄はゼロ。typed handle/opsのpayload契約と同一deviceを確認し、destinationの新しいhandle/resource_idとK保存descriptorを返す。元fdは消費しない。copyout失敗では未公開aliasを回収 |

descriptorは`gpu_image_descriptor` version 1 / 64byte（width/height/format/stride、offset/allocation_bytes、memory_type/usage/tiling、reserved、device_id）。Kのimmutable copyが正であり、receiverが勝手なmetadataをimportへ指定できない。allocation_bytesは対象resource長と一致し、stride>=width×4かつ4byte整列、offsetは32bitで表現可能、stride×heightがallocation内に収まることを検証する。

blob flagsはMAPPABLE=1、SHAREABLE=2、CROSS_DEVICE=4を扱う。CROSS_DEVICEにはSHAREABLEが必要で、共有flagsにはGPU_CAP_SHAREが必要。これはrendererにexport可能なallocationを要求するflagであり、zedBSD側で別GPU間importを許可するという意味ではない。現Kの別device importはEXDEV。

### Venus表示と今回の制限

初版のnative shared scanoutは同一GPU、linear RGBA8/BGRA8、幅・高さ16..4096。resource上限は256MiBで、offset、実row pitch、全row paddingを含む範囲をKで検証する。`GPU_DISPLAY_BLOB` capability flagと`GPU_DISPLAY_PRESENT_BLOB`要求により、受信側sessionへimportした共有blobを既存display lease経路から使う。旧STORAGE presentも維持する。

GPU実行中、転送中、native scanout中の参照はfd数とは別に保持する。producerの元fdやopenが閉じても、必要なconsumer/backend参照が残る間はallocationを生かす。protocol wl_buffer destroyだけで表示中の参照を落とさず、replacementまたはsurface destructionまでcompositorが保持する。

WSI/Waylandの詳細は[実装引き継ぎ](phase006/wayland-implementation.md)を参照する。ホストrenderer内部で必要なLinux dma-buf利用はゲストの公開ABIと分け、ゲストDRM互換やlinux-dmabuf-v1は追加していない。native i915は未実装で、全層zero-copyも主張しない。

### p004へ渡すAPIレビュー事項

- 通常の動的registerとimmutable ops共有を維持し、resource、mapping、display、shareの所有権・capability依存を一つの現行表へ整理する。44候補を未実装のまま必要なく追加しない。
- SHAREABLE/CROSS_DEVICEというrenderer flagと、同一GPUだけを許可するguest capabilityを明確に区別する。将来backendのexport/import条件を表現する際の不足を検討する。
- 現在固定のformat/extentと、factoryにdevice/format/capability eventがない制約を整理する。i915や複数GPUへ広げる前に、GPU core/UAPI/WSIのどこが実能力を供給するかを決める。
- 表示leaseはopen所有、shared allocationはopenから独立という違い、unregister時のshares待機、mapping/GPU/scanout参照の回収順を明文化する。
- 実測したp006の成立範囲を前提に、後続i915の実装依存を整理する。Wayland SDK全体、EGL/GLES、他GPU間共有、外部fence fdを自動で追加目標にしない。
