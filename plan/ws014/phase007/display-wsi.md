# WS014 p007: direct display WSI と非同期 present

q310 の描画/表示経路、同期と所有権の実装記録。以下の PASS は実 production source をリンクした有限 host fixture と構文検証の結果であり、QEMU の統合受入は別の結果記録に従う。HAL、zwl のイベントループ、Wayland private protocol はこの変更で拡張していない。

## GPU 常駐と作成時の経路選択

通常の Venus direct WSI は、アプリの optimal VkImage から別の linear 共有画像へ GPU copy を行い、独立した display open に import した alias を BLOB scanout へ渡す。`VkDisplayPresentInfoKHR` があれば GPU clear と nearest blit で crop/scale/黒余白を作る。黒余白は RGBA=(0,0,0,1) で、明示的な copied fallback と同じ opaque 出力にする。通常経路には CPU map/readback/composition、`GPU_RESOURCE_WRITE` を入れない。

アプリ画像の shared-swapchain group と native 表示先画像の寿命は分ける。各 swapchain が独立した linear/native 画像を所有するため、複数 plane が同じ表示先を上書きしない。native alias を破棄しても、K が保持する current scanout allocation は次の成功した置換または解除まで残る。selection fence 完了だけではその画像を acquire 可能にしない。

`GPU_DISPLAY_CONSTRAINTS` は表示 generation、packed format、stride/offset alignment、placement、DMA 上限と経路を返す。ゼロ alignment は追加制約なし、U では 1 byte として扱う。placement は要求であり、U が実ページの到達性を推測するものではない。作成時に実 import を試し、非対応 (`ENOTSUP`/`EXDEV`、対応する Vulkan FORMAT/FEATURE_NOT_PRESENT) だけで、広告された COPY 経路を一度選べる。途中まで作った linear image/native alias をすべて解放してから `prepare_copy` と staging を作る。ENOMEM、壊れた metadata、device loss は fallback で隠さない。経路は swapchain の寿命中固定で、present 毎の import 再試行や native allocation はない。

物理条件は予約flagだけに留めず、leaseの`placement/max_dma_address`を`vulkan_wsi_shared_image_create`→`vulkan_memory_allocate_placed`→`memory_export`→`GPU_BLOB_CREATE_PLACED`(33)へ渡す。条件pointerはこの同期作成中だけ借り、published memoryやjobへ保存しない。新要求は64B、既存`GPU_BLOB_CREATE`は40Bのまま。Kのoptional `blob_create_placed`は実backingの条件を確かめ、不適合候補はcallback内で解放してENOTSUP、条件零は旧allocatorへ進む。物理base alignmentは新要求で指定できるが、現在のWSIでは物理base条件を持つquery項目がないため0を渡す。native画像のoffset/stride alignmentは別に実layoutで確認する。

VenusのHOST3D blobはguest DMA page列を証明できないため、非零physical条件はENOTSUP。通常のVenus constraintsは零なので新要求から既存BLOB allocatorへ進む。ENOTSUPと旧kernelのENOTTYはFEATURE_NOT_PRESENT、ENOMEMはOUT_OF_DEVICE_MEMORY、その他の不確かなbackend failureはDEVICE_LOSTとする。確定した非対応/OOMではnative allocationを解放し、不確かなdevice lossでは従来通りcontextの退役へ委ねる。条件不成立でcopyを選ぶ場合も、作成時に一度だけ固定し、present中に再判定しない。

## 描画 device と表示 node

`wsi-display-nodes.c` は instance の初回 direct discovery で `/dev/gpu[0-9]+` を一度列挙し、query 30 の K identity と role を保存する。描画能力のない node を VkPhysicalDevice にしない。実際に列挙された renderer への companion hint を優先し、hint がない/存在しない表示 node は最小 K device_id の renderer に対応付ける。表示 identity は `(device_id, local display_id)` であり、別 node の同じ表示番号は衝突しない。

node inventory と discovery fd は instance 所有で、instance destruction まで保持する。新規 GPU node の発見は次の instance による列挙となる。既存 node の output count と generation は通常 query ごとに再照会する。swapchain の実 display connection は `(VkDevice, native device_id)` ごとの独立 open で、renderer context mutex を保持したまま表示待機をしない。open 後にも K identity を再確認し、path 再利用を別 device の同一視に使わない。

foreign native import は、source handle が保持する実 physical page array を借り、destination の optional `scanout.import_image` が address/cache/extent/format/placement を確認する。GPU core は destination resource の destructor が戻った後に source handle を put する。foreign image の immutable `device_id` は producer のまま、native-only `resource_id=0` は renderer command に渡さない。Venus は guest の実 physical backing を公開しないため、別 controller への任意共有を広告しない。display-only fixture の PASS は異種 GPU 実機や IOMMU の実証ではない。

## 非同期 present と fence

