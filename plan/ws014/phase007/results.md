# WS014 p007 / q310 実装・検証結果

2026-09-13、p007全体の有限受入を完了した。最終修正後の同一kernelで下記4試験をPASSとし、q310-i01をclearedとして計画へ反映する。p004/WS029は未queue、WS014全体はincompleteを維持する。

レビュー入力は [P1–P11](review-input.md) と [追補 B/C/E/F/G](review-input2.md)。詳細な実装契約は [GPU UAPI](gpu-uapi-contract.md)、[表示 WSI](display-wsi.md)、[transport・同期](transport-sync.md) を参照する。

## 最終受入

| 最終実QEMU | 結果 | 受入内容 |
| --- | --- | --- |
| [q310-wayland-008](../temp/remote/q310-wayland-008/result.json) | PASS、46.531秒、QEMU exit0 | 同一fdの128回並行操作、標準fence、linear/buffer/optimal共有のproducer終了後利用、初期表示通知QUERY/列挙/ACK、FIFO/MAILBOX各6画像と異常終了/reopen/console復帰 |
| [q310-direct-004](../temp/remote/q310-direct-004/result.json) | PASS、41.830秒、QEMU exit0 | 独立oracleの6画像、通常readback無効で13frame（0–1910ms）と異なる2実画像、SIGINT/reopen/lease競合/console復帰 |
| [q310-fence-exit-003](../temp/remote/q310-fence-exit-003/result.json) | PASS、4.424秒、QEMU exit0 | 生成元終了前NOT_READY、終了後DEVICE_LOST。実wait10ms、故障専用予算30秒。producerのwaitpidも成功 |
| [q310-recovery-004](../temp/remote/q310-recovery-004/result.json) | PASS、13.807秒、QEMU exit0 | 所有renderer停止→10000ms timeout、peer error/旧参照gate、全旧参照退役後に再開・checked reset、新contextで4096byte/decoder |

直接表示004はQEMU側の[scanout trace](../temp/remote/q310-direct-004/evidence/qemu-renderer.log)も保存した。320×240でnonzero resourceの`SET_SCANOUT_BLOB`要求107回、同寸法のlegacy `SET_SCANOUT`要求0回。scopeは診断・通常・再起動・競合を含む直接表示の全試験であり、通常frame単独の件数ではない。traceはQEMU検証前の要求選択を示すため、単独で成功扱いせず、実画面・アプリ正常終了・独立oracleの成功と組み合わせて受入した。console用640×480とscanout解除は別に観測される。

最終kernel SHA256は`9941d92ad403e21732a4e200f7aef2f57cea8317de7f8e7ff3e1c609f8afc0ba`。全4attemptでsource snapshotの前後一致、現在source/artifactとのhash一致、指定renderer libraryの実mapと開始/終了hash一致を照合した。[final-verification.json](final-verification.json)と[全attempt台帳](runtime-acceptance.json)に記録した。後述の007/003などは修正前後を区別した途中の証拠である。

`make -j16 ... disk-image`を成功させ、その生成物を最終4VMへ個別に転送した。限定fixture・ABI・Noct・sanitizerは後述の検証台帳と最新配置/close/traceの追加検証を合わせて確認した。新しいclose回帰の修正前FAILを含む小さなtext logは[保存索引](verification-logs/index.json)へコピーした。旧失敗を消して初回成功とは扱わない。

## 生成元終了: 失敗と修正後の実証

修正前の [q310-fence-exit-002 result](../temp/remote/q310-fence-exit-002/result.json) は **FAIL**。`/bin/gpu-fence-test --producer-exit` で pending fence の生成元終了後、受信側の `vkWaitForFences` が期待する `VK_ERROR_DEVICE_LOST` (-4) ではなく `VK_TIMEOUT` (2) を返した。[観測ログ](../temp/remote/q310-fence-exit-002/evidence/wayland-observed.log) の判定は次のとおり。

