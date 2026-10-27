<!-- awesome-plan project=zedbsd record=ws290-p002 -->

# ws290-p002 語の層

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p002（版 4.5、ghci.md cb937e6）。
- 状態: cleared（2026-10-28 P3 q1638-i01）
- 依存: p001

## 範囲

語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（§4.4 の全部）・`caps.c`（decode と CP_HEAP の勘定）・ring の計画と fence64・EXTCAP の走査（`check.c` に新しい名前で足す）。build に入る今の file は変えない（新しい file は build に入れない）。`ghci-ref-host-test.c`（§14、R23）

詳細は design の該当の節（§4〜§16）と §14 の p002 の場面。

## 受け入れ

- words と ref の試験 PASS、新しい file を amd64 と riscv64 の kernel の flag で compile して warning 0（R23）、`CONFIG_DRIVER_PCI_GHCI=y` の amd64 が今のまま build
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 14 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

- 20bf888（回答 第 14.0 版）で決まった: N70 の `CMD_DISP_SET` は 1 + 2N（`ghci_cmd_disp_set` のまま）、N63 の kind は 1 対 1（`ghci-regs.h` の 6 を `PROT_FAULT`、10 を `PERM_FAULT` に直し、`GHCI_IRQ_HALTED`（bit 6）と `GHCI_FAULT_WRITE`（FAULT_INFO[20]）を足した）、N62 の sampler slot は `w1[9:6]`（p002 の encoder には効かない、p006）。
- N47（attachment は linear、mip・layer は 0、layers は 1）: `ghci_render_pass_desc_encode` は layers ≠ 1 を断り、mip・layer を 0 で書く（cb937e6 で決まった形）。
- 第 15 版の N73〜N77 のうち p002 の code に効く物は無い（N75 の A8B8G8R8 の置き換えは p007 の `vk-caps.c`・p008 の descriptor）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3: 実装。
  - 新しい file（build には入れない。build に入る file は変えていない: `git status` で src は新しい 4 file だけ）: `src/drivers/gpu/ghci/ghci-regs.h`（register・bit・EXTCAP・`struct ghci_caps`）、`caps.c`（identify、decode、`ghci_caps_has`、EXTCAP の走査（8 個まで、範囲・重複を拒む）、CP_HEAP の大きさ、WDT_CYCLES、reset の待ち）、`ghci-cmd.h`（opcode・dword 数・flag・記録の offset・encoder の宣言）、`cmd.c`（全 command の encoder、PSO・texture・sampler・buffer・render pass の encoder、fence64）。ring の計画は今の `check.c` の `ghci_ring_plan`（1.0 の規則と同じ）を使う。design §18 の「`check.c` に新しい名前で足す」は、build に入る file を変えないため `cmd.c`・`caps.c` に置いた。
  - 試験: `plan/ws290/tests/ghci-host-test.sh` → `ghci-words-host-test: 141 passed, 0 failed`、`ghci-ref-host-test: 56 passed, 0 failed`（amaterrace-gpu cb937e6 の include/ghci_cmd.h の sizeof・offsetof と driver の定数を比べる。header は build/tmp の新しい directory に出すだけ）、`PASS ghci290-host-test`。
  - kernel の flag の compile: `make -n` の check.c の行を caps.c・cmd.c に替えて、amd64（plan/ws288/tests/config-amd64-ghci.mk）と riscv64（config-visionfive2-ghci.mk、-flto）の clang -Werror で compile、出力無し（warning 0）、object を build/tmp/p002-kcc/ に。
  - 未実施: `CONFIG_DRIVER_PCI_GHCI=y` の amd64 vmunix の build（build に入る file を変えていないので今のまま。build/amd64 の sysroot がこの worktree に無い）。
- 2026-10-28 P3（q1641-i01）: 仕様 20bf888 に追従。`ghci-regs.h` の kind 6・10 の名前、`GHCI_IRQ_HALTED`・`GHCI_FAULT_WRITE`、試験の既定の版を 20bf888 に。`plan/ws290/tests/ghci-host-test.sh` → words 141 passed・ref 56 passed、`PASS ghci290-host-test`。
