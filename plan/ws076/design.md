<!-- awesome-plan project=zedbsd record=ws076-design -->

# WS076 の設計: libc の libm の自前の書き直し

2026-09-28、ws076-p001。親: [WS076](ws.md)。ユーザーの決定:「libmは独自に書いてください。libcのツリーに入れてください。」
→ FreeBSD msun・musl・glibc・fdlibm・CORE-MATH ほか既存の libm の source を読まず、写さず、取り込まない。公開された算法と論文
（§9）と、自分の host の script で作る係数・表（§7）で書く。置き場は `src/libc/math/`。

## 1. 現状と前提（調べた事実）

- `src/libc/math.c`（413 行）が全部。pow は `exp(y*log(x))`、fmod は `x - trunc(x/y)*y`（大きな x で値が誤り）、exp・log・三角は
  Taylor 級数を double で足すだけ（最後の桁が違う）、sqrt は Newton 8 回（正しく丸めない）、fma は融合しない、erf は
  Abramowitz-Stegun 7.1.26（約 1.5e-7）、tgamma は Lanczos、Bessel は冪級数。float・long double の版は double を呼んで cast。
- 同じ math.c を 6 箇所で compile している: `src/libc/softfloat.mk`（i386 の kernel 側の object と host 試験の rule）、
  `platform/{amd64,pcat,pc98,arm64,sparcv9}/vmunix.mk`（動的な `libc.so`）、`toolchain/llvm/sysroot.mk`（sysroot の
  `libzedbsd-compiler-rt.a`、package が link する）。`tests/math-host-test.c` は softfloat.mk に rule だけ残っていて file は無い。
- 算術の model（全 platform）: double は IEEE binary64 で最近接の丸め。amd64 は SSE2（`-march=x86-64`、FMA 無し、x87 の余分な
  精度無し）、i386・pc98 は `-msoft-float`（compiler-runtime の soft-float、binary64）、arm64・sparcv9 は hardware。
- **arm64 の clang は既定で `a*b+c` を fmadd に縮約する**（`-ffp-contract=on`、確かめた）。Dekker の積の誤差の式が壊れるので、
  全 file で `#pragma STDC FP_CONTRACT OFF`（確かめた: pragma で fmul と fadd に分かれる）。arm64 は `__FP_FAST_FMA` を定義する。
- fenv（`src/libc/fenv.c`）は software の cell で、丸めは `FE_TONEAREST` だけ（`fesetround` は他を拒む）。amd64 の hardware の
  flag は `fetestexcept` に見えない。→ libm は **例外を `feraiseexcept` で明示して立てる**。丸めの mode は最近接だけを前提にする。
- long double: amd64・i386（`-mlong-double-64`）は binary64（`__LDBL_MANT_DIG__` 53、確かめた）で double と同じ。arm64・sparcv9 は
  binary128（113）。
- `math.h` の `math_errhandling` は `MATH_ERREXCEPT` だけだが、今の実装は errno も立てる。
- host に MPFR（libmpfr-dev 4.2.2）、gmpy2、mpmath 1.3.0、sollya 8.0 を入れた（2026-09-28、参照値と係数の生成に使う）。

## 2. 対象の関数と精度の目標

