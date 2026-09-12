# WS014 p006: Wayland client / WSI実装引き継ぎ

2026-09-13 JST、q309-i01。最小Wayland client、Vulkan Wayland WSIのU実装と限定試験を記録する。Phase、master、Queueの状態は本資料では変更しない。K handle/GPU/Venus、zwl、wltestと実VMを含むp006全体の受入は[phase.md](phase.md)の完了条件で判断する。

初回実測時点では、統合担当から、`q309-wayland-001`の実QEMU/Venusで12フレームの全画素照合が成功したとの報告を受けた。これは初回の固定転送payloadの結果であり、後続のWSI teardown・extent能力修正を含む最終ソースのlifecycle受入は当時まだ完了していなかった。生成元終了、client/compositor切断、再起動、console復帰を含め、最終VMの記録で確認する。以下のhost fixture成功をその代わりにはしない。

## 配置と公開境界

| 層・配置 | 所有する処理 |
| --- | --- |
| `libc/include/wayland/` | 最小Wayland clientの公開型、関数、core/xdg-shell interface descriptionと独自factory宣言 |
| `libc/include/wayland-client*.h`、`wayland-util.h`、`xdg-shell-client-protocol.h` | 標準include spellingを維持する薄い入口header |
| `userland/base/libwayland/` | 独立実装の接続、wire、fd、proxy、event queue、listener/dispatcher、utility |
| `libc/include/vulkan/vulkan_wayland.h` | `VK_KHR_wayland_surface` revision 6の2関数と作成情報型 |
| `userland/base/libvulkan/wsi-wayland.c` | アプリ所有のdisplay/surfaceを借り、専用queue、buffer import、commit、frame/releaseを制御 |
| `userland/base/libvulkan/wsi-image.c`、`wsi-swapchain.c` | GPU内共有画像、外部ownership barrier、acquire/present、失敗時の回収 |
| K/GPU/Venusと`zwl` | immutable allocation capability、別contextへのimport、native表示と表示中の参照を保持 |
| `wltest`等のアプリ | 標準Wayland/xdg-shellとVulkan APIを使う。独自factoryやGPU ioctlは呼ばない |

base package名は`libwayland-client`、ソースディレクトリは`base/libwayland`、公開SONAME/配置は`/lib/libwayland-client.so`。現在のpackage対象はamd64。ホストDSOでは183個の公開`wl_*`/`xdg_*`/`zed_gpu_buffer_v1_*` symbolを確認し、内部`wlc_*`はexport mapで隠す。libvulkanは実libwayland-client DSOへ依存する。Vulkan公開関数は従来155個にWaylandの2個を加えた157個で、instance extension enablementにより探索を制限する。

`vulkan.h`は`VK_USE_PLATFORM_WAYLAND_KHR`が定義された場合にWayland型を公開し、`vulkan_wayland.h`の直接includeも可能。WaylandとVulkan Wayland公開型はC/C++、ILP32/LP64で検証した。C++検証で見つかった既存`stddef.h`の`wchar_t`再typedefは、C++では宣言しない最小guardで修正した。targetに不足していた`EPROTO`はlibcへ追加されており、別のerrnoへの読み替えはしていない。

## 最小clientの対応範囲

| 対象 | 対応 |
| --- | --- |
| core interface description | wl_display/registry/callback/region/buffer v1、wl_compositor/surface/output v4 |
| xdg-shell interface description | xdg_wm_base/positioner/surface/toplevel/popup v1 |
| 接続とdispatch | connect/connect_to_fd、disconnect、get_fd/error/protocol_error、flush、dispatch/pending、queue別dispatch、roundtrip |
| 読み取り調停 | prepare_read/prepare_read_queue、read_events、cancel_read、複数readerのmutex/conditionによる世代調停 |
| proxy | 動的ID、create/destroy、constructor/marshal variants、wrapper、queue継承、userdata/tag/listener/dispatcher |
| wire引数 | int/uint/fixed、string、array、object、requestのnew_id、SCM_RIGHTS fd |
| utility | 標準のlist、array、fixed型とinterface/message/argument layout |

