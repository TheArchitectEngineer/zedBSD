// ws074-p087: symbols, the iterator protocol, for-of, Map, Set, WeakMap, WeakSet and the URI functions.

// Symbols.
var s = Symbol("x");
print(typeof s, s.toString(), String(s), s.description, Symbol().description);
print(Symbol.iterator.toString(), Symbol.for("k") === Symbol.for("k"), Symbol.keyFor(Symbol.for("k")), Symbol.keyFor(s));
var keyed = { [s]: 1, plain: 2 };
print(Object.keys(keyed).join(), Object.getOwnPropertySymbols(keyed).length, keyed[s]);
try {
	s + "";
} catch (e) {
	print("symbol to string:", e.name);
}
try {
	new Symbol();
} catch (e) {
	print("new Symbol:", e.name);
}

// for-of over arrays, strings, Maps, Sets, arguments and generators.
for (var x of [1, 2, 3])
	print("of", x);
for (let c of "a\u{1F600}b")
	print("char", c.length);
for (const [k, v] of new Map([["a", 1], ["b", 2]]))
	print("entry", k, v);
function args() {
	var list = [];
	for (var a of arguments)
		list.push(a);
	return list.join("-");
}
print(args(1, 2, 3));
function* numbers() {
	yield 1;
	yield 2;
	return 3;
}
print([...numbers()].join(), Array.from ? "" : "", [..."abc"].length);

// A user's iterator, closed when the loop is left early.
function counter(limit) {
	var i = 0;
	return {
		[Symbol.iterator]() { return this; },
		next() { return i < limit ? { value: i++, done: false } : { value: undefined, done: true }; },
		return() { print("closed at", i); return {}; }
	};
}
for (var v of counter(5)) {
	if (v == 2)
		break;
	print("v", v);
}
function first() {
	for (var v of counter(5))
		return v;
}
print("first", first());
try {
	for (var v of counter(5))
		throw new Error("inside");
} catch (e) {
	print("caught", e.message);
}
outer: for (var i of [1, 2]) {
	for (var j of counter(3))
		continue outer;
}
var [one, two] = counter(10);
print("pattern", one, two);
for (var w of counter(2))
	print("w", w);

// Map and Set.
var set = new Set([1, 2, 2, 3, NaN, NaN, -0, 0, "1"]);
print(set.size, [...set].join(), set.has(NaN), set.has(-0), set.has(1), set.has("1"));
var m = new Map();
m.set("x", 1).set("y", 2).set(NaN, "nan");
m.delete("x");
m.set("z", 3);
print(m.size, m.get(NaN), m.get("x"), m.has("y"));
m.forEach(function (value, key) { print("forEach", key, value); });
print([...m.keys()].join(), [...m.values()].join(), JSON.stringify([...m.entries()]));
var live = new Map([[1, "a"], [2, "b"]]), seen = [];
for (var [key] of live) {
	seen.push(key);
	if (key === 1) {
		live.delete(2);
		live.set(3, "c");
	}
}
print("live", seen.join());
var objectKey = {};
var weak = new WeakMap([[objectKey, "value"]]);
print(weak.get(objectKey), weak.has({}), weak.delete(objectKey), weak.has(objectKey));
var weakSet = new WeakSet();
weakSet.add(objectKey);
print(weakSet.has(objectKey));
try {
	weak.set(1, 2);
} catch (e) {
	print("weak key:", e.name);
}
print(Object.prototype.toString.call(m), Object.prototype.toString.call(set), Object.prototype.toString.call(m.entries()));
print(Object.prototype.toString.call([][Symbol.iterator]()), String(Math), Object.prototype.toString.call(Promise.resolve()));

// Symbol.toPrimitive, Symbol.hasInstance and Symbol.toStringTag.
var primitive = { [Symbol.toPrimitive](hint) { return hint; } };
print(`${primitive}`, primitive + "", +{ [Symbol.toPrimitive]() { return 5; } });
class Even {
	static [Symbol.hasInstance](n) { return n % 2 === 0; }
}
print(2 instanceof Even, 3 instanceof Even, [] instanceof Array);
print(Object.prototype.toString.call({ [Symbol.toStringTag]: "Custom" }));
print(typeof new Date(0)[Symbol.toPrimitive], new Date(0) - 0);

// The URI functions.
print(encodeURIComponent("a b&c=d/é?"), encodeURI("http://x/a b?q=1&r=é#h"));
print(decodeURIComponent("a%20b%26%C3%A9"), decodeURI("%3Fa%20b"), escape("a b+é"), unescape("%u3042%41"));
try {
	decodeURIComponent("%E0%A4%A");
} catch (e) {
	print("malformed:", e.name);
}
