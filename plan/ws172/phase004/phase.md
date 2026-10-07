<!-- awesome-plan project=zedbsd record=ws172-p004 -->
# ws172-p004: セキュリティチップの調べ（survey）

Status: in-progress（2026-10-08 P1: 文書の第 2 版（design-reviewer の指摘を反映）。§6 の確認（5330 の TPM の start method など）の後、ユーザーの review へ（Q1 経由））
Disposition: normal
Parent: [WS172](../ws.md)

- 文書: [survey.md](survey.md)（第 2 版）。
- design-reviewer（2026-10-08）: blocker 3（所有と lockoutAuth、PIN を bus に出さない session、未確認の事実での判断）・major 11・minor 12。全て第 2 版に反映（survey.md §7）。推しを案 C から案 D（kernel は汎用の transport・claim・allow-list・電源・同一性、意味は userland の libsecurity と小さな root の program）に変えた。
- 一次資料で確かめた物: Linux の `drivers/char/tpm/tpm_crb.c`（start method、Pluton、AMD fTPM の quirk）、Dell の Latitude 5330 の資料（discrete の ST33、SPI の見込み）。
- 次: §6 の確認（特に 5330 の実機の ACPI `TPM2` の start method）→ ユーザーの判断 S1〜S13 → p004b。汎用の `/dev/securityN` は disk の暗号・web の passkey も使う基盤なので、別の WS として立てることを Q1 に提案（WS172 はそれに依存）。
