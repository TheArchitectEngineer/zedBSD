<!-- awesome-plan project=zedbsd record=ws073p013 -->

# ws073-p013: BUG-029（BUG-052 の node の部分）— 多くの tmpfs の file が system 全体の inode を尽くさない

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-029](../../bugs/BUG-029.md)、[BUG-052](../../bugs/BUG-052.md)（node の上限の部分）

## 目的と受け入れ

BUG-029 の受け入れの案: tmpfs に 10000 個の file を作って消せ、その間も他の file を開ける。file を消した後に容量が戻る。tmpfs が満ちても
`/dev/null`・pipe・root の file system は使える。

## 再現（修正前、QEMU。p011 の kernel）

[tests/tmpfs-many.sh](../tests/tmpfs-many.sh) を `COUNT=1000` で: 3 回目の round までに `cannot create /dev/null: No space left on device`、
`/tmp` の mkdir も rm も失敗し、16 件 FAIL（ticket の観測と同じ。file を消しても戻らない）。

## 原因

- inode の記憶は、`alloc_inode` を持たない file system（tmpfs・devfs）と mount の無い inode（pipe・socket）では静的な pool（`INODE_COMMON_MAX` 512、
  `src/kern/inode.c`）だけから出ていた。tmpfs の file は存在する間ずっと inode を持つので、約 490 の file で pool が尽き、devfs の node（`/dev/null`）も
  pipe も作れなくなった。
- inode の cache（`INODE_CACHE_MAX` 2048 の slot）も tmpfs の file が占め続け、tmpfs 全体の上限は mount ごとの node の quota（1024）だけで、cache に
  他の file system の余地を残す仕組みが無かった。

## 修正

- `inode.c`: 記憶の出所を `i_storage`（`include/kern/inode.h`、`INODE_STORAGE_FILESYSTEM`・`POOL`・`HEAP`）に記録する。`inode_storage_take()` は
  file system の `alloc_inode`、pool、pool が尽きたら heap（`kern_calloc`）の順。`destroy_inode()` は出所へ返す。早い起動（allocator の前）は従来どおり pool。
- cache の slot を 64 bit では 16384、i386 では 4096 に（`INODE_CACHE_MAX`）。slot ごとの mount を並べた `inode_cache_mount[]` を持ち、`inode_get()` は
  mount の一致する slot の inode だけを見る（tmpfs の inode が cache を埋めても、他の file system の lookup は各 inode に触れない）。`inode_cache_capacity()` を公開。
- `tmpfs.c`: 全ての tmpfs の mount が合わせて持てる node を cache の 7/8 に（`tmpfs_nodes_in_use`・`charge_shared_node()`。root も数える）。残りの 1/8 は
  他の file system・device・pipe のため。mount ごとの node の quota の既定をその共有の上限に（下限 1024）。byte の quota（32 MiB）は変えていない（BUG-052 の残り）。
- HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: amd64 guest の vmunix warning 0。WS001 の inode の host 試験（`plan/ws001/tests/credential-creation-request-host-test.mk`）PASS（50 checks）。
- `tests/tmpfs-many.sh`（既定 `COUNT=10000`）: 3 round とも 10000 個の空の file を作り、その間 `/dev/null`・`/etc/passwd`・pipe が使え、`ls` が全てを数え、
  消せた。1 byte の file は 8191 個で byte の quota（df 100%）が止め、その時も他の file は開け、消した後に新しい file を作れた。17 件全て PASS（修正前 16 件 FAIL）。
- tmpfs の共有の上限まで（14320 個の file）: tmpfs の作成は ENOSPC、`/dev/null`・pipe・`/etc`・root（UFS）への作成は通り、消した後に再び作れた。
- boot test（warning 0）: lean amd64（`build/ws073-img/boot-test-p013/login.png`）・pcat（BIOS/IDE、64 MiB、i386 の cache 4096、
  `build/ws073-pcat/boot-test-p013/login.png`）・rpi4（raspi4b、`build/ws073-rpi4/boot-test-p013/login.png`）全て PASS。
- 規約: `tests/style-diff.py`（inode.c・inode.h・tmpfs.c）0。

## 残り

- BUG-052 の byte の容量（mount ごと 32 MiB）: tmpfs の page は `kern_calloc` の 4112 byte で、固定の heap（4 MiB）を越えると 2 page の large allocation に
  なり、`kern_free` は large allocation の list を線形にたどる（IRQ を止めて）。容量を物理 memory の半分に上げると削除が 2 乗で遅くなるので、tmpfs の page を
  VM の page（swap で裏打ち）にする設計が先に要る。BUG-052 は tracking のまま、この分析を記録した。
