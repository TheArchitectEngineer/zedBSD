<!-- awesome-plan project=zedbsd record=ws256-p026 -->

# ws256-p026 PLDA の PCIe の MSI（VF2）

- 親: [WS256](../ws.md)。設計: [design-peripherals.md](../design-peripherals.md) §3.5・§3.6・§7（MSI の demux は Future Work だった）。
- 状態: in-progress（2026-10-26 P2 が実装と実機の確かめを済ませ、Q1 の判定を待つ。branch agent/p2-ws256p024）
- 目的: VF2 の M.2 の KC705（WS288 [p043](../../ws288/phase043/phase.md)、GHCI は MSI 1 つ、今の bitstream は MSI が無効だと割り込みを出さない）と NVMe で MSI を使う。

## 決定

- 2026-10-26 06:21 ユーザー「では、まだ時間があるので、MSIを実装したらどうでしょう。」→ Q1 の問い（PLDA の MSI の出典、WS256 design-peripherals の Minor 3「後で MSI を足す時は出典の扱いを改めて聞く」）の回答「Linux も値だけ参考」= OpenBSD sys/dev/fdt/stfpcie.c（ISC）の `#ifdef notyet` の MSI の部分に加え、Linux の drivers/pci/controller/plda/pcie-plda-host.c（GPL-2.0）の register の offset と bit の値だけを参照してよい（code は写さない）、出典は file の Value(s) referenced に記録。→ WS256 p026（PLDA の MSI）を新設、P2 が p024 の後に。

## 範囲

1. pci-plda.c: PLDA の MSI の受け口（MSI の address の窓、MSI の status・mask の register、1 本の PLIC の線の中の MSI の demux、vector の割り当て）。
2. PCI core: host の bridge が自分の MSI を出す道（drv_pci_bus_ops の hook など、kern_irq_register_msi の HAL の道の代わり）。HAL の API（include/hal/hal.h）を変えるなら差分ごとの承認（plan に差分を置く）。amd64・arm64・riscv64 virt の MSI と INTx の道は変えない。
3. 試験: host の試験（fake の host）、実機の NVMe を MSI で（INTx から MSI に変わる事、読み書きの一致は p024 の rw.sh）、後で KC705 の GHCI の MSI。

## 受け入れ

実機の NVMe が MSI で動き（log の MSI の表示、rw.sh の一致）、PCIe0 の VL805 と INTx の道も今までどおり。build の warning 0、変えた所の host の試験。

## 記録

- 2026-10-26 06:21 Q1: 新設。

## 結果（2026-10-26 07:28、P2、worktree ~/zedBSD-worktrees/p2-ws256p024、branch agent/p2-ws256p024）

### 実装

| file | 中身 |
| --- | --- |
| `include/drivers/pci/pci.h` | `drv_pci_bus_ops` に省略可能な `msi_establish`・`msi_disestablish`（両方か無し）と `drv_pci_msi_run_t`（int を返す: 仕事が有れば非 0）。無い host（amd64・arm64・virt）は今までどおり `kern_irq_register_msi`。**HAL（hal.h）は変えていない** |
| `src/drivers/pci/pci.c` | `message_register`・`message_unregister`・`message_run`: MSI・MSI-X の 7 か所の `kern_irq_register_msi`/`kern_irq_unregister_msi` をこれに通し、host に hook が有れば host から message を取る（cookie の `host_message`、`irq` に host の handle）。EOI は host が自分で 1 回 |
| `include/drivers/pci/pci-plda.h`・`src/drivers/pci/pci-plda.c` | DT の `msi-controller` で `config.msi`。start で message の status を W1C し、`IMSI_ADDR`（0x190）を読む: JH7110 は 0 を返すので、Linux の StarFive の host が使う address 0x190 を使う（値だけ）。vector 32 本の表（run・argument・in_flight）、`host_msi_establish`（空きの vector、data = vector、最初の message から IMASK の bit 28）・`host_msi_disestablish`（1 ms × 1000 回の drain、超えたら戻して EBUSY）。MSI は 1 message だけ、MSI-X は出さない。割り込みは pin の後に local の status を MSI の bit を除いて W1C、MSI の bit が有れば `serve_messages`: MSI の status を読んで W1C → local の MSI の bit を W1C → handler、status が 0 になるまで最大 8 回。handler が仕事を見つけない時は register を 16 回読む待ちの後に最大 8 回 run し直す（下の 2） |
| `src/drivers/platform/visionfive2/vf2-pcie.c` | PCIe0（STG 0x048、VL805）は `config.msi = false`（DT は両方に `msi-controller` が有るが、USB は p020 のまま INTx） |
| `plan/ws256/tests/pci-plda/` | DT の pcie1 に `msi-controller`、fake の IMSI_ADDR・ISTATUS_MSI・NVMe の MSI capability。試験を足した: host 0 は MSI を拒む、MSI が pin より先・MSI-X は出さない、address・data・enable、IMASK、message の handler・W1C の順・EOI 1 回、pin と message が同時、仕事の遅い handler の run し直し（3 回、上限 1 + 8 回）、vector の無い bit、持ち主の無い vector、解除（IMASK・MSI enable を戻す） |