| 群 | 関数 | 目標 |
| --- | --- | --- |
| A 正確 | fabs copysign frexp ldexp scalbn scalbln modf trunc floor ceil round rint nearbyint lrint llrint lround llround nextafter nexttoward fmin fmax fdim ilogb logb nan fpclassify signbit | 結果は正確（丸めが要るものは IEEE の最近接偶数） |
| A 正確 | fmod remainder remquo | 正確（IEEE 754。remquo の商は下位 3 bit 以上と符号） |
| A 正確 | fma | 1 回の丸め（融合）。arm64 は hardware |
| A 正確 | sqrt | 正しく丸める（整数の桁ごとの平方根） |
| B 正しい丸めに近い | exp exp2 expm1 log log2 log10 log1p pow sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh cbrt hypot | double-double（約 2^-63 以下の相対誤差）で求めて最後に 1 回丸める。**誤差 1 ulp 以内を保証**し、測った最大が 0.51 ulp 程度。正確に表せる結果（整数の冪、exp(0)、log2(2^n)、cbrt(27) など）は正確 |
| C 1 ulp 以内 | erf erfc tgamma lgamma | 1 ulp 以内（lgamma は負の零点の近くを除き、そこは絶対誤差で評価。p006 で確定） |
| D 範囲外 | j0 j1 jn y0 y1 yn | 1 ulp の目標の外。p006 で大きな引数の漸近展開だけ入れるか Future Work |
| float | 全部の `f` の版 | 群 A は正確（fmaf は round-to-odd）。群 B・C は double で求めて 1 回 float へ丸める（誤差 0.5+2^-29 ulp 程度） |
| long double | 全部の `l` の版 | amd64・i386 は double と同じ（正確に同じ関数）。binary128 の arm64・sparcv9 は double へ落として求める（精度は double、制限として記録し Future Work） |

ulp の定義: 真の値 v の binade の ulp（`2^(max(e,-1022)-52)`）。誤差 = |計算値 − v| / ulp(v)。

## 3. 特殊な値・errno・例外

- `math_errhandling` を `MATH_ERRNO | MATH_ERREXCEPT` にする（`include/libc/math.h`）。両方を立てる。
- 定義域の誤り（sqrt(-1)、log(-1)、acos(2)、fmod(x,0)、sin(Inf) ほか）: NaN、`EDOM`、`FE_INVALID`。
- 極（log(0)、atanh(±1)、pow(0,-1)、lgamma(0) ほか）: ±Inf、`ERANGE`、`FE_DIVBYZERO`。
- overflow: ±HUGE_VAL、`ERANGE`、`FE_OVERFLOW|FE_INEXACT`。underflow（結果が subnormal か 0 で不正確）: `ERANGE`、
  `FE_UNDERFLOW|FE_INEXACT`。
- NaN の入力は `x + x`（静かな NaN を返し errno を変えない）。C11 Annex F の表（pow・atan2・hypot の Inf と NaN の規則など）に従う。
- 符号付きの 0 を保つ（sin(-0) = -0、atan(-0) = -0、tanh(-0) = -0、expm1(-0) = -0 など）。
- `FE_INEXACT` は群 B・C で立てても立てなくてもよい（Annex F が許す）。群 A の正確な結果では立てない。

## 4. 方式の共通部分

- `src/libc/math/math-internal.h`: bit の出し入れ（`zm_bits`・`zm_from_bits`、float も）、double-double（`struct zm_dd`、
  `zm_two_sum`〔Knuth〕、`zm_fast_two_sum`〔Dekker〕、`zm_two_prod`〔Veltkamp の分割と Dekker の積。`__FP_FAST_FMA` では
  `__builtin_fma`〕、dd の加算・乗算・除算・平方根）、定数（π/2・π・ln2 の dd）、共通 kernel の宣言。static inline で置く。
- `errors.c`: `zm_invalid`・`zm_pole`・`zm_overflow`・`zm_underflow`（errno と例外を立てて値を返す）、float への丸めの
  `zm_narrow_float`（float の overflow・underflow の検出）。
- `zm_scale_dd(hi, lo, n)`: (hi+lo)·2^n を 1 回だけ丸めて返す。正常な範囲は指数の足し算、overflow は `zm_overflow`、
  subnormal は 2^(n+1022) 倍した値に 1 を足して subnormal と同じ格子（2^-52）で丸め、1 を引いて 2^-1022 倍する（二重の丸めを避ける）。
- 精度の予算: 群 B の kernel は相対誤差 2^-63 以下（pow のための log は 2^-76 以下）を狙う。最後に `hi + lo` を 1 回丸める
  ので、真の値が中点から 2^-63 より離れていれば正しく丸まり、そうでなくても誤差は 0.5+2^-10 ulp 未満。

