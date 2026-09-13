# WS014 p008: 実装と検証結果

承認済みのreview3回答A1–A8とfence所属の改善を、独立したWS014 p008 / q311-i01で実装・検証した。p007/q310の履歴とclearanceを維持し、p004と別WS029 native i915は未queueのまま。GitHubの最終状態は [同期証拠](final-evidence/github-sync.json) を参照する。

## 実装した変更

| 対応 | 結果 |
| --- | --- |
| fenceの所属 | `src/drivers/gpu/gpu-fence.c` / `include/drivers/gpu-fence.h` / `drv_gpu_fence_*`へ移動。kernは不透明handle/fd/refcount/poll/SCM_RIGHTSを所有する。共通DRIVER tagの取得後、GPU側でops identityを検査する。GPU backend選択時だけcoreとfenceをリンク。 |
| A1 | display切断・世代変更とtransport故障を分離。errnoだけから全deviceを故障扱いする処理を削除。backendが明示故障を通知し、callbackが0件でもwrapperの強参照で安全に報告する。Uの局所surface lossと永続device lossも分離。 |
| A2・A8 | no-reply batchingとreply/trailer/opcode/native VkResultの検査を保持。実commands＋contextで1,152,000-byteの合法なCopyBuffer record、End→Reset→再記録を確認。切断・wrong opcode・DEVICE_LOST・OOMを成功や盲目的再試行へ変えない。 |
| A3 | `GPU_JOB_RESERVE/COMMIT/CANCEL`でnative投稿前からKの期限管理を開始。actual native VkFenceの成功をstrict host markerへ結び、GPU frameworkがexact generationを終端。未投入・未commitのU依存fenceはK内の無期限待機へ入れない。 |
| A4 | 最大64descriptor/32slotへ拡張し、実queue上限に合わせて縮小。28 job＋4 controlを確保し、native投入後のCOMMITには追加allocation・容量待ちを不要にした。ring整列・範囲、逆順完了、wrap、IRQ、quarantineを検証。 |
| A5 | acquireはCLOCK_MONOTONICのcondition待機。状態検査とsleep登録を同mutexで行い、release/rollback/errorを通知。Wayland socket等の進行のため10msの有限間隔を残す。 |
| A6 | 同時jobのslotごとにpool/cbを保持し、完了後だけ再利用。RESET_COMMAND_BUFFER許可とBeginのimplicit resetで、定常の記録処理をBegin＋Endへ整理。pending slot、OOM、未受理、partial allocation、最後の回収を検証。 |
| A7 | external fenceごとのworkerを撤去。初期signaled/未使用/importでも監視thread作成0。presentは使用queueごとの遅延workerを維持。Venus watchdogは遅延起動・idle睡眠を維持。consoleは文字/主画面所有権/停止の通知で起き、100ms周期pollを撤去。 |

GPU UAPIは既存v1の要求layoutを維持し、別要求としてGPU_JOBを追加した。内部の`drv_gpu_ops`はversion7。標準Vulkan公開APIは170個を維持し、guest dma-buf/DRM/SYNC_FDは導入していない。

## 最終の実QEMU受入

private host `awe@10.0.10.25`、QEMU10.0.11/KVM、2vCPU/1GiB、i915/Intel ANV、virtio-vga-gl/Venus/256MiB hostmem、isolated strict rendererで実行した。以下は同じ最終kernel・base imageを使った独立VMで、すべてQEMU exit0。VM時間はbuild・転送を含まない。

