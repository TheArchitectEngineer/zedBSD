<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-30 / managed Proxy read foundation

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこのfinite partial scopeへ適用。q29 terminal f68a2f96d/history SHA256004593e63dd19545d6dbaf85d8f43f1288bdcaa1b0c46716629462dc7bdb1da8 read-back。
Timebox: 最大90分、選定19:19:49 JST→20:49:49 JST。

| Attempt | Phase | Exact scope / criteria | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-30-i01 | [p258](phase258/phase.md) native managed read foundation partial | private VM managed Proxy record/factory/revokeとrealm-aware receiver保持getを実装。非callableかつ非Proxyのtarget object限定のprivate factory、target/handler/object type検証・GC trace・idempotent revoke、ordinary vm_getのdefault receiver維持、prototype上Proxyのactual get、trapのGetMethod/this/target/string-or-symbol key/receiver・null fallback/動的lookup・例外、非configurable data/accessor get invariantとSameValue、trap/getter中revoke後の元target/handler保持、managed realm/新born/元receiver/sole result/回収・bounded recursionをnative/Chrome independent対照で確認。script-free native property hooksを変更せず、通常vm_getからrealm-aware pathを使う。final source/full changed C・host warning0/target exact object/access/realm3source warning0、全1508 old whole/callback PASS保持・DOM/VM/factory/producer/observer/error/Acidを確認しraw/hash保存 | in-progress | q29 VM/GC/Windowとoriginal Proxy blocker、C全文・native_operationsのnoscript契約、TC39 Proxy Get算法 |

Dependency graph: q29 verified ordinary VM/GC → managed noncallable/nonProxy record・receiver Get → native/oracle/GC/full1508 gates → partial result/archive。将来のset/descriptor/prototype/call/construct/JS constructor/revocable/Array.isArray等は次finite scopeで選ぶ。
Generation bounds: 既存engine vm/object.c/access.c/realm.c/internal.h/vm.h、tests/host-proxy-read.c、必要な対照sourceを置くtests/proxy-read.jsとWS074記録だけ。既存vm_native_operationsはheap-only/noscriptのまま、ProxyのJS trapはrealm-aware access層だけで呼ぶ。new module/ABI2/Wayland/JS global Proxy/runner/WPT/manifest/既存期待値/SDK/sysroot/toolchain/共有Board/cacheを変更しない。
Exposure bounds: これはprivate VM primitiveのread-only stageで、global Proxy/JS constructorはまだ公開しない。host fixtureだけがnative factoryへtest bridgeし、Chromeではgenuine native Proxy/revocableへ同じcontrol sourceを通す。未実装のwrite/own descriptor/own keys/prototype/IsArray/call/constructをordinary Objectとして誤認するglobal APIを出さず、whole p258/正規WebIDL2 parseは未達に保つ。factoryはこのstageではcallable/Proxy targetを明示拒否し、将来受け入れるscopeを保存する。
Standards: Guardrail/C全文/browser component全文を直接適用、簡約版無し。new/changed function/struct/table/helper declarationsのfull manualとformat19/checker、許可cleanup flagsはrawと別記。targetはq02 exact既存3 commandで-oのみ変更、host build単独完了確認しunique final pair固定。全1508はbudget20/settle5000で直列、画像/strictはq29と同じくq28原証拠を再利用しnew pair全capture一致と主張しない。
Bounds: original receiver/GC/revoke/invariant/old PASSの未達、未解消後退または90分でunclearedとresume。Proxy recursionはrealm-owned counterで有界化しnormal VM depthを別用途へ流用しない。未知APIをJSで補完せず原test bytes/denominator3805/未attempt275/held0を保持。push/削除/make check/QEMU/full target link/runtime/p100無し。
Upcoming Work Outlook: verified read foundation→set/descriptor/prototype等のnative Proxy internal methods→正規global constructor/revocable公開→original WebIDL2再評価、layout/API/未attempt275/target/p100。

