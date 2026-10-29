<!-- awesome-plan project=zedbsd record=ws291-p011 -->

# ws291-p011 NVIDIA: scene-5000 を 50 fps（非同期の submit）

- 親: [WS291](../ws.md)。依存: [p010a](../phase010a/phase.md)（cleared）。
- 状態: in-progress（2026-10-29 10:50 P2、q1670-i04。設計だけ、実装は未着手）。
- 出典: WS291 の表の p011 の行。2026-10-29 Q1「p011 の非同期の submit（GPU の待ちを lock の外へ）は Q1 の判断でこの p011 で行う。
  WS250 は担当が無く止まっているので、plan/ws250/design.md の期限の STOP・session ごとの worker と食い違わない形にして（WS250 はその上に作る）」。
- 基準: NVK（Linux）の scene-5000 は 50.1 fps（p010）。zedBSD の起点は 42.24 fps（p010a の s1）。

## 起点の計測（p010a の s1、vmunix-p010a4、nvrtx.vk.perf=1、mview 5000 copies mailbox 12 s）

- MVIEW STAGES: frame 23.63 ms = acquire 0.14 + record 1.32 + submit 21.63 + present 0.40 + 他。
- libvulkan の VULKAN TIMING: 1 frame に submit が 2 回（execute の平均 11.0 ms、最大 21 ms）、present_submit 1 回（約 1 ms）。
- kernel の perf（5 s）: runs 434（1 frame に 2）、gpu avg 9.8 ms・max 19.5 ms、flips 217（43/s）。mview の描画の run が約 18.5〜19.5 ms、
  もう 1 つが約 1 ms の見込み（平均と最大からの推定、run ごとの内訳は未確かめ）。
- 見立て: GPU は 1 frame に約 19.6 ms。CPU（app の record、executor の push の組み立て、present）の約 4 ms が GPU と重ならない（submit が GPU を待つため）。
  重ねれば frame は約 19.6 ms（51 fps）。余裕が小さいので、GPU の側（2 つ目の run の中身、vertex の program の質）も次に見る。

## 今の形（main f18444bb7 の src/drivers/gpu/nvrtx、読んだ事実）

- runtime.c: worker は node に 1 本。frame は `node->lock`（mutex）を持って executor を走らせる（runtime_execute、768〜775 行）。
  marker は worker が順に completion する（GPU の待ちは無い。frame が同期なので marker の時には GPU は済んでいる）。
- vk-execute.c: session に run の memory が 1 つ（2 MiB: push 1 MiB、constants 512 KiB、header・sampler の pool、zero、corners）。
  execute_flush → nvrtx_vk_gpu_run（vk-node.c 283 行）が nvrtx_work_submit の後に nvrtx_work_wait（VK_NODE_RUN_US）で待つ。
  影の surface の作り直し、CPU の copy（CopyBuffer、tiled の CPU の copy）は「前の work の後」を同期の待ちで満たしている。
- vk-queue.c: vkQueueSubmit は全部を走らせてから fence を signaled にする。vkGetFenceStatus は fence->signaled を見るだけ。
- 期限: run ごとの fence の待ち（2 s）で検出（WS250 design.md の決定 2、`CONFIG_NVRTX_RUN_MS` に出す予定）。

## 設計（amdrdna の形を手本に、WS250 と食い違わない形）

1. vk-node.c: `nvrtx_vk_gpu_run` を `nvrtx_vk_gpu_submit(owner, va, bytes, &fence)`（待たない）と
   `nvrtx_vk_gpu_wait(owner, fence)`（今の待ち・期限・perf の計数・gr の check）に分ける。期限は「submit からの時間」で今と同じ 2 s
   （WS250 の決定 2 の「run ごとの fence の待ち」と同じ意味を保つ。期限を越えた run は session の喪失 → WS250 p003 の STOP がそこに入る）。
2. vk-execute.c: run の memory を slot にする（EXECUTE_SLOTS 3〜4、amdrdna は 4）。execute_flush は submit して slot に fence を残し、
   次の push は fence の過ぎた slot（無ければ最も古い slot を待つ）へ。影の surface の作り直し・CPU の copy の前は全 slot を待つ（execute_idle）。
   session の close も全 slot を待ってから free。
3. vk-queue.c: vkQueueSubmit は submit だけで返し、fence に session の最後の fence の値を持たせる（amdrdna_vk_execute_last と同じ役）。
   vkGetFenceStatus は `nvrtx_work_fence_passed` で見る。vkWaitForFences の道（libvulkan が status を poll するか、待つ command があるか）は実装の最初に確かめる。
4. runtime.c: marker は session の未完の fence を持つ間は park し、completer の thread が fence の過ぎた順に completion する（amdrdna の
   runtime_park・runtime_completer・runtime_runs_wait）。completer の待ちは `node->lock` の外。worker は 1 本・FIFO は 1 本のまま
   （WS250 design.md の「queue の形（1 worker・1 FIFO）は p002〜p004 では不変」と合う）。session ごとの worker（WS250 p005 a）はこの上に作る。
   期限を越えた parked の marker は EIO で completion し、session を喪失にする（WS250 の回復の口に渡す。STOP そのものは WS250 p003）。
5. 資源の寿命: record が object を hold する今の形（record_hold）のまま。vkFreeMemory・vkDestroyBuffer・vkDestroyImage の時に、その memory を
   読む run が残っていれば待つ（簡単には session の全 slot を待つ）。amdrdna の扱いを実装の最初に読む。
6. 計測: perf の行に「submit から fence まで」と「GPU の時間」を残す。受け入れは scene-5000 の 50 fps 以上（mview の SPIN、mailbox）、
   copies 1 の shot が p010a と同じ、hang の確かめ（measure.sh hang）で他の session が続く。

## 未確かめ・次の手順

- 1 frame の 2 つ目の run（約 1 ms）の中身（present の copy か、mview の 2 つ目の submit か）: libvulkan の present の道を読む。
- host: nvrtx-vk-host-test.sh の追従、runtime の host 試験があれば park・completer の check を足す。
- 実機の script: plan/ws291/tests/rtx-scene-profile.sh（shot と perf の 1 本）、rtx-mview-check.sh（fps）。
