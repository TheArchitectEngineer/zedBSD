<!-- awesome-plan project=zedbsd record=handover-20261021 -->

# 引き継ぎ 2026-10-21 07:55（Q1、VF2 の PowerVR と Keiland の desktop）

ユーザーの指示（2026-10-21）:「週次レートリミットが近いので、決定と作業方針を記録して、ハンドオーバー可能にすることを優先にしつつ、作業を続けてください。」
この file は次の Q1 が最初に読む物。決定の原文は Master の decisions-log（2026-10-21 の行）。

## 目標と今の到達点

- 目標（ユーザー 06:47）:「デスクトップが動くまで自走」。fg020 VisionFive 2、WS269（PowerVR BXE-4-32）・WS270（Keiland）・WS277（DC8200）。
- **到達（板、desktop の 10・12 回目）: Keiland の desktop が PowerVR で描かれ、Settings（welcome）と Terminal の window が出て prompt が出る。** ユーザー（HDMI）:「表示内容自体は完璧です。」画面: build/review/vf2-desktop/shot10.png・shot11.png。
- 残る問題:
  1. **遅い**（ユーザー:「全画面書き換えは2fpsも出なかったり、カーソルも3fpsくらい」）。原因: 3D 1 job ≈ 40 ms（396 MHz、1080p）、submit・transfer ごとの同期の待ち（D-P4）、DC8200 の zero copy が効いていない（`display … scanout 0`、`frames copied (zero copy off for the lease)`、kernel が毎 frame 8 MB を uncached で copy）、cursor も GPU の frame で描く。
  2. **compositor の 3D の lockup（間欠）**: `context reset notice … reason 1 dm 3`。d9 で fault の持ち主を特定（1920x1080 の linear image の末尾を transfer が越えて読む）→ P2 の 32 行の padding（7b97d66a5）と transfer の source の clamp（79fee838f）と行ごとの copy の直し（cf79f8703）。d11・d14・d15 で出た（d15 は i06・i07 入りの main）。d12・d13 は出なかった（たまたま）。**未解決、P2 が最優先（q1523-i08、job の draw ごとの log）**。

## 決定（2026-10-21、原文は Master の decisions-log）

- present は CPU copy でなく、PowerVR が linear に描いた buffer を DC8200 が直接 scanout する（ユーザー「タイルを用いず、linearでバッファに描画して、そのままscanoutできるようにしてほしい」）。q1525 で libvulkan 側は済み、kernel の flip は own mode で未（q1529）。
- framebuffer への CPU の書き込みは uncached の alias（kern_pmem_map_uncached）。U74 は Svpbmt が無く kern_device_map の NOCACHE は DRAM に効かない（ユーザー「HALでマップするときにcachedになってるのはおそらく間違いない」、q1526、「キャッシュの挙動が消えました。」）。
- **D-P4 を変更: submit は非同期**（kick して戻り、完了は fence で待つ）。ユーザーのクリック「非同期に変える (Recommended)」。q1528 で設計 → 実装。
- linear の SAMPLED は on（POWERVR_FORMAT_LINEAR_SAMPLED 1、Wayland の client の swapchain は共有の道しか無いため、q1523）。

## 担当と実行中の Queue（main 8a8e32abd 時点）

| 担当 | Queue | 中身 | 状態 |
| --- | --- | --- | --- |
| P1 `ab309998eee17b581`（worktree /home/awe/zedBSD-worktrees/p1） | q1528 | 非同期の submit（D-P4 の変更）: WS269 の新 Phase の設計 → 実装 | 実行中 |
| P1 | q1527 | desktop の速さ（timing、594 MHz の試し）。594 MHz の使い捨ての kernel は p1 の build/q1527-594/vmunix（未試験） | 一部（594 の板の試験が残り） |
| P2 `aa73ad61de6cef181`（/home/awe/zedBSD-worktrees/p2） | q1529 | DC8200 の own mode で zero copy の flip（vblank の IRQ 95）と cursor plane | 実行中 |
| P2 | q1523 | compositor の lockup。i05〜i07 merge 済み、vkmatrix 25/25（板 d13） | 数回の起動で確認待ち |

