<!-- awesome-plan project=zedbsd record=ws129-p013 -->
# ws129-p013: 利用の手引きと既知の問題の下書き

Status: planned（2026-10-05 Q1、p005 から分けた）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q715 / q715-i01（P2）
目安: 1.5h（文書）

## 範囲

[release.md](../release.md) §9 の決定（U3・U8・U13 ほか）に従い、`docs/release/` に英語で次を書く:

1. 利用の手引き: USB への image の書き方（Linux・Windows・macOS）、BIOS/UEFI の設定、最初の login、Wi-Fi の接続、U3 の password と sshd の注意、対象の platform。
2. 既知の問題の最初の一覧: [Bug Board](../../known-bugs.md) の未解決の Bug から利用者に見える物を選ぶ。10/13 の RC で p005 が見直す。

機能の一覧（release notes の本体）は p005 に残す。

## 受け入れ

上の 2 つの文書が docs/release/ に在り、release.md §9 の決定と矛盾しない。docs/ から plan/ へ link しない。

## 所有 path

`docs/release/`、`plan/ws129/`。
