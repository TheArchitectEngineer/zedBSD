<!-- awesome-plan project=zedbsd record=ws247-p002 -->

# ws247-p002: 規約の全文の見直し（WS247 が変えた comment と log の文字列）

Status: cleared（2026-10-16、q990）
Parent: [WS247](../ws.md)
Approval: 2026-10-16 ユーザー「ではp002をサブエージェント1つで実施してください。」

## 範囲

src/drivers/gpu/nvrtx/（compiler/ を含む）で WS247 が変えた comment と log の文字列を、plan/coding-style.md の全文と plan/guardrail.md の「MIT・GPL の出典のデータの引用」（referenced: の形: block の直前、複数行の comment、file の上部に書かない）に照らして直す。差分の基点は 7379c9ffd（WS247 の最初の commit 301f60672 の親）。その間の WS246 p001（compiler の sm75.c・sm75.h へのまとめ）の code の移動は範囲外で、comment の hunk だけを見る。**書き方だけを直し、動作・値・識別子・log の意味は変えない。**

## 確かめ

- 変更が comment だけなら `cpp -fpreprocessed -P` の比較（空白を除く）が同じ。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws247-k build/ws247-k/vmunix` rc 0・warning 0。
- Q1 が merge の後に host 試験 sm75・desktop・vk-d3・vk。

## 結果

- **q990 P1（conformance-reviewer、提出 3826c9184・b2a5b4ce6・c8bab8ab6、main に merge b64f2f7a2）**: 45 file。前の変更で崩れた comment の形（字下げの多い ` * ` 約 118 行、80 桁超 約 100 か所、1 行の長い `/* … */`、ちぎれた段落）を直した。sm75.c の出典だけの block 18 か所を説明と 1 つの block に統合。work-records.c の順序と Procedure への直し。identify.c の `(WS240)`・nvrtx-gsp.h の `(5f)` を除いた。products.c の pci.ids の出典を Values referenced: の形に。log の文字列は不変。cpp の比較で全 file 同じ。
- **Q1 の確かめ（main b64f2f7a2）**: build rc 0・warning 0、host 試験 sm75 287/0・desktop 138 compiled/0 disagreed・vk-d3 122/0・vk 60/0。

## 直さずに残した点（ユーザーの判断・後続）

1. 2 つ目以降の path の省略（`Linux v6.19 drivers/gpu/drm/nouveau/…` の後の `nvkm/…`、NVIDIA の後の `ctrl/…`・`class/…` など約 73 か所）を認めるか（ユーザーに聞いた）。
2. sm75.c の `Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c` の 3 か所: 意図した版（p003 で P2・P3 が 25.0.7 の tree で中身を確かめた。25.3.6 の取得物に src/compiler が無い）。直さない。
3. identify.c の `NVIDIA open-gpu-doc (the class numbers)` に path が無い。
4. nvrtx-gsp.h の `as both other drivers give it`（名前なしで出典を指す文）。
5. log の冗長（`unload unload call`、`devinit begin: devinit`、`static-info begin: static info`）。
6. style-check.py の 716 件（大半は compiler/sm75.c の移動した code と他の WS の code、comment 以外で範囲外）。

## Q1 の確かめ直し（2026-10-16、main 1d8cc4d19）

WS247 の並列作業の前（352b53fee）と今の main で、nvrtx の変更 61 file を `cpp -fpreprocessed -P`（空白を除く）で比べた。差は runtime.c・vk-node.c の log の段の名前の 5 行（D2 → renderer、D3 → executor、意図した改名）だけで、他の 59 file は comment だけの変更。build rc 0・warning 0（amd64 vmunix check PASS）、host 試験 sm75 287/0・desktop 138 compiled/0 disagreed・vk-d3 122/0・vk 60/0。
