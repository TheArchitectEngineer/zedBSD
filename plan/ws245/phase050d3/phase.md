<!-- awesome-plan project=zedbsd record=ws245-p050d3 -->

# ws245-p050d3: plane・flip・vblank を xd に

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 3 段目。p050d2 の後）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p033・p049・p050 の行、§2.10、§3.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 5

## 範囲

- plane の update（`plane.c` の書き手と plane state）、flip の arm/poll/wait/settle と SURFLIVE の読み、vblank の event と evasion（`vblank.c`、`modeset.c` の flip の本体）を xd-pipe の plane と xd-screen の flip（p049 の判断）に。frame counter の読み（own の `control.c` の `drv_i915_pipe_frame_read`）も xd-pipe に。
- 殻が更新する旧の写し（`plane_armed`、flip の field）を減らす。

## 受け入れ

- xd の host 試験（plane の語の列、flip の状態、vblank の event の待ちと evasion）、build 6 組 warning 0、旧 host 試験の前後の比べ、R11 の数。5330 は全部の段の後に 1 回（flip の性能 `perf`、SURFLIVE/CRC の進み）。
