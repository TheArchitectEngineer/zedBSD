<!-- awesome-plan project=zedbsd record=ws290-p016 -->

# ws290-p016 WaitEvents の保留（R10 の (b)）

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §11.4 の「WaitEvents（R10・Q13）」の (b) と「WaitEvents の v1（版 4.13）」。
- 状態: planned
- 依存: p009（WaitEvents の (a) と kernel の待ちの語）、p012（表示の ring の使い方が決まった後）

## 範囲

同じ submission の前に SetEvent の無い WaitEvents（host の vkSetEvent や前の submission の GPU の SetEvent を待つ物）を、v1 の `ENOTSUP` の代わりに実装する: submit をその WaitEvents で切り、残りとその client の後の submission 全部（submission の順を保つ）を kernel が保留し、event の語が 1 になった時（host の vkSetEvent、前の submission の SetEvent の fence の IRQ ごとの読み）に続きを置く。保留は reset で DEAD になった session と executor の close で ERROR で完了する。ring に client が書ける語を待つ `WAIT_FENCE` を置かない（p009 の review の H2）。

## 受け入れ

- 場面 PASS: host の SetEvent で続く、前の submission の SetEvent で続く、保留の間の後の submission の順、ResetEvent・close・reset で保留が ERROR、保留の間も他の session と表示が止まらない。
- host 試験と amd64・riscv64 の build（warning 0）。実機は p014。
- 見積り: 10 LW。

## 記録

- 2026-10-28 P3（q1644-i01）: ユーザーの答え（2026-10-28、Q1 経由のクリック）「WS290 の中で後の Phase」で立てた。v1（p009）は `ENOTSUP` で断る。
