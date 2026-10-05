<!-- awesome-plan project=zedbsd record=ws174-p002 -->
# ws174-p002: 起動の key による parameter record の書き換えの module と host 試験

Parent: [WS174](../ws.md)
Status: cleared（2026-10-06 P1。受け入れの検証を下に記録。Q1 の確認待ち）
Disposition: normal
Queue: Q1 の dispatch（2026-10-05 夜、P1 へ p002・p003・p005）。承認: ユーザー「OKです。ブートローダの仕様変更を実装してください。BIOSは後日でよいです。」

## 範囲

設計 [design.md](../phase001/design.md)（第 3 版）§2・§7.1・§7.4・§9.1 の O1〜O11 と §9.2 の image の config。Q1 の割り当てで、`boot-keys.c/.h`（UEFI の key の検出）と K1・K2 は p003 に移した。

- `bootloader/common/boot-override.h`・`boot-override.c`（新）: `zbl_boot_override_apply(record, keys)`。Ctrl（`ZBL_BOOT_OVERRIDE_KMSG`）は `kmsg`・`logo` の name の token を落とし `kmsg=console` を足す。Shift（`ZBL_BOOT_OVERRIDE_LOGIN`）は `login` を落とし `login=console` を足す。検査（長さ・終端）を先に、未知の bit は無視、上限 3071 に入らない token は足さない、新しい終端から元の長さまで 0 で埋める。
- `platform/amd64/vmunix.mk`: `$(BUILD)/uefi/common-boot-override.o` の rule（`common-logo-path.o` と同じ形）と `BOOTX64.EFI` の link の列。cfg の生成・BIOS の helper は触らない。
- `plan/ws174/tests/`: `boot-override-host-test.c`・`run-boot-override-host-test.sh`（O1〜O10、通常と ASan/UBSan の 2 回）、`boot-override-kernel-host-test.c`・`run-boot-override-kernel-host-test.sh`（O11: 3 つの cfg を `zbl_uefi_kern_config_parse()` → `zbl_boot_override_apply()`（keys 0〜3）→ kernel の `kern_boot_parameters_parse()`、ASan/UBSan）、`config-amd64-keys.mk`（`config-amd64-ssh.mk` + graphical boot + `kmsg=quiet`）。

## 検証（2026-10-06、P1 の worktree `/home/awe/zedBSD-worktrees/p1`）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 O1〜O10 | `timeout 120 plan/ws174/tests/run-boot-override-host-test.sh` | `43 checks, 0 failures` ×2（通常、ASan/UBSan） |
| host 試験 O11 | `timeout 300 plan/ws174/tests/run-boot-override-kernel-host-test.sh` | `72 checks, 0 failures`（clang、ASan/UBSan。3 cfg × 4 key の組で kernel の parser が 0、kmsg・login の値、unknown の数（logo・video）） |
| UEFI loader の build | `make -j16 ZEDBSD_CONFIG=plan/ws174/tests/config-amd64-keys.mk BUILD=build/ws174-keys build/ws174-keys/uefi/BOOTX64.EFI` | rc 0、warning 0、`BOOTX64.EFI check: PASS` |
| BIOS loader と config の形式が不変 | `git diff --stat` | `bootloader/pcat`・`pc98`・`bios`・`zedbsd-config.*`・cfg の生成の rule は変えていない |

- QEMU・実機: この Phase の範囲外（p003 で T1 に依頼）。
- 呼び手（`bootx64.c`）は p003 で足す。この時点の `BOOTX64.EFI` では関数は link されるが呼ばれない。
