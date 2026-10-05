<!-- awesome-plan project=zedbsd record=ws089-p026 -->

# ws089-p026: Users の頁の実装（この computer の利用者の account の管理）

Status: in-progress（2026-10-05、P2。設計を書いた。管理者の操作の口はユーザーの判断待ち。一覧の部分は判断なしに進められる）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q741（Q1、2026-10-05、P2）

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

## 設計（2026-10-05、P2、q741）

### 今の状態（source を読んだ）

- Users の頁はもう stub ではない: ws160-p002 で、Settings を動かす利用者の account（名前・表示名・home）と自分の password の変更（`kl_system_account_set_password` → compositor → libkeiland-backend → `passwd -s`、Settings は権限を持たない）が入っている（`settings/page-users.c`、KL_VERSION 27）。
- account の共通の核 `userland/base/common/account.c`（password の規則、SHA-512 crypt、`/etc/shadow` の安全な書き換え、wheel の確かめ、`/etc/passwd` の lock と読み書き）。`passwd`・`su`・`sudo` が使う。
- 管理者の操作（他の利用者の追加・削除・password の reset・group）の、root の権限を持つ口は無い。

### 案

1. **利用者の一覧**（権限は要らない）: Settings が `getpwent` で `/etc/passwd` を読み、uid 1000 以上（と root を除く system の account を出さない）の利用者を並べる（名前・表示名・管理者（wheel）か・自分か）。一覧は頁を開く時と変更の後に読み直す。
2. **自分の password**: 今のまま（ws160-p002）。
3. **管理者の操作の口**（root の daemon・setuid の口なので **ユーザーの判断が要る**）:
   - 案 A（推奨）: setuid root の小さな program `account-admin`（`userland/base/account-admin/`、`passwd -s` と同じ batch の形）。標準入力から「呼んだ利用者の password」と操作を受け、**呼んだ利用者が wheel に居て password が合う時だけ**、利用者の追加（`/etc/passwd`・`/etc/shadow`・`/etc/group`・home の作成と所有者）・削除（home は残すか消すかを選べる）・password の reset・group（wheel・network）の付け外しを行う。全ての要求を syslog の auth に記録。account.c の核を使う。backend（zedBSD）は `passwd -s` と同じく子として起動する（新しい `kl_system_account_*` の request、KL_VERSION を上げる）。
   - 案 B: sessiond（root の daemon）に `ACCOUNT …` の request を足す（ws089-p025 の `SERVICE sshd` と同じ形、wheel の確かめ）。sessiond は socket の相手の uid を知るので password の再入力が要らないが、root の daemon の口が増える。
   - どちらも、自分を最後の管理者から外す・自分を消す・root を変える、は拒む。
4. **UI**: 一覧の下に「Add User…」（名前・表示名・password・管理者にするか、と自分の password の確認）。各行に「Reset Password…」「Remove…」「Administrator」の switch。管理者でない利用者には一覧だけを出し、操作は出さない（今の password の変更は残る）。
5. 自動 login と greeter: 足した利用者は greeter の一覧に出る（greeter は `/etc/passwd` を読む、ws035）。自動 login の利用者の選択は後（この Phase の範囲の外）。
6. Linux・FreeBSD: 一覧は同じく `getpwent`。管理者の操作は unsupported（ENOTSUP、「この desktop の道具で」と表示）。各 OS の道具（AccountsService・`pw`）を包むのは後。

### 判断が要る点（Q1 経由でユーザーへ）

1. 管理者の操作の口: 案 A（setuid の `account-admin`、呼んだ利用者の password で確かめる）か、案 B（sessiond の request）。
2. 利用者の削除で home を消すかの既定（案: 残す。消すのは確認の上で選んだ時だけ）。
3. 一覧に出す利用者の範囲（案: uid 1000 以上と、shell が nologin でない利用者）。
