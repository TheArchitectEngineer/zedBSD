<!-- awesome-plan project=zedbsd record=ws070p006 -->

# ws070-p006: 共有の file の規約の直し

Phase ID: `ws070-p006`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27、サブエージェント。まず一通り直す方針。残りは下）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが実行）
依存: p005（指摘）、WS071 の menu の変更の merge（済み）

## 範囲

[p005](../phase005/phase.md) が WS071 と共有する file について記録だけした規約（`plan/coding-style.md`）の指摘を直す:
`zdesktop/menu.c`・`menu-shell.c`・`menu.h`、`libzdesktop/menu.c`、`include/libc/zdesktop.h`、`libwayland/menu-protocol.c`。

## 直したもの（2026-09-27）

- 3 節以上（と `&&`・`||` の混ざった）1 行の条件を節ごとの行に（§6）: `zdesktop/menu.c` 3 箇所、`menu-shell.c` 11 箇所（混ざった条件は
  内側も行に）、`libzdesktop/menu.c` 2 箇所。`menu-protocol.c`・`zdesktop.h`・`menu.h` には無かった。
- `zdesktop/menu.c`:
  - 2 つの確保をまとめた検査を 1 つずつに（§9）: `model_add` の label と icon_name、`items_copy`（icon_name を先に NULL にして、途中の
    失敗の解放が正しいまま）。
  - 呼び出しを引数に入れ子にしない（§8）: `model_edit` の `menu_word(…)` を変数（id・parent・before・type・action・first・second）へ、
    `manager_request` の `zwl_find(…, menu_word(…))`、`model_request` の `model_begin`・`model_commit` の serial。
  - 素通しの return（§11）: `model_begin`・`model_commit` は失敗と成功を分けた。`model_fail`（`zwl_error_code` を送る。値は常に EPROTO）を
    `void` にし、18 の呼び出し元を「`model_fail(…); return EPROTO;`」に（失敗の理由を送ったことと返す値が見える形）。
  - `manager_request`・`model_request`・`place_request` の destroy の分岐を検査と成功の段落に分けた。
- 同じ種類の素通しを、この日に足した `zdesktop-files/actions.c` の `fm_action_transfer`（ws071-p010）でも直した。

## 検証（amd64、QEMU の Venus、2026-09-27）

- build warning 0（途中の未使用の変数 5 つは直した）。`plan/ws070/tests/style-compare.sh` で直した file の機械の指摘は前後とも 0。
- menu-p002（protocol の error と libzdesktop の検査）PASS、menu-p003（terminal の System Menu、浮いた bar・docked・submenu・keyboard）PASS、
  files-p009（zdesktop-files の context menu）PASS。
- 画面: `/home/awe/zedBSD-rpi4/build/ws070-shots/p006-20260927-venus-{terminal-submenu,terminal-docked-edit,files-context}.png`。
- boot test はユーザーの指示（2026-09-27）で行わない。実機（i915）: 未実施。

## 残り（締めの照合 ws070-p012 で）

- `menu-shell.c` の長い段落の中の if と呼び出しの comment（`zwl_menu_draw_bar` の label と hit の記録 ほか）は手で読む照合が要る。
  機械で見つかる形（条件・確保・入れ子・素通し）だけをこの Phase で直した。
- `libzdesktop/menu.c`・`libwayland/menu-protocol.c`・`zdesktop.h` の全文の手の照合。
