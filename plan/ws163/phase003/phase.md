<!-- awesome-plan project=zedbsd record=ws163-p003 -->

# ws163-p003: Settings の Users の PIN、greeter（G1 による）

Phase ID: `ws163-p003`
Parent: [WS163](../ws.md)
Status: in-progress（2026-10-05 P1 generation19 q769。Settings の部分は実装と host 試験まで。greeter は G1 待ち）
Phase disposition: normal
Queue: q769（Settings の部分）

## 範囲

[p001 §9.3](../phase001/phase.md): Settings の Users の頁から PIN を設定・変更・削除する（今の password で確かめる）。greeter の PIN は G1（§9.6）の答えの後。

## 実装（d9ab018d）

- 協定（`userland/desktop/keiland/kl-system-protocol.h`）: `kl_system_manager_v1` を version 10 に、`kl_system_account_v1` に
  `request 3 set_pin(uint request, string current, string pin)`（since 10、pin が空なら削除）、能力の bit `KL_SYSTEM_CAPABILITY_PIN`（session manager が
  在る時）。
- compositor（`userland/desktop/wayland/system.c`・`handoff.c`）: set_pin は password と PIN を写して request の byte を消し、6 桁・password が 6 桁で
  ない・session manager が在ることを確かめ、**sessiond の今の `UNLOCK`**（`kl_backend_session_unlock`）で password を確かめる。答えは `handoff.c` が
  lock の画面より先に `zwl_system_pin_answer` に渡し、OK なら `zwl_pin_store_set`（空なら `remove`）、結果を返す。一度に 1 つ（busy）。
- libkeiland: `kl_system_account_set_pin`（`keiland.h` の KL_VERSION 34、`KL_SYSTEM_HAS_PIN`、`exports.map`、`system-protocol.c` の table を version 10）。
- Settings（`userland/desktop/settings/page-users-pin.c` 新規、`page-users.c`・`settings.h`・`page-users-admin.c`、Makefile 3 つ）: 
  password の card の下に PIN の card。PIN の有無（自分で `~/.config/keiland/pin` を見る）、今の password・新しい PIN・もう一度（PIN の欄は数字
  6 桁だけ受ける）、「Set Up PIN」（有れば「Change PIN」）と「Remove PIN」、答えの行。keyboard の持ち主を `SE_USERS_KEYBOARD_*`（password・管理・PIN）
  にし、管理の card の「keyboard が 0 でない」の判定を「管理の card の」に直した（PIN の card の欄に打った文字を管理の card が取らないように）。
  password の card の欄の枠も、その card が keyboard を持つ時だけ光るようにした。

## 確認

- host: `plan/ws131/tests/host-system.sh` PASS。set_pin を足した（設定・password が違う＝EPERM・5 桁＝EINVAL・password が 6 桁＝EINVAL・削除、file の中を
  `zwl_pin_store_check` で確かめる）。能力の期待に `KL_SYSTEM_HAS_PIN` を足した。fake の sessiond は次の pass で答える。
- build（warning 0）: zedBSD と Linux（p002 と同じ）。
- QEMU: Settings の画面の操作（PIN の card は Users の頁の下の方で、欄の位置を試験が知る道が無い）は自動の試験に入れていない。UAT で見る。
  lock の画面の側は p002 の `pin-lock-guest.sh`。

## 残り

- greeter（G1 の答えの後）。
- Settings の PIN の card の見た目と操作（UAT）。
