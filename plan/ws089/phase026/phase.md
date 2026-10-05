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

## 一覧の実装（2026-10-05、P2、q741。判断の要らない部分）

- `settings/page-users.c`: card「Users on this computer」。`getpwent` で uid 1000 以上（nobody を除く）、shell が nologin・false でない account を最大 32 個並べる。各行は名前と「表示名 · Administrator（wheel の group か member）· You」。`getgrnam("wheel")` の結果は `getpwent` が storage を使い直しうるので写してから使う。log `USERS list count=N`。`settings.h` に `struct se_user_row`・`SE_USERS_LIST_MAX`。
- build: zedBSD の `bin/settings`、Linux の Keiland は warning 0。style-check は違反 0（変える前も 0）。
- QEMU は未実施（管理者の操作の判断の後にまとめて T1 に依頼する）。

## ユーザーの決定（2026-10-05）

ユーザー「WS089 p026は案Aにします。あとでレビューできるように、docs/にセキュリティ設計の文書を作成して、この設計について記述しておいてください。」→ **案 A**（setuid root の `account-admin`）。設計は Q1 が `docs/architecture/security.md` に書いた（ユーザーの review 待ち）。実装はその文書に従う。文書で Q1 が決めた細部（請求の形・理由の語・2 秒の遅延・4 KiB・home の既定は残す・login 中の利用者は消せない・uid 1000 以上だけ）は review で変わりうる。

## account-admin の実装（2026-10-05、P2、q748。段 1: 権限のある program と account の核）

- `userland/base/common/account.c/.h`（2d7eff0e）: lock を passwd・group・shadow の共通の lock に（`account_files_lock`・`account_files_unlock`）、任意の account file の全体の読み（`account_file_read`）と原子的な置き換え（`account_file_write`、同じ directory の新しい file に書いて sync・rename・directory の sync）。passwd・su・sudo の動きは同じ（`run-host-account.sh` 32 checks passed）。
- `userland/base/account-admin/`（新規、package `account-admin`、`/usr/libexec/account-admin`、mode 4555）:
  - `edit.c/.h`（file に触れない部分）: 請求の読み（パスワード・操作・引数、4 KiB まで、NUL を含む物・行の数の違う物・他の group は bad-request）、理由の語、名前と表示名の規則、空いている番号（uid と gid の両方で 1000 から）、行の追加・削除、group の member の付け外しと全 group からの削除（primary group は変えない）、人の account（uid 1000 以上）の管理者の数。
  - `main.c`: `security.md` の順に確かめる（実 uid、root は拒否、wheel でなければ password を確かめずに not-administrator、password は `login_verify`・誤りは 2 秒、対象は uid 1000 以上だけ・自分の削除と wheel からの自分の除外は self・人の account の最後の管理者は last-administrator、削除は process（`/dev/system`）か utmpx の login があれば busy、追加は名前・表示名・password の規則・name-taken・`/home/<name>` が在れば home-exists）。変更は lock の下で、追加は group→passwd→shadow、削除は shadow→passwd→group、各 file を原子的に置き換え、止める signal は保留。home は 0700・所有者は新しい利用者・`/etc/skel` の直下の通常 file だけを写す。remove-home は `/home/<name>`（passwd の home と一致する時だけ）を openat/unlinkat で link を辿らずに消す。環境は消し、argv は使わない。結果は syslog（auth）に操作・呼んだ人・対象・理由の語だけ（password と hash は出さない）、password の buffer は消す。
- 試験: `plan/ws089/tests/run-host-account-admin.sh`（gcc・clang、ASan・UBSan）→ 34 checks, 0 failed。image の実 file（root と wheel の GID 0、kei 1000）で、wheel の操作が root の primary group（0）と行を変えないことを確かめた。
- build: `config-amd64-account-admin.mk` で `bin/account-admin`・`passwd`・`su`・`sudo` が warning 0。image では `/usr/libexec/account-admin=4555`。style-check 0。
- 次（段 2・3）: kl_system_account_v1 の version 2（administer・refused）、libkeiland（KL_VERSION 31）、backend、compositor の system.c、Settings の UI。QEMU は全部の後に T1。

