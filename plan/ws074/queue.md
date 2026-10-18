<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-40 / Proxy extensibility operations

Status: finished（i01 extent scoped partial cleared）
Owner: このCodex chat単独。
Approval: [自走指示](autonomy.md)をexact finite partialへ適用。q39 terminal04e338080/sourcec84e12f08/history SHA256f8ebf90c7e8b32acbc638c627292293e0b37e9955321cb8fb609bacd31930c71 read-back。
Timebox: 最大90分、2026-10-18 21:16:06→22:46:06 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-40-i01 | [p258](phase258/phase.md) IsExtensible/PreventExtensions partial | vm_is_extensible/vm_prevent_extensions realm APIsとProxy内部処理を追加し公開Object.isExtensible/preventExtensionsを接続。dynamic GetMethod/null fallback、handler this/元target1引数、ToBoolean、revocation/reentrant depth、IsExtensible trap結果とpost-trap actual targetの一致、Prevent false時target確認無し/true時target nonextensible要求、native policy拒否/error保持を検証。旧336 source prefix保持＋追加native/genuine Chrome対照、managed owner/元target/handler/native policyのactual GC保持と後に実回収、host warning0/q02 exact access+builtin_object2 target warning0/full changed C/focused gates/Acid/元WebIDL2保持 | cleared | q39 Proxy ownership/IsArray、q38公開前診断差 |

Generation bounds: vm/access.c（新public2 APIs/own newhelpers/declarationsだけ、existing Proxy Get/Has/GetOwn/Define/Setのextensibility samplingは未変更）、vm/vm.h（宣言だけ＋既存管理comment除去）、js/builtin_object.c（object_is_extensible/object_prevent_extensions wrappersだけ）、tests/proxy-read.js/host-proxy-read.c（追加条件/GC native no-script policy観察）、WS074 records。触れたsourceの旧WS/Phase/plan管理commentは技術説明を保持し除去。native_operations定義/comment/ABI、global Proxy/Reflect、nested/callable factory bounds、Object seal/freeze/isSealed/isFrozen、remaining Proxy operations、WPT/runner/manifest/expectations/SDK/toolchain/sysroot/shared Boards/cache変更無し。
Design: public realm APIsはmanaged ownerとoriginal inputをroot登録後depthチェックし、ordinary IsExtensibleは既存flag、Preventは既存pure native policyへ委譲。Proxy helperは元target/handlerをsnapshot-rootしlookup/revoke後も保持、fresh methodを保持しactual trapへ渡す。IsExtensibleはtrapのToBooleanとrealm-aware target IsExtensibleを比較、Preventはfalse即done0/true時actual target nonextensibleを要求。native callbacksはuser script禁止の契約不変。nested target生成はまだ拒否し、既存other internal invariantsのrealm化とnested許可は別Queueへ残す。
Standards: Guardrail/C全文/browser component全文直接適用。ANSI宣言first・意味paragraph・public-static順・fn multiline、通常prototypeはtables後/initializer必要例外だけ。clang-format19 ColumnLimit0後fn/3+clause復元、full changed fn review/checker raw許可forward cleanup以外0。host serialized終了・final source/pair hash固定、target q02 literal commandの-oだけ変更。
Verification bounds: focused gatesのみ。all1508 q37/static/scripted/crash q28再利用、newpair全census同値は主張しない。private global未公開で公開ordinary wrapper動作保持を確認。公式PASS加算0・1482/3805・held0・未attempt275保持。raw全member read-back。90分未達はuncleared/resume。no agents/push/削除/make check/QEMU。
Dependency graph: native ordinary policy + existing Proxy graph → new extent realm operations/public wrappers → trap/invariant/native GC/controls/gates → existing internal invariant realm化/nested（未選定）→remaining/global。
Upcoming Work Outlook: keys/delete/prototype/internal snapshot/nested/call/construct/global constructor/revocable → original WPT/未attempt275/API/layout/p100/full target。whole p258/WS100%未達を保持。

## Outcome

vm_is_extensible/vm_prevent_extensionsと共有realm frame/private Proxy helperを追加し、公開Object.isExtensible/preventExtensionsを接続。元target/handlerをsnapshot-rootしdynamic GetMethod/null fallback、handler this/元target1引数、fresh method保持、ToBoolean、lookup/trap中revoke・bounded depthを正規処理する。IsExtensibleはpost-trap actual target IsExtensibleとの一致を要求し、Prevent falseは即done0でtarget確認を省き、trueはactual target nonextensibleを要求。ordinaryは旧flag/既存pure native prevent policyを共有し、native拒否とerrnoを変換せず保持。native_operations定義/コメント/契約は全bytes不変。existing Get/Has/GetOwn/Define/Setのextensibility samplingはまだdirect flags、nested/callable factory bound/global非公開とObject seal/freeze/isSealed/isFrozenの残件を保持。
旧336 source prefix全bytes同一、追加36条件を含むnative/Chromium対照372通過。旧canonical分類のq36 negative-zero primitive filterを継承するので全372をpure genuine exotic一致とは主張しない。native C stack保守保持無し。4独立modeでbare managed ownerと元target/handler/Proxyの全setup rootsを落とし、handler getterのrevoke後script GC、さらに元getterがreturnした後のpure native prevention callback内GCを観察。native成功・拒否・EACCES、fresh IsExtensible script trapの各outcome/depth0を保持し、4 participantの生存を個別確認、frame終了後の全4実回収を確認。native callbackはcollectorとnative allowed/errnoだけでscriptを実行しない。旧IsArray error pressure/Proxy/descriptor/BigInt/typed graph GC対照も保持。
source WIP5eb0f6f11、final source/pair/input317 hash照合、host warning0・q02 exact access/builtin_object2 target warning0。全文/manual review・clang-format19・full changed Cの許可forward cleanup52/他checker0。選定functions/declarations以外のC tokens完全同一、管理WS/Phase/plan commentは触れたsourceから技術説明を保持して除去。DOM41、VM45/factory56、CSSOM12+12、marks7/observer10/ErrorEvent9/ErrorReporting19/delegation10通過。Acid3 score/inventory100・pixel0、Acid2既存873。元WebIDL2はq38同一source bytesと全callbackを保持し、baseline Proxy undefined/private adapter positive4/4・negative元assertion到達。
focused gatesのみ、all1508 q37/static/scripted/crash q28再利用を明示。q40全census/capture同値は未測定、公式gain0・1482/3805（38.948752%）・held0・未attempt275保持。i01 extent2 partialだけcleared、whole p258/WS/p100/full target/100%未達。次候補delete/keys/prototype/other invariant realm化/nested/call/construct/global/revocable、別array/typed残件を維持。共有Master/Queue/cache/GitHub投影pending、push無し。
調査中のsource boundary checkerはexisting prototypesをnew側だけ除去して初回false mismatchになったため、対称除去へ訂正し成功（product source変更無し）。archive初回はfull member read-back後に未作成prefix-proofのcopyで停止。初回archiveをbuildへ保存し不足proofを作成、再archive/read-back/copy完了。失敗と修復をrawに保持。
Final raw archive71 members全read-back、SHA256686b68a24cb0a90845e2c84621852b0fffbf29fb35f70c9301f68d7339311ea0。runtime binaryはhash付きbuildに保持しarchive未同梱。

[exact history](history/ws074-q20261018-40.md)。
