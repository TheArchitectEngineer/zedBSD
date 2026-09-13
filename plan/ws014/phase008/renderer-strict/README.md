# p008: 実 native fence に基づく isolated renderer

virglrenderer 1.1.0 の既存 p007 OPAQUE_FD pair に、GPU完了をKの責任へ移すための strict queue 契約を追加した。ゲストの公開Vulkan API数を増やさず、QEMU自身の変更も不要である。host library/serverはprivate dependency directoryに新規構築し、system library/package・表示サービスを変更していない。

## 成功を区別する理由

stock `src/venus/vkr_queue.c` はmarkerごとに別のempty native QueueSubmitを発行し、そのfenceをwaitする。DEVICE_LOSTも非TIMEOUTの結果として通常retireされ、proxy切断時にもpending fenceをforce-retireする。QEMUのretire callbackにはVkResultがないため、このままではKがmarkerを成功証明にできない。

本差分はproxyとserverの組で次を保証する。

| 境界 | strict動作 |
| --- | --- |
| native QueueSubmit / QueueBindSparse / QueueSubmit2 | 実VkResultが成功なら、その呼出しに付いた非NULL native VkFenceを次のmarker用に保持する。前のhandleを受理失敗時に再利用しない |
| 非zero timeline marker | 直前の成功submissionに付いた実fenceを借りる。独自のempty submit/CreateFence/ResetFencesを追加しない |
| wait | timeoutはpending。VK_SUCCESSだけを正常retireとし、そのfenceへの最後のaccessが終わってからcallbackする |
| native DEVICE_LOST・wait失敗・fence不在 | contextのqueue failureをstickyにする。後続の正常retireを抑止し、より大きいwatermarkが失敗した仕事を覆うことを防ぐ |
| worker teardown | threadをjoinしてから借用対象のnative objectをdestroyする。未確認pendingを成功としてretireしない |
| proxy切断 | 確認済みwatermarkのprefixだけをretireし、pendingをforce-retireしない。非同期loopはHUP/ERR/NVALで終了してspinしない |

各queueのhost workerはFIFO listの先頭fenceから順にwaitする。そのため、先行BindSparseが未完了なら、後続ordinary fenceが既にsignaledでもmarkerの成功順序は追い越さない。これは実native fenceの範囲に加えたhostのretire契約である。guest側はnative submissionの前にK jobを予約し、必ず実fenceを付け、native成功後にcommitする。borrowされたfenceのReset/DestroyはK終端後だけにする。この両側の契約が前提であり、任意の未対応guestにstock同等の動作を約束しない。

CPU0はdecoder完了として保持する。host失敗のexact VkResultを新しいQEMU sidebandで運ばず、失敗したqueue markerを正常ACKしないことと、Kの自律10秒watchdogによるERRORを組み合わせる。実transport契約は [transport-sync.md](../transport-sync.md)。

## profileと互換性

capsetの先頭160Bは維持。全長**168B**、offset160 little-endian magic=`0x5a424453`、offset164 flags=`3`（OPAQUE=1、STRICT_QUEUE=2）。proxyは同じmagic/flagsをserverのINIT ACKで確認した場合だけ広告する。

p007のflags1 pair、stock短いINIT、未知magic/flagsは拒否する。新libvulkanはstrict profileとGPU_CAP_JOBをdevice初期化条件とする。旧hostでもkernelのlegacy 2Dと能力照会は可能だが、Vulkan動作互換は今回保持しない。native OPAQUE memory metadata・fd lifetimeとprivate WSIのnative DMA/CROSS_DEVICE経路はp007差分をそのまま含む。

## 再構築と成果物

[provenance.json](provenance.json) は各変更ファイルのbefore/after SHA256、patch hash、実installのhashを記録する。元sourceの個別license headerを保持し、[COPYING.virglrenderer](COPYING.virglrenderer) を保存した。

- [virglrenderer-1.1.0-strict.patch](virglrenderer-1.1.0-strict.patch): 未変更の固定1.1.0に適用する全18file差分。p007 OPAQUE差分も含む。
- [opaque-to-strict.patch](opaque-to-strict.patch): p007の固定OPAQUE sourceへの追加11file差分。全差分と重ねて適用しない。
- [patch-dry-run.log](evidence/patch-dry-run.log): 全差分を未変更の固定sourceへdry-runし、全file成功。

