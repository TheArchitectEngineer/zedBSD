<!-- awesome-plan project=zedbsd record=ws072p002 -->

# ws072-p002: BUG-059（NVMe の queue の回復の後の root の mount の ETIMEDOUT）

Phase ID: `ws072-p002`
Parent: [WS072](../ws.md)
Status: planned
Queue: none

## 目的

[BUG-059](../../bugs/BUG-059.md): host の高負荷の QEMU で、起動中に NVMe の I/O queue が timeout で回復した後、root の mount が ETIMEDOUT（42）で失敗し起動が止まる。回復の前後の要求の扱いを調べて直す。

## 受け入れ

負荷をかけた host（例: `stress-ng` と並べる）での boot test の繰り返しで、queue の回復が起きても root の mount が成功する（回復が起きたことを gdbstub・QMP で確かめる、または回復を人工的に起こす）。回復の起きない通常の boot の回帰。規約。

## 調べること

NVMe の driver の timeout と queue の回復（未完了の要求を出し直すか、ETIMEDOUT で返すか）、root の mount の一時的な誤りへの再試行の有無。
