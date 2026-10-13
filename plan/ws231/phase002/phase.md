<!-- awesome-plan project=zedbsd record=ws231-p002 -->

# ws231-p002: AHCI の規約の全文の見直し

Status: cleared（書き方の修正のみ。QEMU の回帰は T1 が p001 の依頼で実施中）
Parent: [WS231](../ws.md)
Queue: P2（2026-10-14、conformance 担当）

## 範囲

`src/drivers/pci/pci-ahci.c`、`include/drivers/pci/pci-ahci.h`・`pci-ahci-protocol.h`、`src/kern/platform/pcat.c` の AHCI の部分、`plan/ws231/tests/ahci-protocol-test.c` を `plan/coding-style.md` の全文と照らした。動作・API・ABI・計算の順序・error の値は変えていない。

## 実行

- `python3 plan/tools/style-check.py <各 file>`（修正の前後）。clang-format は host に無く未実施。
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-conf build/p2-conf/vmunix -j16`: exit 0、warning 0、`amd64 vmunix check: PASS`。
- `sh plan/ws231/tests/run-ahci-protocol-test.sh`: c89 -pedantic の PASS、ASan・UBSan の PASS。
- `git diff --check`: 問題なし。

## 直した規則

- 段落の purpose comment が無い所に追加（約 45 か所）: loop 内の port 操作、`sched_yield()` の前、PCI command の書き戻し、`kern_logf` の前、IRQ 確保の後の flag、controller の初期化、など。
- 条件の中の関数呼び出し: `ahci_attach` の `drv_ahci_port_offset(1U)` を変数 `first_port_end` に、`pcat.c` の `drv_pci_ahci_driver_register()` を `ahci_error` に。
- 引数の中の入れ子の呼び出し: `ahci_stop_all_ports` の `ahci_port_read(PXCMD)`、`ahci_port_execute` の `ahci_port_read(TFD/SERR)` を名前の付いた変数に（読む順序は kern_logf の前のまま）。
- 条件演算子: `ahci_port_identify` の `write_cache ? ... : ...` を `if` と `cache_text` に。test の `CHECK` の `? 1 : 0` を `!= 0` に。
- 閉じ括弧の後の空行: `ahci_attach`・`ahci_port_admit`・`ahci_resume_start`・`drv_ahci_identify_parse`・test の `test_identify`。

## 残る点（直さない理由）

- `(void)id;` / `(void)device;` は `UNUSED_PARAMETER()` にしていない。マクロが amd64/i386 の `defs.h` にしか無く、arm64 でも使う driver なので（動作の変更ではなく移植性の判断）。
- `ahci_attach` の `goto fail`（単一の前方の共有 cleanup への jump、規約が許す形）。
- `ahci_port_wait_clear`・`ahci_port_execute` は失敗（ETIMEDOUT）の return で終わる。timeout まで poll する関数と retry 判定の流れで、成功の return を最後に置くと流れが逆になるため構造は変えない。
- style-check の `UINT32_C`/`UINT64_C`（macro）、`bar_claimed`（struct の field）、複数行の文の続き行は誤検出。
- `ahci_port_execute` の `kern_ms_to_ticks()` を含む算術式（`sched_ticks() + kern_ms_to_ticks(...) + 1U`）は代入の右辺で、条件の中ではないので残した。
- 欠陥に見える所: なし。

## 他の WS の違反

`src/kern/platform/pcat.c` の他の `if (drv_*_register() != 0)`（USB・PCI の多数、197〜304 行）は条件の中の関数呼び出しで、所有者の WS の見直しで扱う。
