<!-- awesome-plan project=zedbsd record=ws089-p025 -->

# ws089-p025: Sharing の頁に SSHD の ON/OFF

Status: cleared（2026-10-05 Q1: T1-162 の settings-p025 PASS（kei の session で Remote Login を off・on、rc.conf と auth の syslog）、sharing-on.png を Q1 が目視（Running・port 22・host key の SHA256・ssh kei@10.0.2.15）。wheel でない利用者の拒否は host の試験）。以前: in-progress（実装済み、T1 待ち）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q728（P2、2026-10-05）

## ユーザーの要望（2026-10-04 夜、原文）

「SettingsのSharingタブに、SSHDのON/OFFがあってもいいと思いました。また、あとでクラウドストレージもここで設定できるようにしたいです。」

## 範囲

1. Sharing の頁（今は stub）に SSHD の ON/OFF の switch。状態（動いているか、待ち受けの port、host の鍵の fingerprint は設計で決める）を表示する。
2. 経路: Settings → libkeiland（kl_system_*）→ compositor の拡張 → libkeiland-backend → zedBSD の service の仕組み（WS002 の service の enable・disable と start・stop、`service` の command）。起動時に有効にするかの永続化も service の設定で。app は OS の口を持たない（Guardrail）。
3. 権限: 誰が SSHD を切り替えられるか（管理者だけ、など）を設計で決める。
4. **後で**: クラウドストレージの設定もこの頁に置く（ユーザー）。[WS146](../../ws146/ws.md)（SSH を使う独自のオンラインストレージ）の設定の場所の候補。

## 受け入れ（案）

- switch で sshd が起動・停止し、再起動の後も設定が保たれる。SSH で接続できる・できないを QEMU で確かめる（T1）。
- C の全文の規約、build warning 0、OS の境界の checker。

2026-10-04 夜 ユーザー「SharingにOneDriveを追加したいです。これは独立WSにします。」→ OneDrive は [WS147](../../ws147/ws.md)。この頁はその設定の場所にもなる。

## 設計（2026-10-05、P2。Q1 の回答 (a)(b)(c)）

- **経路**: Settings の Sharing の頁 → libkeiland `kl_system_sharing_*`（KL_VERSION 30、`KL_SYSTEM_HAS_SHARING`）→ compositor の `kl_system_sharing_v1`（**manager の version 7**、request `get_sharing`。WS113 の displays は 8 に繰り下げ（Q1））→ libkeiland-backend `kl_backend_sharing_request`（zedBSD: `sharing-zedbsd.c`）→ 会話の descriptor（sessiond が session に渡したもの）に **`SERVICE sshd on|off|status`** → **sessiond**（root）。Linux・FreeBSD の backend は ENOTSUP の stub（ベータ1 の規則）。
- **sessiond の規則**（Q1 (a)、`userland/desktop/sessiond/service-rules.c`、host で試験）: 固定の表（sshd だけ）、語は on・off・status だけ、空白 1 つで区切り他を拒否（EINVAL → `ERROR`）、利用者は root か wheel の member だけ（sudo と同じ規則、status も。EPERM → `DENIED`）。
- **実行**（`service.c`）: service の command と同じ手順: init に SHOW で service を確かめ、rc.conf の enabled を変え（起動の後も保たれる）、init に RELOAD、START か STOP。答えは `SERVICE available= enabled= running= port=`（port は `/etc/ssh/sshd_config` の最初の `Port`、無ければ 22）。全ての要求を syslog の auth に（`service sshd on by kei: errno=0`、拒否も）。
- **表示**: compositor が backend の状態（最後の答え）に、この利用者が変えられるか（compositor の uid が root か group に wheel）と host 鍵の fingerprint（`/etc/ssh/ssh_host_{ed25519,ecdsa,rsa}_key.pub` の最初の鍵の blob の SHA-256 を base64・padding 無し、`ssh-keygen -l` と同じ。SHA-256 は既存の `userland/base/common/sha256.c`（Q1 (c)））を足して全ての object に送る。object を作った時に最後の状態と done を送り、status を読み直す。
- **Settings**: Sharing の頁（stub だった）に「Remote Login」の card: switch（「Turned on, it also starts with the computer.」、wheel でなければ無効で「Only an administrator (a member of wheel) can change this.」、sshd が無ければ「not installed」）、Status・Port・Host key・「Log in with: ssh 利用者@使用中の network の IPv4」。下に「Cloud storage」（OneDrive と Kei の storage は後、WS146・WS147）。
- ユーザーへの確認（wheel だけに限ること）は Q1 が master の pending に記録。