```text
GPUFENCE FAIL operation=producer death is error, never fabricated success actual=2 expected=-4
GPUFENCE FAIL role=parent stage=vkWaitForFences line=1077 vk=2 errno=42
```

修正前は consumer の有限 Vulkan wait、backend command drain、非同期 watchdog がそれぞれ 10 秒だった。consumer は producer へ終了許可を送ってから待機を始めるが、producer は受信、他 thread の退役、file cleanup を経る。その final close も drain の後で fence binding を error 終端していたため、consumer timeout と終端通知が競合し得た。002 には個別 wait の時刻がなく、過去の正確な scheduling 順序までは復元できない。観測された TIMEOUT は有限 wait の結果であり、成功の捏造や永久に error を伝播しないことを示すものではない。

K は **final file close の入口で pending producer binding を error 終端し、その後に native command drain** へ進むよう修正した。registry lock で late callback と直列化し、session / completion / resource / DMA の寿命を守る drain barrier は維持する。late success は公開済み error を上書きしない。file alias / mmap / 実行中 ioctl 等が参照を持つ間は final close に達しないため、`close(fd)` や `_exit` 開始からの一律の通知時間を保証する変更ではない。[transport 契約](transport-sync.md) に詳細を記録した。

新しい [gpu-fence-close fixture](../tests/run-gpu-fence-close-test.sh) は、追加 file reference がある間の pending、最後の close の drain 入口での error、drain 中の callback / session / resource の生存、late success の拒否、drain 後の資源退役を検査する。修正前は drain 入口の error assertion で FAIL、修正後は通常＋ASan/UBSan と既存 GPU fence suite が PASS。元 log は `/tmp/q310-fence-close-before.log`、`/tmp/q310-fence-close-after.log`、`/tmp/q310-fence-close-existing.log`。

U の故障試験は、通常試験の 10 秒を維持し、producer 終了だけ独立した 30 秒 deadline にした。実 `vkWaitForFences` を `CLOCK_MONOTONIC` で挟み、終了前 NOT_READY、終了後 DEVICE_LOST、producer の waitpid 成功を引き続き要求する。TIMEOUT / SUCCESS は失敗のまま。timeout 予算の拡大で結果を成功扱いに変えていない。[producer-exit-deadline.md](producer-exit-deadline.md) に application の変更と target 検証を記録した。

修正後の [q310-fence-exit-003 result](../temp/remote/q310-fence-exit-003/result.json) は **PASS**。[実観測](../temp/remote/q310-fence-exit-003/evidence/wayland-observed.log) は次のとおりで、実 wait は 30 秒予算に対して 10 ms だった。

```text
GPUFENCE PRODUCER_EXIT_WAIT result=-4 elapsed_ms=10 budget_ms=30000
GPUFENCE PRODUCER_EXIT_ERROR PASS
```

これは当該実行の測定値であり、全 scheduling 条件で 10 ms 以内という保証ではない。002 の FAIL と001の先行 PASS は履歴として保持する。同じ修正後kernelのWayland-008 / direct-004 / recovery-004も上記のとおりPASSとなった。

## 途中の実 QEMU 記録（失敗・修正の履歴）

以下の時間は各 JSON の `remote_result.elapsed_seconds`。転送・build・証拠回収を含む外側の経過時間や、GPU の実行時間とは区別する。QEMU は 10.0.11、KVM / amd64 / 2 vCPU / 1 GiB、`virtio-vga-gl,venus=on,blob=on,hostmem=256M,max_outputs=1`、Intel ANV / i915 `renderD128`。画面取得は QEMU の egl-headless / VNC Unix RAW。

