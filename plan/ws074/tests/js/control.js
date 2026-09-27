// ws074-p025: statements and the ways out of them.

function errorName(e) {
	var name = "";
	if (typeof e === "object")
		return e.name;
	for (var i = 0; i < e.length && e[i] !== ":"; i++)
		name += e[i];
	return name;
}

// if, else if, else.
function classify(n) {
	if (n < 0)
		return "negative";
	else if (n === 0)
		return "zero";
	else
		return "positive";
}
print("if", classify(-1), classify(0), classify(5));

// while, do-while, for with continue and break.
var log = "";
var n = 0;
while (n < 10) {
	n++;
	if (n % 2 === 0)
		continue;
	if (n > 7)
		break;
	log += n;
}
do {
	log += "d";
} while (false);
for (var i = 0, j = 10; i < j; i += 3, j--)
	log += "[" + i + "," + j + "]";
print("loops", log);

// Labeled break and continue across nested loops.
log = "";
outer: for (var a = 0; a < 4; a++) {
	inner: for (var b = 0; b < 4; b++) {
		if (b === 2)
			continue outer;
		if (a === 3)
			break outer;
		log += a + "" + b + " ";
	}
}
block: {
	log += "in-block ";
	if (log.length > 0)
		break block;
	log += "never";
}
print("labels", log);

// switch: fall through, a default in the middle, strict comparison.
function sw(value) {
	var out = "";
	switch (value) {
	case 1:
		out += "one ";
	case 2:
		out += "two ";
		break;
	default:
		out += "default ";
	case "3":
		out += "string-three ";
		break;
	case 4:
		out += "four ";
	}
	return out;
}
print("switch", sw(1), "|", sw(2), "|", sw(3), "|", sw("3"), "|", sw(4));

// try, catch and finally, and what finally does to each way out.
function order() {
	var steps = "";
	try {
		steps += "try ";
		throw "boom";
	} catch (e) {
		steps += "catch:" + e + " ";
	} finally {
		steps += "finally";
	}
	return steps;
}
print("try", order());

function returnThroughFinally() {
	var steps = [];
	function run() {
		try {
			steps[steps.length] = "try";
			return "from-try";
		} finally {
			steps[steps.length] = "finally";
		}
	}
	var value = run();
	return value + " " + steps.length;
}
print("return-finally", returnThroughFinally());

function finallyOverrides() {
	try {
		return "try";
	} finally {
		return "finally";
	}
}
print("finally-overrides", finallyOverrides());

function breakThroughFinally() {
	var steps = "";
	for (var k = 0; k < 3; k++) {
		try {
			try {
				if (k === 1)
					continue;
				if (k === 2)
					break;
				steps += "body" + k + " ";
			} finally {
				steps += "inner" + k + " ";
			}
		} finally {
			steps += "outer" + k + " ";
		}
	}
	return steps;
}
print("jumps-finally", breakThroughFinally());

function rethrow() {
	try {
		try {
			throw "first";
		} finally {
			print("finally-before-rethrow");
		}
	} catch (e) {
		return "caught " + e;
	}
}
print("rethrow", rethrow());

// An exception crosses function frames to the nearest handler.
function deep(level) {
	if (level === 0)
		throw "bottom";
	return deep(level - 1);
}
try {
	deep(50);
} catch (e) {
	print("unwind", e);
}

// Errors of the engine are catchable.
try {
	undefined.property;
} catch (e) {
	print("engine-error", errorName(e));
}
try {
	notDeclaredAnywhere;
} catch (e) {
	print("reference-error", errorName(e));
}
try {
	var notAFunction = 1;
	notAFunction();
} catch (e) {
	print("call-error", errorName(e));
}

// Runaway recursion is a RangeError that can be caught.
function forever(n) { return forever(n + 1) + 1; }
try {
	forever(0);
} catch (e) {
	print("stack", errorName(e));
}

// The conditional and logical operators pick their values.
print("logical", 0 || "b", 1 && "c", null || undefined, "" && "never", null ?? "default", 0 ?? "zero-stays");
