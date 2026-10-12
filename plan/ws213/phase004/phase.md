<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread と cursor の thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: planned（p003 の閾値を満たした後、p001 の S2 の部分の再 review を通ってから）
Phase disposition: normal

## 範囲

- p001 の 2〜6 節: want（番号・種類・heads の slot と世代）と ready、取り消し、R は ready の後 Vulkan を呼ばない不変条件、非同期と同期の park と失敗の `_exit`、R→M の 1 本の FIFO、host image の書き込みは walk と park の中だけ、未着の glyph の list、cursor の shm の 2 枚、cursor の thread C（C0 を C へ）、Mesa の build の queue_lock。
- `kwl_compose_quiesce` と `->in_flight` の全部の割り当て、`KWL LAT` の定義し直し。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/` に p001 の 9 節 (b)〜(i) の決定的な試験（M・R・C の手順を 1 歩ずつ）、全部 PASS。
2. build warning 0（下のコマンド）。
3. Linux の build を ThreadSanitizer 付きで、WS131 の Debian の QEMU+KVM guest で compositor を 10 分動かし（App Home の開閉・窓の drag・app の起動）、報告 0（T1）。
4. T1（i915・Venus）: shot の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
5. Pi: walk の外の M の Vulkan の最長 ≤ 16.7ms、`KWL LAT` の入力→walk の中央値が p003 より改善、C の present の全段の最長を記録（画面の cursor の 60fps の判定は WS141 p016 (B) の後）。

## コマンドと上限

- build（warning 0）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/wayland`、`make -j16 keiland-linux`（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
