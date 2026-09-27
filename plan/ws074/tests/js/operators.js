// ws074-p025: the operators and the conversions they make.

// Arithmetic, with numbers that print the same in every engine.
print("arith", 7 + 2, 7 - 2, 7 * 2, 7 / 2, 7 % 2, -7 % 2, 2 ** 10, 2 ** -1, 1 / 0, -1 / 0, 0 / 0);
print("int32", 2147483647 + 1, -2147483648 - 1, 65536 * 65536, 3 * -0 === 0, 1 / (3 * -0));

// Bitwise and shifts on int32 and uint32.
print("bits", 5 & 3, 5 | 3, 5 ^ 3, ~5, 1 << 31, -8 >> 1, -8 >>> 28, 1 << 33, 4294967295 | 0);

// Unary operators.
print("unary", -"3", +"", +"  12  ", +"0x1f", +"1e3", +"abc", !0, !"a", typeof void 0);

// String concatenation and conversion of the other side.
print("concat", "a" + 1, 1 + "a", "a" + true + null + undefined, 1 + 2 + "3", "1" + 2 + 3);

// Relations: numbers, strings by code unit, mixed, NaN.
print("less", 1 < 2, "a" < "b", "B" < "a", "10" < "9", "10" < 9, NaN < 1, 1 <= NaN, 2 >= 2, 3 > "2");

// Equality: loose and strict.
print("loose", 1 == "1", 0 == "", null == undefined, null == 0, undefined == 0, true == 1, "1" == true, NaN == NaN);
print("strict", 1 === 1.0, "a" === "a", null === undefined, -0 === 0, NaN === NaN);
print("not", 1 != "2", 1 !== "1");

// typeof of every kind of value.
print("typeof", typeof 1, typeof "s", typeof true, typeof undefined, typeof null, typeof {}, typeof [], typeof print, typeof function () {});

// ToPrimitive through valueOf and toString.
var valued = { valueOf: function () { return 42; } };
var named = { toString: function () { return "named"; } };
var both = { valueOf: function () { return 1; }, toString: function () { return "two"; } };
print("primitive", valued + 1, valued * 2, named + "!", both + "", both * 3, valued > 41, both == 1);

// The order conversions happen in: left before right.
var trace = "";
var left = { valueOf: function () { trace += "L"; return 1; } };
var right = { valueOf: function () { trace += "R"; return 2; } };
left + right;
left < right;
left > right;
left - right;
print("order", trace);

// Compound assignments on names and properties, with the property's object read once.
var x = 10;
x += 5;
x -= 3;
x *= 2;
x /= 4;
x %= 4;
x <<= 3;
x >>= 1;
x |= 1;
x &= 13;
x ^= 7;
x **= 2;
var reads = 0;
var box = { value: 1 };
function getBox() { reads++; return box; }
getBox().value += 10;
getBox()["value"] *= 2;
print("compound", x, box.value, reads);

// ++ and -- on names and properties, prefix and postfix.
var counter = "5";
var post = counter++;
var pre = ++counter;
var object = { n: 1 };
var postProperty = object.n--;
var preProperty = --object["n"];
print("update", post, pre, counter, postProperty, preProperty, object.n);

// Logical assignments short-circuit.
var calls = 0;
function value() { calls++; return "set"; }
var a = 1;
var b = 0;
var c = null;
a ||= value();
b ||= value();
a &&= value();
c ??= value();
print("logical-assign", a, b, c, calls);

// The comma operator and the conditional operator.
var comma = (1, 2, 3);
print("comma", comma, true ? "yes" : "no", 0 ? "yes" : "no");

// in and instanceof.
function Shape() {}
var shape = new Shape();
print("in", "value" in box, "missing" in box, 0 in [5], 1 in [5], "length" in []);
print("instanceof", shape instanceof Shape, box instanceof Shape);
