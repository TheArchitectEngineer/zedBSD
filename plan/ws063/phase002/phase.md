<!-- awesome-plan project=zedbsd record=ws063p002 -->

# ws063-p002: 強制終了の試験、回帰、規約の適合（WS060 の変更を含む）

Phase ID: `ws063-p002`
Parent: [WS063](../ws.md)
Status: cleared
Queue: 2026-09-27 の並列実行（ストレージ担当のサブエージェント、ユーザー指示「7 並列で WS を完了まで」）

## 目的

root と作業 volume で、書き込みの途中に QEMU を強制終了して起動し直し、volume が UFS OK で sync した内容が残ることを複数の時点で確かめる。WS060・WS063 の変更の全体を規約（`plan/coding-style.md` の全文）で見直す。

## 受け入れ

WS063 の受け入れのうち、この Phase の分: 強制終了の後に UFS OK、journal の自動作成と `nojournal`、規約（新しいコードの指摘 0、既存 file は悪化させない）、build（warning 0）、boot test。

## 範囲（見直した source）

| file | 見直した部分 | 出典 |
| --- | --- | --- |
| `src/drivers/fs/ufs.c` | v3 journal の全体（`j3_*`、`order_barrier`、`write_cached_intended`）、J3 の macro と struct、書き込みの経路（`write_sectors_impl` の v3 分岐、`write_content_*`）、block の 0 埋め、lookup・readdir の名前の特別扱い、mount・sync・unmount の journal の出入り | WS060-p003、WS063-p001（と隣の ws061-p006 の write cached の分岐） |
| `src/kern/buf.c`・`include/kern/buf.h` | pin（`buf_write_pinned`・`buf_unpin`・`write_lines`）、flusher の hook、flusher と `flush_aged`、write cached の判定、file-scope 変数の comment | WS060-p003（flusher 本体は ws061-p006。同じ block なので一緒に直した） |
| `userland/base/mount/main.c` | `writethru`・`nojournal` の option と一覧の表示 | WS060-p003 |
| `userland/base/mkfs/main.c`・`ufs-format.c` | `--journal-size`、`ZJ3R` の記録 | WS063-p001 |
| `tools/build/zedimage-host.c` | `--journal-size`、`ZJ3R`、`ufs` の引数の解析 | WS063-p001 |
| `src/kern/syscall.c`・`include/kern/mount.h`・`include/uapi/mount.h` | `MNT_WRITETHRU`・`MNT_NOJOURNAL` の 4 行と定義。既存の書式のまま（変更なし） | WS060-p003 |

## 変更（意味を変えない書き直し）

- ufs.c の v3 journal を規約の形に書き直した: 条件の中の呼び出しを外に出し、複合条件を 1 つずつの判定に、`return error;` で終わる関数を失敗と成功の return に分け、段落ごとに comment、関数の最後を成功の return に。長い関数を意味の境界で分けた（`j3_commit_locked` → `j3_logged_ranges`・`j3_copy_range`・`j3_commit_payload`・`j3_commit_seal`・`j3_unpin_all`、`j3_load` → `j3_header_check`・`j3_extents_read`、`j3_replay` → `j3_descriptor_check`・`j3_payload_sum`・`j3_payload_home`・`j3_payload_apply`・`j3_replay_slot`、`j3_allocate` → `j3_file_open`・`j3_file_fill`、block の 0 埋め → `zero_new_block`、mount の write cached の開始 → `write_cached_start`）。全ての static 関数に前方宣言。J3 の macro を file 先頭の macro の節へ、`struct ufs_j3` に役割の comment と各 field の comment。plan の Phase 名の参照（「See ws060-p002」）を code から消した（Phase の記録は WS の完了で消えるため）。
- 割り当てを 1 つずつ確保・検査（`j3_open` の 4 つ）。`long long` の定数（`8ULL << 20` 等）を MiB の比較に。
- buf.c: flusher と pin の関数を規約の形に（公開関数を static の前へ、`oldest_aged`・`writes_delayed` を分けた）、file-scope 変数ごとの comment、macro を file 先頭へ。隣接の既存の指摘も少し直した（`buf_init` の `mutex_init` の条件、複数行 comment の形、`cancel_reservation`・`commit_reservation` の条件演算子）。hash の comment の誤り（2^13 → 2^16、1/16 → 1/8）を直した。
- mount(8): option と flag の表（`mount_flag_options`）と `option_flag()`・`print_mount_options()`。表示の文字列は前と同じ（`(rw,nosuid,writethru,nojournal,bind)` の順）。
- mkfs: `parse_journal_mib` を `main` の後へ（前方宣言）、`main` の comment が別の関数の上に離れていたのを直した、`ZJ3R` の offset を macro に。
- zedimage-host: `ufs` の引数の解析を `ufs_command()` に分けた（`--journal-size` の上限の検査を int64 への変換の前に）。native の layout の関数（WS062）も同じ規約で直した（ws062-p004 に記録）。

