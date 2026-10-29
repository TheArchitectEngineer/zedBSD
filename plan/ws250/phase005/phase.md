<!-- awesome-plan project=zedbsd record=ws250-p005 -->

# ws250-p005: session ごとの lane

Status: in-progress（2026-10-29 P5、q1676-i01。実装済み・試験未実施（実機の S2 待ち）。cleared にしない）
Parent: [WS250](../ws.md)
Approval: p002 と同じ。2026-10-29 Q250-5「含める (推し)」。

## 範囲

[design.md](../design.md) 4.9（番号ごとの lane の pool、slot の再利用と CPU の copy の前の待ちだけ node->lock を手放す、exec_lock）。依存: p003。

## 受け入れ

build warning 0、host 試験 PASS。実機の S2（mview の停滞が GR の分だけ）は Q1 の割当で。

## 実装（2026-10-29 P5、98e568132）

- nvrtx-node.h: `struct nvrtx_lane`（queue・parked・2 つの waitq・started）。node は `main_lane` と番号ごとの `lanes[32]`、session は `lane` と `exec_lock`。
- runtime.c: worker・completer・park・enqueue を lane ごとに（引数が lane）。session の channel と lane は最初の submit・reserve・command の前に決める
  （`runtime_channel` を runtime_submit・runtime_reserve からも呼ぶ。faulted・stopping の session には作らない）。番号の lane の thread はその番号を最初に
  使う時に作り、node の寿命の間終わらせない（作れなければ main の lane）。共有の session は main の lane。fault と reboot の stop は全 lane を起こす。
- `exec_lock`: `runtime_execute`（worker・command）、`nvrtx_vk_node_blob_attach`、`node_close` の executor の close が `exec_lock` → `node->lock` の順に取る。
- vk-node.c: `nvrtx_vk_gpu_wait`（vk 層では execute_start の slot の再利用だけが呼ぶ）は待つ間 `node->lock` を手放す（`mutex_owned` で確かめる）。
  `nvrtx_vk_gpu_idle`（CPU の copy の前の execute_drain・destructor・vkQueueWaitIdle）は手放さない形に分けた（design.md 4.9 は CPU の copy の前も
  手放すと書いたが、execute_drain は idle を使うので、vk 層を変えずに済む slot の再利用だけにした）。

## 確かめた事（2026-10-29、host）

- vmunix rc=0・warning 0、`plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS 86/0、`plan/ws250/tests/nvrtx-recover-host-test.sh` 45/0。

## 未実施・残り

- 実機の S2（mview の停滞が GR の分だけ）と S0（fps の回帰）。lane の thread が増えた時の振る舞いは実機でしか見られない。
- `node->lock` を手放す間に他の session の frame が触る物の監査は vk 層の slot の再利用の道だけ（execute_start は session の slot だけを触る）。
