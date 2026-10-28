<!-- awesome-plan project=zedbsd record=ws129 -->

# WS129: ベータ1 のリリース作業

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。fg019 ベータ2（＝最初の公開ベータ）のリリースは 2026-10-17 OSC で達成（2026-10-23 ユーザー「OSCデモは完了した」）。release notes は docs/release/zedbsd-1.0.0-beta2.md）
Completed: 2026-10-27（q1608）
Primary Milestone: MG007
Related Milestones: MG003, MG006
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-02 ユーザー「次のFeature Goalはベータ1のリリースにします」「リリース目標は10/17です」。nightly から版のついたリリースを作り、fg019 の各 WS の成果を集め、既知の問題を明示し、最終回帰を通す。受け入れ R1〜R7（版の一つの源、release の image の config、CI の release の job、release notes と既知の問題、license の一覧、最終回帰、実機の確認）。定義は [release.md](release.md)、license は [licenses.md](licenses.md)。

## ユーザーの決定

- 2026-10-02「凍結はしません。できたところまででベータ1にします。安定化はベータの最後の方のバージョンで行います。」デモの image は CI の設定を土台に変える。
- 2026-10-02「CIはPrereleaseを生成、それをダウンロードして動作確認した私が、PrereleaseからLatest Releaseに手動昇格します。」→ エージェントは昇格・公開をしない。
- 2026-10-02「zedbsd-0.1.0-beta1 にしましょう。」（当時の決定）
- 2026-10-02「両方載せる、でお願いします。」→ Prerelease の配布物は USB の image と Windows の QEMU/Venus の zip の 2 つ。
- 2026-10-04: REmacs のコマンド名を `/bin/emacs` に（p012）。
- 2026-10-06 夜（Q1 経由）「ベータ1は難なく前倒しできるので、実際には最初のベータはベータ2で、2026年10月17日に公開するのはベータ2に変更です。」→ 版は `1.0.0-beta2`、release の config は `config/release/config-amd64-beta2.mk`、文書は `docs/release/zedbsd-1.0.0-beta2-*.md`、tag は `zedbsd-1.0.0-beta2-rc<N>`・`zedbsd-1.0.0-beta2`。
- 2026-10-10: GitHub の CI の build のエラーの修正（p014）。
- 2026-10-23「OSCデモは完了した」。

## 結果

- 版の一つの源（`VERSION` → uname・起動の表示・`/etc/os-release`、p003）、release の image の config と CI の release の job（`release.yml`、p004）、image の全 component の license の一覧と生成の script（`tools/release/license-inventory.py`、release の config で 27 components・open 0、p002）、試験の QEMU の KVM への統一と build の並列（p011）、`/bin/emacs`（p012）、nightly の CI の nasm の依存（p014）、デモの image を CI の設定を土台に（p009、一部）、desktop の全 app を CI と試験の config に（p010）。
- 利用の手引きと既知の問題の下書き（`docs/release/`、p013）。release notes は `docs/release/zedbsd-1.0.0-beta2.md`。
- ベータ2 は 2026-10-17 の OSC で公開（ユーザーの昇格・公開）。証拠は T1-038・T1-082・T1-104・T1-105・T1-119（QEMU）と host の試験。

## 制限・移管

- p005〜p008（release notes の集約、最終回帰、実機の確認、公開）は Phase としては planning のまま、ユーザーの公開で閉じた。p009 は CI と同じ完全な image の build と boot test が未達（uncleared）、p010 は Q1 の判定の印なし、p013 は review の印なし。
- 次の release の作業は新しい WS で立てる。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws129-p001 | release の定義 | cleared（2026-10-05 Q1、回答は release.md §9） |
| ws129-p002 | license の一覧 | cleared（2026-10-09 Q1、ベータ2 の分） |
| ws129-p003 | 版の一つの源 | cleared（2026-10-05 Q1、T1-104・T1-119） |
| ws129-p004 | release の image の config と CI の release の job | cleared（2026-10-05 Q1、T1-105） |
| ws129-p005 | release notes・既知の問題・利用の手引き | planning のまま WS を閉じた（ベータ2 の公開で済んだ） |
| ws129-p006 | release candidate の最終回帰 | planning のまま WS を閉じた（凍結は無し） |
| ws129-p007 | 実機の確認（5330・5320） | planning のまま WS を閉じた（ユーザーの動作確認と昇格） |
| ws129-p008 | 公開 | planning のまま WS を閉じた（2026-10-17 にユーザーが公開） |
| ws129-p009 | デモの image を CI の設定を土台に作る | uncleared のまま WS を閉じた（完全な image の build と boot test は未達） |
| ws129-p010 | desktop の全 app を CI と試験の config に入れる | cleared 候補のまま WS を閉じた（q606、Q1 の判定の印なし） |
| ws129-p011 | 試験の QEMU を KVM に統一、build の並列 16 | cleared（q646、T1-038） |
| ws129-p012 | REmacs のコマンド名を `/bin/emacs` に | cleared（2026-10-04 Q1、T1-082） |
| ws129-p013 | 利用の手引きと既知の問題の下書き | in-progress のまま WS を閉じた（下書き済み、review の印なし） |
| ws129-p014 | nightly の CI の libavcodec の configure の失敗（nasm） | cleared（2026-10-10） |

## 記録の所在

Phase の計画・結果・試験（`phaseNNN/`・`tests/`）は削除する（Q1 の pipeline）。release の host 試験（`host-release.sh`・`host-version.sh`・`license-inventory-test.sh`）は plan/tools/ へ移す案（Q1 の判断）。git の commit `fb1f6afee` の `plan/ws129/` にある。Queue の履歴は [plan/history](../history/index.md)。
