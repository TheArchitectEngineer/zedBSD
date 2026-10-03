<!-- awesome-plan project=zedbsd record=ws073-p040 -->

# ws073-p040: BUG-030（起動時の USB mass storage の読み取りの ETIMEDOUT）の再現と原因

Status: uncleared（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws073-bugs`、branch `wt/ws073`。main の wrap up で約 40 分で止めた。再現はした（TCG で 2 回に 1 回、KVM でも並列の負荷で 1 回）、原因は未特定、修正なし）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-030](../../bugs/BUG-030.md)
Queue: main の依頼（2026-09-29「WS073 の残りの間欠の bug の再現と修正。まず BUG-030」）。Queue の ID は main が記録する。調べる上限は 2 時間
Resume point: 下の「次にすること」

## 範囲

BUG-030 を再現する率を上げる試験を作り、gdbstub・monitor・QMP で原因を探し、直せたら直す。2 時間で再現しない・原因が絞れないときは、
調べた範囲と結果を ticket に書いて uncleared。

## 手がかり

- 観測は 2026-09-24 の 4 回で、どれも `plan/tools/boot-test.sh` の `BOOT_MODE=uefi-usb`（**TCG**、`-cpu max`、4 vCPU、KVM なし）。
  2026-09-27 の再現の試み（ws073-p017）は `guest.py`（**KVM**）で 60 回起動し 0 回。
- 症状は `BOT CSW error=42`（data の後の CSW の 13 byte の転送が 5 秒（`BOT_TIMEOUT_MS`）で終わらない）と `BOT data dir=in error=42`。
  command の予算は 15 秒で、transport error の後は `bot_reset` と 1 回の再試行。

## 試験

- [tests/usb-stress.sh](../tests/usb-stress.sh): usb-storage の disk から N 回起動し（`MODE=kvm|tcg`）、毎回 SSH で USB の disk を丸ごと
  `dd` で読み（多数の READ(10)）、`dmesg` の usb-storage の error の行を数える。console は読まない。

## 結果（2026-09-29、wrap up まで）

kernel は main（a85ea4cc を merge した tree）の `vmunix`、image は `plan/ws073/tests/kernel-image.sh` で `build/ws053-full-hal-guest/hdd-image.img` の複写に
差し替え（`build/ws073-p040/guest.img`）。`tests/usb-stress.sh` を 3 本並列に走らせた（KVM 2 本は各 4 回・丸読み 3 回の予定、TCG 1 本は 2 回・丸読み 1 回）。
wrap up で途中で止めたので、終わった起動だけを数える（host の load は 3 つの QEMU だけ）。

| run | 起動 | usb-storage の error の行（SSH の `dmesg`） |
| --- | --- | --- |
| TCG（`MODE=tcg`、4 vCPU、boot-test.sh と同じ形） | 2 | 起動 1: 起動時に `BOT CSW error=42 actual=0 status=0 tag=0 expected-tag=319` が 1 行。丸読み（2.2 GB）では増えず。起動 2: 0 |
| KVM 1 | 1 | 起動 1: 起動時に 2 行（grep の数だけ。行の中身は取る前に止めたので無い） |
| KVM 2 | 2 | 0・0（2 回目の丸読みは止めた時に中断） |

- **再現した**: TCG で 2 回の起動に 1 回、KVM でも 3 本並列の負荷で 3 回の起動に 1 回、起動の途中に `BOT CSW error=42`。前回（ws073-p017）の KVM の 60 回 0 と
  違うのは、QEMU を並列に 3 つ走らせたこと（host の負荷）と TCG。
- **起動の後の 2.2 GB の丸読み（数万の READ(10)）では 1 回も出ない**。error は起動の途中（多くの device の attach と、同じ xHCI の usb-net・keyboard が
  同時に動く時期）に限られる。
- 症状はどれも data の後の **CSW の 13 byte の読み取りが 5 秒で終わらない**（`actual=0`）形。data の段は終わっているので、disk の読みの遅さではなく、
  CSW の transfer の完了（xHCI の transfer event）が 5 秒の間に処理されなかったと見られる。xHCI の event を取りこぼすのか、QEMU の usb-storage が CSW を
  返さないのかは**未確認**。[BUG-036](../../bugs/BUG-036.md)（起動時の列挙の段の ETIMEDOUT、同じ時期、同じ xHCI）と同じ根の可能性がある（未証明）。

## 次にすること（Resume point）

1. 再現: `tests/usb-stress.sh` を TCG（`MODE=tcg`）で 2〜3 本並列に回す（起動時の error が 2 回に 1 回ほど出る）。`dmesg` の行の中身と前後（xHCI・列挙・
   usb-net の行）を保存するよう script を直す（今は数だけ）。
2. 取りこぼしか否かの切り分け: CSW の URB が時間切れになる時点で、xHCI の event ring に未処理の event があるかを見る。
   - 案 A: `drv_usb_urb_wait()` の時間切れ（`src/drivers/usb/usb.c`）で、cancel の前に HCD に「event ring を 1 回処理する」op（`pci-xhci.c` の
     `xhci_irq` の event の loop と同じ処理）を呼び、その後に完了していれば log に `urb completed by poll after timeout` と残す。出れば割込みの取りこぼし
     （IMAN・MSI・`command_polling` の間の IE=0 の扱い、ws035-p039 の IMAN の直しの残り）、出なければ QEMU 側。
   - 案 B: gdbstub で `drv_usb_urb_wait` の `return ETIMEDOUT` に breakpoint を置き、`c->events[c->event_dequeue]` の cycle bit と `USBSTS`・`IMAN`
     （runtime 0x20）・`command_polling` を読む。
3. 取りこぼしなら、URB の待ちの中で一定時間ごとに event ring を poll する安全網（i915 の p008 の 1 tick の安全網と同じ考え）か、IE の扱いの修正。

## 試験の道具と成果物

- `plan/ws073/tests/usb-stress.sh`（この Phase で足した）。並列の起動は `build/ws073-p040/stress-a.sh`（worktree の build、git の外）。
- 結果の file: worktree の `build/ws073-p040/{kvm1,kvm2,tcg3}.txt`。
- 未実施: 案 A・B、修正、boot test（kernel は変えていない）。
