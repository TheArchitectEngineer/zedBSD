<!-- awesome-plan project=zedbsd record=ws163-p001 -->

# ws163-p001: 数字 6 桁の PIN の login（要件と設計）

Phase ID: `ws163-p001`
Parent: [WS163](../ws.md)
Status: planning（2026-10-05 P1 generation17 q733 で第 1 版。2026-10-05 ユーザーの決定で mock の設計（§9）に改めた、P1 generation19 q769。§9.6 の G1 の判断待ち）
Phase disposition: normal
Queue: q733（第 1 版）、q769（mock の設計と実装）

## 範囲

- ユーザー（2026-10-05、原文）:「数字6桁のログイン」
- 決めること: PIN を使う場面（greeter・lock の画面）と使わない場面、PIN の保存、試行の制限、password との関係、設定・変更・削除の道、greeter の画面。
- 範囲外: 実装（p002〜）。FIDO2 の login（WS162）。Linux・FreeBSD（login は GDM などで、Keiland の sessiond は zedBSD だけ）。

## 1. 今の形（2026-10-05 の main を読んだ）

| 項目 | 今 | 場所 |
| --- | --- | --- |
| greeter の login | greeter（`_greeter` の uid の compositor）が sessiond に descriptor 3 で `AUTH name password` を送り、sessiond が `login_verify`（shadow の hash）で確かめて `OK`・`FAIL`（失敗が続くと遅れ） | `userland/desktop/sessiond/greeter.c` 14・510〜584、`userland/base/login/verify.h` |
| lock の画面の unlock | session の compositor が control の descriptor で `UNLOCK password`、sessiond が同じ `login_verify` で session の利用者だけを確かめる。遅れは 2 秒から、3 回ごとに 2 倍、最大 16 秒 | `sessiond/session.c` 45・314・340〜384 |
| greeter の画面 | 利用者の card、password の欄（点で表示）、Log In。lock の画面は同じ greeter の描画で session の利用者だけ | `userland/desktop/wayland/greeter.c` 17〜40・150〜154 |
| password の規則（WS160 の決定、2026-10-05） | 最短 8 文字。sudo は毎回認証、wheel は gid 0 | `plan/master.md` 80 |
| 秘密の扱い | 入力された password は確かめた後に memory から消す（sessiond の line、compositor の buffer） | `session.c` 318〜320 |

## 2. PIN を使う場面

| 場面 | PIN | 理由 |
| --- | --- | --- |
| greeter の login（graphical seat） | 使える | ユーザーの要望。手元の端末の前にいる人の手早い login |
| lock の画面の unlock | 使える | 同じ |
| sudo・su・passwd・SSH・console の login | **使えない**（password だけ） | 6 桁は 100 万通りで、遠隔や権限の上昇には弱い。WS160 の「sudo は毎回 password で認証」を保つ |
| PIN の設定・変更・削除 | password で確かめる | PIN を知る人が PIN を変えて持ち主を締め出さない |

## 3. 保存と試行の制限

- 保存: root だけが読める file `/etc/keiland/pins`（0600 root）。1 行 1 account: `name:hash:failures`。hash は password と同じ crypt（SHA-512、
  rounds を password より多く、例 500000）で、salt は account ごと。PIN を平文で残さない。
- 6 桁は全部試しても 100 万通りなので、**hash では守れない**（file が漏れたら短時間で戻せる）。守りは「file を root だけが読める」と「試行の回数の上限」。
- **試行の上限**: PIN の失敗が 5 回続くと、その account の PIN を **無効にする**（password で login すると、失敗の数を 0 に戻して PIN が再び使える）。
  失敗の数は `pins` の file に残す（sessiond を起動し直しても、再起動しても数え直さない）。今の遅れ（2 秒から 16 秒）も掛ける。
- greeter と lock の画面は、PIN が無効になった account に「Too many wrong PINs. Use your password.」と出し、password の欄にする。

## 4. password との関係（protocol を変えずに済む形）

- WS160 で password は最短 8 文字なので、**ちょうど 6 桁の数字の入力は PIN の試み**と決められる。sessiond は `AUTH name secret`・`UNLOCK secret` で、
  secret が 6 桁の数字で、その account に有効な PIN があれば PIN と比べ、それ以外は今のとおり password と比べる。`AUTH`・`UNLOCK` の形は変えない。
- PIN を持つ account の古い password が 6 桁の数字だった場合（WS160 の前に設定）: その password では PIN の試みと見なされて login できない。PIN を
  設定する時に、password が 8 文字以上であることを求める（短い password のままの account には PIN を設定させない）。
- greeter が「この利用者は PIN を持つか」を知るための問い合わせを 1 つ足す: greeter → sessiond `PIN? name`、答え `PIN yes|no|locked`。PIN を持つ利用者を
  選ぶと、欄の上に 6 つの丸を出し、6 桁目で自動で送る。「Use password instead」の link で password の欄にする。

## 5. 設定・変更・削除

