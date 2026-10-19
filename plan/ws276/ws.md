<!-- awesome-plan project=zedbsd record=ws276 -->

# WS276 m68k（x68k・m68k-virt）の静的な program の TLS を kernel と libc で支える

- ID: ws276、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 18:26 Q1 作成、p001 を P2 が q1304）
- 起点: 2026-10-19 ユーザー「x68kにもNICは実在するらしいので、lp,lpr,ntpdateもビルドが通るようにしましょう。」の後の P2 の所見（q1301）: x68k の user の runtime に __m68k_read_tp と user.ld の PT_TLS を足して lp・lpr・ntpdate が link できるようになったが、kernel の elf.c の load_static_tls は amd64・i386 だけで、他の arch の静的な TLS の program は ENOEXEC で拒まれる。x68k で動かすには、elf.c が m68k の TLS の配置（TCB の後 0x7000 の thread pointer、m68k-read-tp.c と同じ）を作り、static-tls.c の __rtld_thread_alloc が m68k の配置を扱う必要がある。
- 担当: P2（q1304）。

## 完了の条件（案）

1. m68k の静的な program の TLS を kernel（elf.c の load_static_tls）と libc（static-tls.c）が m68k の ABI の配置で用意し、x68k・m68k-virt で lp などの TLS を使う program が ENOEXEC にならない。
2. 他の arch（amd64・i386）の静的な TLS の振る舞いは変えない。
3. 確かめ: build warning 0、host 試験（TLS の配置の計算）。QEMU の m68k-virt は userland を持たない段なので、kernel と libc の配置の host 試験で確かめる。x68k の MAME・実機は範囲外（ユーザーの指示まで）。
4. ZEDBSD_STATIC_TLS（sysroot.mk・sparcv9・x68k が渡すが誰も読まない）と KERN_STATIC_TLS（source が見るが誰も定義しない）の食い違いを調べて記録する（toolchain の sysroot.mk の変更は Q1）。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計と実装（elf.c・static-tls.c の m68k の配置、host 試験、名前の食い違いの調べ） | in-progress（q1304、P2。2026-10-19 18:50 実装 73fde3c30・host 試験 PASS・build warning 0、Q1 の判定待ち。T1 は m68k なし（host 試験だけ）、amd64・pcat の起動の回帰は任意） |
