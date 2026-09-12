# p006 GPU allocation共有とnative表示の実装記録

対象はWS014 p006 / q309-i01。amd64・同一Venus GPU・独立process/contextでの共有と全画面表示を実装した。Phase全体の最終受入は[phase.md](phase.md)で管理する。本書は新経路の契約と制約をp004へ引き渡すための記録であり、正式なVulkan CTS認証や全層のzero-copyを意味しない。

## 共有するものと公開境界

`VkImage`と`VkDeviceMemory`のハンドルは各processのrenderer contextに属する。process間で渡すfdが保持するのは、独立したGPU allocationとその不変の画像記述である。receiverは`GPU_RESOURCE_IMPORT`で自分のcontextへ資源をattachし、自分の`VkImage`を作成してimportしたmemoryへbindする。元のGPU session全体は共有しない。

[GPU UAPI](../../../include/uapi/gpu.h)の構造体はILP32/LP64で共通であり、Kポインタを含まない。

| 構造体・操作 | 固定サイズ | 契約 |
| --- | ---: | --- |
| `gpu_image_descriptor` | 64 bytes | version/size、width/height、format/stride、offset/allocation_bytes、memory_type/usage/tiling、reserved、device_id |
| `GPU_RESOURCE_EXPORT` / `gpu_resource_export` | 88 bytes | session resource handleとdescriptorを入力。fdは-1、入力device_idは0。成功時に独立fdとK確定descriptorを返す |
| `GPU_RESOURCE_IMPORT` / `gpu_resource_import` | 96 bytes | version/size/fdだけを入力し、他は0。独立session handle、native resource ID、Kのdescriptorを返す。入力fdは消費しない |

初回exportで確定したdescriptorは後続export/importで変更しない。device_idはGPU共通層が割り当てる世代で、別GPUへのimportは`EXDEV`とする。fdの型とGPU共有opsも照合する。fdへのexportは予約→copyout→commitの順であり、copyout失敗から公開済みfdをcloseする方式は採らない。

共通層は[drv_gpu_share_ops](../../../include/drivers/gpu-share.h)を既存の動的`drv_gpu_ops`登録へ追加した。callbackはexport、import、最終releaseの必要な境界に限定する。登録・PCI接続の仕組みをGPUだけの特別な管理方式へ置き換えない。

## fd、context、scanoutの寿命

[GPU共通層](../../../src/drivers/gpu/gpu.c)は汎用`kernel_handle`を使用する。fd tableとSCM_RIGHTSが同じhandleの強い参照を保持するため、VFSのinodeや匿名file wrapperは不要。最後のhandle参照でGPU共有payloadを一度解放する。fd参照だけではGPU資源の全寿命を決めない。

[Venus共有層](../../../src/drivers/gpu/venus/share.c)では、初回export時にallocationをsessionから独立した保管オブジェクトへ移し、元sessionにはaliasを残す。各contextの最初のaliasで`CTX_ATTACH_RESOURCE`、最後のaliasで`CTX_DETACH_RESOURCE`を行う。同じcontextで複数回importしても早期detachしない。転送中handle、各alias、表示中scanoutが独立にallocationを保持する。

producerのfd close・GPU session close・process終了後もreceiverのaliasで使用できる。最後のaliasを破棄しても表示中ならscanout参照が保持する。表示差し替えまたはlease解放の成功後に以前の表示参照を落とす。不確実なhost完了では資源を再利用せず、checked resetまで保持する。GPU共通層の共有数はsessionが0でもunregisterを阻止する。

## GPU内経路と同期

通常のWayland経路は次の順序で進む。

1. アプリがoptimal imageへ標準Vulkanで描画する。
2. libvulkanがGPU copyで共有linear imageへ移し、`VK_QUEUE_FAMILY_EXTERNAL`へのreleaseと実GPU fence完了を待つ。
3. WSIの独自`zed_gpu_buffer_v1` factoryがallocation fdと64-byte descriptorをSCM_RIGHTSで別processの`zwl`へ渡す。
4. `zwl`が自分のGPU contextでimportし、`GPU_DISPLAY_PRESENT`の`FIFO | BLOB`でnative表示へ渡す。
5. Venusが`SET_SCANOUT_BLOB`と`RESOURCE_FLUSH`を発行する。前のbufferを表示から外せた後に`wl_buffer.release`を返し、clientが再利用前にEXTERNALからacquireする。

