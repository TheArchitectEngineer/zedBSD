<!-- awesome-plan project=zedbsd record=ws172-p004 -->
# ws172-p004: セキュリティチップの調べ（survey）

Status: in-progress（2026-10-08 P1: 文書の第 2 版を仕上げた（design-reviewer の指摘を反映、一次資料の確認）。5330 の TPM の start method の確認（Q1）を待ちつつ、ユーザーの review へ出せる（Q1 経由））
Disposition: normal
Parent: [WS172](../ws.md)

- 文書: [survey.md](survey.md)（第 2 版）。
- design-reviewer（2026-10-08）: blocker 3（所有と lockoutAuth、PIN を bus に出さない session、未確認の事実での判断）・major 11・minor 12。全て第 2 版に反映（survey.md §7）。推しを案 C から案 D（kernel は汎用の transport・claim・allow-list・電源・同一性、意味は userland の libsecurity と小さな root の program）に変えた。
- 一次資料で確かめた物: Linux の `tpm_crb.c`（start method、Pluton、AMD fTPM の quirk）と `Kconfig`（`TCG_TPM2_HMAC` の既定 n）、参照の実装 `DA.c`（`recoveryTime=0` は DA の無効、disorderly の加算、`lockoutRecovery=0`）、Dell の Latitude 5330 の資料（discrete の ST33、SPI の見込み）、Microsoft Learn（Windows の DA の値）、FreeBSD の tpm2-abrmd。
- 未確認で残す: 5330 の ACPI `TPM2` の start method（5330 が Linux の時に Q1 が読む）。S3 はその後。
- 次: §6 の確認（特に 5330 の実機の ACPI `TPM2` の start method）→ ユーザーの判断 S1〜S13 → p004b。汎用の `/dev/securityN` は disk の暗号・web の passkey も使う基盤なので、別の WS として立てることを Q1 に提案（WS172 はそれに依存）。
- 2026-10-13 P2（q950）: 着手の時点で survey は第 2 版のまま完成、残りは (1) §6 の 1（5330 の ACPI `TPM2` の start method と `_HID`、5330 を Linux で起動した時に Q1 が読む。5330 は今 zedBSD の単独起動）と (2) ユーザーの判断 S1〜S13（survey §5）。どちらも P2 の範囲で埋められないので、p004 は判断待ちのまま、p004b（設計）は S1〜S13 と S3 の前提の確認の後に始める。Q1 に判断の依頼を返した。
