<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-51 / CopyDataProperties and object rest consumers

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)、追加「マージしながら」「ws.md/Phase可・master.md不可」。q50 source9a6b5916f/terminal69373a690/historyb0452f67332297238ed8961d3054069061fc014abf3a165e36e89142fe911231 main包含確認。
Timebox: 最大90分、2026-10-18 23:23:24→2026-10-19 00:53:24 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-51-i01 | [p258](phase258/phase.md) spread/rest partial | spread_copyをactual OwnKeys→各keyのexcluded判定→actual GetOwn→enumerableならGet→CreateDataPropertyOrThrow相当actual Defineへ接続。Symbolも列挙、除外keyはGetOwn以前skip、descriptor/value順をinterleaveしsnapshot全descriptor化しない。native keyvectorを収集前からtraceしfresh未訪問Symbol/converted source/getter result/target/managed realm/excludedを保持。vm_object_restはallocation前にsource/excluded/realm/output placeholderをroot。old752 prefix＋native/genuine/GC/order/例外/ordinary spread/rest対照、native direct copy EACCES/Define false＋actualGC/fresh getter payload/owner保持→回収、full changed C/host warning0/q02 exact spread1target warning0/focused/Acid/元WebIDL/validator | in-progress | q43 OwnKeys/q44Object/q47nested/q50latest actual Get/GetOwn/Define・VM call所有 |

Generation bounds: vm/spread.c spread_copy/vm_object_restと必要private keyvector tracer/stateのみ、selected normal helpers宣言を全table後、top2管理comment参照を除去。他iteration/call helpersとspread_is_excludedのbehaviorは変更しない。private host-proxy-read.c mainとdirect-copy native fixture/normal fresh-value getter/pure define policy/traced observation用必要private helpers、shared JS/tests/WS074 records。access.c/VM headers/native_operations定義/comment/ABI2/Wayland/WPT・runner・manifest・期待値/SDK/toolchain/sysroot/共有Boards/cache/master.md変更無し。
Design: keyvectorを空で初期化してtracer登録後actual ownkeys、余分なprivate key Arrayを廃して全initialized nativekeysをtrace。per keyはexclusion→descriptor→Get→default data Define、falseをTypeError・negative/errnoは保存。fresh valueはtarget Defineと拒否error allocationまでroot。rest wrapperは元inputrootをallocation前取得、fresh resultをplaceholderrootに入れてcopyし全exit退役。native direct API fixtureは保守C stack/embedding setup roots無しでmanaged caller/target/source/fresh getter outputのactual collection保持、policyerrno/refusalは成功化しない。pure native hooksはscriptを実行しない。
Partial limits: for-inはlazy own-key/prototype/visited stateを別Queueで設計し今回変更しない。private excluded arrayの既存ToKey/raw length契約、uint32/int32幅/一般resource policy/newer JSON source-context/RawJSON/primitive Set/outer instanceof/callable/bound/call/construct/newtarget/Species/global Proxy/revocable/Reflect/typed backing/immutable prototype/p100/full target/whole p258/WS100%未達。Symbol shape-cache保持の既知資格を継承し、stored Symbol全回収を主張しない。
Standards: Guardrail/C全文/browser component全文直接適用、簡約無し。ANSI declarations first/multiline/public-static/semantic paragraphs/normal helpers tables後、clang-format19 ColumnLimit0後必要形復元、full changed functions manual/checker許可single forward cleanup以外0。host serialized、q02 literal spread1C -oだけ変更、versions/raw全member sequential readback。一次 https://tc39.es/ecma262/multipage/abstract-operations.html#sec-copydataproperties 。
Verification bounds: protocol各30秒、GC old counterpartsは隔離・signal/stdio/status保持。新shared rows genuine Chromium public spread/restと対照、native bridge collect実GC/Chrome no-opを区別。q36classifier filter/q43huge2差を維持。focused各300秒、latest全1508 q49 356.7秒（q48全callback/completion同一）再利用、q51pair全census未測定を明記。static844/scripted105/crash39 q28再利用・newpair全capture未測定。公式1482/3805 held0 未attempt275維持、新gain未実測で加算しない。
Integration: owned WIP/main包含・競合確認、ws/Phaseだけ記録・master.md編集無し。no agents/push/削除/make check/QEMU。共有Master/Queue/cache/GitHub投影pending Q1。
Dependency graph: q43/q44/q47/q50 verified outputs→i01、外部nodesはmembership外。
Outlook（非承認）: for-in/primitive Set/call/construct/newtarget→Species/global。
