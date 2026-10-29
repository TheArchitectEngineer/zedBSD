<!-- awesome-plan project=zedbsd record=ws250-p005 -->

# ws250-p005: session ごとの lane

Status: planned
Parent: [WS250](../ws.md)
Approval: p002 と同じ。2026-10-29 Q250-5「含める (推し)」。

## 範囲

[design.md](../design.md) 4.9（番号ごとの lane の pool、slot の再利用と CPU の copy の前の待ちだけ node->lock を手放す、exec_lock）。依存: p003。

## 受け入れ

build warning 0、host 試験 PASS。実機の S2（mview の停滞が GR の分だけ）は Q1 の割当で。
