# Stock renderer互換性の限定検証

p009ではSTRICT_QUEUEを維持する。固定virglrenderer 1.1.0の普通の描画が常に壊れるという結論ではなく、現zedBSDの「K共有fenceの成功・GPU資源の再利用・独立context停止」に必要な完了情報をstockのcallbackだけから得られないためである。

## 実コードで確認した境界

| 範囲 | 固定実装とfixtureで確認した結果 | 扱い |
| --- | --- | --- |
| 普通のsubmit | markerがアプリ指定fenceとは別のnative fenceを作り、追加の空QueueSubmitを行う | 通常成功の経路は存在する。アプリfenceの直接確認とは区別 |
| sparse | BindSparse後も同じ追加の空QueueSubmit fenceを待つ | 実BindSparse fenceの完了保証を示さない。sparseを一般化して広告しない |
| native待機エラー | WaitForFencesがSUCCESS、DEVICE_LOST、OUT_OF_HOST_MEMORYのいずれでも同じstatusなしretireを呼ぶ | 特にOOMはGPU利用停止の証明ではない |
| proxy切断 | shared watermarkが1のままでもsocket切断とstall閾値でsequence2をretireする | callbackを検証済み成功やcontext停止ACKに読み替えない |
| OPAQUE共有 | stock proxyにはp007のnative OPAQUE attach契約がない | OPAQUE契約はp007由来。最終p009 libvulkanはQUIESCEを含むflags7 pairが必要。今回一般互換を新たに有効化しない |

`vkWaitForFences`/`vkGetFenceStatus`はdevice loss時にSUCCESSを返せる。DEVICE_LOSTでretireすることだけをVulkan違反とは呼ばない。一方、native OOMやproxy断は同じ資源停止証明を提供しない。共有先が生成元の次回native APIエラーを観測する保証もないため、「最大1フレームだけ乱れる」という上限は証明していない。

一次実装: [stock queue](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/src/venus/vkr_queue.c) の94行（追加submit）、177–191行（TIMEOUT以外のretire）、[stock proxy](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.1.0/src/proxy/proxy_context.c) の108–130行（disconnect force-retire）。固定cacheのSHA256はevidence/results.json。

## 再現と証拠

`tests/queue.c`と`tests/proxy.c`は固定stock sourceをそのままincludeし、native Vulkan結果とprivate socketpairだけをfixture側で与える。queue fixtureは実submit fenceとmarker fenceを別の数値にし、OOMをSUCCESSへ変更しない。proxy fixtureは本物のsocketpairを閉じてもshared watermarkが変化していないことを独立に確認する。通常・ASan/UBSanの計4profileはPASSで、これは上記の情報欠落を観測した試験の成功である。実GPUのdevice loss誘発、CTS、stockによる本OSの描画受入ではない。

```sh
python3 plan/ws014/phase009/stock-compat/tests/run.py \
  --source /tmp/q308-virglrenderer-1.1.0 \
  --helpers /tmp/q312-stock-build \
  --output /tmp/q312-stock-evidence
```

helperは既存isolated q311 buildのconfig.h、virgl-version.h、vkr_queue_gen.h、Mesa thin archiveとそのmember object。生成headerとarchiveのhashはprovenance.json、実行要約はevidence/results.json。各compile/runは120秒以内に限定した。sourceはローカルで実行し、公開helperだけを既存private hostから取得した。source送出のauto-review拒否は再試行・迂回せず、未送出のままlocal実行で完了した。
