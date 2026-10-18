<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-40 / Proxy extensibility operations

Status: active
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q39 terminal04e338080/sourcec84e12f08/history SHA256f8ebf90c7e8b32acbc638c627292293e0b37e9955321cb8fb609bacd31930c71 read-back。
Timebox: 最大90分、2026-10-18 21:16:06→22:46:06 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-40-i01 | [p258](phase258/phase.md) IsExtensible/PreventExtensions partial | vm_is_extensible/vm_prevent_extensions realm APIsとProxy内部処理を追加し公開Object.isExtensible/preventExtensionsを接続。dynamic GetMethod/null fallback、handler this/元target1引数、ToBoolean、revocation/reentrant depth、IsExtensible trap結果とpost-trap actual targetの一致、Prevent false時target確認無し/true時target nonextensible要求、native policy拒否/error保持を検証。旧336 source prefix保持＋追加native/genuine Chrome対照、managed owner/元target/handler/native policyのactual GC保持と後に実回収、host warning0/q02 exact access+builtin_object2 target warning0/full changed C/focused gates/Acid/元WebIDL2保持 | in-progress | q39 Proxy ownership/IsArray、q38公開前診断差 |

Generation bounds: vm/access.c（新public2 APIs/own newhelpers/declarationsだけ、existing Proxy Get/Has/GetOwn/Define/Setのextensibility samplingは未変更）、vm/vm.h（宣言だけ＋既存管理comment除去）、js/builtin_object.c（object_is_extensible/object_prevent_extensions wrappersだけ）、tests/proxy-read.js/host-proxy-read.c（追加条件/GC native no-script policy観察）、WS074 records。触れたsourceの旧WS/Phase/plan管理commentは技術説明を保持し除去。native_operations定義/comment/ABI、global Proxy/Reflect、nested/callable factory bounds、Object seal/freeze/isSealed/isFrozen、remaining Proxy operations、WPT/runner/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: public realm APIsはmanaged ownerとoriginal inputをroot登録後depthチェックし、ordinary IsExtensibleは既存flag、Preventは既存pure native policyへ委譲。Proxy helperは元target/handlerをsnapshot-rootしlookup/revoke後も保持、fresh methodを保持しactual trapへ渡す。IsExtensibleはtrapのToBooleanとrealm-aware target IsExtensibleを比較、Preventはfalse即done0/true時actual target nonextensibleを要求。native callbacksはuser script禁止の契約不変。nested target生成はまだ拒否し、既存other internal invariantsのrealm化とnested許可は別Queueへ残す。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI宣言first・意味paragraph・public-static順・fn multiline、通常prototypeはtables後/initializer必要例外だけ。clang-format19 ColumnLimit0後fn/3+clause復元、full changed fn review/checker raw許可forward cleanup以外0。host serialized終了・final source/pair hash固定、target q02 literal commandの-oだけ変更。
Verification bounds: focused gatesのみ。all1508 q37/static/scripted/crash q28再利用、newpair全census同値は主張しない。private global未公開で公開ordinary wrapper動作保持を確認。公式PASS加算0・1482/3805・held0・未attempt275保持。raw全member read-back。90分未達はuncleared/resume。no agents/push/削除/make check/QEMU。
Dependency graph: native ordinary policy + existing Proxy graph → new extent realm operations/public wrappers → trap/invariant/native GC/controls/gates → existing internal invariant realm化/nested（未選定）→remaining/global。
Upcoming Work Outlook: keys/delete/prototype/internal snapshot/nested/call/construct/global constructor/revocable → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS100%未達を保持。