`vkQueuePresentKHR` は caller の image indices、対象 chain、表示 rectangle を所有する job に写し、必要な command storage と private fence を事前確保する。queue mutex の同一区間で `vulkan_queue_submit_locked` と ordered worker への publish を行う。戻った後に caller の配列/pNext を読まず、以前の pResults を書き換えない。pResults は enqueue 時点で分かる結果である。

queue ごとの worker は、実 native Vulkan fence の完了を `vulkan_fences_wait` で確認してから native present または Wayland commit を行う。decoder completion、IRQ の起床、context-zero scanout fence を producer GPU 完了の代わりにしない。Wayland protocol は従来のままで、この実 GPU 完了待ちが cross-context の依存を満たしてから zwl が読み始める。

native adapter が対応するときは、queue-owned private VkFence を標準 OPAQUE_FD で export し、reset 後に query した exact generation を job に保存する。display plane は別の completion fd を所有する。`GPU_DISPLAY_PRESENT_SYNC` は producer fd/generation を再確認し、成功した native selection を別の display completion payload に signal する。producer と display が異なる node でも同じ typed payload の意味を保ち、producer memory と destination device の identity を混同しない。RESET 入力は毎回ゼロ初期化し、QUERY の出力 state/error を流用しない。

private completion は queue の cache に置き、同時 job 数に応じて増やす。後続 frame は reset/generation 更新で再利用し、fd と external completion worker を毎 frame 作り直さない。device teardown は accepted job を drain、WSI worker を join、cached VkFence の completion worker を停止してから fd/cache を解放する。

worker の遅延エラーは future acquire/idle/chain operation に残す。native submit 受理後に notification が失敗した DEVICE_LOST も明示的な終端として扱い、通常の未送信再試行へ戻さない。queue/device idle は accepted native request を drain するが、最後の表示 allocation が画面に残っていることだけでは待ち続けない。copied fallback の staging は chain に一つで、前 job が借りている間は次の予約を待つ。

## Venus 表示待機

`display_next_refresh` は owner/lease/generation を controller mutex の下で保存し、`sched_sleep` 中は mutex を離す。再取得後に transport failure と同じ owner/lease/generation、console への新規 claim を確認する。active callback または停止時に join される console worker が output metadata の寿命を保つ。別 renderer session の command admission を表示 cadence 待ちで塞がない。

FIFO は EDID/validated mode に基づく guest の nominal refresh と、fenced scanout selection/flush の完了である。物理ホストの vblank 到達を保証したとは主張しない。failed transport で通常 release が完了しない場合、software shared-front hold を落とし、hardware backing は checked reset まで quarantine に残す。

## 表示変更の通知

`GPU_CAP_DISPLAY_EVENTS`(4096)と`GPU_DISPLAY_EVENTS`(32)を追加した。QUERYは40Bのsequence snapshotを返すだけで、ACKはそのopenで以前に成功copyoutされたsequence以下を明示する。`QUERY → output/generation再照会 → ACK(S)`で確認する。Kはcopyout成功後にobserved/ackを単調更新するため、途中のcopyout失敗、別threadの確認、新しいevent S+1で通知が消えない。新しいreadable openはsequence1を未ACKとしてPOLLPRI-readyになり、独立openごとに確認する。command完了は従来POLLIN、topologyはPOLLPRIで区別する。

追加のdisplay.events callbackはcontroller mutexや通常session admissionを取らず、短いIRQ-safe lockによるsnapshotだけを返す。呼出し中はVFSのfile参照がsession/backendを保持し、core registry lockの外でcallbackを呼ぶ。VenusはIRQ所有transportにsequenceを置き、INTx config bitまたはshared MSI-X vectorのdevice eventを見て更新、lock外でpoll_notifyする。通常queryがIRQより先にhardware eventをclearする場合もclear前にpublishする。重複通知は許し、output generationとevent sequenceを等しいものにしない。UINT64_MAXでの次eventはsticky EOVERFLOWとなり古いsequenceへwrapしない。

この基盤は既存node内のdisplay再照会に使う。新GPU nodeの自動再列挙、VK_EXT_display_controlの公開API、WSI background hotplug thread、一般zwl event loopは追加していない。

## 限定検証

