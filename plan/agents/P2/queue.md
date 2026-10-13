# P2 の Queue lane（2026-10-11 第 2 generation、Q1 投入）

承認: 2026-10-11 ユーザー（BUG-288 の直しの担当の推し「P2 を立て直して任せる」に）「これでお願いします。」
Worktree: `/home/awe/zedBSD-worktrees/p2`、branch `agent/p2`。agent type phase-runner（high）。

| Queue | 対象 | State |
| --- | --- | --- |
| q938 | [BUG-288](../../bugs/BUG-288.md)（Pi 4 の reboot の後に戻らない・書いた file が消える・read-only） | active |
| q939 | [WS208](../../ws208/ws.md) arm64 の HAL の SMP（2026-10-11 ユーザー「arm64 の HAL の SMP（WS208）はP2にアサインします。現在の処理の後です。」） | reserved（q938 の後） |

---

# P2 の Queue lane（2026-10-11 generation、Q1 投入）

承認: 2026-10-11 ユーザー（このセッション、N=3＋T1 の指示）「P2はバグ修正です。・BUG-263、BUG-274、その他バグがでたら対処。バグが出るまでいったんP2を畳んでおき、必要なときに再度立てる。」
Worktree: `/home/awe/zedBSD-worktrees/p2`、branch `agent/p2`、base main fc3d317ca。agent type phase-runner（high）。

| Queue | 対象 | State |
| --- | --- | --- |
| q934 | [BUG-263](../../bugs/BUG-263.md)（AF_UNIX の datagram で相手が閉じた後 recv が待ち続ける） | finished（kernel の直しは q914 で main、POSIX-R2 の case を追加 c695cc34d、test-wait T1-530） |
| q935 | [BUG-274](../../bugs/BUG-274.md)（`ps -o args` が argv[0] だけ） | finished（直しは main、host PASS、guest の script を追加 048dc8c4b。guest の確かめはユーザーの判断待ち） |

両方の後は通常のラップアップで終了（次の Bug が出たら Q1 が新しい generation を立てる）。

---

以前の記録（履歴）:

# P2 Queue lane

| Queue / attempt | Phase | Scope | Approval | Timebox | State |
| --- | --- | --- | --- | --- | --- |
| q591 / q591-i01 | [ws099-p020](../../ws099/phase020/phase.md) | BUG-125 の原因特定と compositor の修正 | 2026-10-02 user「作業を開始しましょう。」 | 4h | finished / uncleared |

| q609 / q609-i01 | [ws099-p023](../../ws099/phase023/phase.md) | BUG-136/137 と直す前からの C 基準の失敗 | user 2026-10-02 | 4h | finished / cleared |
| q618 / q618-i01 | [ws114-p008](../../ws114/phase008/phase.md) | KDE の server decoration、既定 SSD | user 2026-10-03 | 4h | finished / cleared |
| q620 / q620-i01 | [ws099-p021](../../ws099/phase021/phase.md) | C2 の geometry・BUG-127 | 自走 | 3h | finished / uncleared |
| q622 / q622-i01 | [ws094-p014](../../ws094/phase014/phase.md) | 規約の指摘の直し | 自走 | 2h | finished / cleared |
| q625 / q625-i01 | [ws099-p024](../../ws099/phase024/phase.md) | BUG-147 の試験の頑健化 | 自走 | 3h | in-progress |

Next（予約）: debug を続ける（BUG-147 の後: 優先度の高い bug。ws089-p019 と ws113 は WS131 の後に組み直す）

## Merge requests
| P2-001 | q591 | f1af6cc7f..8bc057b36（base 901037f9f） | wayland/{preferences,seat,toplevel,zwl.h}・popup-probe・plan/ws099・zdesktop-p076.sh・BUG-125 | integrated e90d816c2 |
| P2-002 | q609 | d8943fde2・fb32c0a0b・2624e7365・e20409bbd（前回 8bc057b36） | xserver・notes・terminal・popup-probe・wlshm・vmunix.mk・plan/ws099・BUG-136/137 | integrated d5e5cfa6a |
| P2-003 | q618 途中 | 0ce650b2a | userland/tests/wlshm/Makefile.linux・freebsd（p023 の link の回帰の修正） | integrated 55383d678 |
| P2-004 | q618 | 9f274fc64・fc3c52383・0ea947662・df0b57acb | wayland/{decoration,protocol,objects,zwl.h,extras.h}・plan/ws114 | integrated 8140d36a9 |
| P2-005 | q620 | f0ee0d951（前回 df0b57acb） | plan/ws099/phase021・BUG-127 | integrated eb7235789 |
| P2-006 | q622 | 5ca38bb84・71f726edc・af3b29c53（前回 f0ee0d951） | wayland/{display,menu,desktop}・files/{thumb,ui-context}・imageview/image.c・plan/ws094 | integrated 0f6480f77 |

