<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-43 / native Proxy OwnPropertyKeys foundation

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q42 terminald554a67eb/source7a943134d/history SHA2567d1b4824a6eac055a2ec9c5da0af0dfb8369dc194a6a6a8a4360ae689a73f7b7 read-back。
Timebox: 最大90分、2026-10-18 21:37:07→23:07:07 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-43-i01 | [p258](phase258/phase.md) OwnPropertyKeys VM partial | vm_own_property_keys realm append APIとProxy ownKeys内部処理/private bridgeを追加。dynamic GetMethod/null fallback、handler this/元target1引数、CreateListFromArrayLike object/ToLength/全index順Get/string-symbol限定→全list後duplicates拒否→actual target IsExtensible→actual OwnKeys snapshot→全target GetOwn分類→fixed必須→nonext exact setという正規順。trap orderをpreserveしinternal numeric-string canonicalization/typed ordinary key orderは保持。owner/original participants/arraylike/current value/全key vectors/output prefixをreal GCから保持、fresh never-stored Symbolのaccumulated list/post-trap native own-keys GC/終了後実回収、native fallback/errnoとappend prefix/rollbackを検証。旧452 prefix＋native/genuine追加対照・host/q02 exact access1 target warning0/full changed C/focused gates/Acid/元WebIDL2保持 | in-progress | q40 actual extent、q33 GetOwn、q42 managed Proxy graph |

Generation bounds: vm/access.c（新public vm_own_property_keys、新pure vector tracer/array-like property-key list helper/Proxy helper/declarations）、vm/vm.h（宣言1だけ）、tests/proxy-read.js/host-proxy-read.c（private ownReadKeys bridge/weak Symbol observer/追加controls/GC native pure policy観察）、WS074 records。public Object own-key consumers/for-in/spread/JSON/integrity APIsへは今回接続しない。existing other VM/Proxy/typed policy・native_operations定義/comment/public ABI2/Wayland/global Proxy/Reflect/nested/callable factory bounds/WPT/runner/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: outputは既存prefixへsuccess時append、error時元countへrollback、prefix/途中keysもVM APIがtracerで保持。ordinaryは既存vm_object_own_keysへ委譲。Proxy helperは元target/handler/fresh method/result arraylikeをsnapshot-rootし、trap/target/fixed/configurableのC key vectorsを全GC期間trace。arraylikeはGet length→ToNumberで正規ToLength（NaN/negative0→0、floor/clamp2^53-1、BigInt/Symbol拒否）→index Get、各elementはstring/symbol以外即拒否しprimitive stringをVM keyへcanonicalize。全element評価が済んでからduplicates検査。extent snapshotをOwnKeys前に取得し、全target descriptorを先に分類した後だけ集合照合。fixedは必須、extensibleなら他省略/追加可、nonextはexact set・trap順を保持。malloc-vector pushはC allocationのみ、VM cell heap/query/ToLength/keysはscript/GCを許すrealm層に限定。native own_keys callbackはno user script契約を保持。public consumers/for-inは次finite scopeでrealm GetOwn等の呼出し側も同時に整える。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI declarations first・意味paragraph・public/static順・multiline fn・normal prototypes tables後/table-required例外維持。clang-format19 ColumnLimit0後fn/3+clause復元、full changed C/manual/checker許可forward cleanup以外0。host serialized終了確認・final source/pair/input固定、target q02 literal access commandの-oだけ変更。現versionsを明示保存。
Verification bounds: focused gatesのみ、all1508 q37/static/scripted/crash q28を明示再利用、新pair全census/capture同値は主張しない。product call sites変更無し・公式gain0・1482/3805・held0・未attempt275保持。huge ToLengthは最初のgetter/errorで有界対照し膨大loopを実行しない。raw全member read-back。90分未達はuncleared/resume。no agents/push/削除/make check/QEMU。
Dependency graph: q40 actual extent + q33 GetOwn/ordinary own keys → VM OwnPropertyKeys/internal list/GC/private controls → public Object/for-in consumers（未選定）→ other invariants/nested/call/construct/global。
Upcoming Work Outlook: own-key callers/other internal invariant realm化/nested/call/construct/global/revocable → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS100%未達を保持。
