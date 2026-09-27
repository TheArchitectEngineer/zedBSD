// ws074-p025: bindings, closures and environments.

// A closure keeps its variable after the function returns, each call its own.
function makeCounter(start) {
	var count = start;
	return function () {
		count++;
		return count;
	};
}
var first = makeCounter(10);
var second = makeCounter(100);
first();
print("counters", first(), second(), first());

// Three levels: the innermost reaches past a function without an environment of its own.
function outer(a) {
	var b = a * 2;
	function middle(c) {
		return function inner(d) {
			return a + b + c + d;
		};
	}
	return middle(3);
}
print("levels", outer(1)(4));

// A captured parameter is copied into the environment, and writes go there.
function captureParameter(x) {
	var get = function () { return x; };
	x = x + 1;
	return get();
}
print("parameter", captureParameter(41));

// Closures made in a loop share the one var.
var functions = [];
for (var i = 0; i < 3; i++)
	functions[i] = function () { return i; };
print("loop", functions[0](), functions[1](), functions[2]());

// A named function expression sees itself; assigning its name does nothing in sloppy code.
var factorial = function fact(n) {
	fact = null;
	return n <= 1 ? 1 : n * fact(n - 1);
};
print("callee", factorial(5), typeof fact);

// Function declarations are hoisted, vars are undefined before their assignment.
print("hoisted", hoisted(), typeof later, later);
function hoisted() { return "yes"; }
var later = 1;

// arguments: its length, its elements, and callee in sloppy code.
function args() {
	return arguments.length + ":" + arguments[0] + ":" + arguments[2] + ":" + (arguments.callee === args);
}
print("arguments", args("a", "b", "c", "d"), args());

// arguments captured by a nested function.
function outerArguments() {
	return (function (arguments_holder) { return arguments_holder.length; })(arguments) + (function () { return arguments.length; })();
}
print("nested arguments", outerArguments(1, 2, 3));

// A catch parameter is a binding of its own, also when captured.
var e = "outer";
try {
	throw "inner";
} catch (e) {
	var fromCatch = function () { return e; };
}
print("catch", e, fromCatch());

// this: the global object in sloppy functions, the object for a method, undefined in strict code.
var globalSelf = this;
function sloppyThis() { return this === globalSelf; }
function strictThis() { "use strict"; return this; }
var holder = { method: function () { return this === holder; } };
print("this", sloppyThis(), strictThis(), holder.method());

// Recursion through a global function and through a captured one.
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
var even = function (n) { return n === 0 ? true : odd(n - 1); };
var odd = function (n) { return n === 0 ? false : even(n - 1); };
print("recursion", fib(20), even(100), odd(7));

// Many closures survive collections.
var keep = [];
for (var j = 0; j < 20000; j++)
	keep[j] = makeCounter(j);
var total = 0;
for (var k = 0; k < 20000; k += 1000)
	total += keep[k]();
print("many", total);
