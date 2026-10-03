// ws074-p079: destructuring, default and rest parameters, spread and optional chaining.
var { a, b: [c, ...d], e = 5, ...f } = { a: 1, b: [2, 3, 4], g: 7, h: 8 };
print(a, c, d.join(), e, JSON.stringify(f));
let [x, , y = 9, ...z] = "abcde"; print(x, y, z.join());
const { length } = "hello"; print(length);
var p, q; [p, q] = [1, 2]; [p, q] = [q, p]; print(p, q);
var o = {}; ({ k: o.m, ["n" + 1]: o.n } = { k: "K", n1: "N" }); print(o.m, o.n);
var r = ([s, t] = [7, 8]); print(r.join(), s, t);
print([...[1, 2], ...'ab', 3].join(), Math.max(...[1, 5, 3]), JSON.stringify({ ...{ u: 1 }, v: 2, ...null, ...undefined }));
function f3(a1, b1, c1) { return a1 + b1 + c1; } print(f3(...[1, 2], 3), new Array(...[3]).length, [..."😀x"].length);
try { var { zz } = null; } catch (err) { print(err.name); }
try { var [w1] = {}; } catch (err) { print(err.name); }
try { throw { code: 7, msg: "m" }; } catch ({ code, msg }) { print(code, msg); }
for (const [k1] in { ab: 1 }) print(k1);
var fns = []; for (let [i1, j1] = [0, 0]; i1 < 2; i1++) fns.push(() => i1 + j1); print(fns.map(g => g()).join());
function pf(a2, b2 = a2 + 1, { c2, d2 = 4 } = { c2: 3 }, [e2] = [5], ...rest) { return [a2, b2, c2, d2, e2, rest.length, rest.join("|")].join(); }
print(pf(1), pf(1, 2, { c2: 30, d2: 40 }, [50], 6, 7), pf.length);
var g2 = (x2 = 10, ...ys) => x2 + ys.length; print(g2(), g2(1, 2, 3), g2.length);
function h2(...args) { return args.length + ":" + arguments.length; } print(h2(1, 2), h2.length);
function k2(p2, q2 = () => p2) { p2 = 9; return q2(); } print(k2(1));
var m2 = ({ a3, b3 }) => a3 + b3; print(m2({ a3: 1, b3: 2 }), m2.length);
function n2(a4 = 1) { return arguments.length; } print(n2(), n2(5));
var oc = { a: { b: 1, f: function () { return this.b + 1; } } }, nul = null, und;
print(oc?.a?.b, nul?.a, und?.a.b.c, oc.x?.y, oc.a?.["b"], nul?.[1], oc.a.f?.(), oc.a.g?.(), oc?.a.f(), nul?.f(), (nul?.a)?.b, (oc?.a.f)());
var calls = 0; function side() { calls++; return 1; }
print(nul?.[side()], calls, und?.a(side()), calls);