## 5. 関数ごとの算法

- **sqrt**: 仮数を偶数の指数へ揃え、整数の桁ごとの平方根（毎回 1 bit を決める restoring の算法、余りは 64 bit に収まる）で
  54 bit と余りの sticky を出し、`zsf64_round_pack` で最近接偶数に丸める。
- **fma**: arm64（`__FP_FAST_FMA`）は `__builtin_fma`。他は整数: 53×53 の積（32 bit の部分積）を 128 bit で持ち、z を sticky 付きで
  桁合わせして加減、正規化、`zsf64_round_pack`。0・Inf・NaN の規則は IEEE。**fmaf** は double で x·y が正確（48 bit）なので
  x·y+z を two_sum で求め、不正確なら最下位 bit を奇数に寄せ（round-to-odd、Boldo-Melquiond）て float へ丸める。
- **fmod・remainder・remquo**: 仮数を整数にして、指数の差の分だけ「引けるなら引いて 1 bit ずらす」長い除算（剰余は常に 54 bit
  以下、正確）。remainder は最後に商の最下位 bit と余りの 2 倍を比べて最近接偶数の商へ直す。remquo は商の下位 bit を集める。
- **rint 系・丸め系**: 仮数の bit の mask で。rint・nearbyint は最近接偶数（mode は最近接だけ）。lrint 等は範囲外で FE_INVALID。
- **exp**: Tang の表の方式。x = (128m + j)·ln2/128 + r、|r| ≤ ln2/256。ln2/128 を 3 つ（35 bit・53・53）に分け（Cody-Waite）、r を
  dd で持つ。2^(j/128) の表は dd（128×2）。exp(r)−1 は Taylor の 7 次（係数は 1/n! を丸めたもの）、r² の項まで dd。
  結果は `zm_scale_dd`。kernel `zm_exp_dd(struct zm_dd t, int *scale)` は dd の引数を受け（pow・sinh・erfc が使う）。
- **exp2**: x = k/128 + r（r は正確）、r·ln2 を dd にして同じ kernel。**expm1**: kernel の dd の結果から 1 を dd で引く。
  |x| < ln2/256 は kernel の多項式の相対精度がそのまま効く（7 次で 2^-75）。
- **log**: x = 2^e·m、m ∈ [0.709, 1.418)。m の bit から 128 区間を選び、c ≈ 1/区間の中央（9 bit）、r = m·c − 1 は two_prod で
  正確な dd。**1 を含む区間は c = 1、log c = 0 で、区間の中央が 1**（x ≈ 1 で表の値と級数が打ち消し合わない）。
  log(x) = e·ln2 + (−log c) + log1p(r)。log1p(r) = r − r²/2 + r³/3 − … の 11 次、先頭の 3 項は dd。相対誤差 2^-78 を狙い
  pow もこの kernel（`zm_log_dd`）を使う。
- **log2・log10**: e + log(m)·(1/ln2)、log(x)·(1/ln10) を dd で。2^n・10^n は正確。**log1p**: 1+x を dd にし、
  `zm_log_dd(hi) + lo/hi`。
- **pow**: C11 F.9.4.4 の特殊な場合を先に。|y| ≥ 2^64 は overflow か underflow が決まる。それ以外は y·log|x| を dd（two_prod）、
  dd の引数の exp。負の x は y が整数のときだけ、奇数なら符号を付ける。y·log|x| の誤差は 2^-66 以下なので、正確に表せる結果は
  正確に出る（14^2 = 196）。
