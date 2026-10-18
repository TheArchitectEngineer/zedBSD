<!-- awesome-plan project=zedbsd record=ws260 -->

# WS260 i915 の Gen9 の display（gen9-*.c）を xd と同じく自前で書き直す

- ID: ws260、Master: [master.md](../master.md)、Primary Milestone: MG006
- Status: incomplete（2026-10-18 Q1 作成。p001 を P1、q1252）
- 起点（2026-10-18 ユーザー）:「i915由来のdisplayのソースコードをすべて書き直す件は、どうなったんですか？」→ Q1 の報告（display 12・13 は WS245 で書き直し済み、Gen9 は 2026-10-15 の決定で「ガードレールにそって修正」とし、出典の表記だけを WS258 で直し、写しの点検は未実施）→ ユーザー（クリック）「xd と同じく書き直す」。
- 対象: `src/drivers/gpu/i915/display/gen9-*.c`・`*.h`（22 file、display 9: Skylake・Kaby Lake・Coffee Lake・Comet Lake）と、それだけが使う intel/ の header。
- 手本: [WS245](../ws245/ws.md) の xd（Intel の PRM と hardware の振る舞いから自前で書き、Linux の行ごとの言い換えをしない。値は block ごとに `Values referenced:`、手順は `Procedure referenced:`。PRM に載る名前は使ってよい（2026-10-18 WS244 の決定））。
- 注意: 2026-10-15 のユーザーの決定「Gen9 のディスプレイはガードレールにそって修正してください」を、この決定で書き直しに広げた。WS244 の Gen9 の display の項目（p002 の header は済み）と重ならないように分ける。

## 完了の条件（案、設計で確定）

1. gen9-*.c の code・comment・構造に Linux の写しが無い（設計で決めた点検の方法で確かめる）。
2. Gen9 の機種（SV7 など）で、今と同じに起動・内蔵の画面・backlight・desktop が動く（実機の回帰は T1 か、ユーザーの一覧）。
3. build warning 0、host 試験、規約の全文。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: 今の gen9-*.c の棚卸し（写しの点検）、xd の部品をどこまで共有するか（xd に display 9 を足すか、別の層か）、段の分け方と回帰の方法 | in-progress（q1252、P1） |
