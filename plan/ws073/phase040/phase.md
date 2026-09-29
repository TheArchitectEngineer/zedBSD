<!-- awesome-plan project=zedbsd record=ws073-p040 -->

# ws073-p040: BUG-030（起動時の USB mass storage の読み取りの ETIMEDOUT）の再現と原因

Status: in-progress（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws073-bugs`、branch `wt/ws073`）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-030](../../bugs/BUG-030.md)
Queue: main の依頼（2026-09-29「WS073 の残りの間欠の bug の再現と修正。まず BUG-030」）。Queue の ID は main が記録する。調べる上限は 2 時間
Resume point: 再現の試験を走らせている（下の「試験」）

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
