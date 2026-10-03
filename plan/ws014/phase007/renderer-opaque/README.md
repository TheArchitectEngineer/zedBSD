# p007: 隔離 Venus renderer の OPAQUE_FD allocation 共有

この資料は [WS014 p007](../phase.md) の optimal image 共有に必要になった、virglrenderer 1.1.0 の限定差分とビルド証拠を保存する。zedBSD の公開 API は標準 `VK_KHR_external_memory_fd` / `OPAQUE_FD`、K の受け渡しは型付き kernel handle fd である。ここでいう native fd は Linux ホスト内部の実装であり、ゲストへ Linux dma-buf API を追加するものではない。

隔離依存のビルドと INIT protocol 試験に続き、実 `q310-wayland-006` で optimal/buffer/linear allocation の共有・描画と生成元終了後の独立 context import が通った。以下に依存単体と QEMU 統合の証拠を分けて保存する。Phase の clearance や GitHub 同期はこのファイルでは更新しない。

## 必要になった理由

既存 private host `awe@10.0.10.25` の Intel Iris Xe / ADL GT2 (`8086:46a8`) に対し、ホストの標準 Vulkan API を直接問い合わせた。次は `VK_IMAGE_TYPE_2D`、usage=`TRANSFER_SRC|TRANSFER_DST`、flags=0 の実測である。

| 画像 | native handle | 結果 | external features | compatible / exportFromImported |
| --- | --- | --- | --- | --- |
| RGBA8 optimal | DMA_BUF (`0x200`) | FORMAT_NOT_SUPPORTED (`-11`) | 0 | 0 / 0 |
| BGRA8 optimal | DMA_BUF (`0x200`) | FORMAT_NOT_SUPPORTED (`-11`) | 0 | 0 / 0 |
| RGBA8 linear | DMA_BUF (`0x200`) | SUCCESS | IMPORTABLE・EXPORTABLE (`0x6`) | `0x201` / `0x201` |
| RGBA8 optimal | OPAQUE_FD (`0x1`) | SUCCESS | IMPORTABLE・EXPORTABLE (`0x6`) | `0x1` / `0x1` |

この失敗は `DEDICATED_ONLY` の処理不足ではなかった。実 ANV が optimal image の DMA_BUF profile を拒否していた。成功した native OPAQUE profile に `DEDICATED_ONLY` は付いていない。メモリ型 0 と 4 は DEVICE_LOCAL のみであり、HOST_VISIBLE 型も別に存在する。llvmpipe へ置き換えて受入を成立させる判断はしていない。

照会コードは [native-image-query.c](evidence/native-image-query.c) と [native-memory-query.c](evidence/native-memory-query.c)、実測値は [native-query.json](evidence/native-query.json)。JSON は成功した tool 出力から転記した値であり、ビルド後の再測定結果とは区別する。

## upstream 1.1.0 との差

パッチは [virglrenderer-1.1.0-opaque.patch](virglrenderer-1.1.0-opaque.patch)、変更前後の各ファイル hash は [provenance.json](provenance.json) に保存した。元の copyright/license header は保持しており、upstream の許諾文も [COPYING.virglrenderer](COPYING.virglrenderer) に保存した。

| 境界 | upstream 1.1.0 | 本差分 |
| --- | --- | --- |
| native blob export | 非 CROSS_DEVICE でも、利用可能なら DMA_BUF を OPAQUE より優先する | 非 CROSS_DEVICE では利用可能な OPAQUE を優先。CROSS_DEVICE は DMA_BUF を必須とする契約を維持 |
| proxy の別 context attach | DMA_BUF・SHM・DMA_BUF に export できる資源のみ。OPAQUE を拒否する | OPAQUE を許し、元 allocation の deviceUUID・driverUUID・size・memory type を内部 protocol で渡す |
| renderer の import 資源 | opaque allocation の native identity を保持しない | fd と同じ寿命で immutable identity を保持し、native `vkAllocateMemory` 前に size/type/両 UUID の完全一致を確認 |
| producer context 自身への再 import | 非 CROSS_DEVICE の blob は context 内に import 用 fd を保持しない | OPAQUE blob は独立した fd 参照も保持する |
| proxy/server の組 | 既存 INIT に feature 応答がない | 固有 magic/flags の INIT request/reply を追加し、組の一致を実際に確認 |

