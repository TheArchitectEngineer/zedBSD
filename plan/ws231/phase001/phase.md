<!-- awesome-plan project=zedbsd record=ws231-p001 -->
# ws231-p001: 基本の AHCI の driver

Status: planned（2026-10-14 Q1、q981、P2）
Disposition: normal
Parent: [WS231](../ws.md)
Queue: q981
依存: 無し

## 範囲

[ws.md](../ws.md) の「基本（今日）」。設計の要点を phase.md に短く書いてから実装する（Q1 が design-reviewer を通す。期限があるので実装と並行）。
- NVMe（`src/drivers/pci/pci-nvme.c`）の attach・disk の登録・bio の処理・DMA・MSI の書き方に揃える。USB storage の error の扱いも参考に。
- 既存の root の選び方（boot の root の指定、disklabel）で AHCI の disk が選べること。
- HAL の API は変えない。`plan/coding-style.md` の全文を適用。Makefile（`platform/*/vmunix.mk` の driver の一覧）に足す。

## 確かめ

- amd64 の kernel の build warning 0。host で試せる部分（FIS・command の組み立て、PRDT の分割）の host 試験。
- T1 への依頼の文: QEMU q35 の ich9-ahci に disk image を付け、boot の log でなく SSH で読み書き（dd と cmp）、partition の認識、AHCI の disk からの root の mount（できれば）、boot-test.sh の PNG。