| 実行 | 結果・時間 | JSON / 実ログから確認した内容 | この実行の限界 |
| --- | --- | --- | --- |
| [q310-wayland-007](../temp/remote/q310-wayland-007/result.json) | PASS、46.844 秒、QEMU exit 0 | 同一 GPU fd の 4 pthread × 32 回、計 128 allocation lifecycle。標準 external fence。linear / buffer / optimal allocation の生成元終了後・独立 context import と 1024 pixels 照合。FIFO / MAILBOX 各 6 画面、計 921,600 pixels、mismatch 0。12 present は flags=3 (FIFO＋BLOB)、320×240、50,000 mHz。client 中断・再接続、swapchain 再作成、compositor 通常終了・強制終了・再起動、surface loss と console 復帰 | topology は初期 sequence=1 / outputs=1 / attempts=1 の QUERY→inventory→ACK。実ケーブル抜挿や GPU hotplug ではない。pending producer 終了は修正後の専用 003 試験に従う |
| [q310-direct-003](../temp/remote/q310-direct-003/result.json) | PASS、41.810 秒、QEMU exit 0 | 標準 Vulkan direct WSI の診断 6 画面で検査対象 456,011 pixels、mismatch 0。通常 `--duration=2` は readback 無効で 13 frame、時刻 0–1930 ms。独立 VNC 2 画面の RGB が異なる。SIGINT、再 open、console 復帰、別 app の lease 競合拒否を確認 | 診断 oracle は raster/texel 境界 4,789 pixels を明示除外。通常 2 画面は非同期 submit と exact frame 対応を結ばない。60 fps や性能改善倍率の実証ではない |
| [q310-recovery-003](../temp/remote/q310-recovery-003/result.json) | PASS、14.004 秒、QEMU exit 0 | task 所有の renderer process を停止し、watchdog 10,000 ms / status 42、peer failure、旧 owner retirement gate、fresh open 後 4096 bytes roundtrip と decoder completion を確認。停止した process の再開も確認 | 対象 renderer の PID / starttime / inode を検証した隔離故障注入。物理 GPU reset、任意 driver、pending fence producer 終了の実証ではない |
| [q310-fence-exit-002](../temp/remote/q310-fence-exit-002/result.json) | **FAIL（修正前の履歴）**、14.502 秒、QEMU exit 0 | consumer の deadline 内に error 終端を観測できず、期待 -4 に対して 2。guest_completed=false | 個別 wait 時刻は未記録。QEMU が正常終了したことを guest 試験成功としない |
| [q310-fence-exit-003](../temp/remote/q310-fence-exit-003/result.json) | PASS、4.424 秒、QEMU exit 0 | retire-before-drain と故障専用 30 秒 deadline を使用。実 vkWaitForFences は DEVICE_LOST=-4、elapsed_ms=10 / budget_ms=30000。guest_completed=true | 同じ修正後kernelのWayland / direct / recovery統合確認もPASS。10msを普遍的な通知期限としない |

Wayland の強制 compositor 終了試験で現れる `VK_ERROR_SURFACE_LOST_KHR` と `cleanup=0` は期待された失敗通知であり、上記 PASS の判定に含まれる。一方、`fence-exit-002` の `VK_TIMEOUT` は期待値不一致である。

修正前の Wayland-007 / direct-003 / recovery-003 / fence-exit-002 の kernel SHA256 は `6ff30addf54c7fecbf199c8d7eb0eedf6c680e81e92fd63c82e70c1fbf8a08b6`。修正後の fence-exit-003 は `9941d92ad403e21732a4e200f7aef2f57cea8317de7f8e7ff3e1c609f8afc0ba`。各 `result.json` に image、source manifest、harness、renderer、証拠ファイルの hash がある。kernel が同じでも userland / harness / image 全体が同一とは扱わず、個々の manifest に対応する結果として保存する。

### 途中の通常描画の実測（direct-003）

`direct-003.remote_result.ordinary` は全 13 sample が `readback_disabled=true`。最初の通常実行の画像は次の 2 枚で、再 open 試験では上書きしていない。

