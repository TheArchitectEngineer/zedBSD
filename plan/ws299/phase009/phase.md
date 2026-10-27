<!-- awesome-plan project=zedbsd record=ws299-p009 -->

# ws299-p009 5330 の NVMe の root を p5 いっぱいに広げる（と、それで見えた大きな file の sync の直し）

- 親: [WS299](../ws.md)。Queue: q1617 / q1617-i01（担当 P1、2026-10-27）。
- 出典: 2026-10-27 10:28 ユーザー（クリック）「5330 の NVMe の root は、書く時に p5 いっぱいに広げる」。
- Status: in-progress（受け入れの確かめは全部済み（QEMU と 5330）。Q1 の判定待ち）
- Disposition: normal

## 受け入れ

- `5330-run.sh --nvme-image` で書いた root の UFS が p5（15104 MiB）いっぱいになり、zedBSD から 1 GiB を超えて書けて読み戻せる。
- 広げた volume が host の検査（`tools/build/check-ufs-image.py`）を通る。
- 広げた root で大きく書いた後も reboot が終わる（QEMU と 5330）。

## 設計

- 道具: `plan/tools/hw5330/ufs-grow.py`（5330 の Debian で root として、`5330-nvme-write.sh image` が dd の直後に呼ぶ。`5330-run.sh` が隣に写す）。
  - 群の形（fpg・ipg・各域の位置）は superblock のまま変えない。最後の群を群いっぱいに広げ（free の bit と数え直し）、後ろに空の群を足す（superblock の写し・header・0 の inode の表。`tools/build/zedimage-host.c` の群と同じ配置）。superblock の群の数・大きさ・data の大きさ・空きの合計を書き直し、全部の写しと最後に primary を書く。
  - 断る物: summary の域（cssize≠0）、clean でない volume、journal-snapshot の尾、1 KiB/8 KiB 以外の形、地図の位置が image の build と違う群。
  - 1 GiB の root は 65536 inode のために 256 群（1 群 4 MiB）なので、15104 MiB では 3776 群・966656 inode になる。
- 群の数が増えるので、`src/drivers/fs/ufs.c` の「fragment の群を全部の群から線形に探す」4 箇所（`valid_inode_fragment`・`free_block`・`xattr_release_location`・`release_group_locked`）を `cg_search_range`（fragment / fpg の 1 群だけ。cgoffset で群の頭をずらす古い volume は今までどおり全部）に。
- 見えた不具合と直し（下の「reboot が止まった」）: `src/kern/vm/object-sync.c` の `vm_object_sync_range_buffer` が、取る page を毎回 object の page の list の頭から探していた（resident の page の数の 2 乗）。page の index（AVL、offset 順）で最後に取った page の後から探すように。

## 実施（2026-10-27）

| 確かめ | command | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws299/tests/host/ufs-grow-test.sh`（`ZEDIMAGE_HOST=build/tmp/zedimage-host`） | `ufs-grow-test: PASS`: 64 MiB・256 群 → 384 MiB・1536 群、最後の群が半端（108 MiB・7 群）、最後の群が小さすぎて落とす（96 MiB・96 群）、二度目は「already fills」。広げた後に check-ufs-image の validate（block の所有・bitmap・群と全体の数・backup の superblock）と file の中身の一致 |
| build（5330） | `make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=build/p1-5330-grow vmunix` | rc 0、warning 0（ufs.c の変更の後、object-sync.c の直しの前） |
| 5330 に書いて起動 | `sh plan/tools/hw5330/5330-run.sh --nvme-image build/p1-5330-grow/hdd-image.img p1-ws299-grow-nvme` | `5330-run: PASS`（ssh まで 20 秒、i915 の survey PASS、steady registers 0 違い）。画面 P1 の worktree の `build/hw5330/logs/p1-ws299-grow-nvme.png` |
| 広げた root | `sh plan/ws299/tests/grow-check-5330.sh 3` | `df`: `/dev/nvme0n1p5 14922752 KiB`（前は 1 GiB）。3 GiB の 0 と 256 MiB の乱数を書いて `cksum` 483519800、raw 2 GiB を読んだ後も同じ値、df 25%。**最後の `reboot` で止まった**（下） |

### reboot が止まった（2026-10-27 10:55、5330）

- 5330: `sudo reboot` の後 ping は返り ssh は refused のまま 15 分以上。ユーザーが電源を入れ直した（Q1 経由「5330は画面が表示されない状態で止まっていたので再起動しました。」）。p4 の bootlog の `boot-000012.txt` は `=== bootlog: the system is stopping; 110.2 s after boot ===` まで（前の正常な reboot の `boot-000011.txt` も同じ行で終わる）。
- QEMU で再現（`sh plan/ws299/tests/qemu-grow-reboot.sh build/p1-qemu2/hdd-image.img 3`: 1 GiB・65536 inode の volume を 15104 MiB に広げて 2 本目の NVMe に、3 GiB と 256 MiB を書いて cksum、mount したまま reboot）: 300 秒で reset しない。gdbstub の `thread apply all bt`: CPU 0 が `vm_object_sync_range_buffer ← vm_object_sync_inode_buffer ← vm_object_sync_mount_buffer ← mount_sync_buffer ← mount_sync_all ← kernel_syscall_handler`（shutdown の sync）、他の CPU は idle。
- 原因: 上の「設計」の 2 乗の探し。3 GiB の file は約 79 万 page で、1 page ごとに list を頭から歩く。UFS の group の変更・広げた root そのものではない（広げた root で初めて 1 GiB を超える file が書けた）。
- main の kernel（bb02f7b51、UFS の変更も直しも無い、image `build/p1-qemu`）でも同じ試験が 300 秒で reset せず、CPU 2 が同じ `vm_object_sync_range_buffer ← … ← mount_sync_all` の中（gdbstub）。もとからある不具合で、UFS の変更の所為ではない。
- 直しの後（object-sync.c、image `build/p1-qemu2`、vmunix 11:19）: 同じ script が `qemu-grow-reboot: PASS reset after 3 s`、cksum 2891891104・2725605222、bufcache の失敗 0。

- 5330（直した kernel、`build/p1-5330-grow/vmunix` sha256 `250029d583326c62…`、build rc 0・warning 0、`5330-nvme-write.sh kernel` で p4 へ）: `sh plan/ws299/tests/grow-check-5330.sh 3` が `grow-check: done`（3 GiB と 256 MiB を書き直して cksum 40824270、raw 2 GiB の後も同じ、`reboot` で Debian に戻った）。電源の入れ直しの前に書いた file は残っていた（始めの df が 25%）。

## 未実施・残り

- `ufs.c` の群の探しの変更は、広げた volume の mount・書き・読み・reboot（QEMU と 5330）で通った。xattr・解放の道（`xattr_release_location`・`release_group_locked`）を直に狙う試験はしていない。
- `vm_object_sync_range_buffer` の直しは QEMU の大きな file の sync と 5330 の reboot で確かめた。msync・MS_INVALIDATE・resize の道を狙う試験はしていない（同じ探しの順を変えただけで、取る page の条件は変えていない）。
- 規約の全文の見直しは p008。
