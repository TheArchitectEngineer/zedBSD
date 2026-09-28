// ws074-p027: regular expressions (literals, RegExp, exec, and String's match, replace, replaceAll, search, split).
var r = /a(b+)c/g;
print(r.source, r.flags, r.global, r.ignoreCase, r.lastIndex, String(r));
print(JSON.stringify("xabbcyabcz".match(r)), r.lastIndex);
var m = /(\d+)-(\d+)/.exec("tel 123-456 end");
print(JSON.stringify(m), m.index, m.input, m.groups);
print(JSON.stringify("a,b,,c".split(",")), JSON.stringify("a1b22c".split(/\d+/)), JSON.stringify("abc".split("")));
print(JSON.stringify("x-y_z".split(/([-_])/)), JSON.stringify("a.b.c".split(".", 2)), JSON.stringify("".split(/x/)));
print("John Smith".replace(/(\w+)\s(\w+)/, "$2, $1"), "aaa".replace(/a/g, function (x, i) { return i; }));
print("x".replace("x", "$&$&"), "abc".replaceAll("b", "[$&]"), "a-b-c".replaceAll("-", "+"), "abc".replace("b", "$`|$'"));
print("abc".replace(/(b)/, "$1$1$2$$"), "abcdefghijk".replace(/(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)/, "$11-$10-$01"));
print("hello world".search(/o/), "hello".search("l"), "abc".search(/z/));
print(/^\s*$/.test("   "), /[^a-z]/i.test("ABC"), /[a-z]+/ig.exec("123ABC")[0]);
var n = /(?<year>\d{4})-(?<month>\d\d)/.exec("2026-09");
print(n.groups.year, n.groups.month, "2026-09".replace(/(?<y>\d+)-(?<m>\d+)/, "$<m>/$<y>"));
print(/(?<=\$)\d+/.exec("cost $42")[0], /(?<!\$)\b\d+/.exec("$42 7")[0], /(?=(\d))\d\d/.exec("x12")[1]);
print(/(a)|b/.exec("b")[1], /(?:ab)*c/.exec("ababc")[0], /a{2,3}/.exec("aaaa")[0], /a{2,3}?/.exec("aaaa")[0]);
print(String(/(z)((a+)?(b+)?(c))*/.exec("zaacbbbcac")), String(/(a*)+/.exec("b")), String(/(a|ab)(c|bcd)(d*)/.exec("abcd")));
print(/\u{1F600}/u.test("😀"), /^.$/u.test("😀"), /^.$/.test("😀"), /^.$/s.test("\n"));
print(/\bfoo\b/.test("a foo b"), /(\w)\1/.exec("abccd")[0], /(a)\1/i.test("aA"), /^b/m.test("a\nb"), /a$/m.test("a\nb"));
var y = /o/y; y.lastIndex = 4;
print(y.test("hello"), y.lastIndex, y.test("hello"), y.lastIndex);
print(RegExp("a", "gi").flags, new RegExp(/ab/g).source, new RegExp(/ab/g, "i").flags, RegExp.prototype.source, RegExp.prototype.global);
print(new RegExp("a/b").source, String(new RegExp("")), RegExp(r) === r, new RegExp(r) === r);
print(/[\d-z]/.test("-"), /\x41B\cJ/.test("AB\n"), /[\b]/.test("\b"), /\0/.test("\0"), /a{/.test("a{"), /]/.test("]"));
print(/\w+@\w+\.\w+/.exec("mail me@host.com now")[0], /^[A-Z][a-z]*$/.test("Hello"), /colou?r/.test("color"));
var d = /(?<a>x)(y)?/d.exec("zxw");
print(JSON.stringify(d.indices), JSON.stringify(d.indices.groups));
var all = [];
var g = /\d/g;
var found;
while ((found = g.exec("a1b2c3")) !== null)
	all.push(found[0] + "@" + found.index);
print(all.join(","), g.lastIndex);
print("aaa".match(/x/g), JSON.stringify("aXbX".match(/x/gi)), JSON.stringify("".match(/^/g)));
try { new RegExp("("); } catch (e) { print(e.name); }
try { new RegExp("a", "gg"); } catch (e) { print(e.name); }
try { eval("/(/"); } catch (e) { print(e.name); }
try { "a".replaceAll(/a/, "b"); } catch (e) { print(e.name); }
try { RegExp.prototype.exec.call({}, "a"); } catch (e) { print(e.name); }
print(Object.prototype.toString.call(/x/), typeof /x/, /x/ instanceof RegExp, RegExp.length, RegExp.name);
