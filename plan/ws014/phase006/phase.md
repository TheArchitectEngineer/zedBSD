<!-- awesome-plan project=zedbsd record=ws014-p006 -->

# WS014 p006: kernel handleによるGPUメモリ共有と最小Wayland WSI

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS014](https://github.com/awemorris/zedBSD/issues/15)
Queue: q309 finished / q309-i01 cleared
Execution: completed; final source build, direct and Wayland acceptance verified
Dependencies: cleared ws014-p002 / p003 / p005 and completed WS030 outputs
Next: ws014-p004 planning, not queued
<!-- awesome-plan-current:end -->

Combined ID: `ws014-p006`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Decision source: current user, 2026-09-13 JST. 「p006を1つ作りましょう」およびkernel/driver、zwl、libwayland、wltest、VK_KHR_wayland_surfaceの指定。GitHub計画同期は継続承認。Phaseを分割しない。今回の作成時点では実装Queueを開始していない。

## 単一目標

同一GPU上で、`wltest`が標準VulkanのWayland WSIを使って描画し、別プロセス・別GPU contextの`zwl`が、SCM_RIGHTSで受信したkernel object fdを通じて同じGPU allocationを利用して全画面表示できる。GPU内で画像を扱えるようWSIを拡張し、クライアントからコンポジタへの受け渡しと新しい表示経路にCPU readbackを必須としない。

この目標に必要なK基盤・GPU driver・Wayland通信・WSI・試験アプリと検証を本Phaseにまとめる。以下の順序は実装上の段取りであり、別Phaseや複数の実行項目へ分割しない。終了済みWS030は再利用しない。

## 配置と責務

| 配置 | 実装するもの |
| --- | --- |
| `include/kern/`、`src/kern/` | `kernel_handle`、`handle_fd_*()`、共通fd参照、SCM_RIGHTSの統合と参照寿命 |
| `include/drivers/`、`include/uapi/`、`src/drivers/gpu/` | GPU資源のexport/import、型・所有権・範囲検証、device/sessionから独立した必要寿命 |
| `src/drivers/gpu/venus/` | 同一GPU・別contextへの共有allocation接続、renderer import、同期・回収・GPU内表示経路 |
| `libc/include/wayland/` | 最小Wayland clientの公開header。標準の`<wayland-client.h>`等のinclude互換は薄い入口headerまたはsysroot配置で維持 |
| `userland/base/libwayland/` | 独立実装の最小client library。公開ABI/SONAMEは`libwayland-client.so`、配置は`/lib/libwayland-client.so` |
| `libc/include/vulkan/`、`userland/base/libvulkan/` | `VK_KHR_wayland_surface`の公開header、dispatch、surface/swapchain backend、GPU画像を扱うWSI内部境界 |
| `userland/base/zwl/` | 試験用の全画面Waylandコンポジタ。実際のprotocol/共有fd/表示を扱う。ウィンドウ装飾や一般DE機能は持たない |
| `userland/base/wltest/` | 標準Wayland client APIとVulkan APIを使う試験アプリ。GPU ioctlやVenus wireをアプリへ直接持ち込まない |
| 既存package/build/sysrootと`plan/ws014/tests/` | library/appのbuild・配置、限定試験と既存private QEMU capture loopへの統合 |

計画作成時点では新規作成予定だった配置は、q309で実装・検証済み。ヘッダ・library・protocolの対応範囲を明示し、全Wayland SDK/全拡張対応とは扱わない。サーバ内部の通信部品は必要に応じて共用してよいが、汎用`libwayland-server.so`の公開は完了条件に加えない。

## kernel handleとfdの契約

ユーザー提示の`struct kernel_handle`は参照数・type・ops・`void *object`を保持し、`kernel_handle_ops.release(object)`を最終参照で一度呼ぶ。`handle_fd_create()`、型を確認して強い参照を返す`handle_fd_get()`、`handle_put()`を基本契約にする。実際の宣言・エラー規約・原子型は現行Kの規約に揃える。ユーザー空間へカーネルポインタを公開したり、任意のU指定ポインタを登録したりしない。

VFSの名前解決・inodeを必要としないオブジェクトをfdから保持できるようにする。現行fd table/SCM_RIGHTSの`struct file *`固定を調査し、file・socket・handleの共通参照操作へ整理する。GPU固有の参照操作を各syscallへ散在させない。現行の匿名pseudo-fileは比較対象・既存機能であり、巨大なfile wrapperを採用することを前提にはしない。

fdへのinstall成功/失敗時の参照移譲、handle wrapper自身の解放者、table lock下でのget、lock外でのreleaseを定義する。close/dup/dup2/fork/execとCLOEXEC/CLOFORK、受信fd予約/commit/rollback、fd上限、無効type、非I/O handleへの操作を一貫して扱う。汎用opsはreleaseから始め、GPU操作や同期のための追加callbackを先回りで増やさない。

SCM_RIGHTSは同じobjectの参照を送信待ちmessageが保持し、受信先fdへ移譲する。受信前close、転送失敗、control切り詰め、切断時も回収する。GPU実行中・mapping・表示中の保持はfd数だけで判定しない。共有handleから対象GPUと共有可能な資源・権限をGPU層で検証する。GPU session全体を共有して他資源まで操作可能にする代用をしない。

## GPU allocation共有と表示

プロセスごとの`VkImage`と、共有するallocation/K資源本体を分ける。受信側contextは同じallocationを正当なimport操作で利用し、それに合う画像を作成・bindできる。生成元sessionのcloseやプロセス終了後にも、受信側・転送中・GPU使用中の必要な参照が資源を生かす。GPU identity、format、extent、tiling/layout、usage、allocation offset/sizeを検証する。

初期受入はamd64・既存QEMU/Venus・単一GPU・固定全画面・RGBA8/BGRA8の実証できる組合せへ限定する。制約はruntime capabilityとして表現し、未対応formatや他GPUを誤って宣言しない。現行MAPPABLE-only blobと単一contextへのattachを、固定rendererが供給するexport/import機構と照合して拡張する。

WSIは完成したCPU pixelsだけをbackendへ渡す形から、GPU画像/共有allocationと同期を扱える形へ拡張する。新経路では共有・合成・表示のためのGPU内copy/blitを許容し、CPUへの画像読み戻しと再アップロードを必須にしない。既存のcopy表示経路は互換fallbackとして保持できるが、その成功だけをGPU内経路の受入証拠にしない。キャプチャ/oracle用の読み戻しは検証経路として分離し、QEMUホスト内部のcopyやscanout制約は観測できた範囲を記録する。全層のzero-copyを根拠なく主張しない。

初版の同期はproducer/consumerそれぞれのGPU完了待ちとWaylandのreleaseによって成立させてよい。描画完了、コンポジタの利用完了、表示中の保持を分ける。scanoutへ直接使う画像は表示から外すまで再利用させない。共有fence fdや外部同期拡張は必要性が出た場合に本目標の依存分だけ実装する。

## WaylandとVulkanの公開契約

`VK_KHR_wayland_surface`を採用し、`vkCreateWaylandSurfaceKHR`と`vkGetPhysicalDeviceWaylandPresentationSupportKHR`を追加する。既存のsurface/swapchain APIから、実際の`wl_display`と`wl_surface`へ接続する。FIFOとWayland必須のMAILBOX、acquire/timeout、present順序、画像再利用、surface loss、swapchain再作成、WSI専用event queueを実装・検証する。Wayland 1.11以降のclient契約を採用する場合はproxy wrapperも扱う。

最小libwayland/zwlは実wire形式、object ID/世代、registry/globalのversion交渉、request/eventとfd ancillary data、partial I/O、flush/dispatch、接続切断を扱う。採用するcore版に必要な`wl_display`、`wl_registry`、`wl_compositor`、`wl_surface`、`wl_buffer`、`wl_callback`等を実装する。全画面surfaceのrole/configureは最小の標準`xdg-shell`を基本とし、初期configure/ackとcloseを扱う。未実装の機能やversionを広告しない。

GPU bufferはzedBSD専用のbuffer factory拡張（名称は実装時に固定）にkernel object fdを渡して通常の`wl_buffer`へ変換する。`linux-dmabuf-v1`、ゲストLinux dma-buf ABI、DRM互換は採用しない。ホストrenderer内部のLinux機構はゲストABIの採用と区別する。アプリは独自factoryを直接使わず、libvulkanのWSI内へ隠す。

`zwl`は実際のattach/damage/commit、frame callback、buffer releaseと描画/表示同期を担い、単なる成功応答stubでは完了にしない。入力、装飾、複数window、一般DE、EGL/GLES、native i915、他GPU間共有は今回の目標へ追加しない。

## 単一Phase内の実施順序

1. 既存コードとrenderer契約を確認し、kernel handle/fdの共通参照・SCM_RIGHTS・寿命管理を実装する。
2. GPU共通層/Venusの共有allocation、別context import、同期と回収、GPU内表示を実装する。
3. libvulkanのWSIをGPU画像へ対応させ、Wayland backendと公開dispatchを実装する。
4. 最小libwayland、zwl、wltestとbuild/installを接続する。上記部品間の小さな契約修正は本Phase内で行う。
5. 実装の中核が揃ってから試験・アプリを結合し、実QEMUの結果に基づいて修正する。必要なcompile/ABI/所有権の確認は各段階で行ってよい。試験の誤りも根拠があれば修正し、元結果と理由を保持する。失敗を隠す期待値変更はしない。
6. 最終ソースで限定回帰・build・実表示と適用規約を確認し、p004へAPI/制約/証拠を引き渡す。

## 完了条件と検証

- 型付きhandleの作成/取得/解放とfd送受信を実装し、close競合、dup/fork/exec、受信失敗/切断、最終releaseを実際のKコードの限定fixtureで確認する。
- `wltest`と`zwl`が独立process・独立GPU contextで同じallocationを共有する。fdが届くだけ、pixelのprocess間コピーだけ、同じGPU openを丸ごと渡すだけでは満たさない。
- wltestのVulkan描画→Wayland WSI→実SCM_RIGHTS/commit→zwlのGPU利用→全画面表示が成立する。固定frameと時間変化する複数frameを実VNC/QEMU captureと独立期待値で照合し、GPU内経路の使用も資源対応/commandの証拠で確認する。
- producerの元fd close後の利用、生成元プロセス終了後の必要寿命、client/compositor切断・終了時の解放と再起動を確認する。表示owner終了後には既存console復帰も維持する。
- FIFO/MAILBOX、acquire/release、描画/表示同期、event queue、初期configureと再作成・surface lossの限定意味論検証を行う。`libwayland-client.so`/libvulkanのheader・symbol・dispatch・ELF依存・rootfs配置を確認する。
- 現行の直接表示vkdemo、GPU/PCI/VM/fd/AF_UNIXの変更影響に対応する限定回帰を行う。最終build、規約全文レビュー、実行環境/commands/hashes/失敗履歴/制限を記録し、受入後にp004へ渡す。

検証は既存のawe@10.0.10.25 private QEMU/Venusと許可済みimage/source転送を使う。q308実測環境と現在ホストの差を実行時に確認する。1 build/転送/VMをtimeoutで有限化し、同じ条件の無変更retryは最大3回。fixtureは意味のある境界と主要失敗へ限定し、既存合格試験を理由なく反復しない。時間枠・個別command/attempt IDは実行Queueの具体化時に記録する。

## 依存・規約・実行境界

入力は[p002](https://github.com/awemorris/zedBSD/issues/383)、[p003](https://github.com/awemorris/zedBSD/issues/384)、[標準API訂正済みp005](https://github.com/awemorris/zedBSD/issues/387)、[WS030の受入出力](https://github.com/awemorris/zedBSD/issues/392)。次は[p004](https://github.com/awemorris/zedBSD/issues/385)の最終GPU framework/API整理。p006の規約確認をp004全体のclearanceに流用しない。WS030と既存cleared Phaseは再開しない。

[Guardrail](https://github.com/awemorris/zedBSD/issues/363)、ローカル`plan/coding-style.md`全文、`plan/master-design-policy.md`、`plan/standards/automation.md`を適用する。ANSI Cの宣言・制御構造、関数/コメント/所有権・既存drv_gpu_opsの動的登録を守る。base systemは独立実装とし、公式protocol/XMLの出典・revision・ライセンスを記録する。恒久的generatorはNoctを使う。対象buildは`make -j16`、変更対象のformat/analysis/限定試験と`git diff --check`を行い、aggregate `make check`は禁止。

追加HAL変更を前提にしない。必要になれば既存承認と区別できる具体差分を先に提示し、適用可能な明示承認後に進める。git add/commit/pushはユーザー担当。計画作成時点の「計画のみ」は履歴。q309の実装・build/runtime受入は末尾の完了記録を参照。

## 計画作成時点の設計根拠と未確定事項（履歴）

現在のfd/pseudo-file/SCM_RIGHTS、GPUのsession所有、WSIのCPU readback、固定virglrenderer1.1.0のresource importを静的確認した。別contextの実共有・GPU内表示・最小Waylandの実測はまだない。export/import flags・対応allocation条件・最終scanoutの制約を最初のGPU実装で確認し、達成不能な条件が判明した場合は証拠と残件を記録する。

- [Wayland core protocol / wl_buffer](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_buffer)
- [Wayland wire protocol](https://wayland.freedesktop.org/docs/book/Protocol.html)
- [Vulkan Wayland WSI](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html#_wayland_platform)
- [Vulkan外部メモリと同期の区別](https://github.khronos.org/Vulkan-Site/guide/latest/extensions/external.html)

参照日は2026-09-13。採用するprotocol/spec/headerの固定revisionと実装範囲は実装時に記録する。標準APIを公開しても正式CTS認証や全Wayland互換を根拠なく主張しない。

## q309開始: WS014 p006を単一項目で実行（2026-09-13）

ユーザーの「では、実行してください。」により、[p006](https://github.com/awemorris/zedBSD/issues/393)全体をq309-i01として実行する。kernel handle/fd/SCM_RIGHTS、GPU/Venusの別context allocation共有・GPU内表示、VK_KHR_wayland_surface、最小libwayland-client.so・zwl・wltest、限定検証と実QEMU受入を一つのPhase/項目に含める。中核K/driverを先に実装し、通信/WSI/アプリを接続して実測から改善する。p004は含めない。

時間枠は720 active minutes見積、120分ごとに進捗・残件を確認。各command/VMを有限化し、同条件無変更retryは3回まで。達成の保証や無限継続ではなく、未達は証拠と再開条件を残す。全規約を適用し、既存成果/履歴を保持する。HAL追加変更とgit add/commit/pushは許可されたとは解釈しない。private host・image/source転送の承認を維持する。

q309はactive、q309-i01とp006はin-progress、WS014はincomplete。q308 finished・WS030 completed・既存clearanceは維持。新経路でCPU readbackを必須にせず、実GPU allocation共有と同期/寿命/Wayland protocolを確認する。実装成功・Phase受入はまだ記録していない。

## q309 checkpoint001: GPU画像共有とWayland実表示（2026-09-13）

ユーザーの指摘に沿って表示経路を確認した。従来のCPU readback経路もGOPへは書かず、virtio-gpuの2D resource/SET_SCANOUTへ送っていた。p006では共有GPU allocationをSET_SCANOUT_BLOBへ渡し、通常のWayland表示にCPU readback/再uploadを必須としない。表示数・接続・推奨サイズはGET_DISPLAY_INFO、追加モード・nominal refreshは今回追加したGET_EDIDのbase/CTA progressive DTDから取得する。無効/未対応EDID時は既存50Hz、100Hz超・standard/established timing・DisplayID・物理vblank保証は対象外。

K handle/fd/SCM_RIGHTS、GPU共有、最小libwayland-client.so、VK_KHR_wayland_surface、zwl、標準Wayland/Vulkanアプリwltestを実装した。K参照・copyout rollback、GPU export/import/scanout、Wayland byte/fd FIFO・queue・frame/release・swapchainの限定試験は通常とASan/UBSanで通過。公開ABIはi386/amd64 C/C++で固定公式Wayland/Vulkan headerに一致、Vulkan137 core＋20 WSIの157 dispatch/exportを照合した。正式CTSや全Wayland SDK互換は主張しない。

実QEMU10.0.11/virglrenderer1.1.0/Intel ANVのq309-wayland-001は21.257秒でPASS。生成元プロセス終了後、独立receiver contextでimportしたGPU画像をGPU copyし、検証専用readbackの1024画素が一致。wltest→実SCM_RIGHTS→別processのzwl→scanoutではFIFO/MAILBOX各6枚、計12枚の320×240実VNC画像が独立oracleと全画素一致した。各modeのswapchain再作成と通常終了、zwl12frame/cleanup_failed=0も確認した。これは初回実測で、最終ソースの受入ではない。

検証後、WSI破棄によるアプリ所有surfaceの暗黙unmapを除き、zwlの明示unmap/remapを整理。共有extentの照会範囲とexport上限を一致させた。クライアント/コンポジタ異常終了・再起動、可視console復帰、最終限定回帰/build/規約確認を継続中。p006とq309-i01はin-progress、q309 active、WS014 incomplete。p004未queue、WS030 completedと既存clearanceを維持。HAL追加変更なし、git add/commit/pushはユーザー担当。

local/uncommitted証拠: plan/ws014/phase006/checkpoint001.json、plan/ws014/temp/remote/q309-wayland-001/result.json とevidence/。source/doc/imageはGitHub repository未公開であり、本同期はIssue/Projectの計画と結果記録。

## q309完了: GPU handle共有・Wayland WSI・virtio scanout（2026-09-13）

WS014 p006 / q309-i01をcleared、q309をfinishedとする。active Queueなし。WS014はincomplete、p001/p004 planning、p004未queue。WS030 completedと既存Phaseのclearanceを維持し、native i915は別WS029のまま。

kernel_handle/handle_fd_*と共通fd参照・SCM_RIGHTS、GPU/Venusの独立process/context共有、VK_KHR_wayland_surface、最小libwayland-client.so、全画面zwl、標準Wayland/Vulkanアプリwltestを実装した。共有GPU imageはGPU copyと所有権同期を経て別processへ渡り、SET_SCANOUT_BLOB/RESOURCE_FLUSHで表示する。通常の新WSI経路にCPU readbackや再uploadを必須としない。旧copy経路もGOPではなくvirtio 2D scanoutであり、直接表示の互換経路として保持する。

GET_DISPLAY_INFOとGET_EDIDのbase/CTA progressive DTDで表示・モードを列挙。実QEMUでは1280×800、74,994mHz、320×200mmを取得した。custom framebuffer寸法はEDID寸法と独立に扱い、1〜100Hzのguest nominal pacingを検証する。virtioは物理pixel clockを設定しないため、物理vblank同期の保証とは区別する。

最終q309-wayland-004は43.869秒、QEMU exit0でPASS。FIFO/MAILBOX各6枚の320×240実VNC画像、計921,600画素が独立期待値と全画素一致し、12回の表示が実import資源とBLOB scanoutに対応した。生成元終了後の独立renderer import/GPU copyも検証専用readbackの1024画素が一致。swapchain再作成、client中断・再open、compositor通常終了・SIGKILL・再起動、実SURFACE_LOST/cleanup=0、強制終了直後の640×480 console復帰とechoによる画面更新を確認した。

同じ最終kernelのq309-direct-002も42.705秒、QEMU exit0でPASS。標準vkdemoの回転直方体6枚をGPU readback/VNC/独立oracleで照合し、通常終了とSIGINT後の再open、console復帰、表示競合拒否とowner完走を確認した。K/fd/SCM・GPU/EDID・Wayland/WSIの実コード限定fixtureとsanitizer、157 Vulkan dispatch/export、両ABIの公式header照合、Noct再生成、rootfs配置、対象buildと全適用規約を確認した。正式CTSや全Wayland SDK互換は主張しない。

途中のharness起動待ち不足とEDID/custom mode回帰を修正し、失敗証拠と再実行理由を保存した。最終reviewのMSG_PEEK二重put疑義は、rights付きpeekを既存guardが拒否するため到達不能と確認し、本体変更を戻して拒否後の参照寿命を追加検証した。formatterは規約と設定の不一致によりexit1でありPASSとは扱わず、全文確認とdiffcheckを記録した。HAL追加変更なし。ローカル結果はplan/ws014/phase006/results.md、技術資料3件、conformance.mdとfinal-evidence/verification.json、Queue履歴はplan/history/queue-q309.md。これらsource/doc/imageのgit add/commit/pushはユーザーが行う。本同期はGitHub Issues/Projectの計画・受入結果である。
