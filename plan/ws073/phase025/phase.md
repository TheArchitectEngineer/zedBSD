<!-- awesome-plan project=zedbsd record=ws073p025 -->

# ws073-p025: 新しい build の最初の image に clang の resource の header が入らない（BUG-087）

Status: uncleared（2026-09-28、wrap up。修正は未検証で `plan/bugs/BUG-087-wip.patch` に置き、tree には入れていない）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-087](../../bugs/BUG-087.md)（main の依頼、2026-09-28）

## 目的と受け入れ

新しい checkout（clang の stage が parse の時に無い）の 1 回の make で作る rootfs に、stage の clang の resource の header（`stddef.h` など、
`/usr/lib/clang/23/include/*.h`）が全て入る。

## 原因（確認）

- `userland/packages/lang/clang/Makefile` の `ZEDBSD_CLANG_RESOURCE_HEADERS` は stage への `$(wildcard)`。
- rootfs の tree の rule（`Makefile` の `ZEDBSD_ROOTFS_TREE_RULE`）は `$(ZEDBSD_PACKAGE_FILES)` を `$(call)` の時、つまり make の parse の時に展開する。
  stage が無い parse ではこの一覧は空で、同じ make で stage が作られても rootfs の recipe の `--file` に header は無い。次の make で config の
  stamp が変わって rootfs を作り直すまで、最初の image に header が欠ける。
- 仕組みの確認（GNU Make 4.4.1、scratchpad の小さな Makefile）: 同じ make の prerequisite が作る directory への一覧は、parse の時の展開では `[]`、
  recipe の時の展開（`$$(...)`）では作られた file を全て含む。

## 修正（未適用、`plan/bugs/BUG-087-wip.patch`）

- `Makefile` の `ZEDBSD_ROOTFS_TREE_RULE`: package の file の一覧を recipe の時に展開する（`$$(ZEDBSD_PACKAGE_FILES)`）。rootfs の prerequisite の
  `ZEDBSD_PACKAGE_INPUTS`（clang の `.zedbsd-staged` を含む）が作られた後。package の Makefile は platform の Makefile より前に読まれるので、
  展開の値は stage が同じなら以前と同一。
- clang の Makefile: 一覧を `$(shell find ... -maxdepth 1 -type f -name '*.h' | LC_ALL=C sort)` に（make の directory の cache に左右されない）。

## 検証

- [tests/bug087.sh](../tests/bug087.sh): stage の resource の directory を parse の間だけ隠し、rootfs の prerequisite が戻す（新しい checkout で
  同じ make に stage が現れるのと同じ）。stage は main の `build/packages` から worktree へ複写し `make -o` で固定。
- 2 回の実行は試験の側の誤りで build が止まった（修正の判定まで届いていない）。戻す rule を `--eval` で渡したため、MAKEFLAGS で継いだ
  noct の cmake の sub-make がその最初の rule を goal にした（1 回目は sub-make が戻しを 2 度実行、2 回目は sub-make が Error 1）。
  script は戻す rule を 2 つ目の `-f` の makefile に置く形に直した（未実行）。stage の directory は元に戻っている。
- 未実施: 修正前の tree での FAIL（再現）、修正後の PASS、image の build 全体。

## 再開の手順

1. worktree の `build/packages` に main の package の stage（clang は `stage` と `src/llvm/LICENSE.TXT`）があることを確かめる（p025 で複写済み）。
2. 修正前: `sh plan/ws073/tests/bug087.sh build/ws073-b087 48` → rootfs の header が 0 で FAIL を期待（再現）。
3. `git apply plan/bugs/BUG-087-wip.patch`、同じ script → PASS を期待（stage と同じ数、245 前後）。
4. 通れば patch を commit して patch の file を消し、p025・BUG-087 を閉じる。注意: config の stamp は recipe で古い一覧を記録するので、最初の
   build の次の make で rootfs が 1 度作り直される（害は無い）。