## 板の回し方（Q1 だけが板を触る）

- 板: VF2 `kei@10.0.30.2`（password kei、sudo 同じ）、電源は SwitchBot AC276E42B252（2026-10-20 ユーザーの許可: SSH の全操作・電源の入れ直し）。
- SD は U35 の UAT の image のまま。**desktop は SD を書き直さず `plan/tools/vf2/overlay.sh build/uat/vf2-gpu/rootfs build/uat/vf2-desktop/rootfs OUTDIR` で入れてある**（/boot/boot0/zedbsd.cfg に login=graphical、元は zedbsd.cfg.old・vmunix.old）。
- kernel の入れ替え: `VF2_HOST=kei@10.0.30.2 VF2_PASSWORD=kei plan/tools/vf2/run.sh build/uat/vf2-desktop/vmunix build/uat/vf2-desktop/runs/dN`。build: `make -j16 ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-gpu.mk BUILD=build/uat/vf2-desktop build/uat/vf2-desktop/vmunix`（libvulkan などの userland を変えたら native.img まで作って overlay.sh を流す）。
- 確かめ: `grep "DESKTOP shown" /var/log/sessiond.log`、`XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 nohup terminal &`、`sudo keiland-shot /tmp/x.png`（scp で build/review/vf2-desktop/ へ）、`dmesg | grep -E "context reset|timing:|display"`、`/run/user/1000/session.log` の `KWL PERF rec`、vkmatrix（build/uat/vf2-gpu/bin/vkmatrix を /tmp に scp、今は 25/25）。
- SD の dw-mmc の read の失敗（BUG-343）がときどき出る。SSH が 3 分戻らない時は run.sh がもう一度電源を入れ直す。
- SD の image（U36）: build/uat/vf2-desktop/native.img（main f72c803b4、host key 入り、sha256 9036b471…）。新しい image を作る時は build/uat/vf2-gpu/hostkeys を rootfs/etc/ssh に置いてから root.ufs と native.img を作り直す。

## 次の順

1. （済 d14）594 MHz は明らかな差なし → 396 MHz のまま。p017（非同期の submit）の設計は Q1 が承認（§3 overtaking は powervr の中、§6 は HWR を先に、driver の watchdog を短く）。
2. P2: lockup（q1523-i08）→ vblank の IRQ 95 が数えない（d15 `dc8200: vblank 0`、`frames copied (controller not ready)`）→ cursor plane。
3. P1 q1528 の非同期の submit を板で（vkmatrix・gpu-selftest・vkreset・desktop）。
4. lockup が数回の起動で出なければ q1523 を cleared、WS270 p006a の 2b を cleared に。U36 の image を作り直し beta2.md を更新。
5. 残り: WS269 p015 (4) 長時間の試験（ユーザーの確認待ち、夜間）、vkmatrix・vkcoherency の書き方（p012 の残り）、BUG-344（libc の fscanf）。

## 記録の場所

- 板の経過: plan/ws270/phase006a/phase.md の「板の結果」「板の経過」（d1〜d13）。
- Queue: plan/queue.md の q1517〜q1529。Master の next: BUG-345、q1530、WS278。
- WS269 の知見と制約: plan/ws269/ws.md の「知見と制約」。安定性: plan/ws269/phase015/phase.md。文書: docs/reference/visionfive2-gpu.md。

## 追記 2026-10-21 08:40

- 非同期の submit（P1 2e3e67418、main e4bdff188 以降）を入れると desktop で 3D の lockup が毎回出る（d19）。P1 に切り替え（既定 off）と原因の調査を依頼中。板を使う前に、切り替えが off の main か、非同期の前（f490a65b8）の kernel を使う。
- DC8200 の vblank は IRQ の unmask（aedded233）の後も 0（P2）。cursor plane は Keiland が開く（d16）。
- SD の I/O の失敗（BUG-343）が d18 で重く出た（root の書き込み停止・segfault）。結果を読む前に `refuses writes`・`killed by signal` を確かめる。