- Settings の Users の頁（WS089）の自分の account に「Set up PIN」「Change PIN」「Remove PIN」。今の password と新しい PIN（2 回）を入れる。
- 実行の道: session の control の descriptor に sessiond の新しい要求を足す（sessiond は root で、すでに `UNLOCK`・`SERVICE` を受けている）:
  - `PIN SET password pin` → `OK`・`FAIL`（password が違う、短い）・`INVALID`（6 桁の数字でない）
  - `PIN REMOVE password` → `OK`・`FAIL`
  session の利用者の自分の PIN だけ。秘密は確かめた後に消す。Settings から sessiond への道: Settings → compositor の system の拡張の account の object
  （`kl_system_account_*` に要求を足す）→ compositor が持つ session の control の descriptor（`--control-fd`、`UNLOCK` を送るのと同じ）→ sessiond。
  今の password の変更は、compositor の backend が set-user-ID の `passwd -s` を起こす別の道（`libkeiland-backend-zedbsd/account-zedbsd.c` 47・97）。
- 案の別: set-user-ID の小さな道具（`passwd -s` の形）。新しい set-user-ID の program が増えるので推さない（§7 の H3）。
- `root` の account には PIN を設定させない（root の login は今の image では lock されている）。

## 6. 試験

- host: sessiond の PIN の部分（`pins` の file の読み書き、6 桁の判定、失敗の数と無効化、password の login で戻る）を host で compile する試験。
- QEMU（T1）: login の image で、Settings（か試験の probe）から PIN を設定し、greeter で PIN の login、lock の画面で PIN の unlock、5 回の失敗で無効になり password で
  戻ること、`sudo` に PIN が通らないこと、SSH に PIN が通らないこと。`SESSIOND AUTH ok … method=pin` の log と greeter の screenshot。
- 実機（UAT）: 見た目と速さ。

## 7. 人間の判断が要る点

| ID | 問い | 案 |
| --- | --- | --- |
| H1 | PIN を使う場面 | greeter と lock の画面だけ。sudo・su・SSH・console は password だけ |
| H2 | 試行の上限 | 5 回続けて失敗したら PIN を無効にし、password の login で戻す（数は再起動を越えて残す） |
| H3 | **root の daemon の口**: sessiond に `PIN SET`・`PIN REMOVE`（session の control）と `PIN?`（greeter の auth の descriptor）を足してよいか | 足す（set-user-ID の新しい道具は作らない） |
| H4 | 保存の場所と形 | `/etc/keiland/pins`（0600 root、SHA-512 crypt、失敗の数） |
| H5 | 6 桁の数字の入力を PIN の試みとみなす（password は 8 文字以上なので重ならない。PIN を持つ account の password は 8 文字以上を求める） | みなす（`AUTH`・`UNLOCK` の形は変えない） |

## 8. 段（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | sessiond の PIN（保存・確かめ・上限・`PIN?`・`PIN SET`・`PIN REMOVE`）、host 試験 | H1〜H5 |
| p003 | greeter と lock の画面の PIN の欄、Settings の Users の頁の設定（WS089 の領域、Q1 の調整）、system の拡張の要求、T1 | p002 |
| p004 | 全文規約の見直し | p003 |

## 9. mock の設計（2026-10-05 ユーザーの決定、P1 generation19 q769）

ユーザー（2026-10-05、原文）:「~/.configの中にPINを保存してOKです。sessiondに難しい制御をさせたくないです。移植ができなくなるからです。これはまずモックアップとしての実装で、
あとで鍵管理やPAMのような仕組みをきちんと考えます。」→ §3〜§5 の案（`/etc/keiland/pins`、sessiond の `PIN?`・`PIN SET`・`PIN REMOVE`）をやめる。
H3（root の daemon の口）は「足さない」、H4（保存）は「利用者の ~/.config」に決まった。H1（greeter と lock の画面だけ、sudo・SSH・console は password）、
H2（5 回で無効、password で戻る）、H5（6 桁の数字の入力は PIN の試み）は §2〜§4 の案のまま mock に入れる。

### 9.1 保存

- file: `$HOME/.config/keiland/pin`（compositor の設定と同じ folder、`settings-store.c` の `.config/keiland`）。mode 0600、利用者の持ち物。書き換えは
  同じ folder の一時 file から rename。
- 中身（行の形、1 行 1 項目）: `hash <crypt の文字列>`（SHA-512 crypt、`$6$rounds=50000$<16 文字の salt>$...`、salt は `/dev/urandom`）と
  `failures <数>`（続けて間違えた数）。PIN を平文で残さない。
- 守り: file は利用者だけが読める（home は 0700）。6 桁は 100 万通りなので、file が読めれば短時間で戻せる。mock の限り（§9.5）。
- 道具の場所: compositor（`userland/desktop/wayland/pin-store.c`）。crypt() は POSIX（zedBSD の libc、Linux・FreeBSD は libcrypt）なので、
  Linux・FreeBSD の compositor でも同じ code が動く。

### 9.2 lock の画面（compositor だけで確かめる）

