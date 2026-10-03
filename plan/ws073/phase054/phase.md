<!-- awesome-plan project=zedbsd record=ws073-p054 -->
# ws073-p054: BUG-164 — crash の後、名前の無い inode（nlink 0）が allocated のまま残る

Status: cleared（q665、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-164](../../bugs/BUG-164.md)
Queue: q665（Q1 2026-10-04「host の UFS の reader で inode 300 と journal を読み…原因を特定。直せれば直し、自分の QEMU で確認（T に依頼しない形）。
調べは 2 時間を上限に」）

## 原因（code の読みと image）

- image（p053 の直す前の回の `disk.img`、crash → 再起動の mount（replay）→ check → umount の後）の inode 300: mode 100644、nlink 0、size 0、
  block 0、bitmap は使用中。clean byte は 1（後の umount が書いた）。つまり **crash の後の mount を通っても回収されなかった**。
- unlink は名前を消し nlink を 0 にする。inode の解放（`reclaim_unlinked_inode`: truncate、mode 0、`free_inode_number`）は最後の参照が
  離れた時で、batched journal（j3、v3）では別の transaction に入りうる（開いたままの file なら close まで、そうでなくても commit の境が間に
  入りうる）。その間の crash は nlink 0 の inode を残す。これ自体は FFS と同じで、mount の回収（orphan の scan）が受け持つ。
- mount の回収 `orphan_recover`（`src/drivers/fs/ufs.c`）は **tail journal（v2、`journal_enabled`）の volume だけ**で走り、batched journal（v3）
  と journal の無い volume では何もしなかった（`if (!ms->writable || !ms->journal_enabled) return 0;`）。`orphan_recover_one` も tail journal の
  staging の大きさを要求していた（v3 では `EOPNOTSUPP`）。**これが原因**。unlink と解放を同じ transaction にしても、開いたまま消した file
  （close の前の crash）は残るので、回収の側を直す。

## 直し（403d1e4）

- `orphan_recover`: tail journal の volume は従来どおり毎回。それ以外の書ける volume は、**前の session が clean に終わらなかったとき**
  （mount の時に読んだ superblock の clean byte が 0。mount で 0、umount で 1 を書く既存の `ufs_write_clean`）だけ scan する。
  clean に終わった volume には名前の無い inode は無いので、普段の mount の時間は変わらない。
- `orphan_recover_one`: tail journal の staging の大きさの確かめを tail journal の volume だけに。それ以外は `reclaim_unlinked_inode` の
  順序の経路（`retire_inode_group` は tail journal でなければ handled 0）で、truncate → inode を空に → `order_barrier` → `free_inode_number`。
  scan は mount の中、batched journal を開く前（`write_cached_start` の前）なので、書き込みは write-through で `order_barrier` が効く。
- HAL は変えていない。

## 確かめ

### build（P2）

- kernel（`make -s ZEDBSD_CONFIG=config/ci/config-<p>.mk BUILD=build/p2-<p> build/p2-<p>/vmunix`）: amd64・pcat・rpi4 とも exit 0・warning 0。
- style: `plan/tools/style-check.py` と P2 の補助の checker で ufs.c の新しい指摘 0（HEAD と比べて）。

### QEMU（P2 の QEMU 1 つ、KVM。T には依頼していない。実機は未実施）

道具（新）: [tests/p054-orphan.sh](../tests/p054-orphan.sh)（fresh な v3 の volume に 64 KiB の file と directory を作り、file を `sleep` が読み続ける
ように開いたまま `rm` と `sync`、gdbstub で machine を kill。再起動して mount（replay と回収）・umount、host の check-volume。名前のある file の
中身も確かめる）。直す前の kernel は p053 の前の 25b5533 の image（`build/p2-b163-base`、この部分は main の p053 の後と同じ）。

| kernel | cut の直後の volume | 再起動の mount・umount の後 | 結果 |
| --- | --- | --- | --- |
| 直す前 | orphan inode [6] | **orphan inode [6] のまま** | FAIL（再現、決定的） |
| 直した後（1 回目） | orphan inode [6] | UFS OK、`named` の中身あり | PASS |
| 直した後（2 回目） | （同じ） | UFS OK | PASS |

- BUG-164 の元の image（inode 300）の複写の clean byte を 0 に戻して（crash した session を表す）直した kernel で mount・umount: host の
  check-volume `UFS OK`（inode 300 が回収された）。
- 回帰（直した後の image）: `crash-test.sh … 9`: HOLDS 3182 = PREFIX 3182、`UFS OK`。`p051-window.sh`: PASS（names 2598）。
  `p053-fsync.sh`（sector 63）: 200 回中 0、torn 0、`UFS OK`。どの回も crash の後の root（v3、clean 0）の boot は scan を通って SSH まで来た。

## 未実施・残り

- 実機。T の試験（user の指示で除外）。pc98・intelmac の build。
- 大きい root（inode の多い volume）の crash の後の boot で scan に掛かる時間は測っていない（使用中の inode の block を読むだけ、clean な
  boot では走らない）。
- 使い捨て: `build/p2-b164`（元の image の複写を含む）、`build/p2-b135`、`build/p2-b163-base`。

## 判定（Q1、2026-10-04）

cleared。P2 の自分の QEMU: p054-orphan.sh は直す前に決定的に FAIL（orphan が remount の後も残る）、直した後 UFS OK ×2。元の BUG-164 の image（clean の byte を 0 に戻す）も直した kernel で inode 300 を回収して UFS OK。crash-test.sh・p051-window.sh・p053-fsync.sh の回帰 PASS。kernel は amd64・pcat・rpi4 で warning 0（main でも確認）。実機は未実施。
