// ws074-p025: what strict mode code does differently.
"use strict";

function errorName(e) {
	var name = "";
	if (typeof e === "object")
		return e.name;
	for (var i = 0; i < e.length && e[i] !== ":"; i++)
		name += e[i];
	return name;
}

function attempt(label, action) {
	try {
		action();
		print(label, "no error");
	} catch (e) {
		print(label, errorName(e));
	}
}

// An assignment to a name nothing declares.
attempt("undeclared", function () { undeclaredName = 1; });

// this is not replaced.
function whatIsThis() { return this; }
print("this", whatIsThis() === undefined);

// Assignments that sloppy code would ignore.
var readOnly = { get value() { return 1; } };
attempt("getter-only", function () { readOnly.value = 2; });
attempt("primitive", function () { var s = "text"; s.property = 1; });
attempt("array-length", function () { var a = []; a.length = 1.5; });

// A deletion that cannot happen.
attempt("delete-length", function () { var a = [1]; delete a.length; });

// A named function expression's own name cannot be assigned.
attempt("callee", function () {
	var f = function named() { named = 1; };
	f();
});

// Nothing wrong: the ordinary cases still work.
var counter = 0;
attempt("ordinary", function () { counter++; var o = {}; o.x = 1; delete o.x; });
print("counter", counter);

// The arguments object of strict code.
function strictArguments() { return arguments.length + ":" + arguments[0]; }
print("arguments", strictArguments(1));
