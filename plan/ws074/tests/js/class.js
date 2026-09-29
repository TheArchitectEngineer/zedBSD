// ws074-p085: classes (constructors, methods, accessors, static members, extends and super, fields, static blocks, private names, new.target).
class A { constructor(x) { this.x = x; } get double() { return this.x * 2; } set double(v) { this.x = v / 2; } m() { return "A.m" + this.x; } static s() { return "static " + this.name; } }
var a = new A(3); print(a.x, a.double, a.m(), A.s(), typeof A, Object.keys(a), a instanceof A, A.prototype.constructor === A);
a.double = 10; print(a.x, Object.keys(A.prototype).length);
try { A(); } catch (e) { print(e.name, e.message); }
class B extends A { constructor(x, y) { super(x); this.y = y; } m() { return "B>" + super.m(); } static s() { return super.s() + "!"; } }
var b = new B(1, 2); print(b.x, b.y, b.m(), B.s(), b instanceof A, Object.getPrototypeOf(B) === A, b.double);
class C extends B {} var c = new C(5, 6); print(c.x, c.y, c.m(), c.constructor.name);
class F { a = 1; b = this.a + 1; static t = 7; static { this.u = this.t * 2; } arrow = () => this.a; }
var f = new F(); print(f.a, f.b, F.t, F.u, f.arrow.call(null), Object.keys(f).join());
class G extends F { c = 3; constructor() { super(); this.d = this.c + this.a; } } var g = new G(); print(g.a, g.c, g.d);
var E = class Named { who() { return Named.name; } }; print(new E().who(), E.name, (class {}).name);
class T { constructor() { this.nt = new.target === T; } } print(new T().nt);
try { class D extends A { constructor() { this.z = 1; } } new D(); } catch (e) { print(e.name); }
class N extends null {} print(Object.getPrototypeOf(N.prototype));
class K { ["com" + "puted"]() { return 1; } } print(new K().computed());
class Ar { f() { return [1].map(() => super.toString === Object.prototype.toString); } } print(new Ar().f());
class Ar2 extends A { constructor() { super(4); this.g = () => this.x; } } print(new Ar2().g());
class P { #x = 1; static #count = 0; #m() { return this.#x * 10; } get #g() { return this.#x + 100; } set #g(v) { this.#x = v; }
  constructor() { P.#count++; } inc() { this.#x++; this.#x += 2; return this.#x; } call() { return this.#m(); } acc() { this.#g = 7; return this.#g; }
  static count() { return P.#count; } has(o) { return #x in o; } static #sm() { return "sm"; } static callSm() { return P.#sm(); } arrow() { return (() => this.#x)(); } }
var p = new P(); new P();
print(p.inc(), p.call(), p.acc(), P.count(), p.has(p), p.has({}), P.callSm(), p.arrow(), Object.keys(p).length, JSON.stringify(p));
try { P.prototype.inc.call({}); } catch (e) { print(e.name); }
class Q extends P { #y = 5; y() { return this.#y + this.inc(); } } print(new Q().y());
