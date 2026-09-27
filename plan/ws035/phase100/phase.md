<!-- awesome-plan project=zedbsd record=ws035p100 -->

# ws035-p100: primary selection（zwp_primary_selection_v1）と zdesktop-terminal の中 button の paste

Phase ID: `ws035-p100`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「the terminal PRIMARY selection protocol」。p093 の残り）

## 実装（2026-09-28）

- zdesktop `primary.c`（新）: `zwp_primary_selection_device_manager_v1` version 1（global 18）と source・device・offer。
  clipboard（data.c）と同じ形の別の selection（`server->primary`・`primary_client`）: source の set で置き換えた source に
  cancelled、keyboard の client（focus の変化でも）に新しい offer と selection、offer の receive で source の client に send
  （fd を渡す）、source が消えたら selection を空に。drag and drop は無い。data.c の文字列の読み書きを
  `zwl_data_emit_string`・`zwl_data_read_string` として共有。
- libwayland `primary-selection-protocol.c`（新）と公開 header `wayland/primary-selection-unstable-v1-client-protocol.h`
  （と `<primary-selection-unstable-v1-client-protocol.h>`）: 4 つの interface の記述と要求、listener は generic の dispatch。
  exports に `zwp_primary_selection_*`。`API-PROVENANCE.md` に照合した記述（wayland-protocols 1.44 の
  primary-selection-unstable-v1.xml、SHA256 `d568482b…f52c`）と MIT の通知。
- zdesktop-terminal `primary.c`（新）: pointer で選んだ範囲（語・行・drag）がその都度 primary selection（UTF-8 と plain の
  source、`ZTERM PRIMARY set`）。中 button（BTN_MIDDLE）の press で primary selection を shell に paste（他の client のは pipe、
  自分のはそのまま。改行は Enter の CR）。
- 試験の道具: `qmp-pointer.py` に `middle-down`・`middle-up`。p079 の試験の最後の click の場所を p092 の置き方に合わせて
  直した（新しい probe c が terminal の左上に重なり、click が c に当たっていた。この Phase の変更とは無関係）。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p100.sh`（新）PASS: terminal 1 の double click の `beta-gamma` が primary selection（types=2）、
  後から起こした terminal 2 は focus で offer（text=1）を受け、中 click で terminal 1 から pipe で受けて shell に
  `beta-gamma`（`p100-20260928-pasted.png`）、terminal 1 の中 click は自分の選択を paste（`-own.png`）。
- 回帰 PASS: zdesktop-p093（terminal の選択・drag）、zdesktop-p079（clipboard、上の試験の直しの後）、zdesktop-p087
  （X11 と Wayland の clipboard の橋）。
- 規約: 新しい 4 file（zdesktop・terminal の primary.c、libwayland の primary-selection-protocol.c、header）の style-check 0、
  変えた既存の file は増えていない。build warning 0。
- 実機: 未実施。

## 残り

- X11 の PRIMARY（zdesktop-x11server の橋）と zterm の中 button。→ [p103](../phase103/phase.md) で済み。
- 選択を消したとき（click）に NULL の source を set しない（X と同じく最後の選択が残る）。
