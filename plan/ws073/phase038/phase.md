<!-- awesome-plan project=zedbsd record=ws073-p038 -->

# ws073-p038: TLS の thread pointer を processor から読む（`__tls_get_addr` の syscall を無くす、BUG-110）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-110](../../bugs/BUG-110.md)
Queue: main が WS035 のサブエージェント（worktree `ws035-keiland`、branch `wt/ws035`）に割り当て（2026-09-29、rtld・libc・kernel の thread の TLS の
変更の許可つき、HAL の API は変えない）。Queue の ID は main が記録する

## 範囲と受け入れ

`src/rtld/rtld.c` の `__tls_get_addr` を、amd64 は `%fs:0`、arm64 は `TPIDR_EL0` から thread pointer を読む形にする。先に rtld・libc の pthread・kernel の
thread の TLS の約束を確かめる。確認: TLS の試験の回帰、errno と libm の試験、pthread の複数 thread と signal の中の TLS、dynamic と static の program、
1 回の時間の前後、boot test、Venus の desktop。aarch64 は build だけ。

## 約束の確認（変更の前提）

| 事柄 | 事実 | 場所 |
| --- | --- | --- |
| thread pointer の register | amd64 は FS の base（MSR）、arm64 は `TPIDR_EL0`。`thread_self` の `SET_TLS` がその場で書き、task の switch で保存・復元する（`hal_task_get_tls` は走っている task では register を読む）。signal の frame の save・restore も FS base を保つ | `src/hal/amd64/task.c`（`hal_task_set_tls`・switch・registers の save/restore）、`src/hal/arm64/task.c` |
| 置く値 | 常に control block（`struct __rtld_tcb`、先頭は `struct kern_tls_prefix`）の address。先頭の `self` は自分の address | exec（`src/kern/elf.c`: `prefix.self = tp`）、rtld（`__rtld_thread_alloc`: `tcb->tls.self = tcb`）、static の libc（`static-tls.c`: 同じ） |
| いつ入るか | dynamic: exec が PT_TLS の program に入れ、rtld が entry の前に初期の block を `SET_TLS` で入れる（初期化の関数は entry の後）。新しい thread は `thread_create` の引数で block を持って始まる。static: exec が PT_TLS なら入れ、無ければ `__rtld_thread_attach`（main thread の最初の pthread の操作）が入れる。fork は register と memory を写す | `src/rtld/rtld.c` の `rtld_main` の終わり、`userland/base/libc/pthread.c` の `pthread_create`・`ensure_main` |
| `SET_TLS` の他の値 | 無い（0 を入れる所も無い） | grep |

したがって amd64 の `%fs:0` は `self` = FS base = `thread_self(GET_TLS)` と同じ値、arm64 の `TPIDR_EL0` はその値そのもの。違いは block の無い thread
だけ: amd64 では FS に base が無く `%fs:0` の読みが fault する（今までは rtld_fatal）。dynamic の program の code は block の無い thread では走らない。

## 実装（HAL・kernel は変えていない）

- `src/rtld/rtld.c`: `thread_pointer()`（新、static）: amd64 `movq %fs:0`、arm64 `mrs tpidr_el0`、他（i386・sparc64 等）は今までどおり `thread_self`。
  `__tls_get_addr`・`d_tlsdesc_resolve`（TLSDESC）・`__rtld_pthread_private`（libc の `self_tcb`、thread がある process の pthread の操作）で使う。
  thread の attach・free・dlerror の record 等の頻度の低い所は `thread_self` のまま（0 の判定が要る）。
- `userland/base/libc/static-tls.c`（static の program の同じ役）: main thread の attach の後に立つ `static_tls_everywhere` の後だけ register を読み、
  その前（exec が block を入れない static の program で FS に base が無いことがある）は `thread_self`。`__rtld_pthread_private` で使う。
  static の libc の errno・fenv は thread が作られた後は `self_tcb` を通るので、これで速くなる。

## 検証

**QEMU（amd64、Venus の guest、worktree の graphical の login の image `build/p038-final.img`、前は `build/p037-final.img`）**:

- 新しい試験 `plan/ws073/tests/p038/run-tls-check.sh`（`tls-check.c`: 8 thread が 20000 回ずつ自分の errno と浮動小数点の例外を書いて読み戻し、
  errno の address が thread ごとに違う。各 thread が自分に SIGUSR1 を送り、handler が割り込まれた thread の errno の address と値を見て、handler の
  変更が残らない。dynamic（libc.so）と static（sysroot の libc.a）の両方。加えて rtld の試験 `dyntest`（`tlstest.so` の TLSDESC、pthread の TLS、
  dlopen した module の TLS、thread の安全性））:

| | 前 | 後 |
| --- | --- | --- |
| dynamic: errno 1 回 | 199〜204 ns | **7.5 ns** |
| dynamic: `feraiseexcept` 1 回 | 201.5〜215 ns | **7.5 ns** |
| static（thread の後）: errno・`feraiseexcept` | 237・228 ns | **5.0・5.5 ns** |
| TLS-CHECK（dynamic・static） | PASS | PASS |
| dyntest | DL:01〜DL:06 全て | DL:01〜DL:06 全て |

- WS076 の libm の試験（guest、image の libc.so、2000 件）: PASS。guest の 1 回: `sqrtf` 220.5 → **16.5 ns**（BUG-109 と合わせ 347.5 → 16.5）、`sin` 505 ns（TLS 以外が主）。
- Venus の desktop（1920x1280）: `ZWL STARTUP` の `glyphs` 423 → 286 ms（icons 70 → 14、Kei の印 104 → 44）、compose の計 1663 → 1615 ms。
  wallpaper は 670 → 783 ms（1 回の計測。TLS を使わない経路で、揺れと見て未調査）。desktop の画面の検査 PASS（`build/ws035-shots/p038-20260929-desktop.png`）。
- desktop の login・Log Out（`zdesktop-p126.sh` 2 周）: 黒 0・文字 console 0 で PASS。dmesg に `killed by signal` 0。
- boot test（`plan/tools/boot-test.sh build/p038-final.img`）PASS。

**aarch64（build だけ）**: `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/arm64 build/arm64/dynamic/libc.so build/arm64/dynamic/ld.so` 成功、warning 0。
ld.so の `__tls_get_addr` に `mrs TPIDR_EL0`、sysroot の libc.a（static-tls）にも。試験は未実施。i386 は cross-compile で `thread_self` のままを確かめた。

規約: `plan/tools/style-check.py`: `rtld.c` は変更前の 244 件 → 241 件（消えた syscall の段落の分、新しい指摘 0）、`static-tls.c` は 8 → 7 件、
`tls-check.c` 0。build の log に `Permission denied` なし（共有の toolchain に触れていない。worktree の sysroot と userland は再 build）。

## 制限と残り

- amd64 で block の無い thread が `__tls_get_addr` を呼ぶと、今までの `rtld_fatal("thread has no TLS control block")` の代わりに SIGSEGV になる
  （どちらも致命。dynamic の program では起きない）。
- i386・sparc64・m68k は `thread_self` のまま（各 HAL の register の約束を確かめていない）。
- libm の `sin` 等の残りの時間は double-double の計算そのもの（WS076 の方式）。

## Resume point

2026-09-29: cleared。
