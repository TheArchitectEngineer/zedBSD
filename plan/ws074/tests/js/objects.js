// ws074-p025: objects, arrays, properties and constructors.

function errorName(e) {
	var name = "";
	if (typeof e === "object")
		return e.name;
	for (var i = 0; i < e.length && e[i] !== ":"; i++)
		name += e[i];
	return name;
}

function keys(object) {
	var list = "";
	for (var key in object)
		list += key + ",";
	return list;
}

// Object literals: names, strings, numbers and computed keys, in order.
var key = "computed";
var literal = { b: 1, "a-b": 2, 3: "three", 1.5: "one and a half", [key]: 4, a: 5 };
print("literal", keys(literal), literal["a-b"], literal[3], literal["1.5"], literal.computed);

// A computed key is converted before its value is computed.
var steps = "";
var keyObject = { toString: function () { steps += "key "; return "k"; } };
var ordered = { [keyObject]: (steps += "value ", 1) };
print("key-order", steps, ordered.k);

// Shorthand properties and methods.
var shortValue = 7;
var short = { shortValue, twice(n) { return n * 2; } };
print("short", short.shortValue, short.twice(21));

// Accessors, one half at a time, and this inside them.
var stored = 0;
var accessor = {
	get value() { return stored * 10; },
	set value(v) { stored = v; },
	get onlyGetter() { return "g"; }
};
accessor.value = 4;
accessor.onlyGetter = "ignored";
print("accessor", accessor.value, stored, accessor.onlyGetter, keys(accessor));

// __proto__ in a literal sets the prototype.
var base = { inherited: "from base", shadowed: "base" };
var derived = { __proto__: base, shadowed: "derived", own: 1 };
print("proto", derived.inherited, derived.shadowed, keys(derived));

// Arrays: elements, holes, length, and growing by assignment.
var array = [1, , 3, , ];
print("array", array.length, array[1], 1 in array, 2 in array, keys(array));
array[10] = "ten";
print("grow", array.length, array[10]);
array.length = 2;
print("shrink", array.length, array[2], keys(array));
var nested = [[1, 2], [3, [4, 5]]];
print("nested", nested[1][1][0] + nested[0][1]);

// Strings: length and characters by index.
var text = "hello";
print("string", text.length, text[0], text[4], text[5], "" + text[1] + text[2]);

// delete: own properties, elements, and what cannot go.
var deletable = { a: 1, b: 2 };
var deletedA = delete deletable.a;
var deletedMissing = delete deletable.missing;
var list = [1, 2, 3];
var deletedElement = delete list[1];
var deletedLength = delete list.length;
print("delete", deletedA, deletedMissing, keys(deletable), deletedElement, list.length, 1 in list, deletedLength);

// Global variables: a var cannot be deleted, an implicit global can.
var declared = 1;
implicitGlobal = 2;
var deletedDeclared = delete declared;
var deletedImplicit = delete implicitGlobal;
print("globals", deletedDeclared, deletedImplicit, typeof declared, typeof implicitGlobal);

// for-in: own keys first, then inherited ones not shadowed, and keys deleted meanwhile are skipped.
var parentObject = { inheritedKey: 1, both: 1 };
var child = { __proto__: parentObject, both: 2, ownKey: 3, 2: "index", 0: "zero" };
print("for-in", keys(child));
var shrinking = { first: 1, second: 2, third: 3 };
var seen = "";
for (var k in shrinking) {
	seen += k + ",";
	delete shrinking.third;
}
print("for-in-delete", seen);
var visits = 0;
for (var none in null)
	visits++;
for (var nothing in undefined)
	visits++;
for (var index in "ab")
	seen += index;
print("for-in-other", visits, seen);

// Constructors: the prototype chain, a returned object, and a primitive return.
function Point(x, y) {
	this.x = x;
	this.y = y;
}
Point.prototype.sum = function () { return this.x + this.y; };
function Replaced() {
	this.ignored = true;
	return { replaced: true };
}
function Primitive() {
	this.kept = true;
	return 5;
}
var point = new Point(3, 4);
print("new", point.sum(), point instanceof Point, point.constructor === Point, new Replaced().replaced, new Primitive().kept);
print("prototype", typeof Point.prototype, Point.prototype.constructor === Point, "sum" in point, keys(point));

// Function objects carry their name and length.
function namedFunction(a, b, c) {}
var anonymousName = function () {};
var assigned;
assigned = function () {};
print("function", namedFunction.name, namedFunction.length, anonymousName.name, assigned.name, literal.name);

// Methods, getters and calls on what is not a function.
try {
	var plain = {};
	plain.missing();
} catch (e) {
	print("missing-method", errorName(e));
}
try {
	new namedFunction.name();
} catch (e) {
	print("not-constructor", errorName(e));
}
try {
	null[0] = 1;
} catch (e) {
	print("null-write", errorName(e));
}
