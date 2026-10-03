// ws074-p086: generators, promises, microtasks and async functions.

// A generator: parameters at the call, the body at the first next, sent values, return and throw.
function* counter(start) {
	print("counter starts at", start);
	var sent = yield start;
	print("counter got", sent);
	try {
		yield start + 1;
		yield start + 2;
	} finally {
		print("counter's finally");
	}
	return "end";
}
var it = counter(10);
print("made the generator");
var step = it.next("ignored");
print(step.value, step.done);
step = it.next("hello");
print(step.value, step.done);
step = it.return("early");
print(step.value, step.done);
step = it.next();
print(step.value, step.done);

// throw into a generator, caught inside it.
function* catcher() {
	while (true) {
		try {
			yield "waiting";
		} catch (e) {
			print("caught in the generator:", e);
		}
	}
}
var c = catcher();
c.next();
print(c.throw("oops").value);
try {
	counter(0).throw(new Error("before start"));
} catch (e) {
	print("thrown out:", e.message);
}

// Generators in objects and classes, with this and arguments.
var holder = {
	base: 5,
	*values() {
		yield this.base;
		yield arguments.length;
	}
};
var v = holder.values(1, 2, 3);
print(v.next().value, v.next().value, v.next().done);
class Box {
	constructor(x) { this.x = x; }
	*twice() { yield this.x; yield this.x * 2; }
	static *range(n) { for (var i = 0; i < n; i++) yield i; }
}
var t = new Box(21).twice();
print(t.next().value, t.next().value);
var r = Box.range(3), out = [];
for (var s = r.next(); !s.done; s = r.next())
	out.push(s.value);
print(out.join(","));
print(typeof counter, typeof it.next, Object.getPrototypeOf(counter) === Object.getPrototypeOf(function* () {}));
print(Object.getPrototypeOf(it) === counter.prototype, Object.prototype.toString.call(it));
try {
	new counter();
} catch (e) {
	print("new of a generator:", e.name);
}

// Promises: the order of the microtasks.
print("sync 1");
Promise.resolve(1).then(function (x) { print("then A", x); });
new Promise(function (resolve) { print("executor runs now"); resolve(2); }).then(function (x) { print("then B", x); });
Promise.resolve().then(function () { print("then C"); }).then(function () { print("then C2"); });
print("sync 2");

// Chains, errors and finally.
Promise.resolve(3)
	.then(function (x) { return x * 2; })
	.then(function (x) { throw new RangeError("too big: " + x); })
	.then(function () { print("not reached"); })
	.catch(function (e) { print("chain caught", e.name, e.message); return "recovered"; })
	.finally(function () { print("chain finally"); })
	.then(function (x) { print("after finally", x); });

// A thenable, resolving with a promise, and the combinators.
var thenable = { then: function (resolve) { resolve("from a thenable"); } };
Promise.resolve(thenable).then(function (x) { print(x); });
new Promise(function (resolve) { resolve(Promise.resolve("nested")); }).then(function (x) { print(x); });
Promise.all([1, Promise.resolve(2), new Promise(function (r) { r(3); })]).then(function (xs) { print("all", xs.join(",")); });
Promise.all([Promise.resolve(1), Promise.reject(new Error("no"))]).catch(function (e) { print("all rejected", e.message); });
Promise.all([]).then(function (xs) { print("all of nothing", xs.length); });
Promise.race([new Promise(function () {}), Promise.resolve("fast")]).then(function (x) { print("race", x); });
Promise.allSettled([Promise.reject("bad"), "good"]).then(function (xs) {
	print("allSettled", xs[0].status, xs[0].reason, xs[1].status, xs[1].value);
});
Promise.any([Promise.reject(1), Promise.resolve("first good")]).then(function (x) { print("any", x); });
var p = Promise.resolve(7);
print(p instanceof Promise, Object.prototype.toString.call(p), Promise.resolve(p) === p);
try {
	Promise();
} catch (e) {
	print("Promise without new:", e.name);
}

// Async functions: they run to the first await, then in microtasks.
async function add(a, b) {
	print("add starts");
	var x = await a;
	var y = await Promise.resolve(b);
	print("add has", x, y);
	return x + y;
}
var sum = add(1, 2);
print("add returned a promise:", sum instanceof Promise);
sum.then(function (value) { print("sum", value); });

// await of a rejected promise throws where the await is; an async function's throw rejects its promise.
async function careful() {
	try {
		await Promise.reject(new TypeError("rejected"));
	} catch (e) {
		print("careful caught", e.message);
	}
	return "careful done";
}
careful().then(function (x) { print(x); });
async function failing() {
	await null;
	throw new SyntaxError("failed");
}
failing().catch(function (e) { print("failing rejected", e.name); });

// Async arrows, methods, this and a loop of awaits.
var waiter = {
	name: "waiter",
	async wait() {
		var parts = [];
		for (var i = 0; i < 3; i++)
			parts.push(await i);
		var tag = await (async () => this.name)();
		return tag + ":" + parts.join("");
	}
};
waiter.wait().then(function (x) { print(x); });
class Service {
	constructor() { this.ready = "ready"; }
	async load(n) { return this.ready + n; }
	static async make() { return new Service(); }
}
Service.make().then(function (s) { return s.load(1); }).then(function (x) { print("service", x); });
const later = async x => (await x) + 1;
later(41).then(function (x) { print("arrow", x); });
print(Object.getPrototypeOf(add) === Function.prototype, typeof add, add.length);
print("end of the script");