この経路にはCPU readback、CPU memcpyによる画素転送、`TRANSFER_TO_HOST_2D`による再uploadを必須としない。fdの受け渡し自体はGPU fenceではない。frame callbackもbuffer再利用の許可ではなく、表示中bufferは次の表示または明示解除まで保持する。コンポジタがrendererでsampling/copyする場合も、自分のcontextでmemory import・image bind・external ownership acquire/releaseが必要になる。

[WSI image実装](../../../userland/base/libvulkan/wsi-image.c)はrendererのmemory requirementsとrow pitchを照会する。共有blobは`SHAREABLE | CROSS_DEVICE`で作成し、CPU用の`MAPPABLE`やMMIO apertureを要求しない。ホストVenus rendererが使用する外部memory種別にdma-bufを含むことと、ゲストがLinux dma-buf ABIやlinux-dmabuf-v1を採用することは別である。ゲストの公開契約はtyped kernel handle fdである。

[表示実装](../../../src/drivers/gpu/venus/display.c)の従来copy経路もGOP framebufferへの書き込みではない。従来はCPU pixelsをvirtio 2D resourceへ渡し、`TRANSFER_TO_HOST_2D`→`SET_SCANOUT`→`RESOURCE_FLUSH`で表示する。p006の共有blob経路と、既存console/vkdemo用のcopy経路を両方保持した。追加HAL変更はない。

## 初期対応範囲とモード情報

- 共有表示は同一GPU、1 planeのlinear RGBA8888/BGRA8888、各辺16〜4096。strideは4-byte整列で幅を収容し、offsetと全行がallocation内に入る。memory type、usage、tilingと予約値も検証する。
- 別GPUへのimportは拒否する。初版Wayland factoryにはコンポジタのGPU identity広告がなく、presentation-support照会だけで複数GPU間の適合性を確定できない。今回の受入は単一GPUとする。
- `GET_DISPLAY_INFO`から実scanout数、接続状態、推奨寸法を取得する。EDID featureを交渉できた場合は`GET_EDID`で取得し、header、長さ、baseと宣言されたextension全てのchecksumを検証する。
- EDIDのbase/CTA progressive detailed timing descriptorを列挙し、pixel clockとblankingからnominal refreshを算出する。重複は除き、1〜100Hzを扱う。standard/established timing、DisplayID、interlace、100Hz超は初版対象外。
- EDIDがない・無効・未対応の場合はGET_DISPLAY_INFOと既存50,000mHzを使用する。custom extentと320×240 console互換を保持する。`GPU_DISPLAY_MODE_VALIDATE`へrefresh=0を渡すとbackendが適切なnominal値を選択して返す。
- 明示したrefreshは1,000〜100,000mHzのguest pacingとして扱い、custom framebuffer寸法とEDID寸法の一致を要求しない。virtioのSET_SCANOUT/SET_SCANOUT_BLOBはresource内の矩形を選び、物理pixel clockやblankingをprogramしない。EDID列挙はnative推奨値の発見に使用する。
- guestの時計は100Hz。分数tickを持ち越して60Hz要求を平均60Hzへ近づけるが、nominal EDID refreshと実hostの物理vblankは同じ保証ではない。host fenceはscanout選択/flush完了の境界であり、物理表示の完了時刻を保証しない。

## 検証と証拠

実装後の限定fixtureは通常とASan/UBSanで確認した。試験用peerと実QEMUの証拠は分ける。

