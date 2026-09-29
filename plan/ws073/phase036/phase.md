<!-- awesome-plan project=zedbsd record=ws073-p036 -->

# ws073-p036: BUG-039（UFS・overlay の host 試験）と BUG-031（起動の画面の行の混ざり）の確認

Status: in-progress（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-039](../../bugs/BUG-039.md)、[BUG-031](../../bugs/BUG-031.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29、BUG-039・BUG-031 の確認）。Queue の ID は main が記録する

## 範囲と受け入れ

- BUG-039: 今の tree で `plan/ws001/tests/directory-fsync-host-test.mk` の 3 つの fixture の状態を確かめる。修正は host 試験の土台の WS の仕事
  （2026-09-27 の WS073 の判断。試験の file は WS001 の plan の中で、WS073 の修正の範囲の外）。
- BUG-031: 以前の受け入れ案「起動の途中の画面を撮る試験で 20 回混ざらない」を行う。試験が混ざりを検出できることも修正の無い kernel で確かめる。

## 手順と結果

### BUG-039（host、2026-09-29、main 601de88c を merge した tree）

`make -f plan/ws001/tests/directory-fsync-host-test.mk OUT=build/ws073-p036/b039 build/ws073-p036/b039/{vfs,overlay,ufs}-test`:
- vfs-test: build でき `ws001-p016 VFS dispatch: PASS (13 checks)`（2026-09-27 の修正のまま）。
- overlay-test: link で `undefined reference` 125 件（`overlayfs.c` の `.hightext` の section の関数が gc で落ちない。2026-09-27 と同じ）。
- ufs-test: `No rule to make target build/driver-fragments/src/drivers/fs/ufs/ufs-vfs.c`。`python3 plan/tools/driver-fragments/prepare.py --source src/drivers/fs/ufs.c`
  は `Missing or ambiguous production section: src/drivers/fs/ufs/ufs-allocation.inc`。試験は消えた `plan/ws025/temp/p031-driver-fragments/` の header も include する。
- 結論: 状態は 2026-09-27 から変わらない。tracking のまま、host 試験の土台の WS へ（修正には prepare.py の作り直しか断片に頼らない build、
  overlay の fixture の stub か host の build での section の属性の除去が要る）。

### BUG-031

（記入中）

## Resume point

BUG-031: `python3 plan/ws073/tests/console-midboot.py build/amd64/hdd-image.img 20 build/ws073-p036/midboot`（修正ありの kernel）、
続けて同じ試験を修正の無い kernel（`build/ws073-p036/nofix.img`、klog の `mirror_enter` を即 return にした vmunix を ESP に）で。
