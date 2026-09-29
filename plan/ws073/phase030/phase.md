<!-- awesome-plan project=zedbsd record=ws073-p030 -->

# ws073-p030: sshd の SIGSEGV（BUG-051）: signal の frame が amd64 の red zone を壊す

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-051](../../bugs/BUG-051.md)
Queue: main が WS073 のサブエージェント（`wt/ws073`）に割り当て（2026-09-29）。Queue の ID は main が記録する

## 範囲と受け入れ

- 起動の直後の少ない SSH の session で出る見込み（2026-09-28 の観測、約 8 回目）に沿って、起動と数十の SSH の session を繰り返して再現を試みる。
- 再現したら gdbstub で落ちる瞬間（`src/kern/signal.c` の fault の signal の既定の動作の log の行）に止め、落ちた process の
  address space から library と関数を特定し、原因を切り分ける（fault-around・TLB か、sshd・libc の NULL か）。
- 受け入れ: 原因を特定して修正し、同じ再現の手順で出ないことを確かめる。再現しなければ、試した回数・条件を記録して uncleared（tracking のまま）。
- 調査の上限: 起動と session の繰り返しは合計でおよそ 1 時間の QEMU の時間まで。

## 手順と結果

### 再現の試み（2026-09-29）

- `sh plan/ws073/tests/bug051-repro.sh build/amd64/hdd-image.img 5 30`（worktree の image、main 1b5ce0f8 相当、KVM・4 CPU・8 GiB・NVMe）:
  起動 5 回 × SSH の session 30 回 = 150 session、失敗 0、dmesg の fault の signal 0。
- full guest（`/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img` の userland ＋ この tree の kernel、`tests/kernel-image.sh`、
  2026-09-28 の 2 回目の観測と同じ userland）で、4 本並列の SSH の bulk の転送（各 256 MB、`dd if=/dev/zero` を SSH で読む、chacha20-poly1305）: 落ち 0。

### 落ちた場所の特定（再現なしで、観測の address から）

- probe [tests/libmap.c](../tests/libmap.c)（sshd-auth・sshd-session と同じ NEEDED の順 libutil・libcrypto・libc、`dl_iterate_phdr`）を full guest で実行:
  `/lib/ld.so` 0x100000000、`/lib/libutil.so` 0x10003f000、`/lib/libc.so` 0x100042000、`/usr/lib/libcrypto.so` 0x1000b5000〜0x100564830。
- 2 回目の観測 `0x10030f518` は libcrypto の offset 0x25a518、1 回目 `0x10030f437` は 0x25a437（1 回目の image は 09-25 で layout が同じと仮定）。
  guest の libcrypto（`build/ws073-p030/full-libcrypto.so`）で両方とも **`ChaCha20_ctr32`**（C 実装、OpenSSL の no-asm の build）の中:
  - 0x25a437: `movups (%rsi), %xmm0`（64 byte の block の入力の読み。`%rsi` は 10 命令前に stack の `-0xd0(%rbp)` から load した `inp`）。
  - 0x25a518: `xorb (%rsi,%rcx), %al`（端数の byte の loop。`%rsi` は loop の頭で同じ slot から）。
  - どちらも address 0 の読み = `inp` が NULL（かつ `%rcx` = 0）。SSH の chacha20-poly1305 の暗号・復号の本体。
- 解釈の候補（未確定）: (a) 呼び出し側が `inp` = NULL・長さ > 0 で呼んだ（OpenSSH・OpenSSL 側）、(b) kernel が user の register（`%rsi`）を
  割り込み・切り替えの戻りで 0 にした、(c) stack の page（`-0xd0(%rbp)` の slot）が 0 の page に置き換わった（fault-around・COW・TLB）。
  2 つの異なる build の userland・kernel で同じ関数の同じ変数だけが NULL になっていることから、(b)(c) の kernel の側の疑いが残る。

### 再開（2026-09-29 の 2 回目）

- (b) の probe [tests/regcheck.S](../tests/regcheck.S)・[tests/regcheck.c](../tests/regcheck.c): 全 GPR（%rsp 以外）・xmm0〜15・stack の 8 slot に既知の値を置いて回し毎回検査。
  同時に SIGUSR1（handler が xmm を全て壊す）を 0.2 ms ごと、fork と exit、4 MB の mmap・touch・munmap。self test（`-DREGCHECK_SELFTEST` で %rsi を 0 に）は slot 5 で FAIL を出す。
  guest（4 CPU）で 300 s・8 checker: 約 14 万 pass・85 万 signal で **変化 0**（`REGCHECK:PASS`）。単独の process の register・stack は保たれる。
- 別の発見（BUG-051 とは別の bug、未起票・未修正）: SSH の session を 6 本並列にすると sshd が `reexec socketpair: Too many open files in system`、
  sshd-session が `monitor_openfds: socketpair: ... in system` で失敗し、OpenSSH の PerSourcePenalties で host からの接続が一時止まる。原因は
  `include/kern/net/socket.h` の `SOCKET_MAX 32U`（system 全体の socket の数の上限、`src/kern/net/socket.c` の `socket_create` が ENFILE）。
  静かな guest で socket 9、session 1 つで約 3〜4 使う。[tests/resources.c](../tests/resources.c)（`KERN_SYSTEM_GET_RESOURCES`）で数えた。
