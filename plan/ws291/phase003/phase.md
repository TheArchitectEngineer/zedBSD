<!-- awesome-plan project=zedbsd record=ws291-p003 -->

# ws291-p003 RDNA の投入の経路（非同期の submit、record・submit の費用）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-27 00:50 P2、q1603-i01）。受け入れの条件を満たした（下の結果、2026-10-27 04:30）。clearance は Q1 の判定。
- 出典: 2026-10-27 00:44 ユーザー（Q1 の問い「改善をどの順で？」へのクリック）「投入の経路から（推し）」。GPU 側（NGG の culling・HTILE・tiled）は後の Phase。
- 依存: p002（cleared）・p002a（cleared）。

## 範囲

1. profile で投入の経路の時間の内訳を取る（libvulkan → gpu-op の ioctl → amdrdna の executor の decode・submit・fence の待ち）。
2. 非同期の submit: vkQueueSubmit は GPU に積んで返し、fence・marker は GPU の完了で終える。session・resource の寿命、reset・fault の扱いを崩さない。
3. record の decode と submit の費用を削る。
4. 毎回 `measure.sh bench` で測り Linux の表と並べる（scene・big・subs の全部）。正しさは mview の 1 体と 6500 体の keiland-shot を Linux の基準と比べる（差 0 と 8 pixel から悪化しない）。
5. GPU の hang の時に desktop が戻ることを 1 回確かめる。

## 受け入れ

- 前後の表（zedBSD と Linux）、内訳の数値、正しさの比較、hang の確認を記録。amd64 の vmunix・libvulkan が warning 0、host 試験（amdrdna-vk-host-test）PASS。

## 道具（plan/ws291/tests/measure.sh に足した物）

- `profile OUT [MVIEW-OPTION]...`: libvulkan の `VULKAN_ZEDBSD_SUBMIT_TIMING=1` と kernel の `amdrdna.vk.timing=2000`（USB の zedbsd.cfg に足した 1 行）の mark を run ごとに取る。
- `shot OUT COPIES`: mview の静止 view（FIFO、content game）を keiland-shot で撮り、Linux の基準（build/ws291/measure/ref/linux-shot-COPIES.png）と shot-diff.py と「16 階調以上の差の pixel」で比べる。
- `hang OUT`: vkhang（plan/ws039/tests/vkhang、終わらない draw）→ `--loops`・`--compute --loops`・mview の短い run・desktop の keiland-shot と kernel の loss・reset の行。
- 実機の操作の安定化: zedBSD への SSH は 1 本の接続の使い回し（ControlMaster、2026-10-27 ユーザー「SSHセッションを1本のみ使い、使い回す方式に変更して」、BUG-346: sshd の子の回収漏れで新しい接続が断られる）。mview を SSH の session から切り離して走らせ（`zrun`）、zedBSD からの file は `ssh cat` で取る（scp・sftp が時々止まった）、bench は要約の無い run を 2 回まで再試行、run の始めに test の入力で pointer を動かす（15 分の無操作で画面が消え lock される）。

## 結果

### 1. 内訳（実機 10.0.10.3、変更前 0e693220c、2026-10-27 01:00）

| 所 | 数値 | 原因 |
| --- | --- | --- |
| big-100 の record 90 ms/frame | stream の読み 442 KiB を 81 ms（5.4 MB/s）、decode は約 3 ms | 64 KiB を超える command の stream を libvulkan が VRAM の blob（UC の窓）に置き、kernel が UC で読む |
| big-100 の submit 69 ms/frame | CPU 64 ms（1 draw 約 40 µs）、GPU 3.5 ms | executor の IB・resource table を VRAM の窓（UC の map）へ書く |
| subs-100 の submit 1 回 1.46 ms（kernel） | run の待ち 0.71 ms（GPU は 0.085 ms）、CPU 0.75 ms | `amdrdna_ring_wait` が `kern_usleep_range` で 1 ms の tick まで寝る（EOP の割り込みは数えるだけ）＋ UC の書き |
| present 2.02 ms/frame | 同じ tick の待ち | 同上 |
| scene-6500 の submit 44.7 ms | GPU 43.5 ms（Linux 19.5） | GPU 側（後の Phase）。gfx の clock は負荷の間 1780 MHz に上がる（timing の行）ので、clock ではない |

### 2. 修正

