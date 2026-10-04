<!-- awesome-plan project=zedbsd record=ws089-p027 -->
# ws089-p027: About に版の名前（PRETTY_NAME）を出す

Status: planned（2026-10-05 Q1、ws129-p003 の P2 の依頼）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q716 / q716-i01
目安: 0.5h

## 範囲

ws129-p003 で `/etc/os-release` が入った（PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"、U1）。Settings の About の `se_about_read`（about.c）で PRETTY_NAME を読み、hero の card か Version の行に出す。Kernel の行は uname（`zedBSD 1.0.0-beta1+g<hash>`）のまま。file が無い時は今の表示。

## 受け入れ

build の warning 0、host の試験（os-release の有る・無い）。QEMU の About の PNG は T1 にまとめて依頼。

## 所有 path

`userland/desktop/settings/`（about.c）、`plan/ws089/phase027/`。
