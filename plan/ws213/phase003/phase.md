<!-- awesome-plan project=zedbsd record=ws213-p003 -->

# ws213-p003: S1: 描く物の一覧（display list）を同じ thread で再生する

Parent: [WS213](../ws.md)
Status: planned（review-3 で条件付きで可。着手は Q1 の指示: WS211 の P3（p003 段 2・p006 の drag）と順を決めてから）
Phase disposition: normal

## 範囲

- `struct kwl_list` と op、描画の関数の `VkCommandBuffer command` の引数（37 file）を `struct kwl_list *` に。compose->command を直に使う所（`compose.c:2365〜`、`layer-cache.c:184`）も grep で全部挙げて一覧へ。
- batch（WS211 p003）の頂点を一覧に。bind の省略の鍵を（import、種類、世代）に、`struct kwl_import` に作成ごとの世代の番号。
- cursor の形の画像を起動時に host image の batch（`shm.c:354-392`）で全部作る（review-3 M-6）。
- 再生は今の `compose_record` と同じ所・同じ thread。thread はまだ使わない。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/list-replay-host-test.sh`（p003 で作る）で、p001 の 9 節 (a) の全場面の命令の列が直の記録と一致（descriptor set の handle は（import、種類）に正規化）。golden は p003 の最初の commit で取る。
2. build warning 0（下のコマンド）。
3. T1（i915 の 5330・Venus の QEMU）: shot.c の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
4. Pi: `KWL STARTUP` の READY の遅れが +50ms 以内。walk と再生の時間を分けて `KWL PERF` に出し、pointer だけ・App Home の開閉・drag で測る（1 boot、各 10 秒程度）。
5. **p004 に進む閾値**（記録する、満たさなくても p003 は cleared にできる）: Pi で walk の中央値が普通のコマ（100 draw 未満）4ms 以下、App Home のコマ（300 draw 以上）15ms 以下。

## コマンドと上限

- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland`、`make -j16 keiland-linux`（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
