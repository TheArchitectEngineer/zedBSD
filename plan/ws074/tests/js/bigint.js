// BigInt (ws074-p230): literals, operators, conversions, comparisons, BigInt(), asIntN/asUintN.
function t(f) { try { return String(f()); } catch (e) { return e.name; } }
print(typeof 1n, typeof BigInt(5), typeof Object(1n));
print(123n, 0x1fn, 0b101n, 0o17n, 1_000n, -5n);
print(2n + 3n, 2n - 5n, 6n * 7n, 7n / 2n, -7n / 2n, 7n % 3n, -7n % 3n, 7n % -3n);
print(2n ** 100n, (-2n) ** 3n, 0n ** 0n, 5n ** 0n);
print(123456789012345678901234567890n * 987654321098765432109876543210n);
print(123456789012345678901234567890123456789n / 12345678901234567n, 123456789012345678901234567890123456789n % 12345678901234567n);
print(5n & 3n, 5n | 3n, 5n ^ 3n, ~5n, -5n & 3n, -5n | 3n, -5n ^ 3n, ~-1n);
print(1n << 70n, -1n << 70n, (1n << 70n) >> 69n, -5n >> 1n, -1n >> 100n, 5n >> -2n, 5n << -1n);
print(t(function () { return 1n + 1; }), t(function () { return 1n * 2; }), t(function () { return +1n; }));
print(t(function () { return 1n / 0n; }), t(function () { return 2n ** -1n; }), t(function () { return 1n >>> 0n; }));
print(1n == 1, 1n === 1, 1n == "1", 2n > 1, 1n < 1.5, 2n >= 2, 1n < "2", 10n > 9.5, 1n == true, 0n == false, 1n == 2n, 1n === 1n);
print(0n == "", 1n == "abc", NaN < 1n, 1n < NaN, 1n < Infinity, -1n > -Infinity);
print(!0n, !1n, 0n ? "t" : "f", 3n ? "t" : "f");
print(String(255n), (255n).toString(16), (-255n).toString(2), (255n).toString(36), `${10n}`, "a" + 1n);
print(BigInt(10), BigInt("0x10"), BigInt(" 12 "), BigInt(""), BigInt(true), BigInt(Number.MAX_SAFE_INTEGER) + 2n, BigInt(1e21));
print(t(function () { return BigInt(1.5); }), t(function () { return BigInt("1.5"); }), t(function () { return BigInt(undefined); }), t(function () { return new BigInt(1); }));
print(Number(123n), Number(2n ** 64n), Number(-(2n ** 53n) - 1n), parseInt("12"), Number(2n ** 1100n));
print(BigInt.asUintN(8, 257n), BigInt.asUintN(8, -1n), BigInt.asIntN(8, 255n), BigInt.asIntN(8, 127n), BigInt.asIntN(64, 2n ** 63n), BigInt.asUintN(64, -1n), BigInt.asIntN(0, 5n));
var x = 5n; x++; x += 10n; x *= 2n; --x;
print(x, x++, x, -x, typeof -x);
print(t(function () { return JSON.stringify({ a: 1n }); }), t(function () { return Math.max(1n); }));
print(Object.prototype.toString.call(1n), 5n.constructor === BigInt, (5n).valueOf() === 5n);
var m = new Map(); m.set(1n, "a"); print(m.get(1n), m.has(2n), new Set([1n, 1n, 2n]).size);
var o = {}; o[3n] = "x"; print(o["3"], Object.keys(o).join());
print([3n, 1n, 2n].sort(function (a, b) { return a < b ? -1 : a > b ? 1 : 0; }).join());
print(2n ** 200n > 2n ** 199n, (2n ** 64n).toString(), -(2n ** 64n));
