// ws074-p078: let and const (block scopes, TDZ, per-iteration bindings), arrow functions and template literals.
let a = 1; const b = 2;
{ let a = 10; print(a, b); }
print(a, typeof globalThis.a, "b" in globalThis);
var fs = [];
for (let i = 0; i < 3; i++) fs.push(() => i);
print(fs.map(f => f()).join());
var gs = [];
for (var j = 0; j < 3; j++) { let k = j * 2; gs.push(function () { return k; }); }
print(gs.map(g => g()).join());
var hs = [];
for (const key in { x: 1, y: 2 }) hs.push(() => key);
print(hs.map(h => h()).join());
try { c; } catch (e) { print(e.name, e.message); }
let c = 3;
try { b = 5; } catch (e) { print(e.name, e.message); }
function tdz() { try { return z; } catch (e) { return e.name; } let z; }
print(tdz(), (function () { try { typeof w; } catch (e) { return e.name; } let w; })());
function inner() { let x = 1; const y = 2; x += y; return () => x + y; }
print(inner()());
switch (1) { case 1: let s = "s1"; print(s); break; case 2: print("no"); }
{ function blockFn() { return a + 100; } print(blockFn()); }
print(typeof blockFn);
let counter = 0; for (let n = 0; n < 5; n++) { counter += n; } print(counter);
try { (function () { "use strict"; const q = 1; q = 2; })(); } catch (e) { print(e.name); }
outer: for (let m = 0; m < 3; m++) { for (let p = 0; p < 3; p++) { if (p == 1) continue outer; if (m == 2) break outer; } }
var sq = x => x * x, add = (p, q) => p + q, none = () => 7, body = (v) => { return v + 1; };
print(sq(3), add(1, 2), none(), body(4), typeof sq, sq.length, sq.name, "prototype" in sq);
var o = { v: 10, f: function () { return [1, 2].map(x => x + this.v).join(); }, g: function () { var h = () => () => this.v; return h()(); } };
print(o.f(), o.g());
function args() { var f = () => arguments[0]; return f(9); }
print(args(5));
try { new sq(); } catch (e) { print(e.name); }
print([3, 1, 2].sort((p, q) => p - q).join(), (() => ({ a: 1 }))().a);
var self = this; print((() => this === self)());
var t = 1, u = { toString: function () { return "U"; }, valueOf: function () { return 42; } };
print(`x${t}y${u}z`, `plain`, `${t}${t}`, `multi
line`, `A\x42`, `${1 + 1}` === "2");
function tag(s) { var r = [s.length, s.join("|"), s.raw.join("|")]; for (var n = 1; n < arguments.length; n++) r.push(arguments[n]); return r.join(","); }
print(tag`a${1}b${2}c`, tag`\n${"x"}`);
var obj = { w: "W", f: function (s) { return this.w + s[0]; } };
print(obj.f`!`);
