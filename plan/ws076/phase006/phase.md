<!-- awesome-plan project=zedbsd record=ws076p006 -->

# ws076-p006: cbrt・hypot・erf・erfc・lgamma・tgamma、Bessel、float・long double、legacy.c の削除

Phase ID: `ws076-p006`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

残りの関数（cbrt・hypot は群 B、erf・erfc・tgamma・lgamma は群 C、j0・j1・jn・y0・y1・yn は目標の外）と、その float・long double の
版。旧コード（`legacy.c`）を消し、math.h の全関数が新しい実装にあることを確かめる。

## 受け入れ

1. cbrt・hypot は 1 ulp 未満（正確に表せる結果は正確）、erf・erfc・tgamma・lgamma は 1 ulp 未満（lgamma は負の零点の近くを除く）。
   特殊な値・errno・例外が通る（host と guest）。
2. `legacy.c` が無く、math.h の宣言の全部が定義されている。規約の検査 0、build と boot test。

## 結果（2026-09-28）

cleared。

- `cbrt-hypot.c`: cbrt は v ∈ [1,8) の単位区間の中央の立方根から Newton を 4 回、最後に v − y³ を dd で求めて 1 回の補正（立方は正確）。
  hypot は大きい方の指数で両方を scalbn、x²+y² を dd、dd の sqrt、`__libm_scale`（overflow・subnormal を 1 回の丸めで）。Inf は NaN
  に勝つ。
- `erf.c`: |x| ≤ 1 は erf(x)/x を x² の 13 次の minimax（相対 2^-68）、x ≥ 1/2 の erfc は exp(−x²)·R(x) で R を 15 区間
  （[0.5,1]〜[26,28]、t = (x − c)/h は正確）の minimax（相対 2^-60、次数 10〜16）、exp(−x²) は正確な平方と exp の kernel。多項式の
  最後の段と積は dd。erfc = 1 − erf は |x| < 1/2 だけ、erf = 1 − erfc は |x| > 1 だけ、erfc(−x) = 2 − erfc(x)（打ち消しの無い形）。
  |x| < 2^-900 の erf は 2^100 倍して計算し `__libm_scale` で戻す（subnormal の正しい丸め）。
- 係数は `gen/gen-tables.py` が sollya の fpminimax（Brisebarre-Chevillard の方法）で作る（生成に約 3 分、sollya 8.0 が要る）。
  区間と次数は sollya の探索（相対 2^-60 に届く最小の次数）で決め、script に書いた。
- `gamma.c`: y ≥ 10 は Stirling の級数（12 項、最初の 1/(12y) は dd）、それより小さい x（負は −20 まで）は x + n ≥ 10 まで上げて
  P = x(x+1)…(x+n−1) を dd で割る、−20 以下は反射公式を対数で（sin(πx) は x mod 2 で正確に縮約して dd の sin/cos の kernel、
  trig.c の kernel を `__libm_sin_cos_dd` として共有）。lgamma の 1・2 から 2^-10 以内は Taylor 級数（ζ(k)/k）。x ≥ 2^52 の lgamma は
  2^-128 倍の単位で dd を作って戻す。tgamma の |x| < 2^-54 は 1/x、x > 171.625 は overflow、x < −185 は符号付きの underflow。
  signgam は lgamma が設定する。
- `bessel.c`: |x| ≤ 25 は級数（J は A&S 9.1.10、Y0 は 9.1.13、Y1 は 9.1.11）を dd で（項は整数で正確に割り、調和数も dd）、それより
  大きい x は Hankel の漸近展開（A&S 9.2.5〜9.2.10、項が減らなくなるまで）で位相は正確に縮約した sin・cos から。jn は x より小さい
  次数で上向き、それ以上は Miller の下向きの漸化式（J0 か J1 の零から遠い方で正規化）、小さな x は級数の第 1 項。yn は上向き。
- `support.c`: subnormal の丸めで 0 になった結果が符号を失う誤り（tgamma(−178.23) が +0）を直した。
- long double の wrapper を追加し（modfl が旧コードにしか無かったのを含む）、`src/libc/math/legacy.c` を消した。math.h の宣言の全部
  （`KERN_MATH_UNARY/BINARY` の 3 版と個別の宣言、signgam）が新しい object に定義されていることを nm で確かめた。

## 検証

host（MPFR の参照、`GENERATORS=special plan/tools/libm/host-test.sh --count 200000`）。

| 関数 | 件数 | 最大 ulp | >0.5 | glibc | 関数 | 件数 | 最大 ulp | >0.5 | glibc |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cbrt | 200572 | 0.5000 | 0 | 3.1054 | cbrtf | 100000 | 0.5000 | 0 | 0.5000 |
| hypot | 200015 | 0.5000 | 0 | 1.0000 | hypotf | 100000 | 0.4999 | 0 | 0.4999 |
| erf | 200000 | 0.5130 | 24 | 1.0000 | erff | 100000 | 0.5000 | 0 | 0.5000 |
| erfc | 200006 | 0.5203 | 149 | inf | erfcf | 100000 | 0.5000 | 0 | 0.5000 |
| tgamma | 200023 | 0.6164 | 16 | 5.6302 | tgammaf | 100000 | 0.5000 | 0 | 0.5000 |
| lgamma | 199993 | 0.5044 | 13 | 2.8276 | lgammaf | 99985 | 0.5000 | 0 | 0.5000 |

- 正確であるべき cbrt(n³)・hypot の Pythagoras の 3 つ組（2^±1000 倍と subnormal を含む）・tgamma(n) = (n−1)! は全部一致。特殊な値は
  29 件を足して計 182 件が全部一致。全関数の回帰（各 20000 件）も PASS。
- 目標の外（測定だけ）: lgamma の負の零点の近く（|lgamma| < 0.01、7 件）は最大 0.48 ulp。Bessel は零点の近くで相対誤差が大きい
  （j0 最大 394288 ulp、j1 74200、y0 4884、y1 881、jn 10980、yn 9709。平均 1.0〜5.0 ulp。glibc は同じ入力で j0 395、j1 4324、y0 224、
  y1 417）。絶対誤差は j0・j1・jn が 1.1e-16 以下、y0 が 8.9e-16 以下（y1・yn の大きな絶対誤差は x → 0 の発散する値のもの）。
- guest（QEMU、amd64、各 2000 件）: 群 B・C の全関数が最大 0.5000 ulp 以下、特殊な値 0 failed、PASS。
- 規約: `src/libc/math/` の全 file と試験の runner で `style-check.py` 0。amd64 の build warning 0、i386・arm64 の compile も通る。
- boot test（`build/ws076-boot-p006/login.png`）PASS。

## 移管（Future Work の候補、ID は main が付ける）

- binary128 の long double（arm64・sparcv9）の関数は double の精度（`long-double.c` は double を呼ぶ）。binary128 の精度の libm。
- Bessel 関数の零点の近くの相対精度（零点の近くの展開か、区間ごとの近似）。
