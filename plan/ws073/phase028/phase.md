# ws073-p028: make の Remacs 用 Noct の失敗を直す

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-100](../../bugs/BUG-100.md)
Queue: [q496](../../history/queue-q496.md)、q496-i01 → [q497](../../history/queue-q497.md)、q497-i01 → [q498](../../history/queue-q498.md)、q498-i01 → [q499](../../queue.md)、q499-i02

## 範囲と受け入れ

Remacs が使う host Noct の build の失敗を再現し、現在の canonical Noct で Remacs の bytecode を再生成できるようにする。外部 source は patch で修正する。対象は `userland/packages/editors/remacs/Makefile` と同 package の patch のみ。

## 結果

- Remacs 専用の古い `build/host-noct-package` の CMake build を廃止し、`NOCT_HOST_ARTIFACT` とその build stamp を使う。
- Remacs の取得した source の `tools/build-nap.sh` に `.nbc` 出力名の patch を適用する stamp を追加した。既存 source にも新規 clone にも同じ rule を使う。
- 失敗前: `make -j16` は `CMAKE_HOME_DIRECTORY` の不一致で停止。修正後: `make noct-toolchain-smoke` PASS、Remacs の bytecode を現行 Noct で再生成して package target PASS。
- 全体の `make -j16` は clang package の 5717 step の build に進み、時間を限定して中断した。全体の image と boot test、実機は未実施。

Resume: 全体 build が必要な時に継続。Noct と Remacs の対象は確認済み。

## 2026-09-29 の追補（q497-i01）

ユーザーの `make` で、すでに `.nbc` に直した Remacs source に stamp の rule が再度 patch を当てようとして、reverse patch と `.rej` で失敗した。q496 の「新規 clone にも同じ rule」を満たしていなかったため、clearance を無効にして再試行した。rule は script の出力名を検査し、`.nbc` なら patch 済みとして通し、`.nb` なら patch を当て、それ以外は理由付きで停止する。

確認: 既適用の `build/sources/remacs` で `make -j16 build/amd64/packages/editors/remacs/remacs.nap` PASS。未適用の `tools/build-nap.sh` を作業用の scratch に複写し、同じ make rule で patch が当たり `.nbc` になることを確認。失敗時の `.rej` は内容を確かめて除去。全体の `make`、QEMU、実機は未実施。q497-i01 cleared。

## 2026-09-29 の `make` の完走（q498-i01）

ユーザーの指示で `noct-toolchain-smoke` を `toolchain` の依存から外した。`make toolchain-cache` は `accepted build/llvm is already present`。通常の `make` がビルドしていたのは image に入るゲスト用 clang（ユーザーはソースビルドを続けると決定）で、ホスト用 toolchain の再ビルドではない。

`make -j16` はゲスト用 clang の build と stage を終え、`check-amd64-native-image: ...: OK` で完走（`build/codex-make/make-resume.log`）。続く通常の `make` も同じ image の検査まで PASS（`build/codex-make/make-second.log`）。Noct と Remacs の失敗は再発しなかった。外部 LLVM/LLDB source の warning と、未追跡の `userland/noct/` の `clean` rule の重複 warning が残る。QEMU と実機は未実施。q498-i01 cleared。

## 2026-09-29 の旧 Noct checkout 整理（q499-i02）

ユーザーは userland/base/noct/ を正規の場所として再確認。旧 userland/noct/ が残ると、トップレベル Makefile の三階層 wildcard が userland/noct/apps/remacs/Makefile を読み、clean のレシピ衝突 warning を起こした。USERLAND_PACKAGE_MAKEFILES から旧パスを除外し、変更のない旧 submodule checkout を削除した。canonical な target source は userland/base/noct/noct、host source は build/NoctLang。make -qp の展開に旧パスはなく、通常 make PASS、warning なし、git diff --check PASS。既存の host/target Noct 実行ファイルを確認。
