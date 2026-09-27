<!-- awesome-plan project=zedbsd record=ws076p007 -->

# ws076-p007: 規約の全文の照合と回帰（math_errhandling、ブラウザの operators.js）

Phase ID: `ws076-p007`
Parent: [WS076](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（main の指示でサブエージェント LIBM が worktree の branch で実行）

## 範囲

WS076 の全 source の規約の全文による照合、`math_errhandling` の変更、host と guest の全表、ブラウザの JS の `%`・`**`・Math.* が
Chromium と一致すること、他の platform の build、boot test。

## 受け入れ

1. 規約の全文（`plan/coding-style.md`）の照合: `style-check.py` 0 と、機械で見られない規則の目視。
2. host と guest の libm の試験が PASS。ブラウザの `plan/ws074/tests/js/operators.js`（`14 ** 2`）と `1e17 % 7` が guest で Chromium と
   一致。
3. build（amd64 warning 0、他の platform の libc）と boot test。

## 結果（2026-09-28）

cleared。

- `include/libc/math.h`: `math_errhandling` を `(MATH_ERRNO | MATH_ERREXCEPT)` に（libm は両方を立てる）。
- 規約の照合: `src/libc/math/` の全 file と `plan/tools/libm/*.c` で `style-check.py` 0、`git diff --check` 0。目視の項目のうち、
  §11「意味のある呼び出しの結果を直接 return しない」を関数の最後の return に当てはめ、38 箇所（`__libm_narrow`・`libm_step`・
  `libm_signed_result`・`libm_make` ほか）を変数に受けて返す形に直した。途中の guard の `return __libm_invalid();` などの error
  報告の helper と bit の変換（`libm_from_bits`・`copysign`）はそのまま（guard は条件の comment が説明し、helper は返す値そのもの）。
  型・file 変数・forward 宣言・段落の comment・ANSI の宣言位置は各 Phase で満たした。
- ブラウザ: `plan/ws076/tests/browser-js.sh`（lean image ＋ zdesktop-browser ＋ serial、`config-amd64-browser-libm.mk`）で
  ws074 の JS の試験 7 本と WS076 の `js/libm.js`（`1e17 % 7`、`2 ** 60 % 7`、`14 ** 2`、`3 ** 40`、`10 ** 22`、`2 ** -1074`、
  Math.exp・log・sin・cos・tan・asin・acos・atan・atan2・sinh・cosh・tanh・asinh・acosh・atanh・cbrt・hypot・sqrt・pow）を guest で
  走らせ、Chromium（`--reference` で作った `NAME.expected`）と比べた。
  - **operators が guest で pass**（`compound 196 22 2`、`arith … 1024 0.5 …`）。以前の BUG-078 の除外（run-js-tests.py の
    `GUEST_KNOWN`）無しで一致。builtins・closures・control・objects・strict も pass。
  - libm.js 8 行が pass（`mod 5 1 -1.5 1.5 0 0.09999999999999998`、`exp 2.718281828459045 …`、`log 2.302585092994046 …`、
    `trig 0.8414709848078965 …`）。
  - V8 自身の Math.acosh(2)・Math.atanh(0.5) は 0.61・0.59 ulp ずれている（1.3169578969248166・0.5493061443340548、MPFR の正しい
    丸めは 1.3169578969248168・0.5493061443340549、libc はこちら）。この 2 つは libm.js から外し、理由を file に書いた。
  - collections の 1 行（ギリシャ文字の小文字）が 2 回のうち 1 回だけ serial の写しで化けた（libm と無関係、同じ libc で別の回は
    pass）。未解析。
- 他の platform: pcat（i386、soft float）と rpi4（arm64、`__FP_FAST_FMA` の経路）の `dynamic/libc.so` が build できた
  （`build/ws076-pcat`・`build/ws076-rpi4`）。arm64・i386 の実行の試験は未実施（試験は amd64 だけ）。pc98・sparcv9 は未実施。
- 表の生成（`gen-tables.py`、sollya を含めて約 3 分）が同じ `tables.c`・`math-constants.h` を再現した。

## 検証

- host（全 23 族、各 20000 件）: 群 A は全部正確、群 B は全部 0.5000 ulp 以下、群 C は erf 0.51・erfc 0.51・tgamma 0.54・
  lgamma 0.50 ulp、特殊な値 182 件 0 failed、`libm-test: PASS`（p003〜p006 の大きな件数の表は各 Phase の記録）。
- guest（QEMU、amd64、`plan/tools/libm/guest-test.sh`、各 2000 件）: `libm-test: PASS`、特殊な値 0 failed。
- ブラウザ（guest）: ws074 の JS 6〜7/7（collections の serial の化けは上のとおり）、`ws076-js 1/1`。
- boot test: `build/ws076-boot-p007/login.png`（libm の image）と `build/ws076-boot-p007-browser/login.png`（ブラウザの image）
  PASS。
