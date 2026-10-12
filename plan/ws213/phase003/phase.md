<!-- awesome-plan project=zedbsd record=ws213-p003 -->

# ws213-p003: S1: 描く物の一覧（display list）を同じ thread で再生する

Parent: [WS213](../ws.md)
Status: planned（review-3・review-4 で条件付きで可。着手は Q1 の指示: WS211 の P3（p003 段 2・p006 の drag）と順を決めてから）
Phase disposition: normal

## 範囲

- `struct kwl_list` と op、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に。compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）も grep で全部挙げて一覧へ。
- batch（WS211 p003）の頂点を一覧に。bind の省略の鍵を（import、種類、世代）に、`struct kwl_import` に作成ごとの世代の番号。
- cursor の形の画像を起動時に host image の batch（`shm.c:354-392`）で全部作る（review-3 M-6）。その時間を `KWL STARTUP step=cursor-shapes ms=` で独立に出す（review-4 L-c）。
- walk と再生の時間を分けて `KWL PERF` に出す（walk の中央値・p90・最長、draw の数で 100 未満と 300 以上に分けて）。
- 再生は今の `compose_record` と同じ所・同じ thread。thread はまだ使わない。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/list-replay-host-test.sh`（p003 で作る）で、p001 の 9 節 (a) の全場面の命令の列が直の記録と一致（descriptor set の handle は（import、種類）に正規化）。golden は p003 の最初の commit で取る。
2. build warning 0（下のコマンド）。
3. T1（i915 の 5330・Venus の QEMU）: shot.c の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
4. Pi: `KWL STARTUP step=cursor-shapes` の ms が 50ms 以下（READY 全体の差は boot ごとの揺れと同じ大きさなので使わない、review-4 L-c）。walk と再生の時間を pointer だけ（10 秒）・App Home の開閉（5 回）・drag（10 秒）で測る（1 boot、ユーザーか Q1 の手で）。
5. **p004 に進む閾値**（記録する、満たさなくても p003 は cleared にできる。review-4 H-B で中央値から p90 と最長に）: Pi で walk が普通のコマ（100 draw 未満）の p90 8ms 以下かつ最長 16.7ms 以下、App Home のコマ（300 draw 以上）の p90 16.7ms 以下かつ最長 25ms 以下（p001 review-5 N-6 の Q1 の判断。33.3ms では p004 の `pass_max` ≤ 33.3ms に余裕が無い）。満たさなければ WS211 の削減を先にし、p004 はその後（p004 の依存）。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p003/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p003/arm64 build/ws213-p003/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p003/amd64 build/ws213-p003/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p003/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- build の注記（p001 review-5 の答え (4)、Q1）: `BUILD=` の override は動き（`Makefile:79`）、libc.so なども自分の dir に作るので最初の build は長い（`vmunix.mk:411`）。LLVM と sysroot は共有のまま使う（wayland は `$(DYNAMIC_SYSROOT)/usr/lib/crt1.o` に依存、`userland-dynamic.mk:239`）。**make が共有の sysroot を作り直そうとしたら（sysroot の規則が動き出したら）止めて main に報告する**。toolchain と共有の `build/` は変えない。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
