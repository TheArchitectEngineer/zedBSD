/* Named export declarations keep the usual local binding and hoisting. */
print(ws074ExportFunction());
export function ws074ExportFunction() {
  return 17;
}

print(typeof ws074ExportVar);
export var ws074ExportVar = 19;
export let ws074ExportLet = 23;
export const ws074ExportConst = 29;
export class Ws074ExportClass {
  value() {
    return ws074ExportLet + ws074ExportConst;
  }
}

print(ws074ExportVar);
print(new Ws074ExportClass().value());
print(typeof globalThis.ws074ExportFunction);
print(typeof globalThis.ws074ExportVar);
print(typeof globalThis.ws074ExportLet);
print(typeof globalThis.ws074ExportConst);
print(typeof globalThis.Ws074ExportClass);
print(Function("return typeof ws074ExportFunction")());
print(Function("return typeof ws074ExportLet")());