これはクライアント側の対応範囲であり、zwlがすべての入力・window操作を実装しているという意味ではない。zwlは試験用の全画面構成で、サーバが広告するversionとrequestの意味を別途守る。

byte streamはnative endian、message長は16bitで4byte整列し、最大65532byte。byte FIFOとfd FIFOを独立して保持し、header/payloadの断片化、partial send、fdの先着・後着を扱う。必要なfdがまだない完全なmessageは消費せず、次のrecvmsgを待つ。標準fd引数はpayload内に数値やplaceholderを置かない。

送信marshallerはfdをCLOEXEC付きで複製してからqueueへ載せる。呼び出し元は直後に元fdをcloseできる。最初の正のsendmsg結果でrightsが転送済みになり、残りのbyte再送で同じfdを送らない。受信fdもCLOEXECとし、実際に呼んだlistener/dispatcherへ所有権を渡す。配送前に破棄されたeventやdisconnectでは、未配送rightsを回収する。1messageの送信fd数は現KのSCM_RIGHTS上限8個まで。

proxyはcaller、wire ID map、wrapper、queued eventが同じ世代を保持する。local destroy後はqueued callbackを呼ばず、serverのdelete_idでmapの参照を退役させる。古いqueued eventは古いproxy世代を保持し、新しいID所有者へ配送されない。IDは動的で、固定個数のproxy tableにはしていない。

同じproxy/queueを同時に使うcallerは通常のWayland所有権規約に従って同期する。disconnect前に全利用者を止め、所有するproxy/queueを破棄する。callbackとpollは接続mutexを保持して実行しない。最初のfatal errorはstickyに保持するが、EAGAINのbackpressureとcancel_readは通常の状態として扱う。

入力、wl_shm、wl_subcompositor、EGL、汎用server library、全Wayland SDKは未対応。選択したinterfaceのlistenerは実型で呼び出し、未知の拡張eventは`wl_proxy_add_dispatcher`を使う。libffiは使わず、server-created new_id eventは現subset外として拒否する。接続の通常dispatch/roundtripには標準の無期限待機があり、WSIのFIFO待機watchdogと同一ではない。

## 独自buffer factoryと64byte metadata

独自protocol名は`zed_gpu_buffer_v1`、version 1。opcode 0はdestroy、opcode 1は`create_buffer(new_id wl_buffer, fd, array metadata)`、署名は`nha`。eventはない。WSIとzwlだけがこのfactoryを使い、アプリは通常の`VkSurfaceKHR`/`VkSwapchainKHR`とWayland objectを見る。`linux-dmabuf-v1`は広告しない。

metadataは[include/uapi/gpu.h](../../../include/uapi/gpu.h)の`struct gpu_image_descriptor`、version 1、size 64。Wayland clientはopaqueなwl_arrayとして転送し、内容の信用判断やGPU objectの操作をしない。

| byte offset | 型・field | 意味 |
| --- | --- | --- |
| 0 / 4 | u32 version / size | descriptor revisionと64byteの長さ |
| 8 / 12 | u32 width / height | 画像extent |
| 16 / 20 | u32 format / stride | RGBA/BGRA channel順と実row pitch |
| 24 / 32 | u64 offset / allocation_bytes | allocation内画像offsetとallocation全体の長さ |
| 40 / 44 | u32 memory_type / usage | renderer memory typeと画像usage |
| 48 / 52 | u32 tiling / reserved | 初版LINEAR=1、reserved=0 |
| 56 | u64 device_id | Kが付与するGPU identity |

