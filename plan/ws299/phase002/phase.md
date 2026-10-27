<!-- awesome-plan project=zedbsd record=ws299-p002 -->

# ws299-p002 buffer cache の header の確保を一定の手数に（512 MiB の崖の直し）

- 親: [WS299](../ws.md)。Queue: q1616 / q1616-i01（担当 P1、2026-10-27）。前提: [p001](../phase001/phase.md)（H1 が崖の原因）。
- Status: in-progress（実装・host 試験・build・素の 5330 の前後の測定と起動の回帰、QEMU の回帰（2026-10-27 q1617、P1 自身）は済み。Q1 の判定待ち）
- Disposition: normal

## 受け入れ

- buffer header の確保と返却が slab の数に依らない手数（irq を止める区間も一定）。
- host 試験 PASS、build の warning 0（amd64 と buffer cache を使う他の platform）。
- 5330 の実機で前後を測り、`/dev/nvme0n1` の連続の読み 4 GiB で崖が無い（最後の 256 MiB が最初の 0.8 以上）。

## 設計

- 原因（p001）: `alloc_metadata` が全 slab の list を新しい物から歩き、空きのある slab を探した（`cache_lock`、irq を止めて）。cache が上限に達して evict が古い slab に空きを散らすと 1 header に約 1.7 万 slab（1 ms）、上限の前も head の slab が埋まる度に全 slab を歩いた。`free_metadata` も空いた slab を list から外すのに頭から歩いた。
- 直し: 空きのある slab だけを双方向の list で持つ index（`include/kern/buf-slab.h`、`struct buf_slab_index`・`struct buf_slab_record`）。
  - take: list の先頭の slab の最も低い空きの slot（64 bit の mask を高々 64 回）。最後の空きなら list から外す。
  - put: slot を空きに戻し、満杯だった slab を list に戻す。空になった slab は空きの slab を 1 つ（`BUF_SLAB_EMPTY_KEPT`）だけ残し、それ以上は index から外して page を返す（前の「head の slab だけは残す」の代わり）。
  - 二重の返却・範囲の外の slot は -1 で何もしない（前は範囲の外だけを無視し、二重の返却で `used` がずれ得た）。
- `struct buf_slab` は record を先頭に持つ（index と slab が同じ address）。全 slab の list（`slabs`）は他に使う所が無いので無くした。
- evict（LRU の尾から）、hash、`reserve_bytes` は元から一定の手数（p001 の数で確かめた）なので変えていない。
- HAL・UAPI は変えていない。

## 実施（2026-10-27）

| 物 | 内容 |
| --- | --- |
| `include/kern/buf-slab.h`（新） | 上の index。lock も memory も持たない純粋な記帳（host で試せる、`buf-unjournaled.h` と同じ形） |
| `src/kern/buf.c` | `struct buf_slab`・`slab_index`・`slab_grow`・`alloc_metadata`・`free_metadata` を index に。触った型と変数の `XXX` の注記を直した |
| `plan/ws299/tests/host/buf-slab-test.c`（新） | 全 slot を 1 回ずつ渡す・返して渡し直す・空の slab を 1 つ残して他を返す・誤った slot を拒む・64 slot の mask、2 万 slab を埋めて「最も古い slot を返して 1 つ取る」を 200 万回（1 秒以内） |

### 確かめ