- **sin・cos・tan**: 引数の縮約: |x| < 2^20·π/2 は Cody-Waite（π/2 を 33 bit × 3 + 53 bit に分割）で r を dd に。大きな x は
  Payne-Hanek（2/π の 1200 bit の表から x の指数に合う窓 224 bit を取り、53 bit の仮数と整数で掛け、4 を法とする整数部と小数部
  160 bit 以上を得る。最悪の打ち消し〔約 2^-61〕の後も 100 bit 残る）。kernel: r = a_i + d、a_i = i/64、sin(a_i)・cos(a_i) の dd の表
  （51 項）、sin d と cos d − 1 は |d| ≤ 1/128 で 7 次・8 次の Taylor。sin(a+d) = S + (S·(cos d − 1) + C·sin d)、C·d は two_prod。
  tan は sin と cos の dd の除算（奇数の象限は −cos/sin）。
- **atan・atan2・asin・acos**: kernel `zm_atan_dd(u)`（0 ≤ u ≤ 1、dd）: c_i = i/64、atan(c_i) の dd の表（65 項）、
  t = (u − c)/(1 + u·c) を dd で、atan(t) は 13 次の Taylor。|x| > 1 は π/2 − atan(1/x)（1/x は dd）。atan2 は比を dd で（両方を 2 の冪
  で scale して overflow・underflow を避ける）、象限で π・π/2 を dd で足す。asin(x) = atan(x/√((1−x)(1+x)))（|x| ≥ 1/√2 は
  π/2 − atan(√(…)/x)）、acos も同じ kernel で打ち消しの無い形を選ぶ。
- **sinh・cosh・tanh**: dd の exp と expm1 から。sinh は |x| < 1 で (E + E/(E+1))/2（E = expm1|x|）、それ以上は (e^x − e^-x)/2、
  |x| > 40 は e^|x|/2（scale を 1 減らすので 709.78 < x < 710.47 も overflow しない）。tanh = E/(E+2)（E = expm1(2|x|)）、
  |x| > 22 は ±1。
- **asinh・acosh・atanh**: dd の sqrt と `zm_log_dd` で log(|x| + √(x²+1))、log(x + √(x²−1))（x²−1 は two_prod で正確）、
  log((1+x)/(1−x))/2。大きな |x| は log|x| + ln2。
- **cbrt**: 指数を 3 で割り、仮数の近似から Newton を double で 3 回、最後に y³ − x を dd で求めて 1 回の補正を加える。
  正確な立方は正確。
- **hypot**: 大きい方の指数で両方を scale、x²+y² を dd、dd の sqrt、`zm_scale_dd` で戻す。指数の差が 60 を超えれば |大| + |小|。
- **erf・erfc**（p006）: |x| < 1 は x·P(x²)、それより大きいところは exp(−x²)·R(x)（exp は dd の引数の kernel、R は区間ごとの
  多項式）。係数は sollya の fpminimax か mpmath の Remez（自作の script）で作り、先頭の係数は dd、Horner の最後の数段を dd に。
- **tgamma・lgamma**（p006）: x ≥ 10 は Stirling の級数（Bernoulli 数の項）を dd で、それより小さい正の x は漸化式で 10 以上へ
  上げ積を dd で割る、負の x は反射公式（sin(πx) は x を 2 で割った余りで正確に縮約）。lgamma の 1・2 の近くは Taylor。
- **float の版**: double の実装を呼んで 1 回丸め、`zm_narrow_float` で float の overflow・underflow を立てる。群 A は float の
  bit の操作か double で正確に。

## 6. file の構成

`src/libc/math/` に分ける（全部 `#pragma STDC FP_CONTRACT OFF`、規約の全文を適用）:
`math-internal.h`、`errors.c`、`classify.c`（fpclassify・signbit・fabs・copysign・nan・nextafter・fmin 系・ilogb・logb・frexp・
ldexp・scalbn・modf）、`rounding.c`（trunc 系・rint 系・lrint 系）、`remainder.c`（fmod・remainder・remquo）、`fma.c`、`sqrt.c`、
`exp.c`、`log.c`、`pow.c`、`trig.c`・`trig-reduce.c`、`atrig.c`、`hyperbolic.c`、`cbrt-hypot.c`、`erf.c`、`gamma.c`、
`bessel.c`、`float.c`（f の版）、`long-double.c`（l の版）、`tables.c`（生成した表）。移行中は `legacy.c` に未着手の関数を
残し、p006 で消す。`src/libc/math.c` は p002 で消す。