受信側はfdをGPU_RESOURCE_IMPORTしてK保存のauthoritative metadataを取得し、arrayとの一致を照合する。Kはtype、device identity、allocation資源種別、完全なmetadataと範囲を検証する。`allocation_bytes`は対象resource長と等しく、strideは`width * 4`以上かつ4byte整列、offsetは32bitで表現可能かつresource内、`stride * height <= allocation_bytes - offset`を要求する。引き算と乗算の前提を確認してから範囲を判定する。

Venusの初版export extentは幅・高さとも16..4096。WSIも`minImageExtent=16`、`maxImageExtent=min(physical.maxImageDimension2D,4096)`を返し、空の交差はunsupportedにする。Venusの1resource上限は256MiBで、4096×4096×4の可視領域64MiBとは矛盾しない。実際のrow padding、offset、renderer memory requirementsは別途実allocationとK exportで検証し、packed widthで代用しない。

`vkGetPhysicalDeviceWaylandPresentationSupportKHR`は接続状態、queue能力、GPU_CAP_SHAREに加え、専用queueでfactoryが広告されていることを確認する。factoryにはdevice identity/capability eventがまだないため、query段階では複数GPUの対応を判別できない。初版は同一GPU一つの構成に限定し、他GPUはK importで拒否する。複数GPUを一般化するときには、factoryのdevice/format/extent交渉を拡張する必要がある。

## WSIのqueue、画像所有権、表示

アプリの`wl_display`と`wl_surface`は借用し、WSIが作ったwrapperだけに専用queueを割り当てる。wrapperから作るregistry、factory、wl_buffer、frame callbackはそのqueueを継承する。WSIはprepare_read_queue/read_events/cancel_readとdispatch_queue_pendingを使い、同じconnectionで受信したアプリeventをqueueへ積んでも、アプリlistenerは呼ばない。

アプリが描画する通常のVkImageと、共有するlinear VkImage/allocationを分ける。共有画像はrendererの実memory requirementsとrow layoutから作成し、GPU内copyで更新する。新経路のswapchain backendはCPU image-to-buffer readbackを使わない。従来direct-display backendのcopy経路は維持する。ホストrenderer内部のdma-buf利用はゲストAPIへ公開せず、全層zero-copyは主張しない。

producerはcopy完了後にactual presenting queue familyからEXTERNALへownershipをreleaseする。最初の使用はUNDEFINED、再使用はwl_buffer.releaseを受けた後にGENERALのEXTERNALからpresenting familyへacquireする。`shared_presented`は実queue submit成功後にのみ更新し、command記録・enqueue失敗時は未実行のownership遷移を記録しない。consumer側のGPU利用・完了・native scanout保持はK/GPU/Venus/zwlの契約と組み合わせる。

`wl_surface.frame.done`はpacingの進捗であり、allocation再使用の許可ではない。`wl_buffer.release`だけがcompositorの使用終了を表す。acquireはアプリが取得中の画像とcompositorが保持中の画像を除外し、全画像がbusyならtimeout 0でNOT_READY、有限deadlineでTIMEOUTを返す。この場合semaphore/fenceはsignalしない。acquire signalに失敗した場合も画像取得stateを戻す。

FIFOは前回frameの完了を待ち、10秒のwatchdog超過はterminal surface lossへ移す。MAILBOXは前のframe完了を待たずに新しいcommitを送れるが、画像再使用には同じrelease規約を適用する。socket loss、factory removalなどのlost状態はatomicに保持する。submitがwaitを消費した後のnative OOMは、未変更を約束できないためDEVICE_LOSTへ正規化する。

swapchainのlease解放はframe/buffer protocol ownerを退役させる。古いswapchainと現在のswapchainのどちらを破棄しても、`wl_surface.attach(NULL)`やcommitは送らない。mapping、xdg configure、明示unmapはアプリの所有物であり、WSI teardownが変更しない。表示中のK共有refはcompositorがreplacementまたはsurface destructionまで保持し、buffer protocol objectの破棄だけで表示画像を解放しない。callback proxyはlistener dataをfreeする前にdestroyし、遅れたeventからの参照を抑止する。