| 段 | commit | 内容 |
| --- | --- | --- |
| 1 | 83be6ca5e | ring の fence の待ちを EOP の割り込みで起こす waitq に（cp.c `amdrdna_ring_wait`、engines.c の EOP で wake、各 sleep は次の tick で切れる） |
| 2 | d243caf20 | executor の run（IB・table）を system memory（drv_dma の連続 RAM、session の space に system・snoop の page で map）へ。`amdrdna_backing_create_system`、`amdrdna_vk_gpu_alloc_system`。program は VRAM のまま |
| 3 | 2bdd6cc9a | GPU_BLOB_HOST（uapi、Q1 の承認 2026-10-27「案 A で可」）: libvulkan の transport の storage（reply・stream）を cache の効く RAM に。core は GPU_CAP_HOST_BLOB の無い device には flag を落とす。amdrdna は RAM の storage を blob として list し executor が直に読む |
| 2b | a73cd63b8 | 段 2 の修正: system memory の run を drv_dma（4 GiB 未満の PCI 共有の DMA memory、cache の budget に数える）から plain な RAM（kern_pmem_alloc、GPU は物理 address で読む）へ。実機で run は物理 0x1019e0000 など 4 GiB より上に置かれ、A9 の照合と描画は正しい（GPU が 4 GiB 超の RAM を読める）。DMA の resident は 3,975,872 byte のまま |
| 4 | 9bbe2b60c | 非同期の submit（下） |

非同期の submit の形:

- space.c: `amdrdna_space_run` を `amdrdna_space_submit`（ticket を返す）・`amdrdna_space_wait`・`amdrdna_space_passed` に分けた。run が時間内に終わらなければ `space_hang` が space を lost にして queue を reset（submit_lock で submit と reset を分け、1 回だけ）。reset で落ちた run（reset の時の fence の後から最新まで）は wait が EIO を返す。別の space の run が ring に残っている間は、その終わり（か reset）を待ってから積む（ある session の hang が他の session の run を巻き込まない）。
- runtime.c: marker を worker が取った時、その session の最新の run が終わっていなければ park し、completer の thread が park した順に run の終わりを待って完了する（時間内に終わらない・落ちた・lost なら EIO）。
- vk-execute.c: run の memory を slot（最大 4、必要になった時に作る）にし、IB を積んだら待たずに次の slot へ。slot の再利用の前にその run の終わりを待つ。vk-queue.c の fence は submit の時の各 queue の最新の run を持ち、その終わりで signaled。vkQueueWaitIdle・vkDeviceWaitIdle は最新の run を待つ。graphics と compute の queue の間は、片方に積む前にもう片方の最新の run を待つ。

### 3. 前後の表（実機 10.0.10.3、fresh session、MAILBOX・content game、8 秒の spin）

fps（前 = p002 の zedBSD、build/ws291/measure/bench-zedbsd-2/table.txt。後 = 9bbe2b60c、build/ws291/measure/bench-p003-async3/table.txt。Linux = build/ws291/measure/bench-linux-3/table.txt）:

| run | 前 | 段 3 の後（2bdd6cc9a） | 後 | Linux |
| --- | --- | --- | --- | --- |
| scene-1 | 58.4 | 52.9 | 105 | 9604 |
| scene-1500 | 57.5 | 57.6 | 60.9 | 201 |
| scene-6500 | 20.1 | 21.4 | 21.9 | 50.1 |
| big-1 | 58.1 | 58.3 | 105 | 9345 |
| big-10 | 57.6 | 58.1 | 108 | 2660 |
| big-100 | 6.06 | 57.9 | 62.0 | 533 |
| big-1000 | 0.53 | — | 13.6 | 86.9 |
| subs-10 | 41.7 | — | 85.9 | 3797 |
| subs-100 | 4.35 | 20.1 | 32.5 | 521 |
| subs-1000 | 0.37 | — | 3.24 | 58.2 |

後の frame あたり ms（acquire / record / submit / present / fence）と Linux（record / submit / fence）:

| run | zedBSD 後 | Linux |
| --- | --- | --- |
| scene-6500 | 0.65 / 2.30 / 0.09 / 0.14 / 42.2 | 0.14 / 0.10 / 19.5 |
| big-100 | 7.56 / 4.28 / 1.34 / 0.34 / 2.55 | 0.76 / 0.04 / 1.02 |
| big-1000 | 13.0 / 42.9 / 15.1 / 0.13 / 1.89 | 5.42 / 0.02 / 6.01 |
| subs-100 | 0.44 / 17.4 / 12.4 / 0.16 / 0.28 | 0.59 / 0.91 / 0.39 |
| subs-1000 | 0.01 / 171 / 127 / 0.18 / 0.02 | 6.63 / 10.1 / 0.36 |

