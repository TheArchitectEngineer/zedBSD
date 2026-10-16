<!-- awesome-plan project=zedbsd record=ws245-p050c -->

# ws245-p050c: HDMI と DP-TC の step の本体

Status: planned（2026-10-16 Q1 の判断で p050 を p050a〜d に分けた。design の順のまま p039 の前に行う）
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

p050b と同じ形で、HDMI（combo の DDI B、GMBUS の DDC、SCDC）と DP-TC（Type-C の DP、TC の link の参照は旧 tc.c の殻の hook: p052 まで）の step の中身を xd の部品の直の呼びに。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。
