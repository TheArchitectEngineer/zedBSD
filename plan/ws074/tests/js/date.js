// ws074-p076: Date (the constructor, the getters and setters, the string forms and Date.parse).
// Every line prints the same in any time zone: UTC forms, or local values read back in local time.
var d = new Date(2020, 1, 29, 13, 45, 7, 123);
print(d.getFullYear(), d.getMonth(), d.getDate(), d.getDay(), d.getHours(), d.getMinutes(), d.getSeconds(), d.getMilliseconds());
print(d.toDateString(), d.toString().slice(0, 24), d.toTimeString().slice(0, 8), typeof Date(), typeof Date.now());
print(d.getTimezoneOffset() === (d.getTime() - Date.UTC(2020, 1, 29, 13, 45, 7, 123)) / 60000);
var u = new Date(Date.UTC(2000, 0, 1, 2, 3, 4, 5));
print(u.getTime(), u.toISOString(), u.toUTCString(), u.toJSON(), JSON.stringify({ when: u }));
print(u.getUTCFullYear(), u.getUTCMonth(), u.getUTCDate(), u.getUTCDay(), u.getUTCHours(), u.getUTCMinutes(), u.getUTCSeconds(), u.getUTCMilliseconds());
print(Date.UTC(2000, 0), Date.UTC(99, 11, 31), Date.UTC(2020, 13, 1), Date.UTC(2020, 0, 0), Date.UTC(-1, 0), Date.UTC(275760, 8, 13));
print(new Date(0).toISOString(), new Date(8.64e15).toISOString(), new Date(-8.64e15).toISOString(), new Date(8.64e15 + 1).getTime());
print(new Date(Date.UTC(-1, 0)).toISOString(), new Date(Date.UTC(10000, 0)).toISOString(), new Date(Date.UTC(0, 0)).getUTCFullYear());
var s = new Date(0);
print(s.setUTCFullYear(2001, 5, 7), s.setUTCMonth(0), s.setUTCDate(31), s.setUTCHours(23, 59, 59, 999), s.toISOString());
print(s.setUTCMinutes(0), s.setUTCSeconds(30, 250), s.setUTCMilliseconds(1000), s.toISOString(), s.setTime(86400000), s.toISOString());
var l = new Date(2021, 0, 31);
l.setMonth(1);
print(l.getMonth(), l.getDate());
l.setDate(0);
print(l.getMonth(), l.getDate());
l.setHours(25, 61, 61, 1001);
print(l.getDate(), l.getHours(), l.getMinutes(), l.getSeconds(), l.getMilliseconds());
l.setFullYear(1999);
print(l.getFullYear(), l.getYear(), typeof l.setYear(5), l.getFullYear());
var bad = new Date(NaN);
print(bad.getTime(), String(bad), bad.getFullYear(), bad.setDate(1), bad.setFullYear(2000) === new Date(2000, 0, 1).getTime());
print(JSON.stringify(new Date(NaN)), new Date("nonsense").getTime(), new Date(2020, 0, 1, NaN).getTime());
try { new Date(NaN).toISOString(); } catch (e) { print(e.name); }
try { Date.prototype.getTime.call({}); } catch (e) { print(e.name); }
try { Date.prototype.getTime(); } catch (e) { print(e.name); }
print(Date.parse("2000-01-01T00:00:00Z"), Date.parse("2000-01-01"), Date.parse("2000-01"), Date.parse("2000"), Date.parse("+002000-01-01T00:00:00.000Z"));
print(Date.parse("2000-01-01T09:00:00+09:00"), Date.parse("2000-01-01T00:00:00.5Z"), Date.parse("2000-01-01T24:00:00Z"), Date.parse("2000-02-30"));
print(Date.parse("2000-01-01T00:00") === new Date(2000, 0, 1).getTime(), Date.parse("-000000-01-01T00:00:00Z"), Date.parse("2000-13-01"));
print(Date.parse("Sat, 01 Jan 2000 00:00:00 GMT"), Date.parse("Thu Jan 01 1970 09:00:00 GMT+0900 (Japan Standard Time)"));
print(Date.parse("Jan 1 2000 00:00:00 GMT+0100"), Date.parse("1 January 2000 00:00 UTC"), Date.parse("2000/01/02 03:04:05 GMT"));
print(Date.parse("2000/01/02 03:04:05") === new Date(2000, 0, 2, 3, 4, 5).getTime(), Date.parse("1/2/2000") === new Date(2000, 0, 2).getTime());
print(Date.parse("January 2, 2000") === new Date(2000, 0, 2).getTime(), Date.parse("Feb 3, 2001 4:05 PM") === new Date(2001, 1, 3, 16, 5).getTime());
print(Date.parse("12/31/99 11:59:59 pm") === new Date(1999, 11, 31, 23, 59, 59).getTime(), Date.parse(u.toString()) === u.getTime() - 5, Date.parse(u.toUTCString()) === u.getTime() - 5);
print(new Date(u) - u, new Date(u.getTime()).getTime() === u.getTime(), new Date("2000-01-01T02:03:04.005Z") - u, u < new Date(u.getTime() + 1));
print(typeof (u + 1), (u + 1).slice(-1), u - 1, +u, u.valueOf() === u.getTime(), Object.prototype.toString.call(u));
print(Date.length, Date.name, Date.prototype.constructor === Date, Date.prototype.toGMTString === Date.prototype.toUTCString, Date.UTC.length, Date.prototype.setHours.length);
print(new Date(2020, 0).getTime() === new Date(2020, 0, 1, 0, 0, 0, 0).getTime(), new Date(99, 0).getFullYear(), new Date(Date.UTC(1970, 0, 1)).getUTCDay());
