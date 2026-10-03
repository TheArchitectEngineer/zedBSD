<!-- awesome-plan project=zedbsd record=ws073p003 -->

# ws073-p003: BUG-062 — i386 pcat の vmunix の `sched.c` の -Watomic-alignment

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-062](../../bugs/BUG-062.md)

## 目的と受け入れ

現在の main で i386 pcat の vmunix が -Watomic-alignment（-Werror）で止まらずに build できる。

## 結果

main は ws036-p021（commit 25fec44e）で `src/kern/sched.c` の idle_mask_clear を atomic_u64 の compare-exchange の loop に直していた。
この Phase ではコードを変えず、現在の main（ed1017a1）で確かめただけである。

## 検証（build だけ）

- `make -j48 ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/ws073-pcat vmunix`: rc 0、`warning:` 0、`error:` 0、
  `vmunix` は ELF 32-bit i386。
- pcat・pc98 の boot test は ws036-p021 の記録による。この Phase では未実施（コードを変えていない）。

## 注

- 前の WS073 の agent の branch（1023485a）の p003 の下書きは BUG-066 の別の修正の記録で、main は ba99a981 を採った。その下書きは使っていない。
