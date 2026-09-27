<!-- awesome-plan project=zedbsd record=ws073p018 -->

# ws073-p018: 共有の file の mapping への書き込みが unmap・exit で失われる（BUG-026 の原因）

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-026](../../bugs/BUG-026.md)

## 原因の特定

- guest の lldb で `ld.lld --no-mmap-output-file` の libc の呼び出しを追った: `--no-mmap-output-file` でも LLVM の `OnDiskBuffer` を使う —
  一時 file を作り `ftruncate` で大きさを決め、`mmap(MAP_SHARED, RW)` し、利用者の store で像を書き、`munmap`・`rename` で出力にする。
- 最小の再現（[tests/map-write.c](../tests/map-write.c)）: 同じ手順で、mapping への書き込みを利用者の mode の `memcpy` で行うと、tmpfs・UFS の
  両方で出力が `Blocks: 0`（`sync` の後も）、exec が ENOEXEC。read と mmap の見る中身は正しい。`msync(MS_SYNC)` を挟めば正しい。
  kernel の `read()` で mapping に直接書く（kernel の mode の fault）と再現しない。
- kernel の原因（`src/kern/vmspace.c`）: 共有の file の mapping への store は hardware の dirty bit だけを立て、object page は sync が mapping を
  revoke するときに dirty を知る。ところが `munmap`（`detach_vm_page_for_unmap()`）と exit の space の破棄（`detach_vm_page()`）は mapping を
  dirty bit を見ずに外していた。object page は dirty にならず、書き戻されず、exec（filesystem から読む）は 0 を見る。再起動で中身は失われる。

## 修正

`object_mapping_note_dirty(page)`: 共有（`VM_REGION_SHARED`）で書ける（region の prot に `HAL_SPACE_WRITE`、COW でない）mapping の
object page を、外す前に `vm_object_mark_dirty()` で dirty にする（dirty bit を見ずに控えめに: 読んだだけの page も 1 度余分に書き戻す）。
`detach_vm_page_for_unmap()`（hardware の mapping を外す前）、`free_vm_page()`、`detach_vm_page()`（exit の破棄、hardware の mapping は残る）
から呼ぶ。HAL は触れていない。

## 検証（QEMU、KVM、NVMe、amd64。実機は未実施）

- build: guest の vmunix（`-Werror`）、lean native の disk-image。規約: `tests/style-diff.py`（vmspace.c）0。
- `map-write` の 4 つの mode（何もせず exit、`munmap`、`msync`、lld の 1 byte の probe file の後）を tmpfs（`/tmp`）と UFS（`/root`）で:
  8 つ全て exec できる（終了 7）、`sync` の後 `Blocks` が 24（tmpfs）・32（UFS）。修正の前は `msync` 以外の 6 つが ENOEXEC・`Blocks: 0`。
- 実際の `clang /tmp/h.c -o OUT -Wl,--no-mmap-output-file`: tmpfs・UFS とも exec でき（7）、`Blocks` 24・32。
- 回帰: `plan/tools/process/guest-vfork.sh`（fork の COW、vfork、posix_spawn、並行の fork）ALL OK。boot test（lean native、UEFI・NVMe）PASS
  （`build/ws073-p018/boot-native/login.png`）。
- 未実施: 修正の後に clang の package の workaround（`--mmap-output-file`、`0003-write-the-linked-image-through-the-output-file.patch`）を外す
  判断（package の変更。main へ）。電源断の直後の耐久（書き戻しの前に落ちれば通常の遅延書き込みと同じく失われうる）。
