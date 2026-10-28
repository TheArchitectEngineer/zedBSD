<!-- awesome-plan project=zedbsd record=ws270 -->

# WS270 VisionFive 2 で Keiland の desktop を動かし、OS を完全に動かす

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: 2026-10-22 ユーザー（main e6b12bba9 の kernel と Keiland の VF2 の desktop を操作して）「すばらしい。実用スピードです。」→ ws270-p007 の目的は到達。desktop の 60 fps は WS291 で「ws270-p007 で済み」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG008
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない。重い 3D の速さは [WS291](../ws291/ws.md)）
<!-- awesome-plan-current:end -->

## 目標

VisionFive 2 の実機で、SD か NVMe の rootfs から起動し、graphical の login から Keiland の desktop が動く（USB の keyboard・mouse、LAN、sound）。riscv64 の userland の desktop の package（Keiland と依存）が build でき、image に入る。依存は [WS256](../ws256/ws.md)（周辺の driver）、[WS269](../ws269/ws.md)（GPU）。設計は [design.md](design.md)（改訂 2.1、[design-review-1.md](design-review-1.md)・[design-review-2.md](design-review-2.md)）。

## ユーザーの決定

- 2026-10-19（起点）「VisionFive2はまずフレームバッファ、SDカードを実装して、rootfsから起動できるところまで持って行きます。USBキーボードで操作できるようにします。LANとサウンドとNVMeも使えるようにします。NVMe SSDがボードにつけてあり、テスト可能です。ここまでのphaseを作ってください。そのあと、PowerVRのドライバを独立WSで作ります。最終的に、VF2でkeilandを動かして、OSを完全に動作させます。」
- 2026-10-19 22:04 ユーザーの WS269 D5「(a) 画面なしで先行」。
- 2026-10-20 03:25（クリック）: WS270-D1 (c)（config-visionfive2.mk を desktop、最小は config-visionfive2-server.mk、p007 で切り替え）、WS270-D2 (b)、WS270-D3 (a)、WS270-D5 (a)。D4 は欠番（riscv64 の Noct は F-114）。原文は当時の Master の decisions-log（2026-10-27 に削除、git の履歴）。
- 2026-10-21「lockupは解決したのですが、描画が遅すぎてデスクトップが利用可能な水準ではありません。たぶん2fpsくらいです。これを解決したいです。」（p007）
- 2026-10-22「すべての操作で16msで描画でき、60fpsになることが目標です。ただ、どうしても難しければ、retireしてかまいません。おそらく限界まで最適化して、1280x720に落とせば、目標は自動的にクリアされています。」
- 2026-10-22（d101 の frame-ops の操作を目視して）「アニメーションが素晴らしいです。この調子でチューニングを続けてください。」
- 2026-10-22 d114 の lockup は「熱で止まったかも。再現しないなら無視していいよ。」
- 2026-10-22「CIのVF2イメージGPUがデバッグ用のダンプとかログを使わないようにできる？」（`CONFIG_POWERVR_DEBUG := n` の CI の config）
- 2026-10-22（app から CPU の細かい時計を直接読む件、riscv64 の HAL で scounteren.TM を立てる）「OK」
- 2026-10-22「アニメーションは60fpsでないと違和感が出ますが、文字入力はそうではないので、このあたりで最適化を終えましょう。」「あまりにも劇的な高速化を目の当たりにして驚きました。ハードウェアの性能を十分に引き出してくれたと思います。感謝します。」
- 2026-10-22（main e6b12bba9 の kernel と Keiland の VF2 の desktop を操作して）「すばらしい。実用スピードです。」

## 結果

- riscv64 の desktop の build（p002）: userland-dynamic の include、package の arch の表、kwl_cycles、machine 名。amd64・rpi4 の rootfs も rc=0。
- QEMU riscv64-virt の画面なし（p003、T1-752）: service・USB keyboard の login・mouse の event・display が無い時の sessiond の道・`keiland-preview` の static の実行。
- Keiland の起動（p006a）: sessiond が `hw.gpu.attaching` の間は gpu0 があっても待つ（2a）、Keiland の shader 15/15 が compile。板で desktop が PowerVR で動いた（WS269 の U36）。
- desktop の速さ（p007、2026-10-21〜22、1920x1080、frame-ops）: 出発点は全画面の 1 frame 約 390 ms（体感 2 fps）。終わり（main 8669be87c 相当）: drag 60 fps、App Home 58〜60、Wiseview 58〜61、Terminal の起動 55〜60、文字の連打（61 字/s）44〜48、出力 37〜45。scroll・hover は試験の入力の間隔で決まる。打鍵から compositor の frame の完了まで中央値 30 ms・最大 36 ms（d133、200 ms 間隔）、入力の処理と shell の echo は約 2 ms。
  - 効いた変更（大きい順の目安）: compiler（pairing・shared の program・F16SOP・itrsmp f16）、GPU の clock 594 MHz、job の作り直しの削減と firmware の trace の削減、LINEAR の zero copy の scanout と flip の latch、Keiland の damage・不透明な面の下を描かない・prewarm・2 frame の重ね、command の frame の fence を即時に完了、transfer の非同期、buffer の pool、Terminal の差分の upload と変わった行だけ作る。
  - 効かなかった・捨てた変更: single kick、scheduler の起床の変更（device lost）、free list の page を 0 にしない（lockup）、Wayland の reader の手順・present の fence の前作り、libvulkan の buffer の使い回し。
  - 道具: [plan/tools/frame-ops/](../tools/frame-ops/)（基準の表は [plan/tools.md](../tools.md) の frame-ops の行）。
- CI の VF2 の image（vf2-keiland）は GPU の debug の dump・log を使わない config（d117）。

## 制限・移管

- 文字の連打は Terminal → compositor → callback の直列で 60 fps に届かない（ユーザー: 文字入力は 60 fps でなくてよい）。
- 間欠の GPU の lockup（d114・d122・d130・d132）と device lost からの回復（ユーザーの方針で R2）は p007 の終わりの時点で P2 に渡した物。重い 3D の model の速さは [WS291](../ws291/ws.md)。
- VF2 の実機の画面なしの確かめ（p004）は未実施。冷却（heatsink・ファン）の後の安定化の試験は未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws270-p001 | 設計（riscv64 の desktop の build、画面なしの立ち上げ、Phase の分割） | cleared（2026-10-20、改訂 2.1） |
| ws270-p002 | riscv64 の desktop の build の不足のうち設計に依らない物 | in-progress のまま WS を閉じた（build rc=0、実装は main） |
| ws270-p003 | QEMU riscv64-virt の画面なしの確かめ | cleared（2026-10-20、T1-752） |
| ws270-p004 | VF2 の実機の画面なしの確かめ | in-progress のまま WS を閉じた（image と手順は準備済み、実機は未実施。desktop は p006a・p007 の板で動いた） |
| ws270-p006a | Keiland の起動と不足の一覧（sessiond の attaching の待ち） | in-progress のまま WS を閉じた（板で desktop が動いた、WS269 U36） |
| ws270-p007 | desktop の速さ（使える水準へ） | in-progress のまま WS を閉じた（2026-10-22 ユーザー「このあたりで最適化を終えましょう。」「すばらしい。実用スピードです。」） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws270/phase001`〜`phase007`、`plan/ws270/tests/`）は削除する。p007 の板の回ごとの記録（d42〜d133）と notes も git の commit `fb1f6afee` の `plan/ws270/` にある。Queue の履歴は [plan/history](../history/index.md)。
