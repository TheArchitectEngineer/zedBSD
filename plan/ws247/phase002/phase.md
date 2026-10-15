<!-- awesome-plan project=zedbsd record=ws247-p002 -->

# ws247-p002: 規約の全文の見直し（WS247 が変えた comment と log の文字列）

Status: in-progress（q990、2026-10-16）
Parent: [WS247](../ws.md)
Approval: 2026-10-16 ユーザー「ではp002をサブエージェント1つで実施してください。」

## 範囲

src/drivers/gpu/nvrtx/（compiler/ を含む）で WS247 が変えた comment と log の文字列を、plan/coding-style.md の全文と plan/guardrail.md の「MIT・GPL の出典のデータの引用」（referenced: の形: block の直前、複数行の comment、file の上部に書かない）に照らして直す。差分の基点は 7379c9ffd（WS247 の最初の commit 301f60672 の親）。その間の WS246 p001（compiler の sm75.c・sm75.h へのまとめ）の code の移動は範囲外で、comment の hunk だけを見る。**書き方だけを直し、動作・値・識別子・log の意味は変えない。**

## 確かめ

- 変更が comment だけなら `cpp -fpreprocessed -P` の比較（空白を除く）が同じ。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws247-k build/ws247-k/vmunix` rc 0・warning 0。
- Q1 が merge の後に host 試験 sm75・desktop・vk-d3・vk。

## 結果

（Q1 が書く）
