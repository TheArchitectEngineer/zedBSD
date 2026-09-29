# ws073-p028: make の Remacs 用 Noct の失敗を直す

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-100](../../bugs/BUG-100.md)
Queue: [q496](../../history/queue-q496.md)、q496-i01

## 範囲と受け入れ

Remacs が使う host Noct の build の失敗を再現し、現在の canonical Noct で Remacs の bytecode を再生成できるようにする。外部 source は patch で修正する。対象は `userland/packages/editors/remacs/Makefile` と同 package の patch のみ。

## 結果

- Remacs 専用の古い `build/host-noct-package` の CMake build を廃止し、`NOCT_HOST_ARTIFACT` とその build stamp を使う。
- Remacs の取得した source の `tools/build-nap.sh` に `.nbc` 出力名の patch を適用する stamp を追加した。既存 source にも新規 clone にも同じ rule を使う。
- 失敗前: `make -j16` は `CMAKE_HOME_DIRECTORY` の不一致で停止。修正後: `make noct-toolchain-smoke` PASS、Remacs の bytecode を現行 Noct で再生成して package target PASS。
- 全体の `make -j16` は clang package の 5717 step の build に進み、時間を限定して中断した。全体の image と boot test、実機は未実施。

Resume: 全体 build が必要な時に継続。Noct と Remacs の対象は確認済み。
