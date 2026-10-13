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

## review-1 とユーザーの決定（2026-10-14）

[review-1](review-1.md)（致命的 0、重大 R1〜R4、中 1〜9、軽微 1〜10）。ユーザーの決定（クリック）:
- U1（R4: suspend の hook が無いと S0 idle が全体で失敗）:「既定を n にする」→ CONFIG_DRIVER_PCI_AHCI の既定は n。試験の config でだけ y。suspend・resume は後。
- U2:「Intel の RAID mode にも bind」→ class 01.06.01 に加え、Intel の RAID mode（class 01.04.00）の controller を Intel の device ID の表で AHCI として bind（Linux と同じ）。RST が NVMe を AHCI の BAR に remap している機種は対象外（記録だけ）。
- U3・U4: 推しどおり（shutdown では FLUSH CACHE（EXT）だけ、STANDBY IMMEDIATE は出さない。hw.diskstats の「IDE」は受け入れ、UAPI を増やさない）。
技術判断（Q1）: R1〜R3 と中 1〜9、軽微は review の直し方の案どおり。試験の案 (a)〜(f) を T1 の依頼に入れる。
