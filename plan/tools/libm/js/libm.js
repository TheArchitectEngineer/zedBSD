// ws076: the libm results that the browser's operators and Math functions pass through.
print("mod", 1e17 % 7, 2 ** 60 % 7, -7.5 % 2, 5.5 % -2, 1e300 % 3, 0.3 % 0.1);
print("pow", 14 ** 2, 3 ** 40, 10 ** 22, 2 ** -1074, 1.0000001 ** 1e7, (-2) ** 3, 0.1 ** 3);
print("exp", Math.exp(1), Math.exp(-1), Math.exp(10), Math.expm1(1e-10), Math.exp(709));
print("log", Math.log(10), Math.log2(3), Math.log10(2), Math.log1p(1e-10), Math.log(0.5));
print("trig", Math.sin(1), Math.cos(1), Math.tan(1), Math.sin(1e22), Math.cos(1e300), Math.atan2(1, -1));
print("inverse", Math.asin(0.5), Math.acos(0.3), Math.atan(10));
// acosh(2) and atanh(0.5) are left out: V8's own Math.acosh and Math.atanh give them 0.61 and 0.59 ulp
// away (1.3169578969248166, 0.5493061443340548), and libc rounds them correctly (MPFR).
print("hyper", Math.sinh(1), Math.cosh(1), Math.tanh(0.5), Math.asinh(1), Math.acosh(3), Math.atanh(0.25));
print("other", Math.cbrt(2), Math.hypot(1e200, 1e200), Math.sqrt(2), Math.pow(2, 0.5));
