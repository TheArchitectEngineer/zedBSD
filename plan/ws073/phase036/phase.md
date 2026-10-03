<!-- awesome-plan project=zedbsd record=ws073-p036 -->

# ws073-p036: BUG-039（UFS・overlay の host 試験）と BUG-031（起動の画面の行の混ざり）の確認

Status: cleared（2026-09-29）
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

### BUG-031（QEMU、2026-09-29）

試験 [tests/console-midboot.py](../tests/console-midboot.py)（新規）: guest（`plan/tools/guest/guest.py`、runtime `build/ws073-mix`、4 CPU・KVM）を USB から起動し、
起動の間の画面を QMP の screendump で撮り続け（約 0.1 s ごと、1 起動 60〜120 枚）、同じ frame を除いて並列に console の font で読み
（`plan/tools/boot-test.py` の reader）、kernel の行に見える全ての行を、login の後に SSH で取った `dmesg` の record（80 桁に切ったもの）と照らす。
どの record にも無い行は混ざった行。以前の `console-mix.sh` と違い、起動の途中でもう scroll して消える行（USB と storage の probe の所）も見る。
混ざった行を含む frame は PNG で残す。

- 検出力の確認: klog の console への写しの排他（a06266db の `mirror_enter`）を即 return にした一時の kernel（`build/ws073-p036/nofix/vmunix`、
  同じ image の ESP に差し替えた `build/ws073-p036/nofix.img`。source は build の後すぐ戻した）: **10 起動の 10 回とも混ざった行**
  （例 `usb0: device 1 interface 0 class 08/b0o6o/t5:0  idgrniovreerd= u1s bu-nsktnoorwa`、USB の probe と `boot:` の行が文字ごとに交互、
  `build/ws073-p036/midboot-nofix/boot1-frames/f0028.png`）。BUG-031 の観測と同じ形。
- 今の kernel（`build/amd64/hdd-image.img`、p035 の build、修正を含む）: 22 起動のうち SSH が上がった **21 起動で混ざった行 0**
  （各 35〜37 の kernel の行、`build/ws073-p036/midboot2.log`・`midboot3.log`）。1 起動は SSH が上がらず比べていない（34 行は見えていた。
  USB の列挙の失敗、BUG-036 と同じ見込み）。
- 結論: 受け入れ案「起動の途中の画面を撮る試験で 20 回混ざらない」を満たす（QEMU）。BUG-031 を resolved にする。実機は未実施。
- 最初の版（1 frame ずつ読む）は 1 起動 5〜7 frame しか撮れず、起動の途中を見逃していた（20 起動 PASS だが検出力が低い。記録だけ）。
  frame を先に全て撮り、後で読む形に直した。

## Resume point

完了。WS073 の次は main の指示を待つ（残り: BUG-036・BUG-030・041 の再現、BUG-039 は host 試験の土台の WS、BUG-093 は toolchain）。
