<!-- awesome-plan project=zedbsd record=ws048p003 -->

# ws048-p003: firmware の mailbox と VL805 の firmware の通知

Phase ID: `ws048-p003`
Parent: [WS048](../ws.md)
Status: planned
Queue: —
HAL の承認: 不要（driver が自分の mailbox の client を持つ。design.md §7）
依存: p002

## 範囲

- `include/kern/dcache.h`・`src/kern/dcache.c`: 既存の `hal_dcache_clean_range`・`hal_dcache_invalidate_range` の kernel の wrapper（arm64 だけで build）。
- `src/drivers/platform/rpi4/rpi4-firmware.c`・`.h`: property の mailbox の client（FDT の mailbox の node、1 GiB 未満の buffer、
  VideoCore の bus 番地、FULL・EMPTY の待ち、上限、答えの判定）と「xHCI の reset の通知」（tag `0x00030058`）。
- `src/kern/platform/rpi4.c`: 列挙の前に bus 1 の 1106:3483 へ通知する。失敗は記録して続ける。

## 受け入れ条件

1. host 試験: message の組み立て・bus 番地・register の手順・答えの判定（成功・tag の不明・timeout）が model で通る。
2. rpi4 の `make -j16` が warning 0、`BOOT_MODE=raspi4b plan/tools/boot-test.sh` で login prompt。
3. 新しい file の `style-check.py` が 0 件。
4. 実機: 通知が成功する（`dmesg`）。**ユーザーの確認。行うまで未実施**。