| 画像 | geometry | 観測時点 | RGB SHA256 |
| --- | --- | --- | --- |
| [ordinary-1.ppm](../temp/remote/q310-direct-003/evidence/ordinary-1.ppm) | 320×240 | submit 2 の観測後 | `b8075d3b6500e48cddefaa341589cafbac193928dee0f39eee89484ab7a7c337` |
| [ordinary-2.ppm](../temp/remote/q310-direct-003/evidence/ordinary-2.ppm) | 320×240 | submit 3 の観測後 | `841f4cc460fc61ced6b44f05c9eb9ef85ce3866dbd8438652fa928179db104b8` |

GPU 常駐経路への変更と通常 readback の除去は確認できたが、この測定は **約 2 秒で 13 frame** に留まる。50,000 mHz の display mode / nominal pacing を app の 50 fps と呼ばない。終了・診断・競合を含む transport 累計も frame 当たりの時間ではない。`direct-003` の最後の transport 記録は submitted=3243、completed=3243、interrupts=3089、sleeps=702 であり、同条件の変更前測定を伴う倍率比較は行っていない。

## P1–P11 の処置

以下の処置表は実装と個々の有限検証を対応付ける。最終受入は先頭の4実行と追加fixtureを合わせた判断である。P9 / P10 はユーザーが認めたテストドライバ制限として維持する。

