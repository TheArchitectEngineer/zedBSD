<!-- awesome-plan project=zedbsd record=ws291-p011 -->

# ws291-p011 NVIDIA: scene-5000 を 50 fps（非同期の submit）

- 親: [WS291](../ws.md)。依存: [p010a](../phase010a/phase.md)（cleared）。
- 状態: cleared（2026-10-30 Q1 の判定: NVK と揺れの内で同等。文の「50 fps 以上」そのものは未達（49.5〜49.7 fps）。2026-10-29 の q1670-i05 は 49.1〜49.5、2026-10-30 の q1693-i01 は 49.50・49.69、NVK は平均 50.1・単独の run 49.1）。
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

## q1670-i05（2026-10-29 P2、worktree p2-ws291q、基点 main 509a44ad5）

### 実装（commit 2c3453e93・fa978f0a0・339ddc909）

- work.c: `nvrtx_work_passed`（semaphore が fence に届いた、または fence が 0 か最新の submit より先＝値の wrap の前の物なら済み。lock 無しで読む）。
  fence の値は 0 を飛ばす（ring の entry の「未使用」と区別）。timed（nvrtx.vk.perf=1）の run は start と end の stamp を semaphore の page の
  run ごとの記録（fence の下位 6 bit で 64 個）に書く（後の run が来ても GPU の時間を読める）。ring の entry は timed で 4、普段は 2。
- vk-node.c: `nvrtx_vk_gpu_run` を `nvrtx_vk_gpu_submit`（待たない、session の最新の run を `run_fence`・`run_us` に）・`nvrtx_vk_gpu_wait`
  （submit から 2 s の残り、超えたら session の `run_lost` → 以後の submit・待ちは EIO）・`passed`・`retire`（GPU の時間と gr の check を 1 回）・
  `last`・`idle` に分けた。memory の detach と executor の run の free は session の run が済むのを待ってから（済まなければ map と VRAM を残す）。
  `nvrtx_vk_node_run_wait` は completer が node->lock の外で使う。
- vk-execute.c: slot を 3 個（遅延の作成）。push buffer は「run の済んだ slot → 新しい slot → 最も古い run を待つ」の順で選ぶ。CPU の copy の前
  （buffer↔image の CPU の道、CopyBuffer）は `execute_drain`（submit して session の run を待つ）。影の surface の作り直しと close は free の待ちで。
  fa978f0a0 で slot を coherent の system memory（`nvrtx_vk_gpu_alloc_system`、`nvrtx_work_map_system`）に置いた（取れなければ VRAM）:
  BAR1 の window は UC なので、push・constants・pool の CPU の書き込みが 1 command 約 270 µs かかっていた。
- vk-queue.c: fence は submit の時の session の最新の run を持ち、vkGetFenceStatus が `passed` で見る。vk-device.c: vkQueueWaitIdle・
  vkDeviceWaitIdle は session の最新の run を待つ（失敗は DEVICE_LOST）。
- runtime.c: session の `queued`（worker の queue の中だけ）と `pending`（park を含む）を分けた（同期の command は parked の marker を待たない）。
  marker は session の最新の run が済んでいなければ park し、completer の thread が順に、node->lock の外で待って completion する（期限切れは EIO と
  session の run_lost）。worker と FIFO は 1 本のまま（WS250 の design.md の前提と合う。session ごとの worker は WS250 p005 a でこの上に）。
- node.c: perf の行を runs（submit）・ended（待ちが見た終わりと submit からの時間）・blocked（lock の中の待ち）・parked に。
  339ddc909: blob の user の map を write-combine に（amd64、amdrdna と同じ。gpu.h の `DRV_GPU_MAPPING_WRITE_COMBINE` の既存の口、HAL は不変）。
