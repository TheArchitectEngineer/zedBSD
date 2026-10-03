<!-- awesome-plan project=zedbsd record=ws062 -->

# WS062: amd64 の disk image を ESP の vmunix・UFS の root partition・swap partition の構成にする

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG004
Related Milestones: MG002（fg011: configure の性能）
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: q436・q437・q456（p001〜p003）、2026-09-27 の並列実行（p004）
Resume point: —（完了。残りは下の「制限・移管」）
<!-- awesome-plan-current:end -->

## 目標

2026-09-25 ユーザー指示: 「オーバレイファイルシステムでこれ以上の改善が見られない場合、ディスクイメージを変更して、vmunixをEFIシステムパーティションに置いて、rootfsはUFSのパーティションにしましょう。スワップは単独パーティションにしましょう。これでかなり性能が上がると思います。」

受け入れ: amd64 の既定の image が native で、QEMU の UEFI 起動で login prompt、SSH の harness、swap partition が有効、root に書ける。expat の configure（`/root`）が tmpfs の +20% 以内。sh・make の差分試験、`SMP-STRESS.ELF`、COW、boot の試験が今と同じ。規約。

## 結果（2026-09-27、completed）

| 項目 | 結果 |
| --- | --- |
| layout `native`（amd64 UEFI） | GPT: ESP（FAT32 64 MiB、`EFI/BOOT/BOOTX64.EFI`・`vmunix`・`zedbsd.cfg`）、`zedBSD-root`（UFS、1 GiB）、`zedBSD-swap`（raw の swap、1 GiB）。1 MiB 境界、primary と backup の GPT。`zedbsd.cfg` は `rootpart=PARTLABEL=zedBSD-root`、`swap0=PARTLABEL=zedBSD-swap`。`zedimage-host disk --layout native`、検査器 `platform/amd64/tools/check-amd64-native-image.py` |
| 既定 | amd64 の既定の variant は native（Makefile・menuconfig・CI の config）。image は 2,216,689,664 byte、CI は gzip（約 84 MB）。`hybrid`・`uefi`・`bios` は残した |
| 道具 | `guest.py start` の既定は NVMe、`boot-test.sh` の既定は `BOOT_MODE=uefi-nvme` |
| 起動・harness | QEMU の UEFI（USB・NVMe）で login prompt、root は UFS の partition（rw）、swap は partition、SSH の harness が動く |
| configure | p002 の時点では `/root` 23〜25 秒・tmpfs 12.8 秒で未達（UFS の write-through と flush が原因）。その後 ws061-p006（write cached）と WS060・WS063（batched journal）で `/root` 11.3 秒（ws063-p001、host 10.9〜11.2 秒）となり、tmpfs 以下 |
| p002 で直した不具合 | BUG-053（RAM を超える anonymous memory: reclaim の待ち行列に resident な page だけを置く、SIGKILL で fault の待ちをやめる、page-out の worker の休み、終了した process の page を外す）、kernel heap の `kern_free` を O(1) に |
| 回帰 | p002: make の差分試験 91/91（8 GiB・512 MiB）、SMP 6/6、COW、itimer、swaphog 450 MiB、sh の差分試験。p003: guest-batches 1412/1458（既知と同じ）、CI の再現 |
| 規約 | p004: WS062 で変えた C を全文で見直した（下の表）。style-check の数は WS062 の前より多い file は無い |

QEMU だけ。実機は未実施。

### 規約（ws062-p004、2026-09-27）

見直した C の source と直したこと:

| file | WS062 の変更 | p004 で直したこと | style-check（WS062 の前 → 後） |
| --- | --- | --- | --- |
| `tools/build/zedimage-host.c` | native の layout（`disk_create_native`・`native_copy_sparse`・`native_input_sectors`・`native_align`）、`parse_size_bytes`（`M` の接尾辞）、producer の上限 16 GiB | 3 つ以上の節の条件を行に分けた、`strcmp`・`memcmp` の結果の名前（`same` → `differs`・`repeated`）と Boolean を式で作らない、`gpt_entry` の呼び出しを 1 行 1 引数、段落を分けて comment、GUID の抽選の loop の comment、`disk_create_variant` の複数行 comment の形 | 73 → 70（WS063 の分を含む） |
| `src/kern/vm.c` | swapped の list（`swapped_queue`・`swapped_insert`・`queue_kind`）、page in での移動、SIGKILL で待ちをやめる、page-out の worker の休み | Boolean を if で作る、critical section の空行、`queue_kind` の flag の意味の comment、隣の comment | 673 → 673 |
| `src/kern/signal.c`・`include/kern/signal.h` | `signal_kill_pending()` | 段落を分けた、critical section の空行、成功の return の comment | 108 → 107 |
| `src/kern/heap.c` | `kern_heap_owner` の O(1) の検査 | 規約どおり（変更なし） | 7 → 7 |
| `include/kern/vmspace.h` | `queue_kind` と `VM_QUEUE_*` | 規約どおり（変更なし） | — |

残る指摘は critical section の本体の空行を `paragraph-comment` と数える道具の誤検出（vm.c 2・signal.c 2 の新しい行）。C 以外（`Makefile`・`platform/amd64/*.mk`・Noct の script・`check-amd64-native-image.py`・`guest.py`・`boot-test.sh`）には全文の規約が無く、project の規則（AGENTS.md・Guardrail）で見た（限界として記す）。

確認: amd64 の build warning 0・image の検査器 OK、rpi4 の vmunix warning 0、boot test PASS（`build/ws063/boot-p004/login.png`、native・NVMe）、zedimage-host の UFS の出力が変更前の binary と byte 単位で同じ（`plan/tools/ufs/zedimage-compare.sh`、`4096M`・`64M` の接尾辞を含む 7 通り）。意味を変えない書き直しなので、回帰はこの build と boot（AGENTS.md の規則）。swap の試験は未実施。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws062-p001 | native の layout の image の生成（`zedimage-host`・Makefile の variant・検査器）と QEMU の起動 | cleared（q436-i01） |
| ws062-p002 | native の image で harness・swap・性能・回帰、BUG-053 の修正 | cleared（q437-i01） |
| ws062-p003 | amd64 の既定と試験の道具を native に、文書、CI | cleared（q456-i01） |
| ws062-p004 | 規約の全文との照合 | cleared（2026-09-27） |

Phase の記録は git の履歴にある（WS の完了で削除）。

## 制限・移管

- 範囲外（Future Work）: pcat（BIOS）・pc98・rpi4 の image の native 化、installer の変更。
- [F-014](../future-work.md): `zedbsd.cfg` を ESP の後に BOOT の FAT から探す、vmunix と loopback の image を UFS から直接 load（2026-09-25 ユーザーの追加指示、後回し）。
- `PARTLABEL=` は全 disk で一意である必要がある（zedBSD の disk が 2 台あると曖昧）。root の inode は image の道具の上限（65536）に縛られる。
- BUG-053 の 1300 MiB の swaphog は未確認（BUG-053 に記録）。