| 項目 | 処置・主な実装ソース | 証拠 | 残る制約・注意 |
| --- | --- | --- | --- |
| P1 完了通知 | [GPU core](../../../src/drivers/gpu/gpu.c) と [Venus transport](../../../src/drivers/gpu/venus/transport.c) に SUBMIT/WAIT、exact sequence、poll / IRQ / waitq、copyout 成功後の consume。[context.c](../../../userland/base/libvulkan/context.c) / [sync.c](../../../userland/base/libvulkan/sync.c) は decoder と queue timeline を区別 | transport / notification / fence fixture、Wayland-007、recovery-003 | IRQ、decoder marker、単なる起床を native GPU 成功にしない。marker slot 飽和時の非破壊確認や scheduler 開始前の限定 polling は残る。pending producer 終了は retire-before-drain 修正後の fence-exit-003 で PASS、同kernelの最終統合確認もPASS |
| P2 command batch / mmap | [commands.c](../../../userland/base/libvulkan/commands.c) と `commands-generated.inc` は 44 個の `vkCmd*` を記録し境界で送信。context は persistent reply/stream mmap と lock 内 snapshot を用いる | userland 13 job の commands / context、64 KiB を超える batch と独立 wire 値の確認 | すべての Vulkan API が非同期になる意味ではない。native VkResult と reply trailer の検査は必要 |
| P3 display 待機時の排他 | [display.c](../../../src/drivers/gpu/venus/display.c) は refresh 待ちを controller mutex の外へ移し、戻った後に owner / lease / generation / failure を再検証 | Venus EDID / display fixture で sleep 中 lock depth 0、lease 変更で ESTALE。実 direct / Wayland 表示 | guest clock の nominal pacing。host 物理 vblank や presentation timestamp の保証ではない |
| P4 direct BLOB | [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c)、[wsi-image.c](../../../userland/base/libvulkan/wsi-image.c)、[wsi-display.c](../../../userland/base/libvulkan/wsi-display.c) に optimal→linear GPU copy/blit→BLOB scanout。crop / scale / opaque black 余白も GPU で作る | direct-003、WSI / native fixture。通常 readback 無効、独立 VNC で動作確認 | 従来 direct WSI が BLOB 未対応だった指摘を修正。通常 Venus で CPU map / readback / composition / GPU_RESOURCE_WRITE を使わない。非対応表示 device の明示 COPY fallback は別経路 |
| P5 async present | [wsi-swapchain.c](../../../userland/base/libvulkan/wsi-swapchain.c) の所有 job / queue worker と [queue.c](../../../userland/base/libvulkan/queue.c) の locked submit。配列・矩形をコピーし、実 native fence 完了後に display present / Wayland commit | WSI 3 profile の通常・sanitizer、direct-003 / Wayland-007 の再作成・終了・競合 | 返却済み pResults を書かない。acquire は buffer release、idle は受理済み work の drain、destroy は worker と資源を回収。後発 error は後続操作へ伝える。pending producer loss は修正後 003 で PASS。修正前 002 の失敗を保持し、任意の終了順序の全面保証にはしない |
| P6 session admission | [gpu.c](../../../src/drivers/gpu/gpu.c) は通常操作を waitq で待たせ、競合だけで即 EBUSY にしない。完了 / fence / topology 照会は必要な待機から分離 | [gpu-admission app](../../../userland/base/tests/gpu-admission/main.c)、Wayland-007 の 4 thread × 32 lifecycle / 4096 bytes 照合 | VkQueue の標準 external synchronization 要件は維持。実試験で露呈した amd64 pthread 初期 SP の C ABI 不一致は libc で修正し、HAL は変更していない |
| P7 allocation 共有 | [gpu.c](../../../src/drivers/gpu/gpu.c)、[share.c](../../../src/drivers/gpu/venus/share.c)、[memory.c](../../../userland/base/libvulkan/memory.c) で full allocation と不変 metadata を共有。buffer / optimal image と scanout 適合性を分離 | Wayland-007 の linear / buffer / optimal、生成元終了後の独立 renderer context と 1024 pixels 照合、sharing / external-memory fixture | 標準 OPAQUE_FD は同一 renderer の UUID / type / size を検証。任意の異種 GPU 共有ではない。Intel ANV optimal の受入には下記 paired host patch が必須 |
| P8 transport 並行 / 回復 | [transport.c](../../../src/drivers/gpu/venus/transport.c)、[venus.c](../../../src/drivers/gpu/venus/venus.c) に request ごとの DMA、3 marker＋予約 decoder slot、out-of-order 完了、watchdog、failed DMA quarantine、旧所有者退役後の checked reset / fresh open | transport fixture、recovery-003 の timeout / peer failure / retirement gate / fresh 4096 bytes | reset acknowledgement 前に DMA を解放しない。実証は隔離 renderer 停止条件。pending producer loss は final-close 順序を修正して 003 で PASS。修正後kernelのrecovery-004もPASS |
| P9 zwl | [zwl](../../../userland/base/zwl/) の全画面テストドライバを維持。イベントループ内の同期 present、1 パス 1 surface、成功置換・解除まで front を保持 | Wayland-007 の複数 client / lifecycle と既存 protocol / ownership fixture | 一般合成 compositor、入力、装飾、常駐サービス化を追加しない。現在の同期構造を今回の未達としない |
| P10 libwayland | [libwayland](../../../userland/base/libwayland/) の固定 typed listener、未知 protocol の dispatcher 経路を維持 | 既存 protocol と今回の実 Wayland WSI 往復 | wl_shm / wl_seat、一般 Toolkit / 全 protocol 互換は承認済みの対象外 |
| P11 demo / 診断 | [vkdemo/renderer.c](../../../userland/base/vkdemo/renderer.c) は通常 readback/hash を除き、明示診断だけ有効。[gpu-share app](../../../userland/base/tests/gpu-share/main.c) を package 内へ移し plan source への build 依存を解消。独立 Venus codec は transport 診断として保持 | direct-003 の通常 13 frame / VNC 2 画像、6 診断画面。Wayland-007 と admission app | vkdemo は標準 Vulkan app。wltest の検証用逐次待機は維持。通常描画の速度や同時 client の frame rate を一般化しない |

Wayland の cross-context 依存は U 側 worker が実 native Vulkan fence の完了を待ってから commit することで満たす。zwl の fenced scanout が client の未完了 render を暗黙に待つという前提は置かず、private protocol の意味も変更していない。

## 追補 B/C/E/F/G の処置