受信した fd は context resource が所有する。native import はその fd を複製し、`vkAllocateMemory` 成功で複製 fd を移譲、失敗では複製 fd を閉じる。メッセージ処理失敗時の受信 fd は既存の外側 dispatch cleanup が閉じる。K/U が主張する metadata によってホストが保存した native allocation identity を上書きする経路は作らない。

線形 scanout 用の private WSI は native DMA_BUF + CROSS_DEVICE を使う。公開 OPAQUE memory の対応はゲストの capability negotiation と実 profile query に従う。scanout 適合性、native opaque allocation 共有、異なる renderer GPU 間の共有はそれぞれ別の条件であり、この差分で無条件の別 GPU import を広告しない。

## capability と組の確認

upstream の先頭 160 bytes は変更しない。capset の全長が **168 bytes** の場合だけ、offset 160 の little-endian magic=`0x5a424453`、offset 164 の flags=`1` を今回の契約とする。proxy が同じ magic/flags の INIT reply を受け取った場合だけ、この suffix を広告する。

ゲストは size・magic・flags の完全一致でのみ native OPAQUE 経路を選ぶ。stock の 160-byte capset や未知 suffix は stock DMA_BUF profile に従い、optimal image 対応を推測しない。将来の upstream tail を今回の vendor extension として解釈しない。

この内部 protocol を追加した proxy と render server は同じビルドの組で使う。片側だけの置換はサポートしない。stock/patched の INIT size 不一致は拒否される。同サイズで magic/flags が異なる場合にも、拒否を接続終了まで伝播する。

独立レビューで、既存 outer dispatch が INIT handler の false を記録するだけで接続を残すことが分かった。新しい ACK 待ちでは停止するため、INIT の失敗を outer dispatch から false として返す修正を加えた。GPU context を作らない [handshake-test.py](evidence/handshake-test.py) で次を確認した。

| 試験 | 修正前 | 修正後 |
| --- | --- | --- |
| 正しい INIT | 完全一致する ACK | 完全一致する ACK |
| magic 不一致 | 2 秒の受信 timeout | EOF、待機を終了 |
| flags 不一致 | 2 秒の受信 timeout | EOF、待機を終了 |
| stock INIT の短い size | EOF | EOF |

[修正前](evidence/handshake-before.json) と [修正後](evidence/handshake-after.json) の両方を残した。試験 server の exit 255 は既存の client disconnect 終了経路であり、ACK/EOF の合否と分けて記録した。これらの試験は Vulkan GPU context・QEMU・表示サービスを起動しない。

## 導入・再構築

実際の隔離先は `/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/`。`source/`、`build/`、`install/`、`python/` をこの下へ置いた。既存 host の system library と、以前の隔離 renderer は変更していない。package install、`ldconfig`、表示サービスの停止は行っていない。

再構築は未変更の virglrenderer 1.1.0 source を新しい task directory の `source/` へ展開し、[provenance.json](provenance.json) の変更前 hash を照合してから行う。保存済みの patched source へ再適用しない。

```sh
renderer_dep=/home/awe/zedbsd-q306-venus/dependencies/q310-opaque
renderer_patch=/home/awe/zedBSD/plan/ws014/phase007/renderer-opaque/virglrenderer-1.1.0-opaque.patch
patch --dry-run -d "$renderer_dep/source" -p1 < "$renderer_patch"
patch -d "$renderer_dep/source" -p1 < "$renderer_patch"
```

既存ホストには cc/ninja と Vulkan/libdrm/gbm/epoxy の development files があった。Meson の実行環境はローカルで既に利用していた `mesonbuild` Python package を task directory の `python/` へ転送し、同時にその copyright notice を保存した。追加 package はインストールしていない。次が実際のビルド設定である。