## 限定試験と証拠

すべてrepo rootから実行する。fixtureは実production sourceをlinkし、wire peer、renderer/native境界と期待値を試験側で別に定義する。host試験では実GPUの画素やK fdを偽装した成功を受入根拠にせず、通信・所有権・command選択を検証する。

| command | 確認した結果 |
| --- | --- |
| `plan/ws014/phase006/tests/run-wayland-client.sh` | 通常とASan/UBSan PASS。private/default queue、wrapper継承、破棄callback抑止、delete_id世代、断片wire、4096 proxy、60016byte partial sendとrights一回、元fd close、CLOEXEC、遅延rights、callback fd所有権と二重close、未送信fd回収、2reader cancel barrier、protocol error、切断 |
| `plan/ws014/tests/run-wayland-wsi-test.sh` | 通常とASan/UBSan PASS。実clientと実WSI、factory有無、extent交差、private queue、fd import、frameとreleaseの分離、FIFO/MAILBOX、replacement、暗黙unmapなし、OOM、terminal loss、allocator回収とcallbackによるerrno変更後のsocket error保持 |
| `plan/ws014/tests/run-wayland-swapchain-test.sh` | 通常とASan/UBSan PASS。実shared-image creator/swapchain、GPU image copy・CPU readbackなし、external ownership、enqueue失敗rollback、release前acquire不可、timeoutでsignalなし、submit後OOMのdevice/context/per-chain errorと全回収 |
| `plan/ws030/tests/run-wsi-discovery-test.sh`、同`asan`、同`ubsan` | 旧direct-display discovery/swapchain/native adapterが各PASS。新shared-image hookへ入った場合はdirect専用fixtureがassertする |
| `sh plan/ws030/tests/run-libvulkan-dispatch-test.sh` | 実libvulkanと実libwayland-client DSOをlink、157 symbol addressとscope、全instance 3bit/device 2bitの拡張gateを独立期待値で比較し通常/ASan+UBSan PASS |
| `sh plan/ws030/tests/run-vulkan-abi.sh` | 固定参照と122構造体/union、842field、2348enumがi386/amd64で完全一致 |
| `plan/ws014/phase006/tests/run-wayland-abi.sh` | Wayland utility layout、Vulkan Wayland全field/型幅/定数とPFN/prototype整合をC/C++、i386/amd64の4通りでPASS |

ABI runnerの既定参照は`/tmp/q308-virglrenderer-1.1.0/src/venus/venus-protocol`と`/tmp/q309-vulkan-wayland-1.3.269.h`。別配置ではcore directory、Wayland headerの順に引数を渡す。参照SHAを検査し、target objectの.rodataを比較する。参照coreの動画型includeを解決するため、従来ABI runnerと同様にhostの`/usr/include/vk_video/`も必要。

API Noct maintenanceは出力を一時ディレクトリへ限定して対照した。`vulkan_core.h`、`dispatch-table.inc`、`api-commands.tsv`、`opcodes.h`の維持ファイルが再生成と完全一致する。既存core header先頭のmode/copyright、guard名、空行とずれていた`api-preamble.h.in`を維持headerに同期した。実APIデータは変えていない。

clientの6 production sourceはtarget ClangのC構文確認、宣言位置警告、Clang analyzerで確認し、analyzer診断は0件。通常のtargetビルドとELF配置、最終rootfs、VMの証拠は統合担当のp006結果にまとめる。汎用formatterのdefaultは本repoの関数定義規約と一致しないため、formatter無警告を成果として記載しない。

## 出典と今後の境界

[Wayland API-PROVENANCE.md](../../../libc/include/wayland/API-PROVENANCE.md)に、Wayland 1.23.1 core XML、wayland-protocols 1.36 xdg-shell XMLのURL/SHA、client ABIとwire契約、MIT-style noticesを記録した。公開interface factsを参照し、client/server C、scanner output、libffiは取り込んでいない。維持する有限protocol tableとtyped wrapperは独立sourceで、production generatorはない。

