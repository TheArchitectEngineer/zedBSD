<!-- awesome-plan project=zedbsd record=ws308-p003 -->

# ws308-p003 syscall の I/O の道で kernel heap を確保しない

- 親: [WS308](../ws.md)。Queue: q1650（担当 P1）。前提: [p001](../phase001/phase.md)。
- Status: in-progress（2026-10-28 P1: 実装 01e701914、5330 の計測へ）
- Disposition: normal

## 事実（p001）

- QEMU の block の cached 4 KiB pread・32 thread で CPU の標本の 55% が kernel heap の大域の spin lock（src/kern/entry.c:416、割り込みを止め backoff なし、first-fit の free list の探し src/kern/heap.c:141）。syscall ごとに `uaccess_pin` の page の記録の `kern_calloc`（src/kern/uaccess.c:178）と、block device の 4 KiB の bounce の `kern_malloc`（src/kern/syscall.c `syscall_io_buffer`、`SYSCALL_IO_CHUNK` 512 を越える regular でない file は heap）。5330 の heap lock の待ちは取りの 1〜5%（p001）。

## 設計（P1）

- E1. `struct uaccess_pin`（include/kern/uaccess.h）に page の記録 `UACCESS_PIN_INLINE_PAGES`（2、page を跨ぐ 4 KiB の buffer）個分の内の保存（`uint64_t inline_pages[12]`、型を header に持ち込まないため語の配列、uaccess.c の `_Static_assert` で大きさと揃いを確かめる）。2 page 以下は内に、それ以上は今どおり `kern_calloc`。解放は内でない時だけ。pin は写さない（pages が自分を指す）。今の使い手に pin の写しが無い事を grep で確かめた（`struct uaccess_pin` を値で代入する所は無い。`sockaddr_output_pin` も pointer で渡す）。
- E2. `syscall_io_buffer` の shared の I/O pool（`io_pool_borrow`、lock の無い slot の取り合い、src/kern/io.c）を block device にも使う（`syscall_io_pooled`）。pool を使わない理由（他の process を待って長く持つ）は pipe・socket・端末の事で、block device の転送は disk を待つだけ（regular file と同じ）。pool が尽きた時は今どおり stack の 512 B に落ちる。
- 大きさ: `sys_vector_call` の pins[16] は 1 個 96 byte 増えて約 1.5 KiB 増える（`-Wframe-larger-than=8192` の build で確かめる）。

## 試験

- build（config-5330・pcat・visionfive2・rpi4・m68k-virt）warning 0、QEMU の boot-test・`qemu-nvme-shapes.sh`・`qemu-directio.sh`（readv/writev の 16 iovec 未満・揃わない buffer・page を跨ぐ）、5330 の区間の計測。
