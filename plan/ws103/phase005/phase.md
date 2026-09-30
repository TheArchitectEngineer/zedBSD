<!-- awesome-plan project=zedbsd record=ws103-p005 -->

# ws103-p005: WSI が present ごとに新しい fence を送る

- Parent: [WS103](../ws.md)
- Status: cleared（2026-09-30 夜、q512-i01）
- Disposition: normal
- Queue: q512-i01
- Design: [design.md](../design.md) §2.4（WSI の側）、§3 の p005

## 範囲

`userland/desktop/libvulkan/wsi-swapchain.c`（必要なら同じ library の fence の file）だけ。

- Wayland の target を含む present の job は、compositor へ送る共有の fence を present ごとに新しく作る（slot の fence を再利用しない）。VK_KHR_display だけの job は今どおり再利用。
- 新しい fence の作成の直後の host の `vkResetFences` を省く。
- present ごとの VkFence と `vkGetFenceFdKHR` の fd の 2 つを、全ての道（作成の後・submit の前の失敗、device の喪失の待ちの失敗、`present_drain` による teardown）で閉じる。
- protocol・compositor・kernel は変えない（compositor は p006 まで今の世代の照合のまま）。

## 完了の基準

1. build（warning 0）。
2. Wayland の present ごとに別の kernel の fence が送られ、前の fd の fence は次の present の後も reset されない（guest の試験）。
3. 失敗の道で fd と VkFence が漏れない（code の見直し。試せる道は試す）。
4. QEMU の Venus: 窓の app（C1・C2、acquire-fence の試験、forge-guest の wltest）、compositor の frame の間隔が変わらない。
5. boot test、5330 の passthrough の smoke（i915 には fence が無いので道は通らない。回帰の確かめ）。
6. 規約の全文。

## 記録（2026-09-30 夜、q512-i01、メインのエージェント Q1、ユーザーの自走の指示）

### 変更（commit `f62638f7`）

- `wsi-swapchain.c`（`present_fence_prepare`）: job に Wayland の target（platform に `commit_early` がある）があれば `fresh`。slot に `sent`（compositor へ送る fence を持つ）を足し、
  slot は `shared` と `sent == fresh` の両方で選ぶ（Wayland の job が送った fence を、display の job が借りて reset しない。見直しで見つけた競合）。`fresh` の job は借りた slot の
  前の fence（前の job の退役で signal 済み）を `vkDestroyFence` し、送った fd の自分の分を閉じてから、新しい fence を作って export する。compositor は libwayland が送る時に dup した
  fd を持つので kernel の fence は生きる。失敗の道では slot が空（fence なし、fd -1）になり次の使用で作り直される。teardown は今の道（最後の fence と fd を閉じる）のまま。
- `sync.c`・`sync-internal.h`・`external-fence.c`: `native_unsubmitted`。unsignaled で作った fence の最初の submit の準備では host への `vkResetFences` を省く（present ごとの wire の往復を 1 つ減らす）。
- 範囲の補い: compositor の `protocol.c` の `factory_fence` に、`--log-frames` のときだけ `ZWL ACQUIRE_FENCE client= surface= generation=` を出す診断の行（p005 の観察と p006 のため）。
- 試験: `plan/ws103/tests/fence-guest.sh`（新規）、forge の image の設定に `acquire-fence-test`。

### 確かめ

| 基準 | 結果 |
| --- | --- |
| 1 build | libvulkan・compositor: warning 0。前後の forge の image と passthrough の image: rc 0、desktop の warning 0 |
| 2 present ごとの新しい fence | `fence-guest.sh`（wltest 600 frame、遅延なし）: 後の image で fence 600 個が全て世代 1（`first_generation=600 max_generation=1`）。前の image（libvulkan の p005 の変更だけを外して同じ手順で作った）では世代が 598 まで進んだ（再利用） |
| 3 漏れ | code の見直し: fence と fd は slot に 1 組だけで、次の使用か teardown で必ず閉じる。600 frame の後も compositor・wltest に error 無し |
| 4 QEMU の Venus | wltest 600 frame の時間 前 62 秒 → 後 61 秒（遅くならない）。ws035 p054（acquire fence の試験）PASS。C1 p126・c1-boot-shutdown・C2 PASS。compositor 自身の画面の出力の job は変えていない（`fresh` にならない）ので、compositor の frame の間隔は個別には測っていない |
| 5 boot test、5330 | boot test PASS。`c5-hw.sh` PASS（34 回、最大 64 ms。i915 には fence が無く、この道は通らない） |
| 6 規約 | 変更の範囲を checklist で見直した |
