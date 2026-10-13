/* A module keeps all its top-level declarations away from the global object. */
print(typeof ws074ModuleHoistedVar);
print(ws074ModuleHoistedFunction());
var ws074ModuleHoistedVar = 3;
function ws074ModuleHoistedFunction() {
  return 5;
}

var ws074ModuleVar = 7;
let ws074ModuleLet = 11;
const ws074ModuleConst = 13;
function ws074ModuleFunction() {
  return ws074ModuleVar + ws074ModuleLet;
}
class Ws074ModuleClass {
  value() {
    return ws074ModuleConst;
  }
}

print(typeof this);
print(ws074ModuleFunction());
print(new Ws074ModuleClass().value());
print(typeof globalThis.ws074ModuleVar);
print(typeof globalThis.ws074ModuleLet);
print(typeof globalThis.ws074ModuleConst);
print(typeof globalThis.ws074ModuleFunction);
print(typeof globalThis.Ws074ModuleClass);
print(typeof globalThis.ws074ModuleHoistedVar);
print(typeof globalThis.ws074ModuleHoistedFunction);
print(Function("return typeof ws074ModuleVar")());
print(Function("return typeof ws074ModuleLet")());
print(Function("return typeof ws074ModuleFunction")());
