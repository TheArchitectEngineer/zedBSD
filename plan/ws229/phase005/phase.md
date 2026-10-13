<!-- awesome-plan project=zedbsd record=ws229-p005 -->
# ws229-p005: Vulkan の通しと実機（Gen9）、5330 の回帰

Status: planned（2026-10-14 Q1、q980、P1。準備の部分を先に）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q980
依存: p002・p003（merge 済み）・p004

## 範囲

- 準備（p002・p004 の merge を待たずにできる物）: `tests/execution/eu-test.c` の Gen9 の kernel、Gen9 の実機の USB の image の config（`plan/ws229/tests/config-amd64-gen9.mk`、Gen9 の boot の指定 `i915.gen9=on` 相当（p002 が決める key）を含む）、実機の手順（6・7・8 世代の laptop を USB で直に起動、SSH で Vulkan の試験 eu-test → vkcs → vkx・vke1・vke2・vkc の読み戻し）、5330 の回帰の T1 の依頼の文（`plan/ws101/tests/hw/run-hw.sh`・`plan/ws075/tests/test-hw.sh`）。
- 統合: p002・p004 が main に入った後、全体の build と host の試験、image を作り、実機の手順をユーザーに渡す（Q1 経由）。
- ユーザーの決定（phase001 の末尾）: U1 既定 off（実機 PASS で on）、U2 Gen9 の間は suspend を断る、U3 5330 の回帰 PASS がベータ2 の条件。

## 確かめ

- 実機（ユーザー）で Vulkan の試験の読み戻しが PASS。5330（T1）で回帰が PASS。
