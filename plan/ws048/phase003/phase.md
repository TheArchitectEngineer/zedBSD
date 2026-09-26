<!-- awesome-plan project=zedbsd record=ws048p003 -->

# ws048-p003: firmware の mailbox と VL805 の firmware の通知

Phase ID: `ws048-p003`
Parent: [WS048](../ws.md)
Status: cleared（2026-09-27。受け入れ 1〜3。実機（4）は未実施）
Queue: 2026-09-27 ユーザー指示のサブエージェントの実行（worktree の branch）
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

## 変更

| file | 内容 |
| --- | --- |
| `include/kern/dcache.h`・`src/kern/dcache.c`（新規） | `kern_dcache_clean_range`・`kern_dcache_invalidate_range`。既存の `hal_dcache_clean_range`・`hal_dcache_invalidate_range`（hal.h に既にある）を包むだけ。arm64 の build だけに入れる（amd64 の HAL には実装が無い） |
| `src/drivers/platform/rpi4/rpi4-firmware.c`・`.h`（新規） | FDT の `brcm,bcm2835-mbox` を map し、1 GiB 未満の 1 page を request buffer にする。property の request の組み立て（size・code・tag・value buffer・end tag、16 byte 単位）、clean → mailbox 1 の FULL の待ち → `bus 番地（物理 | 0xc0000000）| 8` の書き込み → mailbox 0 の EMPTY の待ちと自分の word の受け取り（他の word は捨てる）→ invalidate → 答えの判定（EIO・ENOTSUP・ETIMEDOUT）。同時の request は EBUSY。`drv_rpi4_firmware_notify_xhci_reset()`（tag `0x00030058`、値 `bus << 20 | device << 15 | function << 12`） |
| `src/drivers/platform/rpi4/rpi4-pcie.c` | start と publish の間に、1:00.0 が 1106:3483 なら mailbox を用意して通知し、1〜2 ms 待ち、BAR を置き直す。失敗は記録して続ける |
| `include/drivers/pci/pci-brcmstb.h`・`src/drivers/pci/pci-brcmstb.c` | `drv_pci_brcmstb_reassign()`: publish の前に BAR を window の先頭から置き直す（firmware の読み込みが VL805 を reset して BAR を失う場合に備える。同じ番地になる。publish の後は EBUSY） |
| `platform/arm64/vmunix.mk` | `rpi4-firmware.c`・`dcache.c` |

HAL の mailbox（`src/hal/arm64/bsp-rpi4/mailbox.c`）は変えていない。kernel へ移った後は driver だけが mailbox を使う（design.md §7）。

## 検証

| 検証 | 結果 |
| --- | --- |
| host 試験 `make -f plan/ws048/tests/host-test.mk run DTB=...`（ASan・UBSan） | FDT・brcmstb（reassign を追加）・firmware の 3 つが通過 |
| firmware の model で確かめたこと | 初期化の前は ENODEV、2 回目の初期化は何もしない。VL805 の通知の request（32 byte、code 0、tag `0x00030058`、value buffer 4 byte、値 `0x00100000`、end tag）と posted word（`0xc0123008`＝buffer の物理 `0x123000` | `0xc0000000` | channel 8）、post の前の clean と読む前の invalidate、別の番地の符号化、3 word の tag（48 byte）と答えの copy、firmware の拒否 → EIO、tag の不明 → ENOTSUP、他の channel の word を捨てる、答えが無い → 約 1 秒（10 万回の poll）で ETIMEDOUT、mailbox 1 が満ちたまま → ETIMEDOUT（書かない）、不正な引数 → EINVAL、割り込みの禁止の対が釣り合う |
| brcmstb の reassign | 失った BAR0 が `0xc0000000` に戻り、root port の window は同じ。publish の後は EBUSY、NULL は EINVAL |
| rpi4 の `make -j16 vmunix` | 成功、warning 0 |
| QEMU raspi4b の boot test | login prompt（[qemu-login.png](qemu-login.png)、SD の image は p002 と同じ main の 2026-09-25 の build）。QEMU は PCIe を disabled にするので、この Phase の経路は通らない |
| `style-check.py` | 新しい file（`rpi4-firmware.c`・`.h`、`dcache.c`・`.h`）0 件。`rpi4-pcie.c`・`pci-brcmstb.c`・`.h` 0 件 |
| 実機 | **未実施**（受け入れ 4。`dmesg` に `pcie: VL805 firmware loaded`） |

## 注意

- 通知の後の待ち（1〜2 ms）と「firmware の読み込みが VL805 の BAR を失わせる」かどうかは未確認。置き直しは同じ番地になるので害は無い。
- EEPROM を持つ古い基板や古い firmware では通知が失敗しうる（記録して続ける）。その場合 VL805 は EEPROM の firmware で動くはず（未確認）。