- lock の画面は session の利用者の compositor（その利用者の uid）が描くので、自分の `~/.config/keiland/pin` を読める。sessiond は lock の状態を持たず、
  今も `UNLOCK password` の確かめだけをしている（`sessiond/session.c` 340〜384）。PIN の unlock は compositor の中で終わり、sessiond に何も送らない。
- 入力の欄は今のまま（文字を打つ、Enter で送る）。PIN が有効な時は欄の上の案内を「PIN or password」にする。
- Enter で: PIN が有効で、打った文字がちょうど 6 桁の数字なら PIN と比べる（H5）。合えば unlock（今の OK の答えと同じ道）、`failures` を 0 に。違えば
  `failures` を 1 足して書き、「Wrong PIN. Try again.」。5 に達したら PIN を無効と見なし「Too many wrong PINs. Use your password.」、以後の 6 桁は
  password として sessiond に送る。それ以外の入力は今のとおり `UNLOCK password`。password で unlock できたら `failures` を 0 に戻す（PIN が再び使える）。
- 自動で送らない（6 桁目で送ると、6 桁の数字で始まる password が打てない）。
- 遅れ: PIN の失敗には遅れを掛けない（5 回で無効になるので、総当たりは 5 回まで）。password の遅れは sessiond の今のまま。

### 9.3 設定・変更・削除（Settings の Users）

- Settings の Users の頁（自分の account）に「PIN」の行: PIN が無ければ「Set Up PIN」、有れば「Change PIN」「Remove PIN」。設定と変更は今の password と
  新しい PIN（2 回、6 桁の数字）、削除は今の password。PIN の有無は Settings が自分で `~/.config/keiland/pin` を見て決める（Settings も利用者の uid）。
- 道: Settings → compositor の `kl_system_account_v1` に要求を 1 つ足す: `request 3 set_pin(uint request, string current, string pin)`（version 9、
  pin が空なら削除）。答えは今の `result`（ok・denied・invalid・unsupported・busy・failed）。
- compositor は今の password を、**sessiond の今の `UNLOCK password`** で確かめる（session の control の descriptor、`kl_backend_session_unlock`。
  sessiond は変えない）。OK なら PIN を hash して file を書く（削除なら file を消す）。FAIL なら denied（sessiond の遅れが掛かる）。session manager が
  無い（Linux・FreeBSD の手での起動など）なら unsupported。PIN と password は確かめた後に消す。
- 断る: PIN がちょうど 6 桁の数字でない（invalid）、今の password がちょうど 6 桁の数字（H5 で PIN と区別できない、invalid）。root の account は Users の
  頁に PIN を出さない。

### 9.4 試験

- host: `pin-store.c` を host で compile する試験（書く・読む・6 桁の判定・合う/違う・失敗の数・5 回で無効・password で戻す・削除・壊れた file・
  mode 0600）。`plan/ws163/tests/`。
- QEMU（T1、WS の最後にまとめて）: login の image で、Settings の Users から PIN を設定（今の password が違えば断られる）、lock して PIN で unlock、
  間違えて 5 回で PIN が無効、password で unlock して戻る、Remove。`ZWL LOCK unlocked`・`ZWL SYSTEM account` の log と screenshot。

### 9.5 mock の限り（後の鍵管理・PAM の設計で直すこと）

- PIN の hash と失敗の数は利用者の file で、利用者は自分で消せる・数を戻せる（自分の account なので害は小さい）。file が漏れれば PIN は戻せる。
- PIN の設定の時の password の確かめに `UNLOCK` を使うので、sessiond の log と syslog に「unlock」の行が出る。
- lock の画面の PIN の unlock は sessiond・syslog に記録されない（compositor の log の `ZWL LOCK unlocked pin` だけ）。

### 9.6 人間の判断が要る点（G1）

| ID | 問い | 事実 | 案 |
| --- | --- | --- | --- |
| G1 | **greeter の PIN の login をどうするか** | greeter は `_greeter` の uid で動き、利用者の home（0700）の `~/.config` を読めない。また login は sessiond の `AUTH name password` で、sessiond は password を確かめて session を始める（PIN だけで session を始める口が無い）。sessiond に口を足さない決定の下で greeter で PIN の login をするには、利用者の password を PIN で暗号化して `_greeter` が読める所に置くしかない。その file は local の誰でも読めるので、100 万通りを手元で試せば **password そのもの**が漏れる | (a) **mock は lock の画面だけ**。greeter は password のまま、後の鍵管理・PAM の設計で入れる（推す）。(b) password を PIN で包んで `_greeter` が読める所に置き、greeter で PIN の login（上の漏れを受け入れる）。(c) sessiond に口を足す（今回の決定に反する） |

G1 の答えまで、lock の画面・Settings・保存（§9.1〜§9.3）を実装する（G1 に依らない）。

## 結果

（設計の第 1 版。判断 H1〜H5 待ち）

2026-10-05 generation19: ユーザーの決定で mock の設計（§9）に改めた。H3・H4 は決まり、H1・H2・H5 は案のまま mock に入れる。G1（greeter）の判断待ち。