2026-10-03 / N=3: user「BUG-150は今実行してOKです。FreeBSDホストを空けたので使ってください。Emacsは入ってます。」→ P2 generation4 を q636（ws128-p010、BUG-150）で起動。5320（FreeBSD）は P2 が使い、P3 は FreeBSD の確認を済ませた。
| P2-merge q863 | q863-i01 | c98a659ec・52a2e4007（base 777137731） | wayland/touchpad.{c,h}・plan/ws159/tests/host-touchpad.c・plan/ws183 | integrated b776026ca（2026-10-08、実機の確認待ち） |
| P2-merge q864 p001 | q864-i01 | 77fe44dc8（merge 0f16839ac、base b776026ca） | wayland/{lock-clock.{c,h},glass.{c,h},greeter.c,Makefile*}・plan/ws187 | integrated 07096625d（2026-10-08、QEMU の PNG は p003 の後） |
| P2-merge q864 p002 | q864-i01 | dfeea795f（base 0f16839ac） | wayland/{lock-swipe.{c,h},greeter.c,seat.c,input.c,kwl.h,Makefile*}・locale/ja/wayland.tr・plan/ws187 | integrated dd562b688（2026-10-08、猶予 300 秒は案） |
| P2-merge q864 p003 | q864-i01 | da62a2f0d・337b7d4e2・6a1cf2b6c（base dfeea795f） | wayland/greeter.c・locale/ja/wayland.tr・plan/tools/aat/scenarios/helpers_desktop.py・tests/scenarios/desktop/lock/・plan/ws187 | integrated ef0ff8a66（2026-10-08、T1 の試験待ち） |
| P2-merge q864 sleep | q864-i01 | 880ce4cbf | wayland/{sleep.c,lock-swipe.c}・plan/ws187 | integrated（2026-10-08 7bbfb70c8 の前の merge） |

2026-10-13 夜 / P2 の新しい世代（N=3）: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。N=3で、P2を追加し、実装を担当してもらいます。」→ ベータ2 の残りの実装（plan/beta2.md の「ベータ2 の残りの工数」）を順に。worktree /home/awe/zedBSD-worktrees/p2、branch agent/p2-ws215（main 58036a90b に合わせた）。
- q949: [WS157](../../ws157/ws.md) p015（thumbnail の jail）・p017（libjpeg の縮小 decode）。State: active。
- 予約 q950: [WS172](../../ws172/ws.md) p004（security chip の調べ）・p004b（/dev/security0 と TPM の設計）。
- 予約 q951: [WS046](../../ws046/ws.md) の残り（p014 の make check の判断、p007）。
- 予約の続き（Q1 が順を決める）: WS004 p039、WS102 p011、WS145 p005、WS161 の build、WS156 p006、WS051、WS081、WS090 p026、WS033 p001。
- q949 結果（2026-10-13 夜）: WS157 p015（実装は 10-11 の P1）・p017（libjpeg-compat の 1/2・1/4・1/8 の縮小 decode と preview）7278694ff → main 523370caf（main で photos・keiland-preview の build warning 0）。test-wait（T1-584）。
- q950: WS172 p004・p004b はユーザーの判断 S1〜S13 と 5330 の ACPI TPM2 の読み（Linux で起動）待ちで止めた（e06b7d41d、phase004/phase.md の記録）。
- q951: WS046 の残りに進む。
- q951 結果（2026-10-13 夜）: WS046 p007 は T1-050 の証拠で Q1 が cleared、p014 は「make check の status 0」をユーザーの判断に（(a) expat の runtests 4932/4932 で満たしたとみなす が推し）。記録 20a4dab66 → main 0d4ac1421。
- 予約（2026-10-13 夜 Q1）: q952 [WS102](../../ws102/ws.md) p011（画面 keyboard の frame の p95 を目標まで）→ q953 [WS145](../../ws145/ws.md) p005（Linux・FreeBSD の build・install と guest）→ q954 [WS161](../../ws161/ws.md) p006 の残り（Linux・FreeBSD の build）。
- q952〜q954・q960 結果（2026-10-13 夜）: WS102 p011 の計測 030e77da1・WS145 p005 a07a5b174・WS161 p006 c2fcd7d3d（ユーザーの指示で os-freebsd.c と fidoctl の Makefile は main から外した、Linux・FreeBSD の FIDO2 はベータ3）・WS172 p004b の設計 71d457c16 → main（1d9880aa0）。p004b は design-reviewer。

2026-10-13 夜 / q961: Linux・FreeBSD の Keiland に passkey の command を入れる（FIDO2 の backend 無し）。ユーザー「あ、Linux/FreeBSDでは、FIDO2バックエンドがないだけで、passkeyコマンドは入れてください。」。State: active。
- q961 結果（2026-10-13 夜）: WS172 p011 passkey の Linux・FreeBSD（/opt/keiland/sbin/passkey、/opt/keiland/etc/passkey、FIDO2 は fail no-key、FreeBSD の shadow の橋）e7e2c7ce2・ca75d0aa7 → main、test-wait（T1-589）。
- 予約（2026-10-13 夜 Q1、ベータ2 の残り）: q962 [WS143](../../ws143/ws.md) の残り（5330 の firmware の load と scan の後の直しは実機待ちなので、Linux の BlueZ の backend p007 を先に）→ q963 [WS004](../../ws004/ws.md) p039（WLAN の driver の共通化、「抽出しない」の結論でもよい）→ q964 小さな照合（[WS089](../../ws089/ws.md)・[WS193](../../ws193/ws.md)・[WS191](../../ws191/ws.md)）。
- q962〜q964 結果（2026-10-13 夜〜10-14）: WS143 p007 の設計（bd4f767a6・db09ac249、design-reviewer 中）、WS004 p039 は抽出しない（7ccc03c62、Q1 が cleared）、WS089・WS191・WS193 の照合（26488543d、Q1 が判定）。
- 予約: q965 [BUG-301](../../bugs/BUG-301.md)（RTL8822BU の CCMP の header の長さ、確かめて要れば直す）→ q966 [WS171](../../ws171/ws.md)（hal.h の契約の comment、優先度低め、やることが無い時）。p007 の review が返ったら p007 の実装を先に。
