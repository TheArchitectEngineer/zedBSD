<!-- awesome-plan project=zedbsd record=ws073p002 -->

# ws073-p002: BUG-065 — 同じ block device の 2 度目の mount を EBUSY に

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-065](../../bugs/BUG-065.md)（優先度 高: data を壊す）

## 目的と受け入れ

mount 済みの device（または blocks が重なる device: partition と disk 全体など）の 2 度目の mount を、どちらかが書き込み可能なら EBUSY にする。
2 つの read-only の mount は共有してよい（どちらも書かない）。unmount の後は再び mount できる。失敗した mount は何も残さない。

## 再現（修正前、QEMU）

main の測定用 guest image に修正前の vmunix: `mkdir /w; mount -t ufs /dev/nvme0n1p2 /w` が 0、`mount` に `/dev/nvme0n1p2 on / (rw)` と
`/dev/nvme0n1p2 on /w (rw)`。`mount -t auto /dev/nvme0n1p1 /e`（kernel が private に rw で持つ boot の FAT）も 0。

## 原因

`mount_filesystem_on_disk()`（`src/kern/mount.c`）は disk の backing claim（swap・loop・format）との衝突だけを調べ、他の mount が同じ disk を使って
いるかを調べていなかった。書き込みの mutation の予約は互いに衝突しない（並行の書き込みを許す仕組み）ので、2 つ目の mount も通った。

## 修正

`src/kern/mount.c`: `mount_disk_reserve()` が namespace の lock の中で全ての mount（PREPARING・LIVE・DYING、private を含む）の `m_disk` と
blocks の重なり（同じ leaf device の上の範囲、`disk_resolve_range`）を比べ、どちらかが書き込み可能なら EBUSY、そうでなければ同じ critical section で
`m_disk`・`m_flags` を記録する（同時の 2 つの mount の一方だけが取る）。その後の失敗の経路は `mount_disk_unreserve()` で disk を返す。
unmount は従来どおり `finalize_filesystem_destroy()` が `m_disk` を消す。FreeBSD（GEOM の exclusive open）も 2 度目を EBUSY にする
（FreeBSD は read-only 同士も拒む。ここでは許した）。HAL は触れていない。

## 検証（QEMU、KVM、8 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j48 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-a vmunix` warning 0。
- guest（`tests/kernel-image.sh`、2 台目の NVMe に host の `zedimage-host ufs 64M` の空の volume）: `tests/double-mount.sh` 17 件全て PASS
  （root の rw・ro の 2 度目が EBUSY、2 台目の volume の rw の後の rw・ro が EBUSY、ro 2 つは共有でき両方から読める、ro 2 つの上の rw は EBUSY、
  unmount 後の rw の再 mount で file が残る、拒んだ mount は何も残さない）。修正前の kernel では root の 2 度目が 0（再現）。
- 同時の 2 つの mount（`mount … /mnt-a & mount … /mnt-b & wait`）を 10 回: 毎回 1 つだけ成功。
- boot test: `plan/tools/boot-test.sh build/ws073-a/guest.img` PASS（login.png を確認）。
- 規約: `tests/style-diff.py src/kern/mount.c` 0。全文の規約で見直した。

## 判断が要る点（既定を選んで先へ進んだ。可逆）

- **ESP の公開の mount が EBUSY になった**。native の起動で kernel は boot の partition（`/dev/nvme0n1p1`）を private な FAT として rw で持ち続ける
  （`vfs: boot0 ... (private FAT)`）。修正前は同じ partition を `mount -t auto /dev/nvme0n1p1 /e` で 2 つ目の FAT の状態として mount でき、両方が書けば
  壊れうる。今は EBUSY。userland に ESP を mount する道具は見当たらない。running system から ESP を触る必要があるなら、2 度目の mount でなく
  kernel が持つ mount を公開（例 `/boot`）するのが筋。ユーザーの判断で決める。
- read-only 同士の共有を許した（FreeBSD は拒む）。拒むなら `mount_disk_reserve()` の 1 条件を消すだけ。