| attempt | VM時間 | 受入 |
| --- | --- | --- |
| q311-direct-002 | 41.345秒 | 標準Vulkanのテクスチャ付き回転直方体6画面をGPU診断readback・VNC・独立oracleで照合。通常表示はreadback0で15frame。126 BLOB要求、対象320×240のlegacy要求0。終了、SIGINT、再open、console復帰と文字更新、別processの表示競合拒否・owner完走。 |
| q311-wayland-002 | 46.011秒 | FIFO/MAILBOX各6画面、計921,600画素の独立期待値と一致。128回同一fd並行操作、標準fence/linear/buffer/optimal共有、独立context import、topology QUERY/ACK、client/compositor終了・中断・再open・console復帰。12表示の実importとBLOB scanoutを照合。 |
| q311-producer-stop-004 | 14.117秒 | waitpidでSIGSTOPを確認し、producerのfdを開いたまま9970msでDEVICE_LOST。CPU workerもfinal closeも使わずdriver watchdogで終端。終端後だけSIGCONTし、最終waitpidを確認。 |
| q311-producer-exit-002 | 4.214秒 | eventで止まった未完了GPU仕事のproducer終了をERRORで通知。独立consumerの30秒試験期限より前に終了し、成功を捏造しない。 |
| q311-recovery-002 | 13.823秒 | 試験所有rendererだけを停止。10000ms watchdog、peer error、旧参照の退役gate、renderer再開、checked reset、新context4096byte往復・decoder成功。 |

通常の直接/Wayland表示はGPU copy/blit→共有linear画像→`SET_SCANOUT_BLOB`を使う。GPU結果のCPU読み出しは画像照合用診断と既存の明示fallbackに限り、GOP framebufferへのコピーへ戻していない。QEMUホストでのegl-headless/VNC captureはguestの通常表示経路と別である。

最終kernel SHA256: `92dc3985aa7da4829e08952f9f87aa915db13cf4c9da936156e95e3272e9883c`。
最終libvulkan SHA256: `495c880d78f734c8bb6b67e2400d8bfd1e10196bd0901a5d2a219285ba17756b`。
base image SHA256: `53c38ae24b5784af54028ad0d7279a992ab7576ceb4cb0068a2e49830374f59c`。

各payload・host・source hash、実argv、複数の検証結果は [最終検証台帳](final-evidence/verification.json) に保存した。大きな画像/console/trace/source manifestは`plan/ws014/temp/remote/<attempt>/`に保持する。最終snapshot後のproduction変更はない。

## 限定試験・ビルドと規約

- [GPU coreの9 suite](gpu-core-verification/verification.json) と [fd/共有/topology/scanout/配置の5 suite](gpu-core-verification/final-k-regressions.json) は通常・ASan/UBSanでPASS。型取り違え、producer権限、世代、callback/consume、fd再利用、SCM_RIGHTS/poll、close/drainとlate successを確認。
- [transport](transport-evidence/verification.json) と [実host queue/proxy](renderer-strict/evidence/results.json) の通常・sanitizer試験がPASS。実server INIT5case、実submission fence、先行sparseを追い越さないFIFO、host loss/切断/未確認終了の成功retire抑止を確認。
- [Uの12チェック](userland-verification/verification.json) と [console/textの3チェック](console-verification/verification.json) がPASS。U完了回収の2種類の競合は修正前FAIL→修正後PASSを保存。static analyzerの4警告は実calleeの失敗保持・Vulkan有効入力の前提を照合し、[判断記録](userland-verification/review.json) に残した。全警告0の主張はしない。
- 対象`make -j16 disk-image`、6プラットフォーム×GPU有無のmake入力、JOB ABIのILP32/LP64、[GPUなしamd64実ELF](gpu-core-verification/nogpu-amd64.json) がPASS。GPU/fence/Venusのsymbol/objectは0、汎用handle/fdは残る。全6アーキテクチャの実buildを行った意味ではない。
- [公開API・Noct検査](api-verification/verification.json) は170 dispatch/export、両ABI、8生成fileのbyte一致を確認。最終DSOのSONAME/170実exportも検査した。
- [全適用規約と独立レビュー](conformance.md) を実施し、差分の`git diff --check`はPASS。HAL追加変更、aggregate `make check`、git add/commit/pushは行っていない。

## 資源と性能の観測

| 項目 | p007 | p008 |
| --- | --- | --- |
| 最大descriptor / slot | 8 / 4 | 64 / 32、実能力に合わせ縮小 |
| GPU marker / 制御予約 | 3 / 1 | 28 / 4 |
| 4KiB丸め後のqueue backing最低値 | 299,008 bytes | 2,363,392 bytes |
| external fence監視thread | fenceごとに1 | 0 |
| present記録用pool/cb | 毎job作成・破棄 | 同時slot単位で再利用。3pending slotとその再利用をfixtureで確認 |
| 定常の記録通信 | pool/cb作成・記録・解放 | Begin/Endの2トランザクション。submit/表示/fence操作は別 |
| console待機 | 100msごとの起床 | text/主画面所有権/stopの通知 |

