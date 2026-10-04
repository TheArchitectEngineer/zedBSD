<!-- awesome-plan project=zedbsd record=ws089-p026 -->

# ws089-p026: Users の頁の実装（この computer の利用者の account の管理）

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

## ユーザーの指示（2026-10-04 夜）

「SettingsのUsersタブですが、これも実装しましょう。」

## 今の状態

Users の頁は stub（`userland/desktop/settings/pages.c:43`、`se_soon_draw`、「Accounts on this computer.」）。

## 範囲（設計で確定）

1. 利用者の一覧（名前・表示名・管理者か・自分か）。
2. 自分の password の変更（今の password の確認）。
3. 管理者の操作: 利用者の追加・削除・password の reset・管理者の権限（`wheel` 相当の group）・`network` group（WiFi の制御、Guardrail 2026-10-02）などの group の付け外し。
4. 自動 login・greeter での表示（sessiond・greeter との関係）は設計で決める。表示名・avatar は設計で決める。
5. 経路: Settings → libkeiland（kl_system_*）→ compositor の拡張 → libkeiland-backend → zedBSD の account の仕組み（`/etc/passwd`・`/etc/master.passwd` 相当、`pw`・`passwd` の command）。app は OS の口を持たない（Guardrail）。管理者の操作の権限の確かめ（誰が他の利用者を変えられるか、確認の password）を backend と system の側で行う。
6. Linux・FreeBSD の Keiland: 各 OS の仕組み（AccountsService・`pw` など）を backend で包むか、読むだけにするかを設計で決める。

## 受け入れ（案）

- 一覧が system の account と合う。自分の password を変えると次の login で新しい password が効く。管理者が利用者を足すと greeter と login に出る。管理者でない利用者は他の利用者を変えられない。
- C の全文の規約、build warning 0、OS の境界の checker。QEMU は T1、実機は UAT。

## 依存

sessiond・greeter（WS035 の成果）、kl_system_*（WS131）、service・account の userland。
