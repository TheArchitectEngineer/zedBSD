(function(){
 var count=0,failed=0;
 function check(name,fn){count++;var ok=false;try{ok=fn()===true;}catch(e){}if(!ok)failed++;console.log('read-proxy '+name+': '+(ok?'PASS':'FAIL'));}
 function raises(fn,kind){try{fn();}catch(e){return e instanceof kind;}return false;}
 check('ordinary-data-fallback',function(){var t={x:7},p=makeReadProxy(t,{});return p.x===7&&p.missing===undefined;});
 check('ordinary-getter-receiver',function(){var p,t={get x(){return this===p;}};p=makeReadProxy(t,{});return p.x===true;});
 check('ordinary-inherited-getter-receiver',function(){var p,proto={get x(){return this===p;}};p=makeReadProxy(Object.create(proto),{});return p.x;});
 check('proxy-prototype-trap-receiver',function(){var child,t={},p=makeReadProxy(t,{get:function(target,key,receiver){return target===t&&key==='x'&&receiver===child;}});child=Object.create(p);return child.x;});
 check('proxy-prototype-default-receiver',function(){var child,t={get x(){return this===child;}},p=makeReadProxy(t,{});child=Object.create(p);return child.x;});
 check('trap-handler-and-three-arguments',function(){var t={},h={get:function(target,key,receiver){return this===h&&arguments.length===3&&target===t&&key==='x'&&receiver===p;}},p=makeReadProxy(t,h);return p.x;});
 check('numeric-key-is-string',function(){var p=makeReadProxy({},{get:function(t,k){return typeof k==='string'&&k==='0';}});return p[0];});
 check('symbol-key-preserved',function(){var key=Symbol('actual-key'),p=makeReadProxy({},{get:function(t,k){return k===key;}});return p[key];});
 check('explicit-object-receiver',function(){var r={},p=makeReadProxy({},{get:function(t,k,receiver){return receiver===r;}});return getReadReceiver(p,'x',r);});
 check('explicit-primitive-receiver',function(){var p=makeReadProxy({},{get:function(t,k,r){return r===null;}});return getReadReceiver(p,'x',null);});
 check('strict-getter-primitive-receiver',function(){var t={get x(){'use strict';return this;}};return getReadReceiver(makeReadProxy(t,{}),'x',false)===false;});
 check('dynamic-trap-getter',function(){var gets=0,h={get get(){gets++;return function(){return gets;};}},p=makeReadProxy({},h);return p.x===1&&p.x===2&&gets===2;});
 check('null-trap-falls-back',function(){return makeReadProxy({x:4},{get:null}).x===4;});
 check('noncallable-trap',function(){return raises(function(){makeReadProxy({},{get:false}).x;},TypeError);});
 check('trap-getter-error-identity',function(){var error={},p=makeReadProxy({},{get get(){throw error;}});try{p.x;}catch(e){return e===error;}return false;});
 check('trap-error-identity',function(){var error={},p=makeReadProxy({},{get:function(){throw error;}});try{p.x;}catch(e){return e===error;}return false;});
 check('fixed-data-invariant',function(){var t={};Object.defineProperty(t,'x',{value:1,writable:false,configurable:false});return raises(function(){makeReadProxy(t,{get:function(){return 2;}}).x;},TypeError);});
 check('fixed-data-same-value-nan',function(){var t={};Object.defineProperty(t,'x',{value:NaN});return Number.isNaN(makeReadProxy(t,{get:function(){return NaN;}}).x);});
 check('fixed-data-signed-zero',function(){var t={};Object.defineProperty(t,'x',{value:-0});return raises(function(){makeReadProxy(t,{get:function(){return 0;}}).x;},TypeError);});
 check('fixed-data-object-identity',function(){var value={},t={};Object.defineProperty(t,'x',{value:value});return makeReadProxy(t,{get:function(){return value;}}).x===value&&raises(function(){makeReadProxy(t,{get:function(){return {};}}).x;},TypeError);});
 check('fixed-getterless-invariant',function(){var t={};Object.defineProperty(t,'x',{set:function(v){},configurable:false});return makeReadProxy(t,{get:function(){return undefined;}}).x===undefined&&raises(function(){makeReadProxy(t,{get:function(){return 2;}}).x;},TypeError);});
 check('configurable-getterless-flexible',function(){var t={};Object.defineProperty(t,'x',{set:function(v){},configurable:true});return makeReadProxy(t,{get:function(){return 5;}}).x===5;});
 check('writable-fixed-data-flexible',function(){var t={};Object.defineProperty(t,'x',{value:1,writable:true,configurable:false});return makeReadProxy(t,{get:function(){return 2;}}).x===2;});
 check('invariant-observes-post-trap-target',function(){var t={},p=makeReadProxy(t,{get:function(){Object.defineProperty(t,'x',{value:1,writable:false,configurable:false});return 2;}});return raises(function(){p.x;},TypeError);});
 check('revoke-is-idempotent-and-stops-traps',function(){var calls=0,p=makeReadProxy({},{get:function(){calls++;}});revokeReadProxy(p);revokeReadProxy(p);return raises(function(){p.x;},TypeError)&&calls===0;});
 check('revoke-in-trap-getter-default-continues',function(){var p,t={x:27},h={get get(){revokeReadProxy(p);t=null;h=null;collectReadProxy();return undefined;}};p=makeReadProxy(t,h);var value=p.x;return value===27&&raises(function(){p.x;},TypeError);});
 check('revoke-in-trap-invariant-keeps-target',function(){var p,t={};Object.defineProperty(t,'x',{value:1});p=makeReadProxy(t,{get:function(){revokeReadProxy(p);t=null;collectReadProxy();return 2;}});return raises(function(){p.x;},TypeError);});
 check('array-target-index-and-length',function(){var p=makeReadProxy([3,4],{});return p[0]===3&&p[1]===4&&p.length===2;});
 check('gc-in-trap-getter-and-trap',function(){var p=makeReadProxy({x:29},{get get(){collectReadProxy();return function(t,k,r){collectReadProxy();return t[k];};}});return p.x===29;});
 check('gc-in-original-receiver-getter',function(){var receiver={},t={get x(){collectReadProxy();return this===receiver;}};return getReadReceiver(makeReadProxy(t,{}),'x',receiver);});
 check('invalid-target-and-handler',function(){return raises(function(){makeReadProxy(null,{});},TypeError)&&raises(function(){makeReadProxy({},null);},TypeError)&&raises(function(){makeReadProxy(1,{});},TypeError);});
 check('cyclic-native-fallback-bounded',function(){var t={},p=makeReadProxy(t,{});Object.setPrototypeOf(t,p);return raises(function(){p.missing;},RangeError)&&({x:31}).x===31;});
 console.log('read-proxy controls: '+(count-failed)+'/'+count);
 return failed===0;
})()
