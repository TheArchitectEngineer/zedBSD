<!-- awesome-plan project=zedbsd record=ws073p022 -->

# ws073-p022: block・ignore された同期の fault の signal で process が同じ命令の fault を繰り返す（BUG-086）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-086](../../bugs/BUG-086.md)（main の依頼、2026-09-28）

## 目的と受け入れ

block・ignore された SIGSEGV・SIGBUS・SIGILL・SIGFPE を fault が生んだとき、process はその signal で終わる（Linux と同じ）。
handler のある（block されていない）fault の signal は従来どおり handler に届く。修正前の kernel で再現し、修正後の kernel で通ること。

## 原因

`kernel_user_fault_handler()`（`src/kern/user-probe.c`）は fault の signal を `signal_send_process_info()` で process に送るだけだった。
ignore なら捨て、block なら pending のまま、どちらも trap は成功で同じ命令へ戻り、fault を永久に繰り返した。

## 修正

- `signal_send_fault_info()`（`src/kern/signal.c`、宣言 `include/kern/signal.h`）: fault の signal を fault した thread に送る。SIGSEGV・SIGBUS・
  SIGILL・SIGFPE が ignore か thread で block なら、process の lock の中で disposition を SIG_DFL に戻し（mask・flags・restorer も 0）、thread の
  mask から外してから pending にする（Linux の force_sig_info と同じ。block された handler も SIG_DFL）。fault の info は同じ signal の古い記録を置き換える。
  SIGTRAP などは従来の意味（ignore なら捨てる）で thread に送る（trap は命令の後へ戻るので繰り返さない）。
- `kernel_user_fault_handler()` はこれを呼ぶ。fault の signal は process 宛てから thread 宛てに変わった（POSIX・Linux の同期の signal）。
- HAL・hal.h は不変。

## 検証（QEMU、amd64 native の full guest（`tests/kernel-image.sh`）、NVMe、KVM。実機は未実施）

- build: `make -j32 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-guest vmunix`、warning 0、include check・vmunix check PASS。
- 規約: `tests/style-diff.py`（signal.c・user-probe.c・signal.h）の指摘は 1 件、critical section の unlock の前の空行（coding-style §5 の形で、
  検査器の単純な規則の誤検出）。新しい file `tests/fault-blocked.c` は `plan/tools/style-check.py` で 0。
- [tests/fault-blocked.c](../tests/fault-blocked.c)（guest の clang で compile、SSH）: 10 件（SIGSEGV の NULL 読み・SIGFPE の idiv・SIGILL の ud2 を
  block／ignore／handler あり block／handler あり）。子は 5 秒の alarm を持ち、繰り返す kernel では SIGALRM で死ぬ。
  - 修正前の kernel: 7 件 FAIL（block・ignore の全てが signal 14 = SIGALRM で死ぬ）、handler の 3 件は ok。再現。
  - 修正後の kernel: 10 件 ok、`FAULT:PASS`。handler は正しい si_signo・si_code（SEGV_MAPERR・FPE_INTDIV・ILL_ILLOPC）・si_addr 0 を受けた。
  - `kill -SEGV` の process 宛ての signal は従来どおり（status 139）。
- boot test: 修正後の kernel の full guest image で `plan/tools/boot-test.sh` PASS（`build/ws073-p022/boot-test/login.png`）。

## 残り・観察

- 修正後の guest で SSH の session が 1 回「closed by remote host」で切れ、dmesg に `pid 47 killed by signal 11 (vector 14) at 0x10030f518, address 0`
  （NULL の読み）。BUG-051（sshd-session の子の SIGSEGV）の再現の候補。この修正は handler・mask の無い NULL の読みの結果を変えない（修正前も SIGSEGV で死ぬ）。
- 他の arch（i386・arm64 など）の build・試験は未実施（amd64 だけの方針）。signal.c・user-probe.c は共通の code。
