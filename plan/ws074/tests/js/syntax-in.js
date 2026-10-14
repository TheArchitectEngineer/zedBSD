// The in operator inside a function in a for statement's initializer (ws074-p230).
var k = { a: 1 };
for (var f = function (e) { return e in k ? 1 : 0; }, i = 0; i < 1; i++) print(f("a"), f("b"));
for (var p = (function (e) { return "a" in k; })(1), j = 0; j < 1; j++) print(p);
for (var q = [1 in { 1: 1 }], j2 = 0; j2 < 1; j2++) print(q[0]);
for (var key in k) print(key);
