<!-- awesome-plan project=zedbsd record=ws074p003 -->

# ws074-p003: GC heap の核、VM の string と atom

Phase ID: `ws074-p003`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

design.md §11.2・§11.3 の GC heap（非移動の mark-sweep、64 KiB の block の大きさの class、大きな cell の個別の allocation、保守的な
C の stack の走査、正確な trace、root の slot と tracer、finalizer、上限）と、VM の string（Latin-1 か UTF-16、自動の narrow）と
atom（intern の表）。`userland/desktop/browser/vm/{vm.h,internal.h,heap.c,string.c,atom.c}`。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check 0 件。
2. host の `host-heap` が plain と ASan で全部通る。guest でも同じ試験が通る。
3. boot test。

## 結果（2026-09-27）

cleared。

- heap: 29 の大きさの class（16〜4096 byte）。4096 byte を超える cell は `posix_memalign` の個別の allocation を address 順に保ち
  二分探索する。block の address の集合（open addressing）で stack の word から cell を引く（cell の内側を指す pointer も有効）。
  collection は `__builtin_unwind_init` で callee-saved の register を frame へ吐き出し、`__builtin_frame_address(0)` から stack の
  底まで走査し、atom の表・root の slot・tracer から mark し、型の trace で辿り、sweep で finalize して free list を作り直し、空の
  block を返す。次の collection は前回の生存量（最低 8 MiB）を allocate した時。上限を超える allocation は collection の後も
  超えれば NULL。
- string: header（24 byte）の後に文字。UTF-16 から作ると全部 256 未満なら Latin-1 に narrow（同じ文字列は常に同じ形）。hash は
  FNV-1a で Latin-1 と UTF-16 が一致。連結、比較（UTF-16 の unit 順）、UTF-8 への変換。
- atom: heap ごとの open addressing の表。永続（collection のたびに全部を mark）。`vm_atom_find_units` は allocate せずに探す。
- 試験（`plan/ws074/tests/host-heap.c`、31 検査）: garbage の finalize と free、stack の local が保つ cell、root の slot の
  20000 個の list、cell の内側の pointer（小・大）、捨てた大きな cell が溜まらないこと、tracer、40 万回の無作為の graph の負荷
  （185 回の collection、到達できる cell の内容が壊れない）、上限、string、atom。
  - host plain: 31/31。host ASan・UBSan（`ASAN_OPTIONS=detect_stack_use_after_return=0`）: 31/31。
  - guest（QEMU、`plan/ws074/tests/guest-build.sh` で zedBSD 向けに build、GPU 無しの guest）: 31/31、1.7 秒。host-base も 0。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p003-20260927-boot-login.png`）。
- **分かったこと**:
  - stack の底は main の変数の address ではなく `__builtin_frame_address(0)` にする（compiler が他の変数をその上に置くと保守的な
    走査から漏れる。ASan の build で実際に漏れて use-after-free になった）。走査の側も自分の local の address ではなく frame の
    address を使う（ASan が address を取った local を別の stack に移すため）。
  - 保守的な collector は「特定の cell が必ず解放される」ことを約束できない（古い stack の word が残る）。試験は「捨てたものが溜まら
    ない」ことを確かめる形にした。
  - ASan の試験は `ASAN_OPTIONS=detect_stack_use_after_return=0` で走らせる（host-build.sh に記した）。
