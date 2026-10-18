# Primary references / q54

- [Proxy constructor](https://tc39.es/ecma262/2026/multipage/reflection.html#sec-proxy-constructor): constructor-only invocation, actual ProxyCreate, inherits Function.prototype, no own prototype property.
- [Proxy.revocable](https://tc39.es/ecma262/2026/multipage/reflection.html#sec-proxy.revocable): ProxyCreate, fresh empty-name/zero-length builtin revoker, private captured proxy slot cleared before actual target/handler revocation, ordinary result with proxy then revoke default own data properties.
- [Proxy Get](https://tc39.es/ecma262/2026/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-get-p-receiver): outer get trap still invokes the target's descriptor method for invariant checks; wrapping a revoked inner Proxy succeeds, but a later Get cannot skip that revoked-target error. The initial test assertion41/42 is retained separately and was corrected from this primary order, with no product change.

Internal cleared-slot representation uses vm_function.data undefined as the private empty sentinel; this field is not a user-visible property. No script is added to vm_native_operations. Reflect remains outside this Queue's implementation scope.