queueメモリは容量拡張に伴って増える。上表はallocation配置からの計算で、allocator metadataを含む実機RAM実測ではない。threadをなくす効果はthread/stack/TLSの確保要求の削減であり、常駐物理メモリの削減量は未測定。

保存済みp007 image/旧paired rendererとp008を、同じhost・同じ測定harness・通常2秒のシーンで比較した。[比較JSON](performance/comparison.json)

| 観測 | p007再測定 | p008最終 |
| --- | --- | --- |
| frame数 / 最終frame時刻 | 13 / 1910ms | 15 / 1880ms |
| QEMU processのCPU時間 | 0.36秒 | 0.48秒 |
| CPU測定window | 5.312秒 | 5.491秒 |
| QEMU CPU時間/frame | 0.0277秒 | 0.0320秒 |

CPUは`/proc/<owned QEMU pid>/stat`の全thread合計を、コマンド入力前からshell復帰まで測った値。初期化・表示・終了を含み、renderer子processとcapture clientは含まない。これは各1試行・100Hz分解能の短い観測で、CPU削減や定常FPS倍率の実証ではない。今回CPU値は増えており、資源再利用の正しさ・thread撤去・終端責任の改善と、速度の定量評価を区別する。

## 失敗履歴と限界

初回direct001/wayland001/stop003/exit001/recovery001の成功を保持し、最終U回収競合の修正・規約確認後に上記5VMで受け入れた。

- producer-stop-001: share側明示fault通知の引数型誤りでbuild停止。`&controller->transport`へ修正した。
- producer-stop-002: guestは9980msでPASSしたが、local harnessの新mode辞書漏れでouter FAIL。辞書と停止/期限proofの再検査を追加して003がPASS。失敗結果を上書きしていない。
- 限定fixtureの新opcode期待値、リンクpeer、非実行bitのrunner呼出し、誤ったhost compiler選択等の初期調整は実装成功として数えない。最終の実productionリンク・通常・sanitizer結果を採用する。

**このlibvulkanのVkDevice作成にはSTRICT_QUEUE対応のpaired rendererが必要。** exact168B capsetとflags3、proxy/server INITを合意し、stock/旧pairを新契約対応と推測しない。対応しない環境は`VK_ERROR_INITIALIZATION_FAILED`で拒否する。host system packageは変更せず、[隔離build・patch・provenance](renderer-strict/README.md) を保存した。

host library SHA256: `05c7499c9fab287f9692ac37f169b12b5bcdd98fd553f42bca5a9afeef8cda86`。
server SHA256: `f668a5262335790cf7b78ab0b732391bc790e8835dd27f2b2cbfcad561ddd80f`。

異なるGPUの任意DMA共有、native i915、物理vblank、実sparse-capable GPU、正式Vulkan CTS、一般Wayland/toolkitは本Phaseの受入ではない。zwlの同期1surface/passとlibwaylandの限定protocolは承認済みテストドライバの範囲を維持する。source/doc/patchのgit公開はユーザーが行う。

## 再現

`python3 plan/ws014/tests/run-vkdemo-remote.py` または `run-wayland-remote.py` に、新しい一意の`--attempt`、`--build-directory build/vkdemo-amd64`、`--config plan/ws014/tests/config-wayland-amd64.mk`を指定する。host pairは下記を明示する。

```
--render-server /home/awe/zedbsd-q306-venus/dependencies/q311-strict/install/libexec/virgl_render_server
--renderer-library-dir /home/awe/zedbsd-q306-venus/dependencies/q311-strict/install/lib/x86_64-linux-gnu
--timeout 180 --build-timeout 1200 --transfer-timeout 1200
```

通常受入は`--lifecycle`、障害試験はWayland wrapperに`--fault-test producer-stop`、`producer-exit`、`recovery`を一つずつ指定する。各試験は独立した使い捨てVMで実行する。実際の全argvは最終台帳に保存している。