- kernel の内訳（後、amdrdna.vk.timing）: vkQueueSubmit（op 18）1 回 0.025 ms（前 1.46 ms）。subs-100 の 1 submit の 0.12 ms・1 command buffer の Begin＋End の 0.17 ms の大半は、1 回ごとの transaction（ioctl → worker の起動 → 返事の通知 → libvulkan の起床）の往復。
- 60 fps を超えた: 前は flip の待ちで 60 が上限だったが、submit が GPU を待たなくなり、game mode の MAILBOX で 105 fps（acquire 7.6 ms が残りの待ち）。
- 段 3 の後の scene-1 の 52.9 は 1 回目の run（起動直後）の値。

### 4. 正しさ（keiland-shot と Linux の基準、後の kernel）

`measure.sh shot`（FIFO、合成の view: game の間は compositor が INACTIVE で撮れない）: 1 体は差 0（全 pixel 一致）、6500 体は 16 階調以上 8 pixel・8 階調超 9 pixel（box x=584..1288 y=221..891）で p002a と同じ。悪化なし。証拠 build/ws291/measure/p003-async3/shot-{1,6500}/。

### 5. hang の確認（後の kernel、`measure.sh hang`）

`VKHANG lost after=2001 result=-4`（DEVICE_LOST）、kernel `gfx fence 97777 not reached`・`space 87 lost: its run did not end`・`graphics queue reset for context 12 in 4ms, the queue runs again`。続けて `vkhang --loops` ok（pixel=ff6f0000）、`vkhang --compute --loops` ok（steps=111）、mview 58.4 fps、desktop の keiland-shot は正常（Settings の窓と上の bar）。証拠 build/ws291/measure/p003-async3/hang/。

### 6. 途中で見た事（ユーザーの入れ直し 3 回）

- 非同期の試作の撮影の後に zedBSD へ SSH が繋がらなくなった。USB の FAT に dmesg・ps を書く watcher で調べた結果、kernel・userland は動き続け、mview の 900 frame も出し切っていた。止まっていたのは sshd: listener が `sshd-session [priv]`（子の sshd-auth は zombie）を刈らず、残りが MaxStartups に近づくと新しい接続が banner で落ちる。同期の kernel でも同じ残りが出た（約 2 分後に刈られた）。Q1 が BUG-346 として P1（VF2）に回した。この Phase の側は SSH の接続を 1 本の使い回しにして避けた。
- 仮説 H4（DMA の消費）は数値で外れ: dma resident 3,975,872 byte、cache の target 17 GB。ただし段 2 の drv_dma は PCI 共有の DMA memory を MB 単位で取る設計上の誤りなので a73cd63b8 で直した。
- 既知の残り（p002a と同じ）: `--frames` の FIFO の run が 120〜600 frame で vkAcquireNextImageKHR の SURFACE_LOST（同期の kernel でも出る）。

## 残り（次の Phase の案）

1. GPU 側（scene-6500 の GPU 42 ms 対 19.5 ms）: clock は負荷の間 1780 MHz に上がる（timing の行）ので clock の差ではない。NGG の小さい三角形の culling、HTILE（HiZ）、tiled の texture、primitive の binning の順に候補。
2. transaction の往復（subs-1000 の 297 ms の大半、big-1000 の record 43 ms）: libvulkan の vkBeginCommandBuffer を End の stream に畳む・submit の返事を待たない（共有の libvulkan の変更、Q1 の承認が要る）、kernel の worker の起動の遅れの短縮。
3. vk timing の `gpu busy` は非同期では数えない（completer の待ちで数える形に直す）。
4. game mode の 60 fps 上限は非同期で外れた（MAILBOX で 105 fps）。FIFO の SURFACE_LOST は別に調べる。

## 記録

- 2026-10-27 00:50 P2: 着手。内訳を測った（1 節）。
- 2026-10-27 01:10 P2: 段 1（83be6ca5e）。01:22 段 2（d243caf20）。01:50 段 3（2bdd6cc9a、Q1 merge 済み）。
- 2026-10-27 02:20 P2: 段 4（非同期）を実機で測定。撮影の後に SSH が死ぬ件の調べ（6 節）、ユーザーの入れ直し 3 回（02:55・03:23・04:06）。
- 2026-10-27 04:30 P2: 段 4（9bbe2b60c）、前後の表・正しさ・hang の確認を記録。
