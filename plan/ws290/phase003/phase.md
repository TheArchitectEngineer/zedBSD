<!-- awesome-plan project=zedbsd record=ws290-p003 -->

# ws290-p003 model・stub・script

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p003（版 4.5、ghci.md cb937e6）。
- 状態: cleared（2026-10-28 P3 q1641-i01）
- 依存: p002

## 範囲

model・stub・script（§14）。WS288 の 1.1 の試験の削除の list を Q1 へ

詳細は design の該当の節（§4〜§16）と §14 の p003 の場面。

## 受け入れ

- model の自己試験 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 16 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

- 無い。仕様担当とのレビューは d9252e4（回答 第 16.0 版、2026-10-28）で N01〜N77 が全部解決し閉じた（design §19）。model は d9252e4 の読みで書いた。
- 仕様が開いたままの読みではなく、driver を悲観の側で試すための model の switch（`struct model_switches`）: CLOSE が CP_HEAP を返さない、PROT = 0 の CLOSE が共有の PTE を消す、閉じた session への VM_UNMAP が kind 1、RESET・MEM_READY・HALTED が遅れて見える、reset が終わらない（AXI_STUCK）。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1641-i01）: 実装。
  - `plan/ws290/tests/ghci-model.h`・`ghci-model.c`: GHCI 1.0 の device の model。register の window（§3.1）と EXTCAP の list・block（VEND・PROT・MEM・FMT・DISP2 の offset は §4.1）、profile 10 個（design §14: kc705-uma・kc705-discrete・minimal・full・flat・prot・no-blt・no-su・discrete・mismatch）、host 32 MiB と local 16 MiB、doorbell で ring を読む CP（header の 0 dword・端を跨ぐ・WPTR の先 → kind 3、未知の opcode → kind 1、長さ → kind 3、guard の無い command → kind 2、WPTR が ring の外 → kind 2）、`CMD_FENCE`（64 bit、MEM_WRITE・IRQ、届かない番地は kind 5）、`CMD_WAIT_FENCE`（CP_STATE 4 で待つ）、session（OPEN・CLOSE・BIND、SESSION_ACTIVE）、VM（PROT = 1 は session ごとの table と root、PROT = 0 は共有の table、衝突 → kind 10、閉じた session への MAP → kind 1、flat は SESSION_OPEN・VM_MAP が kind 2）、CP_HEAP の勘定（root、40 bit の L1、2 MiB ごとの L0、N64）と MMU_OOM（client は session だけ、session 0 は装置、RUN で root が入らなければ装置）、reset の保つ・消す（§5.2）、HALT（IRQ_HALTED）、IRQ の level（IRQ_GLOBAL・IRQ_ENABLE、handler が消さなければ数える）、fault・watchdog の注入（今か session の次の draw か）、PROT = 1 の client の fault（FAULTED = 0、その session の draw は skip、fence は書く）、hold（fault の無い hang）、Bus Master が切れた間の fetch を数える、走っている間の RING_BASE などの書き込み → kind 7、受けた command の記録。DISP2 の動作は p012 で足す。
  - `plan/ws290/tests/ghci-stubs.h`・`ghci-stubs.c`: kernel の service と GPU core の stub（1 thread、偽の時計、MMIO を model の window へ、RAM を model から、lock の名前ごとの順の検査（`stub_lock_order`）、待ちの queue ごとに持ってはいけない lock（`stub_forbid_wait`、門と ticket の待ちに使う）、待ちの途中の hook、二重の lock・持っていない unlock・BAR0 の外の MMIO を数える）。`drv_gpu_register` の検査は `src/drivers/gpu/gpu.c` の `gpu_ops_validate` を試験の時に awk で取り出して `stub_gpu_ops_validate` として使う（gpu.c と常に同じ）。今の `plan/ws288/tests/ghci-driver-host-test.c` の stub の部分を移して書き直した。
  - `plan/ws290/tests/ghci-model-host-test.c`: model と stub の自己試験 16 場面（identify は caps.c の decode で全 profile、ring と fence、wrap、framing、reset の表と switch、共有の VM、PROT の VM と client の fault、MMU_OOM、flat、watchdog、HALT、IRQ の line、Bus Master、kind 7・kind 5・hold、WAIT_FENCE、stub の検査）。期待値は ghci.md d9252e4 から書いた。
  - 試験: `plan/ws290/tests/ghci-host-test.sh`（既定の版を d9252e4 に）→ `ghci-words-host-test: 141 passed, 0 failed`、`ghci-ref-host-test: 56 passed, 0 failed`、`ghci-model-host-test: 253 passed, 0 failed`、`PASS ghci290-host-test`（gnu11、-Wall -Wextra -Werror、ASan・UBSan）。
  - kernel の flag の compile（build に入る file は変えていないが ghci-regs.h の定義を直したので）: `make -n` の check.c の行を caps.c・cmd.c に替え、amd64（plan/ws288/tests/config-amd64-ghci.mk）と riscv64（config-visionfive2-ghci.mk）で -Werror、出力無し（warning 0）、object は build/tmp/p003-kcc/。
  - 未実施: `CONFIG_DRIVER_PCI_GHCI=y` の amd64 vmunix の build（build に入る file は変えていない。p004 で行う）。
  - WS288 の 1.1 の試験の削除の list（Q1 へ送った。p004 で 1.1 の driver を build から外す時に消すのがよい）: `plan/ws288/tests/ghci-driver-host-test.c`・`ghci-host-test.c`・`ghci-host-test.sh`・`ghci-vk-host-test.c`・`ghci-e2e-core.c`・`ghci-e2e-kernel.c`・`ghci-e2e-main.c`・`ghci-e2e-wayland.c`・`ghci-e2e.h`・`ghci-e2e.sh`・`ghci-e2e/scale.comp`・`ghci-su-board.py`。理由: 1.1 の driver・firmware・Verilator の model の試験で、plan/tools.md・未完の WS・tests/scenarios から参照されない（参照は WS288 の閉じた phase の記録と T1 の済んだ依頼だけ）。残す物: `bringup-tb.*`・`zybo-gpu-sd.dts`（plan/tools.md が参照）、`gsi1-*`・`gsi1/`（p006 で移す）、`config-*-ghci.mk`（p004・p013 で ws290 に写して直す）。
- 2026-10-28 P3: design 版 4.8 の H2（d9252e4 の 1931 行: VM_UNMAP は table を返さず、CLOSE と reset だけが返す）に model を合わせた: L0 の印を table ごとに持ち、VM_UNMAP では残し、PROT の SESSION_CLOSE と reset で消す。`ghci-model-host-test: 254 passed, 0 failed`、`PASS ghci290-host-test`（08808fd79）。