| コマンド | 実 production 境界で確認したこと | 証拠 |
| --- | --- | --- |
| `sh plan/ws014/tests/run-wayland-swapchain-test.sh /tmp/q310-wsi-placement-fixture` | ordinary/ASan+UBSan の各 3 profile。3 job が実 producer wait 中に返る、caller array 変更、idle drain、external barrier、GPU crop/scale/opaque black、release-gated acquire、late OOM。private fence 3 slot の export/reset/generation/final close。部分 import rollback、物理条件の実shared-image helperへの伝播、placement OOM abort/ENOTSUP fallback、作成時の経路固定とpresent中の再判定ゼロ | `/tmp/q310-wsi-placement.log` |
| `sh plan/ws030/tests/run-wsi-discovery-test.sh`、同 `asan` / `ubsan` | dynamic output/image 数、shared rollback、実 native adapter の独立 display admission、BLOB layout、wait/失敗/alias destroy 後の front 保持、typed wait/signal generation、foreign identity、copy storage 事前確保、leaseからallocationへの物理条件snapshot | `/tmp/q310-native-placement.log`、`/tmp/q310-native-placement-asan.log`、`/tmp/q310-native-placement-ubsan.log` |
| `sh plan/ws014/tests/run-wsi-display-nodes-test.sh` | ordinary/ASan+UBSan。renderer-only/display-only、companion/default、複数表示 node と同じ local ID、1回の discovery、fd/allocator の最終回収 | `/tmp/q310-nodes-test-style.log` |
| `sh plan/ws014/tests/run-gpu-scanout-test.sh` | ordinary/ASan+UBSan。実 K handle/fd/GPU core、DMA/cache 非対応拒否、copyout rollback、元 fd/process open 退役後のページ pin、native destructor 後の final put、正常/エラー producer prerequisite と別 display signal | `/tmp/q310-scanout-style.log` |
| `sh plan/ws014/tests/run-venus-edid-test.sh` | ordinary/ASan+UBSan。実 display pacing、60 Hz 等の fractional cadence、sleep 中 controller lock depth=0・戻り時=1、wait 中 lease 変更で ESTALE | `/tmp/q310-display-final.log` |
| `sh plan/ws014/tests/run-gpu-topology-test.sh` | ordinary/ASan+UBSan。実GPU coreのQUERY/ACK、copyout失敗、途中event、独立openと保持alias、通常admissionからのreentry、POLLIN併存、offline時backend寿命 | `/tmp/q310-gpu-topology.log` |
| `sh plan/ws014/tests/run-venus-transport-test.sh` | ordinary/ASan+UBSan。実IRQのINTx/MSI-X config event、configだけでcommand完了を偽らないこと、used-ring完了との併存、lock外poll_notify、sequence飽和 | `/tmp/q310-transport-topology.log` |
| `sh plan/ws014/tests/run-gpu-placement-test.sh` | ordinary/ASan+UBSan。独立DMA page metadataの各endpoint/連続性/coherence/base alignmentと実確保storage、不適合候補とcopyout失敗の解放、旧backendの拒否、zero条件と旧40B互換 | `/tmp/q310-gpu-placement.log` |
| `sh plan/ws030/tests/run-libvulkan-external-memory.sh` | ordinary/ASan+UBSan。NULL legacy/zero placed64、非零64bit条件の独立offset照合、ENOTSUP/ENOTTYはnativefree exactly1・legacy再試行なし、ENOMEM rollback、EIOはdevice lossとしてnativefreeを止めcontext退役へ、native事前OOMではioctlゼロ | [placement-verification.md](placement-verification.md)、`plan/ws014/temp/q310-placement-enotty.log` |
| ILP32/LP64 `gpu-uapi-layout.c` | 旧要求とevents40B/placed64Bのsize、offset、ioctl encoding | `/tmp/q310-topology-placement-abi.log` |
| target compiler `-fsyntax-only -Wdeclaration-after-statement` | WSI6 source、K4 source、memory.cとgpu-share app、実target ABIと設定 | `/tmp/q310-wsi-target-final.log`、`/tmp/q310-topology-placement-target.log`、`/tmp/q310-memory-placement-target.log` |
| host Clang 19 `--analyze`、同 target ABI/sysroot | WSI 6 source、警告なし。repo-built compiler は analyzer を含まないため host analyzer を使用 | `/tmp/q310-wsi-analyzer-final.log` |

主な source は `userland/base/libvulkan/wsi-swapchain.c`、`wsi-display.c`、`wsi-display-nodes.c`、`wsi-internal.h`、`src/drivers/gpu/venus/display.c` と `include/{uapi,drivers}/gpu-scanout.h`。K query/import/fence と標準外部 memory/fence の全体契約は [gpu-uapi-contract.md](gpu-uapi-contract.md) を参照する。実 GPU の描画成功、公開 Vulkan 拡張の全受入、transport reset の実行結果は親タスクの統合記録が担当する。

最終のgpu.c目的コメント2箇所の補完は、文字列/文字定数を保持しコメントと空白だけを除いた20,667 C tokenが変更前後で一致することを確認した。証拠は`/tmp/q310-gpu-final-style-equivalence.json`と`/tmp/q310-gpu-final-style.diff`。受入binaryとの再build照合は統合記録に従う。


## q310 最終実受入

同一最終kernelのwayland-008、direct-004、fence-exit-003、recovery-004は全てPASS。producer終了の実waitは10msでDEVICE_LOST、recoveryは10000ms timeout後に新contextを確認。直接表示全runのQEMU traceで320×240 BLOB要求107回・同寸法legacy要求0回と実画面を照合した。source/artifact/renderer hashと全失敗履歴は[最終結果](results.md)および[verification](final-verification.json)に対応する。旧attemptの数値・fixture限界は履歴として保持する。