| 確かめ | command | 結果 |
| --- | --- | --- |
| host 試験 | `cc -O2 -Wall -Wextra -Werror -I include -o buf-slab-test plan/ws299/tests/host/buf-slab-test.c && ./buf-slab-test` | `buf-slab-test: PASS`、200 万 cycle・2 万 slab で 0.029 s |
| build（5330） | `make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=build/p1-5330-p002 vmunix` | rc 0、warning 0 |
| build（他） | `config/ci/config-{rpi4,armv7-virt,pcat,riscv64-virt}.mk` の `vmunix` | 全部 rc 0、warning 0 |
| 5330 の前後の測定 | `sh plan/ws299/tests/nvme-bench-5330.sh zedbsd zb-p002a build/p1-5330-p002/vmunix read write`（前は `zb-main`、main 7ceb026de） | 下の表 |
| 5330 の起動の回帰（NVMe から） | `sh plan/tools/hw5330/5330-run.sh --nvme-kernel build/p1-5330-p002/vmunix p1-ws299-p002-nvme` | `5330-run: PASS`（ssh まで 20 秒、i915 の survey PASS、steady registers 0 違い）。要約 `plan/tools/hw5330/evidence/p1-ws299-p002-nvme.md`、画面 P1 の worktree の `build/hw5330/logs/p1-ws299-p002-nvme.png` |
| `git diff --check` | | 何も出ない |

- 測った kernel（sha256 `82aef0ec…`）は、`slab_grow` の注記を 1 つ足す前の build。足した後の build（`build/p1-5330-p002b`）は行の番号の分だけ binary が違う（code は同じ）。

### 前後の数（素の 5330、MB/s、要約 `plan/ws299/evidence/zb-main-summary.md`・`zb-p002a-summary.md`）

| 測り方 | 前（main） | 後（p002） |
| --- | --- | --- |
| 冷えた 128 MiB、bs 64k・1m・4m | 305・163・91 | 344・384・363 |
| 連続 256 MiB ×16（4 GiB）、最初の段・最後の段 | 56・15.0（上限の後の段 3.3〜21） | 384・363（比 0.95） |
| 同、各段の dd の sys / real（秒） | 4.0/4.8 → 上限の後 12〜66/13〜83 | 0.18〜0.39/0.70〜0.84 |
| raw の書き p6 256 MiB ×4 | 6.2・4.1・7.4・14.3 | 655・655・597・597 |
| UFS の file の書き 256 MiB ずつ | 268・77・53 | 433・479 |

- 上限（985 MiB）の後も evictions = misses のまま一定の速さ。残りの時間の大半は driver の待ち（p003・p004 の対象）。

### QEMU の回帰（2026-10-27 10:43、q1617-i01、P1、tree bb02f7b51、QEMU の証拠のみ）

- image: `make ZEDBSD_CONFIG=plan/tools/posix/config-amd64-serial.mk BUILD=build/p1-qemu disk-image`（rc 0、7 分 38 秒。warning は openssl の package だけ、kernel は 0。vmunix sha256 `eb48f3d428011dbb…`）。
- `OUTPUT=build/p1-qemu-boot-nvme plan/tools/boot-test.sh build/p1-qemu/hdd-image.img`（既定の uefi-nvme）: exit 0、画面に `login:`。PNG は P1 の worktree の `build/review/ws299/p002-qemu-nvme-login.png`。
- `BOOT_MODE=uefi-usb`（USB の mass storage）: exit 0、画面に `login:`。PNG `build/review/ws299/p002-qemu-usb-login.png`。
- 読み書き（`plan/tools/guest/amd64-serial.sh`、NVMe の root、8 GiB、cache の上限 1072910336）: 400 MiB の乱数の file を書いて `cksum` 2907330156 → raw の `/dev/nvme0n1` を 2000 MiB 読んで cache を一巡（current_bytes が上限、evictions 267745、writeback_errors・capacity_failures・physical_failures 0）→ 同じ file の `cksum` が同じ値 → 64k で写した file も同じ値 → 残りを `/dev/zero` で埋めて ENOSPC（`No space left on device`、df 100%）の後も stats の失敗 0。
- virtio の disk: tree に virtio-blk の driver が無い（`virtio` は venus と m68k の framebuffer だけ）ので行えない。arm64・riscv64 などの QEMU の起動は未実施（build の warning 0 は上の表）。

## 未実施・残り

- 規約の全文の見直しは p008 で。
