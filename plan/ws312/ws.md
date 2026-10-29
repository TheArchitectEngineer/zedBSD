<!-- awesome-plan project=zedbsd record=ws312 -->

# WS312: IPv6 の fragment の受け取りと再組立て

Status: incomplete
Primary Milestone: MG005
Resume point: p004（規約の全文の見直し）。担当は P6（q1672）。

## ユーザーの指示（原文、2026-10-29）

「IPv6のfragment対応は、今P6を立ててやっていいです。」

（前の流れ: WS221 p011 で、`ipv6_extensions` が fragment の header を持つ packet を捨てる事が分かり、Future Work の F-WS221-ipv6-frag に置いた。TCP には効かないが、大きい UDP（DNS など）が fragment で来ると落ちる。）

## 目標（Q1 の解釈）

- 受けた IPv6 の fragment（RFC 8200 §4.5）を再組立てして上の層（UDP・ICMPv6・TCP）に渡す。重なる fragment の扱いは RFC 5722、atomic fragment は RFC 6946、時間切れは 60 秒と ICMPv6 Time Exceeded（code 1）。資源の上限（組立て中の数・byte）を持ち、溢れたら古い物を捨てる。
- 送りの fragment（自分が分けて送る）は範囲に入れない（TCP は PMTU で分けない、UDP の大きい送りが要る時は別に）。
- IPv4 の fragment の再組立ても今は無い（`ipv4.c` が捨てる）が、ユーザーの指示は IPv6 なので範囲の外（Q1 がユーザーに伝える）。
- 試験: host の単体の試験（順・逆順・重なり・欠け・時間切れ・上限）と QEMU の guest で大きい UDP・ping6 の fragment の受け取り（相手は host の Linux）。
- P6 の通常の「実装だけ・merge しない」とは違い、この WS はユーザーの今回の指示で、試験まで行い merge する（Q1 の解釈。2026-10-29）。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（RFC 8200・5722・6946、今の ipv6.c の extension の walk、資源の上限、時間切れの timer の口） | cleared | — |
| [p002](phase002/phase.md) | 再組立ての実装と host の単体の試験 | cleared | p001 |
| [p003](phase003/phase.md) | QEMU の試験（大きい UDP・ping6 の fragment、手で作る fragment と walk との結合） | cleared | p002 |
| p004 | 規約の全文の見直し | in-progress | p002・p003 |
