# ws274-p008 VM の接頭辞の無い helper 65 に接頭辞を付ける

- 親: [WS274](../ws.md)、Queue: q1306 / q1306-i01（P1、2026-10-19）
- Status: cleared（2026-10-19 Q1 の判定、T1-741: amd64 の uefi-usb・nvme、pcat の bios-ide、pc98、sun4u の起動の回帰 PASS）
- ユーザーの決定（2026-10-19、原文）:「接頭辞をつけます。それらを宣言するヘッダに内部APIであることを書いておき、将来的に保証されないことを示して、公開のVMシステムのヘッダでは宣言されないことに気をつけてください。」
- 範囲: p007 の一覧の 65 の名前を変える（宣言と呼び出しだけ、本文は変えない）。hal.h は変えない。

## 変更（9d489a216）

- 名前の対応は [p008-rename.txt](../tests/p008-rename.txt)（旧 新、65 行）。object-internal.h の群は `vm_object_`（元の `object_` は外す。`destroy_object`→`vm_object_destroy` など重複語も外す）、reclaim-internal.h は `vm_reclaim_`、vmspace-internal.h は `vmspace_`。新しい名前は変更前の src/・include/ に 1 つも無い事を grep で確かめた（衝突 0）。
- 置換は src/kern/vm/ の 23 file だけ。注記の中は `name(` の形だけ変え、「it overlaps」などの文は変えていない。
- 3 つの内部の header の頭に「Internal VM API: used only between the files in src/kern/vm/. Its compatibility is not guaranteed ... None of these declarations belongs in the public headers under include/kern.」を足した。
- include/ の grep: 旧名・新名とも 0。
- 試験の追従: pin-host-test.sh の find_region_locked → vmspace_find_region_locked。body-compare.py の --rename を、注記の中では `old(` だけ置き換えるように直した（文の中の語で偽の差が出たため）。

## 確かめ

- build: `make -j16 ZEDBSD_CONFIG=<config> vmunix`、amd64（LTO）・pcat・sun4u、base（e24176cfd を git archive）と after の両方で exit 0、warning 0。
- body-compare: `python3 -I plan/ws274/tests/body-compare.py $(awk '{printf "--rename %s=%s ",$1,$2}' plan/ws274/tests/p008-rename.txt) HEAD~ src/kern/vm/*.c -- src/kern/vm/*.c` → functions 424 names 423 differences 0 PASS（--rename 無しでは FAIL）。
- nm（llvm-nm の名前と型）: sun4u は差が旧 65 / 新 65 で全部が対応の通り、他 0。amd64・pcat は LTO の static の番号の接尾辞（.NNNN）を除くと差 36/36 で全部が対応の通り（残りの 29 は LTO で inline され base・after とも無い）、他 0。
- host: kd-host-test PASS、pin-host-test PASS。
- 未実施: QEMU の起動（下）。

## T1 への依頼（WS274 p008、起動の回帰）

worktree agent/p1 の 9d489a216 以降（または Q1 が merge した main）で、名前だけの変更の後の起動の回帰。p007 の依頼とまとめて 1 つの QEMU で流してよい。

1. `plan/tools/boot-test.sh` の uefi-usb（amd64、8 GiB）と bios-ide（pcat）で login prompt。
2. `plan/tools/ports/` の sun4u の既定の手順で起動。

## 片付け（Q1 に依頼）

/home/awe/zedBSD-worktrees/p1/build/ws274-p008/（after-*、base-src、各 log と nm）。