## 確認（2026-10-05）

- host: `plan/ws089/tests/run-host-service-rules.sh` PASS（root・wheel の on・off・status、wheel でない利用者の拒否（EPERM、status も）、他の service・名前の前後の違い・他の語・大文字・語の過多と不足・空の語・空白 2 つ・先頭の空白・空・path の拒否（EINVAL））。`run-host-sharing.sh` PASS（sessiond の答えの読み取り、DENIED → EPERM・他 → EIO、`ssh-keygen` で作った鍵の fingerprint が `ssh-keygen -l -E sha256` と一致、session が無いと ENOTSUP、on・off・status の行）。`plan/ws131/tests/host-system.sh` PASS（version 7 で sharing が提供され状態が届き、sessiond が無い時の set_ssh の答えが ENOTSUP）。settings-render で Sharing の頁を目視。
- build（warning 0）: zedBSD の wayland・settings・sessiond・files、Linux の Keiland（-Werror）。`keiland-os-boundary/check.sh` PASS。style-check: 新しい file は違反 0、変えた既存の file に新しい違反 0。
- QEMU（T1 に依頼）: `plan/ws089/tests/settings-p025.sh`（graphical の login の image、boot の kei の session で Settings を kei として、状態・switch で off（SSH が切れる間は QMP で PNG）・on・SSH の復帰・log と rc.conf、PNG 2 枚）。
- 未実施: wheel でない利用者の session での guest の確認（規則は host の試験）、実機（UAT）。

## T1-159 の後（2026-10-05、P2）

- FAIL: Settings が起動せず `/tmp/s.log` が無かった（SHARING state・service sshd・compositor の sharing の行は全てこの連鎖）。T1 が使った graphical の login の image（`build-login-image.sh BUILD graphical`、`config-amd64-graphical.mk`）に `su` が無く、試験の `su kei -c '… /bin/settings …'` が失敗していた（試験の前提の誤り。kei の session は HANDOFF で確かめ済み）。
- 直し: `plan/ws089/tests/config-amd64-sharing.mk`（graphical の login の image ＋ su）を足し、作り方を script の注記に書いた: `SETTINGS_CONFIG=plan/ws089/tests/config-amd64-sharing.mk plan/ws089/tests/build-settings-image.sh BUILD` → `files-guest.sh start BUILD/hdd-image.img`。試験は始めに su の有無を確かめ（無ければ理由を出して止まる）、Settings の起動（`/tmp/s.log`）も ok/FAILED で出す。sessiond・compositor・Settings は変えていない。

## ユーザーの承認（2026-10-05 朝、Q1 経由）

- sessiond の wheel の利用者だけが使える `SERVICE sshd on|off|status`（sshd だけに限る）: 承認。

## Q1 の判定（2026-10-05）

T1-162 の settings-p025 PASS（kei の session で Remote Login を off・on、rc.conf と auth の syslog）、sharing-on.png を Q1 が目視（Running・port 22・host key の SHA256・ssh kei@10.0.2.15）。wheel でない利用者の拒否は host の試験。**cleared**。

## T1-169 の後（2026-10-05、P2）

- FreeBSD の backend-test の host-session と host-power が link の誤り（`undefined symbol: kl_backend_sharing_take`）。p025 で session-zedbsd.c が sessiond の SERVICE の答えを sharing-zedbsd.c に渡すようにしたが、ws131 の host の試験の link の一覧に sharing-zedbsd.c と sha256.c が無かった（試験の側の漏れ。製品の build には入っている）。
- 直し: `plan/ws131/tests/host-session.sh` と `host-power.sh`（zedBSD の側）の link に 2 つを足した。host（Linux、gcc と clang）で host-session 31/31・host-power 6/6。session-zedbsd.c を link する試験で、他に sharing の抜けている物は無い（grep）。FreeBSD での再試験は T1。

Q1（2026-10-05）: FreeBSD の host-session・host-power の link の退行（試験の link の一覧の漏れ）は 7813fe3f で直し、T1-171 で PASS。
