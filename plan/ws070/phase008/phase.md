<!-- awesome-plan project=zedbsd record=ws070p008 -->

# ws070-p008: titlebar の protocol と model

Phase ID: `ws070-p008`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが計画・実行。main の Queue への反映は main の session）

## 範囲

[titlebar-design.md](../titlebar-design.md) §2〜§5、§12: `zed_titlebar_manager_v1`・`zed_titlebar_v1` の client 側（libwayland）と server 側
（zdesktop の model・transaction・error・寿命・event の送り手・log）、libzdesktop の `zdesktop_titlebar_*`（鏡と局所の検査）、
titlebar-probe。描画は変えない（p010）。

## 受け入れ

1. titlebar-probe の server の case（error の interface と code）と library の case が Venus（QEMU）で全部 ok。
2. 正しい model（controls・tabs・mode の切替）が commit の log に出る。MENU mode（zdesktop-terminal の menu）は変わらない。
3. 新しい file は `style-check.py` 0、変えた既存の file は悪化させない。warning 0。

## 結果（2026-09-27）

cleared。

- 実装: libwayland の `titlebar-protocol.c`（新規: 2 interface の表、16 の request の wrapper、7 event の typed dispatch
  `wlc_titlebar_dispatch`）と非公開 header `zed-titlebar-v1-client-protocol.h`、`event.c`（dispatch の分岐）、`internal.h`、
  `exports.map`、`Makefile`。zdesktop の `titlebar.c`・`titlebar.h`（新規: request、controls 64・tabs 128・段 32・文字列 1023 の
  model、begin の深い写しと commit の差し替え、error 6 種、寿命の hook、event の送り手 `zwl_titlebar_send_*`、log
  `ZWL TITLEBAR create|commit|activate|text|tab`）、`zwl.h`（object の種類 2 つ、`titlebar_model`・`titlebar` の field）、
  `protocol.c`（global 16 番 `zed_titlebar_manager_v1`、dispatch）、`objects.c`（退場の hook）、`Makefile`。libzdesktop の
  `titlebar.c`（新規: `zdesktop_titlebar_create`（窓ごとに manager を探して bind、get_titlebar の後に manager を捨てる、窓の queue）、
  begin・commit・mode・control・tab・focus の検査つきの呼び出し、listener の中継）、`zdesktop.h`（API と定数、`ZDESKTOP_VERSION` 4）、
  `exports.map`、`Makefile`。`userland/base/tests/titlebar-probe`（新規）、`platform/amd64/vmunix.mk`（probe の link の規則）、
  `plan/ws070/tests/config-amd64-menu.mk`（image に probe）。
- 試験: `plan/ws070/tests/titlebar-p008.sh`（新規）。
- **QEMU（Venus）**: `titlebar-p008.sh` **PASS**（server の 12 case の error（outside 2、zero-id 0、duplicate 0、role 1、mode 1、
  serial 4、nested 3、breadcrumb-role 1、value-role 1、tab-flags 1、focus-uncommitted 0、exists は manager の 0）、good の 3 つの commit
  （mode 1・controls 6・tabs 1、mode 2、mode 1）、library の 20 の呼び出しの答えと round trip、その後の terminal の menu）。
  回帰 WS070 `menu-p002.sh`・`menu-p003.sh` **PASS**（画面 build/ws070-p008-reg/menu-p003/ の floating・docked-edit 等を見た、MENU は
  変わらない）、WS071 `files-p008.sh`・`files-p012.sh` **PASS**。
- build warning 0（変えた file。image の build の他の warning は既存の package のもの）、`style-check.py` 0（新しい file）、
  既存の file は `style-compare.sh` で悪化なし（protocol.c 5→5、event.c 3→3 等）。
- 実機（i915）: 未実施（描画を変えない Phase）。
- 制限・注記: 窓（toplevel）が先に消えた titlebar への request は受けて model だけ変わる（描かれない。design §2.3 の「無視」と
  同じ効果）。