build: `src/libc/libc.mk` に `ZEDBSD_LIBM_SOURCES` を置き、softfloat.mk・5 つの vmunix.mk・sysroot.mk の math.c の 1 つの
object を、この一覧の object に変える。

## 7. 係数と表の生成

`src/libc/math/gen/gen-tables.py`（mpmath、200 bit の精度）が `tables.c` を書く: exp の 2^(j/128)、log の c_i と −log c_i、
sin/cos の i/64、atan の i/64、2/π の bit、π/2 の分割、Taylor の係数（1/n! などの有理数の最近接の double）。区間ごとの minimax
（erf・gamma）は同じ directory の sollya の script か mpmath の Remez で作り、方法を file の先頭に書く。生成は build の一部に
しない（出力を commit し、script の再実行で同じ出力になることを確かめる）。

## 8. 試験

- 参照: `plan/tools/libm/gen-reference.py`（gmpy2 = MPFR、精度 160 bit）が関数ごとに入力（境界の値、特殊な値、各 binade の
  無作為、既知の難しい点〔π/2 の倍数の近く、整数の冪、1 の近く〕）と真の値（dd の hi と lo）を binary の file に書く。
  host の glibc の結果も比較の欄に入れる。
- 実行: `plan/tools/libm/libm-test.c`（標準 C だけ、MPFR 無し）が file を読み、関数ごとに最大 ulp 誤差、平均、1 ulp 以上の件数、
  正確であるべきものの不一致、特殊な値の表（C11 Annex F の値、errno、例外）を出す。
- host: `plan/tools/libm/host-test.sh` が `src/libc/math/*.c`・`softfloat.c`・`fenv.c` と runner を host の cc で
  （`-fno-builtin`、libm を link しない）build して走らせる。
- guest: 同じ runner を sysroot の clang で amd64 向けに build し（libc.so の libm を使う）、`plan/tools/guest/guest.sh` で
  data と一緒に送って走らせ、host と同じ表を得る。
- ブラウザ: `plan/ws074/tests/js/operators.js` の `14 ** 2` と `1e17 % 7` が Chromium と一致（p007、guest）。

## 9. 算法の出典

T. J. Dekker, "A floating-point technique for extending the available precision" (1971)（Fast2Sum・Veltkamp の分割・積）;
D. E. Knuth, TAOCP vol. 2（TwoSum）; W. J. Cody, W. Waite, "Software Manual for the Elementary Functions" (1980)（縮約）;
M. Payne, R. Hanek, "Radian reduction for trigonometric functions" (1983); P. T. P. Tang, "Table-driven implementation of the
exponential function" (1989)、"… of the logarithm function" (1990); S. Boldo, G. Melquiond, "Emulation of FMA and correctly
rounded sums: proved algorithms using rounding to odd" (2008); J.-M. Muller, "Elementary Functions: Algorithms and
Implementation"; N. Brisebarre, S. Chevillard, "Efficient polynomial L∞-approximations" (2007)（fpminimax）。
Taylor・Stirling・反射公式・漸化式は標準の数学の公式。

## 10. Phase

| Phase | 内容 |
| --- | --- |
| p002 | `src/libc/math/` への分割と build の rule、共通の header と errors.c、群 A（sqrt・fma・fmod・remainder・remquo・丸め系・分類系）、試験の道具（参照の生成、runner、host の script） |
| p003 | exp・exp2・expm1・log・log2・log10・log1p と `zm_scale_dd`、表の生成 |
| p004 | pow |
| p005 | 三角・逆三角・双曲線・逆双曲線 |
| p006 | cbrt・hypot・erf・erfc・lgamma・tgamma、float・long double の版、Bessel の扱い、legacy.c の削除 |
| p007 | 規約の全文の照合、host と guest の全表、ブラウザの operators.js、boot test |