### 実機で分かった事（JH7110、生きた serial と SSH）

1. `IMSI_ADDR`（0x190）は 0 を読む（両方の host）。address 0x190 に向けた message は届く（ISTATUS_MSI の bit 0）。
2. **message が data を追い越す**: MSI を使うと、起動の最初の I/O（GPT の読み）で毎回 `nvme: I/O queue recovered`（5 秒の timeout）。handler の前に register を 32 回読む待ちを入れた debug の kernel で消えた。NVMe の CQ の entry が、その message を処理する時に memory にまだ見えていない。INTx は level なので、見えるまで割り込みが続き、表に出なかった。対策: handler が仕事を見つけない時は短く待って run し直す。最終の kernel の log: `pcie1: MSI 0 found its work after 1 retries (the message came before the data)`（起動で 1 回、log は host ごとに最初の 1 回だけ）。
3. local の MSI の bit を先に W1C すると、MSI の status の W1C の後にまた立ち、message ごとに空の割り込みが 1 回増えた（trace で見た）。順を「MSI の status → local の bit」にした。
4. 途中の誤り: debug で ISR から `kern_usleep_range` を呼び `fatal: src/hal/riscv64/clock.c:99: nested riscv64 IRQ acknowledgement`。SSH が戻らず、U-Boot の `load …/vmunix.old` と `fatwrite … /vmunix` で 5c83c16 の kernel に戻した（serial、生きた console）。

### 確かめ

| 確かめ | 結果 |
| --- | --- |
| 実機の log（この branch の kernel 24635ae59 と同じ code） | `pcie1: MSI at 190 (register 0), 32 vectors`、`nvme: PCI controller 0001:01:00.0 … MSI`、`xhci: PCI controller … irq=1 INTx`（PCIe0 は INTx のまま）、`queue recovered` 0 回、init は 2.1 秒（INTx の時と同じ） |
| `rw.sh write /dev/nvme0n1 3`（MSI） | 21/21 ok、末尾の先は拒否。write_ops 3903 を MSI で、recovery 0 |
| 電源の入れ直しの後の `rw.sh verify`（MSI） | 7/7 の cksum が 3 回目の書き込みと一致。read_ops 1321（device から、MSI）、recovery 0 |
| host 試験 | `plan/ws256/tests/pci-plda/run.sh build/tmp/p024-host` → `180 PASS, 0 FAIL`・`oversize: 35 PASS`、通常と ASan/UBSan。`pci-intx-hook/run.sh`・`ecam/run.sh` も PASS。変異: serve を外す → 6 FAIL、ack を handler の後 → 1 FAIL、IMASK の MSI の bit を立てない → 1 FAIL |
| build | VF2・amd64・rpi4（`config/ci/config-rpi4.mk`）の vmunix が exit 0・warning 0 |

### 残り

- KC705 の GHCI の MSI（WS288 p043、P3）。GHCI の handler は仕事が有った時に非 0 を返す事（0 だと PLDA が 8 回 run し直す）。
- 仕事の無い message（NVMe の ISR が前の割り込みで先に取った分）でも run し直しの 8 回 × 16 回の読みが掛かる。rw.sh の時間は INTx と同じだったが、測ってはいない。
- MSI-X、複数の message（MME）は出さない。
- 2026-10-26 07:30 Q1: agent/p2-ws256p024 565ac7991（p026 の 4 commit）を main に merge = 7ec661a6e（衝突なし）。受け入れ（実機の NVMe が MSI で動く: log の「pcie1: MSI at 190」「nvme: … MSI」、rw.sh 21/21・電源の入れ直しの後 7/7・回復 0、PCIe0 の VL805 は INTx のまま、build の warning 0（VF2・amd64・rpi4）、host の試験 180 + 35 PASS と mutation 3 種の検出）を満たす → **cleared**。PCI core の変更（drv_pci_bus_ops の msi_establish・msi_disestablish、7 つの呼び出しを helper に、hook の無い host は今までどおり）は amd64・arm64 の QEMU の回帰が未実施（WS の区切りで T に）。残り: MSI-X・複数の message、仕事の無い message の再試行の費用（未測定）。
