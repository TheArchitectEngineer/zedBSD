<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-50 / JSON parse and reviver ownership and internal consumers

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)、追加ユーザー「マージしながら」「ws.md/Phase可・master.md不可」。q49 source2419e264c/terminal94aeee9a5/history9d71d361c4b8deef4a0642ef9c683b815b1ca8752e2f911ec1c3c9465903135b main包含確認。
Timebox: 最大90分、2026-10-18 23:13:14→2026-10-19 00:43:14 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-50-i01 | [p258](phase258/phase.md) JSON parse/reviver partial | parseのmanaged realm/coerced text/parsed root/reviver/root holder/keyとrecursive object/arrayのcontainer/name/key/memberを明示trace。reviver frameのholder/key/value/callback/child result/string keyとkeyvectorを保持、value array判定actual IsArray、object ownkeys/全enumerable string GetOwn snapshotを最初のchild callback前に完了。途中Proxyのlength/Get/Define/Delete/abrupt/revocation/拒否の規定順を維持。old726 prefix＋native/genuine/実GC対照、full changed C/host warning0/q02 exact json1target warning0/focused/Acid/元WebIDL/validator | in-progress | q49 stringify/rooting/q39 IsArray/q43 OwnKeys/q47 nested/actual Define Delete |

Generation bounds: js/builtin_json.c json_parse/json_object/json_array/json_internalizeと必要private wrapper/inner/parser/frame/reviver tracers/enumerable-key helperのみ。json_string/number/value/parse grammarとq49 stringify関数群は変更しない。private host-proxy-read.c mainのbridge登録＋新しいscript-free pressure allocation bridgeが必要なら追加（normal VM native function、native_operationsではない）、shared JS/tests/WS074 records。VM全source/header/native_operations/ABI2/Wayland/WPT・runner・manifest・期待値/SDK/toolchain/sysroot/共有Boards/cache/master.md変更無し。
Design: top parserは全trace slots初期化してinput coercion前tracer登録、coerced textを以後保持。object/arrayのwrapper+innerで未publish container/name/memberを再帰alloc中保持し全exit退役。reviver wrapper+innerは取得済valueと独立child resultをcallback中保持。actual IsArrayでarrayならlength snapshot/全index、ordinaryならactual OwnKeys→Symbols除外→全GetOwn→enumerable文字key snapshot→child recursion、native vectorをtrace。Delete/Define falseは仕様に従って無視し元throw/errnoは伝播。新native pressure bridgeは既存production threshold8MiBへinert allocationを積むだけ、product設定/GC policyを変えない。
Partial limits: newer JSON.parse source-context第三引数/parseRecord/source text access/RawJSON、uint32/int32幅/JSON_DEPTH_MAX/resource policy、for-in/spread/primitive Set/outer instanceof/callable/BoundFunction/call/construct/newtarget/Species/global Proxy/revocable/Reflect/typed backing/immutable prototype/p100/full target/whole p258/WS100%未達。parser全mid-parse threshold弱参照検証を新証拠無しに主張しない。
Standards: Guardrail/C全文/browser component全文直接、簡約無し。ANSI declaration groups/multiline functions/normal prototypes/public-static/semantic paragraphs、clang-format19 ColumnLimit0後必要形復元・full changed functions manual/checker許可single forward cleanup以外0。q02 literal json1Ctarget -oだけ変更、host serialized warning0、versions実取得、archives全member sequential readback。
Verification bounds: 各protocol30秒、precode GC入力は隔離しsignal/stdio/status保持。新条件genuine Chromium結果対照、native collectは実GC/C stack root無し。q36classifier filter/q43huge2差を保持し全pure genuine equalityとしない。focused各300秒、full1508最新q49 356.7秒q48全callback/completion同一を再利用、本pair全census未測定を明記。images/crash q28再利用。公式1482/3805 held0 未attempt275維持、未実測new gainを加算しない。known failure/未達は修復またはuncleared、criteriaを縮小しない。
Integration: owned paths WIP＋main包含/競合確認、WS/Phaseだけ記録、master編集無し。no agents/push/削除/make check/QEMU。共有Master/Queue/cache/GitHub投影pending Q1。
Dependency graph: q39/q43/q47/q49 verified outputs→i01、外部nodesはmembership外。
Outlook（非承認）: for-in/spread/primitive Set/call/construct/newtarget→Species/global。