- 短い session の大量の試行: 2 本並列 × 150（chacha20-poly1305）= 300 session、失敗 0・落ち 0。
- **再現**: regcheck の負荷（`/tmp/regcheck 240 6`、4 CPU を飽和）の中で 2 本並列の SSH の session（chacha20、`dd` の 256 KB を読む）を回すと、
  99 s の間に 2 つの process が落ちた:
  - `pid 12`（**sshd の listener**）`at 0x1002d287d, address 0xe0` → libcrypto + 0x21287d = `AES_encrypt`（`mov -0x60(%rbp),%r9; xor 0x20(%r9,%r10),%edi`、r9 = 0）。
    sshd が死に、以後の session は全て失敗（297 本）。
  - `pid 27804 at 0x100435ead, address 0` → libcrypto + 0x375ead = `sha512_block_data_order`（`mov -0xf8(%rbp),%rax; mov %r12,(%rax)`、rax = 0）。
  - この image の layout（2 つ目の guest で `libmap`、`build/ws073-p030/libmap-wt.txt`）: libcrypto 0x1000c0000。
- 4 回の落ちの共通の形: **直前に stack の slot（`%rbp` の負の offset）から load した pointer が 0**。ChaCha20（`-0xd0(%rbp)`）、AES（`-0x60(%rbp)`）、
  SHA-512（`-0xf8(%rbp)`）。どれも fork を繰り返す process（sshd の listener・privsep の子）で、register の probe（fork しない checker）では出ない。
  → (c) stack の page の中身が 0 に見える（fork の COW・fault-around・TLB の失効）が最有力。

### 3 回目の観測（main から、ws073-p033 = BUG-106 の調査）

WS081 の pen の image で ssh を約 500 回流した間に `kern: pid 12 killed by signal 11 (vector 14) at 0x100300c64, address 0x38`（sshd の listener）。
image の layout が無く library は未特定。同じ種類（共有 library の中、NULL + 小さな offset の読み）。

### gdbstub で落ちる瞬間を捕まえた（[tests/bug051-gdb.py](../tests/bug051-gdb.py)、[tests/bug051-load.sh](../tests/bug051-load.sh)）

`exit1_signal`（rdi = 11）に hardware breakpoint、止めた CPU の TSS の rsp0 から user の割り込みの frame を読む。1 回目の捕獲（`build/ws073-p030/gdb1.log`）:

- `rip=0x100435ead`（`sha512_block_data_order`、`mov -0xf8(%rbp),%rax; mov %r12,(%rax)`）、`rbp=0x7fffffffe730`、**`rsp=0x7fffffffe650` = rbp - 0xe0**。
- 今の page table で読んだ stack: `rbp-0x140`〜`rbp-0xf8`（= rsp-0x60〜rsp-0x18）の 10 語が **全て 0**、`rbp-0xf0` 以上は正しい値。
- 読まれた slot（`rbp-0xf8` = rsp-0x18）は **%rsp より下**、System V amd64 の **red zone**（%rsp の下 128 byte、leaf の関数が %rsp を動かさずに使ってよい）の中。

### 原因

`src/kern/signal.c` の signal の配達は、handler の frame を **中断した %rsp の真下から** 積んでいた（`sp = interrupted_sp`）。amd64 の ABI では
%rsp の下 128 byte は中断された関数の生きた変数の置き場で、handler から戻ると frame の中身（多くが 0）で壊れている。zedBSD の userland は
`-mno-red-zone` で build するので出ず、red zone を使う外部の package（OpenSSL の libcrypto の leaf の関数 `ChaCha20_ctr32`・`AES_encrypt`・
`sha512_block_data_order`）が、SIGCHLD などの handler を持つ process（sshd の listener、privsep の子）で signal を受けた時だけ落ちた。
kernel の register・TLB・COW は無関係（register の probe で 0 件）。Linux も x86-64 で 128 byte を空ける。

### 修正

`src/kern/signal.c`: `SIGNAL_USER_RED_ZONE`（amd64 は 128、他の user ABI は 0）を定め、中断した stack の上に積む時は `interrupted_sp - SIGNAL_USER_RED_ZONE`
から始める（alternate stack に切り替える時はその top から、従来どおり）。HAL の API は変えていない（kern の側の user ABI の定数。HAL の arch の header の
定数にする案もあるが、それは HAL の API の変更で承認が要る）。

### 確認

- red zone の probe（`regcheck_red_zone`: leaf の asm が %rsp の下 128 byte に 16 語を置いて検査、handler 付きの SIGUSR1 を 0.2 ms ごと）:
  修正前の kernel で 4 checker 全て即座に `slot 100`（red zone）FAIL（`build/ws073-p030/redzone-before.txt`）、Linux の host では PASS。
  修正後の kernel で 120 s・6 checker `REGCHECK:PASS`（約 4.9 万 pass・36 万 signal、`redzone-after.txt`）。
- SSH の再現（`sh plan/ws073/tests/bug051-load.sh 200`: regcheck の負荷 ＋ 2 本並列の chacha20 の session）: 修正前は 99 s で 2 process が落ちた
  （別の 200 s の実行でも sha512 で 1 回、gdb で捕獲）。修正後 200 s で **1092 session・失敗 0・`killed by signal` 0**。
- build（`build-image-noclang.sh`）`check-amd64-native-image: OK`、signal.c の warning 0、style-diff findings 0。boot test PASS（`build/ws073-p030/boot-test/login.png`）。
- 未実施: 実機、i386・arm64 の image（修正は amd64 だけで値を変え、他は 0 で従来どおり）。

### 別の発見（範囲外、未修正、main に起票を依頼）

system 全体の socket の上限 `SOCKET_MAX 32U`（`include/kern/net/socket.h`）。SSH の session を 6 本並列にすると `socketpair: Too many open files in system`
（ENFILE）で sshd・sshd-session が失敗し、OpenSSH の PerSourcePenalties が host を一時締め出す。静かな guest で 9、session 1 つで約 3〜4 使う。

## Resume point

完了。BUG-106（ssh -tt の出力の欠け）が BUG-051 の現れなら、この修正の image で再確認する（ws073-p033 の担当）。次は BUG-039・BUG-031 の確認。
