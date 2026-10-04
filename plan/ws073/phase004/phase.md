<!-- awesome-plan project=zedbsd record=ws073p004 -->

# ws073-p004: BUG-063 — `truncate -s N` が無い file を作る、`mount -o rw` を受ける

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-063](../../bugs/BUG-063.md)

## 目的と受け入れ

- `truncate -s N FILE` が無い FILE を大きさ N で作る。`-c` のときは作らず、error にもしない（GNU・FreeBSD の truncate と同じ）。
- `mount -o rw` を受ける（既定と同じ、書き込み可能）。`-o` は comma の並び（`-o ro,nosuid`）と `defaults` も受ける。知らない option は従来どおり断る。

## 再現（修正前、QEMU）

main の測定用 guest image（`build/ws053-full-hal-guest`）の既存の utility: `truncate -s 100 nofile` が
`No such file or directory` で 1、`mount -t tmpfs -o rw tmpfs /mnt-t` が `unsupported option: rw` で 2。

## 原因

- `userland/base/truncate/main.c` は `truncate(2)` を呼ぶだけで、無い file を作る道が無かった。option は `-s` の固定の位置だけ。
- `userland/base/mount/main.c` の command line の `-o` は flag の option（ro・nosuid・writethru・nojournal）と `fspec=` の 1 つだけを受け、
  `rw`・`defaults`・comma の並びは fstab の経路にしか無かった。

## 修正

- truncate: `[-c] -s size`・`-sSIZE`・`--` を受け、各 file を `open(O_WRONLY | O_NONBLOCK [| O_CREAT], 0666)` して `ftruncate`。
  `-c` で ENOENT は成功として飛ばす。`O_NONBLOCK` は FIFO の open が reader を待たないため（ENXIO で断る。GNU と同じ）。
- mount: `apply_command_options()` が `-o` の値を comma で分け、flag の option は立て、`rw` は read-only を落とし（`-r` や前の `ro` より後が勝つ）、
  `defaults` は何もせず、`fspec=` は disk を名指す。usage の文言を更新。fstab の経路は変えていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j16 ZEDBSD_CONFIG=plan/tools/gnu-utils/config-amd64-base.mk BUILD=build/ws073-u build/ws073-u/bin/truncate build/ws073-u/bin/mount`
  warning 0。
- guest（main の測定用 image。新しい binary を `/tmp` に置いて比べた）: [tests/truncate-mount-options.sh](../tests/truncate-mount-options.sh)
  27 件。新しい binary で全て PASS、既存の binary では 19 件 FAIL（再現）。追加の確認: FIFO への `truncate -s 1` は待たずに ENXIO で 1、
  知らない option は usage で 1、`truncate -s 5 -- -dash` は `-dash` を作る。
- boot test: `make -j48 ZEDBSD_CONFIG=plan/tools/gnu-utils/config-amd64-base.mk BUILD=build/ws073-img disk-image`（warning 0）の image で
  `plan/tools/boot-test.sh` PASS（`build/ws073-img/boot-test/login.png`。fstab の mount の経路を含む起動）。
- 規約: truncate は全文の規約で書き直し `style-check.py` 0。mount は既存の file で `tests/style-diff.py` 0（変えた行）。