元sourceは [virglrenderer 1.1.0](https://gitlab.freedesktop.org/virgl/virglrenderer/-/tree/1.1.0)。ローカル固定baseline `/tmp/q308-virglrenderer-1.1.0` と、p007のbefore hashを引き継ぐ。p008 sourceは `/tmp/q311-virglrenderer-1.1.0-strict`、private host上は `/home/awe/zedbsd-q306-venus/dependencies/q311-strict/source`。

```sh
renderer_dep=/home/awe/zedbsd-q306-venus/dependencies/q311-strict
export PYTHONPATH=/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/python
python3 -m mesonbuild.mesonmain setup \
    "$renderer_dep/build" "$renderer_dep/source" \
    --prefix="$renderer_dep/install" \
    -Dvenus=true -Dplatforms=egl -Dtests=false -Dvideo=false \
    -Drender-server-worker=process -Dminigbm_allocation=false \
    -Dbuildtype=release
ninja -C "$renderer_dep/build" -j2
python3 -m mesonbuild.mesonmain install -C "$renderer_dep/build" --no-rebuild
```

| 成果物 | 値 |
| --- | --- |
| library directory | `/home/awe/zedbsd-q306-venus/dependencies/q311-strict/install/lib/x86_64-linux-gnu` |
| library | `libvirglrenderer.so.1.9.0` |
| library SHA256 | `05c7499c9fab287f9692ac37f169b12b5bcdd98fd553f42bca5a9afeef8cda86` |
| server | `/home/awe/zedbsd-q306-venus/dependencies/q311-strict/install/libexec/virgl_render_server` |
| server SHA256 | `f668a5262335790cf7b78ab0b732391bc790e8835dd27f2b2cbfcad561ddd80f` |

[build-0.log](evidence/build-0.log)、[build-1.log](evidence/build-1.log)、[build-2.log](evidence/build-2.log) はconfigure/build/installを記録し、すべてexit0。serverは静的なlibvirgl.aを含み、QEMUは同じsourceからのshared libraryを使用する。[build-identity.json](evidence/build-identity.json) で全変更sourceと両install成果物のremote hash一致を確認した。

## 独立意味fixture

[tests/run.py](tests/run.py) は実Mesonのcompile command/configを使い、実 `vkr_queue.c` / `proxy_context.c` をfixtureへlinkする。releaseの`-DNDEBUG`を除去し、fixture内のcompile-time guardでassertが無効なら失敗させる。native Vulkan呼出しだけをscripted oracleにし、copyした実装で合格させない。各compile/runを120秒に制限し、実GPUや他processに操作しない。

```sh
python3 "$renderer_dep/tests/run.py" --dependency-root "$renderer_dep"
python3 "$renderer_dep/tests/handshake.py"
```

[results.json](evidence/results.json) は通常・ASan/UBSanの各queue/proxy、計4profileすべてPASS。

- QueueSubmit/BindSparseの実handle、成功、TIMEOUT→成功、native/wait失敗、sticky後続拒否、非NULL fence不足、unchecked teardownの非成功。
- pending sparse fence→後続の別native fenceがreadyでも、先行成功まで後続callbackを出さない。markerによる余分なnative submitが0件。
- 実private SOCK_SEQPACKET pairで、確認済みprefixだけretire、socket閉鎖後のstock force-retire閾値でもpending保持、async HUP loop終了。
- 実serverの [handshake.json](evidence/handshake.json) はstrict3 ACK、old1・unknown7・badmagic・stock短sizeのEOF拒否、5case PASS。GPU contextを作らない。serverのexit255は既存のclient終了経路であり、ACK/EOFの合否と別に記録する。

初期fixtureのrelease `-DNDEBUG`、link不足、fixture object ID初期化漏れは試験構築時に修正し、PASSとは扱わなかった。結果は上記のassert有効・実source link後の最終実行である。fixtureはnative GPU DEVICE_LOST自体を実機で誘発した証拠ではなく、当該結果を受け取ったproduction分岐の意味検証である。

## 実QEMUと制限

rootの実 `q311-direct-001` は41.325秒、BLOB126要求、対象320×240 legacy scanoutなし、画像・lifecycle・console・競合の受入PASS。同じ固定pairでWayland、producer-stop、producer-exit、renderer停止/recoveryもPASSとrootから報告された。最終stable guestの正確なrun/hash/性能値はowning Phaseの結果資料を参照し、ここで別のclearanceを作らない。

host sourceに対する固定patchであり、任意upstream revision、任意Vulkan driver、正式CTSを保証しない。実 sparse-capable GPUの描画受入は行っていない。base systemの新規Cはrepository規約で確認し、license分離されたupstream patchは既存側のC11 atomics・構造を保持した。新HAL、host system package、ldconfig、GDM/VFIO、git commit/pushは行っていない。
