<!-- awesome-plan project=zedbsd record=ws072p002 -->

# ws072-p002: BUG-059（NVMe の queue の回復の後の root の mount の ETIMEDOUT）

Phase ID: `ws072-p002`
Parent: [WS072](../ws.md)
Status: cleared
Queue: 2026-09-27 の並列実行

## 目的

[BUG-059](../../bugs/BUG-059.md): host の高負荷の QEMU で、起動中に NVMe の I/O queue が timeout で回復した後、root の mount が ETIMEDOUT（42）で失敗し起動が止まる。

## 受け入れ

command が driver の timeout を超える状況を作り、回復の後に同じ要求が成功する（呼び出し元に ETIMEDOUT が返らない）。通常の boot と UFS の回帰。規約。

## 原因

`pci-nvme.c` は command が timeout（controller の CAP.TO、上限 10 秒。QEMU は 7.5 秒）を超えると、走っている全ての command を ETIMEDOUT で失敗させ、queue を作り直す（`nvme_io_recover`、「I/O queue recovered」）。失敗した要求は出し直さず、そのまま bio の誤りになる。root の mount の superblock の読みがこれに当たると、VFS は root の mount を諦める。host の負荷で QEMU の NVMe の処理や guest の vCPU が止まると、tick が進んで timeout になりうる（BUG-059 の観測）。

## 修正

`nvme_disk_submit` は要求が ETIMEDOUT で終わったら、queue の回復を待って（`nvme_io_begin_bio` が回復の済んだ queue に要求を入れ直す）同じ要求をもう一度走らせる。最大 3 回（`NVME_IO_TIMEOUT_RETRIES`）。読み・同じ data の書き・flush はどれも繰り返して安全。1 回分の実行は `nvme_disk_run` に分けた。timeout 以外の誤り（device の status の誤りなど）と、回復できない queue（quarantine）はそのまま返す。場所: `src/drivers/pci/pci-nvme.c`。

## 再現と確認（QEMU、native の image、NVMe、2026-09-27）

再現の道具 `plan/tools/nvme/timeout-retry.sh IMAGE BPS READERS SECONDS`: 2 台目の NVMe を QMP の `block_set_io_throttle` で 2048 byte/秒に絞り、4 つの `dd` が 4 KiB の command を並べ、SECONDS 秒後に絞りを外して、各 `dd` の結果を見る。

| 確認 | 直す前（`build/ws063/fix.img`） | 直した後（`build/ws072/retry.img`） |
| --- | --- | --- |
| 絞り 22 秒 | 4 つ全部 `Connection timed out`（ETIMEDOUT） | 4 つ全部成功 |
| 絞り 15 秒 | 1 つが ETIMEDOUT | 4 つ全部成功 |
| 絞り 30 秒（直す前だけ） | 4 つ全部 ETIMEDOUT。その後の読みは成功（queue は回復している） | — |
| 絞りを外した後の読み | 成功 | 成功 |
| boot test | — | PASS（`build/ws072/boot-retry/login.png`） |
| UFS の強制終了（v3、5 秒） | — | UFS OK |
| build | — | amd64・rpi4 の vmunix warning 0 |
| 規約 | — | `pci-nvme.c` の style-check 336 → 335、変えた行の指摘 0 |

未実施: 起動の途中（root の mount の最中）に timeout を起こす試験（UEFI の loader の読みも絞られて起動が非常に遅くなるため）。root の mount の読みも同じ `nvme_disk_submit` を通るので、同じ直し方が効く。host の高負荷での boot の繰り返し（BUG-059 の元の条件）も未実施。実機は未実施。

## 結果（2026-09-27、cleared）

受け入れを満たした。BUG-059 は resolved にできる（元の条件の再現は未実施と記録）。
