<!-- awesome-plan project=zedbsd record=ws110-p003 -->

# ws110-p003: 全文規約と回帰

Status: cleared（2026-10-05 Q1: 全文規約の見直し（新しい file の違反 0、振る舞いは不変）、host-role 16 case PASS、zedBSD と Linux の build warning 0。QEMU は T1-168（roles-guest・files-p002・zdesktop-p095）と T1-168 以降の boot で確認済み）。以前: in-progress（見直しと build は済み。cleared の判定は Q1）
Disposition: normal
Parent: [WS110](../ws.md)
Queue: Q1（2026-10-05）
依存: p001・p002（2026-10-05 Q1: T1-168 で cleared）

## 見直し（2026-10-05、P2）

[全文規約](../../coding-style.md) の §14 で、WS110 の新しい file と変えた行を読んだ。対象は `role.c`・`role.h`・`main.c`（`parse_options`・usage・READY の行）・`zwl.h`・`plan/ws110/tests/host-role.c`・`roles-guest.sh`・`add-testing.py`。

直した物（振る舞いは変えていない）:
- `main.c` の `parse_options` の終わり: `if (role.role == ZWL_ROLE_NORMAL)` が代入の段落の中にあった。空行と comment を置いて独立の段落にした。
- `role.c` の `zwl_role_resolve`: `if (request->timeout)` と `if (request->greeter)` を、それぞれ comment 付きの段落にした。
- `host-role.c`: Boolean を式（`(options & X) != 0U`）で作っていた 7 行を、`if` で 1・0 を返す小さな関数 `option_given` にした（§6）。

確かめ:
- `plan/tools/style-check.py`: role.c・role.h・host-role.c は違反 0。main.c は関数ごとの違反の数が WS110 の前と同じ（増えた関数は無い）。
- `sh plan/ws110/tests/run-host-role.sh`: 16 case、`host-role: PASS`。
- build: Linux の Keiland（`make -f userland/desktop/keiland-linux.mk`）と zedBSD の compositor（`build/ws140-p002/bin/wayland`）は warning 0。`git diff --check` も問題無し。
- 未実施: FreeBSD の build（native の FreeBSD が要る。FreeBSD の backend-test は T1 が流す）。

## 回帰（QEMU、T1）

- T1-168（WS110 の後の main の image）: `roles-guest.sh`（引数なし・`--session` が normal で期限なし、`--testing` 150 秒、`--testing --timeout=20` が自分で終わる、拒む 4 通り）、`files-p002.sh`（置き換えた試験の代表）、`zdesktop-p095.sh`（greeter → `--session` の session → Log Out）が PASS（Q1 の判定）。
- この Phase の直しは段落の形と試験の補助の関数だけで、compositor の振る舞いは変えていない。host の試験で確かめたので、QEMU の再試験は求めない（2026-10-03 の試験の方針）。Q1 が要ると判断すれば、roles-guest.sh を次の T1 の依頼に足す。

## WS の受け入れ（ws.md の T1〜T4）との対応

- T1（引数なしの通常の session が期限なしで動き、Log Out と desktop が成立）: roles-guest（role=normal・期限なし）と zdesktop-p095（Log Out）。
- T2（`--testing` だけが有限の試験。期限・frame・矛盾・順の契約）: host-role の 16 case と roles-guest。
- T3（現役の compositor の試験の mode）: p002 の置き換え（233 file・246 行、dry run 0 件）と files-p002。3 OS で同じ main.c（Linux・FreeBSD の launcher は `--session` の別名のまま）。
- T4（最後の source の全文規約・build・試験・boot）: この Phase の見直しと build。boot は T1-168 以降の T1 の boot-test（Q1 が確かめる）。FreeBSD の build は未実施。

## Q1 の判定（2026-10-05）

全文規約の見直し（新しい file の違反 0、振る舞いは不変）、host-role 16 case PASS、zedBSD と Linux の build warning 0。QEMU は T1-168（roles-guest・files-p002・zdesktop-p095）と T1-168 以降の boot で確認済み。**cleared**。WS110 の受け入れは満たしたので、P2 が完了の書き換え（ws.md・Phase の片付け・roles の試験を tools に移すか）を行う。