```sh
export PYTHONPATH="$renderer_dep/python"
python3 -m mesonbuild.mesonmain setup \
    "$renderer_dep/build" "$renderer_dep/source" \
    --prefix="$renderer_dep/install" \
    -Dvenus=true -Dplatforms=egl -Dtests=false -Dvideo=false \
    -Drender-server-worker=process -Dminigbm_allocation=false \
    -Dbuildtype=release
ninja -C "$renderer_dep/build" -j4
python3 -m mesonbuild.mesonmain install \
    -C "$renderer_dep/build" --no-rebuild
```

process worker を選択し、minijail/追加 seccomp package に依存させていない。これは task の隔離テスト用ビルド設定であり、ホストの security settings は変更していない。元 source の MIT 許諾と個別 header の notice は維持する。

QEMU harness は新しい shared library と server の両方を明示する。`--renderer-library-dir` には下表の library directory、`--render-server` には server path を指定する。host の通常実行環境全体へ `LD_LIBRARY_PATH` を設定する運用はしない。

| 成果物 | 値 |
| --- | --- |
| library directory | `/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/install/lib/x86_64-linux-gnu` |
| library | `libvirglrenderer.so.1.9.0` |
| library SHA256 | `3f692e604f7653153b6b7340afa0f97e65ee78956babe004eb7d0b8ead8a6a26` |
| server | `/home/awe/zedbsd-q306-venus/dependencies/q310-opaque/install/libexec/virgl_render_server` |
| server SHA256 | `c9383cef62253c995cb067d7e1eb11c29d801bdd3514fa5d0dcbdb26e9056f0d` |
| patch SHA256 | `04def7cd3fd9e50fad62ff98298a9e34880ce793de4b5942d5a908dd0132379f` |

server は Venus を静的な `libvirgl.a` として含む。QEMU は同じ source から作った shared `libvirglrenderer` をロードする。このため server の ldd に shared `libvirglrenderer` が出ないことを、組の不一致と誤認しない。

## 検証の範囲

[configure.log](evidence/configure.log)、[build.log](evidence/build.log)、[build-final.log](evidence/build-final.log) と [verification.json](evidence/verification.json) に実設定、91 target の初回成功、修正後の増分成功、source hash 一致、成果物 hash、server の loader 依存を保存した。手作りの最小 config を使った初期 syntax probe は生成 header 不足で成立しておらず、PASS としていない。その後の実 Meson/Ninja build が全変更 translation unit を実際に compile/link した。

capability の成立は任意 format/usage の共有を保証しない。native profile query を維持し、実 ANV での OPAQUE fd export、生成元終了、SCM_RIGHTS、独立 context import、GPU 操作による画素検証を次節の QEMU 実行で確認した。CTS、任意 Vulkan driver、任意 upstream renderer revision への互換性を主張しない。この資料内の `qemu_launched=false` は dependency 単体検証時点の値であり、後続実行の状態は owning Phase の evidence に従う。


## 実 QEMU 統合: q310-wayland-006

[検証要約](evidence/q310-wayland-006-summary.json) と元の [result.json](../../temp/remote/q310-wayland-006/evidence/result.json) / [guest 観測](../../temp/remote/q310-wayland-006/evidence/wayland-observed.log) を照合した。QEMU 10.0.11 / Intel ANV、上記の patched server/library の組で 46.651 秒、QEMU exit 0、全受入 PASS。harness は新しい library が QEMU に実際に map されていることと実行前後の hash 一致を確認している。

- linear、buffer、optimal の各 allocation で、生成元終了後の SCM_RIGHTS 受信、独立 renderer context import、GPU 操作後の検証用 readback 1024 pixels 一致。
- 標準 external fence の process 間共有と寿命、および同一 GPU fd の 4 pthread / 計 128 回の独立資源操作。
- Wayland FIFO/MAILBOX 各 6 frame、計 12 frame / 921,600 pixels が独立 oracle と一致し、mismatch 0。実資源 import と BLOB scanout の記録を照合。
- swapchain 再作成、client 中断・再 open、compositor 通常終了・強制終了・再起動、surface loss と console 復帰。

観測ログ内の `WLTEST FAILED ... VK_ERROR_SURFACE_LOST_KHR ... cleanup=0` は compositor 強制終了を検証する期待された結果であり、描画受入の失敗ではない。transport timeout/recovery の専用試験と、直接表示の回帰はこの実行に混ぜず、それぞれ別の有限実行で判定する。
