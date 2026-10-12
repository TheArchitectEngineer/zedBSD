<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread・cursor の thread・入力の thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: planned（p003 の閾値を満たした後、p001 の S2 の部分の確認（第 5 版、review-4 の差分）を通ってから）

## 依存と再開の条件（review-4 L-f）

- p003 の cleared と、p003 の 5 の閾値（walk の p90・最長）を満たすこと。満たさなければ WS211 の描画の削減（P3）を先にし、その結果で p003 の 5 を測り直して満たした後に着手する。
- 受け入れ 5 の受け箱の間隔を 1 core で満たせない原因が kernel の中の長い transaction（p001 の 11 節 9）なら、uncleared にして WS141・WS208 への依存として Q1 に返す。
Phase disposition: normal

## 範囲

- p001 の 2〜6 節: want（番号は種類の変化だけ、heads の slot と世代）と ready、取り消し、R の持つ画像（出力の k と slot ごとの head、取り消しと park を越えて使い回す）、compare-and-clear、R は ready の後 Vulkan を呼ばない不変条件、破棄は R の step 1、R と C の両方を止める park と失敗の `_exit`、R→M の 1 本の FIFO、lock の規則（4.6）、host image の書き込みは walk と park の中だけ、未着の glyph の list、cursor の 2 枚（中身の大きさ、全体の写し、pin、plane の元の制限）、cursor の thread C（C0 を C へ、専用の command pool、M と C の持ち物の分け方、失敗の経路、計数）、入力の thread I（evdev・touchpad・加速・位置を受け箱へ、M へ順序付きの FIFO、warp の番号）、client の破棄の延ばし（4.9）、Mesa の build の queue_lock（新しく足す）。
- `kwl_compose_quiesce` と `->in_flight` の全部の割り当て、`KWL LAT` の定義し直し、`KWL PERF main` の `pass_max_us`・`walk_max_us`・`vk_outside_max_us` と `KWL PERF cursor`（p001 6.4）。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/` に p001 の 9 節 (b)〜(i)・(k)・(l) の決定的な試験（M・R・C・I の手順を 1 歩ずつ）と (m) の本物の thread と stub の Vulkan の TSan の試験、全部 PASS（TSan の報告 0、外部同期の object の重なり 0）。
2. build warning 0（下のコマンド）。
3. Linux の build を ThreadSanitizer 付きで、WS131 の Debian の QEMU+KVM guest で compositor を 10 分動かし（guest の uinput を SSH で: pointer の円 60 秒、App Home の開閉 10 回、title の drag 30 秒、app の起動）、WS213 の thread と lock に触れる報告 0（T1）。Mesa の中は `called_from_lib:` で抑え、既存の thread どうしの報告は `plan/ws213/tests/tsan.supp` に理由付きで置き Q1 に ticket を頼む（p001 9 節）。C はこの guest では通らない（plane が開かない、p001 3.3）。
4. T1（i915・Venus）: shot の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
5. Pi（1 boot、ユーザーか Q1 の手で、p001 6.4）: 場面 (1) pointer だけ 10 秒、(2) pointer を動かしながら App Home を 5 回開閉、(3) 窓の drag 10 秒、(4) Music・Settings の起動 1 回ずつ。各場面の `KWL PERF cursor` の `box_gap_p99_us`・`box_gap_max_us` ≤ 16700（**必達**）、`KWL PERF main` の `pass_max_us` ≤ 33300 と `vk_outside_max_us` ≤ 16700、`KWL LAT` の入力→walk の中央値が p003 より改善、C の present の全段の最長（`present_max_us` と内訳）を記録（画面の cursor の 60fps の判定は WS141 p016 (B) の後）。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p004/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p004/arm64 build/ws213-p004/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p004/amd64 build/ws213-p004/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p004/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。
