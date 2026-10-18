<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-49 / JSON stringify internal consumers and ownership

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)、ユーザー「マージしながら」「ws.md/Phase可・master.md不可」。q48 selection75d0bed66/source599508209/terminal3d9c40763/history211a05fb5caae755857caa081de4b52979b0d0d824ec09e247d0d82e28b9bb54 verified main。
Timebox: 最大90分、2026-10-18 22:57:07→2026-10-19 00:27:07 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-49-i01 | [p258](phase258/phase.md) JSON stringify partial | JSON.stringifyのvalue/replacer-array判定をactual IsArrayへ、object ownkeys/GetOwnをrealm APIへ接続。enumerable string keys全snapshotは全value Getより前。explicit PropertyListはown/enumerability filter無し。writer realm/replacer/list/root holder/key/cycle stackとproperty holder/key/fresh value/toJSON method/key args、keyvectors/property-list fresh elementsをcallback/GC中保持。primitive BigInt toJSONもGetし残存BigIntはTypeError。old690 prefix維持・新native/genuine order/error/GC対照、full changed C/host warning0/q02 literal json1 Ctarget warning0/focused/Acid/元WebIDL/全1508 q48比較 | in-progress | q39 IsArray/q43 OwnKeys/q47 nested/q48 prior consumers・actual VM call rooting |

Generation bounds: js/builtin_json.c json_stringify/json_property/json_property_list/json_serialize_objectと必要private trace/state/inner helpersのみ。json_parse/reviver/space/serialize_array等は変更しない。private tests/WS074 recordsのみ。VM source/headers/native_operations/ABI2/Wayland/WPT・runner・manifest・期待値/SDK/toolchain/sysroot/共有Boards/cache/master.md変更無し。
Design: writer tracerをscript前登録、cycle stackを含むinitialized VM fieldsのみtrace。property wrapper+innerで全exitのtemporary roots退役を保証。ownkeys nativevectorをtraceしstring-only descriptor全検査→compact→value Get。explicit listはrootされたnormalized listからcopy。list array判定のrevoked例外を伝播、list element/coerced stringをtrace。BigInt toJSON順は一次仕様SerializeJSONProperty、object kindによるProxy unwrap無し。
Partial limits: uint32/int32 length/storage幅・JSON_DEPTH_MAX/resource policy、JSON.parse/reviver所有/Proxy routing、RawJSON/callableProxy/global Proxy/Reflect/call/construct/newtarget/species/for-in/spread/primitive Set/outer instanceof/typed backing/immutable prototype/p100/full target/whole p258/WS100%は残件。
Standards: Guardrail/C全文/browser component全文直接適用、簡約無し。ANSI declarations first/semantic paragraphs/normal prototypes tables後、clang-format19 ColumnLimit0後必要形復元、full changed functions manual/checker許可single forward cleanup以外0。host build serialized、q02 literal json Ctarget -oだけ変更。一次 https://tc39.es/ecma262/multipage/structured-data.html#sec-json.stringify 、#sec-serializejsonproperty 、#sec-serializejsonobject 。
Verification bounds: protocol各30秒、新GC controlsのprecode crashは別processで隔離・raw保持。旧huge2 Chrome差/q36classifier filter資格維持、全pure genuine equalityとはしない。全1508eligible digest82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7 settle5000 process20、q48全whole/callback/completion比較。static844/scripted105/crash39はq28再利用・newpair全capture未測定。公式1482/3805 held0 未attempt275を維持、new gain実測無しで加算しない。
Integration: owned pathsだけWIP/main包含/競合確認、master編集無し。no agents/push/削除/make check/QEMU。共有投影pending Q1。
Dependency graph: q39/q43/q47/q48 verified outputs → i01、外部nodesは今回membership外。
Outlook（非承認）: parse/reviver/for-in/spread/primitive Set/call/construct/newtarget/global。
