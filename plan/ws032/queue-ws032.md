<!-- awesome-plan project=zedbsd record=queue-ws032 -->

# Queue q315: WS032 外部パッケージのクロスビルド導入

<!-- awesome-plan-current:start -->
Status: active
Workspace: ws032
Executor: q315-root
Goal: WS032 完了（`userland/packages/` の clang・OpenSSL・OpenSSH がクロスビルドででき、実機で動く）
Item: q315-i01..i03 cleared; i06 はビルド・パッケージ化まで完了（ターゲット実行の確認は未了）、i04 は継続
<!-- awesome-plan-current:end -->

本記録は WS032 専用の Queue である。`plan/queue.md`（q314 finished）と
`plan/ws031/queue-ws031.md`（WS031、別エージェントが `~/zedBSD/` で実行中）には触らない。

Authorization (planning): current user「今回は、WSを新規作成します。～ userland/packages/ に、
clang, OpenSSH, OpenSSLを追加することです。makeでリリースのsource code tarballを取得して
展開し、ビルド用のパッチを当ててビルドする形にします。」（2026-09-20）
Authorization (scope decisions): 2026-09-20 のユーザー選択 — C++ランタイムは本WSに含める、
ツールは clang+lld+llvm-ar 等一式、OpenSSH は sshd を含む、リンクは最初から動的。
Authorization (start): current user「では実行してください。」（2026-09-21）

Prepared UTC: 2026-09-20
Start UTC: 2026-09-21

Timebox: 1440 active minutes 見積、120 active minutes ごとに進捗・残件を確認。
各コマンドを有限化する: 取得 600 秒、host 試験 300 秒、通常 build 1800 秒、
LLVM/clang のクロス build 7200 秒、VM 300 秒。同条件無変更 retry は 3 回まで。
達成の保証ではない。未達は証拠と再開条件を残す。

## 実行順と現況

| Order | Attempt | Phase | Status | Scope |
| --- | --- | --- | --- | --- |
| 1 | q315-i01 | ws032-p001 | cleared | 設計固め・版と入手元の確定・ライセンス監査方針（180 分） |
| 2 | q315-i02 | ws032-p002 | cleared | 共通取得機構 `external.mk` と host 試験（300 分） |
| 3 | q315-i03 | ws032-p003 | cleared | クロスビルド契約（wrapper・cross cache・toolchain file）（300 分） |
| 4 | q315-i04 | ws032-p004 | in-progress | libc・ヘッダの不足補完（300 分） |
| 5 | q315-i05 | ws032-p005 | planning | C++ ランタイム（libunwind/libc++abi/libc++）（360 分） |
| 6 | q315-i06 | ws032-p006 | in-progress | OpenSSL（360 分） |
| 7 | q315-i07 | ws032-p007 | planning | OpenSSH（420 分） |
| 8 | q315-i08 | ws032-p008 | planning | clang（480 分） |
| 9 | q315-i09 | ws032-p009 | planning | イメージ統合・ライセンス・provenance（240 分） |
| 10 | q315-i10 | ws032-p010 | planning | レビュー（180 分） |

p004 は一度で閉じない。後続 item が見つけた libc/ヘッダ不足は p004 へ差し戻して閉じる。

## 停止条件

- 取得した tarball に GPL 系ライセンスが混入した。
- HAL（`include/hal/hal.h`）または UAPI の変更が必要になった。
- `ZEDBSD_LLVM_PATCH_LEVEL` の変更（toolchain cache 無効化・LLVM 全再ビルド）が必要になった。
- `~/zedBSD/` の別エージェントが担当する `src/drivers/gpu/i915/`、`plan/ws031/`、
  `platform/amd64/` を変更する必要が生じた。
- ユーザー承認が要る操作（host の package 導入、reboot、git add/commit/push、
  GitHub 公開）に達した。

## 境界

HAL/UAPI 不変。aggregate `make check` は使わない。既存の無関係な変更を保護する。
source/doc の git add/commit/push はユーザーが行う。GitHub Issue/Project 公開は別途の指示。