| 項目 | 処置・主な実装ソース | 証拠 | 限界 |
| --- | --- | --- | --- |
| B fence handle fd | [kernel fence](../../../src/kern/fence.c)、[gpu-fence UAPI](../../../include/uapi/gpu-fence.h)、[external-fence.c](../../../userland/base/libvulkan/external-fence.c)。型付き payload / generation / owner、dup / SCM_RIGHTS、poll、create/query/wait/reset/bind/signal、command / display wait・signal を接続 | kernel-fence / gpu-fence / fd reuse / external-fence fixture、Wayland-007 の通常共有 | native fence の成功確認後だけ K signal。display signal は scanout selection 完了で、front buffer の再利用許可ではない。**pending producer 終了は 002 FAIL を保持し、retire-before-drain 修正後の 003 は PASS** |
| C 標準 external memory | [external-properties.c](../../../userland/base/libvulkan/external-properties.c)、[memory.c](../../../userland/base/libvulkan/memory.c)、[Vulkan 公開 header](../../../libc/include/vulkan/) に標準 OPAQUE_FD query/export/import。成功時だけ入力 fd を消費し、UUID / type / size と public bounds を検査 | 独立 ABI / DSO、external-memory fixture、Wayland-007 の buffer / optimal | native query が許さない profile を広告しない。dedicated-only の未実装 profile は拒否。公開 170 API を維持し、CTS / Vulkan 全体の適合を主張しない |
| E renderer / display の組 | [gpu-scanout.h](../../../include/uapi/gpu-scanout.h)、[gpu.c](../../../src/drivers/gpu/gpu.c)、[wsi-display-nodes.c](../../../userland/base/libvulkan/wsi-display-nodes.c)。query 30 の role / identity / companion、(node id, local display id) の表示 identity。display-only を VkPhysicalDevice にしない | renderer-only / display-only / companion / default / 同一 local ID / fd 寿命 fixture | node 一覧は初回 instance discovery で保持。既存 node の output / generation は再照会するが、新 GPU node の自動発見は次 instance。異種 GPU 実機は未検証 |
| F import と経路決定 | optional scanout.import_image が source backing の DMA / cache / extent / format を検証。core は destination destructor 後まで source handle を pin。WSI 作成時に共有 import を試し、非対応なら広告済み COPY 経路へ一度だけ固定 | gpu-scanout の元 fd / open 終了後 pin、copyout rollback、driver 拒否 / final put。WSI の部分 rollback / OOM / 固定経路 fixture | ENOTSUP / EXDEV / 適切な ENOTTY の非対応と、OOM / device loss を区別。Venus は foreign physical backing を公開せず任意の別 controller import を拒否。実 foreign DMA は fixture 範囲 |
| G capability / topology / 配置 | [GPU UAPI 表](gpu-uapi-contract.md) を統合。旧 ABI version 1 / layout を保持し、内部 ops v6。ioctl 32 は 40B events QUERY / exact ACK と POLLPRI、ioctl 33 は 64B placed blob と具体 backing 条件。旧 blob 40B は維持 | kernel-final JSON の topology / placement / transport 通常＋ASan/UBSan、placement supplement、WSI route / ILP32・LP64 fixture。Wayland-007 の初期 events、direct-003 の通常 BLOB | 物理 hotplug は未実証。DMA32 / contiguous / coherent / max DMA / base alignment を無視した成功は禁止。Venus の非零物理条件は ENOTSUP。VK_EXT_display_control 全 API、physical placement allocator、HAL cache 変更を追加した意味ではない |

Topology の QUERY は非破壊で、成功 copyout 後だけ observed / ACK を進める。`QUERY → output/generation 再照会 → ACK(S)` 中の新 event は古い ACK で消えない。独立 open は別 cursor、dup / SCM_RIGHTS は同じ open description の cursor。INTx / MSI-X の config event、command completion との共存、copyout 失敗、sequence 飽和を fixture で確認した。