## 規約の検査（`python3 plan/tools/style-check.py FILE --summary`）

| file | WS060 の前（cc4433d4） | この Phase の前 | 後 |
| --- | --- | --- | --- |
| `src/drivers/fs/ufs.c` | 621 | 639 | 579 |
| `src/kern/buf.c` | 140 | 150 | 137 |
| `userland/base/mount/main.c` | 57 | 63 | 50 |
| `userland/base/mkfs/main.c` | 6 | 8 | 6 |
| `userland/base/mkfs/ufs-format.c` | 26 | 27 | 26 |
| `tools/build/zedimage-host.c` | 73 | 79 | 70 |

書き直した範囲に残る指摘は、規約 §5 の critical section の形（lock の後と unlock の前の空行）を `paragraph-comment` と数える道具の誤検出だけ（ufs.c 9 件、buf.c 10 件）。規約の本文が優先する。`git diff --check` は差分なし。規約の全文（§14 の checklist）で読んで確かめた。

## 確認

| 確認 | 結果 |
| --- | --- |
| build（amd64、`plan/ws063/tests/config-amd64-ssh.mk`） | warning 0。image の検査器 OK |
| build（i386 pcat・arm64 rpi4 の kernel） | rpi4 の vmunix は warning 0。pcat は `ufs.o`・`buf.o` が warning 0。pcat の vmunix の link までは `src/kern/sched.c:2012` の既存の `-Watomic-alignment` の error で止まる（この Phase と無関係、HEAD でも同じ） |
| boot test（native、NVMe） | PASS（`build/ws063/boot-new/login.png`） |
| 強制終了（`plan/tools/ufs/crash-test.sh`、v3: journal の無い volume を作り mount で作成） | 3・5・8・13 秒: 名前 1679・2801・3182・3182、途切れない prefix が同じ、replay と unmount の後 UFS OK |
| 強制終了（`PROFILE=journal-snapshot`、v2 の tail の journal） | 3・7 秒: 16・70（v2 は操作ごとに flush で遅い、BUG-040 のとおり）、UFS OK |
| root の強制終了（`plan/ws063/tests/root-crash.sh`、4 秒） | sync した `/root/keep.txt` が残る、replay の後 churn 51、sync の後の root の partition が UFS OK |
| journal の機能（`plan/ws063/tests/journal-func.sh`） | root は `(rw)`、`.ufs-journal` は `ls -a` に出ず touch・rm・cat・mkdir・ln -s・mv が拒否。journal の無い volume A は最初の mount で `ZJ3L`（locator）が書かれ記録は 16 MiB、`-o nojournal` は `(rw,nojournal)`、`-o writethru` は `(rw,writethru)`、`--journal-size=0` の volume B は locator なし。A の上で `dir-grow.sh`（300/600/100/300）が LIMIT 4031・VERIFY-OK。A・B とも UFS OK |
| zedimage-host の出力の同一性（`plan/ws063/tests/zedimage-compare.sh`、変更前の binary と比べる） | 7 通り（既定、`--journal-size=0/32`、`--profile=journal-snapshot`、`--inodes=4096`、`4096M`、`64M --journal-size=1024`）が byte 単位で同じ、UFS OK。`--journal-size=1025`・`x`・未知の option は拒否 |
| mkfs（guest、`--journal-size=8`） | `ZJ3R` の記録 8 MiB、UFS OK を確かめた。ただし mkfs の終了は EBUSY（下の「見つけた不具合」） |

未実施: make・sh の差分試験、`smp-resource-stress`、COW、itimer、swaphog、512 MiB の guest。この Phase の code の変更は意味を変えない書き直しなので、AGENTS.md の規則（refactor は build と boot test）に従い、journal に直接かかわる強制終了・機能の試験を加えた。configure の計測はしていない（他の作業で機械の負荷が高く、数字を受け入れに使わない指示）。実機は未実施。

## 見つけた不具合（この Phase の前から、base の image でも同じ）

1. **write cached の volume の上の regular file への `mkfs -t ufs` が EBUSY で終わる**。format は書かれる（host の検査で UFS OK、`ZJ3R` も正しい）が、mkfs は `Device or resource busy` を返し、その後その volume の `umount` が EBUSY になる。`-o writethru` の mount では成功する（`ufs initialized`）。`KERN_FILE_FORMAT_RESERVE` の lease と write cached（ws061-p006）の組み合わせと推定。穴のある file（`truncate` だけ）では EIO、tmpfs では EOPNOTSUPP（これは設計どおり）。
2. `truncate -s N FILE` は無い file を作らない（GNU・BSD の既定と違う）。
3. `mount -o rw` は command line では `unsupported option`（fstab の option としては受け付ける）。

## 結果（2026-09-27、cleared）

受け入れを満たした。WS060・WS063 の新しい code を規約の全文で見直し、各 file の指摘を WS060 の前より減らした。