[Vulkan API-PROVENANCE.md](../../../libc/include/vulkan/API-PROVENANCE.md)に、core 1.3.269由来の選択データと、[公式Vulkan-Headers v1.3.269のWayland header](https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.3.269/include/vulkan/vulkan_wayland.h)を記録した。Wayland header SHA-256は`3728578b8d6d98f6f3d20672406f869253eeacae8546a9d9577bca5c63a88d12`、宣言licenseはApache-2.0。libvulkanはVulkan 1.0と選択した表示拡張を実装するもので、後世のextension enum値が宣言にあることを実装広告にしない。

この引き継ぎは、amd64、既存QEMU/Venus、同一GPU、linear RGBA8/BGRA8、全画面最小compositorの範囲である。入力、複数window/装飾、一般DE、Wayland完全SDK/CTS認証、EGL/GLES、native i915、他GPU間共有、公開external-memory/fence拡張は受入対象に足さない。p004では本Phaseの実測で必要になったAPI・制約を整理するが、p006の最終lifecycle受入を省略しない。

## C coding-style最終レビュー

`wsi-wayland.c`全体と`wsi-swapchain.c`のq309変更箇所を全文規約に照合した。public関数の複数行comment、先頭宣言、個別allocation/check、条件式外のatomic load、関数/loop/lock/returnの意味paragraph、成功・失敗の別終了を整理した。swapchainの既存direct-display動作を維持し、変更後の実Wayland WSI、shared-image swapchain通常/ASan+UBSan、旧direct WSI、target構文/宣言位置とdiff whitespace確認が通った。ユーザーallocatorがerrnoをEAGAINへ変更しても、flush直後に保存したsocket errorでSURFACE_LOSTを返す境界も独立fixtureで確認した。

§2のfile順序について、`wsi-wayland.c`のimmutable ops/listener tableはstatic callback名をinitializerで参照するため、必要な一行forward declarationをそのtableより前に置く。Cの名前解決を満たす最小の順序例外として、実行担当でこの配置を維持した。構造上のためだけにops accessorを追加せず、残りの一行prototype、public definition→static definitionの順序は守る。型・file-scope定数の役割とlistenerの配送先をcommentに残した。

clang-format 19のdefault dry-runは一行prototypeや関数引数定義の改行等で本repoの規約と衝突するため、自動適用しない。raw診断と全文manual reviewを区別し、formatter無警告とは扱わない。最終DSO/rootfs buildとVMによるlifecycle受入の追記は統合担当が行う。

固定後のformatter raw結果は`build/q309-wayland-wsi/clang-format-19-wsi.log`に保存した。clang-format 19.1.7はexit 1、863件の整形提案を出し、canonicalな長い一行prototype、引数を別行に置くdefinition、意味paragraph等との衝突を含む。これは自動適用済み/formatter合格の記録ではない。

同梱`build/llvm/bin/clang`は`RunAnalysis not compiled in`でanalyzerを実行できなかった（`build/q309-wayland-wsi/clang-analyze-wsi.log`）。targetによる構文・宣言位置確認は成功している。代わりにhost Clang 19.1.7を`--analyze --target=x86_64-unknown-elf`、同じproject headersで実行し、WSI 2sourceはexit 0・診断0件となった（`build/q309-wayland-wsi/clang-analyze-wsi-host.log`）。検証ツール自体の機能差を、ソース解析失敗やtarget analyzer合格へ読み替えない。

## q309最終受入

最終kernelでq309-direct-002とq309-wayland-004を受入済み。先行実測時点の未完了記述は履歴として保持し、現状は[結果と失敗履歴](results.md)および[最終証拠](final-evidence/verification.json)を参照する。p006 cleared、p004はplanning・未queue。
