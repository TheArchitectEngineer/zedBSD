# WS014 focused verification

p003/q306はGPU core、Venus PCI transport/backend、独立したVulkanテストクライアントとQEMUの実画面取得を完了した。現在のp005/q307はテクスチャ付き回転直方体でshader/3D描画を検証する。結果と範囲は各 [p003](../phase003/phase.md)・[p005](../phase005/phase.md) が所有する。以下は再現コマンドであり、単体試験だけで実描画の完了を判定しない。

```sh
sh plan/ws014/tests/run-gpu-framework-test.sh
sh plan/ws014/tests/run-pci-service-lifecycle-host.sh
python3 -B plan/ws014/tests/test-vkdemo-oracle.py
python3 -B plan/ws014/tests/test-vkdemo-shaders.py
sh plan/ws014/tests/run-vkdemo-cli-test.sh
```

GPU core試験は動的登録、session/resource所有権、世代handle、権限とcopy境界、失敗rollback、ILP32/LP64のUAPI layoutを確認する。Venus backend/transport試験は本番ソースを使い、共有BAR mapping、capset、blob/scanout、queue index、timeout時のDMA保持とreset後の回収を確認する。RFB試験は有限Unix peerで分割受信、画像範囲、完全coverage、異常入力、期限を確認する。対応するC runnerは通常実行に加えてASan/UBSanで検証する。

実QEMUのビルド・転送・起動・画像照合は [リモート検証README](README-venus-remote.md) を参照する。2Dは表示経路の対照試験、VenusはVulkanによる生成とfence/readback検証を含む。全Vulkan適合とnative i915は対象外。

共通U codecの試験は独立した複数session、途中失敗の回収、返信marker・wire境界、chunk転送と期限を確認する。3Dの数理oracleはrayと直方体の交差から期待値を作り、clearのみ、無地、未回転、誤UV、古いframeを拒否する。6枚の実画像と通常アニメーションの手順は [vkdemo検証README](README-vkdemo-remote.md) を参照。

<details>
<summary>初期アーキテクチャ検討の履歴</summary>

# WS014 architecture review cases

Parent: [WS014](../ws.md)

The architecture review is on manual hold (`MB-005` in the master plan). These
cases remain a future discussion checklist and are not active Queue inputs.

The only current Phase is architectural discussion. These are design review
cases, not executable conformance tests.

| Case | Required design result |
| --- | --- |
| `GPU-D001` | Public objects, handles, ownership, lifetime, and process-exit cleanup are complete |
| `GPU-D002` | Memory, images, mapping, sharing, cache transitions, queues, and fences are coherent |
| `GPU-D003` | Malformed shader/command/input and GPU hang/reset paths fail without escaping isolation |
| `GPU-D004` | Mandatory, reduced GLES2-class, optional, and unsupported capabilities are explicit |
| `GPU-D005` | Display takeover, fallback, console, panic, and permission ownership are deterministic |
| `GPU-D006` | WS004, WS007, WS008, WS009, i915, Vulkan, and GLES responsibilities have one owner each |

## 2026-09-12 GPU planning handoff

WS014/p001 architecture discussion resumed by user; first target is QEMU virtio-gpu, superseding i915-first/manual design hold. Vulkan display API is a proposal, not a frozen ABI. Linux DRM compatibility is not required, but OS memory/sync/display/permission machinery remains necessary. No implementation Queue. Other WS holds stay unchanged. See WS014 and p001; old review cases remain design inputs, not runtime tests.

</details>

## 2026-09-24: 動かない試験の削除（ws034-p049）

build できない、または今の source と合わなくなった試験を、書き直さずに削除した（ユーザー指示）。
削除した file の一覧は [`plan/ws034/phase049/deleted-tests.txt`](../../ws034/phase049/deleted-tests.txt)。
上の説明のうち、そこに載っている file の記述は履歴である。

## 2026-09-28: 初期のグラフィックの試験の削除（WS075）

ユーザーの判断（2026-09-28、`plan/master.md` の「古いグラフィックの試験の driver」）により、初期の Venus・WSI の試験を
削除した。デスクトップの起動が回帰の代わりで、特定の機能は要るときに試験を書く。上の説明のうち次の file の記述は履歴である。

- build できないもの（消えた `plan/ws030/tests/` を include する）: `venus-backend.c`・`venus-backend-linked.c`・
  `run-venus-backend-test.sh`、`venus-edid.c`・`run-venus-edid-test.sh`、`venus-sharing.c`・`run-venus-sharing-test.sh`、
  `gpu-topology.c`・`run-gpu-topology-test.sh`、`wayland-wsi-swapchain.c`・`run-wayland-swapchain-test.sh`、
  `wayland-wsi.c`・`run-wayland-wsi-test.sh`、`wsi-acquire-fd.c`・`run-wsi-acquire-fd-test.sh`。
- Venus だけの初期の試験: `venus-client.c`・`run-venus-client-test.sh`、`venus-transport.c`・`venus-transport-peer.inc`・
  `run-venus-transport-test.sh`、`test-venus-rfb.py`。
- 参照の無い試験: `test-renderer-pause.py`、`test-vulkan-elf.py`。
- 使う試験が無くなった道具: `plan/tools/venus-console.c`。

残したもの: GPU core の host 試験（`gpu-*.c`、`handle-fd*`、`pci-service-lifecycle-host.c`: i915 の作業で `src/drivers/gpu/gpu.c`
を変えるときに使う）、libvulkan・vkdemo の host 試験、QEMU の遠隔の harness（`run-venus-remote.py`・`venus-qemu.py`・
`venus_rfb.py`・`vkdemo-qemu.py`・`wayland-qemu.py`: `plan/ws031/tests/` と `plan/ws035/tests/` が import する）、
`vkdemo_oracle.py`（`plan/ws031/tests/vkloop-hw.sh` の oracle）。
