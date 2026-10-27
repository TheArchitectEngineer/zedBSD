# GHCI の K01〜K19 の回答を読んだ点（2026-10-27 Q1）

- K01 の回答: 正本は docs/ghci.md と include/ghci_regs.h・ghci_cmd.h。ghci.md・include・RTL・testbench は同期済み（回答の文）。MMU（段・PTE・walker・TLB）は driver から不可視で、driver が扱うのは session・VM_MAP/UNMAP・Grant の保護だけ。
- Q1 は貼られた回答だけを読んだ（ghci.md の本文とは未照合）。WS290 の再開の時に、その時の ghci.md の SHA で照合する。

| ID | 元 | 残る点 |
| --- | --- | --- |
| L01 | K17・H01 | `CMD_PIPELINE_BARRIER` を 0x2D とするが、H01 の表では 0x2D は `CMD_LOAD_CONST`。 |
| L02 | K06・K18・J04 | 0x084 は J04・J06 で EXT_CAP_FEATURE、K06・K18 で EXT_CAP_CLOCK（FEATURE は 0x088）。0x080〜0x08C の 4 本は §3.1 の表に入ったか。 |
| L03 | K02・K03・J02 | WDT_CYCLES は設定の値だが「EXT_CAP から読む」とある。watchdog の周期は driver が書けるのか、読むだけか。CTRL の bit 4 は J02 で TLB_FLUSH、K03 で WDT_EN: TLB_FLUSH は無くなったか。 |
| L04 | K09・H22・J13 | CMD_FENCE は K09 で 32 bit の値、H22・J13 では 64 bit が必須。6 dw の中の配置（値は何 dword か）。 |
| L05 | K02・K08・J12 | CP_HEAP を driver が設定しないなら、MMU_OOM の後の回復は何か（session を閉じるだけか）。MMU が不可視なら、CMD_SESSION_OPEN の `root_pt_phys`（host が root を確保）は残るのか。 |
| L06 | K03 | CP_STATE は 3 bit になった: STATUS のどの bit か（以前は [19:16]）。session だけの FAULT は「該当セッションのステータス」に記録とあるが、その status はどこで読むか。 |