## 段 2・3: 経路と Settings の UI（2026-10-05、P2、q748）

- protocol（`keiland/kl-system-protocol.h`）: manager を version 8 に。`kl_system_account_v1` に request 2 `administer(uint request, string password, string operation)` と event 1 `refused(uint request, string reason)`（どちらも since 8）、capability `KL_SYSTEM_CAPABILITY_ADMINISTER`（0x100、system に tool が在る時だけ）、`KL_SYSTEM_OPERATION_MAX` 1024・`KL_SYSTEM_REASON_MAX` 31。
- libkeiland（KL_VERSION 31）: `kl_system_account_administer(system, password, operation, &request)`、`kl_system_account_refusal(system, request, word, size)`、`KL_SYSTEM_HAS_ADMINISTER`、`KL_ACCOUNT_OPERATION_MAX`・`KL_ACCOUNT_REASON_SIZE`。exports.map に足した。
- libkeiland-backend: `kl_backend_account_can_administer`・`kl_backend_account_administer`。zedBSD は `/usr/libexec/account-admin` を子として起動し（passwd -s と同じ形、標準入力に password と操作の行、標準出力の 1 行を読む）、`not-administrator`・`bad-password` は EACCES、他の拒否は EINVAL、`failed` は EIO、語を返す。Linux・FreeBSD は ENOTSUP。
- compositor（`wayland/system.c`）: account の job に administer を足し（同じ thread、1 度に 1 つ）、結果の前に refused で語を送る。log は `ZWL SYSTEM account administer ...`・`... result ... reason=WORD`（set-password の行は前のまま）。password と操作は job の後に消す。
- Settings（`page-users-admin.c` 新規、`page-users.c`）: 管理者（自分の行が wheel）で desktop が administer を持つ時、一覧の下に「Manage users」の card。一覧の行を click で選ぶ（選んだ行は selection の地）。「Add User...」と、選んだ他の利用者に「Reset Password...」「Remove...」「Make/Remove Administrator」「Allow/Stop Wi-Fi Control」。form（名前・表示名・新しい password・自分の password、追加は管理者の switch、削除は home を消す switch）、Cancel と実行、結果は語ごとの文。成功で一覧を読み直す。一覧に Wi-Fi（network）も出す。field は依頼か取り消しで消す。
- build: zedBSD の `bin/wayland`・`bin/settings`・`bin/account-admin` が warning 0、Linux の Keiland（`make keiland-linux`）も warning 0、`makefile-sync.sh` PASS。style-check: 新しい file は 0、変えた file は関数ごとに増えていない。
- T1 の試験（tty なし、Q1 の求めた passwd の短い password の拒否も含む）: `plan/ws089/tests/admin-p026-guest.sh`（image は `config-amd64-account-admin.mk`）。
- 未実施: QEMU（T1）、Settings の UI の PNG（desktop を kei で動かす必要があり、UAT で見る）、FreeBSD の build。

## T1-183 の FAIL の解析（2026-10-05、P2 g15）

- FAIL: `remove-alice` が `error busy`、続く alice-gone・alice-gone-group・home-exists（name-taken）は連鎖。
- 原因は試験の誤り: busy の後片付けが `ps -A -o pid,args | grep "[s]leep 40"` で sleep を探していたが、zedBSD の ps の args は kernel の command（`exec` の argv[0] だけ、`src/kern/exec.c`）なので "sleep 40" に一致せず、何も kill していなかった。sleep 40 が動いたまま alice は busy（account-admin の判定は正しい）。
- 直し（`admin-p026-guest.sh`）: `su alice -c 'exec sleep 40'` を 1 process にして `$!` を保存し、その PID を kill して、`ps -A -o user` に alice が無くなるまで（zombie も含む。init は約 1 秒ごとに reap）最大 10 秒待つ。待った結果を `OUTDIR/busy-end.txt` に残す。製品の変更なし。