配置条件は `vulkan_wsi_shared_image_create` → `vulkan_memory_allocate_placed` → `memory_export` → ioctl 33 に渡す。条件 pointer は同期作成中だけ使用する。非零条件に未対応の backend は ENOTSUP、零条件は旧 allocator。Venus HOST3D allocation を guest の物理連続 / DMA32 memory と偽って扱わない。現在の WSI query に physical base alignment の項目はなく、この値は 0 を渡す。row pitch / image offset alignment は別に実 layout を検査する。

## 限定検証と生成物

結果は既存 JSON と記録された log hash に対応する。古い失敗記録を削除して「初回から PASS」としない。

| 検証 | 結果・範囲 | 証拠 |
| --- | --- | --- |
| userland 13 job | 全 PASS。通常＋ASan/UBSan。Noct 8 file が byte 一致、137 core＋20 WSI＋13 external/query = 170 command、145 native opcode、100 encoders / 47 decoders、44 recording functions。ILP32 / LP64 で 148 type / 942 field / 2394 constants | [userland-final-verification.json](userland-final-verification.json)、[説明](userland-final-verification.md)。実行中 140 入力の hash 変化なし。後続配置追加の前の記録であり、その追加分は次の supplement に従う |
| K / fd / share / display matrix | 初回 10 件のうち kernel-fence、gpu-fence、gpu-fence-reuse、gpu-sharing、gpu-scanout、gpu-framework、handle-fd は PASS。Venus backend / sharing / EDID の初回 compile/link 失敗は保存し、fixture 接続と対象 source 修正後の通常＋ASan/UBSan は 3 件とも PASS | [kernel-final-verification.json](kernel-final-verification.json) の initial_matrix と additional_or_corrected_runs |
| producer final close | 新 `run-gpu-fence-close-test.sh` は修正前 FAIL、修正後通常＋ASan/UBSan PASS。最終参照より前は pending、drain 入口では error、callback / completion / resource は drain 後まで存命。late success は error を覆さない。既存 GPU fence suite も PASS | [transport-sync.md](transport-sync.md)、`/tmp/q310-fence-close-before.log` / `-after.log` / `-existing.log`。実測は fence-exit-003、独立 deadline は [producer-exit-deadline.md](producer-exit-deadline.md) |
| topology core / IRQ | `run-gpu-topology-test.sh` と `run-venus-transport-test.sh` が通常＋ASan/UBSan PASS。exact ACK、copyout failure、途中 event、独立 open / alias、admission bypass、INTx read-to-clear / MSI-X、config-only で偽の command completion を作らないこと、lock 外 wake、飽和を確認 | 同 JSON の gpu-topology / venus-transport。元 log `/tmp/q310-gpu-topology.log`、`/tmp/q310-transport-topology.log` と hash を保持 |
| K 配置 | `run-gpu-placement-test.sh` が通常＋ASan/UBSan PASS。実確保 storage と独立した DMA page metadata で endpoint / 連続性 / coherence / base alignment、不適合解放、copyout rollback、旧 backend 拒否、旧 40B / 零条件互換を検証 | 同 JSON の gpu-placement、`/tmp/q310-gpu-placement.log`。実機 DMA allocator の試験ではない |
| context / memory 配置 | `timeout 120 sh plan/ws030/tests/run-libvulkan-context.sh`、`timeout 120 sh plan/ws030/tests/run-libvulkan-external-memory.sh` が通常＋ASan/UBSan PASS。NULL legacy / 非 NULL zero placed64、独立 offset と 64-bit max DMA / alignment、ENOTSUP / ENOTTY / ENOMEM rollback、EIO terminal 留保 | [placement-verification.json](placement-verification.json)、[説明](placement-verification.md)。ENOTTY 再試験も入力 hash 不変、native free exactly 1、placed ioctl exactly 1、legacy 再試行なし |
| WSI / display | async job / private fence 3 slot、実 producer wait、caller 配列寿命、crop / scale / black alpha、acquire / idle / destroy、部分 import rollback、shared / COPY 経路固定、配置伝播を通常・sanitizer で確認。node / native / scanout / pacing fixture も PASS | [display-wsi.md のコマンド・証拠一覧](display-wsi.md)、`/tmp/q310-wsi-placement.log` ほか。同書の fixture 範囲を実機性能と混同しない |
| target / ABI / 規約 | 影響する WSI / K / memory / app の target syntax、WSI 6 source の target ABI analyzer、旧要求と events40B / placed64B の ILP32 / LP64 size / offset / ioctl encoding を確認。対象 diff の空白検査 PASS | [display-wsi.md](display-wsi.md) の対象・log 一覧。aggregate `make check` は実行していない |