- host: `make ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/k build/ws291/k/vmunix` warning 0・amd64 vmunix check PASS（3 回とも）、
  `plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS 86/0（stub を新しい口に追従）。libvulkan は不変。worktree の build/ に main の llvm 等への symlink。

### 実機（10.0.10.3、RTX 2070、2026-10-29 11:09〜11:29 の枠、`plan/ws291/tests/rtx-async-check.sh`）

| kernel | scene-5000 fps | copies 1 fps | MVIEW STAGES（frame・record・submit・fence ms） | kernel |
| --- | --- | --- | --- | --- |
| 起点 p010a4 | 42.24 | — | 23.63・1.32・21.63・— | runs 2/frame、gpu 19.6 ms/frame |
| a1 vmunix-p011a（2c3453e93、slot は VRAM） | 43.64・43.59 | 640 | 22.90・1.26・1.07・20.04 | held 271 µs/command、blocked 0、parked 446/5 s |
| b1 vmunix-p011b（fa978f0a0、slot は system memory） | 45.04・45.33 | 1953 | 22.02・1.36・0.16・20.21 | held 21 µs、gpu 1 frame に 19.5+0.1 ms |
| NVK（p010） | 50.1 | — | 19.90・0.08・0.05・19.36 | — |

- shot（copies 1、起動の直後）: a1・b1 とも Linux の参照と max 76、16 以上の差 2 画素（p010a と同じ）。PNG は worktree の build/review/p011-b1-shot-1.png。
- 証拠: worktree p2-ws291q の build/ws291/measure/p011-a1・p011-b1（summary.txt）。終わりに Debian、RTX は vfio-pci、NVMe の kernel は vmunix-va1
  （cksum 1463007375、nvrtx.vk.perf の行 0）に戻した。
- 50 fps に届かない理由: mview は fence 1 本で毎 frame 待つ（frame の重なりは 1）ので、frame = CPU（acquire・record・submit・present）+ GPU。
  非同期の submit で submit は消えたが、record 1.36 ms が残る（NVK 0.08）。見立て: --spin で毎 frame 書き直す 5000 個の instance の位置（80 KiB）を
  UC の BAR1 の user の map に書いている。339ddc909 で write-combine にした（実機は未確かめ、次の枠）。fence は NVK より 0.85 ms 長い（GPU の質）。
- hang（`measure.sh hang`、vkhang は今の tree から作った物と byte 一致 cksum 2845471830）: vkhang は 2 s で DEVICE_LOST（result=-4）、その後の
  vkhang --loops も lost、mview after-hang は描かず、desktop の shot は取れない。共有の work channel に終わらない run が残り、取り消しが無いため。
  Q1 の判断（2026-10-29）: 「hang の後に他の session が続く件は p011 の範囲の外（WS250 の取り消しと復帰）」。p011 の受け入れは scene-5000 の fps（50）で見る。

### 2 回目の枠（2026-10-29 11:43〜12:14、同じ script、hang は無し）

- de077be78: completer の待ちは 2 ms の spin の後 1 tick（1 ms）の sleep で見ていたので、20 ms の run の終わりを遅れて見ていた。session の前の run の
  長さ（submit から終わり）を `run_took_us` に覚え、その 1.5 ms 前まで sleep、そこから 2 ms spin（`nvrtx_work_wait_late`）。host: build warning 0・
  vmunix check PASS・host test PASS 86/0。

| kernel | scene-5000 fps（perf 無し・perf 有り） | copies 1 fps | STAGES（frame・record・submit・fence ms） | kernel |
| --- | --- | --- | --- | --- |
| c vmunix-p011c（339ddc909、WC、cksum 286899472） | 48.90・48.46 | 1908 | 20.60・0.08・0.17・20.03 | ended avg 19.61 ms、gpu 19.5+0.1 |
| d vmunix-p011d（de077be78、WC+late spin、cksum 3979244274） | 49.45・48.83 | 1870 | 20.45・0.11・0.25・19.83 | ended avg 19.50 ms（＝GPU） |
| d2（同じ kernel、2 回目） | 49.14・49.15 | 1888 | 20.31・0.12・0.22・19.69 | gpu 19.5+0.1 |
| NVK（p010） | 50.1（単独の run 49.1） | — | 19.90・0.08・0.05・19.36 | — |

- record は 1.36 → 0.08〜0.12 ms（NVK 0.08）: 見立てどおり UC の user の map への instance の書き込みだった。起床の遅れも消えた（ended ＝ GPU の時間）。
- shot（copies 1、起動の直後）: c・d・d2 とも Linux の参照と max 76、16 以上の差 2 画素（p010a と同じ）。PNG: worktree の build/review/p011-d2-shot-1.png。
- 証拠: build/ws291/measure/p011-c・p011-d・p011-d2。終わりに Debian、RTX は vfio-pci（lspci）、NVMe の kernel は vmunix-va1（cksum 1463007375、
  nvrtx.vk.perf の行 0）。lock は 12:14 に返した。

### 再開点（12:15 P2）

- 起点 42.24 → 49.1〜49.5 fps。frame 20.2〜20.3 ms のうち GPU 19.5 ms、CPU（acquire・record・submit・present）約 0.5 ms と fence の残り約 0.2 ms。
  NVK の 50.1 との差は約 0.3 ms、NVK 自身の run の幅（49.1〜50.1）の中。50 以上を確かにするには GPU の側（vertex の program の質、1 frame 19.5 ms）
  を p012 以降の予備で詰める。cleared の判定（50.1 に対する 49.1〜49.5 の扱い）は Q1 に頼む。

## q1693-i01（2026-10-30 P5、worktree p5-ws291、基点 main c70e11b35）

- 承認: Queue q1693-i01（2026-10-30 ユーザー「…そのあと、デスクトップ機では、RTXドライバの最適化に進んでください。エージェントは再利用しても新規で立ててもOKです。」）。
- 見立て: p011 の再開点の「GPU の側（vertex の program の質）」を確かめる。

### 実装（commit 9d0adf916・374eb20c3、Q1 が main に merge）

- SM75 の compiler（src/drivers/gpu/nvrtx/compiler/sm75）: 一度だけ FADD・FSUB に読まれる FMUL を FFMA に融合（間に loop の境・MOVE が無い時）。
  定数・push 定数・UBO の語を、読む算術（FADD・FSUB の被減数・FMUL・FMIN・FMAX・FFMA）の 1 つの imm・cbuf operand として直に読む（MOV を省き register を取らない）。
  NoContraction の module は融合しない（frontend は decoration を IR に運ばないので module の OpDecorate を走査。今の frontend は NoContraction の module を拒否する）。
- mview.vert 108 → 59 命令（register 64 → 54）、mview.frag 26 → 20。
- host: nvrtx-sm75-desktop-test --strict 186 compiled・0 refused・0 disagreed、nvrtx-sm75-host-test 299/0（contract.comp・precise.comp を追加）、
  nvrtx-vk-host-test 94/0、nvrtx-vk-d3-host-test 218/0、qmd PASS。vmunix（plan/ws291/tests/config.mk）warning 0・amd64 vmunix check PASS。
  試験の解釈器 plan/tools/nvrtx/sm75-machine.c に FFMA と FORM_CB_SRC2 を追加。

### 実機（10.0.10.3 RTX 2070、2026-10-30 03:22〜03:40 の枠、`plan/ws291/tests/rtx-async-check.sh`、hang 無し）

| kernel | scene-5000 fps（perf 無し・有り） | copies 1 fps | STAGES（frame・record・submit・fence ms） | kernel の perf |
| --- | --- | --- | --- | --- |
| base（main c70e11b35、cksum 1281945519） | 49.50・49.28 | 1877 | 20.26・0.12・0.21・19.67 | ended avg 19.42 ms、1 frame に run 2 |
| f1（374eb20c3、cksum 609932745） | 49.69・49.08 | 1918 | 20.34・0.13・0.21・19.77 | ended avg 19.49 ms |

- 頂点の program の命令を約半分にしても GPU の時間は動かない（揺れの内）。この scene（1 frame に三角形 約 1.85 億、頂点 約 1.3 億）は shader の命令でなく
  固定機能（primitive の処理）で決まる見込み。NVK（fence 19.36）とほぼ同じ GPU の時間。
- shot（copies 1、起動の直後）: 両方とも Linux の参照と max 76、16 以上の差 2 画素（p010a と同じ）。
- 証拠: worktree p5-ws291 の build/ws291/measure/q1693-base・q1693-f1（summary.txt）。終わりに Debian、RTX は vfio-pci、NVMe の kernel は vmunix-va1
  （cksum 1463007375、nvrtx.vk.perf の行 0）。lock は 03:40 に返した。

### 残り（03:45 P5）

- frame と GPU の差: mview の fence 19.7 ms と GPU の run 19.4〜19.5 ms の約 0.3 ms（完了の検出から app の起床まで）と、1 frame の 2 つ目の run（present、約 0.1 ms）。
- nvrtx の big-N・subs-N・scene-1500 は zedBSD で未計測（NVK: big-1000 136.2、subs-1000 75.4、scene-1500 165.6 fps）。差の大きい所を探すには bench の 1 枠が要る。
