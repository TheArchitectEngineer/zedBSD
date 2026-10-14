// ArrayBuffer, typed arrays and DataView (ws074-p230).
function t(f) { try { return String(f()); } catch (e) { return e.name; } }
var b = new ArrayBuffer(8);
print(b.byteLength, ArrayBuffer.isView(b), ArrayBuffer.isView(new Uint8Array(1)), Object.prototype.toString.call(b));
var u = new Uint8Array(b);
u[0] = 257; u[1] = -1; u[2] = 3.9; u[100] = 5;
print(u.length, u[0], u[1], u[2], u[100], u.byteLength, u.byteOffset, u.buffer === b, Object.prototype.toString.call(u));
var c = new Uint8ClampedArray([300, -5, 1.5, 2.5, NaN]);
print(c.join());
var i16 = new Int16Array(b, 2, 2); i16[0] = 40000; i16[1] = -2;
print(i16.join(), u[2], u[3], u[4], u[5]);
var f = new Float32Array([1.5, 0.1, -3]); print(f[0], f[1] === Math.fround ? "x" : "y", f.length, f.BYTES_PER_ELEMENT, Float32Array.BYTES_PER_ELEMENT);
var d = new Float64Array(2); d[0] = 0.1; d[1] = 1e300; print(d[0], d[1]);
var i32 = new Int32Array([1, 2, 3, 4, 5]);
print(i32.subarray(1, 3).join(), i32.slice(-2).join(), i32.map(function (x) { return x * 2; }).join(), i32.filter(function (x) { return x % 2; }).join());
var s = i32.subarray(1, 3); s[0] = 99; print(i32[1], s.byteOffset);
print(Array.from(i32).join(), Array.prototype.slice.call(i32, 1, 3).join(), [].concat(Array.from(i32)).length);
var sum = 0; i32.forEach(function (x) { sum += x; }); print(sum, i32.reduce(function (a, x) { return a + x; }, 0), i32.indexOf(3), i32.includes(99));
var x = new Uint32Array([3, 1, 2, 100, 20]); x.sort(); print(x.join(), x.reverse().join(), x.fill(7, 1, 3).join());
x.set([9, 8], 3); print(x.join()); x.set(new Uint8Array([1, 2]), 0); print(x.join());
print(t(function () { x.set([1, 2, 3], 4); }), t(function () { return new Uint8Array(-1); }), t(function () { return Uint8Array(2); }), t(function () { return new Int32Array(b, 1); }));
var dv = new DataView(b); dv.setInt16(0, -2); dv.setUint32(4, 0xdeadbeef, true); dv.setFloat64(0, 1.5);
print(dv.getInt8(0), dv.getUint8(1), dv.getFloat64(0), dv.getUint32(4, true), dv.getInt16(4), dv.byteLength);
var bi = new BigInt64Array(2); bi[0] = -5n; bi[1] = 2n ** 63n; print(bi[0], bi[1], new BigUint64Array([-1n])[0], t(function () { bi[0] = 1; }));
print(Uint8Array.from([1, 2, 3], function (v) { return v * 2; }).join(), Int8Array.of(1, 130).join(), new Uint8Array(new Set([5, 6])).join(), new Uint8Array({ length: 2, 0: 7, 1: 8 }).join());
var keys = []; for (var k in new Uint8Array(2)) keys.push(k); print(keys.join(), Object.keys(new Int8Array(3)).join(), JSON.stringify(new Uint8Array([1, 2])));
print(new Uint16Array([1, 2, 3]).toString(), [...new Uint8Array([4, 5])].join(), Object.getPrototypeOf(Uint8Array) === Object.getPrototypeOf(Int8Array), new Uint8Array(3) instanceof Uint8Array);
