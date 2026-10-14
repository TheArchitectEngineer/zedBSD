// The web platform pieces that are written in the page's own language (ws074-p230).
// bind/prelude.c holds this text as a C string; this file is the readable source it is made from.
(function (parseURL, g) {
  'use strict';
  function def(o, name, f) { Object.defineProperty(o, name, { value: f, writable: true, configurable: true, enumerable: false }); }
  function hidden(o, name, v) { Object.defineProperty(o, name, { value: v, writable: true, configurable: true, enumerable: false }); }
  function tag(C, name) { Object.defineProperty(C.prototype, Symbol.toStringTag, { value: name, configurable: true }); }
  var KEY = {};

  // ---- AbortSignal and AbortController
  function abort(signal, reason) {
    if (signal.aborted) return;
    signal.aborted = true;
    signal.reason = reason === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : reason;
    var ev = new Event('abort');
    if (typeof signal.onabort === 'function') {
      try { signal.onabort.call(signal, ev); } catch (e) { setTimeout(function () { throw e; }, 0); }
    }
    signal.dispatchEvent(ev);
  }
  class AbortSignal extends EventTarget {
    constructor(key) {
      if (key !== KEY) throw new TypeError('Illegal constructor');
      super();
      this.aborted = false;
      this.reason = undefined;
      this.onabort = null;
    }
    throwIfAborted() { if (this.aborted) throw this.reason; }
    static abort(reason) { var s = new AbortSignal(KEY); abort(s, reason); return s; }
    static timeout(ms) {
      var s = new AbortSignal(KEY);
      setTimeout(function () { abort(s, new DOMException('signal timed out', 'TimeoutError')); }, ms);
      return s;
    }
    static any(signals) {
      var s = new AbortSignal(KEY), i, list = Array.from(signals);
      for (i = 0; i < list.length; i++) {
        if (list[i].aborted) { abort(s, list[i].reason); return s; }
      }
      list.forEach(function (x) { x.addEventListener('abort', function () { abort(s, x.reason); }); });
      return s;
    }
  }
  tag(AbortSignal, 'AbortSignal');
  class AbortController {
    constructor() { hidden(this, '_s', new AbortSignal(KEY)); }
    get signal() { return this._s; }
    abort(reason) { abort(this._s, reason); }
  }
  tag(AbortController, 'AbortController');
  def(g, 'AbortSignal', AbortSignal);
  def(g, 'AbortController', AbortController);

  // ---- URLSearchParams and URL
  function formEncode(s) {
    return encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, function (c) {
      return '%' + c.charCodeAt(0).toString(16).toUpperCase();
    });
  }
  function formDecode(s) {
    s = s.replace(/\+/g, ' ');
    try { return decodeURIComponent(s); } catch (e) {
      return s.replace(/(%[0-9a-fA-F]{2})+/g, function (m) { try { return decodeURIComponent(m); } catch (e2) { return m; } });
    }
  }
  class URLSearchParams {
    constructor(init) {
      hidden(this, '_l', []);
      hidden(this, '_u', null);
      if (init === undefined || init === null) return;
      if (typeof init === 'object' || typeof init === 'function') {
        if (typeof init[Symbol.iterator] === 'function') {
          var pair, p;
          for (pair of init) {
            p = Array.from(pair);
            if (p.length !== 2) throw new TypeError("Failed to construct 'URLSearchParams': Each pair must be an iterable [name, value] tuple");
            this._l.push([String(p[0]), String(p[1])]);
          }
        } else {
          Object.keys(init).forEach(function (k) { this._l.push([k, String(init[k])]); }, this);
        }
      } else {
        this._parse(String(init));
      }
    }
    _parse(s) {
      if (s.charAt(0) === '?') s = s.slice(1);
      this._l.length = 0;
      if (s === '') return;
      s.split('&').forEach(function (part) {
        if (part === '') return;
        var i = part.indexOf('=');
        this._l.push(i < 0 ? [formDecode(part), ''] : [formDecode(part.slice(0, i)), formDecode(part.slice(i + 1))]);
      }, this);
    }
    _changed() { if (this._u) this._u._setSearch(this.toString()); }
    get size() { return this._l.length; }
    append(k, v) { this._l.push([String(k), String(v)]); this._changed(); }
    delete(k, v) {
      k = String(k);
      this._l = this._l.filter(function (e) { return !(e[0] === k && (v === undefined || e[1] === String(v))); });
      this._changed();
    }
    get(k) { k = String(k); for (var i = 0; i < this._l.length; i++) if (this._l[i][0] === k) return this._l[i][1]; return null; }
    getAll(k) { k = String(k); return this._l.filter(function (e) { return e[0] === k; }).map(function (e) { return e[1]; }); }
    has(k, v) {
      k = String(k);
      return this._l.some(function (e) { return e[0] === k && (v === undefined || e[1] === String(v)); });
    }
    set(k, v) {
      k = String(k); v = String(v);
      var found = false;
      this._l = this._l.filter(function (e) {
        if (e[0] !== k) return true;
        if (found) return false;
        found = true; e[1] = v; return true;
      });
      if (!found) this._l.push([k, v]);
      this._changed();
    }
    sort() {
      var l = this._l.map(function (e, i) { return [e, i]; });
      l.sort(function (a, b) { return a[0][0] < b[0][0] ? -1 : a[0][0] > b[0][0] ? 1 : a[1] - b[1]; });
      this._l = l.map(function (x) { return x[0]; });
      this._changed();
    }
    forEach(f, thisArg) { this._l.slice().forEach(function (e) { f.call(thisArg, e[1], e[0], this); }, this); }
    keys() { return this._l.map(function (e) { return e[0]; })[Symbol.iterator](); }
    values() { return this._l.map(function (e) { return e[1]; })[Symbol.iterator](); }
    entries() { return this._l.map(function (e) { return [e[0], e[1]]; })[Symbol.iterator](); }
    toString() { return this._l.map(function (e) { return formEncode(e[0]) + '=' + formEncode(e[1]); }).join('&'); }
  }
  def(URLSearchParams.prototype, Symbol.iterator, URLSearchParams.prototype.entries);
  tag(URLSearchParams, 'URLSearchParams');

  var PARTS = ['href', 'origin', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash'];
  function reparse(url, text) {
    var r = parseURL(text);
    if (!r) return false;
    url._r = r;
    if (url._sp) url._sp._parse(r[9]);
    return true;
  }
  class URL {
    constructor(url, base) {
      var r = parseURL(String(url), base === undefined ? undefined : String(base));
      if (!r) throw new TypeError("Failed to construct 'URL': Invalid URL");
      hidden(this, '_r', r);
      hidden(this, '_sp', null);
    }
    static canParse(url, base) { return !!parseURL(String(url), base === undefined ? undefined : String(base)); }
    static parse(url, base) { try { return new URL(url, base); } catch (e) { return null; } }
    static createObjectURL() { return 'blob:' + location.origin + '/' + Math.random().toString(16).slice(2); }
    static revokeObjectURL() {}
    _setSearch(s) {
      var h = this._r[0], i = h.indexOf('#'), hash = i < 0 ? '' : h.slice(i), rest = i < 0 ? h : h.slice(0, i), q = rest.indexOf('?');
      if (q >= 0) rest = rest.slice(0, q);
      reparse(this, rest + (s ? '?' + s : '') + hash);
    }
    get href() { return this._r[0]; }
    set href(v) { if (!reparse(this, String(v))) throw new TypeError("Failed to set the 'href' property on 'URL': Invalid URL"); }
    get origin() { return this._r[1]; }
    get protocol() { return this._r[2]; }
    set protocol(v) { v = String(v).replace(/:.*$/, ''); reparse(this, v + ':' + this._r[0].slice(this._r[2].length)); }
    get username() { return this._r[3]; }
    set username(v) { this._auth(encodeURIComponent(String(v)), this._r[4]); }
    get password() { return this._r[4]; }
    set password(v) { this._auth(this._r[3], encodeURIComponent(String(v))); }
    _auth(u, p) {
      var h = this._r[0], m = /^([a-zA-Z][a-zA-Z0-9+.-]*:\/\/)([^\/?#]*@)?(.*)$/.exec(h);
      if (!m) return;
      reparse(this, m[1] + (u ? u + (p ? ':' + p : '') + '@' : '') + m[3]);
    }
    _host(host) {
      var h = this._r[0], m = /^([a-zA-Z][a-zA-Z0-9+.-]*:\/\/)([^\/?#@]*@)?([^\/?#]*)(.*)$/.exec(h);
      if (!m) return;
      reparse(this, m[1] + (m[2] || '') + host + m[4]);
    }
    get host() { return this._r[5]; }
    set host(v) { v = String(v); if (v) this._host(v); }
    get hostname() { return this._r[6]; }
    set hostname(v) { v = String(v); if (v) this._host(v + (this._r[7] ? ':' + this._r[7] : '')); }
    get port() { return this._r[7]; }
    set port(v) { v = String(v).replace(/[^0-9].*$/, ''); this._host(this._r[6] + (v ? ':' + v : '')); }
    get pathname() { return this._r[8]; }
    set pathname(v) {
      v = String(v);
      var h = this._r[0], m = /^([a-zA-Z][a-zA-Z0-9+.-]*:\/\/[^\/?#]*)([^?#]*)(.*)$/.exec(h);
      if (!m) return;
      reparse(this, m[1] + (v.charAt(0) === '/' ? '' : '/') + v.replace(/\?/g, '%3F').replace(/#/g, '%23') + m[3]);
    }
    get search() { return this._r[9]; }
    set search(v) { v = String(v); if (v.charAt(0) === '?') v = v.slice(1); this._setSearch(v); }
    get searchParams() {
      if (!this._sp) {
        var sp = new URLSearchParams(this._r[9]);
        sp._u = this;
        this._sp = sp;
      }
      return this._sp;
    }
    get hash() { return this._r[10]; }
    set hash(v) {
      v = String(v); if (v.charAt(0) === '#') v = v.slice(1);
      var h = this._r[0], i = h.indexOf('#');
      reparse(this, (i < 0 ? h : h.slice(0, i)) + (v ? '#' + v : ''));
    }
    toString() { return this._r[0]; }
    toJSON() { return this._r[0]; }
  }
  tag(URL, 'URL');
  def(g, 'URLSearchParams', URLSearchParams);
  def(g, 'URL', URL);

  // ---- Language additions
  if (!Object.fromEntries) {
    def(Object, 'fromEntries', function fromEntries(entries) {
      var o = {}, e;
      for (e of entries) o[e[0]] = e[1];
      return o;
    });
  }
  if (!String.prototype.matchAll) {
    def(String.prototype, 'matchAll', function matchAll(re) {
      var s = String(this), r, out = [], m;
      if (typeof re === 'string') r = new RegExp(re, 'g');
      else {
        if (re instanceof RegExp && !re.global) throw new TypeError('String.prototype.matchAll called with a non-global RegExp argument');
        r = new RegExp(re.source, re.flags); r.lastIndex = re.lastIndex;
      }
      while ((m = r.exec(s)) !== null) { out.push(m); if (m[0] === '') r.lastIndex++; }
      return out[Symbol.iterator]();
    });
  }
  if (!g.AggregateError) {
    class AggregateError extends Error {
      constructor(errors, message) {
        super(message);
        hidden(this, 'errors', Array.from(errors));
        hidden(this, 'name', 'AggregateError');
      }
    }
    def(g, 'AggregateError', AggregateError);
  }
  if (!Error.captureStackTrace) {
    def(Error, 'captureStackTrace', function captureStackTrace(o) {
      hidden(o, 'stack', String(o && o.name ? o.name + ': ' + o.message : o));
    });
  }
  if (!g.Reflect) {
    var R = {
      apply: function (f, t, a) { return Function.prototype.apply.call(f, t, a); },
      construct: function (C, a) { return new (Function.prototype.bind.apply(C, [null].concat(Array.from(a))))(); },
      defineProperty: function (o, k, d) { try { Object.defineProperty(o, k, d); return true; } catch (e) { return false; } },
      deleteProperty: function (o, k) { try { return delete o[k]; } catch (e) { return false; } },
      get: function (o, k) { return o[k]; },
      set: function (o, k, v) { try { o[k] = v; return true; } catch (e) { return false; } },
      has: function (o, k) { return k in o; },
      ownKeys: function (o) { return Object.getOwnPropertyNames(o).concat(Object.getOwnPropertySymbols(o)); },
      getPrototypeOf: function (o) { return Object.getPrototypeOf(o); },
      setPrototypeOf: function (o, p) { try { Object.setPrototypeOf(o, p); return true; } catch (e) { return false; } },
      isExtensible: function (o) { return Object.isExtensible(o); },
      preventExtensions: function (o) { Object.preventExtensions(o); return true; },
      getOwnPropertyDescriptor: function (o, k) { return Object.getOwnPropertyDescriptor(o, k); }
    };
    var Refl = {};
    Object.keys(R).forEach(function (k) { def(Refl, k, R[k]); });
    Object.defineProperty(Refl, Symbol.toStringTag, { value: 'Reflect', configurable: true });
    def(g, 'Reflect', Refl);
  }

  // ---- Intl (the formats of the page's locale, without locale data)
  if (!g.Intl) {
    var Intl_ = {};
    var locales = function (l) { return Array.isArray(l) ? (l[0] || 'ja-JP') : (l || 'ja-JP'); };
    function intlType(name, init, methods) {
      var C = function (l, o) {
        if (!(this instanceof C)) return new C(l, o);
        hidden(this, '_l', locales(l));
        hidden(this, '_o', o || {});
        if (init) init.call(this);
      };
      Object.defineProperty(C, 'name', { value: name });
      Object.keys(methods).forEach(function (k) { def(C.prototype, k, methods[k]); });
      def(C, 'supportedLocalesOf', function (l) { return Array.isArray(l) ? l : [l]; });
      return C;
    }
    var DateTimeFormat = intlType('DateTimeFormat', null, {
      format: function (d) { d = d === undefined ? new Date() : new Date(d); return this._o.timeStyle || this._o.hour ? d.toLocaleString() : d.toLocaleDateString(); },
      formatToParts: function (d) { return [{ type: 'literal', value: this.format(d) }]; },
      resolvedOptions: function () { return { locale: this._l, calendar: 'gregory', numberingSystem: 'latn', timeZone: 'UTC' }; }
    });
    var NumberFormat = intlType('NumberFormat', null, {
      format: function (n) { return Number(n).toLocaleString(this._l, this._o); },
      formatToParts: function (n) { return [{ type: 'integer', value: this.format(n) }]; },
      resolvedOptions: function () { return { locale: this._l, numberingSystem: 'latn', style: this._o.style || 'decimal' }; }
    });
    var Collator = intlType('Collator', null, {
      compare: function (a, b) { return String(a).localeCompare(String(b)); },
      resolvedOptions: function () { return { locale: this._l, usage: 'sort', sensitivity: 'variant' }; }
    });
    var PluralRules = intlType('PluralRules', null, {
      select: function (n) { return n === 1 ? 'one' : 'other'; },
      resolvedOptions: function () { return { locale: this._l }; }
    });
    var RelativeTimeFormat = intlType('RelativeTimeFormat', null, { format: function (v, unit) { return v + ' ' + unit; } });
    var ListFormat = intlType('ListFormat', null, { format: function (list) { return Array.from(list).join(', '); } });
    var Segmenter = intlType('Segmenter', null, {
      segment: function (s) { s = String(s); return Array.from(s).map(function (c, i) { return { segment: c, index: i, input: s }; }); }
    });
    [['DateTimeFormat', DateTimeFormat], ['NumberFormat', NumberFormat], ['Collator', Collator], ['PluralRules', PluralRules],
      ['RelativeTimeFormat', RelativeTimeFormat], ['ListFormat', ListFormat], ['Segmenter', Segmenter]].forEach(function (p) {
      def(Intl_, p[0], p[1]);
    });
    def(Intl_, 'getCanonicalLocales', function (l) { return l === undefined ? [] : Array.isArray(l) ? l : [l]; });
    def(Intl_, 'supportedValuesOf', function () { return []; });
    Object.defineProperty(Intl_, Symbol.toStringTag, { value: 'Intl', configurable: true });
    def(g, 'Intl', Intl_);
  }

  // ---- matchMedia, history, MessageChannel
  if (!g.matchMedia) {
    def(g, 'matchMedia', function matchMedia(q) {
      q = String(q);
      var m = {
        media: q, matches: false, onchange: null,
        addListener: function () {}, removeListener: function () {},
        addEventListener: function () {}, removeEventListener: function () {}, dispatchEvent: function () { return false; }
      };
      if (/^\(\s*prefers-color-scheme\s*:\s*light\s*\)$/.test(q)) m.matches = true;
      if (/^\(\s*(any-)?hover\s*:\s*hover\s*\)$/.test(q)) m.matches = true;
      if (/^\(\s*(any-)?pointer\s*:\s*fine\s*\)$/.test(q)) m.matches = true;
      return m;
    });
  }
  if (!g.history) {
    var hist = {
      length: 1, state: null, scrollRestoration: 'auto',
      pushState: function (s) { hist.state = s === undefined ? null : s; },
      replaceState: function (s) { hist.state = s === undefined ? null : s; },
      back: function () {}, forward: function () {}, go: function () {}
    };
    def(g, 'history', hist);
  }
  class MessagePort extends EventTarget {
    constructor(key) { if (key !== KEY) throw new TypeError('Illegal constructor'); super(); this.onmessage = null; hidden(this, '_peer', null); hidden(this, '_open', true); }
    postMessage(data) {
      var peer = this._peer;
      if (!peer || !this._open) return;
      setTimeout(function () {
        var ev = new MessageEvent('message', { data: data });
        if (typeof peer.onmessage === 'function') peer.onmessage.call(peer, ev);
        peer.dispatchEvent(ev);
      }, 0);
    }
    start() {}
    close() { this._open = false; }
  }
  class MessageChannel {
    constructor() {
      var a = new MessagePort(KEY), b = new MessagePort(KEY);
      a._peer = b; b._peer = a;
      this.port1 = a; this.port2 = b;
    }
  }
  if (!g.MessageChannel) { def(g, 'MessagePort', MessagePort); def(g, 'MessageChannel', MessageChannel); }
  if (!g.requestIdleCallback) {
    def(g, 'requestIdleCallback', function requestIdleCallback(f) {
      return setTimeout(function () { f({ didTimeout: false, timeRemaining: function () { return 10; } }); }, 1);
    });
    def(g, 'cancelIdleCallback', function cancelIdleCallback(id) { clearTimeout(id); });
  }
  // ---- Small objects scripts probe for
  if (!('isSecureContext' in g)) def(g, 'isSecureContext', location.protocol === 'https:');
  if (!g.open) def(g, 'open', function open() { return null; });
  if (!g.close) def(g, 'close', function close() {});
  if (!g.focus) { def(g, 'focus', function focus() {}); def(g, 'blur', function blur() {}); }
  if (!g.getSelection) {
    def(g, 'getSelection', function getSelection() {
      return { rangeCount: 0, isCollapsed: true, type: 'None', anchorNode: null, focusNode: null,
        toString: function () { return ''; }, removeAllRanges: function () {}, addRange: function () {}, getRangeAt: function () { return null; } };
    });
  }
  if (!g.PerformanceObserver) {
    class PerformanceObserver {
      constructor(f) { hidden(this, '_f', f); }
      observe() {}
      disconnect() {}
      takeRecords() { return []; }
    }
    PerformanceObserver.supportedEntryTypes = [];
    def(g, 'PerformanceObserver', PerformanceObserver);
  }
  if (!g.customElements) {
    var registry = new Map();
    def(g, 'customElements', {
      define: function (name, ctor) { registry.set(name, ctor); },
      get: function (name) { return registry.get(name); },
      whenDefined: function (name) { return Promise.resolve(registry.get(name)); },
      upgrade: function () {}
    });
  }
  [['ErrorEvent', 'error'], ['ProgressEvent', 'progress'], ['HashChangeEvent', 'hashchange'], ['PopStateEvent', 'popstate'],
    ['PointerEvent', 'pointer'], ['InputEvent', 'input'], ['TouchEvent', 'touch'], ['StorageEvent', 'storage']].forEach(function (p) {
    if (g[p[0]]) return;
    var Base = p[0] === 'PointerEvent' && g.MouseEvent ? g.MouseEvent : Event;
    var C = class extends Base {
      constructor(type, init) {
        super(type, init);
        if (init) Object.keys(init).forEach(function (k) { if (!(k in this)) hidden(this, k, init[k]); }, this);
      }
    };
    Object.defineProperty(C, 'name', { value: p[0] });
    def(g, p[0], C);
  });
  if (!g.DOMPointReadOnly) {
    class DOMPointReadOnly {
      constructor(x, y, z, w) {
        hidden(this, '_x', Number(x) || 0); hidden(this, '_y', Number(y) || 0);
        hidden(this, '_z', Number(z) || 0); hidden(this, '_w', w === undefined ? 1 : Number(w) || 0);
      }
      get x() { return this._x; }
      get y() { return this._y; }
      get z() { return this._z; }
      get w() { return this._w; }
      static fromPoint(p) { p = p || {}; return new this(p.x, p.y, p.z, p.w); }
      toJSON() { return { x: this.x, y: this.y, z: this.z, w: this.w }; }
    }
    class DOMPoint extends DOMPointReadOnly {
      get x() { return this._x; } set x(v) { this._x = Number(v) || 0; }
      get y() { return this._y; } set y(v) { this._y = Number(v) || 0; }
      get z() { return this._z; } set z(v) { this._z = Number(v) || 0; }
      get w() { return this._w; } set w(v) { this._w = Number(v) || 0; }
    }
    def(g, 'DOMPointReadOnly', DOMPointReadOnly);
    def(g, 'DOMPoint', DOMPoint);
  }
  if (typeof document !== 'undefined' && !('activeElement' in document)) {
    Object.defineProperty(document, 'activeElement', { get: function () { return document.body; }, configurable: true });
    def(document, 'hasFocus', function hasFocus() { return true; });
  }
})