初回 K matrix と追加 6 実行の JSON は初回失敗も保持している。単に job 数を足して「全 16 件初回 PASS」とは記載しない。full userland suite 後の配置追加については専用 supplement と統合 image の結果を用いる。

## native OPAQUE のホスト依存

**今回の Intel ANV optimal allocation 共有は、stock virglrenderer だけでは未達。隔離ディレクトリ内の paired proxy library / render server patch が必要である。** native query では optimal RGBA8 / BGRA8 の DMA_BUF は FORMAT_NOT_SUPPORTED、OPAQUE_FD は import/export 可能で、原因は dedicated-only 要求ではなかった。未実装 dedicated profile は public query で拒否し、公開 API 数を増やして回避していない。

[renderer-opaque/README.md](renderer-opaque/README.md) と [provenance.json](renderer-opaque/provenance.json) に source、許諾、build、組の交渉、fd 所有権、native metadata の照合を記録した。既存 160B capset prefix を維持し、正確な 168B suffix と proxy/server INIT handshake が一致した組だけ native OPAQUE を広告する。stock / 不一致時に機能を偽って有効にしない。独立レビューで見つけた INIT mismatch の切断伝播も修正し、wrong magic / flags は即 EOF を確認した。

| 成果物 | SHA256 |
| --- | --- |
| [virglrenderer-1.1.0-opaque.patch](renderer-opaque/virglrenderer-1.1.0-opaque.patch) | `04def7cd3fd9e50fad62ff98298a9e34880ce793de4b5942d5a908dd0132379f` |
| libvirglrenderer.so.1.9.0 | `3f692e604f7653153b6b7340afa0f97e65ee78956babe004eb7d0b8ead8a6a26` |
| virgl_render_server | `c9383cef62253c995cb067d7e1eb11c29d801bdd3514fa5d0dcbdb26e9056f0d` |

上記の実 QEMU result は指定 library が QEMU に map されたことを記録する。最終4実行はlibraryの開始・終了hash一致も確認済み。system library、package、ldconfig、ホスト通常表示を変更していない。guest の公開型は標準 OPAQUE_FD / kernel handle fd であり、guest に Linux dma-buf / DRM を導入したわけではない。patch のない stock 構成、任意 host Vulkan driver、任意 renderer revision での optimal 共有を成功済みとしない。

## 引き渡しと範囲

p004 / native i915 に渡す契約は [gpu-uapi-contract.md](gpu-uapi-contract.md) にまとめた。物理 DMA / IOMMU / foreign GPU、実ケーブル hotplug、物理 vblank は fixture を超える未検証範囲として残す。VK_EXT_display_control 全体、external semaphore fd、EGL / GLES、一般 Wayland / Toolkit、native display driver は今回の追加実装としない。

今回の作業で追加 HAL 変更は行っていない。`git add` / `commit` / `push` は行わず、資料と source の Git 公開はユーザーが担当する。GitHub の計画同期と repository の Git 公開は別に扱う。最終結果をPhase / Queue / MasterとGitHub Issues/Projectへ同期する。

fence-exitの修正前失敗、Kの順序修正、独立deadline、修正後の実waitを保存し、同kernelのWayland-008 / direct-004 / recovery-004と現在source/imageの一致まで確認した。p007をcleared、q310をfinishedとして引き渡す。p004/WS029を自動実行しない。