| 検証 | 確認した境界 |
| --- | --- |
| `run-gpu-framework-test.sh` / `run-gpu-sharing-test.sh` | 実GPU共通層とhandle/fd、ABI配置、型・範囲・別GPU拒否、copyout rollback、producer close、最終参照とunregister |
| `run-venus-sharing-test.sh` | 実Venusコード、別context attach/detach、同context複数alias、GPU-only資源、blob scanout保持、不確実完了 |
| `run-venus-edid-test.sh` / `run-venus-transport-test.sh` | optional EDID交渉、base/CTA DTD、破損時fallback、refresh上限、60Hz分数tick、queue/reset |
| WS030 `run-gpu-display-test.sh` / `run-venus-console-test.sh` | 既存display/consoleのlease・復帰・停止境界とrefresh自動選択 |
| WS030 `run-libvulkan-memory.sh` / `run-libvulkan-context.sh` | q309規約整理後もwire、allocation/map、rollback、独立reply、有限timeoutを維持 |

`gpu-share-client.c`は検証専用private helperを使い、producer終了後の別renderer context import→GPU copy→1024画素のreadback照合を行う。このreadbackはoracle専用で、通常のWSI表示経路にはない。標準アプリ`wltest`へprivate GPU ioctlやVenus wireを入れていない。

[q309-wayland-001](../temp/remote/q309-wayland-001/result.json)はQEMU 10.0.11 / virglrenderer 1.1.0 / Intel ANVでPASS。producer終了後の1024画素、およびFIFO/MAILBOX各6枚・計12枚の320×240 VNC画像が独立期待値と全画素一致した。独立contextでのrenderer importと、zwlの直接blob scanoutはそれぞれ別に実証した。通常終了・swapchain再作成も確認済み。初回実測後のlifecycle追加確認、最終build、全体受入は[checkpoint001](checkpoint001.json)以降のPhase記録に従う。

transport負例fixtureの既存256MiB入力は、q308時点でproduction上限256MiBになっていたため「上限超過」の前提を満たさず失敗した。負例を512MiBへ修正して拒否-before-mapの期待を保ち、production上限は変更していない。失敗を隠す期待値緩和は行っていない。


## 実EDIDとdirect互換の最終確認中に判明した修正

[q309-wayland-003](../temp/remote/q309-wayland-003/result.json)は43.887秒でPASS。実際のGET_DISPLAY_INFO/GET_EDIDによる列挙はpreferred=1280×800、refresh=74,994mHz、physical=320×200mm、flags=47で、mode index 0も同じ値だった。custom320×240のWayland表示はrefresh=50,000mHz、BLOB flags=3を使用し、12枚の全画素一致、SIGKILL直後の640×480 console復帰、client/compositor再起動を確認した。

その後の[q309-direct-001失敗ログ](../temp/remote/q309-direct-001/evidence/console.log)では既存vkdemoが4.812秒・frames=0、`api=direct display surface result=-3`で停止した。vkdemoは列挙された74,994mHzをcustom320×240にも指定するが、追加したdriverの検証が50Hz以外にEDID寸法の完全一致を要求していた。virtioは物理timingを設定しないため、この制限には実装上の根拠がなく、native推奨値の列挙と仮想framebuffer設定を不当に結び付けていた。

修正ではMODE_VALIDATE、通常copy present、blob presentを既存の共通寸法・1〜100Hz cadence検証へ揃え、EDID一致を要求するhelperを除いた。refresh=0の自動選択、EDIDの実列挙、100Hz超拒否、分数tick pacingは維持した。vkdemoへ50Hz定数を追加せず、EDIDも隠していない。

`venus-edid.c`の追加fixtureは先頭DTDの640×480/60Hzを列挙し、周波数を維持してcustom320×240へ変更、通常SET_SCANOUTで表示する。続いて実測値74,994mHzでも表示し、999mHz/100,001mHzはcommand送信前に拒否する。旧fixtureの「別寸法に64Hzを使うとEINVAL」という期待も上記の誤った契約に従っていたため、理由を明記して成功へ訂正した。元の実QEMU失敗証拠は保持する。修正後のEDID fixtureとGPU共有/scanout fixtureは通常・ASan/UBSanでPASS、amd64 display object buildもPASS。修正後の実QEMU再受入はrootのPhase記録で追跡する。

## q309最終受入

最終kernelでq309-direct-002とq309-wayland-004を受入済み。先行実測時点の未完了記述は履歴として保持し、現状は[結果と失敗履歴](results.md)および[最終証拠](final-evidence/verification.json)を参照する。p006 cleared、p004はplanning・未queue。
