// ws074-p046: Array, String and JSON.

// Array: the methods that make new arrays or read them.
var a = [3, 1, 2];
print("map", a.join("-"), a.map(function (x) { return x * 2; }).join(), a.filter(function (x) { return x > 1; }).length);
print("sort", [5, 1, 10, 2].sort().join(), [5, 1, 10, 2].sort(function (x, y) { return x - y; }).join(), [undefined, 3, , 1].sort().length);
var stable = [{ k: 1, v: "a" }, { k: 0, v: "b" }, { k: 1, v: "c" }, { k: 0, v: "d" }];
print("stable", stable.sort(function (x, y) { return x.k - y.k; }).map(function (e) { return e.v; }).join(""));
var b = [1, 2, 3, 4, 5];
print("splice", b.splice(1, 2, "a", "b", "c").join(), b.join(), b.length);
print("search", b.indexOf("b"), b.includes(NaN), [NaN].includes(NaN), [NaN].indexOf(NaN), b.slice(-2).join(), b.reverse().join());
print("make", [1, [2, [3, [4]]]].flat(2).join(), [1, 2].concat([3], 4).join(), Array.isArray([]), Array(3).length, Array.of(7).join());
print("reduce", [1, 2, 3].reduce(function (s, x) { return s + x; }), [1, 2, 3].reduceRight(function (s, x) { return s + x; }, ""), String([1, [2, 3]]), [, 1].join(), b.at(-1));
var c = [];
c.push(1, 2);
c.unshift(0);
print("ends", c.join(), c.shift(), c.pop(), c.join());
print("copies", [3, 2, 1].toSorted().join(), [1, 2, 3].with(1, 9).join(), [1, 2, 3].toReversed().join(), [1, 2, 3, 4].toSpliced(1, 2).join());
print("find", [1, 2, 3].findLast(function (x) { return x < 3; }), [1, 2, 3].findIndex(function (x) { return x > 1; }), [1, 2, 3].find(function (x) { return x > 5; }));
print("every", [1, 2].every(function (x) { return x > 0; }), [1, 2].some(function (x) { return x > 1; }), [0, 0, 0].fill(7, 1).join(), [1, 2, 3, 4, 5].copyWithin(0, 3).join());
print("from", Array.from({ length: 2, 0: "x", 1: "y" }).join(), Array.from({ length: 3 }, function (v, i) { return i * i; }).join(), [1, [2]].flatMap(function (x) { return x; }).length);
var holes = [1, , 3];
print("holes", holes.length, 1 in holes, holes.map(function (x) { return x; }).length, Object.keys(holes).join());
var grow = [];
grow[5] = 1;
grow.length = 2;
print("length", grow.length, grow[5], Array.prototype.join.call({ length: 2, 0: "p", 1: "q" }, "+"));

// String: case, search, slices and padding.
var s = "Hello, World";
print("case", s.toUpperCase(), s.toLowerCase(), "straße".toUpperCase(), "ΑΣ ΣΑΣ".toLowerCase(), "İ".toLowerCase().length);
print("search", s.indexOf("o"), s.lastIndexOf("o"), s.includes("World"), s.startsWith("Hell"), s.endsWith("ld"), s.slice(-5), s.substring(5, 0), s.substr(-5, 3));
print("pad", "  x  ".trim() + "|", "ab".repeat(3), "5".padStart(3, "0"), "5".padEnd(3, "-"), s.charAt(4), s.charCodeAt(0), "😀".codePointAt(0), "😀".length);
print("make", String.fromCharCode(72, 105), String.fromCodePoint(0x1F600).length, "a,b".concat("c", 1), "b".localeCompare("a"), s.at(-1), String(123), new String("x").length);
print("wrapper", typeof new String("q"), Object.prototype.toString.call(new String("")), "x" + new String("y"), new String("ab")[1], Object.keys(new String("ab")).join());
print("trim", " ﻿x ".trim().length, " a ".trimStart() + "|", " a ".trimEnd() + "|", "abc".at(5), "".padStart(2, ""));
print("wellformed", "a\ud800".isWellFormed(), "a\ud800".toWellFormed().charCodeAt(1), "😀".isWellFormed());
print("positions", "abc".startsWith("b", 1), "abc".endsWith("b", 2), "abc".includes("a", 1), "abcabc".lastIndexOf("c", 4));
try {
	"a".repeat(-1);
} catch (e) {
	print("repeat", e.name);
}

// JSON: parse with a reviver, and stringify with a replacer and indentation.
var o = JSON.parse('{"a": [1, 2.5, -3e2, true, null], "b": {"c": "x\\u0041\\n"}, "__proto__": 1}');
print("parse", o.a.length, o.a[2], o.b.c.length, Object.keys(o).join());
print("stringify", JSON.stringify(o), JSON.stringify([undefined, function () {}, NaN, "q\"\\"]), JSON.stringify({ u: undefined, d: 1 }));
print("indent", JSON.stringify({ a: 1, b: [1, 2], e: {}, f: [] }, null, 2));
print("indent-string", JSON.stringify([1, { x: 2 }], null, "--"));
print("replacer", JSON.stringify({ a: 1, b: 2, c: 3 }, ["a", "c"]), JSON.stringify({ a: 1 }, function (k, v) { return typeof v === "number" ? v * 10 : v; }));
print("reviver", JSON.parse("[1,2,3]", function (k, v) { return typeof v === "number" ? v + 1 : v; }).join());
var cyc = {};
cyc.self = cyc;
try {
	JSON.stringify(cyc);
} catch (e) {
	print("cycle", e.name);
}
try {
	JSON.parse("{bad}");
} catch (e) {
	print("bad", e.name);
}
print("special", JSON.stringify("\ud800"), JSON.stringify({ toJSON: function () { return 5; } }), JSON.stringify(undefined), JSON.stringify(new Number(3)), JSON.stringify(-0));
print("numbers", JSON.stringify([1e21, 0.1, -1.5e-7, Infinity]), JSON.parse("-0") === 0, JSON.parse(" 1e2 "));
