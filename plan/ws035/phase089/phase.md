<!-- awesome-plan project=zedbsd record=ws035p089 -->

# ws035-p089: libwayland（client）の zombie の扱いと、DnD の選ばれた action を offer へ

Phase ID: `ws035-p089`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「libwayland zombie handling ... then send the chosen DnD action to the offer as the
protocol intends and re-run p084/p088」。fg010: 2026-10-17 の OSC Tokyo Fall の Wayland desktop の demo）

## 範囲

p088 で分かった: zedBSD の libwayland-client は、client が壊した server 側の object（id ≥ 0xff000000、offer 等）を wl_proxy_destroy で
map から直ちに外すので、server がその destroy を受ける前に送った event が「知らない object」になり protocol error（EPROTO、切断）。
upstream の libwayland はその id を zombie として map に残し、event を捨て（fd は閉じる）、server が同じ id で新しい object を
作るまで保つ。

1. libwayland: server 側の id の proxy を destroy しても map に残す（`destroyed` の zombie）。event はその proxy の既存の
   扱い（`wlc_wire_parse` の `proxy->destroyed` で event を捨て、`wlc_event_destroy` が fd を閉じる。object の引数としては
   NULL）で落ちる。server が同じ id で object を作るとき（new_id）、zombie を map から外して置き換える。
2. zdesktop: ask の drop の後に選ばれた action を offer へも `wl_data_offer.action` で送る（p088 で外していた）。

## 実装（2026-09-28）

- `userland/base/libwayland/proxy.c`: `wlc_proxy_destroy` は server 側の id を map から外さない。`wlc_proxy_insert_server` は
  既存の proxy が zombie なら外して新しい object を入れる（生きている proxy と重なるのは従来どおり EEXIST）。connection の
  終わりに map の全部を解放するのは従来どおり（`client.c`）。
- `userland/base/zdesktop/data.c`: `offer_set_actions` の ask の後の分岐で `OFFER_ACTION` も送る。
- 試験 `plan/ws035/tests/p075/`（libwayland の host 試験）に zombie の段: 試験の protocol の child に fd の event `data`
  （pong の後に server が pipe を送る）。client は 3 回目の child に ping と destroy を続けて送り、roundtrip の後で接続が保たれ・
  pong と data が捨てられ・fd の数が増えないことを確かめ、4 回目の child（同じ id を再利用）の ping・pong・data が動くことを確かめる。
  server の `children_destroyed` は 4。

## 検証（2026-09-28）

- host: `plan/ws035/tests/p075/run-host.sh build/ws035-p089-host` PASS（`zombie events keep the connection`・`zombie events
  dropped`・`zombie fd closed`・`reused=1`・`child after zombie`、CLIENT DONE failures=0、SERVER DONE children_destroyed=4）。
  前の `proxy.c` で同じ試験（`LIBWAYLAND_ROOT`）は FAIL（接続を失い、fd が 1 つ残る）: 試験が不具合を捉えることを確かめた。
- Venus の guest（amd64）: zdesktop-p088 PASS（`drag chosen client=2 action=1` の後も zdesktop-files が生きて 2 段目・3 段目が
  通る。p088 の最初の実行で落ちた経路）、zdesktop-p084 PASS、zdesktop-p087 PASS（x11server の data offer）。
- 規約: `proxy.c` の style-check は前と同数（37）、試験の `client.c`・`server.c` は 0、`data.c` 0。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p089-20260928-venus-ask.png`（offer へ action を送る版の ask の menu）、
  `p089-20260928-venus-term-drop.png`。
- 実機（i915）: 未実施。boot test: ユーザーの指示で無し。

## 残り

- zombie の proxy は server が id を再利用するまで memory を持つ（upstream と同じ）。zdesktop は offer の id を再利用するので増えない。
