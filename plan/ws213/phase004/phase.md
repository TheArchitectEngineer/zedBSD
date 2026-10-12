<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread・cursor の thread・入力の thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: planned（p003 の閾値を満たした後。p001 の S2 は review-5 で条件付き可、第 6 版の N-1〜N-4 の反映を Q1 が確かめてから）

## 依存と再開の条件（review-4 L-f）

- p003 の cleared と、p003 の 5 の閾値（walk の p90・最長）を満たすこと。満たさなければ WS211 の描画の削減（P3）を先にし、その結果で p003 の 5 を測り直して満たした後に着手する。
- 受け入れ 5 の box_lag の最長を 1 core で満たせない原因が kernel の中の長い transaction（p001 の 11 節 9、`KWL CURSOR_LAG` と kernel の `hold_us` の時刻の対応で確かめる）なら、uncleared にして WS208（SMP）・kernel への依存として Q1 に返す（Q1 の判断）。p99 は必達のまま。
- C と R が同じ VkQueue を同時に使うことの libvulkan の安全は WS141 p016 B-u が明記する前提（p001 6.3、review-B4 M-d）。
- p004 の始めに確かめる事（p001 11 節 3・9・12）: `event_us` と `kwl_microseconds` の時計が同じ基準か（違えば box_lag の代わりに超過分で判定）、zedBSD の evdev の fd を別の thread から `poll` してよいか、kernel の preempt と tick の周期・decode が preempt を止めるか、I の pthread の優先度を上げる手段、`seat-freebsd.c` の fd を閉じる順。
Phase disposition: normal

## 範囲

- p001 の 2〜6 節: want（番号は種類の変化だけ、heads の slot と世代）と ready、取り消し、R の持つ画像（出力の k と slot ごとの head、取り消しと park を越えて使い回す）、compare-and-clear、R は ready の後 Vulkan を呼ばない不変条件、破棄は R の step 1、R と C の両方を止める park と失敗の `_exit`、R→M の 1 本の FIFO、lock の規則（4.6）、host image の書き込みは walk と park の中だけ、未着の glyph の list、cursor の 2 枚（中身の大きさ、全体の写し、pin、plane の元の制限）、cursor の thread C（C0 を C へ、専用の command pool、M と C の持ち物の分け方、失敗の経路、計数）、入力の thread I（evdev・touchpad・加速・位置を受け箱へ、M へ順序付きの FIFO、warp の番号）、client の破棄の延ばし（4.9）、Mesa の build の queue_lock（新しく足す）。
- review-5 と WS141 review-B4 で足した物（p001 第 6 版）: device の付け外しを I への依頼に（付ける、同期の外すと上限付きの ack、backend の 3 つの callback と session の pause、logind の resume の close の順の入れ替え）、絶対の pointer を I が受け箱に（warp の番号を進めない）、I の位置の計算と M の warp を `cursor_lock` の 1 区間で（`band_replay` は一時の書き換え）、I と M の持ち物の表、`set_cursor` の写し元、C の copied の gate を acquire した画像の fence だけに（pin は飛んでいる copy の数）、import の layout の溢れと shm の画像の layout を一覧の barrier にして M の `vkQueueWaitIdle` を外す、`KWL CURSOR_LAG` と box_lag の計数。
- `kwl_compose_quiesce` と `->in_flight` の全部の割り当て、`KWL LAT` の定義し直し、`KWL PERF main` の `pass_max_us`・`walk_max_us`・`vk_outside_max_us` と `KWL PERF cursor`（p001 6.4）。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/` に p001 の 9 節 (b)〜(i)・(k)（(k1)〜(k4) を含む）・(l) の決定的な試験（M・R・C・I の手順を 1 歩ずつ）と (m) の本物の thread と stub の Vulkan の TSan の試験、全部 PASS（TSan の報告 0、外部同期の object の重なり 0）。
2. build warning 0（下のコマンド）。
3. Linux の build を ThreadSanitizer 付きで、WS131 の Debian の QEMU+KVM guest で compositor を 10 分動かし（guest の uinput を SSH で: pointer の円 60 秒、App Home の開閉 10 回、title の drag 30 秒、app の起動）、WS213 の thread と lock に触れる報告 0（T1）。Mesa の中は `called_from_lib:` で抑え、既存の thread どうしの報告は `plan/ws213/tests/tsan.supp` に理由付きで置き Q1 に ticket を頼む（p001 9 節）。C はこの guest では通らない（plane が開かない、p001 3.3）。
4. T1（i915・Venus）: shot の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
5. Pi（1 boot、ユーザーか Q1 の手で、p001 6.4）: 場面 (1) pointer だけ 10 秒、(2) pointer を動かしながら App Home を 5 回開閉、(3) 窓の drag 10 秒、(4) Music・Settings の起動 1 回ずつ。各場面の `KWL PERF cursor` の `box_lag_p99_us` ≤ 16700（**必達**）と `box_lag_max_us` ≤ 16700（超えた原因が kernel の長い transaction なら上の依存で uncleared、`KWL CURSOR_LAG` と `hold_us` の時刻の対応を結果に記録。時計が確かめられなければ `box_excess_p99_us`・`box_excess_max_us` ≤ 16700 を代わりに）、`KWL PERF main` の `pass_max_us` ≤ 33300（場面 (1)〜(3)。(4) の app の起動は `pass_p99_us` を記録）と `vk_outside_max_us` ≤ 16700、`box_gap` は記録だけ、`KWL LAT` の入力→walk の中央値が p003 より改善、C の present の全段の最長（`present_max_us` と内訳）を記録（画面の cursor の 60fps の判定は WS141 p016 (B) の後）。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p004/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p004/arm64 build/ws213-p004/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p004/amd64 build/ws213-p004/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p004/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- build の注記（p001 review-5 の答え (4)、Q1）: `BUILD=` の override は動き（`Makefile:79`）、libc.so なども自分の dir に作るので最初の build は長い（`vmunix.mk:411`）。LLVM と sysroot は共有のまま使う（wayland は `$(DYNAMIC_SYSROOT)/usr/lib/crt1.o` に依存、`userland-dynamic.mk:239`）。**make が共有の sysroot を作り直そうとしたら（sysroot の規則が動き出したら）止めて main に報告する**。toolchain と共有の `build/` は変えない。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
