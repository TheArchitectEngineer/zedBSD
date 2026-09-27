// ws074-p026: Object, Function, the errors, Boolean, Number, Math and the global functions.

// Errors are objects now: their kind, name, message and toString.
try {
	null.x;
} catch (e) {
	print("error", e instanceof TypeError, e instanceof Error, e.name, e.constructor === TypeError);
}
var made = new RangeError("too far", { cause: "reason" });
print("made", "" + made, made.message, made.cause, Object.prototype.toString.call(made));
print("error-call", Error("called") instanceof Error, Error.prototype.toString.call({ name: "N", message: "" }));
print("syntax", (function () { try { eval("var = 1"); } catch (e) { return e.name; } })());

// Number formatting (the shortest round trip, and the fixed, exponential and precision forms).
print("number", 0.1 + 0.2, 1e21, 1e-7, 123456789012345680000, -0.0000015, (255).toString(16), (0.5).toString(2));
print("fixed", (1.005).toFixed(2), (1.5).toFixed(0), (2.5).toFixed(0), (-1.5).toFixed(0), (1e21).toFixed(2), (0.000001).toFixed(7));
print("exponential", (123456).toExponential(2), (0).toExponential(), (1.25).toExponential(1), (5e-7).toExponential());
print("precision", (123.456).toPrecision(4), (0.000123).toPrecision(2), (123456).toPrecision(2), (1e21).toPrecision(3));
print("constants", Number.MAX_SAFE_INTEGER, Number.EPSILON > 0, Number.MIN_VALUE, Number.isInteger(5.0), Number.isSafeInteger(2 ** 53));

// Reading numbers.
print("parse", parseInt("  42px"), parseInt("0x1F"), parseInt("z", 36), parseInt("-0"), parseInt(""), parseFloat("3.14abc"), parseFloat(".5e1x"), parseFloat("-Infinity"));
print("convert", Number("  12  "), Number("0b101"), Number("1e1000"), Number(""), Number("12px"), +"-0x10", isNaN("abc"), isFinite("12"));

// Boolean and the wrappers.
var wrapped = new Boolean(false);
print("boolean", Boolean(""), Boolean("0"), typeof wrapped, wrapped ? "truthy" : "falsy", wrapped.valueOf(), true.toString());
var boxed = new Number(7);
print("wrapper", typeof boxed, boxed + 1, boxed.toFixed(1), Object.prototype.toString.call(boxed), Object(3) instanceof Number);

// Math.
print("math", Math.max(1, 5, 3), Math.min(), Math.max(), Math.round(2.5), Math.round(-2.5), Math.round(-0.2), Math.abs(-3), Math.floor(-1.5), Math.ceil(1.2));
print("math2", Math.sign(-4), Math.trunc(-4.7), Math.sqrt(16), Math.pow(2, 8), Math.hypot(3, 4), Math.clz32(1), Math.imul(3, 4), Math.fround(5.5));
var random = Math.random();
print("random", random >= 0 && random < 1);

// Object: properties, descriptors and integrity.
var target = {};
Object.defineProperty(target, "fixed", { value: 1, enumerable: false });
Object.defineProperty(target, "shown", { value: 2, enumerable: true, writable: true, configurable: true });
var descriptor = Object.getOwnPropertyDescriptor(target, "fixed");
print("define", target.fixed, descriptor.writable, descriptor.enumerable, descriptor.configurable, Object.keys(target).length, Object.keys(target)[0]);
try {
	Object.defineProperty(target, "fixed", { value: 3 });
} catch (e) {
	print("redefine", e.name);
}
var frozen = Object.freeze({ a: 1 });
frozen.a = 2;
print("freeze", frozen.a, Object.isFrozen(frozen), Object.isSealed(frozen), Object.isExtensible(frozen));
var child = Object.create({ inherited: true }, { own: { value: "yes", enumerable: true } });
print("create", child.inherited, child.own, child.hasOwnProperty("own"), child.hasOwnProperty("inherited"), Object.getPrototypeOf(child).inherited);
var assigned = Object.assign({}, { x: 1 }, null, { y: 2 });
print("assign", assigned.x, assigned.y, Object.is(NaN, NaN), Object.is(0, -0));
print("names", Object.getOwnPropertyNames({ b: 1, a: 2 }).length, Object.entries({ k: "v" })[0][1], Object.values({ k: "v" })[0]);
var noProto = Object.create(null);
print("null-proto", Object.getPrototypeOf(noProto), typeof noProto.toString);

// Accessors through defineProperty and the Annex B methods.
var counter = { count: 0 };
Object.defineProperty(counter, "next", { get: function () { return ++this.count; } });
counter.__defineGetter__("twice", function () { return this.count * 2; });
print("accessor", counter.next, counter.next, counter.twice, typeof counter.__lookupGetter__("twice"));

// Function: call, apply, bind and the constructor.
function describe(greeting, mark) { return greeting + ", " + this.name + mark; }
var person = { name: "Ada" };
print("call", describe.call(person, "Hello", "!"), describe.apply(person, ["Hi", "?"]));
var bound = describe.bind(person, "Hey");
print("bind", bound("."), bound.name, bound.length);
function Point(x, y) { this.x = x; this.y = y; }
var BoundPoint = Point.bind(null, 1);
var point = new BoundPoint(2);
print("bound-new", point.x, point.y, point instanceof Point);
var add = new Function("a", "b", "return a + b;");
print("function", add(2, 3), typeof add, Function("return 7")());
print("eval", eval("1 + 2"), eval("var evaluated = 5; evaluated * 2"), typeof evaluated, eval(42));

// toString of objects and primitives.
print("tostring", Object.prototype.toString.call(null), Object.prototype.toString.call([]), Object.prototype.toString.call(function () {}), "" + {});
