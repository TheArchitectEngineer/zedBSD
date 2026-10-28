<!-- awesome-plan project=zedbsd record=ws194 -->

# WS194: make keiland-linux・keiland-freebsd の必要な package の確認と導入、build 後の install の確認

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p002 cleared（T1-498・T1-506、2026-10-10 ユーザーの UAT「WS194はOK」）。p003 の規約は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

- `make keiland-freebsd`: 足りない FreeBSD の package を一覧にし、ユーザーに確かめてから `pkg install` する。build が成功したら install してよいかを確かめ、実行する。
- `make keiland-linux`: apt・yum（dnf）・pacman のどれかがあれば、足りない package を一覧にして確かめてから導入する。build が成功したら install してよいかを確かめ、実行する。
- 確かめは端末の対話（y/N）。端末でない時は導入・install をせず、何が要るかを出して止まる。

## ユーザーの決定

- 2026-10-09（由来）「make keiland-freebsdは、必要なFreeBSDパッケージを、ユーザに確認してからインストールしてほしいです。ビルドが成功したらinstallしていいか確認して、インストールを実行してほしいです。
  あと、make keiland-linuxは、apt/yum/pacmanがあれば、必要なパッケージをユーザに確認してからインストールしてほしいです。テストはaptだけでいいです。ビルドが成功したらinstallしていいか確認して、インストールを実行してほしいです。」
- 2026-10-09（クリック）「両方ベータ2」。
- 2026-10-10（UAT）「WS194はOK」。

## 結果

- keiland-linux・keiland-freebsd の規則に package の一覧（apt・yum・pacman・pkg）と y/N の確認、build の後の install の確認を足した（main）。
- Debian 13 の QEMU+KVM の guest（T1-498）で一覧 → y/N → 導入 → build → install の y/N → rc 0、FreeBSD 15 の guest（T1-506）で質問・MAKE_RC=0・install、端末なしの場合は両方 PASS。yum・pacman は一覧の対応だけで試験していない（ユーザー「テストはaptだけでいいです」）。

## 制限・移管

- p003（規約の全文の見直し、shell・make）は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws194-p001 | 規則と依存の調べ、package の一覧と確認の対話の実装、host 試験 | cleared（2026-10-09、T1-498・T1-506） |
| ws194-p002 | T1 の Debian（apt）と FreeBSD（pkg）の guest での試験 | cleared（2026-10-10、ユーザー「WS194はOK」） |
| ws194-p003 | 規約の全文の見直し | planning のまま WS を閉じた（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws194/` にある。Queue の履歴は [plan/history](../history/index.md)。
