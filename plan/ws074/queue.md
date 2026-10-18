<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-48 / array classification and Object tag consumers

Status: active
Owner: このchat単独。Approval: [自走指示](autonomy.md)とユーザー追加「マージしながら」「ws.md/Phase可・master.md不可」を適用。q47 terminal53563fb51/source0fb5a4c85/history8cd894015623e589ecd43ba7a86638d75049b02929b8486a9d45c282123cfae6、main包含確認。統合指示record6e9201a44。
Timebox: 最大90分、2026-10-18 22:37:22→00:07:22 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-48-i01 | [p258](phase258/phase.md) IsArray consumer partial | Object.toStringをIsArray→builtinTag→actual @@toStringTag順へ、concatをIsConcatSpreadable（object判定→actual @@isConcatSpreadable Get→undefined時だけrealm IsArray）へ接続。flat/flatMap flattenはdepth>0時だけrealm IsArray、depth0でrevoked item検査しない。concat resultはfresh valuesをrootしCreateDataProperty相当own default define、holes保持、flat/flatMap boxed sourceを変換/mapper/recursion中保持。old658 prefix維持＋native/genuine/GC/order/例外/普通array対照、full changed C/host warning0/q02 exact2 target warning0/focused/Acid/元WebIDL/全1508 q46比較 | in-progress | q39 IsArray/q47 nested/q46 Set/actual Get Has Define・native VM call所有 |

Generation bounds: js/builtin_object.c object_to_stringのみ。js/builtin_array.c array_concat/array_flat/array_flat_map/array_flatten、old array_is_array_value helperをstatus＋bool outのarray_is_concat_spreadableへ置換・normal helper prototypes全tables後。必要private tests/WS074 records。VM source/headers/native_operations定義/comment/ABI2/Wayland/WPT・runner・manifest・期待値/SDK/toolchain/sysroot/共有Boards/cache/master.md変更無し。
Design: objectタグのIsArrayは@@tag Getより先でrevokedならGetterに到達せずthrow、nonarray Proxyはtarget internal brandを模倣しない。concatは@@spreadable overrideを尊重してから必要なIsArray、plain output definitionsはprototype setterを実行せずfresh valueをGC中保持。concatの登録を先初期化＋単一forward cleanupへ統合。flat/flatMapはarray_this後のboxed sourceを早期root、result placeholder登録後にallocation、flattenのmapped temporary rootを保持しdepth predicate先判定。新しいIsArray例外はstatus伝播、ordinary array/holes/mapper orderを保存。
Explicit partial limits: ArraySpeciesCreate/constructor/@@species/custom result/newtarget/callable Proxy、2^53長とuint32/int32既存storage幅・flatten recursion一般resource policyは本partial外でwhole acceptanceに残す。concat/flat全仕様適合とは主張しない。for-in/spread/JSON/primitive Set/outer instanceof/bound/call/construct/global/Reflect/upper-index storage/typed backing/immutable proto/p100/full target/WS未達も維持。
Standards: Guardrail/C全文/browser component全文直接適用（簡約無し）。ANSI declarations first/意味paragraph/multiline/public-static順、normal helpers全table後、initializer必須wrapper例外維持。clang-format19 ColumnLimit0後必要形復元、full changed C manual/checker許可single forward cleanup以外0。host serialized終了確認、q02 literal builtin_object/array2Ctarget -oだけ変更、versions取得、raw全member read-back。
Verification bounds: old658native658/Chrome656 q43huge2差/q36filter資格維持、新条件genuine対照、native collection bridgeは実GC・C stack root無し。各protocol30秒/focused300秒/全1508eligible digest82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7 settle5000 process20固定、q46全whole/callback/completion比較・old PASS loss/gain audit。普通arrayにも@@spreadable参照を追加するため全1508実測する。images/crash q28証拠再利用でnewpair全capture未測定。確認済1482/3805・held0・未attempt275維持。回帰/未達は修復かuncleared、受け入れは縮小しない。
Integration: main上のowned pathsだけWIP保存し各source/terminal SHAのmain包含/競合無し確認、必要な実branch統合は対象SHA固定。master.md/他担当branchは触らずWS/Phaseに記録。no agents/push/削除/make check/QEMU。共有投影pending。
Dependency graph: q39/q46/q47 verified outputs → i01。external nodesは今回membership外。
Outlook（非承認）: for-in/JSON/spread、primitive Set、call/construct/newtarget→species/global constructor/revocable。