## 追記 2026-10-21 09:15

- **DC8200 の zero copy の flip が動いた**（d22・d23、`frames scanned directly`、vblank が数える）。原因は P2 の診断の read が割り込みを消していた事（bb98e59d5 で整理）。cursor plane は Keiland が開く。
- **3D の lockup は複数の session が同時に 3D を使うと起きる**（3 つの vkmatrix を同時に流して再現）。単独では load-1080-* は全部通る。P1 が session 間で共有している物を調べる（q1530）。非同期の submit は既定 off（POWERVR_ASYNC_SUBMIT 0）。
- 二分探索用の一時の sysctl `hw.gpu.powervr.stages=N`（P1 b07ea4a73、coding-style §12 の一時の例外、Q1 承認、原因が分かったら外す）。session の起動し直しは `sudo service restart greeter`（autologin する）。補助の script は scratchpad の bisect.sh（host 側、残さない）。

## 追記 2026-10-21 09:45

- **lockup の正体: BIF fault（GPU の MMU の fault）**。P2 の HWR の dump（37645e247）で `type 3`、page catalogue 0x58ae9001。複数 session の同時の描画で起きる。再現: `sudo /tmp/vkmatrix -p 3 -r 3 load-1080-textured`（desktop を止めずに、build/uat/vf2-gpu/bin/vkmatrix を /tmp に scp）。疑い: FW の memory context の page_cat_base_reg_set を全 session で同じ値にしている、または MMU cache の invalidate。P1 が修正中。
- 同時の試験の途中で板が log 無しで再起動する事がある（d25）。dmesg は host から 1 秒ごとに取る（scratchpad の cap の loop）。

## 追記 2026-10-21 10:30

- lockup（BIF fault、IPF CREQ が address 0）の調べの経過: context switch off（f9aa75bc9）、DM の overlap off（680f339b0）、geometry が前の 3D を待つ（679e119bb）はどれも効かず。fault の PM の catalogue は fault した session 自身の local free list の中、grow・partial render は起きていない。次の疑い: 別 context の TA が間に入った時の PM の state の保存・戻し（HWRT の mlist・vheap 表・macrolist の half 共有）。P1 が Mesa の rt_data と照合中。login だけなら reset 0（d29・d30）。

## 追記 2026-10-21 09:55 — lockup 解決

- **3D の lockup は直った**（d34、P1 562c6550c: render が target の 2 つの half を交互に使う）。原因: half 0 だけを使っていたので、別 session の 3D の transfer だけが間に入ると firmware が PM の引き継ぎ（Loading stack-pointers）を飛ばし、3D が前の TE の表を辿った（P2 の trace の解読）。
- 板: 同時の試験（`vkmatrix -p 2 -r 10 load-1080-6`、`-p 3 -r 3 load-1080-textured`）全部 PASS、HWR 0。desktop で 3 window、vkmatrix 29/29、vkcoherency PASS。
- U36 の SD の image を作り直した（main 78391c8a1、sha256 f68cf6c2…、host key 入り）。
- 次: P1 q1533（後片付け、非同期の submit と DM の overlap を on にして試し直す＝速さ）、P2 q1534（transfer の速さ）。板の確認は vkmatrix の -p の試験、vkreset（desktop を `sudo service stop greeter` で止めて）、desktop。

## 最終の引き継ぎ 2026-10-21 12:35（このセッションの終わり）

ユーザー:「別のセッションに引き継ぎます。ハンドオーバーの資料をアップデートして、ラップアップ、オフボードしてください。」P1・P2 は 12:30 に週次の上限で止まった（解除は 2026-10-25 04:00 JST）。両者の worktree に main に無い作業は無い（P1 の tip 7ca8ed527 は clock の計測を on にした使い捨て、P2 の tip 8ffca3039 も main と同じ内容）。担当は終了扱い。新しい session は新しい担当を起動してよい（古い担当の worktree は /home/awe/zedBSD-worktrees/p1・p2、main を merge してから使う）。

