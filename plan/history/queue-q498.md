# Queue q498: smoke を外し通常の make を完走する

Status: finished（2026-09-29）
Approval: ユーザー「smokeは不要なので、makeだけ通るようにしてください。」。`make toolchain-cache` でホスト用 LLVM の cache を使い、ゲスト用 clang のソースビルドは続けるとの追補の回答。
Scope: `toolchain` から Noct smoke の依存を外し、通常の `make` を完走させる。

| Order | Attempt | Phase | Status |
| --- | --- | --- | --- |
| 1 | q498-i01 | [ws073-p028](../ws073/phase028/phase.md) | cleared（`make -j16` と通常の `make` が image 検査まで PASS） |

`make toolchain-cache` は既存の `build/llvm` を受け入れた。ゲスト用 clang は source から build。QEMU・実機は未実施。GitHub へは未公開。
