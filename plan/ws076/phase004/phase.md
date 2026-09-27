<!-- awesome-plan project=zedbsd record=ws076p004 -->

# ws076-p004: pow

Phase ID: `ws076-p004`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

pow・powf（powl は double と同じ）。[design.md](../design.md) §5 の方式、C11 F.10.4.4 の特殊な場合。

## 受け入れ

1. 最大誤差 1 ulp 未満、正確に表せる結果（整数の冪、2^n、10^n、平方）が正確、`14 ** 2` が 196、特殊な値・errno・例外が通る（host と
   guest）。
2. 規約の検査 0、build と boot test。

## 結果（2026-09-28）

cleared。`src/libc/math/pow.c`。

- y·log|x| を dd で（`__libm_log_dd` は 2^-78、`libm_two_product` で y との積）、`__libm_exp_dd` に dd の引数のまま渡し、
  `__libm_scale` で 1 回だけ丸める（全体で約 2^-68）。このため正確に表せる結果は正確に出る。
- 2 の冪の底と整数の y は scalbn で直接（正確な subnormal の結果に余計な underflow を立てない）。|y| ≥ 2^64 は overflow か underflow。
- 負の底は y が整数のときだけ（偶奇を encoding から判定、|y| ≥ 2^53 は偶数）、奇数なら符号を付ける。0・Inf・NaN は Annex F の表の
  とおり（pow(NaN, 0) = 1、pow(1, NaN) = 1、pow(−1, ±Inf) = 1 を含む）。
- powf は double から 1 回、`__libm_narrow` で float の範囲を報告。legacy.c から旧 pow を消した。

## 検証

| 関数 | 件数（host） | 最大 ulp | 平均 ulp | >0.5 ulp | host（glibc 2.41）の最大 |
| --- | --- | --- | --- | --- | --- |
| pow | 208139 | 0.5000 | 0.2312 | 0 | 0.5050 |
| powf | 200000 | 0.5000 | 0.2317 | 0 | 0.5528 |

- 入力: 結果が範囲に入る無作為の x と y、1 の近くの x と大きな y、整数の y と負の底、小さな y、[0,10]² の一様。正確であるべき
  8139 件（2〜199 の底の整数の冪で 2^53 未満のもの、その負の底、10^n〔n ≤ 22〕、2^n と 0.5^−n〔n = −1074〜1023〕、平方根）は全部
  一致。特殊な値 30 件（`pow(14, 2) = 196`、`pow(−3, 3) = −27`、pow(10, 400) の overflow、pow(2, −1074) の underflow の無い
  subnormal を含む）は全部一致。全関数の回帰（各 20000 件）も PASS。
- guest（QEMU、amd64、image の libc.so）: pow 10139 件・powf 2000 件の最大 0.5000 ulp、特殊な値 0 failed、PASS。
- 規約 0、amd64 の build warning 0、boot test（`build/ws076-boot-p004/login.png`）PASS。
- ブラウザの operators.js（`14 ** 2`）の guest での確認は p007（ブラウザの image）で行う。未実施。
- 道具の修正: float の結果が float の範囲を越える参照は ±Inf にする（powf の 14677 件の見かけの失敗の原因）。