### 今の到達点（main 550a402f2 以降、板は d47 = main の kernel）

- **VF2 で Keiland の desktop が PowerVR で動く**: Settings・Terminal・Files の window、linear の swapchain を DC8200 が直接 scanout（zero copy の flip、vblank）、hardware cursor、cache のノイズは解消。
- **3D の lockup は解決**（HWRT の half の交互、d34）。非同期の submit と DM overlap は on で安定。watchdog は全 session の timeline が 5 s 動かない時だけ（b88dc3975）。vkreset PASS。
- 試験: vkmatrix 36/36（`-p 2`・`-p 3` の同時の試験も PASS、HWR 0）、vkcoherency PASS、gpu-selftest 13/13（desktop が display を持つ時は present の 2 段が fail するのは想定どおり）。
- U36 の SD の image: build/uat/vf2-desktop/native.img（main 78391c8a1、09:55、host key 入り）。その後の kernel・userland の変更は板に overlay と run.sh で入れてある。新しい image が要る時は手順どおり作り直す。

### 残る大きな問題: 遅い（全画面の draw 1 つで約 15 ms）

- 計測（vkmatrix の perf、1920x1080、board d42〜d46）: clear 3.85 ms、定数の色の quad 15.1 ms、flat 15.55 ms、1/4 面積 7.2 ms、402 op の ALU 268.75 ms、texture 38 ms。
- 分かった事: GPU の clock は実測 395.65 MHz（設定どおり）。ALU は 0.32 ns/op/pixel（妥当）。命令数に依らない固定費が約 5.4 ns/pixel。
- 原因でないもの: shader の長さ（P2 の短縮で 1 割）、temps・occupancy（48 に固定しても同じ）、program・定数の SLC bypass（試しで変化なし）、clock、dual-phase（Mesa も使わない）、clear の 3D の command（Mesa と同じ）。
- **手掛かり**: clear（background object も pixel ごとに USC を走らせる）は 3.85 ms と速いのに、全画面の 2 三角形の draw は約 15 ms → 差は draw の道（TA・ISP の primitive の処理、coefficient の PDS、pixel の出力、pass type OPAQUE と Mesa の PUNCH_THROUGH の違い、pds_tempsize 1 と 0）。
- 次の手（中断した q1536-i06・q1537-i04）: (1) 全画面の quad の PPP・ISP・PDS の語を Mesa（P2 の drm-shim の harness、scratchpad の pco/、BVNC 36.50.54.182）と項目ごとに比べる、(2) PUNCH_THROUGH の試しの kernel、(3) quad を 1・2・8・128 の三角形に分けた perf の case で、時間が三角形と tile の対に比例するか見る。
- 危険: device-local の memory を SLC に通す変更（POWERVR_DEVICE_MEMORY_CACHED 1）は板を log 無しで再起動させた → 0 のまま。

### 板の注意

- SD の read の失敗（BUG-343）が多く、起動に電源の入れ直しが 2〜3 回要る事がある（P2 の dw-mmc の直しで実行中の EIO は止まった）。SD の card の交換を検討（ユーザーの判断）。
- `sudo service stop greeter` は kei の SSH の session も閉じる（つなぎ直せばよい）。vkreset は desktop を止めてから。
- 試しの kernel を入れたら、必ず板の /boot/boot0/vmunix を取り出して cmp で確かめてから結果を読む。

### 記録

- 板の経過の全部: plan/ws270/phase006a/phase.md（d1〜d47）。lockup の原因と直し: plan/ws269/phase015/phase.md。
- Queue: plan/queue.md の q1517〜q1537（q1536・q1537 は中断）。Master の next: BUG-345、q1538、WS278。
